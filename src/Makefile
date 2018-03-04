CC=gcc
CFLAGS=-Wall -Wextra -std=gnu11 -pedantic -O2 -fno-strict-aliasing -ggdb

server: globals.h map.h map.c server.c
	$(CC) $(CFLAGS) map.c server.c -o server

clean:
	rm -f *.o server hunter prey
