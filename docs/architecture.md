# UROAM Architecture Design Document

## Version 1.0
## Date: April 2026

---

## 1. Executive Summary

UROAM (Unified RAM Optimization and Management) is a production-grade Linux system framework that optimizes RAM utilization across heterogeneous workloads including AI/ML, gaming, compilation, and general-purpose computing. The framework operates transparently below the application layer, intercepting and optimizing memory behavior through kernel interfaces, eBPF programs, and user-space policy enforcement.

This document specifies the complete architecture of UROAM, including all eight subsystems, their interfaces, internal data structures, and the data flows between them.

---

## 2. System Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        UROAM Architecture                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐             │
│  │   ramctl    │    │  External   │    │  Developers │             │
│  │    CLI      │    │   Tools     │    │    APIs     │             │
│  └──────┬──────┘    └──────┬──────┘    └──────┬──────┘             │
│         │                   │                   │                      │
│         └───────────────────┼───────────────────┘                      │
│                             │                                          │
│                    ┌────────▼────────┐                                 │
│                    │  UNIX Socket   │                                 │
│                    │  /run/uroam/  │                                 │
│                    │  uroamd.sock   │                                 │
│                    └────────┬───────┘                                 │
│                           │                                            │
│         ┌─────────────────┼─────────────────┐                         │
│         │                 │                 │                          │
│  ┌──────▼──────┐  ┌────▼─────┐  ┌──────▼──────┐                   │
│  │   Process    │  │  Memory  │  │    Idle    │                    │
│  │   Aware     │  │ Compression│  │Optimization│                     │
│  │   Engine    │  │   Layer   │  │  System    │                     │
│  └──────┬──────┘  └────┬─────┘  └──────┬──────┘                     │
│         │               │               │                              │
│  ┌──────▼─────────────▼───────────────▼──────┐                      │
��  │         User-Space Daemon (uroamd)           │                          │
│  │     - Configuration Management            │                          │
│  │     - Workload Classification          │                          │
│  │     - Policy Enforcement              │                          │
│  │     - External Tool Integration        │                          │
│  └──────────────────┬───────────────────┘                          │
│                      │                                               │
│         ┌────────────┼────────────┐                                   │
│         │            │            │                                    │
│  ┌──────▼─────┐ ┌──▼────┐ ┌───▼────┐                            │
│  │   Kernel   │  │ eBPF  │  │  Zram  │                            │
│  │  Module   │  │ Progs │  │/Zswap │                            │
│  │ (uroam.ko)│  └──────┘  └───────┘                            │
│  └──────┬──────┘                                                │
│         │                                                        │
│  ┌──────▼──────────────────────────────────┐                   │
│  │        Linux Kernel VMM Layer            │                   │
│  │   (vm.swappiness, drop_caches, MGLRU)   │                   │
│  └───────────────────────────────────────────┘                   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. Subsystem Specifications

### 3.1 Kernel-Level Optimization Layer (Subsystem 1)

**Responsibilities:**
- All interactions with Linux kernel virtual memory subsystem
- MGLRU detection and configuration (Linux 6.1+)
- Dynamic vm.swappiness tuning
- Adaptive vm.vfs_cache_pressure tuning
- Automatic drop_caches scheduling
- Memory pressure detection via eBPF

**Interface to User-Space:**
- Character device: `/dev/uroam`
- Netlink socket: `NETLINK_UROAM (31)`
- Procfs entries: `/proc/uroam/{status,config,stats}`
- Sysfs entries: `/sys/kernel/uroam/{enabled,mode,pressure}`

**Data Structures:**

```c
// /kernel/uroam.h:90-117
struct uroam_state {
    bool enabled;
    enum psi_pressure pressure_level;
    unsigned long last_pressure_time;
    u64 pressure_count;
    u64 allocation_count;
    u64 reclaim_count;
    struct list_head allocation_list;
    struct uroam_process_info tracked_processes[MAX_TRACKED_PROCESSES];
    u32 tracked_count;
    struct uroam_numa_info numa_nodes[MAX_NUMA_NODES];
    u32 numa_node_count;
    u8 optimization_mode;
    bool numa_aware;
    bool compression_enabled;
    bool dedup_enabled;
};
```

**Key Functions:**
- `uroam_notify_pressure()` - Notify user-space of pressure changes
- `uroam_netlink_send_pressure()` - Send pressure via netlink
- `uroam_pressure_check()` - Periodic pressure monitoring

---

### 3.2 Memory Compression Layer (Subsystem 2)

**Responsibilities:**
- Zram device management (creation, sizing, algorithm selection)
- Zswap configuration and pool management
- Per-device compression ratio monitoring
- Automatic algorithm switching based on performance

**Interface:**
- Sysfs paths:
  - `/sys/block/zram*/size`
  - `/sys/block/zram*/comp_algorithm`
  - `/sys/block/zram*/mm_stat`
  - `/sys/module/zswap/parameters/*`

**Zram Device Configuration:**

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| num_devices | CPU cores | 1-64 | Number of zram devices |
| device_size | 50% RAM | 1GB-100% RAM | Size per device |
| default_algorithm | lz4 | lz4,lzo,zstd | Default compression |
| latency_threshold | 1ms | 0.1-10ms | Algorithm switch trigger |
| ratio_threshold | 1.2 | 1.0-3.0 | Min compression ratio |

**Algorithm Selection Strategy:**

```
if (workload_type == AI_TRAINING || workload_type == COMPILATION) {
    // Throughput-oriented: prefer compression ratio
    algorithm = zstd
} else if (workload_type == GAMING || workload_type == INFERENCE) {
    // Latency-sensitive: prefer speed
    algorithm = lz4
} else {
    algorithm = lz4  // Default
}
```

---

### 3.3 RAM Disk and Caching Layer (Subsystem 3)

**Responsibilities:**
- tmpfs-based RAM disk management
- Build directory optimization
- AI model cache directories
- In-memory cache pool with Redis RESP compatibility

**Configuration:**

```toml
[tmpfs]
build_dir = "/tmp/uroam-build"
build_size = "8G"
ai_cache_dir = "/tmp/uroam-ai-cache"
ai_cache_size = "16G"
general_tmp_dir = "/tmp/uroam-tmp"
general_tmp_size = "4G"

[cache_pool]
enabled = true
max_size = "4G"
eviction_policy = "lru"  # lru or lfu
socket_path = "/run/uroam/cache.sock"
protocol = "redis"     # redis-compatible
```

**tmpfs Mount Points:**

| Path | Default Size | Trigger |
|------|-------------|---------|
| /tmp/uroam-build | 8GB | Compilation detected |
| /tmp/uroam-ai-cache | 16GB | AI framework detected |
| /tmp/uroam-tmp | 4GB | Always available |

---

### 3.4 User-Space Optimization Daemon (Subsystem 4)

**Binary Name:** `uroamd`
**Socket Path:** `/run/uroam/uroamd.sock`

**Startup Sequence:**

```
1. Parse /etc/uroam/uroam.conf
2. Validate configuration
3. Load kernel module or eBPF programs
4. Initialize zram devices
5. Read hardware profile
6. Establish memory baseline (5s sample)
7. Classify initial workload
8. Apply initial policies
9. Open UNIX socket
10. Enter main loop
```

**Main Loop:**

```
every 500ms:
    1. Read /proc/meminfo, /proc/vmstat
    2. Read per-process stats from /proc/[pid]/smaps_rollup
    3. Update workload classification
    4. Check for profile changes
    5. Apply memory policies
    6. Send stats to subscribers
```

**Signal Handling:**
- SIGTERM: Restore kernel parameters, cleanup, exit
- SIGHUP: Reload configuration
- SIGUSR1: Trigger immediate optimization
- SIGUSR2: Dump debug statistics

---

### 3.5 Process-Aware Optimization Engine (Subsystem 5)

**Workload Classification Categories:**

| Category | Priority | Detection Method |
|----------|----------|-----------------|
| AI_ML_INFERENCE | P0 | Process name + cmdline |
| COMPILATION | P1 | Process name + file I/O |
| GAMING | P1 | Steam/Proton detection |
| INTERACTIVE | P0 | EWMH _NET_ACTIVE_WINDOW |
| BACKGROUND | P2 | Default |
| IDLE | P3 | No activity threshold |

**Detection Rules:**

```rust
// Process name matching
let ai_processes = [
    "llama.cpp", "llama-server", "ollama", "ollama serve",
    "python", "python3" // with torch/tensorflow in cmdline
];

let gaming_processes = [
    "steam", "steamwebhelper", "proton", "wine",
    "wine64", "gamescope", "steam-runtime"
];

let compilation_processes = [
    "gcc", "g++", "clang", "clang++", "rustc",
    "cargo", "make", "ninja", "cmake"
];
```

**Process Priority Application:**

```
if (priority == P0) {
    oom_score_adj = -900
    // Protect from OOM killing
} else if (priority == P1) {
    oom_score_adj = -500
    // Hard to kill
} else if (priority == P2) {
    oom_score_adj = 100
    // Normal
} else {
    oom_score_adj = 500
    // Preferred for OOM killing
}
```

---

### 3.6 Steam and Gaming Optimization Layer (Subsystem 6)

**Detection Parameters:**

```toml
[gaming]
detect_steam = true
detect_proton = true
detect_wine = true
detect_gamescope = true

# Environment variables to check
STEAM_COMPAT_DATA_PATH
STEAM_RUNTIME
PROTON_NO_ESYNC
WINEDEBUG
```

**Gaming Profile Activations:**

| Parameter | Normal | Gaming | Description |
|------------|--------|-------|-------------|
| vm.swappiness | 60 | 5 | Prevent swap thrashing |
| vm.page-cluster | 2 | 0 | Low latency |
| oom_score_adj (game) | 0 | -900 | Protect game |
| oom_score_adj (other) | 0 | +300 | Prefer killing other |
| KSM | auto | disabled | No dedup during game |
| THP | madvise | always | Maximize huge pages |

**Activation Triggers:**
- Steam client process detected
- Proton process with STEAM_COMPAT_DATA_PATH
- Game executable started by Steam
- gamescope process running

**Deactivation:**
- All game + Steam + Proton processes terminated
- 2-second timeout for clean exit

---

### 3.7 Idle Optimization System (Subsystem 7)

**Idle Detection:**

```
idle = (
    no_P0_or_P1_processes &&
    memory_used_percent < 40 &&
    psi_full_10 < 1.0 &&
    psi_some_10 < 5.0
) || (
    no_input_events > idle_threshold
)
```

**Idle Operations Sequence:**

```
1. Identify cold pages (not accessed in 5min)
2. Move to zram via swap
3. Drop VFS cache if > 30% RAM
4. Drop page cache for non-critical files
5. Trigger khugepaged defrag
6. Preload likely libraries (optional)
```

**Cancellation:**
- Any P0 or P1 process becomes active
- Input event detected
- Memory pressure exceeds threshold

---

### 3.8 VRAM and RAM Coordination Layer (Subsystem 8) - Optional

**GPU Detection:**
- NVIDIA: NVML API (`nvidia-smi` compatible)
- AMD: ROCm SMI (`rocm-smi` compatible)
- Intel: Level Zero / NEO API

**Coordination Interface:**

```rust
struct VRAMInfo {
    pub total: u64,
    pub used: u64,
    pub free: u64,
    pub vendor: GPUVendor,  // Nvidia, Amd, Intel
}

pub trait VRAMMonitor {
    fn get_vram_info(&self) -> Result<VRAMInfo>;
    fn set_memory_limit(&self, limit: u64) -> Result<()>;
    fn enable_ emergency_swap(&self, enable: bool) -> Result<()>;
}
```

---

## 4. Optimization Profiles

### 4.1 AI Mode

```toml
[profiles.ai]
swappiness = 5
zram_algorithm = "zstd"
zram_size_percent = 50
ksm_enabled = true
ksm_scan_pages = 5000
thp_mode = "always"
cache_pressure = 100
background_cgroup_limit = "2G"
lock_memory = true
```

### 4.2 Gaming Mode

```toml
[profiles.gaming]
swappiness = 1
zram_algorithm = "lz4"
zram_size_percent = 30
ksm_enabled = false
thp_mode = "always"
cache_pressure = 50
min_free_kbytes = 65536
page_cluster = 0
```

### 4.3 Balanced Mode

```toml
[profiles.balanced]
swappiness = 60
zram_algorithm = "lz4"
zram_size_percent = 25
ksm_enabled = true
ksm_scan_pages = 1000
thp_mode = "madvise"
cache_pressure = 100
```

### 4.4 Power Saver Mode

```toml
[profiles.powersaver]
swappiness = 90
zram_algorithm = "zstd"
zram_size_percent = 60
ksm_enabled = true
ksm_scan_pages = 3000
thp_mode = "always"
polling_interval = "2s"
cache_pressure = 25
```

---

## 5. CLI Tool: ramctl

### 5.1 Command Specification

| Command | Description |
|---------|-------------|
| `ramctl status` | Display system memory status, active profile, workload classification |
| `ramctl optimize` | Trigger immediate optimization pass |
| `ramctl profile set ai\|gaming\|balanced\|powersaver` | Switch optimization profile |
| `ramctl profile get` | Display current profile parameters |
| `ramctl clean` | Aggressive one-time memory reclamation |
| `ramctl monitor` | Live monitoring mode (like htop) |
| `ramctl zram status` | Show zram device statistics |
| `ramctl zram resize <size>` | Resize zram device |
| `ramctl config reload` | Reload configuration file |

### 5.2 Output Format

```
=== UROAM Status ===
Mode: AI
Pressure: LOW
Memory: 8.2G / 16G (51.3%)
Swap: 512M / 8G
ZRAM: 4G (2.1x ratio, lz4)

Workload Classification:
  AI Inference: llama-server (PID 1234, 4.2G)
  Background: 45 processes (1.2G)

Active Policies:
  - vm.swappiness = 5
  - OOM protection: enabled
  - KSM: active
```

---

## 6. Configuration File Format

### 6.1 /etc/uroam/uroam.conf

```toml
[general]
enabled = true
log_level = "info"
log_file = "/var/log/uroam/uroamd.log"
socket_path = "/run/uroam/uroamd.sock"
polling_interval = "500ms"

[zram]
enabled = true
num_devices = "auto"  # or number
total_size_percent = 50
default_algorithm = "lz4"

[profiles.ai]
swappiness = 5
zram_algorithm = "zstd"
zram_size_percent = 50
# ... etc

[profiles.gaming]
# ... etc

[profiles.balanced]
# ... etc

[profiles.powersaver]
# ... etc

[process_rules]
# Custom process priority rules
# ["process_name", "priority"]

[idle]
idle_threshold = "5m"
optimization_level = "normal"
preload_enabled = false
```

---

## 7. Inter-Subsystem Data Flows

### 7.1 Memory Pressure Flow

```
Kernel VMM
    │
    ├── PSI metric changes
    │
    ▼
Kernel Module (uroam.ko)
    │
    ├── Read by pressure.c
    │
    ▼
uroam_notify_pressure()
    │
    ├── netlink_send to user-space
    │
    ▼
uroamd: pressure_event handler
    │
    ├── Classification engine update
    │
    ▼
Policy enforcement
    │──► vm.swappiness adjustment
    │──► zram allocation
    │──► cache drop triggers
```

### 7.2 Workload Classification Flow

```
/proc/[pid]/cmdline
/proc/[pid]/smaps_rollup
/proc/[pid]/environ
    │
    ▼
uroamd: Process Scanner (every 2s)
    │
    ├── Match process names
    ├── Check cmdline patterns
    ├── Analyze memory patterns
    │
    ▼
Classification Engine
    │
    ▼
Priority Assignment
    │──► oom_score_adj
    │──► cgroup limits
    │──► nice/ionice
```

---

## 8. Security Model

### 8.1 Capability Requirements

Required capabilities after initialization:
- `CAP_SYS_PTRACE` - Process inspection
- `CAP_SYS_ADMIN` - cgroup management
- `CAP_IPC_LOCK` - mlock operations

Dropped capabilities:
- All others

### 8.2 Socket Access Control

```
Socket: /run/uroam/uroamd.sock
Permissions: 0770 (owner: root, group: uroam)

Allowed callers:
- UID 0 (root)
- GID uroam
```

### 8.3 Configuration File Security

```
/etc/uroam/uroam.conf
Owner: root:root
Permissions: 0640

Refuse to start if:
- Not owned by root
- World-writable
- Group-writable by non-uroam group
```

---

## 9. Build System

### 9.1 Component Build Order

```
1. Kernel module (C)
   make -C kernel/ KDIR=/lib/modules/$(uname -r)/build
   
2. eBPF programs (C with libbpf)
   clang -target bpf -O2 -I include/ ebpf/uroam.bpf.c
   
3. Rust daemon and CLI
   cargo build --release
   
4. Python plugins and scripts
   python -m py_compile src/python/*.py
```

### 9.2 Package Artifacts

```
uroam_1.0.0_amd64.deb
uroam-1.0.0-1.x86_64.rpm
uroam-1.0.0-1.pkg.tar.zst (Arch)
uroam-1.0.0-x86_64-linux.tar.gz (generic)
```

---

## 10. Testing Strategy

### 10.1 Unit Test Coverage Targets

| Component | Target |
|-----------|--------|
| Configuration parser | 95% |
| Classification engine | 90% |
| CLI commands | 85% |
| Kernel module | 80% |

### 10.2 Integration Tests

```
test_daemon_startup
test_workload_classification
test_zram_initialization
test_gaming_profile
test_ai_profile
test_idle_optimization
test_ramctl_commands
test_config_reload
```

### 10.3 Performance Benchmarks

```
benchmark_llama_inference: Memory use with/without UROAM
benchmark_kernel_compile: Build time with/without UROAM  
benchmark_gaming: Frame time with/without UROAM
benchmark_swap_reduction: Swap I/O reduction
```

---

## 11. Cross-Distribution Compatibility

### 11.1 Supported Distributions

| Distribution | Version | Kernel Min |
|--------------|---------|------------|
| Fedora | 39+ | 5.15 |
| Ubuntu | 22.04 LTS+ | 5.15 |
| Debian | 12+ | 5.15 |
| Arch Linux | rolling | 5.15 |
| openSUSE | Leap 15.5+ | 5.15 |

### 11.2 Kernel Version Handling

```
if (kernel_version >= 6.1) {
    enable_mglru = true
    // Use MGLRU-specific tunables
} else {
    enable_mglru = false
    // Use classic LRU
}
```

---

## 12. Appendix: Key File Paths

| Path | Purpose |
|------|---------|
| /etc/uroam/uroam.conf | Main configuration |
| ~/.config/uroam/uroam.conf | User override |
| /var/log/uroam/uroamd.log | Daemon log |
| /run/uroam/uroamd.sock | Control socket |
| /run/uroam/cache.sock | Cache pool socket |
| /dev/uroam | Kernel module device |
| /proc/uroam/{status,config,stats} | Procfs interface |
| /sys/kernel/uroam/ | Sysfs interface |
| /sys/fs/cgroup/uroam/ | cgroup hierarchy |

---

## 13. Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | April 2026 | UROAM Team | Initial architecture document |

---

*End of Architecture Design Document*