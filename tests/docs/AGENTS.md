# UROAM Agent Documentation

## Project Overview
Unified RAM Optimization Framework for Linux - optimizes memory across heterogeneous workloads.

## Key Components

### Kernel Module (src/kernel/uroam_main.c)
- Memory interception hooks
- Deduplication management
- Swap optimization
- NUMA awareness

### User Daemon (src/user/uroam_daemon.py)
- Workload classification
- Policy engine
- Optimization orchestration

### CLI Tool (src/user/uroam_cli.py)
- Runtime configuration
- Status monitoring
- Parameter adjustment

## Build System
- CMake for cross-platform builds
- Supports kernel module and user-space components
- Systemd integration

## Testing
- Unit tests in tests/unit/
- Integration and performance tests planned
- Benchmark suite for validation

## Configuration
- YAML-based: /etc/uroam/uroam.yaml
- CLI control: uroam-cli
- Systemd service: uroam.service

## Performance Targets
- CPU overhead: <2%
- Memory savings: 20-60%
- Latency: <1ms for gaming
