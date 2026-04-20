#define _GNU_SOURCE
#include "memopt.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>

static void* (*_REAL_MALLOC_)(size_t) = NULL;
static void (*_REAL_FREE_)(void*) = NULL;
static void* (*_REAL_REALLOC_)(void*, size_t) = NULL;
static void* (*_REAL_CALLOC_)(size_t, size_t) = NULL;
static bool constructor_called = false;

__attribute__((constructor))
static void memopt_init_constructor(void) {
    _REAL_MALLOC_ = dlsym(RTLD_NEXT, "malloc");
    _REAL_FREE_ = dlsym(RTLD_NEXT, "free");
    _REAL_REALLOC_ = dlsym(RTLD_NEXT, "realloc");
    _REAL_CALLOC_ = dlsym(RTLD_NEXT, "calloc");
    constructor_called = true;
}

__attribute__((destructor))
static void memopt_shutdown_destructor(void) {
    // Don't call shutdown - let the OS clean up
}

// Exported malloc intercept
void* malloc(size_t size) {
    void* ptr = memopt_alloc(size);
    if (ptr) {
        return ptr;
    }
    
    if (!_REAL_MALLOC_) {
        _REAL_MALLOC_ = dlsym(RTLD_NEXT, "malloc");
    }
    return _REAL_MALLOC_(size);
}

void free(void* ptr) {
    if (!ptr) return;
    
    // Always use memopt_free - it handles both pool and mmap allocations
    // memopt_free checks the header to determine how to free
    memopt_free(ptr);
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    return memopt_realloc(ptr, size);
}

void* calloc(size_t nmemb, size_t size) {
    void* ptr = memopt_calloc(nmemb, size);
    if (ptr) {
        return ptr;
    }
    
    if (!_REAL_CALLOC_) {
        _REAL_CALLOC_ = dlsym(RTLD_NEXT, "calloc");
    }
    return _REAL_CALLOC_(nmemb, size);
}
