#define _GNU_SOURCE
#include "memopt.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/sysinfo.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#ifndef MPOL_BIND
#define MPOL_BIND 1
#endif
#ifndef MPOL_INTERLEAVE
#define MPOL_INTERLEAVE 3
#endif
#ifndef MPOL_PREFERRED
#define MPOL_PREFERRED 4
#endif
#ifndef MPOL_MF_MOVE
#define MPOL_MF_MOVE 2
#endif

#ifdef HAVE_NUMA
#include <numa.h>
#endif
#ifdef HAVE_NUMAIF
#include <numaif.h>
#endif

#define NUMA_MAX_NODES 64
#define MIN_PAGE_SIZE 4096

typedef struct {
    int node_id;
    size_t total_memory;
    size_t free_memory;
    int cpu_count;
} numa_node_info_t;

static numa_node_info_t numa_nodes[NUMA_MAX_NODES];
static int numa_node_count = 0;
static int current_numa_node = -1;
static bool numa_initialized = false;
static pthread_mutex_t numa_lock = PTHREAD_MUTEX_INITIALIZER;

static int detect_numa_topology(void) {
#ifdef HAVE_NUMA
    if (numa_available() < 0) {
        return 0;
    }

    numa_node_count = numa_max_node() + 1;
    if (numa_node_count > NUMA_MAX_NODES) {
        numa_node_count = NUMA_MAX_NODES;
    }

    for (int i = 0; i < numa_node_count; i++) {
        numa_nodes[i].node_id = i;
        numa_nodes[i].total_memory = numa_node_size(i, NULL);
        numa_nodes[i].free_memory = numa_nodes[i].total_memory;

        struct bitmask *mask = numa_allocate_nodemask();
        if (mask) {
            numa_nodes[i].cpu_count = numa_num_cpus_nodes(i, mask);
            numa_free_nodemask(mask);
        }
    }

    return numa_node_count;
#else
    numa_nodes[0].node_id = 0;
    numa_nodes[0].total_memory = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
    numa_nodes[0].free_memory = numa_nodes[0].total_memory;
    numa_nodes[0].cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    numa_node_count = 1;
    return 1;
#endif
}

int memopt_numa_node_count(void) {
    pthread_mutex_lock(&numa_lock);
    if (!numa_initialized) {
        numa_node_count = detect_numa_topology();
        numa_initialized = true;
    }
    pthread_mutex_unlock(&numa_lock);
    return numa_node_count;
}

int memopt_numa_node_of_cpu(int cpu) {
#ifdef HAVE_NUMA
    if (numa_available() < 0) {
        return 0;
    }
    return numa_node_of_cpu(cpu);
#else
    (void)cpu;
    return 0;
#endif
}

size_t memopt_numa_node_memory(int node) {
    if (node < 0 || node >= numa_node_count) {
        return 0;
    }
    return numa_nodes[node].total_memory;
}

int memopt_bind_to_node(int node) {
#ifdef HAVE_NUMA
    if (numa_available() < 0) {
        return -1;
    }

    struct bitmask *mask = numa_allocate_nodemask();
    if (!mask) {
        return -1;
    }

    numa_bitmask_setbit(mask, node);
    int ret = numa_run_on_node(node);
    numa_free_nodemask(mask);
    return ret;
#else
    (void)node;
    return -1;
#endif
}

int memopt_alloc_on_node(void **ptr, size_t size, int node) {
    if (!ptr || size == 0) {
        return -1;
    }

#ifdef HAVE_NUMA
    if (numa_available() >= 0) {
        *ptr = numa_alloc(size, node);
        if (*ptr) {
            return 0;
        }
    }
#endif

    *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (*ptr == MAP_FAILED) {
        *ptr = NULL;
        return -1;
    }

    return 0;
}

int memopt_migrate_pages(void *ptr, size_t size, int from_node, int to_node) {
    (void)ptr;
    (void)size;
    (void)from_node;
    (void)to_node;
    return 0;
}

int memopt_get_mempolicy(int *mode, void **addr, size_t *max_items, int node) {
#ifdef HAVE_NUMA
    if (numa_available() < 0) {
        return -1;
    }
    return get_mempolicy(mode, (unsigned long *)addr, max_items, node, 0);
#else
    (void)mode;
    (void)addr;
    (void)max_items;
    (void)node;
    return -1;
#endif
}

int memopt_set_mempolicy(int mode, const unsigned long *nmask, unsigned long maxnode) {
#ifdef HAVE_NUMA
    if (numa_available() < 0) {
        return -1;
    }
    return set_mempolicy(mode, nmask, maxnode);
#else
    (void)mode;
    (void)nmask;
    (void)maxnode;
    return -1;
#endif
}

int memopt_bind_mempolicy(int node) {
    unsigned long nodemask = 1UL << node;
    return memopt_set_mempolicy(MPOL_BIND, &nodemask, numa_node_count);
}

int memopt_interleave_mempolicy(int node_count) {
    unsigned long nodemask = 0;
    for (int i = 0; i < node_count && i < NUMA_MAX_NODES; i++) {
        nodemask |= (1UL << i);
    }
    return memopt_set_mempolicy(MPOL_INTERLEAVE, &nodemask, node_count);
}

int memopt_prefer_node_mempolicy(int node) {
    unsigned long nodemask = 1UL << node;
    return memopt_set_mempolicy(MPOL_PREFERRED, &nodemask, numa_node_count);
}

void* memopt_numa_aware_alloc(size_t size, int preferred_node) {
    int node_count = memopt_numa_node_count();
    if (node_count <= 1) {
        return memopt_alloc(size);
    }

    int target_node = preferred_node >= 0 ? preferred_node : 0;

    if (preferred_node >= 0 && preferred_node < node_count) {
        if (numa_nodes[preferred_node].free_memory > size) {
            void *ptr = NULL;
            if (memopt_alloc_on_node(&ptr, size, target_node) == 0) {
                numa_nodes[target_node].free_memory -= size;
                return ptr;
            }
        }
    }

    for (int i = 0; i < node_count; i++) {
        int node = (target_node + i) % node_count;
        if (numa_nodes[node].free_memory > size) {
            void *ptr = NULL;
            if (memopt_alloc_on_node(&ptr, size, node) == 0) {
                numa_nodes[node].free_memory -= size;
                return ptr;
            }
        }
    }

    return memopt_alloc(size);
}

void memopt_numa_aware_free(void *ptr, size_t size, int node) {
    if (!ptr) return;

    if (node >= 0 && node < numa_node_count) {
        numa_nodes[node].free_memory += size;
    }

    free(ptr);
}

int memopt_get_local_node(void) {
    if (current_numa_node >= 0) {
        return current_numa_node;
    }

    int cpu = sched_getcpu();
    if (cpu >= 0) {
        current_numa_node = memopt_numa_node_of_cpu(cpu);
    }

    if (current_numa_node < 0) {
        current_numa_node = 0;
    }

    return current_numa_node;
}

void memopt_numa_stats(memopt_numa_node_info_t *nodes, int *count) {
    (void)nodes;
    if (count) *count = 1;
}