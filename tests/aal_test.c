#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "uroam_aal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name(void)
#define RUN(name) do { \
    printf("Running " #name "... "); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED: %s at line %d\n", #cond, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

TEST(init_shutdown) {
    aal_init();
    aal_system_info_t info;
    aal_get_system_info(&info);
    ASSERT(info.total_ram_kb > 0);
    aal_shutdown();
}

TEST(version) {
    const char* ver = aal_get_version();
    ASSERT(ver != NULL);
    ASSERT(strlen(ver) > 0);
    printf(" (v%s)", ver);
}

TEST(arch_detection) {
    aal_system_info_t info;
    aal_get_system_info(&info);
    ASSERT(info.cpu.arch != AAL_ARCH_UNKNOWN);
    printf(" (arch: %s)", aal_get_arch_name(info.cpu.arch));
}

TEST(page_size) {
    uint64_t page_size = aal_get_page_size();
    ASSERT(page_size >= 4096);
    printf(" (page_size: %lu)", page_size);
}

TEST(cache_line) {
    uint32_t cl_size = aal_get_cache_line_size();
    ASSERT(cl_size >= 32 && cl_size <= 256);
    printf(" (cache_line: %u)", cl_size);
}

TEST(cpu_count) {
    uint32_t cpu_count = aal_get_cpu_count();
    ASSERT(cpu_count >= 1);
    printf(" (cpus: %u)", cpu_count);
}

TEST(numa_nodes) {
    uint32_t nodes = aal_get_numa_node_count();
    ASSERT(nodes >= 1);
    printf(" (nodes: %u)", nodes);
}

TEST(total_ram) {
    uint64_t ram = aal_get_total_ram_kb();
    ASSERT(ram > 0);
    printf(" (ram: %lu KB)", ram);
}

TEST(endian) {
    bool little = aal_is_little_endian();
    ASSERT(little == true || little == false);
    printf(" (endian: %s)", little ? "little" : "big");
}

TEST(atomic_64) {
    aal_atomic64_t atom = { .value = 0 };
    
    aal_atomic_store64(&atom, 42);
    ASSERT(aal_atomic_load64(&atom) == 42);
    
    uint64_t old = aal_atomic_add64(&atom, 8);
    ASSERT(old == 42);
    ASSERT(aal_atomic_load64(&atom) == 50);
    
    old = aal_atomic_sub64(&atom, 10);
    ASSERT(old == 50);
    ASSERT(aal_atomic_load64(&atom) == 40);
}

TEST(atomic_32) {
    aal_atomic32_t atom = { .value = 0 };
    
    aal_atomic_store32(&atom, 100);
    ASSERT(aal_atomic_load32(&atom) == 100);
    
    aal_atomic_add32(&atom, 50);
    ASSERT(aal_atomic_load32(&atom) == 150);
}

TEST(atomic_cas) {
    aal_atomic64_t atom = { .value = 42 };
    uint64_t expected = 42;
    
    ASSERT(aal_atomic_cas64(&atom, &expected, 100) == true);
    ASSERT(aal_atomic_load64(&atom) == 100);
    
    expected = 999;
    ASSERT(aal_atomic_cas64(&atom, &expected, 200) == false);
    ASSERT(aal_atomic_load64(&atom) == 100);
}

TEST(barriers) {
    aal_mb_full();
    aal_mb();
    aal_rmb();
    aal_wmb();
    aal_compiler_barrier();
}

TEST(memcpy_fn) {
    const char src[] = "Hello, AAL!";
    char dest[32] = {0};
    
    void* result = aal_memcpy(dest, src, strlen(src) + 1);
    ASSERT(result == dest);
    ASSERT(strcmp(dest, src) == 0);
}

TEST(memset_fn) {
    char buf[16];
    aal_memset(buf, 'A', 10);
    buf[10] = '\0';
    ASSERT(strcmp(buf, "AAAAAAAAAA") == 0);
}

TEST(memcmp_fn) {
    char s1[] = "test";
    char s2[] = "test";
    char s3[] = "best";
    
    ASSERT(aal_memcmp(s1, s2, 4) == 0);
    ASSERT(aal_memcmp(s1, s3, 4) != 0);
}

TEST(byte_swap) {
    uint32_t val = 0x12345678;
    uint32_t original = val;
    aal_byte_swap32(&val, 4);
    
    ASSERT(val != original);
    
    aal_byte_swap32(&val, 4);
    ASSERT(val == original);
}

TEST(timestamp) {
    uint64_t ts1 = aal_rdtsc();
    for (volatile int i = 0; i < 1000; i++);
    uint64_t ts2 = aal_rdtsc();
    ASSERT(ts2 >= ts1);
}

TEST(prefetch_fn) {
    char buf[64];
    aal_prefetch_read(buf);
    aal_prefetch_write(buf);
    aal_prefetch_read_local(buf);
    aal_prefetch_write_local(buf);
}

TEST(aligned_alloc_fn) {
    void* ptr = aal_alloc_aligned(1024, 4096);
    ASSERT(ptr != NULL);
    
    uintptr_t addr = (uintptr_t)ptr;
    ASSERT((addr % 4096) == 0);
    
    aal_free_aligned(ptr);
}

TEST(numa_node_of_cpu) {
    uint32_t cpu_count = aal_get_cpu_count();
    int node = aal_get_numa_node_of_cpu(cpu_count - 1);
    ASSERT(node >= 0);
    printf(" (node: %d)", node);
}

TEST(simd_info) {
    aal_system_info_t info;
    aal_get_system_info(&info);
    printf(" (simd_level: %d)", info.simd.simd_level);
}

int main(void) {
    printf("=== UROAM AAL Test Suite ===\n\n");
    
    RUN(init_shutdown);
    RUN(version);
    RUN(arch_detection);
    RUN(page_size);
    RUN(cache_line);
    RUN(cpu_count);
    RUN(numa_nodes);
    RUN(total_ram);
    RUN(endian);
    RUN(atomic_64);
    RUN(atomic_32);
    RUN(atomic_cas);
    RUN(barriers);
    RUN(memcpy_fn);
    RUN(memset_fn);
    RUN(memcmp_fn);
    RUN(byte_swap);
    RUN(timestamp);
    RUN(prefetch_fn);
    RUN(aligned_alloc_fn);
    RUN(numa_node_of_cpu);
    RUN(simd_info);
    
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}