Name:           uroam
Version:        1.0.0
Release:        1%{?dist}
Summary:        Universal RAM Optimization Layer
License:        GPL-2.0-only
URL:            https://github.com/uroam/uroam
BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  gcc
BuildRequires:  kernel-headers
BuildRequires:  make
BuildRequires:  golang
BuildRequires:  python3-devel
BuildRequires:  numactl-devel
BuildRequires:  libbpf-devel
Requires:       %{name}-libs = %{version}-%{release}
Requires:       systemd
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-buildroot

%description
UROAM (Universal RAM Optimization Layer) is a system-level RAM optimization
framework for Linux that intelligently manages memory usage across AI workloads,
gaming, and general applications to reduce waste and improve performance.

%package libs
Summary:        UROAM shared libraries

%package devel
Summary:        UROAM development files
Requires:       %{name}-libs = %{version}-%{release}

%prep
%autosetup -p1

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=%{_usr} \
          -DLIB_INSTALL_DIR=%{_libdir}

%build
cd build
make -j$(nproc)

%check
cd build
ctest --output-on-failure

%install
cd build
make install DESTDIR=%{buildroot}

%post libs -p /sbin/ldconfig

%postun libs -p /sbin/ldconfig

%files
%doc README.md LICENSE
%{_bindir}/uroamd
%{_bindir}/uroamctl
%{_sysconfdir}/uroam/uroam.conf
%{_unitdir}/uroam.service

%files libs
%{_libdir}/libmemopt.so.*

%files devel
%{_libdir}/libmemopt.so
%{_includedir}/memopt.h
%{_includedir}/uroam_client.h
%{_libdir}/pkgconfig/memopt.pc
%{_libdir}/cmake/uroam/

%changelog
* Mon Apr 20 2026 UROAM Team <uroam@example.com> - 1.0.0-1
- Initial RPM package release