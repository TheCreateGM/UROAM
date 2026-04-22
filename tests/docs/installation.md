# UROAM Installation Guide

## Supported Distributions

- Arch Linux
- Ubuntu
- Debian
- Fedora
- openSUSE

## Installation Methods

### Method 1: Package Manager (Recommended)

#### Ubuntu/Debian
```bash
sudo dpkg -i uroam-package.deb
sudo systemctl enable uroam
sudo systemctl start uroam
```

#### Fedora/RHEL
```bash
sudo rpm -ivh uroam-package.rpm
sudo systemctl enable uroam
sudo systemctl start uroam
```

#### Arch Linux (AUR)
```bash
paru -S uroam
# or
yay -S uroam
```

### Method 2: Manual Installation

```bash
# Clone repository
git clone https://github.com/example/uroam.git
cd uroam

# Install systemd service
sudo cp packaging/systemd/uroam.service /etc/systemd/system/
sudo systemctl daemon-reload

# Install configuration
sudo mkdir -p /etc/uroam
sudo cp packaging/config/uroam.yaml /etc/uroam/

# Start service
sudo systemctl enable uroam
sudo systemctl start uroam
```

## Verification

Check service status:
```bash
sudo systemctl status uroam
sudo journalctl -u uroam -f
```

Check configuration:
```bash
uroam-cli status
```

## Uninstallation

```bash
sudo systemctl stop uroam
sudo systemctl disable uroam

# For package installations
sudo apt remove uroam    # Debian/Ubuntu
sudo dnf remove uroam    # Fedora
sudo rpm -e uroam        # RPM-based
```

## Troubleshooting

### Service won't start
- Check logs: `journalctl -u uroam -f`
- Verify kernel support: `lsmod | grep uroam`
- Check configuration: `uroam-cli show`

### Performance issues
- Review logs for errors
- Check CPU usage: top
- Monitor memory: free -h

### Application compatibility
- Test critical applications
- Adjust per-application profiles in config
- Disable specific optimizations if needed
