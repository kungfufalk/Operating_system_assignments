#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    setbuf(stdout, NULL); 

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        printf("child:  pid=%d, ppid=%d\n", getpid(), getppid());
        _exit(0);
    } else {
        printf("parent: pid=%d, child=%d\n", getpid(), pid);
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            return 1;
        }
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
}
