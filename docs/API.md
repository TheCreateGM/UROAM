# API Reference

## Library API (C)

### Initialization

```c
int memopt_init(void);
int memopt_init_with_policy(memopt_workload_t workload);
void memopt_shutdown(void);
```

Initialize the memory optimizer. `memopt_init_with_policy()` accepts a workload type:
- `MEMOPT_WORKLOAD_GENERAL` - Default optimization
- `MEMOPT_WORKLOAD_AI_INFERENCE` - Low latency
- `MEMOPT_WORKLOAD_AI_TRAINING` - High throughput
- `MEMOPT_WORKLOAD_GAMING` - Responsiveness
- `MEMOPT_WORKLOAD_BACKGROUND` - Compression

### Allocation

```c
void* memopt_alloc(size_t size);
void* memopt_alloc_flags(size_t size, uint64_t flags);
void memopt_free(void* ptr);
void* memopt_realloc(void* ptr, size_t size);
void* memopt_calloc(size_t nmemb, size_t size);
```

Standard allocation functions. Flags:
- `MEMOPT_ALLOC_HUGEPAGE` - Use huge pages
- `MEMOPT_ALLOC_NUMA_AWARE` - NUMA-aware allocation
- `MEMOPT_ALLOC_ZEROED` - Zero-initialize
- `MEMOPT_ALLOC_SHARED` - Shared memory
- `MEMOPT_ALLOC_LOW_LATENCY` - Low latency priority
- `MEMOPT_ALLOC_HIGH_THROUGHPUT` - High throughput

### Pool Management

```c
memopt_pool_t* memopt_pool_create(size_t size, uint64_t flags, int numa_node);
void memopt_pool_destroy(memopt_pool_t* pool);
void* memopt_pool_alloc(memopt_pool_t* pool, size_t size);
void memopt_pool_free(memopt_pool_t* pool, void* ptr);
```

Create and manage custom memory pools.

### System Optimization

```c
int memopt_enable_ksm(void);
int memopt_disable_ksm(void);
int memopt_set_swappiness(int value);
int memopt_tune_zswap(void);
int memopt_enable_hugepages(void);
```

Kernel-level optimizations.

### Statistics

```c
void memopt_get_stats(memopt_stats_t* stats);
void memopt_print_stats(void);
```

Get memory statistics.

## CLI Commands

### Enable Optimizations

```bash
memopt enable
```

Enable all memory optimizations (KSM, zswap, THP).

### Disable Optimizations

```bash
memopt disable
```

Disable all memory optimizations.

### Tune Workload

```bash
memopt tune inference   # AI inference
memopt tune training  # AI training
memopt tune gaming   # Gaming
memopt tune general  # General purpose
```

Apply workload-specific tuning.

### Statistics

```bash
memopt stats
```

Show current system statistics.

### Daemon Control

```bash
memopt daemon start   # Start daemon
memopt daemon stop  # Stop daemon
memopt daemon status # Check status
```

Manage the optimization daemon.

## Environment Variables

- `MEMOPT_WORKLOAD` - Set workload type (inference, training, gaming)

## LD_PRELOAD Usage

```bash
LD_PRELOAD=/usr/local/lib/libmemopt.so your-application
```

Or link against the library:

```c
#include <memopt.h>

int main() {
    memopt_init_with_policy(MEMOPT_WORKLOAD_AI_INFERENCE);
    void* data = memopt_alloc(1024 * 1024);
    memopt_free(data);
    memopt_shutdown();
    return 0;
}
```