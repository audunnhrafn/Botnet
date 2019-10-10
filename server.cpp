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
#include <typeinfo>

#include <iostream>
#include <sstream>
#include <thread>
#include <map>

#include <unistd.h>

// fix SOCK_NONBLOCK for OSX
#ifndef SOCK_NONBLOCK
#include <fcntl.h>
#define SOCK_NONBLOCK O_NONBLOCK
#endif

#define BACKLOG  5          // Allowed length of queue of waiting connections

// Simple class for handling connections from clients.
//
// Client(int socket) - socket to send/receive traffic from client.
class Client
{
  public:
    int sock;              // socket of client connection
    std::string name;           // Limit length of name of client's user

    Client(int socket) : sock(socket){}
    Client(int socket, std::string name) {
        this->sock = socket;
        this->name = name;
    }

    ~Client(){}            // Virtual destructor defined for base class
};

class Server
{
    public: 
        int sock;
        std::string name;
        char* ip;
        char* port;

        Server(int socket, std::string name, char* ip, char* port){
            this->sock = socket;
            this->name = name;
            this->ip = ip;
            this->port = port;
            std::cout << "ip:" << this->ip << " port: " << this->port << std::endl;
        }
        
        ~Server(){}
};

// Note: map is not necessarily the most efficient method to use here,
// especially for a server with large numbers of simulataneous connections,
// where performance is also expected to be an issue.
//
// Quite often a simple array can be used as a lookup table, 
// (indexed on socket no.) sacrificing memory for speed.

std::map<int, Client*> clients; // Lookup table for per Client information
std::map<int, Server*> servers; // Lookup table for per Server information


/*
 * Function to get local ip address of user
 * src: https://www.binarytides.com/tcp-syn-portscan-in-c-with-linux-sockets
 */
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


// Open socket for specified port.
//
// Returns -1 if unable to create the socket for any reason.

int open_socket(int portno)
{
   struct sockaddr_in sk_addr;   // address settings for bind()
   int sock;                     // socket opened for this port
   int set = 1;                  // for setsockopt

   // Create socket for connection. Set to be non-blocking, so recv will
   // return immediately if there isn't anything waiting to be read.
#ifdef __APPLE__     
   if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
   {
      perror("Failed to open socket");
      return(-1);
   }
#else
   if((sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
   {
     perror("Failed to open socket");
    return(-1);
   }
#endif

   // Turn on SO_REUSEADDR to allow socket to be quickly reused after 
   // program exit.

   if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
   {
      perror("Failed to set SO_REUSEADDR:");
   }
   set = 1;
#ifdef __APPLE__     
   if(setsockopt(sock, SOL_SOCKET, SOCK_NONBLOCK, &set, sizeof(set)) < 0)
   {
     perror("Failed to set SOCK_NOBBLOCK");
   }
#endif
   memset(&sk_addr, 0, sizeof(sk_addr));

   sk_addr.sin_family      = AF_INET;
   sk_addr.sin_addr.s_addr = INADDR_ANY;
   sk_addr.sin_port        = htons(portno);

   // Bind to socket to listen for connections from clients

   if(bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0)
   {
      perror("Failed to bind to socket:");
      return(-1);
   }
   else
   {
      return(sock);
   }
}

// Information about this server;
char * serverName = (char*)"V_Group_69";
char SERVERIP[32];
char * SERVERPORT;

// Close a client's connection, remove it from the client list, and
// tidy up select sockets afterwards.

void closeClient(int clientSocket, fd_set *openSockets, int *maxfds)
{
     // Remove client from the clients list
     clients.erase(clientSocket);

     // If this client's socket is maxfds then the next lowest
     // one has to be determined. Socket fd's can be reused by the Kernel,
     // so there aren't any nice ways to do this.

     if(*maxfds == clientSocket)
     {
        for(auto const& p : clients)
        {
            *maxfds = std::max(*maxfds, p.second->sock);
        }
     }

     // And remove from the list of open sockets.

     FD_CLR(clientSocket, openSockets);
}

// Connect a new server and add it to the lookup servers

void connectToBotServer(char* ip, char* port, char* name){
    int sock;
    struct addrinfo hints, *svr;
    struct sockaddr_in serv_addr;
    int set = 1;                              // Toggle for setsockopt
 
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
      
    memset(&hints, 0, sizeof(hints));

    std::cout << ip << ", " << port << std::endl;
    if(getaddrinfo(ip, port, &hints, &svr) != 0) {
        perror("getaddrinfo failed");
        exit(0);
    }

   struct hostent *server;
   server = gethostbyname(ip);

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);

     serv_addr.sin_port = htons(atoi(port));

     sock = socket(AF_INET, SOCK_STREAM, 0);

    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
    {
        printf("Failed to set SO_REUSEADDR for port %s\n", port);
        perror("setsockopt failed: ");
    }

    if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr) )< 0)
    {
        printf("Failed to open socket to server: %s\n", ip);
        perror("Connect failed: ");
        exit(0);
    }

    std::string con = "LISTSERVERS ";
    size_t len = strlen(name);
    char *connectName = (char*)malloc(len+1);
    strcpy(connectName, name);
    serverName = connectName;
    std::string c = con + connectName;
    const char* connectionString = c.c_str();


    if(send(sock, connectionString, strlen(connectionString), 0) == -1) {
        perror("send(), to server failed");
    }
    else {
        servers[servers.size() - 1] = new Server(sock, name, ip, port);
    }
}

// Process command from client on the server

void clientCommand(int clientSocket, fd_set *openSockets, int *maxfds, 
                  char *buffer) 
{
    std::vector<std::string> tokens;
    std::string token;

    // Split command from client into tokens for parsing
    std::stringstream stream(buffer);

    while(stream >> token)
      tokens.push_back(token);

    if((tokens[0].compare("CONNECT") == 0) && (tokens.size() == 3))
    {
        char ip[tokens[1].size() + 1];
        char port[tokens[2].size() + 1];
        std::strcpy(ip, tokens[1].c_str());
        std::strcpy(port, tokens[2].c_str());

        connectToBotServer(ip, port, serverName);
    }
    else if((tokens[0].compare("CONNECT") == 0) && (tokens.size() == 2))
    {
        clients[clientSocket]->name = tokens[1];
    }
    else if((tokens[0].compare("LISTSERVERS") == 0) && (tokens.size() == 2))
    {
        std::string msg = "SERVER," + (std::string)serverName + "," + (std::string)SERVERIP + "," + (std::string)SERVERPORT + ";" ;
        for(auto const& serv : servers) 
        {
            Server *ser = serv.second;
            msg += (std::string)ser->name + "," + std::string(ser->ip) + "," + std::string(ser->port) + ";"; 
        }

        std::cout << msg << std::endl;
    }
    else if(tokens[0].compare("LEAVE") == 0)
    {
        // Close the socket, and leave the socket handling
        // code to deal with tidying up clients etc. when
        // select() detects the OS has torn down the connection.
    
        closeClient(clientSocket, openSockets, maxfds);
    }
    else if(tokens[0].compare("WHO") == 0)
    {
        std::cout << "Who is logged on" << std::endl;
        std::string msg;

        for(auto const& names : clients)
        {
            msg += names.second->name + ",";
            std::cout << names.first << " " << names.second->name << std::endl;
        }
        // Reducing the msg length by 1 loses the excess "," - which
        // granted is totally cheating.
        send(clientSocket, msg.c_str(), msg.length()-1, 0);

    }
    // This is slightly fragile, since it's relying on the order
    // of evaluation of the if statement.
    else if((tokens[0].compare("MSG") == 0) && (tokens[1].compare("ALL") == 0))
    {
        std::string msg;
        for(auto i = tokens.begin()+2;i != tokens.end();i++) 
        {
            msg += *i + " ";
        }

        for(auto const& pair : clients)
        {
            send(pair.second->sock, msg.c_str(), msg.length(),0);
        }
    }
    else if(tokens[0].compare("MSG") == 0)
    {
        for(auto const& pair : clients)
        {
            if(pair.second->name.compare(tokens[1]) == 0)
            {
                std::string msg;
                for(auto i = tokens.begin()+2;i != tokens.end();i++) 
                {
                    msg += *i + " ";
                }
                send(pair.second->sock, msg.c_str(), msg.length(),0);
            }
        }
    }
    else
    {
        std::cout << "Unknown command from client:" << buffer << std::endl;
    }
     
}

int main(int argc, char* argv[])
{
    bool finished;
    int listenSock;                 // Socket for connections to server
    int clientSock;                 // Socket of connecting client
    fd_set openSockets;             // Current open sockets 
    fd_set readSockets;             // Socket list for select()        
    fd_set exceptSockets;           // Exception socket list
    int maxfds;                     // Passed to select() as max fd in set
    struct sockaddr_in client;
    struct sockaddr_in serv_addr;
    socklen_t clientLen;
    char buffer[1025];              // buffer for reading from clients

    if(argc != 2 && argc != 5)
    {
        printf("Usage1: chat_server <ip port>\nUsage2: chat_server <ip_port> <ip_to_connect_to> <port_to_connect_to>");
        exit(0);
    }

    // set the ip and port to global variables
    get_local_ip(SERVERIP);
    SERVERPORT = argv[1];
    std::cout << "server id: " << SERVERIP;

    // Setup socket for server to listen to

    listenSock = open_socket(atoi(argv[1])); 
    printf("Listening on port: %d\n", atoi(argv[1]));

    if(listen(listenSock, BACKLOG) < 0)
    {
        printf("Listen failed on port %s\n", argv[1]);
        exit(0);
    }
    else 
    // Add listen socket to socket set we are monitoring
    {
        FD_ZERO(&openSockets);
        FD_SET(listenSock, &openSockets);
        maxfds = listenSock;
        if ( argc == 5) {
            connectToBotServer(argv[2], argv[3], argv[4]);
        }
    }

    finished = false;

    while(!finished)
    {
        // Get modifiable copy of readSockets
        readSockets = exceptSockets = openSockets;
        memset(buffer, 0, sizeof(buffer));

        // Look at sockets and see which ones have something to be read()
        int n = select(maxfds + 1, &readSockets, NULL, &exceptSockets, NULL);

        if(n < 0)
        {
            perror("select failed - closing down\n");
            finished = true;
        }
        else
        {
            // First, accept  any new connections to the server on the listening socket
            if(FD_ISSET(listenSock, &readSockets))
            {
               clientSock = accept(listenSock, (struct sockaddr *)&client,
                                   &clientLen);
               printf("accept***\n");
               // Add new client to the list of open sockets
               FD_SET(clientSock, &openSockets);

               // And update the maximum file descriptor
               maxfds = std::max(maxfds, clientSock) ;

               // create a new client to store information.
               clients[clientSock] = new Client(clientSock);

               // Decrement the number of sockets waiting to be dealt with
               n--;

               printf("Client connected on server: %d\n", clientSock);
            }
            // Now check for commands from clients
            while(n-- > 0)
            {
               for(auto const& pair : clients)
               {
                  Client *client = pair.second;

                  if(FD_ISSET(client->sock, &readSockets))
                  {
                      // recv() == 0 means client has closed connection
                      if(recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0)
                      {
                          printf("Client closed connection: %d", client->sock);
                          close(client->sock);      

                          closeClient(client->sock, &openSockets, &maxfds);

                      }
                      // We don't check for -1 (nothing received) because select()
                      // only triggers if there is something on the socket for us.
                      else
                      {
                          std::cout << buffer << std::endl;
                          clientCommand(client->sock, &openSockets, &maxfds, 
                                        buffer);
                      }
                  }
               }
            }
        }
    }
}
