CC=gcc
CFLAGS=-Wall -Wextra -std=gnu11 -pedantic -Og -fno-strict-aliasing -ggdb

all: server hunter prey

server: globals.h server.c
	$(CC) $(CFLAGS) server.c -o server

hunter: globals.h hunter.c
	$(CC) $(CFLAGS) hunter.c -o hunter

prey: globals.h prey.c
	$(CC) $(CFLAGS) prey.c -o prey

clean:
	rm -f *.o server hunter prey
