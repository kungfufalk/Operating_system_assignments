#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>

#define CHUNK 16

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <num_children> <sleep_seconds> <file>\n", argv[0]);
        return 1;
    }

    char *endptr;

    long x = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || x < 0) {
        fprintf(stderr, "Invalid number of children: %s\n", argv[1]);
        return 1;
    }

    long y = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0' || y < 0) {
        fprintf(stderr, "Invalid sleep time: %s\n", argv[2]);
        return 1;
    }

    const char *filename = argv[3];

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    /* Make stdout unbuffered so prints appear immediately */
    setvbuf(stdout, NULL, _IONBF, 0);

    for (long i = 0; i < x; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            close(fd);
            return 1;
        } else if (pid == 0) {
            char buf[CHUNK + 1];
            ssize_t n;

            while ((n = read(fd, buf, CHUNK)) > 0) {
                buf[n] = '\0';
                printf("Child %ld (pid=%d) read %zd bytes: \"%s\"\n",
                       i, getpid(), n, buf);
                sleep((unsigned int)y);
            }

            if (n < 0) {
                perror("read");
            } else {
                printf("Child %ld (pid=%d) reached EOF\n", i, getpid());
            }

            close(fd);
            _exit(0);
        }
    }

    close(fd); 

    for (long i = 0; i < x; i++) {
        wait(NULL);
    }

    printf("Parent: all children finished\n");
    return 0;
}

