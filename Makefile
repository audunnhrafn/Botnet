all: 
	g++ -std=c++11 server.cpp -o server
	g++ -std=c++11 client.cpp -o client
client: 
	g++ -std=c++11 client.cpp -o client
server: 
	g++ -std=c++11 server.cpp -o server
clean: 
	rm server
	rm client