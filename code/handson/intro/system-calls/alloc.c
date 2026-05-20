#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    size_t n = 10 * 1024 * 1024;
    char *p = (char *)malloc(n);
    if (!p) {
        perror("malloc");
        return 1;
    }

    memset(p, 0xAB, n);

    printf("malloc(%zu) -> %p\n", n, (void *)p);
    free(p);
    return 0;
}
