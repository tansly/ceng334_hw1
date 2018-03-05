CC=gcc
CFLAGS=-Wall -Wextra -std=gnu11 -pedantic -O2 -fno-strict-aliasing -ggdb

server: globals.h server.c
	$(CC) $(CFLAGS) server.c -o server

clean:
	rm -f *.o server hunter prey
