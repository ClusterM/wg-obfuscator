#!/bin/bash
# Build script for wg-obfuscator OpenWrt package
# Copyright (C) 2024-2026 Alexey Cluster <cluster@cluster.wtf>
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

usage() {
    cat <<EOF
Usage: $0 [--local-src]

By default the package is built exactly as an OpenWrt feed would build it, from
the upstream revision pinned in the Makefile.

  --local-src   Build the checkout this script lives in instead of the pinned
                revision. Uses the stock USE_SOURCE_DIR mechanism, so the
                Makefile itself stays feed-clean.

Environment:
  OPENWRT_BUILD_DIR   Path to a configured OpenWrt SDK or buildroot (required)
EOF
}

# Get the script and project directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PACKAGE_NAME="wg-obfuscator"

LOCAL_SRC=0
while [ $# -gt 0 ]; do
    case "$1" in
        --local-src) LOCAL_SRC=1 ;;
        -h|--help) usage; exit 0 ;;
        *) print_error "Unknown argument: $1"; usage; exit 1 ;;
    esac
    shift
done

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
chmod +x "$SCRIPT_DIR/test-version.sh"

print_status "Symlink created successfully"

# USE_SOURCE_DIR makes OpenWrt symlink the build directory at the given tree, so
# it gets a copy of the checkout rather than the checkout itself - otherwise
# cross-compiled objects would land next to (and collide with) host builds.
MAKE_ARGS=()
if [ $LOCAL_SRC -eq 1 ]; then
    SRC_COPY="${TMPDIR:-/tmp}/$PACKAGE_NAME-local-src-$(id -u)"
    print_status "Building local source tree: $PROJECT_DIR"
    rm -rf "$SRC_COPY"
    mkdir -p "$SRC_COPY"
    cp "$PROJECT_DIR"/Makefile "$PROJECT_DIR"/LICENSE "$SRC_COPY/"
    cp "$PROJECT_DIR"/*.c "$PROJECT_DIR"/*.h "$SRC_COPY/"
    MAKE_ARGS+=("USE_SOURCE_DIR=$SRC_COPY")
else
    print_status "Building pinned upstream revision from the package Makefile"
fi

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

# Build package (install is not needed - package file is created during compile)
set +e
make package/$PACKAGE_NAME/{clean,download,prepare,compile} \
    CONFIG_PACKAGE_$PACKAGE_NAME=y \
    "${MAKE_ARGS[@]}" \
    V=s \
    2>&1 | tee /tmp/$PACKAGE_NAME-build.log | tail -20
BUILD_STATUS=${PIPESTATUS[0]}
set -e

# Soft success signal from log (opkg .ipk or apk packaging)
LOG_PACKAGED=0
if grep -qE "Packaged contents.*$PACKAGE_NAME|apk mkpkg.*name:$PACKAGE_NAME|--output \".*$PACKAGE_NAME.*\.apk\"" /tmp/$PACKAGE_NAME-build.log 2>/dev/null; then
    LOG_PACKAGED=1
fi

# Accept both OpenWrt ≤24 (.ipk / opkg) and OpenWrt ≥25 (.apk / apk)
if [ $BUILD_STATUS -eq 0 ] || [ $BUILD_STATUS -eq 2 ] || [ $LOG_PACKAGED -eq 1 ]; then
    mapfile -t PKG_FILES < <(find bin \( -name "${PACKAGE_NAME}*.ipk" -o -name "${PACKAGE_NAME}*.apk" \) 2>/dev/null | sort)
    if [ ${#PKG_FILES[@]} -gt 0 ] && [ -n "${PKG_FILES[0]}" ]; then
        print_status "Package built successfully!"
        for PKG_FILE in "${PKG_FILES[@]}"; do
            FILE_SIZE=$(ls -lh "$PKG_FILE" | awk '{print $5}')
            print_status "Package file: $PKG_FILE ($FILE_SIZE)"
        done

        # Update package index
        print_status "Updating package index..."
        make package/index >/dev/null 2>&1

        exit 0
    else
        print_error "Package file (.ipk or .apk) was not created!"
        exit 1
    fi
else
    print_error "Package build failed!"
    exit 1
fi
