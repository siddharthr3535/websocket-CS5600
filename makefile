all: server rfs

server: server.c
	gcc -Wall -pthread server.c -o server

rfs: client.c
	gcc -Wall client.c -o rfs

clean:
	rm -f server rfs
	rm -rf server_root

.PHONY: all clean