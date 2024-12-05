#define FUSE_USE_VERSION 26
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stddef.h>
#include <pthread.h>
#include <sys/statvfs.h>
#include <libgen.h>

#define BYTE_LENGTH 8
#define MAX_FILE_SIZE (1024 * 1024 * 10)
#define MAX_FILENAME_LENGTH 255
#define MAX_FILES 256
#define METADATA_START_OFFSET 32

typedef struct
{
    char name[MAX_FILENAME_LENGTH];
    size_t size;
    size_t offset;
    time_t mtime;
    mode_t mode;
} stego_file_t;

typedef struct
{
    unsigned char *image_data;
    int width;
    int height;
    int channels;
    char *image_path;
    stego_file_t files[MAX_FILES];
    size_t file_count;
    size_t total_data_size;
    pthread_mutex_t mutex;
    int dirty;
} stego_fs_t;

static stego_fs_t stego_fs;

// Common structure for metadata
typedef struct
{
    uint32_t file_size;
    uint8_t ext_length;
    char extension[10];
} file_metadata_t;

static const char *get_file_name(const char *filename)
{
    char *bname = basename(filename);
    return bname;
}

static const char *get_file_extension(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
    {
        return "";
    }
    return dot + 1;
}

static void get_metadata_extension(const char *filename, char *ext_buf, size_t buf_size, uint8_t *ext_len)
{
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
    {
        ext_buf[0] = '\0';
        *ext_len = 0;
        return;
    }

    dot++; // Skip the dot
    *ext_len = strlen(dot);
    if (*ext_len >= buf_size)
    {
        *ext_len = buf_size - 1;
    }
    strncpy(ext_buf, dot, *ext_len);
    ext_buf[*ext_len] = '\0';
}

static void embed_bit(unsigned char *data, int width, int height, size_t position, int bit)
{
    size_t max_bits = width * height * 3;
    if (position >= max_bits)
    {
        fprintf(stderr, "Position %zu exceeds maximum bits %zu\n", position, max_bits);
        return;
    }

    size_t pixel_pos = position / 3;
    size_t channel = position % 3;
    data[(pixel_pos * 3) + channel] = (data[(pixel_pos * 3) + channel] & 0xFE) | bit;
}

static int extract_bit(unsigned char *data, int width, int height, size_t position)
{
    size_t max_bits = width * height * 3;
    if (position >= max_bits)
    {
        fprintf(stderr, "Position %zu exceeds maximum bits %zu\n", position, max_bits);
        return 0;
    }

    size_t pixel_pos = position / 3;
    size_t channel = position % 3;
    return data[(pixel_pos * 3) + channel] & 1;
}

static void write_bits(size_t value, size_t num_bits, size_t *current_position)
{
    for (int i = num_bits - 1; i >= 0; i--)
    {
        embed_bit(stego_fs.image_data, stego_fs.width, stego_fs.height, *current_position, (value >> i) & 1);
        (*current_position)++;
    }
}

static size_t read_bits(size_t num_bits, size_t *current_position)
{
    size_t value = 0;
    for (size_t i = 0; i < num_bits; i++)
    {
        value = (value << 1) | extract_bit(stego_fs.image_data, stego_fs.width, stego_fs.height, *current_position);
        (*current_position)++;
    }
    return value;
}

static void save_filesystem() {
    if (!stego_fs.dirty) return;

    size_t position = 0;
    uint32_t magic = 0x5354454;

    // Write magic number
    for (int i = 31; i >= 0; i--) {
        embed_bit(stego_fs.image_data, stego_fs.width, stego_fs.height, 
                 position++, (magic >> i) & 1);
    }

    // Write file size
    size_t file_size = stego_fs.files[0].size;
    for (int i = 31; i >= 0; i--) {
        embed_bit(stego_fs.image_data, stego_fs.width, stego_fs.height,
                 position++, (file_size >> i) & 1);
    }

    // Write minimal extension info
    uint8_t ext_length = 0;
    for (int i = 7; i >= 0; i--) {
        embed_bit(stego_fs.image_data, stego_fs.width, stego_fs.height,
                 position++, 0);
    }

    printf("Saving file size: %zu bytes\n", file_size);
    stbi_write_png(stego_fs.image_path, stego_fs.width, stego_fs.height, 
                   3, stego_fs.image_data, stego_fs.width * 3);
    stego_fs.dirty = 0;
}

static void *stego_init(struct fuse_conn_info *conn)
{
    pthread_mutex_init(&stego_fs.mutex, NULL);
    return NULL;
}

static void stego_destroy(void *private_data)
{
    pthread_mutex_lock(&stego_fs.mutex);
    save_filesystem();
    stbi_image_free(stego_fs.image_data);
    free(stego_fs.image_path);
    pthread_mutex_unlock(&stego_fs.mutex);
    pthread_mutex_destroy(&stego_fs.mutex);
}

static int stego_getattr(const char *path, struct stat *stbuf)
{
    pthread_mutex_lock(&stego_fs.mutex);
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        pthread_mutex_unlock(&stego_fs.mutex);
        return 0;
    }

    path++;
    for (size_t i = 0; i < stego_fs.file_count; i++)
    {
        if (strcmp(path, stego_fs.files[i].name) == 0)
        {
            stbuf->st_mode = stego_fs.files[i].mode;
            stbuf->st_nlink = 1;
            stbuf->st_size = stego_fs.files[i].size;
            stbuf->st_mtime = stego_fs.files[i].mtime;
            pthread_mutex_unlock(&stego_fs.mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&stego_fs.mutex);
    return -ENOENT;
}

static int stego_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    if (strcmp(path, "/") != 0)
        return -ENOENT;

    pthread_mutex_lock(&stego_fs.mutex);
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (size_t i = 0; i < stego_fs.file_count; i++)
    {
        filler(buf, stego_fs.files[i].name, NULL, 0);
    }

    pthread_mutex_unlock(&stego_fs.mutex);
    return 0;
}

static int stego_open(const char *path, struct fuse_file_info *fi)
{
    pthread_mutex_lock(&stego_fs.mutex);
    path++;

    for (size_t i = 0; i < stego_fs.file_count; i++)
    {
        if (strcmp(path, stego_fs.files[i].name) == 0)
        {
            pthread_mutex_unlock(&stego_fs.mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&stego_fs.mutex);
    return -ENOENT;
}

static int stego_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    pthread_mutex_lock(&stego_fs.mutex);

    if (stego_fs.file_count >= MAX_FILES)
    {
        pthread_mutex_unlock(&stego_fs.mutex);
        return -ENOSPC;
    }

    const char *filename = path + 1;
    stego_file_t *new_file = &stego_fs.files[stego_fs.file_count];

    strncpy(new_file->name, filename, MAX_FILENAME_LENGTH - 1);
    new_file->size = 0;
    new_file->offset = stego_fs.total_data_size;
    new_file->mtime = time(NULL);
    new_file->mode = mode;

    stego_fs.file_count++;
    stego_fs.dirty = 1;

    pthread_mutex_unlock(&stego_fs.mutex);
    return 0;
}

static int stego_write(const char *path, const char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    pthread_mutex_lock(&stego_fs.mutex);
    
    path++;
    stego_file_t *file = NULL;
    
    for (size_t i = 0; i < stego_fs.file_count; i++) {
        if (strcmp(path, stego_fs.files[i].name) == 0) {
            file = &stego_fs.files[i];
            break;
        }
    }
    
    if (!file) {
        pthread_mutex_unlock(&stego_fs.mutex);
        return -ENOENT;
    }

    printf("Writing %zu bytes at offset %ld\n", size, offset);
    
    // Calculate new file size
    size_t new_size = offset + size;
    file->size = new_size > file->size ? new_size : file->size;

    // Write data at correct bit position
    size_t bit_position = METADATA_START_OFFSET + (offset * 8);
    for (size_t i = 0; i < size; i++) {
        for (int j = 7; j >= 0; j--) {
            embed_bit(stego_fs.image_data, stego_fs.width, stego_fs.height,
                     bit_position++, (buf[i] >> j) & 1);
        }
    }
    
    file->mtime = time(NULL);
    stego_fs.dirty = 1;
    
    pthread_mutex_unlock(&stego_fs.mutex);
    return size;
}

static int stego_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    pthread_mutex_lock(&stego_fs.mutex);

    path++;
    stego_file_t *file = NULL;

    for (size_t i = 0; i < stego_fs.file_count; i++)
    {
        if (strcmp(path, stego_fs.files[i].name) == 0)
        {
            file = &stego_fs.files[i];
            break;
        }
    }

    if (!file)
    {
        pthread_mutex_unlock(&stego_fs.mutex);
        return -ENOENT;
    }

    if (offset >= file->size)
    {
        pthread_mutex_unlock(&stego_fs.mutex);
        return 0;
    }

    if (offset + size > file->size)
    {
        size = file->size - offset;
    }

    size_t current_bit = file->offset + (offset * 8);
    for (size_t i = 0; i < size; i++)
    {
        buf[i] = 0;
        for (int j = 7; j >= 0; j--)
        {
            buf[i] |= extract_bit(stego_fs.image_data, stego_fs.width, stego_fs.height,
                                  current_bit++)
                      << j;
        }
    }

    pthread_mutex_unlock(&stego_fs.mutex);
    return size;
}

static int stego_unlink(const char *path)
{
    pthread_mutex_lock(&stego_fs.mutex);

    path++;
    size_t idx;
    for (idx = 0; idx < stego_fs.file_count; idx++)
    {
        if (strcmp(path, stego_fs.files[idx].name) == 0)
        {
            break;
        }
    }

    if (idx == stego_fs.file_count)
    {
        pthread_mutex_unlock(&stego_fs.mutex);
        return -ENOENT;
    }

    memmove(&stego_fs.files[idx], &stego_fs.files[idx + 1],
            (stego_fs.file_count - idx - 1) * sizeof(stego_file_t));

    stego_fs.file_count--;
    stego_fs.dirty = 1;

    pthread_mutex_unlock(&stego_fs.mutex);
    return 0;
}

static int stego_truncate(const char *path, off_t size)
{
    pthread_mutex_lock(&stego_fs.mutex);

    path++;
    stego_file_t *file = NULL;

    for (size_t i = 0; i < stego_fs.file_count; i++)
    {
        if (strcmp(path, stego_fs.files[i].name) == 0)
        {
            file = &stego_fs.files[i];
            break;
        }
    }

    if (!file)
    {
        pthread_mutex_unlock(&stego_fs.mutex);
        return -ENOENT;
    }

    if (size > MAX_FILE_SIZE)
    {
        pthread_mutex_unlock(&stego_fs.mutex);
        return -EFBIG;
    }

    file->size = size;
    file->mtime = time(NULL);
    stego_fs.dirty = 1;

    pthread_mutex_unlock(&stego_fs.mutex);
    return 0;
}

static int stego_utimens(const char *path, const struct timespec tv[2])
{
    pthread_mutex_lock(&stego_fs.mutex);

    path++;
    stego_file_t *file = NULL;

    for (size_t i = 0; i < stego_fs.file_count; i++)
    {
        if (strcmp(path, stego_fs.files[i].name) == 0)
        {
            file = &stego_fs.files[i];
            break;
        }
    }

    if (!file)
    {
        pthread_mutex_unlock(&stego_fs.mutex);
        return -ENOENT;
    }

    file->mtime = tv[1].tv_sec;
    stego_fs.dirty = 1;

    pthread_mutex_unlock(&stego_fs.mutex);
    return 0;
}

static int stego_chmod(const char *path, mode_t mode)
{
    pthread_mutex_lock(&stego_fs.mutex);

    path++;
    stego_file_t *file = NULL;

    for (size_t i = 0; i < stego_fs.file_count; i++)
    {
        if (strcmp(path, stego_fs.files[i].name) == 0)
        {
            file = &stego_fs.files[i];
            break;
        }
    }

    if (!file)
    {
        pthread_mutex_unlock(&stego_fs.mutex);
        return -ENOENT;
    }

    file->mode = mode;
    stego_fs.dirty = 1;

    pthread_mutex_unlock(&stego_fs.mutex);
    return 0;
}

static int init_stego_fs(const char *image_path)
{
    stego_fs.image_data = stbi_load(image_path, &stego_fs.width, &stego_fs.height, &stego_fs.channels, 3);
    if (!stego_fs.image_data)
        return -1;

    size_t position = 0;
    uint32_t magic = 0;
    for (int i = 31; i >= 0; i--)
    {
        magic |= (extract_bit(stego_fs.image_data, stego_fs.width, stego_fs.height, position++) << i);
    }

    if (magic != 0x5354454)
    {
        stego_fs.file_count = 0;
        stego_fs.total_data_size = position;
        stego_fs.dirty = 0;
        return 0; // Empty but valid filesystem
    }

    uint32_t total_size = 0;
    for (int i = 31; i >= 0; i--)
    {
        total_size |= (extract_bit(stego_fs.image_data, stego_fs.width, stego_fs.height, position++) << i);
    }

    uint8_t ext_length = 0;
    for (int i = 7; i >= 0; i--)
    {
        ext_length |= (extract_bit(stego_fs.image_data, stego_fs.width, stego_fs.height, position++) << i);
    }

    // Skip extension
    position += ext_length * 8;

    // Initialize filesystem with single file if present
    if (total_size > 0)
    {
        stego_fs.file_count = 1;
        stego_fs.files[0].size = total_size;
        stego_fs.files[0].offset = position;
        stego_fs.files[0].mode = S_IFREG | 0644;
        stego_fs.files[0].mtime = time(NULL);
        strcpy(stego_fs.files[0].name, "hidden_file");
    }
    else
    {
        stego_fs.file_count = 0;
    }

    stego_fs.total_data_size = position + (total_size * 8);
    stego_fs.dirty = 0;
    return 0;
}

static struct fuse_operations stego_oper = {
    .init = stego_init,
    .destroy = stego_destroy,
    .getattr = stego_getattr,
    .readdir = stego_readdir,
    .open = stego_open,
    .create = stego_create,
    .write = stego_write,
    .read = stego_read,
    .unlink = stego_unlink,
    .truncate = stego_truncate, // Add if not present
    .utimens = stego_utimens,   // Add if not present
    .chmod = stego_chmod,       // Add if not present
};

void write_metadata(size_t *position, const file_metadata_t *metadata)
{
    write_bits(metadata->file_size, 32, position);
    write_bits(metadata->ext_length, 8, position);
    for (int i = 0; i < metadata->ext_length; i++)
    {
        write_bits(metadata->extension[i], 8, position);
    }
}

static void read_metadata(size_t *position, file_metadata_t *metadata)
{
    metadata->file_size = read_bits(32, position);
    metadata->ext_length = read_bits(8, position);
    for (int i = 0; i < metadata->ext_length; i++)
    {
        metadata->extension[i] = read_bits(8, position);
    }
    metadata->extension[metadata->ext_length] = '\0';
}

static int do_hide_file(const char *cover_image, const char *secret_file, const char *output)
{
    int width, height, channels;
    unsigned char *image_data = stbi_load(cover_image, &width, &height, &channels, 3);
    if (!image_data)
        return 1;

    size_t max_capacity = (width * height * 3) / 8;
    printf("Image capacity: %zu bytes\n", max_capacity);

    FILE *f = fopen(secret_file, "rb");
    if (!f)
    {
        stbi_image_free(image_data);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Check if file fits in image
    if (file_size > max_capacity - 64)
    { // Reserve space for metadata
        fprintf(stderr, "File too large for image\n");
        fclose(f);
        stbi_image_free(image_data);
        return 1;
    }

    printf("Embedding file of size: %zu bytes\n", file_size);

    unsigned char *file_data = malloc(file_size);
    fread(file_data, 1, file_size, f);
    fclose(f);

    size_t position = 0;
    // Write magic number for validation
    uint32_t magic = 0x5354454; // "STEG"
    for (int i = 31; i >= 0; i--)
    {
        embed_bit(image_data, width, height, position++, (magic >> i) & 1);
    }

    // Write file size
    for (int i = 31; i >= 0; i--)
    {
        embed_bit(image_data, width, height, position++, (file_size >> i) & 1);
    }

    // Write extension
    uint8_t ext_length = 0;
    char extension[11] = {0};
    get_metadata_extension(secret_file, extension, sizeof(extension), &ext_length);

    for (int i = 7; i >= 0; i--)
    {
        embed_bit(image_data, width, height, position++, (ext_length >> i) & 1);
    }

    for (int i = 0; i < ext_length; i++)
    {
        for (int j = 7; j >= 0; j--)
        {
            embed_bit(image_data, width, height, position++, (extension[i] >> j) & 1);
        }
    }

    // Write file data
    for (size_t i = 0; i < file_size; i++)
    {
        for (int j = 7; j >= 0; j--)
        {
            embed_bit(image_data, width, height, position++, (file_data[i] >> j) & 1);
        }
    }

    stbi_write_png(output, width, height, 3, image_data, width * 3);

    free(file_data);
    stbi_image_free(image_data);
    return 0;
}

static int do_extract_file(const char *stego_image, const char *output)
{
    int width, height, channels;
    unsigned char *image_data = stbi_load(stego_image, &width, &height, &channels, 3);
    if (!image_data)
        return 1;

    size_t position = 0;

    // Read and verify magic number
    uint32_t magic = 0;
    for (int i = 31; i >= 0; i--)
    {
        magic |= (extract_bit(image_data, width, height, position++) << i);
    }

    if (magic != 0x5354454)
    {
        fprintf(stderr, "Invalid steganographic image\n");
        stbi_image_free(image_data);
        return 1;
    }

    // Read file size
    size_t file_size = 0;
    for (int i = 31; i >= 0; i--)
    {
        file_size |= ((size_t)extract_bit(image_data, width, height, position++) << i);
    }

    size_t max_capacity = (width * height * 3) / 8;
    if (file_size > max_capacity - 64)
    {
        fprintf(stderr, "Invalid file size: %zu\n", file_size);
        stbi_image_free(image_data);
        return 1;
    }

    printf("Extracting file of size: %zu bytes\n", file_size);

    // Continue with extension and data extraction
    uint8_t ext_length = 0;
    for (int i = 7; i >= 0; i--)
    {
        ext_length |= (extract_bit(image_data, width, height, position++) << i);
    }

    if (ext_length > 10)
    {
        fprintf(stderr, "Invalid extension length\n");
        stbi_image_free(image_data);
        return 1;
    }

    char extension[11] = {0};
    for (int i = 0; i < ext_length; i++) {
        uint8_t c = 0;
        for (int j = 7; j >= 0; j--) {
            c |= (extract_bit(image_data, width, height, position++) << j);
        }
        extension[i] = c;
    }

    unsigned char *file_data = malloc(file_size);
    for (size_t i = 0; i < file_size; i++)
    {
        file_data[i] = 0;
        for (int j = 7; j >= 0; j--)
        {
            file_data[i] |= (extract_bit(image_data, width, height, position++) << j);
        }
    }

    char *full_output = malloc(strlen(output) + ext_length + 2);
    if (ext_length > 0) {
        sprintf(full_output, "%s.%s", output, extension);
    } else {
        strcpy(full_output, output);
    }

    FILE *f = fopen(full_output, "wb");
    if (f)
    {
        fwrite(file_data, 1, file_size, f);
        fclose(f);
    }

    free(full_output);
    free(file_data);
    stbi_image_free(image_data);
    return 0;
}

static int do_mount_point(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "Usage: %s mount <image_path> <mount_point> [FUSE options]\n", argv[0]);
        return 1;
    }

    stego_fs.image_path = realpath(argv[2], NULL);
    if (!stego_fs.image_path)
    {
        fprintf(stderr, "Invalid image path\n");
        return 1;
    }

    if (init_stego_fs(stego_fs.image_path) != 0)
    {
        fprintf(stderr, "Failed to initialize filesystem\n");
        free(stego_fs.image_path);
        return 1;
    }

    // Adjust FUSE arguments
    char **fuse_argv = malloc((argc - 1) * sizeof(char *));
    fuse_argv[0] = argv[0];
    fuse_argv[1] = argv[3]; // mount point
    for (int i = 4; i < argc; i++)
    {
        fuse_argv[i - 2] = argv[i];
    }

    struct fuse_args args = FUSE_ARGS_INIT(argc - 2, fuse_argv);
    int ret = fuse_main(args.argc, args.argv, &stego_oper, NULL);

    free(fuse_argv);
    return ret;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        // fprintf(stderr, "Usage: %s [hide|extract|mount] <image_path> <mount_point> [FUSE options]\n", argv[0]);
        fprintf(stderr, "Usage: steganography <command> <arg1> <arg2>\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  hide    Combine an image file with a generic file into one output.\n");
        fprintf(stderr, "           Arguments:\n");
        fprintf(stderr, "             <arg1> - Path to the image file.\n");
        fprintf(stderr, "             <arg2> - Path to a generic file (executable, image, text, etc.).\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "  extract  Extract a generic file from an image.\n");
        fprintf(stderr, "           Arguments:\n");
        fprintf(stderr, "             <arg1> - Path to the image file.\n");
        fprintf(stderr, "             <arg2> - Name for the extracted file.\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "  mount    Mount an image file to a directory.\n");
        fprintf(stderr, "           Arguments:\n");
        fprintf(stderr, "             <arg1> - Path to the image file.\n");
        fprintf(stderr, "             <arg2> - Path to the target directory.\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  steganography hide image.png file.txt\n");
        fprintf(stderr, "  steganography extract image.png output.txt\n");
        fprintf(stderr, "  steganography mount image.png /mnt/mydir\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Note: Ensure proper permissions and valid paths for all arguments.\n");
        return 1;
    }

    if (strcmp("hide", argv[1]) == 0 || strcmp("-h", argv[1]) == 0)
    {
        if (argc < 3)
        {
            /* code */
            fprintf(stderr, "Usage: steganography hide <arg1> <arg2>\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "  hide    Combine an image file with a generic file into one output.\n");
            fprintf(stderr, "           Arguments:\n");
            fprintf(stderr, "             <arg1> - Path to the image file.\n");
            fprintf(stderr, "             <arg2> - Path to a generic file (executable, image, text, etc.).\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Examples:\n");
            fprintf(stderr, "  steganography hide image.png file.txt\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Note: Ensure proper permissions and valid paths for all arguments.\n");
            return 1;
        }

        char stego_image[256];
        snprintf(stego_image, sizeof(stego_image), "stego_%s", get_file_name(argv[2]));
        fprintf(stderr, "Starting hiding file... \n");
        int r = do_hide_file(argv[2], argv[3], stego_image);
        if (r != 0)
        {
            /* code */
            fprintf(stderr, "Finishing with error \n");
        }
        fprintf(stderr, "Done. \n");

        return r;
        // return 0;
    }

    if (strcmp("extract", argv[1]) == 0 || strcmp("-e", argv[1]) == 0)
    {
        if (argc < 3)
        {
            /* code */
            fprintf(stderr, "Usage: steganography extract <arg1> <arg2>\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "  extract  Extract a generic file from an image.\n");
            fprintf(stderr, "           Arguments:\n");
            fprintf(stderr, "             <arg1> - Path to the image file.\n");
            fprintf(stderr, "             <arg2> - Name for the extracted file.\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Examples:\n");
            fprintf(stderr, "  steganography extract image.png output.txt\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Note: Ensure proper permissions and valid paths for all arguments.\n");
            return 1;
        }

        fprintf(stderr, "Starting file extraction... \n");
        int r = do_extract_file(argv[2], argv[3]);
        if (r != 0)
        {
            /* code */
            fprintf(stderr, "Finishing with error \n");
        }
        fprintf(stderr, "Done. \n");
        return r;
    }

    if (strcmp("mount", argv[1]) == 0 || strcmp("-m", argv[1]) == 0)
    {
        if (argc < 3)
        {
            /* code */
            fprintf(stderr, "Usage: steganography mount <arg1> <arg2>\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "  mount    Mount an image file to a directory.\n");
            fprintf(stderr, "           Arguments:\n");
            fprintf(stderr, "             <arg1> - Path to the image file.\n");
            fprintf(stderr, "             <arg2> - Path to the target directory.\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Examples:\n");
            fprintf(stderr, "  steganography mount image.png /mnt/mydir\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Note: Ensure proper permissions and valid paths for all arguments.\n");
            return 1;
        }

        return do_mount_point(argc, argv);
    }

    fprintf(stderr, "Command not found <%s>.\n", argv[1]);
    return 0;
}
