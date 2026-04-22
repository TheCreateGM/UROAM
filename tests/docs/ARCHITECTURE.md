# RAM Optimization Layer - Architecture Design

## Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           USER APPLICATIONS                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐   │
│  │  PyTorch    │  │ TensorFlow  │  │   ONNX      │  │   Games/Steam   │   │
│  │  (Python)   │  │   (Python)  │  │  (C++/Py)   │  │   (Native)      │   │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └───────┬───────┘   │
└─────────┼────────────────┼────────────────┼──────────────────┼──────────┘
          │                │                │                  │
          ▼                ▼                ▼                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     AI FRAMEWORK INTEGRATION LAYER                        │
│  ┌─────────────────────────────────────────────────────────────────────┐│
│  │  liburoam (Python C Extension / C++ Library)                            ││
│  │  - Allocation hooks (malloc, mmap wrappers)                           ││
│  │  - Memory pool management                                             ││
│  │  - Workload classification (inference/training/gaming)              ││
│  │  - Framework-specific optimizations                                   ││
│  └─────────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────┘
                                          │
                                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          USER-SPACE DAEMON (Go)                            │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌──────────────────┐   │
│  │   Config   │  │  Manager   │  │  Monitor   │  │   Optimizer      │   │
│  │   Module   │◄─►│   Module   │◄─►│   Module   │◄─►│   Modules         │   │
│  └────────────┘  └────────────┘  └────────────┘  └──────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │  REST API / gRPC Server (Port 8080)                                  │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │  IPC: Unix Domain Sockets, Shared Memory, DBus                     │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
          │                              │                              │
          ▼                              ▼                              ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│  Compression     │    │  Deduplication   │    │   NUMA          │
│  Engine          │    │  Engine (KSM)    │    │  Optimizer      │
│  - zRAM Tuning   │    │  - Page Merging  │    │  - Node-Aware   │
│  - zswap Tuning  │    │  - KSM Wrapper   │    │    Allocation   │
│  - LZ4/Zstd      │    │  - Scan Rates    │    │  - Page Migration│
└─────────────────┘    └─────────────────┘    └─────────────────┘
          │                              │                              │
          └──────────────────────────────┼──────────────────────────┘
                                           ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        KERNEL MODULE (uroam.ko)                           │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────┐   │
│  │  Memory Hooks   │  │  Page Tracking  │  │  Pressure Monitor    │   │
│  │  - mmap         │  │  - Page States  │  │  - psi interface     │   │
│  │  - brk          │  │  - Access Patterns│  │  - OOM Notifier     │   │
│  │  - kmalloc      │  │  - LRU Lists    │  │  - Reclaim Hooks    │   │
│  └─────────────────┘  └─────────────────┘  └─────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │  Kernel Interfaces:                                                  │  │
│  │  - /proc/pressure, /sys/kernel/mm, cgroups v2, KSM, zRAM, NUMA    │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                          │
                                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          LINUX KERNEL SUBSYSTEM                           │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────┐  ┌────────┐│
│  │   mm     │  │  cgroups │  │  zRAM    │  │   KSM        │  │ NUMA   ││
│  │ (memory  │  │   v2     │  │  /zswap  │  │ (samepage    │  │        ││
│  │  manager)│  │          │  │          │  │  merging)    │  │        ││
│  └──────────┘  └──────────┘  └──────────┘  └──────────────┘  └────────┘│
└─────────────────────────────────────────────────────────────────────────┘
```

## Component Hierarchy

### Layer 1: Kernel Module (uroam.ko)
- **Purpose**: Low-latency memory event interception and kernel-level optimizations
- **Language**: C
- **Interface**: Netlink socket for user-space communication

#### Submodules:
- `hooks/` - Allocation interception (mmap, brk, kmalloc)
- `tracking/` - Page state monitoring
- `pressure/` - Memory pressure monitoring via PSI
- `reclaim/` - Custom reclaim strategies
- `numa/` - NUMA-aware kernel helpers

### Layer 2: Core Library (liburoam)
- **Purpose**: User-space memory management with optimized allocators
- **Language**: C++ (with C ABI for compatibility)
- **Features**:
  - Memory pooling with size-class segregation
  - Lazy allocation strategies
  - Shared memory management across processes
  - Zero-copy buffer support

#### Subcomponents:
- `allocator/` - Custom allocator implementations
  - `pool_allocator.cpp` - Size-class based pooling
  - `numa_allocator.cpp` - NUMA-aware allocations
  - `hugepage_allocator.cpp` - HugeTLBfs wrapper
- `compression/` - Transparent compression helpers
  - `zram_tuner.cpp` - Dynamic zRAM configuration
  - `zswap_tuner.cpp` - zswap parameter optimization
- `dedup/` - Deduplication helpers
  - `ksm_wrapper.cpp` - KSM integration
  - `page_dedup.cpp` - User-space deduplication
- `numa/` - NUMA optimization
  - `topology.cpp` - System NUMA topology detection
  - `migration.cpp` - Page migration helpers

### Layer 3: User-Space Daemon (uroamd)
- **Purpose**: Orchestration, monitoring, and policy enforcement
- **Language**: Go
- **Architecture**: Modular microservice-style design

#### Modules:
- `cmd/` - Main entry points
  - `uroamd/` - System daemon
  - `uroamctl/` - CLI control tool
- `internal/config/` - Configuration management
  - `config.go` - Toml/JSON config parser
  - `policies.go` - Optimization policy definitions
- `internal/manager/` - Core orchestration
  - `manager.go` - Main daemon logic
  - `workload.go` - Process classification and prioritization
  - `lifecycle.go` - Daemon lifecycle management
- `internal/monitor/` - Telemetry collection
  - `collector.go` - Metrics collection
  - `metrics.go` - Prometheus/StatsD metrics
  - `tracer.go` - eBPF-based tracing
- `internal/optimizer/` - Optimization engines
  - `compression.go` - Compression policy engine
  - `dedup.go` - Deduplication policy engine
  - `swap.go` - Swap optimization
  - `numa.go` - NUMA policy engine
- `internal/api/` - External interfaces
  - `grpc_server.go` - gRPC API server
  - `rest_server.go` - REST API server
  - `ipc.go` - IPC with kernel module and clients

### Layer 4: AI Framework Integration
- **Purpose**: Seamless integration with AI frameworks
- **Language**: Python (C extensions for performance)

#### Integrations:
- `pytorch/`
  - `_uroam.pyx` - Cython extension
  - `hook.py` - PyTorch allocator hooks
  - `tensor_optimizer.py` - Tensor-specific optimizations
- `tensorflow/`
  - `_uroam.cc` - C++ extension with Python bindings
  - `bfc_allocator_hook.py` - BFC allocator wrapper
- `onnx/`
  - `uroam_provider.cc` - ONNX Runtime memory provider

### Layer 5: CLI Tool (uroamctl)
- **Purpose**: Monitoring and control interface
- **Language**: Go (daemon) + Python (rich CLI)

#### Commands:
- `uroamctl status` - System overview
- `uroamctl monitor` - Real-time monitoring
- `uroamctl config` - Configuration management
- `uroamctl optimize` - Manual optimization triggers
- `uroamctl profile` - Process profiling

### Layer 6: Monitoring Dashboard
- **Purpose**: Visualization and analytics
- **Language**: Python (FastAPI) + TypeScript (Frontend)

#### Components:
- `dashboard/server.py` - FastAPI backend
- `dashboard/frontend/` - React/Vue.js frontend
- Real-time WebSocket updates
- Historical metrics storage (SQLite)

## Data Flow

### Allocation Flow:
```
User App (PyTorch) 
  → liburoam (Python Extension)
    → Check memory policy (daemon via IPC)
    → Allocate from optimized pool
    → Register with tracking system
    → Return memory to app
```

### Monitoring Flow:
```
Kernel (PSI, /proc, /sys)
  → eBPF Tracing
    → Daemon Monitor Module
      → Metrics Aggregation
        → Storage (SQLite) / API
          → Dashboard / CLI
```

### Optimization Flow:
```
Monitor (pressure detected)
  → Policy Engine (evaluate rules)
    → Optimizer Modules (apply actions)
      → Kernel Module (low-level adjustments)
        → Notify Daemon (confirmation)
          → Update Metrics
```

## Inter-Process Communication

### 1. Kernel ↔ Daemon
- **Mechanism**: Netlink socket (NETLINK_USERSOCK)
- **Protocol**: Custom binary protocol
- **Messages**:
  - Memory pressure events
  - Page state updates
  - Reclaim requests
  - Policy adjustments

### 2. Daemon ↔ Library
- **Mechanism**: Unix Domain Socket
- **Protocol**: Protocol Buffers
- **Messages**:
  - Allocation requests
  - Policy queries
  - Telemetry reports

### 3. Daemon ↔ CLI
- **Mechanism**: REST API (localhost:8080) + gRPC
- **Endpoints**:
  - GET /api/v1/status
  - GET /api/v1/metrics
  - POST /api/v1/optimize
  - POST /api/v1/config

### 4. Daemon ↔ Dashboard
- **Mechanism**: WebSocket + REST
- **Protocol**: JSON
- **Updates**: Real-time metrics stream

## Memory Management Strategy

### Allocation Hierarchy:
```
1. Fast Path (Hot):      LRU Cache → HugePages → Local NUMA Node
2. Normal Path (Warm):   Buddy Allocator → Regular Pages → Local Node
3. Slow Path (Cold):     Reclaim → Compress → Swap → OOM
```

### Workload Classification:
| Priority | Workload Type | Latency Req | Strategy |
|----------|--------------|--------------|----------|
| P0 (Highest) | AI Inference | <1ms | Pre-allocated pools, HugePages, local NUMA |
| P1 | Gaming | <16ms | High-priority, defrag on demand |
| P2 | AI Training | <100ms | Throughput-optimized, NUMA-balanced |
| P3 | Background | >100ms | Aggressive compression, swap-friendly |
| P4 (Lowest) | Idle | N/A | Compress everything, page out |

### Policy Engine:
```
Input: Memory Pressure, Workload Mix, System State
  → Rule Evaluation (Priority-based)
    → Action Selection:
      - Adjust cgroup limits
      - Enable/disable KSM
      - Tune zRAM/zswap
      - Trigger page reclaim
      - Migrate pages between NUMA nodes
```

## Configuration

### Main Config File: `/etc/uroam/uroam.conf`
```toml
[global]
enabled = true
daemon_socket = "/var/run/uroam.sock"
api_address = "localhost:8080"
log_level = "info"

[monitor]
collection_interval = "1s"
ebpf_enabled = true
pressure_threshold = 80

[compression]
zram_enabled = true
zram_size = "4G"
zram_algorithm = "zstd"
zswap_enabled = true
zswap_max_pool_percent = 20

[deduplication]
ksm_enabled = true
ksm_scan_rate = 100
ksm_merge_across_nodes = true

[numa]
aware_allocation = true
auto_migrate = true
migration_threshold = 0.7

[workload]
default_priority = "P2"
priority_rules = [
    { name = "python", priority = "P2" },
    { name = "steam", priority = "P1" },
    { name = "game", priority = "P1" },
]

[ai_frameworks]
pytorch_hook = true
tensorflow_hook = true
onnx_hook = true
```

## Build System

### Directory Structure:
```
CMakeLists.txt              # Root build configuration
kernel/CMakeLists.txt        # Kernel module build
lib/CMakeLists.txt           # Core library
lib/allocator/CMakeLists.txt
lib/compression/CMakeLists.txt
...
daemon/Makefile            # Go daemon build
integrations/setup.py        # Python package build
```

### Build Targets:
- `uroam-ko` - Kernel module
- `liburoam.so` - Core library
- `uroamd` - System daemon
- `uroamctl` - CLI tool
- `uroam-python` - Python package

## Installation

### Package Structure:
```
uroam_<version>_amd64.deb
├── /usr/lib/modules/< kernel >/kernel/drivers/misc/uroam.ko
├── /usr/lib/liburoam.so
├── /usr/lib/liburoam_python.so
├── /usr/bin/uroamd
├── /usr/bin/uroamctl
├── /etc/uroam/uroam.conf
├── /etc/systemd/system/uroamd.service
├── /var/lib/uroam/           (state files)
├── /var/log/uroam/           (logs)
└── /var/run/uroam.sock       (IPC socket)
```

### Systemd Service:
```ini
[Unit]
Description=Universal RAM Optimization Layer Daemon
After=network.target syslog.target

[Service]
Type=notify
ExecStart=/usr/bin/uroamd
Restart=on-failure
RestartSec=5
Environment=GOTRACEBACK=crash

[Install]
WantedBy=multi-user.target
```

## Security Considerations

### Privileges:
- **Kernel Module**: Root (CAP_SYS_MODULE)
- **Daemon**: Root (CAP_SYS_ADMIN, CAP_SYS_RESOURCE)
- **Library**: Unprivileged (user-space only)
- **CLI**: Unprivileged (read-only by default)

### Isolation:
- Daemon runs as dedicated user `uroam` (UID > 1000)
- Capabilities dropped after initialization
- Seccomp filters for system calls
- Namespaces for process isolation

### Communication Security:
- Unix sockets: File permissions (0600)
- REST API: Localhost only, TLS optional
- gRPC: mTLS for remote connections

## Performance Targets

| Metric | Target |
|--------|--------|
| Daemon CPU overhead | <5% |
| Allocation latency (fast path) | <1μs overhead |
| Memory reduction | 20-40% |
| NUMA locality | >90% |
| Compression ratio | 2:1 (minimum) |
| Swap latency | <10ms (with zRAM) |

## Testing Strategy

### Unit Tests:
- Kernel module: KUnit tests
- Library: Google Test
- Daemon: Go test packages

### Integration Tests:
- Allocator correctness under load
- Kernel-user-space IPC reliability
- Framework integration benchmarks

### Benchmark Tests:
- AI workload memory usage comparison
- Latency measurements with/without optimization
- System responsiveness under pressure

### Compatibility Matrix:
| Distribution | Kernel | Supported |
|--------------|--------|-----------|
| Ubuntu | 5.15+ | ✓ |
| Debian | 5.10+ | ✓ |
| Fedora | 6.0+ | ✓ |
| Arch | latest | ✓ |
| RHEL/CentOS | 8.0+ | ✓ |
