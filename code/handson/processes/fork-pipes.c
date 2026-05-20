#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

int main(void) {
    int fd[2];
    pid_t pid;
    const char *msg = "Hello from parent through the pipe!\n";
    char buf[128];

    if (pipe(fd) == -1) {
        perror("pipe");
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* Child: read from pipe */
        close(fd[1]);

        ssize_t n = read(fd[0], buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read");
            close(fd[0]);
            return 1;
        }

        buf[n] = '\0';
        printf("Child received: %s", buf);

        close(fd[0]);
        return 0;
    } else {
        /* Parent: write to pipe */
        close(fd[0]);

        if (write(fd[1], msg, strlen(msg)) < 0) {
            perror("write");
            close(fd[1]);
            return 1;
        }

        close(fd[1]);
        wait(NULL);
        printf("Parent: child finished\n");
    }

    return 0;
}
