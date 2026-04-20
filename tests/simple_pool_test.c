#include <stdio.h>
#include <stdlib.h>

int main() {
    fprintf(stderr, "Starting test\n");
    
    void *ptr = malloc(4096);
    fprintf(stderr, "malloc: %p\n", ptr);
    
    if (ptr) {
        free(ptr);
        fprintf(stderr, "free done\n");
    }
    
    fprintf(stderr, "Test complete\n");
    return 0;
}