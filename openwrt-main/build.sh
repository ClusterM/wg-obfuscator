#!/bin/bash
# Build script for wg-obfuscator OpenWrt package
# Copyright (C) 2024-2025 Alexey Cluster <cluster@cluster.wtf>
# Licensed under GPLv3

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Get the script and project directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_NAME="wg-obfuscator"

print_status "Building $PACKAGE_NAME OpenWrt package..."

# Check if OpenWrt build system is available
if [ -z "$OPENWRT_BUILD_DIR" ]; then
    print_error "OPENWRT_BUILD_DIR environment variable is not set"
    print_error "Please set it to your OpenWrt SDK directory"
    print_error "Example: export OPENWRT_BUILD_DIR=~/openwrt-sdk"
    exit 1
fi

if [ ! -f "$OPENWRT_BUILD_DIR/rules.mk" ]; then
    print_error "OpenWrt build system not found at $OPENWRT_BUILD_DIR"
    print_error "Please set OPENWRT_BUILD_DIR to a valid OpenWrt SDK directory"
    exit 1
fi

# Package directory in OpenWrt build system
PACKAGE_DIR="$OPENWRT_BUILD_DIR/package/network/$PACKAGE_NAME"

print_status "Setting up package directory..."

# Remove old symlink/directory if it exists
if [ -L "$PACKAGE_DIR" ]; then
    rm "$PACKAGE_DIR"
elif [ -d "$PACKAGE_DIR" ]; then
    rm -rf "$PACKAGE_DIR"
fi

# Create parent directory
mkdir -p "$(dirname "$PACKAGE_DIR")"

# Create symlink to our package directory
print_status "Creating symlink..."
ln -sf "$SCRIPT_DIR" "$PACKAGE_DIR"
print_status "  → $PACKAGE_DIR -> $SCRIPT_DIR"

# Make scripts executable
chmod +x "$SCRIPT_DIR/files/wg-obfuscator.init"
chmod +x "$SCRIPT_DIR/files/wg-obfuscator-config.sh"

print_status "Symlink created successfully"

# Build the package
print_status "Building package..."
cd "$OPENWRT_BUILD_DIR"

make package/$PACKAGE_NAME/{clean,compile,install} \
    CONFIG_PACKAGE_$PACKAGE_NAME=y \
    V=s \
    2>&1 | tee /tmp/$PACKAGE_NAME-build.log | tail -20

BUILD_STATUS=$?

# Check if .ipk was created
if [ $BUILD_STATUS -eq 0 ] || [ $BUILD_STATUS -eq 2 ] || \
   grep -q "Packaged contents.*$PACKAGE_NAME.*\.ipk" /tmp/$PACKAGE_NAME-build.log; then
    IPK_FILE=$(find bin/packages -name "${PACKAGE_NAME}_*.ipk" 2>/dev/null | head -1)
    if [ -n "$IPK_FILE" ]; then
        print_status "Package built successfully!"
        print_status "Package file: $IPK_FILE"
        FILE_SIZE=$(ls -lh "$IPK_FILE" | awk '{print $5}')
        print_status "Package size: $FILE_SIZE"
        
        # Update package index
        print_status "Updating package index..."
        make package/index >/dev/null 2>&1
        
        exit 0
    else
        print_error ".ipk file was not created!"
        exit 1
    fi
else
    print_error "Package build failed!"
    exit 1
fi


