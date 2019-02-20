all: build 

build: client server

client: client.o
	gcc -g client.o -o client

server: server.o
	gcc -g server.o -o server

.c.o: 
	gcc -Wall -g -c $? 

clean:
	-rm -f *.o *.log client server 
