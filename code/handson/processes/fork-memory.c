#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_children>\n", argv[0]);
        return 1;
    }

    char *endptr;
    long nchildren = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || nchildren < 0) {
        fprintf(stderr, "Invalid number of children: %s\n", argv[1]);
        return 1;
    }

    char buffer[128] = "Hello from parent buffer";

    /* Avoid duplicated buffered output across fork */
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Parent before fork: pid=%d, buffer=\"%s\"\n", getpid(), buffer);

    for (long i = 0; i < nchildren; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return 1;
        }

        if (pid == 0) {
            printf("Child %ld before change: pid=%d, buffer=\"%s\"\n",
                   i, getpid(), buffer);

            snprintf(buffer, sizeof(buffer),
                     "Modified by child %ld (pid=%d)", i, getpid());

            printf("Child %ld after change:  pid=%d, buffer=\"%s\"\n",
                   i, getpid(), buffer);

            _exit(0);
        }
    }

    for (long i = 0; i < nchildren; i++) {
        wait(NULL);
    }

    printf("Parent after all children: pid=%d, buffer=\"%s\"\n",
           getpid(), buffer);

    return 0;
}
