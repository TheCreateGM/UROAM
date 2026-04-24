#!/bin/bash
set -eu

echo "=== Installing build dependencies ==="
dnf install -y rust cargo pkgconfig(openssl) openssl-devel git make gcc

echo "=== Cloning UROAM repository ==="
cd /tmp
git clone https://github.com/TheCreateGM/UROAM.git
cd UROAM

echo "=== Building Rust components ==="
cargo build --release

echo "=== Creating source tarball ==="
mkdir -p build-artifacts
cp target/release/uroamd build-artifacts/
cp target/release/ramctl build-artifacts/

echo "=== Build complete ==="
ls -la build-artifacts/