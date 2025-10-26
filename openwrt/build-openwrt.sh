#!/bin/bash
# Build script for WireGuard Obfuscator OpenWRT package

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

# Check if we're in the right directory
if [ ! -f "Makefile" ] || [ ! -d "files" ]; then
    print_error "This script must be run from the openwrt/ directory"
    exit 1
fi

print_status "Building WireGuard Obfuscator OpenWRT package..."

# Check if OpenWRT build system is available
if [ -z "$OPENWRT_BUILD_DIR" ]; then
    print_warning "OPENWRT_BUILD_DIR not set, assuming current directory is OpenWRT build root"
    OPENWRT_BUILD_DIR="."
fi

if [ ! -f "$OPENWRT_BUILD_DIR/rules.mk" ]; then
    print_error "OpenWRT build system not found at $OPENWRT_BUILD_DIR"
    print_error "Please set OPENWRT_BUILD_DIR environment variable to your OpenWRT build directory"
    exit 1
fi

# Create package directory in OpenWRT build system
PACKAGE_DIR="$OPENWRT_BUILD_DIR/package/network/wg-obfuscator"
print_status "Creating package directory: $PACKAGE_DIR"

mkdir -p "$PACKAGE_DIR"

# Copy files
print_status "Copying package files..."
cp Makefile "$PACKAGE_DIR/"
cp -r files "$PACKAGE_DIR/"

# Make scripts executable
chmod +x "$PACKAGE_DIR/files/wg-obfuscator.init"
chmod +x "$PACKAGE_DIR/files/wg-obfuscator-config.sh"

print_status "Package files copied successfully"

# Build the package
print_status "Building package..."
cd "$OPENWRT_BUILD_DIR"
make package/wg-obfuscator/compile

if [ $? -eq 0 ]; then
    print_status "Package built successfully!"
    print_status "Package files are located in: $OPENWRT_BUILD_DIR/bin/packages/*/network/wg-obfuscator_*.ipk"
else
    print_error "Package build failed!"
    exit 1
fi

print_status "Build completed successfully!"