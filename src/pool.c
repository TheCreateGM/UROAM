#define _GNU_SOURCE
#include "memopt.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

memopt_pool_t* memopt_pool_create(size_t size, uint64_t flags, int numa_node) {
    (void)numa_node;

    memopt_pool_t* pool = mmap(NULL, sizeof(memopt_pool_t), 
                                PROT_READ | PROT_WRITE, 
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pool == MAP_FAILED) return NULL;

    memset(pool, 0, sizeof(memopt_pool_t));
    pool->size = size;
    pool->flags = flags;
    pool->numa_node = numa_node;
    pool->used = 0;
    pool->free_list = NULL;

    int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (flags & MEMOPT_ALLOC_HUGEPAGE) {
        mmap_flags |= MAP_HUGETLB;
    }

    pool->base = mmap(NULL, size, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
    if (pool->base == MAP_FAILED) {
        if (flags & MEMOPT_ALLOC_HUGEPAGE) {
            mmap_flags &= ~MAP_HUGETLB;
            pool->base = mmap(NULL, size, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
        }
        if (pool->base == MAP_FAILED) {
            munmap(pool, sizeof(memopt_pool_t));
            return NULL;
        }
    }

    if (madvise(pool->base, size, MADV_MERGEABLE) != 0) {
        perror("madvise MERGEABLE");
    }

    if (pthread_mutex_init(&pool->lock, NULL) != 0) {
        munmap(pool->base, size);
        munmap(pool, sizeof(memopt_pool_t));
        return NULL;
    }

    return pool;
}

void memopt_pool_destroy(memopt_pool_t* pool) {
    if (!pool) return;

    void* base = pool->base;
    if (base && base != MAP_FAILED) {
        munmap(base, pool->size);
    }
    pthread_mutex_destroy(&pool->lock);
    munmap(pool, sizeof(memopt_pool_t));
}

void* memopt_pool_alloc(memopt_pool_t* pool, size_t size) {
    if (!pool || size == 0) return NULL;

    // Align to page size
    size = (size + MEMOPT_PAGE_SIZE - 1) & ~(MEMOPT_PAGE_SIZE - 1);

    pthread_mutex_lock(&pool->lock);
    
    if (pool->used + size > pool->size) {
        pthread_mutex_unlock(&pool->lock);
        return NULL;
    }

    void* ptr = (char*)pool->base + pool->used;
    pool->used += size;

    pthread_mutex_unlock(&pool->lock);
    return ptr;
}

void memopt_pool_free(memopt_pool_t* pool, void* ptr) {
    if (!pool || !ptr) return;
    
    pthread_mutex_lock(&pool->lock);
    
    memopt_free_block_t* block = (memopt_free_block_t*)ptr;
    
    // MADV_DONTNEED to release physical pages back to kernel
    if (madvise(ptr, block->size, MADV_DONTNEED) != 0) {
        perror("madvise DONTNEED");
    }
    
    // Find actual size if it was a bump allocation
    char* base = (char*)pool->base;
    char* ptr_char = (char*)ptr;
    size_t offset = ptr_char - base;
    
    if (offset + MEMOPT_PAGE_SIZE <= pool->used) {
        block->size = MEMOPT_PAGE_SIZE;
    } else {
        block->size = pool->used - offset;
    }
    
    // Insert into free list in sorted order
    memopt_free_block_t **prev = &pool->free_list;
    memopt_free_block_t *curr = pool->free_list;
    
    while (curr && curr < block) {
        prev = &curr->next;
        curr = curr->next;
    }
    
    // Coalesce with previous block if adjacent
    if (*prev && (char*)(*prev) + (*prev)->size == (char*)block) {
        (*prev)->size += block->size;
        block = *prev;
        *prev = (*prev)->next;
    }
    
    // Coalesce with next block if adjacent
    if (curr && (char*)block + block->size == (char*)curr) {
        block->size += curr->size;
        block->next = curr->next;
    } else {
        block->next = curr;
    }
    
    *prev = block;
    pthread_mutex_unlock(&pool->lock);
}
