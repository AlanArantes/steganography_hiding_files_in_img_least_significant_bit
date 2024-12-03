#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BYTE_LENGTH 8
#define MAX_FILE_SIZE (1024 * 1024 * 10) // 10MB limit

// Function prototypes
void embed_bit(unsigned char *data, int width, int height, int position, int bit);
int extract_bit(unsigned char *data, int width, int height, int position);
const char* get_file_extension(const char *filename);

void hide_file(const char *cover_image_path, const char *secret_file_path, const char *output_path) {
    // Add debug output
    printf("Loading cover image: %s\n", cover_image_path);
    
    int width, height, channels;
    unsigned char *image_data = stbi_load(cover_image_path, &width, &height, &channels, 3);
    if (!image_data) {
        fprintf(stderr, "Error loading cover image: %s\n", stbi_failure_reason());
        return;
    }
    
    printf("Image loaded successfully. Width: %d, Height: %d, Channels: %d\n", width, height, channels);

    // Read secret file
    FILE *secret_file = fopen(secret_file_path, "rb");
    if (!secret_file) {
        fprintf(stderr, "Error opening secret file: %s\n", secret_file_path);
        stbi_image_free(image_data);
        return;
    }

    // Get file size
    fseek(secret_file, 0, SEEK_END);
    long file_size = ftell(secret_file);
    fseek(secret_file, 0, SEEK_SET);

    printf("Secret file size: %ld bytes\n", file_size);

    if (file_size > MAX_FILE_SIZE) {
        fprintf(stderr, "File too large. Maximum size is %d bytes\n", MAX_FILE_SIZE);
        fclose(secret_file);
        stbi_image_free(image_data);
        return;
    }

    // Get file extension
    const char *extension = get_file_extension(secret_file_path);
    size_t ext_len = strlen(extension);
    
    printf("File extension: %s (length: %zu)\n", extension, ext_len);
    
    // Calculate required space
    int required_space = 4 + ext_len + 1 + file_size;
    int available_space = (width * height * 3) / 8;

    printf("Required space: %d bytes, Available space: %d bytes\n", required_space, available_space);

    if (required_space > available_space) {
        fprintf(stderr, "Secret file too large for this cover image\n");
        fclose(secret_file);
        stbi_image_free(image_data);
        return;
    }

    // Allocate memory for file data
    unsigned char *file_data = malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "Failed to allocate memory for file data\n");
        fclose(secret_file);
        stbi_image_free(image_data);
        return;
    }

    size_t bytes_read = fread(file_data, 1, file_size, secret_file);
    fclose(secret_file);

    if (bytes_read != file_size) {
        fprintf(stderr, "Failed to read entire file. Expected %ld bytes, got %zu bytes\n", file_size, bytes_read);
        free(file_data);
        stbi_image_free(image_data);
        return;
    }

    int current_bit = 0;

    // Store file length
    for (int i = 24; i >= 0; i--) {
        int bit = (file_size >> i) & 1;
        embed_bit(image_data, width, height, current_bit++, bit);
    }

    // Store extension length
    unsigned char ext_length = (unsigned char)ext_len;
    for (int i = 7; i >= 0; i--) {
        int bit = (ext_length >> i) & 1;
        embed_bit(image_data, width, height, current_bit++, bit);
    }

    // Store extension
    for (size_t i = 0; i < ext_len; i++) {
        unsigned char b = extension[i];
        for (int j = 7; j >= 0; j--) {
            int bit = (b >> j) & 1;
            embed_bit(image_data, width, height, current_bit++, bit);
        }
    }

    // Store file data
    for (long i = 0; i < file_size; i++) {
        unsigned char b = file_data[i];
        for (int j = 7; j >= 0; j--) {
            int bit = (b >> j) & 1;
            embed_bit(image_data, width, height, current_bit++, bit);
        }
    }

    printf("Writing output image to: %s\n", output_path);
    
    // Save the stego image
    int success = stbi_write_png(output_path, width, height, 3, image_data, width * 3);
    
    if (!success) {
        fprintf(stderr, "Failed to write output image\n");
    } else {
        printf("Successfully wrote output image\n");
    }

    free(file_data);
    stbi_image_free(image_data);
}

void extract_file(const char *stego_image_path, const char *output_path) {
    printf("Loading stego image: %s\n", stego_image_path);
    
    int width, height, channels;
    unsigned char *image_data = stbi_load(stego_image_path, &width, &height, &channels, 3);
    if (!image_data) {
        fprintf(stderr, "Error loading stego image: %s\n", stbi_failure_reason());
        return;
    }

    printf("Image loaded successfully. Width: %d, Height: %d, Channels: %d\n", width, height, channels);

    int current_bit = 0;

    // Extract file length
    int file_length = 0;
    for (int i = 24; i >= 0; i--) {
        int bit = extract_bit(image_data, width, height, current_bit++);
        file_length |= bit << i;
    }

    printf("Extracted file length: %d bytes\n", file_length);

    // Validate file length
    if (file_length < 0 || file_length > MAX_FILE_SIZE) {
        fprintf(stderr, "Invalid file length detected: %d\n", file_length);
        stbi_image_free(image_data);
        return;
    }

    // Extract extension length
    int ext_length = 0;
    for (int i = 7; i >= 0; i--) {
        int bit = extract_bit(image_data, width, height, current_bit++);
        ext_length |= bit << i;
    }

    printf("Extracted extension length: %d\n", ext_length);

    // Validate extension length
    if (ext_length < 0 || ext_length > 10) {
        fprintf(stderr, "Invalid extension length detected: %d\n", ext_length);
        stbi_image_free(image_data);
        return;
    }

    // Extract extension
    char *extension = malloc(ext_length + 1);
    if (!extension) {
        fprintf(stderr, "Failed to allocate memory for extension\n");
        stbi_image_free(image_data);
        return;
    }

    for (int i = 0; i < ext_length; i++) {
        unsigned char b = 0;
        for (int j = 7; j >= 0; j--) {
            int bit = extract_bit(image_data, width, height, current_bit++);
            b |= bit << j;
        }
        extension[i] = b;
    }
    extension[ext_length] = '\0';

    printf("Extracted extension: %s\n", extension);

    // Extract file data
    unsigned char *file_data = malloc(file_length);
    if (!file_data) {
        fprintf(stderr, "Failed to allocate memory for file data\n");
        free(extension);
        stbi_image_free(image_data);
        return;
    }

    for (int i = 0; i < file_length; i++) {
        unsigned char b = 0;
        for (int j = 7; j >= 0; j--) {
            int bit = extract_bit(image_data, width, height, current_bit++);
            b |= bit << j;
        }
        file_data[i] = b;
    }

    // Create output filename with extension
    size_t output_path_len = strlen(output_path);
    size_t ext_len = strlen(extension);
    char *full_output_path = malloc(output_path_len + ext_len + 2); // +2 for '.' and null terminator
    
    if (!full_output_path) {
        fprintf(stderr, "Failed to allocate memory for output path\n");
        free(file_data);
        free(extension);
        stbi_image_free(image_data);
        return;
    }

    sprintf(full_output_path, "%s.%s", output_path, extension);
    
    printf("Writing extracted file to: %s\n", full_output_path);

    // Save the extracted file
    FILE *output_file = fopen(full_output_path, "wb");
    if (!output_file) {
        fprintf(stderr, "Failed to open output file for writing\n");
        free(full_output_path);
        free(file_data);
        free(extension);
        stbi_image_free(image_data);
        return;
    }

    size_t bytes_written = fwrite(file_data, 1, file_length, output_file);
    fclose(output_file);

    if (bytes_written != file_length) {
        fprintf(stderr, "Failed to write entire file. Expected %d bytes, wrote %zu bytes\n", 
                file_length, bytes_written);
    } else {
        printf("Successfully wrote extracted file\n");
    }

    free(full_output_path);
    free(extension);
    free(file_data);
    stbi_image_free(image_data);
}

void embed_bit(unsigned char *data, int width, int height, int position, int bit) {
    int x = position / (3 * height);
    int y = (position / 3) % height;
    int channel = position % 3;
    
    // Add bounds checking
    if (x >= width || y >= height) {
        fprintf(stderr, "Warning: Attempted to embed bit outside image bounds\n");
        return;
    }
    
    int pixel_pos = (y * width + x) * 3 + channel;
    data[pixel_pos] = (data[pixel_pos] & 0xFE) | bit;
}

int extract_bit(unsigned char *data, int width, int height, int position) {
    int x = position / (3 * height);
    int y = (position / 3) % height;
    int channel = position % 3;
    
    // Add bounds checking
    if (x >= width || y >= height) {
        fprintf(stderr, "Warning: Attempted to extract bit outside image bounds\n");
        return 0;
    }
    
    int pixel_pos = (y * width + x) * 3 + channel;
    return data[pixel_pos] & 1;
}

const char* get_file_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return "";
    }
    return dot + 1;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <cover-image> <secret-file> <output-prefix>\n", argv[0]);
        return 1;
    }

    // Print arguments for debugging
    printf("Cover image: %s\n", argv[1]);
    printf("Secret file: %s\n", argv[2]);
    printf("Output prefix: %s\n", argv[3]);

    char stego_image[256];
    char extracted_file[256];
    
    snprintf(stego_image, sizeof(stego_image), "%s_stego.png", argv[3]);
    snprintf(extracted_file, sizeof(extracted_file), "%s_extracted", argv[3]);

    // Hide the file
    hide_file(argv[1], argv[2], stego_image);
    printf("\nStarting extraction...\n\n");
    // Extract the file
    extract_file(stego_image, extracted_file);

    return 0;
}
