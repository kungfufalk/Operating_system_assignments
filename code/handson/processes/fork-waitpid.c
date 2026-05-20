// reap-in-fork-order.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>

int main(void) {
    pid_t children[8];

    for (size_t i = 0; i < 8; i++) {
        children[i] = fork();

        if (children[i] < 0) {
            perror("fork");
            exit(1);
        }

        if (children[i] == 0) {
            printf("Child %ld was born: PID=%d, PPID=%d, sleeping for 10 seconds\n",
                   i, getpid(), getppid());
	    sleep(10);
            _exit(110 + (int)i);
        }
    }

    for (size_t i = 0; i < 8; i++) {
        int status;
        pid_t pid = waitpid(children[i], &status, 0);

        if (pid < 0) {
            perror("waitpid");
            exit(1);
        }
        assert(pid == children[i]);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 110 + (int)i);
        printf("Child with pid %d accounted for (return status of %d).\n",
               (int)children[i], WEXITSTATUS(status));
    }

    return 0;
}
