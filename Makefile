.DEFAULT_GOAL := default

CC=gcc
CFLAGS=-g -Wall
BINARIES=mytail

%: %.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(BINARIES)

default: $(BINARIES)
