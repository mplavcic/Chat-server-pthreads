all:
	$(CC) -Wall server.c -O2 -std=c11 -lpthread -o server
