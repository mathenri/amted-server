550server: 550server.cpp threadpool.cpp
	g++ -Wall -o 550server 550server.cpp threadpool.cpp -lpthread
