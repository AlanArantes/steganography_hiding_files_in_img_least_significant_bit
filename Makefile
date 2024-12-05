CC=gcc
CFLAGS=-I./include -Wall -Wextra -D_FILE_OFFSET_BITS=64
LIBS=-lm -lfuse -lpthread

all: build/steganography

build/steganography: src/steganography.c
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -f build/steganography

.PHONY: all clean
