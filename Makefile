CC=gcc
CFLAGS=-Wall -Wextra -std=gnu11
SRC=src/msh.c
BIN=msh

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

clean:
	rm -f $(BIN)
