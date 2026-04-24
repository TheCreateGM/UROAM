Name:           uroam
Version:        1.0.0
Release:        1%{?dist}
Summary:        Unified RAM Optimization and Management Framework
License:       GPL-3.0
URL:           https://github.com/TheCreateGM/UROAM
Source0:       %{name}-%{version}.tar.gz
BuildRequires:  golang >= 1.20
Requires:      glibc >= 2.34

# Disable debug package generation
%global debug_package %{nil}

%description
UROAM is a production-grade, low-level system framework for Linux that optimizes 
RAM utilization across heterogeneous workloads including AI/ML, gaming, rendering, 
and general applications.

%package cli
Summary:       UROAM CLI control tool

%description cli
Command-line interface for UROAM daemon control.

%prep
%setup -q

%build
# Set up GOPATH for Go build
mkdir -p _build/src/github.com/axogm
ln -s $(pwd) _build/src/github.com/axogm/uroam
export GOPATH=$(pwd)/_build:%{gopath}

# Build daemon from daemon/ directory  
cd daemon
go build -ldflags "-s -w" -o ../uroamd .
cd ..

# Build CLI from cli/ directory
cd cli
go build -ldflags "-s -w" -o ../uroamctl .
cd ..

%install
# Install daemon/CLI binaries
install -Dm755 uroamd %{buildroot}%{_bindir}/uroamd
install -Dm755 uroamctl %{buildroot}%{_bindir}/uroamctl  
# Install config
install -Dm644 etc/uroam.conf %{buildroot}%{_sysconfdir}/uroam.conf

%check
true

%files
%defattr(-,root,root,-)
%doc README.md LICENSE
%{_bindir}/uroamd
%{_sysconfdir}/uroam.conf

%files cli
%defattr(-,root,root,-)
%{_bindir}/uroamctl

%changelog
* Thu Apr 24 2026 UROAM Team <uroam@example.com> - 1.0.0-1
- Initial RPM package for COPR