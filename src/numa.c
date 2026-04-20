#define _GNU_SOURCE
#include "memopt.h"
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <sys/sysinfo.h>

int memopt_numa_node_count(void) {
#ifdef HAVE_NUMA
    if (numa_available() < 0) {
        return 1;
    }
    return numa_max_node() + 1;
#else
    return 1;
#endif
}

int memopt_numa_node_of_cpu(int cpu) {
#ifdef HAVE_NUMA
    if (numa_available() < 0) {
        return 0;
    }
    return numa_node_of_cpu(cpu);
#else
    return 0;
#endif
}

size_t memopt_numa_node_memory(int node) {
#ifdef HAVE_NUMA
    if (numa_available() < 0) {
        return sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
    }
    return numa_node_size(node, NULL);
#else
    (void)node;
    return sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
#endif
}