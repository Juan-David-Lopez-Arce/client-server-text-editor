CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -fsanitize=address -g -Ilibs
# If you need to sanitize threads, eliminate -fsanitize=address and add -fsanitize=thread

all: server client #tests

server: source/server.c markdown.o libs/document.h libs/markdown.h libs/threads.h
#./fifo_clean.sh
	$(CC) $(CFLAGS) -o server source/server.c markdown.o

client: source/client.c markdown.o libs/document.h libs/markdown.h
	$(CC) $(CFLAGS) -o client source/client.c markdown.o

markdown.o: source/markdown.c libs/markdown.h
	$(CC) $(CFLAGS) -c source/markdown.c -o markdown.o

#THIS IS ONLY FOR TESTING
#tests: tests.c markdown.o libs/document.h libs/markdown.h
#	$(CC) $(CFLAGS) -o tests tests.c markdown.o

clean:
#remove tests, IT IS ONLY FOR TESTING
	rm -f server client markdown.o 
#tests