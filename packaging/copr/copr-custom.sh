#!/bin/bash
set -ex

# Custom script method runs as root with network access
# Download the pre-built binary RPMs from local rpmbuild
curl -L -o /tmp/uroam.rpm "file:///home/axogm/rpmbuild/RPMS/x86_64/uroam-1.0.0-1.fc43.x86_64.rpm"
curl -L -o /tmp/uroam-cli.rpm "file:///home/axogm/rpmbuild/RPMS/x86_64/uroam-cli-1.0.0-1.fc43.x86_64.rpm"

# Install them to prepare for running (or just verify they exist)
rpm -qlp /tmp/uroam.rpm || true
rpm -qlp /tmp/uroam-cli.rpm || true

echo "=== RPMs downloaded successfully ==="
ls -la /tmp/*.rpm