#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <num_children> <sleep_seconds>\n", argv[0]);
        return 1;
    }

    printf("Parent PID=%d, PPID=%d, ppid is bash!\n",
                    getpid(), getppid());
    
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

    for (long i = 0; i < x; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return 1;
        } else if (pid == 0) {
            printf("Child %ld: PID=%d, PPID=%d, sleeping for %ld seconds\n",
                   i, getpid(), getppid(), y);
            sleep((unsigned int)y);
            printf("Child %ld: PID=%d woke up and exits\n", i, getpid());
            exit(0);
        }
    }

    for (long i = 0; i < x; i++) {
        wait(NULL);
    }

    printf("Parent: all children finished\n");
    return 0;
}
