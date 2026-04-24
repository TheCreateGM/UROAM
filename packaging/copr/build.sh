#!/bin/bash
set -e

echo "=== Starting COPR build ==="

# Use Athens proxy which might be more reliable
export GOPROXY=https://goproxy.cn,direct
export GONOSUMDB=off

cd /tmp/target

# Clean build directory  
rm -rf _build uroamd uroamctl 2>/dev/null || true

# GOPATH setup  
mkdir -p _build/src/github.com/axogm
ln -s $(pwd) _build/src/github.com/axogm/uroam 2>/dev/null || true  
export GOPATH=$(pwd)/_build

echo "=== Building daemon ==="
cd daemon
go build -ldflags "-s -w" -o ../uroamd . || { echo "Daemon build failed"; exit 1; }
cd ..

echo "=== Building CLI ==="  
cd cli
go build -ldflags "-s -w" -o ../uroamctl . || { echo "CLI build failed"; exit 1; }
cd ..

echo "=== Binaries built ==="
ls -la uroamd uroamctl

echo "=== Build complete ==="