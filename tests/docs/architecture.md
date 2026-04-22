# UROAM Architecture Documentation

## System Overview

The Unified RAM Optimization Framework (UROAM) consists of two main components:

### 1. Kernel-Space Component
- **Purpose**: Low-level memory interception and optimization
- **Interface**: Kernel module or eBPF programs
- **Responsibilities**:
  - Hook into VMM, page cache, and swap subsystem
  - Track page access patterns
  - Provide optimization hooks
  - Communicate with user-space daemon via Netlink

### 2. User-Space Component
- **Purpose**: Policy enforcement and optimization management
- **Main Process**: `uroam_daemon.py`
- **Responsibilities**:
  - Workload classification
  - Optimization policy enforcement
  - Configuration management
  - Metrics collection

## Data Flow

```
Application → VMM → Page Fault Handler → Classification → Optimization → Physical RAM/Swap
```

## Key Design Patterns

### Modularity
- Separate classification, optimization, and policy modules
- Pluggable architecture for different optimization strategies
- Configurable via YAML configuration

### Performance
- Minimal overhead (<2% CPU in steady state)
- Asynchronous optimization cycles
- Efficient data structures for page tracking

### Compatibility
- No application modifications required
- Transparent operation
- Standard Linux interfaces (VMM, cgroups, sysfs)

## Implementation Details

### Kernel Module
- Uses kprobes/uprobes for function tracing
- Implements page fault handler hooks
- Maintains per-process memory statistics
- Integrates with existing kernel subsystems

### User-Space Daemon
- Python-based for rapid development
- Threaded architecture for parallel operations
- YAML-based configuration
- Systemd integration for service management

## Integration Points

### Virtual Memory Manager (VMM)
- Page fault handling
- Page access tracking
- Memory reclamation coordination

### Page Cache
- Cache entry tracking
- Cache optimization hints
- Dirty page management

### Swap Subsystem
- Swap priority management
- Hybrid swap model (zram + disk)
- Swap timing optimization

### cgroups v2
- Per-cgroup memory policies
- Resource isolation
- Memory limit enforcement

### NUMA
- Cross-node memory migration
- Node-local allocation optimization
- Memory affinity management

### THP (Transparent Huge Pages)
- THP consolidation optimization
- Huge page allocation tracking
- THP splitting/joining coordination

## Configuration

The framework is configured through `/etc/uroam/uroam.yaml`:

```yaml
uroam:
  enabled: true
  optimization:
    compression:
      enabled: true
      algorithm: auto
    deduplication:
      enabled: true
    swapping:
      enabled: true
```

## Monitoring

Metrics are exposed through:
- System log files (`/var/log/uroam/uroam.log`)
- Performance counters
- Optional GUI dashboard (stretch goal)
