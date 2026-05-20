#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static int mysystem(const char *command) {
    pid_t pid;
    int status;

    if (command == NULL) {
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        char *arguments[] = {"/bin/sh", "-c", (char *)command, NULL};
        execv(arguments[0], arguments);

        perror("execv");
        _exit(127);
    }

    while (wait(&status) < 0) {
        if (errno == EINTR) {
            continue;
        }
        perror("wait");
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return -WTERMSIG(status);
    }

    return -1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s \"shell command\"\n", argv[0]);
        return 1;
    }

    int rc = mysystem(argv[1]);
    printf("mysystem returned %d\n", rc);
    return 0;
}
