#include <stdio.h>
#include <stdlib.h>
#include "memopt.h"

int main() {
    fprintf(stderr, "Creating pool...\n");
    
    memopt_pool_t *pool = memopt_pool_create(16 * 1024 * 1024, 0, -1);
    if (!pool) {
        fprintf(stderr, "Pool creation failed\n");
        return 1;
    }
    
    fprintf(stderr, "Pool created: %zu bytes\n", pool->size);
    
    void *ptr1 = memopt_pool_alloc(pool, 4096);
    if (!ptr1) {
        fprintf(stderr, "Allocation failed\n");
        return 1;
    }
    
    fprintf(stderr, "Allocated: %p\n", ptr1);
    fprintf(stderr, "Pool used: %zu\n", pool->used);
    
    memopt_pool_free(pool, ptr1);
    fprintf(stderr, "Freed\n");
    
    memopt_pool_destroy(pool);
    fprintf(stderr, "Pool destroyed\n");
    
    fprintf(stderr, "Test passed!\n");
    return 0;
}