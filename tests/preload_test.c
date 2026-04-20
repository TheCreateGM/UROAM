#include <stdio.h>
#include <stdlib.h>

int main() {
    fprintf(stderr, "Starting test - using LD_PRELOAD...\n");
    fflush(stderr);
    
    void *ptr = malloc(1024);
    fprintf(stderr, "malloc returned %p\n", ptr);
    fflush(stderr);
    
    if (ptr) free(ptr);
    fprintf(stderr, "Done!\n");
    return 0;
}
