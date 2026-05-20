#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 16

int main(int argc, char *argv[])
{
    int fd_in, fd_out;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    ssize_t bytes_written;
    size_t total_read = 0;
    size_t total_written = 0;
    int read_calls = 0;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input-file> <output-file>\n", argv[0]);
        return 1;
    }

    /* strlen() works on C strings (null-terminated character arrays). */
    printf("Input filename: %s\n", argv[1]);
    printf("strlen(argv[1]) = %zu\n", strlen(argv[1]));

    /* sizeof() gives the size in bytes of an object/type at compile time. */
    printf("sizeof(buffer) = %zu bytes\n", sizeof(buffer));
    printf("sizeof(buffer[0]) = %zu byte\n", sizeof(buffer[0]));
    printf("buffer can hold %zu chars\n", sizeof(buffer) / sizeof(buffer[0]));

    /* Open the input file in read-only mode. */
    fd_in = open(argv[1], O_RDONLY);
    if (fd_in == -1) {
        perror("open input");
        return 1;
    }

    /*
     * Open the output file in write-only mode.
     * O_CREAT   -> create file if it does not exist
     * O_TRUNC   -> truncate file to 0 length if it exists
     * 0644      -> file permissions: rw-r--r--
     */
    fd_out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out == -1) {
        perror("open output");
        close(fd_in);
        return 1;
    }

    /*
     * read() returns:
     *   > 0 : number of bytes actually read
     *   = 0 : end of file (EOF)
     *   -1 : error
     */
    while ((bytes_read = read(fd_in, buffer, sizeof(buffer))) > 0) {
        read_calls++;
        total_read += (size_t)bytes_read;

        printf("read() call %d got %zd bytes\n", read_calls, bytes_read);

        /*
         * IMPORTANT:
         * buffer is raw bytes from the file.
         * It is NOT necessarily a C string.
         * So for write(), we must use bytes_read, NOT strlen(buffer).
         */
        bytes_written = write(fd_out, buffer, (size_t)bytes_read);
        if (bytes_written == -1) {
            perror("write");
            close(fd_in);
            close(fd_out);
            return 1;
        }

        total_written += (size_t)bytes_written;
    }

    if (bytes_read == -1) {
        perror("read");
        close(fd_in);
        close(fd_out);
        return 1;
    }

    if (close(fd_in) == -1) {
        perror("close input");
        close(fd_out);
        return 1;
    }

    if (close(fd_out) == -1) {
        perror("close output");
        return 1;
    }

    printf("\nSummary:\n");
    printf("  total bytes read    = %zu\n", total_read);
    printf("  total bytes written = %zu\n", total_written);
    printf("  number of read() calls = %d\n", read_calls);

    return 0;
}

