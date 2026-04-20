/*
 * UROAM Client Library - Implementation
 *
 * Copyright (C) 2024 Universal RAM Optimization Project
 * License: GPL v2
 */

#define _GNU_SOURCE
#include "uroam_client.h"
#include "memopt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/mman.h>
#include <malloc.h>

static bool g_initialized = false;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uroam_config_t g_config = {0};
static uroam_stats_t g_stats = {0};
static uroam_workload_t g_current_workload = UROAM_WORKLOAD_GENERAL;
static uroam_mode_t g_current_mode = UROAM_MODE_DEFAULT;

static char g_error_message[256] = {0};

static void* (*real_malloc)(size_t) = NULL;
static void (*real_free)(void*) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;

static void get_real_allocators(void) {
    if (!real_malloc) {
        real_malloc = dlsym(RTLD_NEXT, "malloc");
    }
    if (!real_free) {
        real_free = dlsym(RTLD_NEXT, "free");
    }
    if (!real_realloc) {
        real_realloc = dlsym(RTLD_NEXT, "realloc");
    }
    if (!real_calloc) {
        real_calloc = dlsym(RTLD_NEXT, "calloc");
    }
}

static void set_error(const char *msg) {
    strncpy(g_error_message, msg, sizeof(g_error_message) - 1);
}

int uroam_init(void) {
    return uroam_init_with_config(NULL);
}

int uroam_init_with_config(const uroam_config_t *config) {
    pthread_mutex_lock(&g_lock);

    if (g_initialized) {
        pthread_mutex_unlock(&g_lock);
        return UROAM_ERROR_ALREADY_INITIALIZED;
    }

    get_real_allocators();

    memset(&g_config, 0, sizeof(g_config));
    g_config.workload_type = UROAM_WORKLOAD_GENERAL;
    g_config.optimization_mode = UROAM_MODE_DEFAULT;
    g_config.ksm_enabled = false;
    g_config.zswap_enabled = false;
    g_config.hugepages_enabled = true;
    g_config.swappiness = 60;

    if (config) {
        memcpy(&g_config, config, sizeof(g_config));
    }

    memopt_init();

    if (g_config.hugepages_enabled) {
        memopt_enable_hugepages();
    }

    if (g_config.ksm_enabled) {
        memopt_enable_ksm();
    }

    if (g_config.zswap_enabled) {
        memopt_tune_zswap();
    }

    g_stats.peak_used = 0;
    g_stats.pool_count = 0;
    g_stats.active_pools = 0;

    g_initialized = true;
    pthread_mutex_unlock(&g_lock);

    return UROAM_OK;
}

void uroam_shutdown(void) {
    pthread_mutex_lock(&g_lock);

    if (!g_initialized) {
        pthread_mutex_unlock(&g_lock);
        return;
    }

    if (g_config.ksm_enabled) {
        memopt_disable_ksm();
    }

    memopt_shutdown();

    g_initialized = false;
    pthread_mutex_unlock(&g_lock);
}

int uroam_get_version(char *version, size_t len) {
    if (!version) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    strncpy(version, UROAM_VERSION, len - 1);
    version[len - 1] = '\0';

    return UROAM_OK;
}

int uroam_set_workload(uroam_workload_t workload) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    g_current_workload = workload;

    switch (workload) {
    case UROAM_WORKLOAD_AI_INFERENCE:
        memopt_tune_for_workload(MEMOPT_WORKLOAD_AI_INFERENCE);
        memopt_set_swappiness(5);
        break;

    case UROAM_WORKLOAD_AI_TRAINING:
        memopt_tune_for_workload(MEMOPT_WORKLOAD_AI_TRAINING);
        memopt_set_swappiness(10);
        break;

    case UROAM_WORKLOAD_GAMING:
        memopt_tune_for_workload(MEMOPT_WORKLOAD_GAMING);
        memopt_set_swappiness(5);
        break;

    case UROAM_WORKLOAD_BACKGROUND:
        memopt_tune_for_workload(MEMOPT_WORKLOAD_BACKGROUND);
        memopt_set_swappiness(100);
        break;

    default:
        break;
    }

    return UROAM_OK;
}

int uroam_set_mode(uroam_mode_t mode) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    g_current_mode = mode;

    switch (mode) {
    case UROAM_MODE_CONSERVATIVE:
        memopt_conservative_dedup();
        memopt_set_swappiness(100);
        break;

    case UROAM_MODE_AGGRESSIVE:
        memopt_aggressive_dedup();
        memopt_set_swappiness(5);
        break;

    case UROAM_MODE_GAMING:
        memopt_set_swappiness(5);
        memopt_enable_hugepages();
        break;

    case UROAM_MODE_AI:
        memopt_tune_for_workload(MEMOPT_WORKLOAD_AI_TRAINING);
        break;

    default:
        break;
    }

    return UROAM_OK;
}

int uroam_alloc(size_t size, void **out_ptr) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    if (!out_ptr) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    *out_ptr = memopt_alloc(size);

    if (*out_ptr) {
        g_stats.total_allocated += size;
        g_stats.current_used += size;
        if (g_stats.current_used > g_stats.peak_used) {
            g_stats.peak_used = g_stats.current_used;
        }
        return UROAM_OK;
    }

    return UROAM_ERROR_OUT_OF_MEMORY;
}

int uroam_alloc_aligned(size_t alignment, size_t size, void **out_ptr) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    if (!out_ptr) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    void *ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return UROAM_ERROR_OUT_OF_MEMORY;
    }

    *out_ptr = ptr;
    g_stats.total_allocated += size;
    return UROAM_OK;
}

int uroam_alloc_numa(int node, size_t size, void **out_ptr) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    if (!out_ptr || node < 0) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    *out_ptr = memopt_numa_aware_alloc(size, node);

    if (*out_ptr) {
        g_stats.total_allocated += size;
        g_stats.current_used += size;
        return UROAM_OK;
    }

    return UROAM_ERROR_OUT_OF_MEMORY;
}

int uroam_free(void *ptr) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    if (ptr) {
        memopt_free(ptr);
        g_stats.total_freed += 4096;
        if (g_stats.current_used >= 4096) {
            g_stats.current_used -= 4096;
        }
    }

    return UROAM_OK;
}

int uroam_pool_create(const char *name, size_t size, uint64_t flags, void **out_pool) {
    (void)name;

    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    if (!out_pool) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    memopt_pool_t *pool = memopt_pool_create(size, flags, -1);
    if (pool) {
        *out_pool = pool;
        g_stats.pool_count++;
        g_stats.active_pools++;
        return UROAM_OK;
    }

    return UROAM_ERROR_OUT_OF_MEMORY;
}

int uroam_pool_destroy(void *pool) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    if (pool) {
        memopt_pool_destroy((memopt_pool_t*)pool);
        g_stats.active_pools--;
        return UROAM_OK;
    }

    return UROAM_ERROR_INVALID_PARAM;
}

int uroam_pool_alloc(void *pool, size_t size, void **out_ptr) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    if (!pool || !out_ptr) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    *out_ptr = memopt_pool_alloc((memopt_pool_t*)pool, size);

    if (*out_ptr) {
        return UROAM_OK;
    }

    return UROAM_ERROR_OUT_OF_MEMORY;
}

int uroam_pool_free(void *pool, void *ptr) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    if (!pool || !ptr) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    memopt_pool_free((memopt_pool_t*)pool, ptr);
    return UROAM_OK;
}

int uroam_get_stats(uroam_stats_t *stats) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    if (!stats) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    memopt_stats_t internal_stats;
    memopt_get_stats(&internal_stats);

    stats->total_allocated = internal_stats.total_allocated;
    stats->total_freed = internal_stats.total_freed;
    stats->current_used = internal_stats.pool_used;
    stats->peak_used = g_stats.peak_used;
    stats->pool_count = g_stats.pool_count;
    stats->active_pools = g_stats.active_pools;
    stats->compression_ratio = internal_stats.compression_ratio;
    stats->ksm_pages_shared = internal_stats.ksm_pages_shared;
    stats->ksm_pages_sharing = internal_stats.ksm_pages_sharing;

    return UROAM_OK;
}

int uroam_reset_stats(void) {
    if (!g_initialized) {
        return UROAM_ERROR_NOT_INITIALIZED;
    }

    memset(&g_stats, 0, sizeof(g_stats));
    return UROAM_OK;
}

int uroam_get_numa_node_count(void) {
    return memopt_numa_node_count();
}

int uroam_get_numa_node_info(int node, uroam_numa_node_t *info) {
    if (!info || node < 0) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    int node_count = memopt_numa_node_count();
    if (node >= node_count) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    info->node_id = node;
    info->total_memory = memopt_numa_node_memory(node);
    info->free_memory = info->total_memory;
    info->cpu_count = 1;

    return UROAM_OK;
}

int uroam_enable_ksm(void) {
    return memopt_enable_ksm();
}

int uroam_disable_ksm(void) {
    return memopt_disable_ksm();
}

int uroam_get_ksm_stats(uint64_t *shared, uint64_t *sharing) {
    if (!shared || !sharing) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    memopt_stats_t stats;
    memopt_get_stats(&stats);

    *shared = stats.ksm_pages_shared;
    *sharing = stats.ksm_pages_sharing;

    return UROAM_OK;
}

int uroam_enable_zswap(void) {
    return memopt_tune_zswap();
}

int uroam_disable_zswap(void) {
    return UROAM_OK;
}

int uroam_set_zswap_params(int pool_percent, const char *compressor) {
    (void)pool_percent;
    (void)compressor;
    return UROAM_OK;
}

int uroam_enable_hugepages(void) {
    return memopt_enable_hugepages();
}

int uroam_disable_hugepages(void) {
    return memopt_disable_hugepages();
}

int uroam_set_memory_limit(uint64_t bytes) {
    (void)bytes;
    g_config.memory_limit = bytes;
    return UROAM_OK;
}

int uroam_get_memory_limit(uint64_t *bytes) {
    if (!bytes) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    *bytes = g_config.memory_limit;
    return UROAM_OK;
}

int uroam_madvise_mergeable(void *addr, size_t size) {
    if (!addr) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    return memopt_merge_pages(addr, size);
}

int uroam_madvise_unmergeable(void *addr, size_t size) {
    if (!addr) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    return memopt_unmerge_pages(addr, size);
}

int uroam_madvise_dontneed(void *addr, size_t size) {
    if (!addr) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    return madvise(addr, size, MADV_DONTNEED);
}

typedef struct {
    uroam_metrics_callback_t callback;
    void *user_data;
} metrics_callback_entry_t;

#define MAX_CALLBACKS 16
static metrics_callback_entry_t g_callbacks[MAX_CALLBACKS];
static int g_callback_count = 0;

int uroam_register_metrics_callback(uroam_metrics_callback_t callback, void *user_data) {
    if (!callback || g_callback_count >= MAX_CALLBACKS) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    g_callbacks[g_callback_count].callback = callback;
    g_callbacks[g_callback_count].user_data = user_data;
    g_callback_count++;

    return UROAM_OK;
}

int uroam_unregister_metrics_callback(uroam_metrics_callback_t callback) {
    if (!callback) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    for (int i = 0; i < g_callback_count; i++) {
        if (g_callbacks[i].callback == callback) {
            for (int j = i; j < g_callback_count - 1; j++) {
                g_callbacks[j] = g_callbacks[j + 1];
            }
            g_callback_count--;
            return UROAM_OK;
        }
    }

    return UROAM_ERROR_INVALID_PARAM;
}

int uroam_tune_for_workload(uroam_workload_t workload) {
    return uroam_set_workload(workload);
}

int uroam_get_error_string(int error, char *buf, size_t len) {
    if (!buf || len == 0) {
        return UROAM_ERROR_INVALID_PARAM;
    }

    const char *msg = "Unknown error";

    switch (error) {
    case UROAM_OK:
        msg = "Success";
        break;
    case UROAM_ERROR:
        msg = "General error";
        break;
    case UROAM_ERROR_NOT_INITIALIZED:
        msg = "UROAM not initialized";
        break;
    case UROAM_ERROR_ALREADY_INITIALIZED:
        msg = "UROAM already initialized";
        break;
    case UROAM_ERROR_INVALID_PARAM:
        msg = "Invalid parameter";
        break;
    case UROAM_ERROR_OUT_OF_MEMORY:
        msg = "Out of memory";
        break;
    case UROAM_ERROR_NOT_SUPPORTED:
        msg = "Feature not supported";
        break;
    case UROAM_ERROR_PERMISSION_DENIED:
        msg = "Permission denied";
        break;
    }

    strncpy(buf, msg, len - 1);
    buf[len - 1] = '\0';

    return UROAM_OK;
}

bool uroam_is_initialized(void) {
    return g_initialized;
}

bool uroam_is_supported(void) {
    return true;
}

const char *uroam_get_error_message(void) {
    return g_error_message;
}