//
// Project 3
// Simple botnet server for TSAM-409
//
// Command line: ./tsamvgroup69 <port>
//
// Group: 69
// Authors: Auðunn Hrafn Alexandersson & Gunnlaugur Guðmundsson

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <vector>

#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <map>

#include <unistd.h>

using namespace std;

// fix SOCK_NONBLOCK for OSX
#ifndef SOCK_NONBLOCK
#include <fcntl.h>
#define SOCK_NONBLOCK O_NONBLOCK
#endif

#define BACKLOG 5 // Allowed length of queue of waiting connections

char local_ip[15];                              // Local ip address
const char *local_server_port;                  // Local server port
const string local_group_name = "P3_GROUP_69";  // Server/group name
time_t keepaliveTimer = time(0);                // Keepalive timer
const int MINUTE = 60;                          // Time for how often message can be sent
struct timeval waitTime = {60};                 // Waiting time for each select
int n;                                          // number of sockets
int maxfds;                                     // Passed to select() as max fd in set
fd_set openSockets;                             // Current open sockets

/* 
*  Simple class for handling connections from clients.
*  Client(int socket) - socket to send/receive traffic from client.
*/
class Client
{
public:
    int sock;    // socket of client connection
    string name; // Limit length of name of client's user

    Client(int socket) : sock(socket) {}

    ~Client() {} // Virtual destructor defined for base class
};
/*
*  Simple class for handling Servers.
*  Socket - Server socket
*  name - Name of the server
*  ip - Ip address of server
*  port - Port server is connected on
*  keepalivetime - Time that last keepalive was sent
*  listserverssent - Initial listservers sent, for sending it only two times.
*/
class Server
{
public:
    int sock;              
    string name;            
    string ip;              
    string port;            
    time_t keepAliveTime;
    bool listserversSent = false;

    Server(int socket) : sock(socket) {}

    Server(int socket, string name, string ip, string port) : sock(socket)
    {
        this->sock = socket;
        this->name = name;
        this->ip = ip;
        this->port = port;
        this->keepAliveTime = time(0);
    }
     
    ~Server() {} // Virtual destructor defined for base class
};


map<int, Client *> clients;              // Lookup table for per Client information
map<int, Server *> servers;              // Lookup table for per Server information
map<string, vector<string>> outgoingMsg; // Lookup tables for outgoing msg <to_group_id, msg>

int open_socket(int portno);                                                            // Opens the socket
void closeClient(int clientSocket, fd_set *openSockets, int *maxfds);                   // Closes the client connection
void closeServer(int serverSocket, fd_set *openSockets, int *maxfds);                   // Closes the server connection
void clientCommand(int clientSocket, fd_set *openSockets, int *maxfds, char *buffer);   // Client commands actions
void serverCommand(int serverSocket, fd_set *openSockets, int *maxfds, char *buffer);   // Server commands actions
void connectToBotServer(const char *ip, const char *port);                              // Connecting to another server
void destuffHex(string &s);                                                             // Removes SOH and EOT from string
void stuffHex(string &s);                                                               // Adds SOH and EOT to string
void get_local_ip ( char * buffer);                                                     // Gets the local Ip address of user
void sendKeepalive(fd_set &openSockets, fd_set &readSockets, int *maxfds);              // Sends Keepalive to server or kicks him out
string constructMsg(vector<string> tokens);                                             // Constructs message for SEND_MSG
vector<string> split(string s, string delim);                                           // Splits string on delimiter
inline void Logger(string logMsg, string path);                                         // Logs info into file
inline string getCurrentDateTime(string s);                                             // Gets date for logger



int main(int argc, char *argv[])
{
    bool finished;       // Used for while loop

    int clientListenSock; // Socket for CLIENT connections to server
    int serverListenSock; // Socket for SERVER connections to server
    int clientSock;       // Socket of connecting client
    int serverSock;       // Socket of connecting client

    fd_set readSockets;   // Socket list for select()
    fd_set exceptSockets; // Exception socket list

    // Client socket
    struct sockaddr_in client; 
    socklen_t clientLen;

    // Server socket
    struct sockaddr_in server;
    socklen_t serverLen;

    char buffer[5001]; // Buffer for reading from clients

    // Initial usage
    if (argc != 2)
    {
        printf("Usage: chat_server <ip port>\n");
        exit(0);
    }

    // Client port info
    string clientPort;

    // Client port input
    cout << "Port for client connection: ";
    cin >> clientPort;

    // Getting Ip address
    get_local_ip(local_ip);
    local_server_port = argv[1];

    // Setup socket for server to listen to CLIENTS
    clientListenSock = open_socket(atoi(clientPort.c_str()));
    printf("Listening for CLIENTS on port: %d\n", atoi(clientPort.c_str()));

    // Listen to client
    if (listen(clientListenSock, BACKLOG) < 0)
    {
        printf("Listen for CLIENTS failed on port %s\n", clientPort.c_str());
        exit(0);
    }
    else
    // Add listen socket to socket set we are monitoring
    {
        FD_ZERO(&openSockets);
        FD_SET(clientListenSock, &openSockets);
        maxfds = clientListenSock;
    }

    // Setup socket for server to listen to other SERVERS
    serverListenSock = open_socket(atoi(argv[1]));
    printf("Listening for SERVERS on port: %d\n", atoi(argv[1]));

    // Listen to server
    if (listen(serverListenSock, BACKLOG) < 0)
    {
        printf("Listen for SERVERS failed on port %s\n", argv[1]);
        exit(0);
    }
    else
    // Add listen socket to socket set we are monitoring
    {
        FD_SET(serverListenSock, &openSockets);
        maxfds = max(serverListenSock, maxfds);
    }

    finished = false;
    while (!finished)
    {
        // Get modifiable copy of readSockets
        readSockets = exceptSockets = openSockets;
        memset(buffer, 0, sizeof(buffer));

        // Look at sockets and see which ones have something to be read()
        n = select(maxfds + 1, &readSockets, NULL, &exceptSockets, &waitTime);

        // If select failed
        if (n < 0)
        {
            perror("select failed - closing down\n");
            finished = true;
        }
        else
        {
            // Check for Keepalive every 60 seconds
            if(difftime(time(0), keepaliveTimer) > MINUTE) 
            {
                sendKeepalive(openSockets, readSockets, &maxfds);
            }
            // First, accept  any new connections to the server on the listening socket
            if (FD_ISSET(clientListenSock, &readSockets))
            {
                // Accepting the client socket
                clientSock = accept(clientListenSock, (struct sockaddr *)&client, &clientLen);
                printf("accept CLIENT***\n");
                // Add new client to the list of open sockets
                FD_SET(clientSock, &openSockets);
                // And update the maximum file descriptor
                maxfds = max(maxfds, clientSock);
                // create a new client to store information.
                clients[clientSock] = new Client(clientSock);
                // Decrement the number of sockets waiting to be dealt with
                n--;
                printf("Client has connected \n");
            }
            // same as if statement above but for Servers
            if (FD_ISSET(serverListenSock, &readSockets))
            {
                // Accepting the server socket
                serverSock = accept(serverListenSock, (struct sockaddr *)&server, &serverLen);
                printf("accept SERVER***\n");
                // Add a new server to the list of open sockets
                FD_SET(serverSock, &openSockets);
                // update the maximum file descriptor
                maxfds = max(maxfds, serverSock);
                // create a new server to store information
                servers[serverSock] = new Server(serverSock);
                n--;
                printf("A new server has connected! \n");
                // The message we are sending
                string msg = "LISTSERVERS," + local_group_name;
                // Adding end and front hexes
                stuffHex(msg); 
                // Sending LISTSERVERS and our local name to the server connecting
                if (send(serverSock, msg.c_str(), msg.length(), 0) < 0)
                {
                    perror("Failed to send LISTSERVERS to new server");
                }
                else{
                    // Logging LISTSERVERS if it gets sent
                    Logger(msg, "Sent");
                }
            }
            // Now check for commands from clients and servers
            while (n-- > 0)
            {
                vector<int> socksToClose; // sockets to close after each for loops, this way we don't get segfault!

                // Looping through clients
                for (auto const &pair : clients)
                {
                    Client *client = pair.second;

                    if (FD_ISSET(client->sock, &readSockets))
                    {
                        // recv() == 0 means client has closed connection
                        if (recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0)
                        {
                            socksToClose.push_back(client->sock);
                        }
                        // We don't check for -1 (nothing received) because select()
                        // only triggers if there is something on the socket for us.
                        else
                        {
                            cout << buffer << endl;
                            // Calling the client command
                            clientCommand(client->sock, &openSockets, &maxfds, buffer);
                        }
                    }
                }
                // close connections if any
                for (int i : socksToClose)
                {
                    printf("Client closed connection: %d", i);
                    close(i);
                    closeClient(i, &openSockets, &maxfds);
                }
                socksToClose.clear();

                // same as above but for servers
                for (auto const &pair : servers)
                {
                    Server *server = pair.second;

                    if (FD_ISSET(server->sock, &readSockets))
                    {
                        if (recv(server->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0)
                        {
                            socksToClose.push_back(server->sock);
                        }
                        else
                        {
                            // A message recived from another server
                            cout << "From : " << server->name << " ----------------" << endl;
                            cout << buffer << endl;
                            // Logging message in recive
                            Logger(buffer, "Recived");
                            serverCommand(server->sock, &openSockets, &maxfds, buffer);
                            cout << "---------------------" << endl;
                            cout << endl;
                        }
                    }
                }
                // closing sockets
                for (int i : socksToClose)
                {
                    printf("External server closed connection: %d", i);
                    close(i);
                    closeServer(i, &openSockets, &maxfds);
                }
                socksToClose.clear();
            }
        }
    }
}

// Open socket for specified port.
//
// Returns -1 if unable to create the socket for any reason.

int open_socket(int portno)
{
    struct sockaddr_in sk_addr; // address settings for bind()
    int sock;                   // socket opened for this port
    int set = 1;                // for setsockopt

    // Create socket for connection. Set to be non-blocking, so recv will
    // return immediately if there isn't anything waiting to be read.
#ifdef __APPLE__
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Failed to open socket");
        return (-1);
    }
#else
    if ((sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
    {
        perror("Failed to open socket");
        return (-1);
    }
#endif

    // Turn on SO_REUSEADDR to allow socket to be quickly reused after
    // program exit.

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
    {
        perror("Failed to set SO_REUSEADDR:");
    }
    set = 1;
#ifdef __APPLE__
    if (setsockopt(sock, SOL_SOCKET, SOCK_NONBLOCK, &set, sizeof(set)) < 0)
    {
        perror("Failed to set SOCK_NOBBLOCK");
    }
#endif
    memset(&sk_addr, 0, sizeof(sk_addr));

    sk_addr.sin_family = AF_INET;
    sk_addr.sin_addr.s_addr = INADDR_ANY;
    sk_addr.sin_port = htons(portno);

    // Bind to socket to listen for connections from clients

    if (::bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0)
    {
        perror("Failed to bind to socket:");
        return (-1);
    }
    else
    {
        return (sock);
    }
}

// Close a client's connection, remove it from the client list, and
// tidy up select sockets afterwards.
void closeClient(int clientSocket, fd_set *openSockets, int *maxfds)
{
    // Remove client from the clients list
    clients.erase(clientSocket);

    // If this client's socket is maxfds then the next lowest
    // one has to be determined. Socket fd's can be reused by the Kernel,
    // so there aren't any nice ways to do this.
    if (*maxfds == clientSocket)
    {
        for (auto const &p : clients)
        {
            *maxfds = max(*maxfds, p.second->sock);
        }
    }
    // And remove from the list of open sockets.
    FD_CLR(clientSocket, openSockets);
}

// Splits string with the delimiter given
// src: https://stackoverflow.com/questions/14265581/parse-split-a-string-in-c-using-string-delimiter-standard-c
vector<string> split (string s, string delim) {
    size_t pos_start = 0, pos_end, delim_len = delim.length();
    string token;
    vector<string> res;

    while ((pos_end = s.find (delim, pos_start)) != string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (s.substr (pos_start));
    return res;
}

// Closes a Server connection
// Exactly same as closeClient() but for servers
void closeServer(int serverSocket, fd_set *openSockets, int *maxfds)
{
    servers.erase(serverSocket);
    if (*maxfds == serverSocket)
    {
        for (auto const &p : servers)
        {
            *maxfds = max(*maxfds, p.second->sock);
        }
    }
    FD_CLR(serverSocket, openSockets);
}

// Process command from client on the server
void clientCommand(int clientSocket, fd_set *openSockets, int *maxfds, char *buffer)
{
    vector<string> tokens;
    string token;

    // Split command from client into tokens for parsing
    stringstream stream(buffer);

    while (stream >> token)
    {
        tokens.push_back(token);
    }

    // CONNECT - Connecting to a server with ip and port
    if ((tokens[0].compare("CONNECT") == 0) && (tokens.size() == 3))
    {
        connectToBotServer(tokens[1].c_str(), tokens[2].c_str());
    }
    // LISTSERVERS - Printing out servers the server is connected to
    else if (tokens[0].compare("LISTSERVERS") == 0)
    {
        string msg;
        // Setting our server in the first servers
        msg += "SERVERS," + local_group_name + "," + local_ip + "," + local_server_port + ";";
        // Looping through every server we are connected to
        for (auto const &server : servers)
        {
                msg += server.second->name + "," + server.second->ip + "," + server.second->port + ";";
        }
        // Sending and logging listservers
        if(send(clientSocket, msg.c_str(), msg.length(), 0) < 0){
            perror("Failed to send Listservers");
        } else {
            Logger(msg, "Sent");
        }
    }
    // Sending message to server
    else if ((tokens[0].compare("SENDMSG") == 0)) 
    {
        bool groupfound = false;
        // Check if the server is connected to us then send him the message
        for (auto const& serv: servers) 
        {
            Server *server = serv.second;
            if (server->name == tokens[1])
            {
                groupfound = true;
                // Putting the message back together
                string msg = constructMsg(tokens);
                // Sending and logging
                if(send(server->sock, msg.c_str(), msg.length(), 0) < 0) {
                    perror("Failed to SEND_MSG to server");
                }
                else{
                    Logger(msg, "Sent");
                }
            }
        }
        // Server is not connected to us then store the message for him to get it later.
        if (!groupfound) {
            cout << "Group ID not found, setting message to outgoingMessages" << endl;
            string msg;
            for (int i = 2; i < tokens.size(); i++) {
                msg += tokens[i] + " ";
            }
            outgoingMsg[tokens[1]].push_back(msg);
        }
    }
    // GETMSG - Get messages that are stored for group passed in
    else if ((tokens[0].compare("GETMSG") == 0) && tokens.size() == 2) 
    {
        // find the server with the group_id in the server
        for (auto const& serv: servers) 
        {
            Server *server = serv.second;
            string msg = "GET_MSG," + tokens[1];
            stuffHex(msg);
            // Sending GET_MSG and loggin
            if(send(server->sock, msg.c_str(), msg.length(), 0) < 0) {
                perror("Failed to GETMSG to server");
            }
            else{
                Logger(msg, "Sent");
            }
        }
    }
    // Command is not found
    else
    {
        cout << "Unknown command from client:" << buffer << endl;
    }
}

// Same as clientCommand but for server
void serverCommand(int serverSocket, fd_set *openSockets, int *maxfds, char *buffer)
{
    // parse commands by prefix and suffix (\x01 and \x04)
    vector<string> commands;
    string command;
    stringstream commandsStream(buffer);
    // Getting each command until we get EOT
    while (getline(commandsStream, command, '\x04'))
    {
        commands.push_back(string(command));
    }

    // Activity on the socket, keep it alive.
    servers[serverSocket]->keepAliveTime = time(0);

    // Looping through each command
    for (string command : commands)
    {
        destuffHex(command); // Remove SOH
        stringstream cStream(command);
        string c;
        getline(cStream, c, ';'); // used to extract first servers on list, ; assuming only appears on SERVERS command
        vector<string> tokens;
        string token;
        stringstream stream(c);
        // Parsing tokens with ,
        while (getline(stream, token, ','))
        {
            tokens.push_back(string(token));
        }
        // LISTSERVERS - Listservers creates a message reply with SERVERS
        if ((tokens[0].compare("LISTSERVERS") == 0) && (tokens.size() == 2))
        {
            string msg;
            // If the listservers has already be sent
            if(!servers[serverSocket]->listserversSent) {
                msg += "SERVERS," + local_group_name + "," + local_ip + "," + local_server_port + ";";
                // Go through all of the servers and add them into message
                for (auto const &server : servers)
                {
                    Server *s = server.second;
                    if (s->name != "" && s->ip != "" && s->port != "")
                    {
                        msg += server.second->name + "," + server.second->ip + "," + server.second->port + ";";
                    }
                }
                // Adding SOH and EOT
                stuffHex(msg);

                // Sending SERVERS
                if ((send(serverSocket, msg.c_str(), msg.length(), 0)) < 0)
                {
                    perror("Failed to send SERVERS");
                }
                string newCommand = "LISTSERVERS," + local_group_name;
                stuffHex(newCommand);
                // Sending LISTSERVERS again to make sure our server will go to the other server
                if ((send(serverSocket, newCommand.c_str(), newCommand.length(), 0)) < 0)
                {
                    perror("Failed to send LISTSERVERS again");
                }
                // Listservers has been sent twice
                servers[serverSocket]->listserversSent = true;
            }
        }
        // SERVERS - Parsing server command, adding to server
        else if (tokens[0].compare("SERVERS") == 0)
        {
            Server *s = servers[serverSocket];
            if (s->name == "" && s->ip == "" && s->port == "")
            {
                s->name = tokens[1];
                s->ip = tokens[2];
                s->port = tokens[3];
            }

        }
        // KEEPALIVE - Resetting the keepalive of server
        else if ((tokens[0].compare("KEEPALIVE") == 0) && tokens.size() == 2) 
        {
            servers[serverSocket]->keepAliveTime = time(0); // Resetting keepalive time
        }
        // GET_MSG - Getting message for group
        else if ((tokens[0].compare("GET_MSG") == 0) && tokens.size() == 2)
        {
            if (outgoingMsg.size() != 0) {
                // send all messages that have the tokens[1] as key on the outgoingMsg loookup
                for (auto const& outgoingMessage : outgoingMsg[tokens[1]]) {
                    // Creating send message of the message
                    string msg = "SEND_MSG," + local_group_name + "," + tokens[1] + "," + outgoingMessage;
                    stuffHex(msg);

                    if ((send(serverSocket, msg.c_str(), msg.length(), 0)) < 0)
                    {
                        perror("Failed to send SEND_MSG");
                    }
                    else {
                        Logger(msg,"Sent");
                    }
                }
                // delete messages that have been sent
                outgoingMsg.erase(tokens[1]);
            }
        }
        // SEND_MSG - Print the message we get, only from 1-hop servers so it has to be to us.
        else if ((tokens[0].compare("SEND_MSG") == 0))
        {
            cout << "Message recived from" << token[1] << ": ";
            for(int i = 3; i < tokens.size() - 1; i++){
                cout << tokens[i] << " ";
            }
            cout << endl;
        }
        // LEAVE - server leaves from our server map
        else if ((tokens[0].compare("LEAVE") == 0) && tokens.size() == 3)
        {
            for(auto const& serv : servers) {
                Server* server = serv.second;
                if (server->ip == tokens[1] && server->port == tokens[2]) 
                {
                    // closes server connection
                    closeServer(server->sock, openSockets, maxfds);
                }
            }
        }
        // STATUSREQ - Sends back
        else if ((tokens[0].compare("STATUSREQ") == 0) && tokens.size() == 2)
        {
            string message = "STATUSRESP," + local_group_name;;
            for (auto const& msg : outgoingMsg)
            {
                message += "," + msg.first + "," + to_string(msg.second.size()) + ";";
            }
            stuffHex(message);
            if(send(servers[serverSocket]->sock, message.c_str(), message.size(), 0) < 0){
                perror("Sending STATUSRESP failed");
            }
            else 
            {
                Logger(message, "Sent");
            }
        }
        // Unknown command
        else
        {
            cout << "Unknown command from server: " << buffer << endl;
        }
    }
}

// Connecting server to your server
// Code very similar to code from client.cpp
void connectToBotServer(const char *ip, const char *port)
{
    struct addrinfo hints, *svr;  // Network host entry for server
    struct sockaddr_in serv_addr; // Socket address for server
    int serverSocket;             // Socket used for server
    int set = 1;                  // Toggle for setsockopt

    hints.ai_family = AF_INET; // IPv4 only addresses
    hints.ai_socktype = SOCK_STREAM;

    memset(&hints, 0, sizeof(hints));

    if (getaddrinfo(ip, port, &hints, &svr) != 0)
    {
        perror("getaddrinfo failed: ");
        exit(0);
    }

    struct hostent *server;
    server = gethostbyname(ip);

    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(atoi(port));

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
    {
        printf("Failed to set SO_REUSEADDR for port %s\n", port);
        perror("setsockopt failed: ");
    }

    // Connecting to server socket
    if (connect(serverSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("Failed to open socket to server: %s\n", ip);
        perror("Connect failed: ");
        exit(0);
    }

    // Added a new server to map
    servers[serverSocket] = new Server(serverSocket);

    FD_SET(serverSocket, &openSockets);
    maxfds = max(maxfds, serverSocket);
    // Adding server to server map
    servers[serverSocket] = new Server(serverSocket);
    n--;

    string msg = "LISTSERVERS," + local_group_name;
    stuffHex(msg);
    // Sending LISTSERVERS for connection
    if (send(serverSocket, msg.c_str(), msg.length(), 0) < 0)
    {
        perror("Failed to send LISTSERVERS to external server");
    }
    else{
        Logger(msg, "Sent");
    }
}

// Getting Local ip address
// src: https://www.binarytides.com/tcp-syn-portscan-in-c-with-linux-sockets
void get_local_ip ( char * buffer) {
    int sock = socket (AF_INET, SOCK_DGRAM, 0);
    
    const char* kGoogleDnsIp = "8.8.8.8";
    int dns_port = 53;
    
    struct sockaddr_in serv;
    
    memset( &serv, 0, sizeof(serv) );
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(kGoogleDnsIp);
    serv.sin_port = htons( dns_port );
    
    int err = connect( sock , (const struct sockaddr*) &serv , sizeof(serv) );
    
    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    err = getsockname(sock, (struct sockaddr*) &name, &namelen);
    
    const char *p = inet_ntop(AF_INET, &name.sin_addr, buffer, 100);
    
    close(sock);
}

// Remove SOH from string
void destuffHex(string &s)
{
    s.erase(remove(s.begin(), s.end(), '\x01'), s.end());
}

// Adding SOH and EOT to string
void stuffHex(string &s) 
{
    s = "\x01" + s + "\x04";
}

// Construct send message for SEND_MSG
string constructMsg(vector<string> tokens) {
    string msg = "SEND_MSG," + local_group_name + "," + tokens[1] + ",";

    for(int i = 2; i < tokens.size(); i++) 
    {
        msg += tokens[i] + " ";
    }

    stuffHex(msg);

    return msg;
}

// Sending keepalive to server if he doesn't respond we kick him out
void sendKeepalive(fd_set &openSockets, fd_set &readSockets, int *maxfds){
    for(auto const& server: servers){
        Server* serv = server.second;
        // Close connection if inactive for 10 mins
        if(difftime(time(0), serv->keepAliveTime) > MINUTE * 10)
        {
            string msg = "LEAVE," + serv->ip + "," + serv->port;
            stuffHex(msg);
            if(send(serv->sock, msg.c_str(), msg.length(),0) < 0){
                perror("Sending KEEPALIVE failed");
            }
            closeServer(serv->sock, &openSockets, maxfds);
        }
        else
        {
            string msg;

            msg = "KEEPALIVE," + to_string(outgoingMsg[serv->name].size());
            cout << "KEEPALIVE MSG = " << msg << endl;
            stuffHex(msg);
            if(send(serv->sock, msg.c_str(), msg.length(),0) < 0){
                perror("Sending KEEPALIVE failed");
            }
        }
    }
    // Reseting keepalive timer
    keepaliveTimer = time(0);
}

// Logger for logging and getting date
// https://stackoverflow.com/questions/7400418/writing-a-log-file-in-c-c
inline string getCurrentDateTime(string s)
{
    time_t now = time(0);
    struct tm  tstruct;
    char  buf[80];
    tstruct = *localtime(&now);
    if(s=="now")
        strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
    else if(s=="date")
        strftime(buf, sizeof(buf), "%Y-%m-%d", &tstruct);
    return string(buf);
};
inline void Logger(string logMsg, string path)
{
    string filePath = "./"+path+"_log_"+getCurrentDateTime("date")+".txt";
    string now = getCurrentDateTime("now");
    ofstream ofs(filePath.c_str(), std::ios_base::out | std::ios_base::app );
    ofs << now << '\t' << logMsg << '\n';
    ofs.close();
}