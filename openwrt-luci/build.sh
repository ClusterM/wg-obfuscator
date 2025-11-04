#!/bin/bash
# Build script for luci-app-wg-obfuscator OpenWrt package
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

# Get the script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_NAME="luci-app-wg-obfuscator"

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
PACKAGE_DIR="$OPENWRT_BUILD_DIR/package/luci/$PACKAGE_NAME"

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

print_status "Symlink created successfully"

# Build the package
print_status "Building package..."
cd "$OPENWRT_BUILD_DIR"

# Check if .config exists (required for proper SDK configuration)
if [ ! -f .config ]; then
    print_error "OpenWrt SDK is not configured. Please configure it first."
    echo ""
    print_status "To configure the SDK, run the following commands:"
    echo ""
    echo "  cd $OPENWRT_BUILD_DIR"
    echo "  make defconfig"
    echo ""
    echo "This will create a default .config file for your target architecture."
    echo "After that, you can run this build script again."
    echo ""
    echo "Alternatively, you can use menuconfig to configure manually:"
    echo "  make menuconfig"
    echo ""
    exit 1
fi

# Enable package in config if not already enabled
if ! grep -q "CONFIG_PACKAGE_$PACKAGE_NAME=y" .config 2>/dev/null; then
    print_status "Enabling package in .config..."
    echo "CONFIG_PACKAGE_$PACKAGE_NAME=y" >> .config
    make defconfig
fi

# Build package (install is not needed - .ipk is created during compile)
make package/$PACKAGE_NAME/{clean,compile} \
    CONFIG_PACKAGE_$PACKAGE_NAME=y \
    V=s \
    2>&1 | tee /tmp/$PACKAGE_NAME-build.log | tail -20

BUILD_STATUS=$?

# Check if .ipk was created
if [ $BUILD_STATUS -eq 0 ] || [ $BUILD_STATUS -eq 2 ] || \
   grep -q "Packaged contents.*$PACKAGE_NAME.*\.ipk" /tmp/$PACKAGE_NAME-build.log; then
    IPK_FILE=$(find bin/packages -name "${PACKAGE_NAME}*.ipk" 2>/dev/null | head -1)
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


