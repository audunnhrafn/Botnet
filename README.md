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
**GETMSG  *GROUPID***  -  Get messages waiting for the group ID given.
**SENDMSG *GROUPID MESSAGE* ** - Send a message to the group ID given with the message given.
**GROUPIDLISTSERVERS** - Lists servers connected to your server.

