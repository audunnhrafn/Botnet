# Botnet - Group 69
Botnet - Project 3 in TSAM

Creators: Auðunn Hrafn Alexandersson & Gunnlaugur Guðmundsson

#### Compile instructions
Included is a make file with 4 commands:  
<i>make</i> - Compiles both the server and client files.  
<i>clean</i> - Removes both the server and client compiled files.  
<i>server</i> - Compiles the server file.  
<i>client</i> - Compiles the client file.  

#### Run instructions
1. Run make to compile both the client and server.  
2. Run ./tsamvgroup69  < port for server >   
3. The server then asks for a port for the client to connect to.  
4. Run ./client < port given to server >  
5. If both the ports were open then the server and client should be up and running.  

#### Client Commands
- <b>CONNECT <"IP"> <"PORT"> </b> - Connects to server in given ip and port. When this connection is esthablised, the connecting server will send LISTSERVERS,<"GROUP_ID"> and SERVERS,<"CONNECTING_GROUP_ID">,<"CONNECTING_ID">;  
- **GETMSG  <*"GROUPID">***  -  Get messages waiting for the group ID given.    
-- <b>SENDMSG <"GOUPID"> <"MESSAGE"></b> - Send a message to the group ID given with the message given.  
- **LISTSERVERS** - Lists servers connected to your server.    

#### About the pictures  
Please note that the attached pictures refer to CLIENT comands to the server, they are screenshot of wireshark data from Client to Server.

