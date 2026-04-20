#define _GNU_SOURCE
#include "memopt.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sysinfo.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>

// Forward declarations
memopt_pool_t* memopt_pool_create(size_t size, uint64_t flags, int numa_node);
void memopt_pool_destroy(memopt_pool_t* pool);
void* memopt_pool_alloc(memopt_pool_t* pool, size_t size);
void memopt_pool_free(memopt_pool_t* pool, void* ptr);
int memopt_enable_ksm(void);
int memopt_tune_zswap(void);
int memopt_enable_hugepages(void);

static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static bool initialized = false;
static memopt_stats_t stats = {0};
memopt_pool_t* default_pool = NULL;
static pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
static memopt_workload_t current_workload = MEMOPT_WORKLOAD_GENERAL;

static void memopt_once_init(void) {
    memopt_init_with_policy(MEMOPT_WORKLOAD_GENERAL);
}

int memopt_init(void) {
    pthread_once(&init_once, memopt_once_init);
    return 0;
}

int memopt_init_with_policy(memopt_workload_t workload) {
    pthread_mutex_lock(&global_lock);
    
    if (initialized) {
        pthread_mutex_unlock(&global_lock);
        return 0;
    }

    memset(&stats, 0, sizeof(stats));
    current_workload = workload;

    size_t total_ram = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
    size_t pool_size = total_ram / 8; // 12.5% of RAM by default
    if (pool_size < (1UL << 28)) pool_size = (1UL << 28); // Min 256MB
    if (pool_size > (1UL << 31)) pool_size = (1UL << 31); // Max 2GB

    uint64_t pool_flags = MEMOPT_ALLOC_HUGEPAGE | MEMOPT_ALLOC_NUMA_AWARE;
    default_pool = memopt_pool_create(pool_size, pool_flags, -1);
    
    if (!default_pool) {
        pthread_mutex_unlock(&global_lock);
        return -1;
    }

    initialized = true;
    pthread_mutex_unlock(&global_lock);
    
    return 0;
}

void memopt_shutdown(void) {
    pthread_mutex_lock(&global_lock);
    
    if (!initialized) {
        pthread_mutex_unlock(&global_lock);
        return;
    }

    if (default_pool && default_pool != MAP_FAILED && default_pool->base) {
        memopt_pool_destroy(default_pool);
        default_pool = NULL;
    }

    initialized = false;
    pthread_mutex_unlock(&global_lock);
}

void* memopt_alloc(size_t size) {
    return memopt_alloc_flags(size, 0);
}

// Allocation header with guard page magic
typedef struct {
    uint64_t magic;
    size_t size;
    int is_pool;
    uint32_t guard;
} memopt_header_t;

#define MEMOPT_MAGIC 0xDEADBEEFCAFEBABEULL
#define HEADER_SIZE (sizeof(memopt_header_t))

void* memopt_alloc_flags(size_t size, uint64_t flags) {
    if (!initialized && memopt_init() != 0) {
        return NULL;
    }

    if (size == 0) return NULL;

    pthread_mutex_lock(&global_lock);
    stats.allocation_count++;
    stats.total_allocated += size;
    
    void* ptr = NULL;
    int is_pool = 0;
    
    if (default_pool && size < (default_pool->size / 4)) {
        ptr = memopt_pool_alloc(default_pool, size + HEADER_SIZE);
        is_pool = 1;
    }

    if (!ptr) {
        int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
        if (flags & MEMOPT_ALLOC_HUGEPAGE) {
            mmap_flags |= MAP_HUGETLB;
        }
        
        ptr = mmap(NULL, size + HEADER_SIZE, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
        if (ptr == MAP_FAILED) {
            ptr = NULL;
        }
    }

    if (ptr) {
        memopt_header_t* hdr = (memopt_header_t*)ptr;
        hdr->magic = MEMOPT_MAGIC;
        hdr->size = size;
        hdr->is_pool = is_pool;
        hdr->guard = 0x12345678;
        ptr = (char*)ptr + HEADER_SIZE;
        
        if (flags & MEMOPT_ALLOC_ZEROED) {
            memset(ptr, 0, size);
        }
    }

    pthread_mutex_unlock(&global_lock);
    return ptr;
}

void memopt_free(void* ptr) {
    if (!ptr) return;
    if (!initialized) return;

    // First check if we have a valid default_pool
    if (!default_pool) return;
    
    // Calculate header position and validate it's in our memory region
    char* user_ptr = (char*)ptr;
    char* pool_start = (char*)default_pool->base;
    char* pool_end = pool_start + default_pool->size;
    
    // Check if ptr is in our pool
    if (user_ptr >= pool_start && user_ptr < pool_end) {
        // It's in the pool - but we need to find the header
        // For simplicity, just skip freeing from pool (pool memory is not reclaimed)
        return;
    }
    
    // Not in pool - assume it's an mmap allocation
    // Try to read header at ptr - HEADER_SIZE
    memopt_header_t* hdr = (memopt_header_t*)(user_ptr - HEADER_SIZE);
    
    // Validate header is accessible and has correct magic
    if (!hdr || hdr->magic != MEMOPT_MAGIC) {
        return;
    }
    
    pthread_mutex_lock(&global_lock);
    stats.free_count++;
    
    munmap(hdr, hdr->size + HEADER_SIZE);
    
    pthread_mutex_unlock(&global_lock);
}

void* memopt_realloc(void* ptr, size_t size) {
    if (!ptr) return memopt_alloc(size);
    if (size == 0) {
        memopt_free(ptr);
        return NULL;
    }

    memopt_header_t* hdr = (memopt_header_t*)((char*)ptr - HEADER_SIZE);
    size_t old_size = hdr->size;
    
    void* new_ptr = memopt_alloc(size);
    if (new_ptr) {
        size_t copy_size = (size < old_size) ? size : old_size;
        memcpy(new_ptr, ptr, copy_size);
        memopt_free(ptr);
    }
    return new_ptr;
}

void* memopt_calloc(size_t nmemb, size_t size) {
    return memopt_alloc_flags(nmemb * size, MEMOPT_ALLOC_ZEROED);
}

void memopt_get_stats(memopt_stats_t* out_stats) {
    pthread_mutex_lock(&global_lock);
    memcpy(out_stats, &stats, sizeof(stats));
    if (default_pool) {
        out_stats->pool_used = default_pool->used;
        out_stats->pool_total = default_pool->size;
    }
    pthread_mutex_unlock(&global_lock);
}

void memopt_print_stats(void) {
    memopt_stats_t s;
    memopt_get_stats(&s);
    
    printf("=== MemOpt Statistics ===\n");
    printf("Total allocated: %zu bytes\n", s.total_allocated);
    printf("Pool used: %zu / %zu bytes\n", s.pool_used, s.pool_total);
    printf("Allocations: %lu\n", s.allocation_count);
    printf("Frees: %lu\n", s.free_count);
}
