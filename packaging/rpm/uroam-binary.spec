%global src_name uroam
%global version 1.0.0

Name:           uroam
Version:       %{version}
Release:       1%{?dist}
Summary:      Unified RAM Optimization and Management Framework
License:       GPL-3.0-or-later
URL:           https://github.com/TheCreateGM/UROAM
Source0:       %{name}-%{version}.tar.gz

BuildArch:     x86_64
# No build needed - binaries are pre-built
BuildRequires: trivial

%description
UROAM is a production-grade, low-level system framework for Linux that optimizes
RAM utilization across heterogeneous workloads including AI/ML, gaming, rendering,
and general applications.

%package cli
Summary:      UROAM CLI tool

%description cli
Command-line interface for UROAM.

%prep
# No preparation needed - use prebuilt binaries

%build
# No build - binaries already exist

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_includedir}
mkdir -p %{buildroot}%{_sysconfdir}/uroam

# Install pre-built binaries
install -m 755 %{_builddir}/bin/uroamd %{buildroot}%{_bindir}/
install -m 755 %{_builddir}/bin/uroamctl %{buildroot}%{_bindir}/

%files
%{_bindir}/uroamd
%doc README.md

%files cli
%{_bindir}/uroamctl