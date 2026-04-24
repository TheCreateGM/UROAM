Name: uroam
Version: 1.0.0
Release: 1%{?dist}
Summary: Unified RAM Optimization and Management Framework
License: GPLv3+
URL: https://github.com/TheCreateGM/UROAM
Group: System Environment/Daemons
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

# For pre-built binary package
BuildRequires: systemd

%description
UROAM is a cross-distribution, cross-architecture Linux framework
that transparently optimizes RAM for AI, gaming, development, and
general workloads without requiring application changes.

Features:
- Architecture Abstraction Layer (AAL) for x86_64, ARM64, ARM32,
  RISC-V64, PPC64LE, s390x, and LoongArch64
- Rust-based daemon (uroamd) with workload classification
- Memory compression via ZRAM and Zswap
- Gaming optimizations (Steam/Proton/Wine detection)
- Idle optimizations with automatic cache management
- CLI tool (ramctl) for status and control
- TOML-based configuration with multiple profiles

%package devel
Summary: Development files for UROAM
Group: Development/Libraries
Requires: %{name} = %{version}-%{release}
Obsoletes:        %{name}-devel < %{version}-%{release}

%description devel
This package contains the development files for UROAM.

%prep
%setup -q

%build
# No build needed - using pre-built binaries from upstream

%install
rm -rf %{buildroot}

mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_includedir}
mkdir -p %{buildroot}%{_sysconfdir}/uroam
mkdir -p %{buildroot}%{_unitdir}
mkdir -p %{buildroot}%{_rundir}/uroam
mkdir -p %{buildroot}%{_localstatedir}/log/uroam

# Install pre-built Rust binaries (from upstream release)
install -m 755 target/release/uroamd %{buildroot}%{_bindir}/
install -m 755 target/release/ramctl %{buildroot}%{_bindir}/

# Install kernel module (if built)
if [ -f kmod/uroam.ko ]; then
    mkdir -p %{buildroot}%{_libdir}/modules/%{kernel}/kernel/drivers/misc
    install -m 644 kmod/uroam.ko %{buildroot}%{_libdir}/modules/%{kernel}/kernel/drivers/misc/
fi

# Install configuration
cat > %{buildroot}%{_sysconfdir}/uroam/uroam.toml << 'EOF'
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
EOF'

# Install systemd service
cat > %{buildroot}%{_unitdir}/uroam.service << 'EOF'
[Unit]
Description=UROAM - Unified RAM Optimization and Management Daemon
After=network.target local-fs.target

[Service]
Type=simple
ExecStart=%{_bindir}/uroamd --config %{_sysconfdir}/uroam/uroam.toml
Restart=on-failure
RestartSec=5
User=root
Group=root
LimitNOFILE=65536
LimitNPROC=65536

[Install]
WantedBy=multi-user.target
EOF'

%post
if [ -d "%{_libdir}/modules/%{kernel}" ]; then
    depmod -a %{kernel}
fi
systemctl daemon-reload 2>/dev/null || true

%preun
if [ $1 -eq 0 ]; then
    systemctl stop uroam 2>/dev/null || true
    systemctl disable uroam 2>/dev/null || true
fi

%postun
systemctl daemon-reload 2>/dev/null || true

%files
%defattr(-,root,root,-)
%{_bindir}/uroamd
%{_bindir}/ramctl
%{_libdir}/libaal.a
%{_libdir}/libmemopt.a
%{_includedir}/uroam_aal.h
%{_includedir}/uroam.h
%dir %{_sysconfdir}/uroam
%config(noreplace) %{_sysconfdir}/uroam/uroam.toml
%{_unitdir}/uroam.service
%dir %{_rundir}/uroam
%dir %{_localstatedir}/log/uroam

%files devel
%defattr(-,root,root,-)
%{_libdir}/libaal.a
%{_libdir}/libmemopt.a
%{_includedir}/uroam_aal.h
%{_includedir}/uroam.h

%changelog
* Thu Apr 23 2026 UROAM Team <team@uroam.org> 1.0.0-1
- Initial release
- Architecture Abstraction Layer (AAL) for 7 architectures
- Rust-based daemon with workload classification
- Memory compression via ZRAM/Zswap
- Gaming and idle optimizations
- CLI tool (ramctl)
