# UROAM - Unified RAM Optimization and Management Framework

## Overview

UROAM is a production-grade, low-level system framework for Linux that optimizes RAM utilization across heterogeneous workloads including AI/ML, gaming (Steam/Proton), rendering, and general applications.

## Features

- **Cross-Architecture Support**: x86_64, ARM64, ARM32, RISC-V64, PPC64LE, S390X, LoongArch64
- **Architecture Abstraction Layer (AAL)**: Runtime detection of page size, hugepages, cache line, SIMD, NUMA, endianness
- **Intelligent Classification**: Real-time workload detection (AI, Gaming, Compilation, Interactive, Background, Idle)
- **Adaptive Optimization**: Compression (ZRAM/Zswap), deduplication (KSM), cache management
- **Gaming Mode**: Steam/Proton/Wine detection, mlock critical pages, preserve shader cache
- **Idle Optimization**: Automatic cold page compression, cache dropping, THP hints
- **Transparent Operation**: No application modifications required
- **Performance Focused**: <2% CPU overhead, <1ms latency impact

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Applications                      │
└──────────────────────┬───────────────────────────┘
                       │
┌──────────────────────▼───────────────────────────┐
│              UROAM Daemon (uroamd)                │
│  ┌─────────────┬─────────────┬─────────────┐  │
│  │  Classifier  │ Compression │  Optimizer  │  │
│  └─────────────┴─────────────┴─────────────┘  │
└──────────────────────┬───────────────────────────┘
                       │
┌──────────────────────▼───────────────────────────┐
│         Architecture Abstraction Layer (AAL)        │
│  (Atomics, Barriers, SIMD, NUMA, Byte-Swap)    │
└──────────────────────┬───────────────────────────┘
                       │
┌──────────────────────▼───────────────────────────┐
│              Linux Kernel (≥5.15)                  │
│  (MGLRU, THP, ZRAM, Zswap, KSM, cgroups v2)  │
└─────────────────────────────────────────────────────┘
```

## Components

### 1. Architecture Abstraction Layer (AAL)
- Runtime detection of CPU features (SIMD: SSE→AVX-512, NEON, RVV)
- Page size detection (4KB, 16KB, 64KB, 2MB, 1GB)
- Cache line size detection
- NUMA topology detection
- Portable atomics and memory barriers
- Cross-architecture byte-swap primitives

### 2. Kernel Module (`uroam.ko`)
- MGLRU support hooks
- Memory pressure monitoring via procfs
- Transparent Hugepage management
- KSM integration
- Workqueue-based periodic scanning

### 3. eBPF Module
- Memory allocation tracking (`__alloc_pages_nodemask`, `kmem_cache_alloc`)
- mmap/brk system call interception
- Perf event output for userspace analysis

### 4. Daemon (`uroamd`)
- Initializes: Config → AAL → Kernel → ZRAM → Profile → Classification
- Polls `/proc` every 500ms
- Manages OOM scores and cgroups v2
- Unix socket IPC for CLI communication

### 5. CLI (`ramctl`)
- `status` - Show current memory status
- `optimize` - Trigger immediate optimization
- `profile set/get` - Manage workload profiles
- `clean` - Aggressive memory cleanup
- `monitor` - Real-time monitoring mode
- `zram` - ZRAM control
- `config reload` - Reload configuration

## Profiles

| Profile | Swappiness | ZRAM Algo | ZRAM % | KSM | THP | Cache Pressure |
|---------|------------|-----------|--------|-----|-----|----------------|
| AI | 5 | zstd | 50% | Yes | always | 100 |
| Gaming | 1 | lz4 | 30% | No | always | 50 |
| Balanced | 60 | lz4 | 25% | Yes | madvise | 100 |
| Powersaver | 90 | zstd | 60% | Yes | always | 25 |

## Quick Start

### Prerequisites

- Linux kernel ≥ 5.15
- GCC ≥ 12 or LLVM ≥ 15
- CMake ≥ 3.25, Ninja (optional)
- Rust ≥ 1.75 (for daemon/CLI)
- Python ≥ 3.10 (optional, for tests)

### Building

```bash
# Using install script (recommended)
chmod +x install.sh
./install.sh

# Manual build
mkdir build && cd build
cmake ..
ninja

# Build kernel module
cd kmod
make

# Build Rust components
source $HOME/.cargo/env
cargo build --release
```

### Installation

```bash
# Debian/Ubuntu
sudo dpkg -i ../uroam_1.0.0_amd64.deb

# Fedora/RHEL
sudo rpm -ivh ../uroam-1.0.0.x86_64.rpm

# Arch Linux (AUR)
paru -S uroam

# From source
sudo ./install.sh
```

### Usage

```bash
# Check status
ramctl status

# View configuration
ramctl config show

# Monitor in real-time
ramctl monitor

# Set profile
sudo ramctl profile set gaming

# Trigger optimization
sudo ramctl optimize

# Clean memory
sudo ramctl clean
```

### Daemon Control

```bash
# Enable service
sudo systemctl enable uroam

# Start service
sudo systemctl start uroam

# Check status
sudo systemctl status uroam

# View logs
sudo journalctl -u uroam -f
```

## Configuration

Location: `/etc/uroam/uroam.toml`

```toml
[general]
enabled = true
log_level = "info"
socket_path = "/run/uroam/uroamd.sock"
polling_interval_ms = 500

[zram]
enabled = true
size_percent = 50
num_devices = "auto"
default_algorithm = "lz4"

[zswap]
enabled = true
max_pool_percent = 20

[profiles.ai]
swappiness = 5
zram_algorithm = "zstd"
zram_size_percent = 50
ksm_enabled = true
thp_mode = "always"

[profiles.gaming]
swappiness = 1
zram_algorithm = "lz4"
zram_size_percent = 30
ksm_enabled = false
thp_mode = "always"

[profiles.balanced]
swappiness = 60
zram_algorithm = "lz4"
zram_size_percent = 25
ksm_enabled = true
thp_mode = "madvise"

[profiles.powersaver]
swappiness = 90
zram_algorithm = "zstd"
zram_size_percent = 60
ksm_enabled = true
thp_mode = "always"

[tmpfs]
build_dir = "/tmp/uroam-build"
build_size = "8G"
ai_cache_dir = "/tmp/uroam-ai-cache"
ai_cache_size = "16G"

[idle]
idle_threshold_secs = 300
optimization_level = "normal"
preload_enabled = false
```

## Testing

### Unit Tests
```bash
# Run AAL tests
gcc -std=c11 -I include aal/aal.c tests/aal_test.c -o /tmp/aal_test
/tmp/aal_test

# Run integration tests
./tests/run_tests.sh
```

### Benchmarks
- **AI**: llama.cpp model load time, inference latency
- **Gaming**: FPS measurement with/without UROAM
- **Compilation**: Linux kernel compile time
- **General**: stress-ng memory tests

### Stress Tests
- 72-hour continuous operation
- Memory pressure scenarios
- Workload transition tests

## Project Structure

```
UROAM/
├── aal/                    # Architecture Abstraction Layer
│   ├── aal.c
│   └── CMakeLists.txt
├── kmod/                  # Kernel module
│   ├── uroam_kernel.c
│   └── Makefile
├── ebpf/                 # eBPF programs
│   ├── uroam.bpf.c
│   └── ebpf_loader.c
├── src/
│   ├── shared/           # C shared library (memopt)
│   ├── daemon/          # Rust daemon (uroamd)
│   ├── cli/             # Rust CLI (ramctl)
│   ├── gaming/          # Gaming optimizations
│   ├── idle/            # Idle optimizations
│   ├── ramdisk/         # RAM disk/cache management
│   ├── classification/  # Workload classifier
│   ├── compression/     # ZRAM/Zswap management
│   ├── kernel/          # Kernel interface
│   └── config/          # Configuration
├── include/              # Header files
│   ├── uroam_aal.h
│   ├── uroam.h
│   └── uroam_client.h
├── tests/               # Test suite
├── packaging/           # Distribution packages
│   ├── deb/
│   ├── rpm/
│   └── arch/
├── docs/                # Documentation
├── CMakeLists.txt
├── Cargo.toml
├── Makefile
├── install.sh
└── README.md
```

## Implementation Order

1. ✅ AAL (Architecture Abstraction Layer)
2. ✅ Kernel module/eBPF
3. ✅ Daemon core
4. ✅ Classifier
5. ✅ Compression
6. ✅ Kernel tuning
7. ✅ RAM disk/cache
8. ✅ Process optimization
9. ✅ Gaming
10. ✅ Idle
11. ✅ CLI
12. ⚠️ Toolchains (Rust build environment)
13. 🔄 Tests (≥80% coverage)
14. 🔄 Packaging (.deb/.rpm/PKGBUILD)
15. 🔄 Documentation

## License

GPLv3 - See LICENSE file

## Contributing

See CONTRIBUTING.md for guidelines.

## Support

- GitHub: https://github.com/TheCreateGM/UROAM
- Issues: https://github.com/TheCreateGM/UROAM/issues
