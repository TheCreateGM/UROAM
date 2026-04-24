#!/bin/bash
# UROAM Install Script
# Download and install UROAM from GitHub release

set -e

REPO="TheCreateGM/UROAM"
VERSION="v1.0.0"
BASE_URL="https://github.com/${REPO}/releases/download/${VERSION}"

echo "UROAM Installer v${VERSION}"
echo "========================"

# Check for root
if [[ $EUID -ne 0 ]]; then
    echo "Error: Run as root (sudo)"
    exit 1
fi

# Detect OS
if [[ -f /etc/fedora-release ]]; then
    OS="fedora"
    ARCH=$(uname -m)
    if [[ "$ARCH" == "x86_64" ]]; then
        ARCH="x86_64"
    fi
elif [[ -f /etc/redhat-release ]]; then
    OS="rhel"
else
    echo "Warning: Not Fedora/RHEL - continuing anyway"
    OS="linux"
fi

echo "Detected: $OS ($ARCH)"

# Download RPMs
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "Downloading..."
curl -sL "${BASE_URL}/uroam-1.0.0-1.fc43.${ARCH}.rpm" -o "${TMPDIR}/uroam.rpm"
curl -sL "${BASE_URL}/uroam-cli-1.0.0-1.fc43.${ARCH}.rpm" -o "${TMPDIR}/uroam-cli.rpm"

# Install
echo "Installing..."
rpm -ivh "${TMPDIR}/uroam.rpm" "${TMPDIR}/uroam-cli.rpm"

echo ""
echo "Installed!"
echo ""
echo "Commands:"
echo "  ramctl status    - Show memory status"
echo "  ramctl optimize - Run optimization"
echo "  ramctl profile - Manage profiles"
echo ""
echo "Config: /etc/uroam/uroam.toml"