#include <stdio.h>
#include <stdlib.h>
#include "uroam.h"

int main() {
    fprintf(stderr, "Starting test...\n");
    
    if (memopt_init() != 0) {
        fprintf(stderr, "Init failed\n");
        return 1;
    }
    
    fprintf(stderr, "Init done, allocating...\n");
    
    void *ptr = memopt_alloc(1024 * 1024);
    if (!ptr) {
        fprintf(stderr, "Allocation failed\n");
        return 1;
    }

    fprintf(stderr, "Allocated at %p\n", ptr);
    
    memopt_stats_t stats;
    memopt_get_stats(&stats);
    fprintf(stderr, "Stats: alloc=%zu, pool=%zu/%zu\n",
           stats.total_allocated, stats.pool_used, stats.pool_total);

    memopt_free(ptr);
    fprintf(stderr, "Freed\n");
    
    fprintf(stderr, "Test passed!\n");
    return 0;
}