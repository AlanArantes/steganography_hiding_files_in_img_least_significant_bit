CC=gcc
CFLAGS=-I./include -Wall -Wextra
LIBS=-lm

all: build/steganography

build/steganography: src/steganography.c
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -f build/steganography

.PHONY: all clean
