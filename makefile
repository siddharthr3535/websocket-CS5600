# Makefile for Remote File System

CC = gcc
CFLAGS = -Wall -pthread

all: server rfs

server: server.c server.h
	$(CC) $(CFLAGS) server.c -o server

rfs: client.c
	$(CC) $(CFLAGS) client.c -o rfs

clean:
	rm -f server rfs
	rm -rf server_root

.PHONY: all clean