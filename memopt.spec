Name:           memopt
Version:        1.0.0
Release:        1%{?dist}
Summary:        Universal RAM Optimization Layer for AI Workloads on Linux
License:        MIT
URL:            https://github.com/TheCreateGM/UROAM
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc, make, clang, numactl-devel, go >= 1.20
Requires:       glibc >= 2.34, numactl-libs

%description
Universal RAM Optimization Layer (memopt) is a system-level RAM optimization
framework for Linux that improves memory efficiency, allocation, and utilization
for AI workloads across different environments.

%prep
%setup -q

%build
# Build C library
make lib

# Build CLI
cd cli && go build -ldflags "-s -w" -o ../memopt
cd ..

# Build daemon
cd daemon && go build -ldflags "-s -w" -o ../memopt-daemon
cd ..

%install
# Install library
install -Dm755 libmemopt.so %{buildroot}/%{_libdir}/libmemopt.so

# Install CLI
install -Dm755 memopt %{buildroot}/%{_bindir}/memopt

# Install daemon
install -Dm755 memopt-daemon %{buildroot}/%{_bindir}/memopt-daemon

# Install header
install -Dm644 include/memopt.h %{buildroot}/%{_includedir}/memopt.h

# Install systemd service
install -Dm644 scripts/memopt.service %{buildroot}/%{_unitdir}/memopt.service

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%{_libdir}/libmemopt.so
%{_bindir}/memopt
%{_bindir}/memopt-daemon
%{_includedir}/memopt.h
%{_unitdir}/memopt.service

%changelog
* Mon Apr 20 2026 AxoGM <creategm10@proton.me> - 1.0.0-1
- Initial RPM package
