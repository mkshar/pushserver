CC=g++
CFLAGS=-std=c++11 -c -Wall -lgcc

all: server client

server: server.o
	$(CC) server.o -o server

client: client.o
	$(CC) client.o -o client

server.o: server.cpp shedule.hpp
	$(CC) $(CFLAGS) server.cpp

client.o: client.cpp
	$(CC) $(CFLAGS) client.cpp

clean:
	rm -rf *.o server client
