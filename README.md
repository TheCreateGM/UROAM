# Universal RAM Optimization Layer (UROAM)

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)
[![Python 3.8+](https://img.shields.io/badge/python-3.8+-blue.svg)](https://www.python.org/downloads/)
[![Go 1.21+](https://img.shields.io/badge/go-1.21+-blue.svg)](https://go.dev/)

**UROAM** is a system-level RAM optimization framework for Linux that intelligently manages memory usage across AI workloads, gaming, and general applications to reduce waste and improve performance.

## Features

- **Kernel-level Memory Tracking**: Monitors memory pressure, allocations, and usage patterns via kernel module
- **User-space Optimization Daemon**: Orchestrates memory optimization policies in Go
- **NUMA-aware Allocation**: Intelligently distributes memory across NUMA nodes for optimal performance
- **Memory Deduplication**: leverages Kernel Samepage Merging (KSM) for duplicate page elimination
- **Compression Optimization**: Tunes zRAM and zswap for optimal memory compression
- **AI Framework Integration**: Seamless integration with PyTorch, TensorFlow, and ONNX Runtime
- **Gaming Support**: Optimizes memory for Steam and Proton games
- **Real-time Monitoring**: CLI and API interfaces for monitoring system state
- **Policy-based Optimization**: Intelligent rules engine for workload-specific optimization

## Architecture

```
User Apps (PyTorch, TensorFlow, ONNX, Games)
            │
            ▼
┌─────────────────────────────────────────┐
│  AI Framework Integration (Python/C++)    │
└─────────────────────────┬────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────┐
│         User-Space Library (C++)         │
│  Allocator | Compression | NUMA | Dedup   │
└─────────────────────────┬────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────┐
│        User-Space Daemon (Go)            │
│  Monitor | Optimizer | Manager | API     │
└─────────────────────────┬────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────┐
│         Kernel Module (C)                 │
│  Pressure | Hooks | Tracking | NUMA      │
└─────────────────────────┬────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────┐
│           Linux Kernel                    │
│  PSI | cgroups | zRAM | zswap | KSM       │
└─────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

- Linux kernel 5.10+ (for full feature support)
- Go 1.21+
- Python 3.8+
- CMake 3.16+
- GCC/Clang
- Kernel headers (`linux-headers-*` package)

### Installation

#### 1. Build the kernel module
```bash
cd kernel
make build
sudo make install
sudo make load
```

#### 2. Run the daemon
```bash
cd daemon
sudo go run ./cmd/uroamd /etc/uroam/uroam.conf
```

#### 3. Verify it's working
```bash
# Check daemon status
python cli/uroamctl.py status

# View memory metrics
python cli/uroamctl.py metrics

# List AI workloads
python cli/uroamctl.py processes --ai
```

## Project Structure

```
uroam/
├── ARCHITECTURE.md          # System architecture documentation
├── README.md               # This file
├── CMakeLists.txt           # Top-level build configuration
│
├── kernel/                  # Kernel module (C)
│   ├── Makefile
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── uroam.h
│   ├── ipc.c               # Netlink IPC
│   ├── pressure.c          # Memory pressure via PSI
│   ├── hooks.c             # Allocation hooks
│   ├── tracking.c          # Process tracking
│   └── numa.c              # NUMA optimization
│
├── lib/                     # User-space library (C++)
│   ├── CMakeLists.txt
│   ├── allocator/           # Memory allocators
│   ├── compression/        # zRAM/zswap tuners
│   ├── dedup/              # KSM wrapper
│   └── numa/               # NUMA-aware alloc
│
├── daemon/                  # User-space daemon (Go)
│   ├── go.mod
│   ├── cmd/uroamd/         # Daemon entry point
│   └── internal/
│       ├── config/         # Config management
│       ├── manager/        # Core orchestration
│       ├── monitor/        # System monitoring
│       └── optimizer/      # Optimization engine
│
├── cli/                     # CLI tools (Python)
│   └── uroamctl.py
│
├── integrations/            # AI framework integrations (Python)
│   ├── __init__.py
│   └── core.py             # Core Python API
│
├── dashboard/               # Web dashboard (FastAPI)
│   └── server.py
│
└── docs/
```

## Building

### Kernel Module
```bash
cd kernel
make build        # Build the module
sudo make load    # Load into kernel
sudo make unload  # Unload from kernel
```

### Go Daemon
```bash
cd daemon
go build ./cmd/uroamd
./uroamd /etc/uroam/uroam.conf
```

### Full Build (CMake)
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

## Usage Examples

### CLI
```bash
# Status
python cli/uroamctl.py status

# Memory usage
python cli/uroamctl.py metrics --memory

# AI processes
python cli/uroamctl.py processes --ai

# Trigger optimization
python cli/uroamctl.py optimize

# Set mode to gaming
python cli/uroamctl.py mode gaming

# Real-time monitor
python cli/uroamctl.py monitor
```

### Python
```python
import sys
sys.path.insert(0, '/path/to/project/integrations')

import uroam

# Initialize
uroam.init(uroam.UROAMConfig(
    enabled=True,
    workload_type='ai',
    priority='P0'
))

# Context-based optimization for inference
with uroam.inference_context():
    # AI inference code here
    result = model.predict(data)

# Manual allocation
buffer = uroam.allocate(1024 * 1024)  # 1 MB
# ... use buffer ...
uroam.free(buffer)

# Get stats
stats = uroam.get_memory_stats()
print(f"Usage: {stats.usage_percent:.1f}%")
```

### REST API
```bash
# Health check
curl http://localhost:8080/api/v1/health

# Metrics
curl http://localhost:8080/api/v1/metrics

# AI processes
curl http://localhost:8080/api/v1/metrics/ai

# Trigger optimization
curl -X POST http://localhost:8080/api/v1/optimize

# Set mode
curl -X POST -H "Content-Type: application/json" \
    -d '{"mode": "gaming"}' \
    http://localhost:8080/api/v1/optimize/mode
```

## Configuration

`/etc/uroam/uroam.conf`:
```toml
[global]
enabled = true
log_level = "info"

[monitor]
collection_interval = "1s"
pressure_threshold = 80

[compression]
zram_enabled = true
zram_size = "4G"
zswap_enabled = true

[deduplication]
ksm_enabled = true
ksm_scan_rate = 100

[numa]
aware_allocation = true
auto_migrate = true

[workload]
default_priority = "P2"
```

## License

GNU GPLv2 - see [LICENSE](LICENSE)

## Status

This is a **production-ready implementation** with the following features implemented:

- ✅ Kernel module with memory tracking
- ✅ PSI (pressure stall information) monitoring
- ✅ Process tracking and classification
- ✅ Netlink IPC between kernel and user-space
- ✅ Go daemon with modular architecture
- ✅ REST API server
- ✅ System monitoring via /proc
- ✅ Python CLI tool
- ✅ Python integration library
- ✅ Workload type detection (AI/Gaming)
- ✅ Priority-based optimization
- ✅ KSM configuration
- ✅ CMake build system
- ✅ Kernel module allocation hooks (kprobes/ftrace)
- ✅ NUMA-aware allocator in user-space
- ✅ zRAM/zswap dynamic tuning
- ✅ Page deduplication engine
- ✅ eBPF-based tracing infrastructure
- ✅ PyTorch/TensorFlow specific hooks
- ✅ Client library for C/C++ applications
- ✅ Package generation (Deb/RPM)

