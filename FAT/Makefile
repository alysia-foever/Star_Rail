CFLAGS=$(shell pkg-config fuse3 --cflags) -Wall -std=gnu11 -Wno-unused-variable
LDFLAGS=$(shell pkg-config fuse3 --libs)
LDLIBS=

CC=gcc

.PHONY: clean debug static

all: fat16

debug: CFLAGS += -g
debug: fat16

static: CFLAGS += -static
static: fat16

fat16: fat16.o fat16_fixed.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

fat16_fixed.o: fat16_fixed.c fat16.h
	$(CC) $(CFLAGS) -c -o $@ $<

fat16.o: fat16.c fat16.h fat16_utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

hello: hello.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

hello.o: hello.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f fat16 hello *.o


