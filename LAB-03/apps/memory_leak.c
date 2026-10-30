#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    printf("=== LAB-03: Memory Leak Test ===\n");

    char *buffer = malloc(100);

    if (buffer == NULL) {
        perror("malloc");
        return 1;
    }

    strcpy(buffer, "Hello Embedded Linux");

    printf("Buffer: %s\n", buffer);

    // BUG: Memory leak - missing free(buffer)

    return 0;
}
