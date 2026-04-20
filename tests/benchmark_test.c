#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "memopt.h"

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

int main(int argc, char *argv[]) {
    int iterations = 100000;
    size_t alloc_size = 1024;
    
    if (argc > 1) iterations = atoi(argv[1]);
    if (argc > 2) alloc_size = atoi(argv[2]);
    
    printf("Benchmark: %d allocations of %zu bytes\n", iterations, alloc_size);
    
    memopt_init_with_policy(MEMOPT_WORKLOAD_AI_INFERENCE);
    
    double start = get_time_ms();
    
    void **ptrs = malloc(iterations * sizeof(void*));
    for (int i = 0; i < iterations; i++) {
        ptrs[i] = memopt_alloc(alloc_size);
        if (!ptrs[i]) {
            fprintf(stderr, "Allocation failed at %d\n", i);
            return 1;
        }
    }
    
    double alloc_time = get_time_ms() - start;
    printf("Allocation time: %.2f ms (%.2f us/op)\n", 
           alloc_time, alloc_time * 1000 / iterations);
    
    start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        memopt_free(ptrs[i]);
    }
    double free_time = get_time_ms() - start;
    printf("Free time: %.2f ms (%.2f us/op)\n", 
           free_time, free_time * 1000 / iterations);
    
    memopt_stats_t stats;
    memopt_get_stats(&stats);
    printf("\nStats:\n");
    printf("  Total allocated: %zu bytes\n", stats.total_allocated);
    printf("  Pool used: %zu / %zu bytes\n", stats.pool_used, stats.pool_total);
    printf("  Allocations: %lu\n", stats.allocation_count);
    printf("  Frees: %lu\n", stats.free_count);
    
    free(ptrs);
    memopt_shutdown();
    
    return 0;
}