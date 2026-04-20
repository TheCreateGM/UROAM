# Fedora Installation & Usage Guide

## Fedora-Specific Notes

Fedora has excellent out-of-the-box support for all memory optimization features:

- zswap is enabled by default
- KSM modules are included in default kernel
- cgroups v2 is default
- Transparent HugePages are configured properly

## Installation

```bash
# Install build dependencies
sudo dnf install numactl-devel clang golang make gcc

# Clone and build
git clone <repository-url>
cd Driver
make all
sudo make install
```

## Systemd Service Setup

```bash
# Install service file
sudo cp scripts/memopt.service /etc/systemd/system/

# Enable and start daemon
sudo systemctl daemon-reload
sudo systemctl enable --now memopt
```

## Verify Installation

```bash
# Check daemon status
systemctl status memopt

# Show current optimizations
memopt stats

# Tune for AI inference workloads
memopt tune inference
```

## Usage with AI Workloads

### Running LLM inference with optimizations:
```bash
memopt tune inference
LD_PRELOAD=/usr/local/lib/libmemopt.so python run_llm.py
```

### Running model training:
```bash
memopt tune training
LD_PRELOAD=/usr/local/lib/libmemopt.so python train_model.py
```

### Gaming optimizations:
```bash
memopt tune gaming

# Launch Steam with optimizations
LD_PRELOAD=/usr/local/lib/libmemopt.so steam
```

## Performance Tuning for Fedora

Add these to `/etc/sysctl.d/99-memopt.conf` for permanent optimizations:
```
vm.swappiness = 10
vm.dirty_ratio = 15
vm.dirty_background_ratio = 5
vm.vfs_cache_pressure = 50
```

Apply with: `sudo sysctl -p /etc/sysctl.d/99-memopt.conf`

## Troubleshooting

- If KSM is not working: `sudo modprobe ksm`
- Check zswap status: `grep -R . /sys/module/zswap/parameters/`
- Verify THP: `cat /sys/kernel/mm/transparent_hugepage/enabled`
