#include <stdio.h>
#include <stdlib.h>
#include "uroam.h"

int main() {
    fprintf(stderr, "Starting test...\n");
    fflush(stderr);
    
    if (memopt_init_with_policy(MEMOPT_WORKLOAD_AI_INFERENCE) != 0) {
        fprintf(stderr, "Failed to initialize memopt\n");
        return 1;
    }

    fprintf(stderr, "Init done, allocating...\n");
    fflush(stderr);
    
    void *ptr = memopt_alloc(1024 * 1024);
    if (!ptr) {
        fprintf(stderr, "Allocation failed\n");
        return 1;
    }

    fprintf(stderr, "Allocated at %p\n", ptr);
    fflush(stderr);
    
    memopt_stats_t stats;
    memopt_get_stats(&stats);
    fprintf(stderr, "Stats: alloc=%zu, pool=%zu/%zu\n",
           stats.total_allocated, stats.pool_used, stats.pool_total);

    memopt_free(ptr);
    memopt_shutdown();
    
    fprintf(stderr, "Test passed!\n");
    return 0;
}