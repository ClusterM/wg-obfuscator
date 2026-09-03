#!/bin/bash
# Build script for luci-app-wg-obfuscator OpenWrt package
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

# Get the script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_NAME="luci-app-wg-obfuscator"
# luci.mk splits translations into their own package
I18N_NAME="luci-i18n-wg-obfuscator-ru"

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

if [ ! -f "$OPENWRT_BUILD_DIR/feeds/luci/luci.mk" ]; then
    print_error "LuCI feed not found at $OPENWRT_BUILD_DIR/feeds/luci"
    print_error "Install it first:"
    print_error "  cd $OPENWRT_BUILD_DIR && ./scripts/feeds update -a && ./scripts/feeds install -a -p luci"
    exit 1
fi

# The Makefile is the one submitted to the LuCI feed, so it pulls in luci.mk as
# ../../luci.mk and luci.mk derives LUCI_NAME from the directory name. Both only
# hold inside the feed, hence a copy into applications/ rather than a symlink
# from package/ straight to this checkout.
FEED_APP_DIR="$OPENWRT_BUILD_DIR/feeds/luci/applications/$PACKAGE_NAME"
PACKAGE_DIR="$OPENWRT_BUILD_DIR/package/luci/$PACKAGE_NAME"

print_status "Staging package into the LuCI feed..."
rm -rf "$FEED_APP_DIR"
mkdir -p "$FEED_APP_DIR"
(cd "$SCRIPT_DIR" && tar -cf - --exclude=build.sh .) | (cd "$FEED_APP_DIR" && tar -xf -)
print_status "  → $FEED_APP_DIR"

# Remove old symlink/directory if it exists
if [ -L "$PACKAGE_DIR" ]; then
    rm "$PACKAGE_DIR"
elif [ -d "$PACKAGE_DIR" ]; then
    rm -rf "$PACKAGE_DIR"
fi

mkdir -p "$(dirname "$PACKAGE_DIR")"

print_status "Creating symlink..."
ln -sf "$FEED_APP_DIR" "$PACKAGE_DIR"
print_status "  → $PACKAGE_DIR -> $FEED_APP_DIR"

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

# Enable the app and its translation in config if not already enabled
if ! grep -q "CONFIG_PACKAGE_$PACKAGE_NAME=y" .config 2>/dev/null; then
    print_status "Enabling package in .config..."
    echo "CONFIG_PACKAGE_$PACKAGE_NAME=y" >> .config
    echo "CONFIG_PACKAGE_$I18N_NAME=y" >> .config
    make defconfig
fi

# Build package (install is not needed - package file is created during compile)
set +e
make package/$PACKAGE_NAME/{clean,compile} \
    CONFIG_PACKAGE_$PACKAGE_NAME=y \
    CONFIG_PACKAGE_$I18N_NAME=y \
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
    # PKGARCH:=all lands in bin/targets/<target>/<subtarget>/packages rather
    # than in bin/packages/<arch>, so search the whole output tree.
    mapfile -t PKG_FILES < <(find bin \
        \( -name "${PACKAGE_NAME}*.ipk" -o -name "${PACKAGE_NAME}*.apk" \
           -o -name "${I18N_NAME}*.ipk" -o -name "${I18N_NAME}*.apk" \) 2>/dev/null | sort)
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
