# UROAM Architecture Documentation

## System Overview

UROAM (Unified RAM Optimization & Management) is a cross-distribution, cross-architecture Linux framework that transparently optimizes RAM for AI, gaming, development, and general workloads.

## Design Principles

1. **No Application Changes**: Applications don't need modification
2. **Cross-Architecture**: Supports 7 CPU architectures via AAL
3. **Runtime Detection**: All hardware features detected at runtime
4. **Strict Order**: Components implemented in dependency order
5. **No Stubs**: All functionality fully implemented
6. **≥80% Test Coverage**: AAL, integration, benchmarks, stress tests

## Architecture Layers

### Layer 1: Architecture Abstraction Layer (AAL)

**Location**: `aal/aal.c`, `include/uroam_aal.h`

Runtime detection of:
- Page size (4KB, 16KB, 64KB, 2MB, 1GB)
- Hugepages (THP, explicit)
- Cache line size
- SIMD capabilities (SSE→AVX-512, NEON, RVV)
- NUMA topology
- Endianness
- Memory model (flat, NUMA)

Portable primitives:
- Atomics (32/64-bit, CAS, fetch-and-*)
- Memory barriers (mb, rmb, wmb, compiler barrier)
- Byte-swap (16/32/64/128-bit)
- Prefetch hints
- Aligned allocation

### Layer 2: Kernel Module

**Location**: `kmod/uroam_kernel.c`

Kernel-level hooks:
- MGLRU support detection and tuning
- Memory pressure monitoring via procfs (`/proc/uroam/uroam_status`)
- sysctl interface (`/proc/sys/uroam/`)
- KSM control
- Workqueue-based periodic scanning
- Transparent hugepage management

### Layer 3: eBPF Module

**Location**: `ebpf/uroam.bpf.c`, `ebpf/ebpf_loader.c`

eBPF programs for:
- Memory allocation tracking (`__alloc_pages_nodemask`, `kmem_cache_alloc`)
- mmap/brk system call interception
- Perf event output to userspace

### Layer 4: Daemon (uroamd)

**Location**: `src/daemon/main.rs`

Initialization sequence:
1. Load TOML configuration
2. Initialize AAL (architecture detection)
3. Initialize kernel interface
4. Set up ZRAM devices
5. Load profile (AI/Gaming/Balanced/Powersaver)
6. Initialize classifier
7. Enter main loop (poll /proc every 500ms)

Main loop:
- Scan processes
- Classify workload
- Apply optimizations
- Manage OOM scores
- Update cgroups v2
- Handle Unix socket IPC

### Layer 5: Workload Classifier

**Location**: `src/classification/mod.rs`

Classifies processes into:
- **AI Inference**: llama.cpp, ollama, PyTorch, TensorFlow
- **AI Training**: Training scripts, GPU workloads
- **Gaming**: Steam, Proton, Wine, gamescope
- **Compilation**: gcc, clang, rustc, cargo, make, ninja
- **Rendering**: Rendering engines
- **Interactive**: GNOME Shell, KWin, Xorg, browsers
- **Background**: kworker, ksoftirqd, migration
- **Idle**: No significant workload

Updates every 2 seconds.

### Layer 6: Memory Compression

**Location**: `src/compression/mod.rs`

ZRAM management:
- ~50% RAM default size
- Per-core/NUMA device support
- Algorithms: lz4 (default), zstd (AI), lzo (fallback)
- Runtime algorithm switching

Zswap management (optional):
- Pool size configuration
- Compressor selection

### Layer 7: Kernel Tuning

**Location**: `src/kernel/mod.rs`

Sysctl tunables:
- `vm.swappiness`
- `vm.vfs_cache_pressure`
- `vm.page-cluster`
- `vm.min_free_kbytes`
- THP mode (`always`, `madvise`, `never`)
- KSM control

### Layer 8: RAM Disk/Cache

**Location**: `src/ramdisk/mod.rs`

Dynamic tmpfs:
- Build directory (`/tmp/uroam-build`, 8GB default)
- AI cache directory (`/tmp/uroam-ai-cache`, 16GB default)
- Optional LRU/LFU in-memory cache with Redis RESP

### Layer 9: Process Optimization

**Location**: `src/optimization/mod.rs`

OOM score adjustment:
- High priority: -900 (AI, gaming, interactive)
- Low priority: +300/+500 (background)

cgroups v2 management:
- Memory limits per profile
- CPU affinity
- I/O priorities

### Layer 10: Gaming Optimization

**Location**: `src/gaming/mod.rs`

Features:
- Steam/Proton/Wine detection
- Swappiness → 1 (reduce swapping)
- Reserve RAM for gaming
- mlock critical pages
- Preserve shader cache
- Restore settings within 2 seconds

### Layer 11: Idle Optimization

**Location**: `src/idle/mod.rs`

Triggers after ~5 minutes of idle:
- Compress cold pages
- Drop caches (level 1/2/3)
- THP hints
- Cancel within 100ms on activity

### Layer 12: CLI (ramctl)

**Location**: `src/cli/main.rs`

Commands:
- `status` - Show memory/cache/ZRAM status
- `optimize` - Trigger immediate optimization
- `profile get/set` - Manage profiles
- `clean` - Aggressive memory cleanup
- `monitor` - Real-time monitoring
- `zram status/resize` - ZRAM control
- `config reload` - Reload configuration

## Data Flow

```
+-----------+
|  /proc     |  (process list, meminfo, pressure)
+-----+-----+
      |
      v
+-----+-----+
| Classifier |  (workload detection every 2s)
+-----+-----+
      |
      v
+-----+-----+
| Optimizer  |  (apply profile settings)
+-----+-----+
      |
      +---> +-----------+  (ZRAM/Zswap)
      |   | Compression|
      |   +-----------+
      |
      +---> +-----------+  (OOM, cgroups)
      |   | Process    |
      |   | Optimization|
      |   +-----------+
      |
      +---> +-----------+  (KSM, THP, sysctl)
          | Kernel     |
          | Tuning     |
          +-----------+
```

## Profiles

### AI Profile
- Swappiness: 5
- ZRAM algorithm: zstd
- ZRAM size: 50% RAM
- KSM: enabled (5000 pages/scan)
- THP: always
- Cache pressure: 100
- Background cgroup: 2G limit

### Gaming Profile
- Swappiness: 1
- ZRAM algorithm: lz4
- ZRAM size: 30% RAM
- KSM: disabled
- THP: always
- Cache pressure: 50
- Background cgroup: 4G limit

### Balanced Profile
- Swappiness: 60
- ZRAM algorithm: lz4
- ZRAM size: 25% RAM
- KSM: enabled (1000 pages/scan)
- THP: madvise
- Cache pressure: 100
- Background cgroup: 4G limit

### Powersaver Profile
- Swappiness: 90
- ZRAM algorithm: zstd
- ZRAM size: 60% RAM
- KSM: enabled (3000 pages/scan)
- THP: always
- Cache pressure: 25
- Background cgroup: 1G limit

## Build System

### CMake (C Components)
```bash
mkdir build && cd build
cmake ..
ninja
```

### Cargo (Rust Components)
```bash
source $HOME/.cargo/env
cargo build --release
```

### Kernel Module
```bash
cd kmod
make
```

## Testing Strategy

### AAL Correctness (22 tests)
- Init/shutdown
- Architecture detection
- Page size, cache line, CPU count
- NUMA detection
- Endianness
- Atomics (64/32-bit, CAS)
- Memory barriers
- Byte-swap
- Prefetch
- Aligned allocation

### Integration Tests
- C library (memopt) - allocation, pools, KSM, NUMA
- Kernel module loading/unloading
- eBPF program loading
- Daemon startup/shutdown
- CLI commands

### Benchmarks
- **AI**: llama.cpp model load time, inference latency
- **Gaming**: FPS measurement with/without UROAM
- **Compilation**: Linux kernel compile time
- **General**: stress-ng memory tests

### Stress Tests
- 72-hour continuous operation
- Memory pressure scenarios
- Workload transition tests (AI→Gaming→Idle)
- Cross-architecture validation

## Cross-Architecture Support

| Architecture | AAL Detection | Kernel Module | eBPF | Daemon | CLI |
|-------------|----------------|---------------|------|--------|-----|
| x86_64      | ✅             | ✅            | ✅   | ✅      | ✅  |
| ARM64       | ✅             | ✅            | ✅   | ✅      | ✅  |
| ARM32       | ✅             | ✅            | ⚠️  | ✅      | ✅  |
| RISC-V64    | ✅             | ✅            | ⚠️  | ✅      | ✅  |
| PPC64LE     | ✅             | ✅            | ⚠️  | ✅      | ✅  |
| s390x       | ✅             | ✅            | ⚠️  | ✅      | ✅  |
| LoongArch64 | ✅             | ✅            | ⚠️  | ✅      | ✅  |

## Files and Directories

```
UROAM/
├── aal/                    # Architecture Abstraction Layer
│   ├── aal.c
│   └── CMakeLists.txt
├── kmod/                   # Kernel module (uroam.ko)
│   ├── uroam_kernel.c
│   └── Makefile
├── ebpf/                  # eBPF programs
│   ├── uroam.bpf.c
│   └── ebpf_loader.c
├── src/
│   ├── shared/           # C shared library (memopt)
│   │   └── memopt.c
│   ├── daemon/          # Rust daemon (uroamd)
│   ├── cli/             # Rust CLI (ramctl)
│   ├── gaming/          # Gaming optimizations
│   ├── idle/            # Idle optimizations
│   ├── ramdisk/         # RAM disk/cache
│   ├── classification/  # Workload classifier
│   ├── compression/     # ZRAM/Zswap
│   ├── kernel/          # Kernel interface
│   ├── config/          # Configuration
│   └── lib.rs          # Rust library root
├── include/              # Header files
│   ├── uroam_aal.h    # AAL API
│   ├── uroam.h        # C library API
│   └── uroam_client.h
├── tests/               # Test suite
│   ├── aal_test.c     # AAL tests (22 tests)
│   ├── simple_test.c
│   ├── pool_test.c
│   ├── basic_test.c
│   └── unit/          # Python test stubs
├── packaging/           # Distribution packages
│   ├── deb/
│   ├── rpm/
│   └── arch/
├── docs/                # Documentation
│   └── architecture.md
├── CMakeLists.txt
├── Cargo.toml
├── Makefile
├── install.sh
├── README.md
└── LICENSE
```

## Implementation Order (Strict)

1. ✅ AAL → Architecture detection and portable primitives
2. ✅ Kernel module/eBPF → Kernel-level hooks
3. ✅ Daemon core → Main loop and IPC
4. ✅ Classifier → Workload detection
5. ✅ Compression → ZRAM/Zswap management
6. ✅ Kernel tuning → Sysctl and THP
7. ✅ RAM disk/cache → tmpfs and caching
8. ✅ Process optimization → OOM and cgroups
9. ✅ Gaming → Steam/Proton/Wine optimization
10. ✅ Idle → Cold page compression
11. ✅ CLI → User commands
12. ⚠️ Toolchains → Build system validation
13. ⚠️ Tests → ≥80% coverage
14. ⚠️ Packaging → .deb/.rpm/PKGBUILD
15. ⚠️ Documentation → Complete and accurate

## Configuration Validation

TOML config is strictly validated:
- `polling_interval_ms` ≥ 100
- `zram.size_percent` ≤ 100
- Profile values within valid ranges
- Required fields present
- Type checking on all values

## Performance Requirements

- CPU overhead: <2%
- Latency impact: <1ms gaming
- Memory reduction: 20-60%
- AI model load: 30-50% faster
- Test coverage: ≥80%
