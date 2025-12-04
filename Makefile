CC = gcc
CFLAGS = -Wall

# Trova tutti i .c del progetto (inclusi sottocartelle), escludendo main.c e client.c
CSRC = $(filter-out ./main.c ./client.c,$(shell find . -name '*.c'))

all: server client

server: main.c $(CSRC)
	$(CC) $(CFLAGS) main.c $(CSRC) -o server

client: client.c $(CSRC)
	$(CC) $(CFLAGS) client.c $(CSRC) -o client

run-server: server
	@echo "Avvio del server in background..."
	./server &

run-client: client
	@echo "Avvio del client..."
	./client

clean:
	rm -f server client