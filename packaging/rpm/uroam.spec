%global version 1.0.0
%global release 4.fc43

Name:           uroam
Version:       %{version}
Release:       %{release}
Summary:      Unified RAM Optimization and Management Framework
License:       GPL-3.0-or-later
URL:           https://github.com/TheCreateGM/UROAM
Source0:      https://github.com/TheCreateGM/UROAM/releases/download/v1.0.0/uroam-1.0.0-1.fc43.x86_64.rpm
Source1:      https://github.com/TheCreateGM/UROAM/releases/download/v1.0.0/uroam-cli-1.0.0-1.fc43.x86_64.rpm

BuildArch:    x86_64
Requires:    rpm

%description
UROAM is a production-grade, low-level system framework for Linux that optimizes
RAM utilization across heterogeneous workloads.

%package cli
Summary:      UROAM CLI tool
Requires:     %{name} = %{version}-%{release}

%description cli
Command-line interface for UROAM daemon.

%prep
cd %{_builddir}
mkdir -p usr/bin etc
cd usr/bin
rpm2cpio %{SOURCE0} | cpio -idmv 2>/dev/null || true
rpm2cpio %{SOURCE1} | cpio -idmv 2>/dev/null || true

%build
# No build needed

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sysconfdir}/uroam
mkdir -p %{buildroot}%{_localstatedir}/log/uroam
mkdir -p %{buildroot}%{_rundir}/uroam

cp -a usr/bin/* %{buildroot}%{_bindir}/ 2>/dev/null || true

%files
%{_bindir}/uroamd
%dir %{_sysconfdir}/uroam
%dir %{_localstatedir}/log/uroam
%dir %{_rundir}/uroam

%files cli
%{_bindir}/uroamctl