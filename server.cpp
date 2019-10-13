//
// Simple chat server for TSAM-409
//
// Command line: ./chat_server 4000
//
// Author: Jacky Mallett (jacky@ru.is)
//
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

char local_ip[15];
const char *local_server_port;
const string local_group_name = "P3_GROUP_69";

int n;              // number of sockets
int maxfds;         // Passed to select() as max fd in set
fd_set openSockets; // Current open sockets

// Simple class for handling connections from clients.
//
// Client(int socket) - socket to send/receive traffic from client.
class Client
{
public:
    int sock;    // socket of client connection
    string name; // Limit length of name of client's user

    Client(int socket) : sock(socket) {}

    ~Client() {} // Virtual destructor defined for base class
};

class Server
{
public:
    int sock;    // socket of client connection
    string name; // Limit length of name of servers user
    string ip;  // The ip of the connecting server
    string port; 

    Server(int socket) : sock(socket) {}

    Server(int socket, string name, string ip, string port) : sock(socket)
    {
        this->sock = socket;
        this->name = name;
        this->ip = ip;
        this->port = port;

        cout << "name from constructor: " << name << endl;
    }
     
    ~Server() {} // Virtual destructor defined for base class
};

map<int, Client *> clients; // Lookup table for per Client information
map<int, Server *> servers; // Lookup table for per Server information
map<string, vector<string>> incommingMsg; // Lookup tables for incomming msg <to_group_id, msg>
map<string, vector<string>> outgoingMsg; // Lookup tables for outgoing msg <to_group_id, msg>

int open_socket(int portno);
void closeClient(int clientSocket, fd_set *openSockets, int *maxfds);
void closeServer(int serverSocket, fd_set *openSockets, int *maxfds);
void clientCommand(int clientSocket, fd_set *openSockets, int *maxfds, char *buffer);
void serverCommand(int serverSocket, fd_set *openSockets, int *maxfds, char *buffer);
void connectToBotServer(const char *ip, const char *port);
void destuffHex(string &s);
void stuffHex(string &s);
void get_local_ip ( char * buffer);
string constructMsg(vector<string> tokens);
vector<string> split(string s, string delim);

int main(int argc, char *argv[])
{
    bool finished;

    int clientListenSock; // Socket for CLIENT connections to server
    int serverListenSock; // Socket for SERVER connections to server
    int clientSock;       // Socket of connecting client
    int serverSock;       // Socket of connecting client

    fd_set readSockets;   // Socket list for select()
    fd_set exceptSockets; // Exception socket list

    struct sockaddr_in client;
    socklen_t clientLen;

    struct sockaddr_in server;
    socklen_t serverLen;

    char buffer[5001]; // buffer for reading from clients

    if (argc != 2)
    {
        printf("Usage: chat_server <ip port>\n");
        exit(0);
    }

    string clientPortInput;

    cout << "Enter port to listen to CLIENTS: ";
    cin >> clientPortInput;

    get_local_ip(local_ip);
    local_server_port = argv[1];

    // Setup socket for server to listen to CLIENTS
    clientListenSock = open_socket(atoi(clientPortInput.c_str()));
    printf("Listening for CLIENTS on port: %d\n", atoi(clientPortInput.c_str()));

    if (listen(clientListenSock, BACKLOG) < 0)
    {
        printf("Listen for CLIENTS failed on port %s\n", clientPortInput.c_str());
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
        n = select(maxfds + 1, &readSockets, NULL, &exceptSockets, NULL);

        if (n < 0)
        {
            perror("select failed - closing down\n");
            finished = true;
        }
        else
        {
            // First, accept  any new connections to the server on the listening socket
            if (FD_ISSET(clientListenSock, &readSockets))
            {
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
                serverSock = accept(serverListenSock, (struct sockaddr *)&server, &serverLen);
                printf("accept SERVER***\n");
                FD_SET(serverSock, &openSockets);
                maxfds = max(maxfds, serverSock);
                servers[serverSock] = new Server(serverSock);
                n--;
                printf("A new server has connected! \n");

                string msg = "\x01LISTSERVERS," + local_group_name + "\x04";
                if (send(serverSock, msg.c_str(), msg.length(), 0) < 0)
                {
                    perror("Failed to send LISTSERVERS to new server");
                }
            }
            // Now check for commands from clients and servers
            while (n-- > 0)
            {
                vector<int> socksToClose; // sockets to close after each for loops, this way we don't get segfault!

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
                            cout << "From : " << server->name << "----------------" << endl;
                            cout << buffer << endl;
                            serverCommand(server->sock, &openSockets, &maxfds, buffer);
                            cout << "---------------------" << endl;
                            cout << endl;

                        }
                    }
                }
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

    if ((tokens[0].compare("CONNECT") == 0) && (tokens.size() == 3))
    {
        connectToBotServer(tokens[1].c_str(), tokens[2].c_str());
    }
    else if (tokens[0].compare("LISTSERVERS") == 0)
    {
        string msg;

        msg += "SERVERS," + local_group_name + "," + local_ip + "," + local_server_port + ";";
        for (auto const &server : servers)
        {
                msg += server.second->name + "," + server.second->ip + "," + server.second->port + ";";
        }

        send(clientSocket, msg.c_str(), msg.length(), 0);
    }
    else if ((tokens[0].compare("SENDMSG") == 0)) 
    {
        // find the server with the group_id in the server
        bool groupfound = false;
        for (auto const& serv: servers) 
        {
            Server *server = serv.second;
            if (server->name == tokens[1])
            {
                groupfound = true;
                string msg = constructMsg(tokens);

                if(send(server->sock, msg.c_str(), msg.length(), 0) < 0) {
                    perror("Failed to SEND_MSG to server");
                }
            }
        }
        if (!groupfound) {
            cout << "Group ID not found, setting message to outgoingMessages" << endl;
            string msg;
            for (int i = 2; i < tokens.size(); i++) {
                msg += tokens[i] + " ";
            }
            outgoingMsg[tokens[1]].push_back(msg);
        }
    }
    else if ((tokens[0].compare("GETMSG") == 0) && tokens.size() == 2) 
    {
        // find the server with the group_id in the server
        for (auto const& serv: servers) 
        {
            Server *server = serv.second;
                string msg = "\x01GET_MSG," + tokens[1] + "\x04";

                if(send(server->sock, msg.c_str(), msg.length(), 0) < 0) {
                    perror("Failed to GETMSG to server");
                }
        }
    }
    
    else
    {
        cout << "Unknown command from client:" << buffer << endl;
    }
}

void serverCommand(int serverSocket, fd_set *openSockets, int *maxfds, char *buffer)
{
    // parse commands by prefix and suffix (\x01 and \x04)
    vector<string> commands;
    string command;
    stringstream commandsStream(buffer);
    while (getline(commandsStream, command, '\x04'))
    {
        commands.push_back(string(command));
    }



    for (string command : commands)
    {
        // parse for each command 
        destuffHex(command); // remove \x01 prefix
        stringstream cStream(command);
        string c;
        getline(cStream, c, ';'); // used to extract first servers on list, ; assuming only appears on SERVERS command
        vector<string> tokens;
        string token;
        stringstream stream(c);
        while (getline(stream, token, ','))
        {
            tokens.push_back(string(token));
        }

        if ((tokens[0].compare("LISTSERVERS") == 0) && (tokens.size() == 2))
        {
            string msg;

            msg += "SERVERS," + local_group_name + "," + local_ip + "," + local_server_port + ";";
            for (auto const &server : servers)
            {
                Server *s = server.second;
                if (s->name != "" && s->ip != "" && s->port != "")
                {
                    msg += server.second->name + "," + server.second->ip + "," + server.second->port + ";";
                }
            }

            stuffHex(msg);
 
            if ((send(serverSocket, msg.c_str(), msg.length(), 0)) < 0)
            {
                perror("FAILED TO SEND");
            }
            string newCommand = "LISTSERVERS," + local_group_name;
            stuffHex(newCommand);
            if ((send(serverSocket, newCommand.c_str(), newCommand.length(), 0)) < 0)
            {
                perror("FAILED TO SEND");
            }
        }
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
        else if ((tokens[0].compare("KEEPALIVE") == 0) && tokens.size() == 2) 
        {
            cout << "INSIDE KEEPALIVE" << endl;
        }
        else if ((tokens[0].compare("GET_MSG") == 0) && tokens.size() == 2)
        {
            if (outgoingMsg.size() != 0) {
                // send all messages that have the tokens[1] as key on the outgoingMsg loookup
                for (auto const& outgoingMessage : outgoingMsg[tokens[1]]) {
                    string msg = "SEND_MSG," + local_group_name + "," + tokens[1] + "," + outgoingMessage;
                    stuffHex(msg);

                    if ((send(serverSocket, msg.c_str(), msg.length(), 0)) < 0)
                    {
                        perror("FAILED TO SEND");
                    }
                }
                // delete messages that have been sent
                outgoingMsg.erase(tokens[1]);
            }
        }
        else if ((tokens[0].compare("SEND_MSG") == 0) && tokens.size() == 4)
        {
            cout << "I GOT THE SEND_MSG COMMAND" << endl;
        }
        else if ((tokens[0].compare("LEAVE") == 0) && tokens.size() == 3)
        {
            for(auto const& serv : servers) {
                Server* server = serv.second;
                if (server->ip == tokens[1] && server->port == tokens[2]) 
                {
                    closeServer(server->sock, openSockets, maxfds);
                }
            }
        }
        else
        {
            cout << "Unknown command from server:" << buffer << endl;
        }
    }
}

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

    // Turn on SO_REUSEADDR to allow socket to be quickly reused after program exit.

    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
    {
        printf("Failed to set SO_REUSEADDR for port %s\n", port);
        perror("setsockopt failed: ");
    }

    if (connect(serverSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("Failed to open socket to server: %s\n", ip);
        perror("Connect failed: ");
        exit(0);
    }

    servers[serverSocket] = new Server(serverSocket);

    // TODO: PRINT smth here...
    FD_SET(serverSocket, &openSockets);
    maxfds = max(maxfds, serverSocket);
    servers[serverSocket] = new Server(serverSocket);
    n--;

    string msg = "\x01LISTSERVERS," + local_group_name + "\x04";

    if (send(serverSocket, msg.c_str(), msg.length(), 0) < 0)
    {
        perror("Failed to send LISTSERVERS to external server");
    }
}

void get_local_ip ( char * buffer) {
    int sock = socket ( AF_INET, SOCK_DGRAM, 0);
    
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

// used to remove \x01 from string
void destuffHex(string &s)
{
    s.erase(remove(s.begin(), s.end(), '\x01'), s.end());
}

void stuffHex(string &s) 
{
    s = "\x01" + s + "\x04";
}

string constructMsg(vector<string> tokens) {
    string msg = "SEND_MSG," + tokens[1] + "," + local_group_name + ",";

    for(int i = 2; i < tokens.size(); i++) 
    {
        msg += tokens[i] + " ";
    }

    stuffHex(msg);

    return msg;
}