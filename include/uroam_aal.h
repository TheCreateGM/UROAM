#ifndef UROAM_AAL_H
#define UROAM_AAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AAL_VERSION_MAJOR 1
#define AAL_VERSION_MINOR 0
#define AAL_VERSION_PATCH 0

#define AAL_MAX_CPUS 256
#define AAL_MAX_NUMA_NODES 16
#define AAL_PAGE_SIZE_4KB   4096
#define AAL_PAGE_SIZE_16KB  16384
#define AAL_PAGE_SIZE_64KB  65536
#define AAL_PAGE_SIZE_2MB   (2*1024*1024)
#define AAL_PAGE_SIZE_16MB (16*1024*1024)
#define AAL_PAGE_SIZE_1GB  (1024*1024*1024)

#define AAL_CACHE_LINE_MIN  32
#define AAL_CACHE_LINE_MAX 256

#define AAL_SIMD_NONE    0
#define AAL_SIMD_SSE     1
#define AAL_SIMD_SSE2    2
#define AAL_SIMD_SSE3    3
#define AAL_SIMD_SSSE3  4
#define AAL_SIMD_SSE4_1 5
#define AAL_SIMD_SSE4_2 6
#define AAL_SIMD_AVX    7
#define AAL_SIMD_AVX2   8
#define AAL_SIMD_AVX512 9
#define AAL_SIMD_NEON   10
#define AAL_SIMD_RVV    11

#define AAL_ENDIAN_LITTLE 0
#define AAL_ENDIAN_BIG    1

#define AAL_ATOMIC_RELAXED 0
#define AAL_ATOMIC_ACQUIRE 1
#define AAL_ATOMIC_RELEASE 2
#define AAL_ATOMIC_SEQ_CST 3

typedef enum {
    AAL_ARCH_X86_64 = 0,
    AAL_ARCH_ARM64,
    AAL_ARCH_ARM32,
    AAL_ARCH_RISCV64,
    AAL_ARCH_PPC64LE,
    AAL_ARCH_S390X,
    AAL_ARCH_LOONGARCH64,
    AAL_ARCH_UNKNOWN
} aal_arch_t;

typedef enum {
    AAL_PAGE_SIZE_UNKNOWN = 0,
    AAL_PAGE_SIZE_4KB_FLAG = 1,
    AAL_PAGE_SIZE_2MB_FLAG = 2,
    AAL_PAGE_SIZE_1GB_FLAG = 4,
    AAL_PAGE_SIZE_16KB_FLAG = 8,
    AAL_PAGE_SIZE_64KB_FLAG = 16
} aal_page_size_flag_t;

typedef enum {
    AAL_MEM_MODEL_FLAT = 0,
    AAL_MEM_MODEL_NEAR,
    AAL_MEM_MODEL_UMA,
    AAL_MEM_MODEL_NUMA
} aal_mem_model_t;

typedef struct {
    aal_arch_t arch;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    char vendor[16];
    char brand[64];
} aal_cpu_info_t;

typedef struct {
    uint64_t page_size;
    uint64_t huge_page_size;
    aal_page_size_flag_t supported_pages;
    bool has_transparent_hugepages;
    bool has_explicit_hugepages;
} aal_memory_info_t;

typedef struct {
    uint32_t cache_line_size;
    uint32_t l1d_cache_size;
    uint32_t l2_cache_size;
    uint32_t l3_cache_size;
    uint32_t l1i_cache_size;
    uint32_t l1d_assoc;
    uint32_t l2_assoc;
    uint32_t l3_assoc;
} aal_cache_info_t;

typedef struct {
    uint32_t simd_level;
    bool has_sse;
    bool has_sse2;
    bool has_sse3;
    bool has_ssse3;
    bool has_sse4_1;
    bool has_sse4_2;
    bool has_avx;
    bool has_avx2;
    bool has_avx512f;
    bool has_avx512bw;
    bool has_avx512dq;
    bool has_avx512vl;
    bool has_avx512cd;
    bool has_neon;
    bool has_neon_dotprod;
    bool has_neon_i8mm;
    bool has_rvv;
} aal_simd_info_t;

typedef struct {
    uint32_t node_count;
    uint32_t distances[AAL_MAX_NUMA_NODES][AAL_MAX_NUMA_NODES];
    uint64_t node_mem_size[AAL_MAX_NUMA_NODES];
    bool has_local_memory[AAL_MAX_CPUS];
    uint32_t node_of_cpu[AAL_MAX_CPUS];
} aal_numa_info_t;

typedef struct {
    uint32_t endianness;
    bool is_little_endian;
    bool is_big_endian;
} aal_endian_info_t;

typedef struct {
    aal_cpu_info_t cpu;
    aal_memory_info_t memory;
    aal_cache_info_t cache;
    aal_simd_info_t simd;
    aal_numa_info_t numa;
    aal_endian_info_t endian;
    aal_mem_model_t mem_model;
    uint32_t cpu_count;
    uint32_t online_cpu_count;
    uint32_t thread_count;
    uint64_t total_ram_kb;
    uint64_t base_page_size;
} aal_system_info_t;

typedef struct {
    volatile uint64_t value;
} aal_atomic64_t;

typedef struct {
    volatile uint32_t value;
} aal_atomic32_t;

typedef struct {
    volatile uintptr_t value;
} aal_atomic_ptr_t;

typedef void (*aal_barrier_func_t)(void);
typedef uint64_t (*aal_fence_func_t)(void);

void aal_init(void);
void aal_shutdown(void);

const char* aal_get_version(void);
const char* aal_get_arch_name(aal_arch_t arch);

bool aal_cpu_has_sse(void);
bool aal_cpu_has_sse2(void);
bool aal_cpu_has_sse3(void);
bool aal_cpu_has_ssse3(void);
bool aal_cpu_has_sse4_1(void);
bool aal_cpu_has_sse4_2(void);
bool aal_cpu_has_avx(void);
bool aal_cpu_has_avx2(void);
bool aal_cpu_has_avx512f(void);
bool aal_cpu_has_neon(void);
bool aal_cpu_has_rvv(void);

uint64_t aal_get_page_size(void);
uint64_t aal_get_huge_page_size(void);
uint32_t aal_get_cache_line_size(void);
uint32_t aal_get_cpu_count(void);
uint32_t aal_get_numa_node_count(void);
uint64_t aal_get_total_ram_kb(void);

int aal_get_numa_node_of_cpu(uint32_t cpu);
int aal_get_numa_node_of_addr(const void* addr);
uint64_t aal_get_numa_node_mem_size(uint32_t node);

bool aal_is_little_endian(void);

void aal_mb_full(void);
void aal_mb(void);
void aal_rmb(void);
void aal_wmb(void);
void aal_compiler_barrier(void);

uint64_t aal_atomic_load64(const aal_atomic64_t* atom);
void aal_atomic_store64(aal_atomic64_t* atom, uint64_t value);
uint64_t aal_atomic_add64(aal_atomic64_t* atom, uint64_t value);
uint64_t aal_atomic_sub64(aal_atomic64_t* atom, uint64_t value);
uint64_t aal_atomic_inc64(aal_atomic64_t* atom);
uint64_t aal_atomic_dec64(aal_atomic64_t* atom);
uint64_t aal_atomic_and64(aal_atomic64_t* atom, uint64_t value);
uint64_t aal_atomic_or64(aal_atomic64_t* atom, uint64_t value);
bool aal_atomic_cas64(aal_atomic64_t* atom, uint64_t* expected, uint64_t desired);
bool aal_atomic_cas32(aal_atomic32_t* atom, uint32_t* expected, uint32_t desired);
uint64_t aal_atomic_swap64(aal_atomic64_t* atom, uint64_t value);

uint32_t aal_atomic_load32(const aal_atomic32_t* atom);
void aal_atomic_store32(aal_atomic32_t* atom, uint32_t value);
uint32_t aal_atomic_add32(aal_atomic32_t* atom, uint32_t value);
uint32_t aal_atomic_sub32(aal_atomic32_t* atom, uint32_t value);
uint32_t aal_atomic_inc32(aal_atomic32_t* atom);
uint32_t aal_atomic_dec32(aal_atomic32_t* atom);

uintptr_t aal_atomic_load_ptr(const aal_atomic_ptr_t* atom);
void aal_atomic_store_ptr(aal_atomic_ptr_t* atom, uintptr_t value);

void aal_byte_swap16(void* data, size_t len);
void aal_byte_swap32(void* data, size_t len);
void aal_byte_swap64(void* data, size_t len);
void aal_byte_swap128(void* data, size_t len);

void* aal_memset(void* s, int c, size_t n);
void* aal_memcpy(void* dest, const void* src, size_t n);
int aal_memcmp(const void* s1, const void* s2, size_t n);

void* aal_alloc_aligned(size_t size, size_t alignment);
void aal_free_aligned(void* ptr);

uint64_t aal_rdtsc(void);
uint64_t aal_rdtscp(void);

void aal_prefetch_read(const void* ptr);
void aal_prefetch_write(void* ptr);
void aal_prefetch_read_local(const void* ptr);
void aal_prefetch_write_local(void* ptr);

void aal_get_system_info(aal_system_info_t* info);

#ifdef __cplusplus
}
#endif

#endif