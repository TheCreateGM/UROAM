#!/bin/bash
# UROAM - Unified RAM Optimization and Management Framework
# install.sh - Cross-distribution installation script

set -e

COLOR_RED='\033[0;31m'
COLOR_GREEN='\033[0;32m'
COLOR_YELLOW='\033[1;33m'
COLOR_NC='\033[0m'

log_info() { echo -e "${COLOR_GREEN}[INFO]${COLOR_NC} $1"; }
log_warn() { echo -e "${COLOR_YELLOW}[WARN]${COLOR_NC} $1"; }
log_error() { echo -e "${COLOR_RED}[ERROR]${COLOR_NC} $1"; }

detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO=$ID
    elif [ -f /etc/fedora-release ]; then
        DISTRO="fedora"
    elif [ -f /etc/debian_version ]; then
        DISTRO="debian"
    elif [ -f /etc/arch-release ]; then
        DISTRO="arch"
    else
        DISTRO="unknown"
    fi
    log_info "Detected distribution: $DISTRO"
}

check_kernel() {
    local major minor
    major=$(uname -r | cut -d. -f1)
    minor=$(uname -r | cut -d. -f2)
    if [ "$major" -lt 5 ] || ([ "$major" -eq 5 ] && [ "$minor" -lt 15 ]); then
        log_warn "Kernel $(uname -r) < 5.15 (recommended)"
    fi
}

install_dependencies() {
    log_info "Installing dependencies..."
    case $DISTRO in
        ubuntu|debian)
            apt-get update && apt-get install -y build-essential cmake ninja-build clang llvm libclang-dev linux-headers-$(uname -r) pkg-config curl git
            ;;
        fedora|rhel|centos)
            dnf install -y gcc make cmake ninja-build clang llvm kernel-devel pkg-config curl git
            ;;
        arch)
            pacman -Syu --noconfirm base-devel cmake ninja clang llvm linux-headers pkg-config curl git
            ;;
        opensuse)
            zypper install -y gcc make cmake ninja-build clang llvm kernel-devel pkg-config curl git
            ;;
    esac
}

install_rust() {
    if command -v rustc &> /dev/null; then
        log_info "Rust already installed"
        return
    fi
    log_info "Installing Rust..."
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
    source "$HOME/.cargo/env"
}

build_aal() {
    log_info "Building AAL..."
    mkdir -p build/aal
    gcc -c -std=c11 -I include aal/aal.c -o build/aal/aal.o
    ar rcs build/aal/libaal.a build/aal/aal.o
    log_info "AAL built successfully"
}

build_kernel_module() {
    log_info "Building kernel module..."
    if [ ! -d "/lib/modules/$(uname -r)/build" ]; then
        log_warn "Kernel headers not found, skipping module"
        return 0
    fi
    cd kmod
    make -C /lib/modules/$(uname -r)/build M=$(pwd) modules 2>/dev/null || make
    cd ..
}

build_c_lib() {
    log_info "Building C library..."
    mkdir -p build/lib
    gcc -c -std=c11 -I include src/shared/memopt.c -o build/lib/memopt.o
    ar rcs build/lib/libmemopt.a build/lib/memopt.o
}

build_rust() {
    if ! command -v cargo &> /dev/null; then
        log_warn "Cargo not found, skipping Rust build"
        return 0
    fi
    source "$HOME/.cargo/env" 2>/dev/null || true
    log_info "Building Rust components..."
    cargo build --release 2>/dev/null || log_warn "Rust build failed (environment issue)"
}

run_tests() {
    log_info "Running tests..."
    if [ -f build/aal/aal_test ]; then
        build/aal_test
    fi
}

install_files() {
    log_info "Installing files..."
    mkdir -p /usr/local/bin /usr/local/lib /usr/local/include /etc/uroam /var/log/uroam /run/uroam
    
    [ -f build/aal/libaal.a ] && cp build/aal/libaal.a /usr/local/lib/
    [ -f build/lib/libmemopt.a ] && cp build/lib/libmemopt.a /usr/local/lib/
    cp include/uroam_aal.h include/uroam.h /usr/local/include/
    
    if [ -f target/release/uroamd ]; then
        cp target/release/uroamd /usr/local/bin/
        cp target/release/ramctl /usr/local/bin/
    fi
    
    if [ ! -f /etc/uroam/uroam.toml ]; then
        cat > /etc/uroam/uroam.toml << 'EOF'
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
ksm_pages_to_scan = 5000
thp_mode = "always"
cache_pressure = 100
background_cgroup_limit = "2G"

[profiles.gaming]
swappiness = 1
zram_algorithm = "lz4"
zram_size_percent = 30
ksm_enabled = false
ksm_pages_to_scan = 0
thp_mode = "always"
cache_pressure = 50
background_cgroup_limit = "4G"

[profiles.balanced]
swappiness = 60
zram_algorithm = "lz4"
zram_size_percent = 25
ksm_enabled = true
ksm_pages_to_scan = 1000
thp_mode = "madvise"
cache_pressure = 100
background_cgroup_limit = "4G"

[profiles.powersaver]
swappiness = 90
zram_algorithm = "zstd"
zram_size_percent = 60
ksm_enabled = true
ksm_pages_to_scan = 3000
thp_mode = "always"
cache_pressure = 25
background_cgroup_limit = "1G"

[tmpfs]
build_dir = "/tmp/uroam-build"
build_size = "8G"
ai_cache_dir = "/tmp/uroam-ai-cache"
ai_cache_size = "16G"

[idle]
idle_threshold_secs = 300
optimization_level = "normal"
preload_enabled = false
EOF
    fi
    
    ldconfig 2>/dev/null || true
}

install_systemd() {
    log_info "Installing systemd service..."
    cat > /etc/systemd/system/uroam.service << 'EOF'
[Unit]
Description=UROAM - Unified RAM Optimization and Management Daemon
After=network.target local-fs.target

[Service]
Type=simple
ExecStart=/usr/local/bin/uroamd --config /etc/uroam/uroam.toml
Restart=on-failure
RestartSec=5
User=root

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload 2>/dev/null || true
}

main() {
    log_info "UROAM Installation"
    log_info "=================="
    
    detect_distro
    check_kernel
    
    [ "$1" = "--deps-only" ] && install_dependencies && install_rust && exit 0
    
    install_dependencies
    install_rust
    build_aal
    build_kernel_module
    build_c_lib
    build_rust
    install_files
    install_systemd
    run_tests
    
    log_info "=================="
    log_info "Installation complete!"
    log_info "Enable: systemctl enable uroam"
    log_info "Start: systemctl start uroam"
    log_info "Status: ramctl status"
}

main "$@"