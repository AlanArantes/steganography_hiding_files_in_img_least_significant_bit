# Steganography File Hiding Tool

A C program that implements steganography techniques to hide files within images using the Least Significant Bit (LSB) method. The tool also supports mounting the image as a filesystem.

## Features

- Hide files within images using LSB steganography
- Extract hidden files from images
- Mount images as filesystems
- Cross-platform support (Linux and Windows)
  _Only for hide and extract actions, no support for mounting file systems on Windows_

## Directory Structure

```
.
├── src/
│   └── steganography.c    # Main source code
├── include/               # Header files and libraries
├── build/                 # Compiled binaries
├── setup.sh               # Generates Makefile for Linux
├── run.sh                 # Usage helper script
└── build.bat              # Windows build script
```

## Building

### Linux

```bash
./setup.sh
make
```

### Windows

```cmd
build.bat
```

## Usage

The easiest way to use the program is through the run.sh script:

```bash
./run.sh
```

This will display help text with available commands and options.

### Manual Usage

1. Hide a file in an image:

```bash
./steganography -h <input_file> <image_file>
```

2. Extract hidden file from image:

```bash
./steganography -e <image_file>
```

3. Mount image as filesystem:

```bash
./steganography -m <image_file> <mount_point>
```

## Requirements

- GCC compiler
- Make build system (Linux)
- Windows build tools (for Windows)
- Library Fuse [libfuse](https://github.com/libfuse/libfuse?tab=readme-ov-file)
  _The easiest way to install it is using the Meson's install steps_

## License

[Add your license information here]

## Contributing

Contributions are welcome. Please fork the repository and submit pull requests with any improvements.
