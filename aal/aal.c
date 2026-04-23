#define _GNU_SOURCE
#include "uroam_aal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <dirent.h>
#include <sys/types.h>
#include <time.h>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

static aal_system_info_t g_aal_info;
static bool g_aal_initialized = false;

static const char* aal_arch_names[] = {
    "x86_64", "arm64", "arm32", "riscv64",
    "ppc64le", "s390x", "loongarch64", "unknown"
};

static void aal_detect_cpu_x86_64(void) {
    g_aal_info.cpu.arch = AAL_ARCH_X86_64;
    g_aal_info.cpu.family = 0;
    g_aal_info.cpu.model = 0;
    g_aal_info.cpu.stepping = 0;
    memset(g_aal_info.cpu.vendor, 0, sizeof(g_aal_info.cpu.vendor));
    memset(g_aal_info.cpu.brand, 0, sizeof(g_aal_info.cpu.brand));

#if defined(__x86_64__) || defined(__i386__)
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
        memcpy(g_aal_info.cpu.vendor, &ebx, 4);
        memcpy(g_aal_info.cpu.vendor + 4, &ecx, 4);
        memcpy(g_aal_info.cpu.vendor + 8, &edx, 4);
    }
    
    if (eax >= 1) {
        __cpuid(1, eax, ebx, ecx, edx);
        g_aal_info.cpu.family = ((eax >> 8) & 0xF) + ((eax >> 20) & 0xFF);
        g_aal_info.cpu.model = ((eax >> 4) & 0xF) | ((eax >> 12) & 0xF0);
        g_aal_info.cpu.stepping = eax & 0xF;
        
        g_aal_info.simd.has_sse = (edx >> 25) & 1;
        g_aal_info.simd.has_sse2 = (edx >> 26) & 1;
        g_aal_info.simd.has_sse3 = (ecx >> 0) & 1;
        g_aal_info.simd.has_ssse3 = (ecx >> 9) & 1;
        g_aal_info.simd.has_sse4_1 = (ecx >> 19) & 1;
        g_aal_info.simd.has_sse4_2 = (ecx >> 20) & 1;
        g_aal_info.simd.has_avx = (ecx >> 28) & 1;
        
        if (eax >= 7) {
            __cpuid(7, eax, ebx, ecx, edx);
            g_aal_info.simd.has_avx2 = (ebx >> 5) & 1;
            g_aal_info.simd.has_avx512f = (ebx >> 16) & 1;
            g_aal_info.simd.has_avx512bw = (ebx >> 30) & 1;
            g_aal_info.simd.has_avx512dq = (ebx >> 17) & 1;
            g_aal_info.simd.has_avx512vl = (ebx >> 1) & 1;
            g_aal_info.simd.has_avx512cd = (ebx >> 28) & 1;
        }
        
        if (g_aal_info.simd.has_avx512f) g_aal_info.simd.simd_level = AAL_SIMD_AVX512;
        else if (g_aal_info.simd.has_avx2) g_aal_info.simd.simd_level = AAL_SIMD_AVX2;
        else if (g_aal_info.simd.has_avx) g_aal_info.simd.simd_level = AAL_SIMD_AVX;
        else if (g_aal_info.simd.has_sse4_2) g_aal_info.simd.simd_level = AAL_SIMD_SSE4_2;
        else if (g_aal_info.simd.has_sse4_1) g_aal_info.simd.simd_level = AAL_SIMD_SSE4_1;
        else if (g_aal_info.simd.has_ssse3) g_aal_info.simd.simd_level = AAL_SIMD_SSSE3;
        else if (g_aal_info.simd.has_sse3) g_aal_info.simd.simd_level = AAL_SIMD_SSE3;
        else if (g_aal_info.simd.has_sse2) g_aal_info.simd.simd_level = AAL_SIMD_SSE2;
        else if (g_aal_info.simd.has_sse) g_aal_info.simd.simd_level = AAL_SIMD_SSE;
        else g_aal_info.simd.simd_level = AAL_SIMD_NONE;
    }
#endif
}

static void aal_detect_cpu_arm64(void) {
    g_aal_info.cpu.arch = AAL_ARCH_ARM64;
    g_aal_info.simd.has_neon = true;
    g_aal_info.simd.simd_level = AAL_SIMD_NEON;
#if defined(__ARM_FEATURE_CRYPTO)
    g_aal_info.simd.has_neon_dotprod = true;
#endif
}

static void aal_detect_cpu_arm32(void) {
    g_aal_info.cpu.arch = AAL_ARCH_ARM32;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    g_aal_info.simd.has_neon = true;
    g_aal_info.simd.simd_level = AAL_SIMD_NEON;
#endif
}

static void aal_detect_cpu_riscv64(void) {
    g_aal_info.cpu.arch = AAL_ARCH_RISCV64;
#if defined(__riscv_v)
    g_aal_info.simd.has_rvv = true;
    g_aal_info.simd.simd_level = AAL_SIMD_RVV;
#endif
}

static void aal_detect_cpu_ppc64le(void) {
    g_aal_info.cpu.arch = AAL_ARCH_PPC64LE;
}

static void aal_detect_cpu_s390x(void) {
    g_aal_info.cpu.arch = AAL_ARCH_S390X;
}

static void aal_detect_cpu_loongarch64(void) {
    g_aal_info.cpu.arch = AAL_ARCH_LOONGARCH64;
}

static void aal_detect_cpu_default(void) {
    g_aal_info.cpu.arch = AAL_ARCH_UNKNOWN;
    g_aal_info.simd.simd_level = AAL_SIMD_NONE;
}

static void aal_detect_cpu(void) {
    memset(&g_aal_info.cpu, 0, sizeof(g_aal_info.cpu));
    memset(&g_aal_info.simd, 0, sizeof(g_aal_info.simd));
    
#if defined(__x86_64__) || defined(_M_X64)
    aal_detect_cpu_x86_64();
#elif defined(__aarch64__) || defined(_M_ARM64)
    aal_detect_cpu_arm64();
#elif defined(__arm__)
    aal_detect_cpu_arm32();
#elif defined(__riscv) && defined(__riscv64)
    aal_detect_cpu_riscv64();
#elif defined(__powerpc64__)
    aal_detect_cpu_ppc64le();
#elif defined(__s390x__)
    aal_detect_cpu_s390x();
#elif defined(__loongarch64)
    aal_detect_cpu_loongarch64();
#else
    aal_detect_cpu_default();
#endif
}

static void aal_detect_memory(void) {
    g_aal_info.memory.page_size = AAL_PAGE_SIZE_4KB;
    g_aal_info.memory.huge_page_size = AAL_PAGE_SIZE_2MB;
    g_aal_info.memory.supported_pages = AAL_PAGE_SIZE_4KB_FLAG;
    g_aal_info.memory.has_transparent_hugepages = false;
    g_aal_info.memory.has_explicit_hugepages = false;
    
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size > 0) {
        g_aal_info.memory.page_size = (uint64_t)page_size;
        g_aal_info.memory.supported_pages = AAL_PAGE_SIZE_4KB_FLAG;
    }
    
    if (access("/sys/kernel/mm/transparent_hugepage/enabled", R_OK) == 0) {
        g_aal_info.memory.has_transparent_hugepages = true;
    }
    
    if (access("/proc/self/numa_maps", R_OK) == 0) {
        g_aal_info.mem_model = AAL_MEM_MODEL_NUMA;
    }
    
    g_aal_info.base_page_size = g_aal_info.memory.page_size;
}

static void aal_detect_cache(void) {
    g_aal_info.cache.cache_line_size = 64;
    g_aal_info.cache.l1d_cache_size = 32 * 1024;
    g_aal_info.cache.l2_cache_size = 256 * 1024;
    g_aal_info.cache.l3_cache_size = 8 * 1024 * 1024;
    
    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strstr(line, "cache size")) {
                char* colon = strchr(line, ':');
                if (colon) {
                    int size = atoi(colon + 1);
                    if (strstr(line, "L1")) {
                        if (strstr(line, "d")) {
                            g_aal_info.cache.l1d_cache_size = size * 1024;
                        }
                    } else if (strstr(line, "L2")) {
                        g_aal_info.cache.l2_cache_size = size * 1024;
                    } else if (strstr(line, "L3")) {
                        g_aal_info.cache.l3_cache_size = size * 1024;
                    }
                }
            }
        }
        fclose(fp);
    }
}

static void aal_detect_numa(void) {
    g_aal_info.numa.node_count = 1;
    memset(g_aal_info.numa.distances, 0, sizeof(g_aal_info.numa.distances));
    memset(g_aal_info.numa.node_mem_size, 0, sizeof(g_aal_info.numa.node_mem_size));
    
    if (access("/sys/bus/node/devices", R_OK) != 0) {
        g_aal_info.numa.node_mem_size[0] = g_aal_info.total_ram_kb * 1024;
        return;
    }
    
    DIR* dir = opendir("/sys/bus/node/devices");
    if (!dir) {
        g_aal_info.numa.node_mem_size[0] = g_aal_info.total_ram_kb * 1024;
        return;
    }
    
    struct dirent* entry;
    char path[256];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "node", 4) == 0) {
            int node_id = atoi(entry->d_name + 4);
            if (node_id >= 0 && node_id < AAL_MAX_NUMA_NODES) {
                snprintf(path, sizeof(path), "/sys/bus/node/devices/%s/meminfo", entry->d_name);
                FILE* fp = fopen(path, "r");
                if (fp) {
                    char line[128];
                    while (fgets(line, sizeof(line), fp) != NULL) {
                        if (strstr(line, "MemTotal:")) {
                            char* kbp = strstr(line, "kB");
                            if (kbp) {
                                while (kbp > line && *(kbp-1) != ' ') kbp--;
                                while (*kbp >= '0' && *kbp <= '9') kbp++;
                                g_aal_info.numa.node_mem_size[node_id] = atoll(kbp) * 1024;
                            }
                        }
                    }
                    fclose(fp);
                }
            }
        }
    }
    closedir(dir);
    
    if (g_aal_info.numa.node_mem_size[0] == 0) {
        g_aal_info.numa.node_mem_size[0] = g_aal_info.total_ram_kb * 1024;
    }
    
    g_aal_info.numa.node_count = 1;
    for (int i = 0; i < AAL_MAX_NUMA_NODES; i++) {
        if (g_aal_info.numa.node_mem_size[i] > 0) {
            g_aal_info.numa.node_count = i + 1;
        }
    }
}

static void aal_detect_endian(void) {
    union { uint32_t i; uint8_t c[4]; } test;
    test.i = 0x01020304;
    
    if (test.c[0] == 4) {
        g_aal_info.endian.endianness = AAL_ENDIAN_LITTLE;
        g_aal_info.endian.is_little_endian = true;
        g_aal_info.endian.is_big_endian = false;
    } else {
        g_aal_info.endian.endianness = AAL_ENDIAN_BIG;
        g_aal_info.endian.is_little_endian = false;
        g_aal_info.endian.is_big_endian = true;
    }
}

static void aal_detect_cpu_count(void) {
    g_aal_info.cpu_count = (uint32_t)sysconf(_SC_NPROCESSORS_CONF);
    g_aal_info.online_cpu_count = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
    g_aal_info.thread_count = g_aal_info.online_cpu_count;
    
    if (g_aal_info.cpu_count == 0 || g_aal_info.cpu_count > AAL_MAX_CPUS) {
        g_aal_info.cpu_count = 1;
        g_aal_info.online_cpu_count = 1;
    }
}

static void aal_detect_total_ram(void) {
    g_aal_info.total_ram_kb = (uint64_t)sysconf(_SC_PHYS_PAGES) * 
                             (uint64_t)sysconf(_SC_PAGESIZE) / 1024;
}

void aal_init(void) {
    if (g_aal_initialized) return;
    
    memset(&g_aal_info, 0, sizeof(g_aal_info));
    g_aal_info.mem_model = AAL_MEM_MODEL_FLAT;
    
    aal_detect_total_ram();
    aal_detect_cpu();
    aal_detect_memory();
    aal_detect_cache();
    aal_detect_numa();
    aal_detect_endian();
    aal_detect_cpu_count();
    
    g_aal_initialized = true;
}

void aal_shutdown(void) {
    g_aal_initialized = false;
}

const char* aal_get_version(void) { return "1.0.0"; }

const char* aal_get_arch_name(aal_arch_t arch) {
    if (arch < AAL_ARCH_UNKNOWN) return aal_arch_names[arch];
    return "unknown";
}

bool aal_cpu_has_sse(void) { return g_aal_info.simd.has_sse; }
bool aal_cpu_has_sse2(void) { return g_aal_info.simd.has_sse2; }
bool aal_cpu_has_sse3(void) { return g_aal_info.simd.has_sse3; }
bool aal_cpu_has_ssse3(void) { return g_aal_info.simd.has_ssse3; }
bool aal_cpu_has_sse4_1(void) { return g_aal_info.simd.has_sse4_1; }
bool aal_cpu_has_sse4_2(void) { return g_aal_info.simd.has_sse4_2; }
bool aal_cpu_has_avx(void) { return g_aal_info.simd.has_avx; }
bool aal_cpu_has_avx2(void) { return g_aal_info.simd.has_avx2; }
bool aal_cpu_has_avx512f(void) { return g_aal_info.simd.has_avx512f; }
bool aal_cpu_has_neon(void) { return g_aal_info.simd.has_neon; }
bool aal_cpu_has_rvv(void) { return g_aal_info.simd.has_rvv; }

uint64_t aal_get_page_size(void) { return g_aal_info.memory.page_size; }
uint64_t aal_get_huge_page_size(void) { return g_aal_info.memory.huge_page_size; }
uint32_t aal_get_cache_line_size(void) { return g_aal_info.cache.cache_line_size; }
uint32_t aal_get_cpu_count(void) { return g_aal_info.cpu_count; }
uint32_t aal_get_numa_node_count(void) { return g_aal_info.numa.node_count; }
uint64_t aal_get_total_ram_kb(void) { return g_aal_info.total_ram_kb; }

int aal_get_numa_node_of_cpu(uint32_t cpu) {
    if (cpu < AAL_MAX_CPUS) return g_aal_info.numa.node_of_cpu[cpu];
    return 0;
}

int aal_get_numa_node_of_addr(const void* addr) {
    (void)addr;
    return 0;
}

uint64_t aal_get_numa_node_mem_size(uint32_t node) {
    if (node < AAL_MAX_NUMA_NODES) return g_aal_info.numa.node_mem_size[node];
    return 0;
}

bool aal_is_little_endian(void) {
    return g_aal_info.endian.is_little_endian;
}

void aal_mb_full(void) {
#if defined(__x86_64__)
    __asm__ __volatile__("mfence" ::: "memory");
#elif defined(__aarch64__)
    __asm__ __volatile__("dmb ishst" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

void aal_mb(void) {
#if defined(__x86_64__)
    __asm__ __volatile__("lock; addl $0,0(%%rsp)" ::: "memory", "cc");
#elif defined(__aarch64__)
    __asm__ __volatile__("dmb ish" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

void aal_rmb(void) {
#if defined(__x86_64__)
    __asm__ __volatile__("lfence" ::: "memory");
#elif defined(__aarch64__)
    __asm__ __volatile__("dmb ishld" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

void aal_wmb(void) {
#if defined(__x86_64__)
    __asm__ __volatile__("sfence" ::: "memory");
#elif defined(__aarch64__)
    __asm__ __volatile__("dmb ishst" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

void aal_compiler_barrier(void) {
    __asm__ __volatile__("" ::: "memory");
}

uint64_t aal_atomic_load64(const aal_atomic64_t* atom) {
    return __atomic_load_n(&atom->value, __ATOMIC_SEQ_CST);
}

void aal_atomic_store64(aal_atomic64_t* atom, uint64_t value) {
    __atomic_store_n(&atom->value, value, __ATOMIC_SEQ_CST);
}

uint64_t aal_atomic_add64(aal_atomic64_t* atom, uint64_t value) {
    return __atomic_fetch_add(&atom->value, value, __ATOMIC_SEQ_CST);
}

uint64_t aal_atomic_sub64(aal_atomic64_t* atom, uint64_t value) {
    return __atomic_fetch_sub(&atom->value, value, __ATOMIC_SEQ_CST);
}

uint64_t aal_atomic_inc64(aal_atomic64_t* atom) {
    return __atomic_fetch_add(&atom->value, 1, __ATOMIC_SEQ_CST);
}

uint64_t aal_atomic_dec64(aal_atomic64_t* atom) {
    return __atomic_fetch_sub(&atom->value, 1, __ATOMIC_SEQ_CST);
}

uint64_t aal_atomic_and64(aal_atomic64_t* atom, uint64_t value) {
    return __atomic_fetch_and(&atom->value, value, __ATOMIC_SEQ_CST);
}

uint64_t aal_atomic_or64(aal_atomic64_t* atom, uint64_t value) {
    return __atomic_fetch_or(&atom->value, value, __ATOMIC_SEQ_CST);
}

bool aal_atomic_cas64(aal_atomic64_t* atom, uint64_t* expected, uint64_t desired) {
    return __atomic_compare_exchange_n(&atom->value, expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

bool aal_atomic_cas32(aal_atomic32_t* atom, uint32_t* expected, uint32_t desired) {
    return __atomic_compare_exchange_n(&atom->value, expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

uint64_t aal_atomic_swap64(aal_atomic64_t* atom, uint64_t value) {
    return __atomic_exchange_n(&atom->value, value, __ATOMIC_SEQ_CST);
}

uint32_t aal_atomic_load32(const aal_atomic32_t* atom) {
    return __atomic_load_n(&atom->value, __ATOMIC_SEQ_CST);
}

void aal_atomic_store32(aal_atomic32_t* atom, uint32_t value) {
    __atomic_store_n(&atom->value, value, __ATOMIC_SEQ_CST);
}

uint32_t aal_atomic_add32(aal_atomic32_t* atom, uint32_t value) {
    return __atomic_fetch_add(&atom->value, value, __ATOMIC_SEQ_CST);
}

uint32_t aal_atomic_sub32(aal_atomic32_t* atom, uint32_t value) {
    return __atomic_fetch_sub(&atom->value, value, __ATOMIC_SEQ_CST);
}

uint32_t aal_atomic_inc32(aal_atomic32_t* atom) {
    return __atomic_fetch_add(&atom->value, 1, __ATOMIC_SEQ_CST);
}

uint32_t aal_atomic_dec32(aal_atomic32_t* atom) {
    return __atomic_fetch_sub(&atom->value, 1, __ATOMIC_SEQ_CST);
}

uintptr_t aal_atomic_load_ptr(const aal_atomic_ptr_t* atom) {
    return __atomic_load_n(&atom->value, __ATOMIC_SEQ_CST);
}

void aal_atomic_store_ptr(aal_atomic_ptr_t* atom, uintptr_t value) {
    __atomic_store_n(&atom->value, value, __ATOMIC_SEQ_CST);
}

void aal_byte_swap16(void* data, size_t len) {
    uint16_t* p = (uint16_t*)data;
    for (size_t i = 0; i < len / 2; i++) {
        p[i] = ((p[i] & 0xFF00) >> 8) | ((p[i] & 0x00FF) << 8);
    }
}

void aal_byte_swap32(void* data, size_t len) {
    uint32_t* p = (uint32_t*)data;
    for (size_t i = 0; i < len / 4; i++) {
        p[i] = ((p[i] & 0xFF000000) >> 24) |
               ((p[i] & 0x00FF0000) >> 8) |
               ((p[i] & 0x0000FF00) << 8) |
               ((p[i] & 0x000000FF) << 24);
    }
}

void aal_byte_swap64(void* data, size_t len) {
    uint64_t* p = (uint64_t*)data;
    for (size_t i = 0; i < len / 8; i++) {
        p[i] = ((p[i] & 0xFF00000000000000ULL) >> 56) |
               ((p[i] & 0x00FF000000000000ULL) >> 40) |
               ((p[i] & 0x0000FF0000000000ULL) >> 24) |
               ((p[i] & 0x000000FF00000000ULL) >> 8) |
               ((p[i] & 0x00000000FF000000ULL) << 8) |
               ((p[i] & 0x0000000000FF0000ULL) << 24) |
               ((p[i] & 0x000000000000FF00ULL) << 40) |
               ((p[i] & 0x00000000000000FFULL) << 56);
    }
}

void aal_byte_swap128(void* data, size_t len) {
    uint64_t* p = (uint64_t*)data;
    for (size_t i = 0; i < len / 8; i += 2) {
        uint64_t t0 = p[i];
        uint64_t t1 = p[i + 1];
        p[i] = ((t1 & 0xFF00000000000000ULL) >> 56) |
               ((t1 & 0x00FF000000000000ULL) >> 40) |
               ((t1 & 0x0000FF0000000000ULL) >> 24) |
               ((t1 & 0x000000FF00000000ULL) >> 8) |
               ((t1 & 0x00000000FF000000ULL) << 8) |
               ((t1 & 0x0000000000FF0000ULL) << 24) |
               ((t1 & 0x000000000000FF00ULL) << 40) |
               ((t1 & 0x00000000000000FFULL) << 56);
        p[i + 1] = ((t0 & 0xFF00000000000000ULL) >> 56) |
                   ((t0 & 0x00FF000000000000ULL) >> 40) |
                   ((t0 & 0x0000FF0000000000ULL) >> 24) |
                   ((t0 & 0x000000FF00000000ULL) >> 8) |
                   ((t0 & 0x00000000FF000000ULL) << 8) |
                   ((t0 & 0x0000000000FF0000ULL) << 24) |
                   ((t0 & 0x000000000000FF00ULL) << 40) |
                   ((t0 & 0x00000000000000FFULL) << 56);
    }
}

void* aal_memset(void* s, int c, size_t n) { return memset(s, c, n); }
void* aal_memcpy(void* dest, const void* src, size_t n) { return memcpy(dest, src, n); }
int aal_memcmp(const void* s1, const void* s2, size_t n) { return memcmp(s1, s2, n); }

void* aal_alloc_aligned(size_t size, size_t alignment) {
    void* ptr;
    if (posix_memalign(&ptr, alignment, size) != 0) return NULL;
    return ptr;
}

void aal_free_aligned(void* ptr) { if (ptr) free(ptr); }

uint64_t aal_rdtsc(void) {
#if defined(__x86_64__)
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

uint64_t aal_rdtscp(void) {
#if defined(__x86_64__)
    unsigned int lo, hi, aux;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return ((uint64_t)hi << 32) | lo;
#else
    return aal_rdtsc();
#endif
}

void aal_prefetch_read(const void* ptr) {
#if defined(__x86_64__)
    __asm__ __volatile__("prefetcht0 %0" : : "m"(*(const char*)ptr));
#elif defined(__aarch64__)
    __asm__ __volatile__("prfm pldl1keep, [%0]" : : "r"(ptr));
#endif
}

void aal_prefetch_write(void* ptr) {
#if defined(__x86_64__)
    __asm__ __volatile__("prefetcht1 %0" : : "m"(*(char*)ptr));
#elif defined(__aarch64__)
    __asm__ __volatile__("prfm pstl1keep, [%0]" : : "r"(ptr));
#endif
}

void aal_prefetch_read_local(const void* ptr) {
#if defined(__aarch64__)
    __asm__ __volatile__("prfm pldl1strm, [%0]" : : "r"(ptr));
#endif
}

void aal_prefetch_write_local(void* ptr) {
#if defined(__aarch64__)
    __asm__ __volatile__("prfm pstl1strm, [%0]" : : "r"(ptr));
#endif
}

void aal_get_system_info(aal_system_info_t* info) {
    if (!g_aal_initialized) aal_init();
    memcpy(info, &g_aal_info, sizeof(aal_system_info_t));
}