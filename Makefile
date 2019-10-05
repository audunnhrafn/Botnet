all: 
	g++ -std=c++11 server.cpp -o tsamvgroup69
	g++ -std=c++11 client.cpp -o client
client: 
	g++ -std=c++11 client.cpp -o client
server: 
	g++ -std=c++11 server.cpp -o tsamvgroup69
clean: 
	rm tsamvgroup69
	rm client