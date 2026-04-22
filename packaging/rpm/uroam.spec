Name:           uroam
Version:        1.0.0
Release:        1%{?dist}
Summary:        Unified RAM Optimization Framework

License:        GPL-3.0-or-later
URL:            https://github.com/example/uroam
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  python3-devel, python3-pip, gcc, git
Requires:       python3, libc, libstdc++

%description
A production-grade low-level system framework for RAM optimization
on Linux. Provides dynamic memory management, compression,
deduplication, and caching across heterogeneous workloads.

%prep
%setup -q

%build
# Nothing to build for Python-based daemon

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/lib/uroam
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/lib/systemd/system

cp -r src/* %{buildroot}/usr/lib/uroam/
cp packaging/systemd/uroam.service %{buildroot}/usr/lib/systemd/system/

%files
/usr/lib/uroam/
/usr/bin/uroam
/usr/lib/systemd/system/uroam.service

%changelog
* Wed Apr 22 2026 UROAM Team <team@uroam.example.com> - 1.0.0-1
- Initial package
