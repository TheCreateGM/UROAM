#ifndef MEMOPT_H
#define MEMOPT_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sched.h>
#include <pthread.h>

#ifdef HAVE_NUMA
#include <numa.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Configuration
#define MEMOPT_POOL_SIZE_DEFAULT (1UL << 30) // 1GB
#define MEMOPT_PAGE_SIZE 4096
#define MEMOPT_HUGEPAGE_SIZE (2UL << 20) // 2MB

// Allocation flags
#define MEMOPT_ALLOC_NONE        0x00
#define MEMOPT_ALLOC_HUGEPAGE    0x01
#define MEMOPT_ALLOC_NUMA_AWARE  0x02
#define MEMOPT_ALLOC_ZEROED      0x04
#define MEMOPT_ALLOC_SHARED      0x08
#define MEMOPT_ALLOC_LOW_LATENCY 0x10
#define MEMOPT_ALLOC_HIGH_THROUGHPUT 0x20

// Free list node
typedef struct memopt_free_block {
    size_t size;
    struct memopt_free_block* next;
} memopt_free_block_t;

// Memory pool
typedef struct memopt_pool {
    void* base;
    size_t size;
    size_t used;
    uint64_t flags;
    int numa_node;
    pthread_mutex_t lock;
    memopt_free_block_t* free_list;
    struct memopt_pool* next;
} memopt_pool_t;

#define MEMOPT_POOL_INITIALIZER { \
    .base = NULL, \
    .size = 0, \
    .used = 0, \
    .flags = 0, \
    .numa_node = -1, \
    .lock = PTHREAD_MUTEX_INITIALIZER, \
    .free_list = NULL, \
    .next = NULL \
}

// Memory statistics
typedef struct memopt_stats {
    size_t total_allocated;
    size_t total_freed;
    size_t pool_used;
    size_t pool_total;
    size_t compression_ratio;
    size_t ksm_pages_shared;
    size_t ksm_pages_sharing;
    uint64_t allocation_count;
    uint64_t free_count;
} memopt_stats_t;

// Workload types
typedef enum {
    MEMOPT_WORKLOAD_GENERAL,
    MEMOPT_WORKLOAD_AI_INFERENCE,
    MEMOPT_WORKLOAD_AI_TRAINING,
    MEMOPT_WORKLOAD_GAMING,
    MEMOPT_WORKLOAD_BACKGROUND
} memopt_workload_t;

// Initialization
int memopt_init(void);
int memopt_init_with_policy(memopt_workload_t workload);
void memopt_shutdown(void);

// Allocation API
void* memopt_alloc(size_t size);
void* memopt_alloc_flags(size_t size, uint64_t flags);
void memopt_free(void* ptr);
void* memopt_realloc(void* ptr, size_t size);
void* memopt_calloc(size_t nmemb, size_t size);

// Pool management
memopt_pool_t* memopt_pool_create(size_t size, uint64_t flags, int numa_node);
void memopt_pool_destroy(memopt_pool_t* pool);
void* memopt_pool_alloc(memopt_pool_t* pool, size_t size);
void memopt_pool_free(memopt_pool_t* pool, void* ptr);

// System optimizations
int memopt_enable_ksm(void);
int memopt_disable_ksm(void);
int memopt_set_swappiness(int value);
int memopt_tune_zswap(void);
int memopt_enable_hugepages(void);
int memopt_disable_hugepages(void);

// Compression (zRAM/zswap)
int memopt_enable_zram(void);
int memopt_disable_zram(void);
int memopt_set_zram_size(const char *size_str);
int memopt_set_zram_compressor(const char *comp);
int memopt_get_zram_compression_ratio(float *ratio);
int memopt_dynamic_tune(void);

// Page deduplication
int memopt_merge_pages(void *addr, size_t size);
int memopt_unmerge_pages(void *addr, size_t size);
int memopt_set_ksm_batch_size(unsigned int size);
int memopt_set_ksm_scan_interval(unsigned int ms);
int memopt_start_dedup_monitor(void);
int memopt_stop_dedup_monitor(void);
int memopt_conservative_dedup(void);
int memopt_aggressive_dedup(void);
int memopt_tune_for_workload(int workload_type);

// NUMA utilities
int memopt_numa_node_count(void);
int memopt_numa_node_of_cpu(int cpu);
size_t memopt_numa_node_memory(int node);
int memopt_bind_to_node(int node);
int memopt_alloc_on_node(void **ptr, size_t size, int node);
int memopt_migrate_pages(void *ptr, size_t size, int from_node, int to_node);
int memopt_get_mempolicy(int *mode, void **addr, size_t *max_items, int node);
int memopt_set_mempolicy(int mode, const unsigned long *nmask, unsigned long maxnode);
int memopt_bind_mempolicy(int node);
int memopt_interleave_mempolicy(int node_count);
int memopt_prefer_node_mempolicy(int node);
void* memopt_numa_aware_alloc(size_t size, int preferred_node);
void memopt_numa_aware_free(void *ptr, size_t size, int node);
int memopt_get_local_node(void);

typedef struct {
    int node_id;
    size_t total_memory;
    size_t free_memory;
    int cpu_count;
} memopt_numa_node_info_t;

void memopt_numa_stats(memopt_numa_node_info_t *nodes, int *count);
void memopt_numa_stats_internal(memopt_numa_node_info_t *nodes, int *count);

// Statistics
void memopt_get_stats(memopt_stats_t* stats);
void memopt_print_stats(void);

#ifdef __cplusplus
}
#endif

#endif // MEMOPT_H
