#!/bin/bash

# Create project structure
mkdir -p src include build

# Check if wget is installed, if not install it
if ! command -v wget &> /dev/null; then
    echo "wget not found. Installing wget..."
    sudo apt-get update
    sudo apt-get install -y wget
fi

# Download STB headers
echo "Downloading STB headers..."
wget -O include/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
wget -O include/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h

# Check if build essentials are installed
if ! command -v gcc &> /dev/null; then
    echo "GCC not found. Installing build-essential..."
    sudo apt-get update
    sudo apt-get install -y build-essential
fi

# Create Makefile
cat > Makefile << 'EOF'
CC=gcc
CFLAGS=-I./include -Wall -Wextra
LIBS=-lm

all: build/steganography

build/steganography: src/steganography.c
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -f build/steganography

.PHONY: all clean
EOF

echo "Setup complete!"
echo "To build the project:"
echo "1. Place your steganography.c file in the src directory"
echo "2. Run 'make' to compile"
echo "3. The executable will be created in the build directory"
