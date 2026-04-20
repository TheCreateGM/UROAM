/*
 * UROAM Client Library - C/C++ API
 *
 * Universal RAM Optimization Layer - Client Library
 * Provides a clean C API for applications to use UROAM features.
 *
 * Copyright (C) 2024 Universal RAM Optimization Project
 * License: GPL v2
 */

#ifndef UROAM_CLIENT_H
#define UROAM_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UROAM_VERSION "1.0.0"
#define UROAM_MAX_POOLS 64
#define UROAM_MAX_METRICS 128

typedef enum {
    UROAM_OK = 0,
    UROAM_ERROR = -1,
    UROAM_ERROR_NOT_INITIALIZED = -2,
    UROAM_ERROR_ALREADY_INITIALIZED = -3,
    UROAM_ERROR_INVALID_PARAM = -4,
    UROAM_ERROR_OUT_OF_MEMORY = -5,
    UROAM_ERROR_NOT_SUPPORTED = -6,
    UROAM_ERROR_PERMISSION_DENIED = -7
} uroam_error_t;

typedef enum {
    UROAM_WORKLOAD_GENERAL,
    UROAM_WORKLOAD_AI_INFERENCE,
    UROAM_WORKLOAD_AI_TRAINING,
    UROAM_WORKLOAD_GAMING,
    UROAM_WORKLOAD_BACKGROUND
} uroam_workload_t;

typedef enum {
    UROAM_MODE_DEFAULT,
    UROAM_MODE_CONSERVATIVE,
    UROAM_MODE_AGGRESSIVE,
    UROAM_MODE_GAMING,
    UROAM_MODE_AI
} uroam_mode_t;

typedef enum {
    UROAM_ALLOC_NORMAL,
    UROAM_ALLOC_HUGEPAGE,
    UROAM_ALLOC_NUMA_AWARE,
    UROAM_ALLOC_COMPRESSED,
    UROAM_ALLOC_PERSISTENT
} uroam_alloc_type_t;

typedef struct {
    uint64_t total_allocated;
    uint64_t total_freed;
    uint64_t current_used;
    uint64_t peak_used;
    uint64_t pool_count;
    uint64_t active_pools;
    uint64_t total_mmap_calls;
    uint64_t total_brk_calls;
    uint64_t total_page_faults;
    float compression_ratio;
    uint64_t ksm_pages_shared;
    uint64_t ksm_pages_sharing;
} uroam_stats_t;

typedef struct {
    int node_id;
    uint64_t total_memory;
    uint64_t free_memory;
    uint64_t used_memory;
    uint32_t cpu_count;
    uint32_t active_cpu_count;
} uroam_numa_node_t;

typedef struct {
    void *base;
    size_t size;
    size_t used;
    uint64_t flags;
    int numa_node;
    bool is_active;
} uroam_pool_info_t;

typedef struct {
    int workload_type;
    int optimization_mode;
    uint64_t memory_limit;
    bool ksm_enabled;
    bool zswap_enabled;
    bool hugepages_enabled;
    int swappiness;
} uroam_config_t;

typedef struct {
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
    void *(*realloc)(void *ptr, size_t size);
    void *(*calloc)(size_t nmemb, size_t size);
    void *(*aligned_alloc)(size_t alignment, size_t size);
} uroam_allocator_t;

typedef void (*uroam_metrics_callback_t)(const uroam_stats_t *stats, void *user_data);

int uroam_init(void);
int uroam_init_with_config(const uroam_config_t *config);
void uroam_shutdown(void);

int uroam_get_version(char *version, size_t len);

int uroam_set_workload(uroam_workload_t workload);
int uroam_set_mode(uroam_mode_t mode);

int uroam_alloc(size_t size, void **out_ptr);
int uroam_alloc_aligned(size_t alignment, size_t size, void **out_ptr);
int uroam_alloc_numa(int node, size_t size, void **out_ptr);
int uroam_free(void *ptr);

int uroam_pool_create(const char *name, size_t size, uint64_t flags, void **out_pool);
int uroam_pool_destroy(void *pool);
int uroam_pool_alloc(void *pool, size_t size, void **out_ptr);
int uroam_pool_free(void *pool, void *ptr);

int uroam_get_stats(uroam_stats_t *stats);
int uroam_reset_stats(void);

int uroam_get_numa_node_count(void);
int uroam_get_numa_node_info(int node, uroam_numa_node_t *info);

int uroam_enable_ksm(void);
int uroam_disable_ksm(void);
int uroam_get_ksm_stats(uint64_t *shared, uint64_t *sharing);

int uroam_enable_zswap(void);
int uroam_disable_zswap(void);
int uroam_set_zswap_params(int pool_percent, const char *compressor);

int uroam_enable_hugepages(void);
int uroam_disable_hugepages(void);

int uroam_set_memory_limit(uint64_t bytes);
int uroam_get_memory_limit(uint64_t *bytes);

int uroam_madvise_mergeable(void *addr, size_t size);
int uroam_madvise_unmergeable(void *addr, size_t size);
int uroam_madvise_dontneed(void *addr, size_t size);

int uroam_register_metrics_callback(uroam_metrics_callback_t callback, void *user_data);
int uroam_unregister_metrics_callback(uroam_metrics_callback_t callback);

int uroam_tune_for_workload(uroam_workload_t workload);

int uroam_get_error_string(int error, char *buf, size_t len);

bool uroam_is_initialized(void);
bool uroam_is_supported(void);

const char *uroam_get_error_message(void);

#ifdef __cplusplus
}
#endif

#endif /* UROAM_CLIENT_H */
