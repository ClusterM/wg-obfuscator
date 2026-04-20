#!/bin/sh

# Trimmed for the ramanarupa/wg0-obfuscator fork — only linux/arm64 is
# packaged (for MikroTik hAP ax^3 / RouterOS container runtime).
PLATFORMS="linux/arm64"
MULTIARCH_IMAGE="wg0-obfuscator:multiarch"
TARGET_DIR=containers

rm -rf "$TARGET_DIR"
mkdir -p "$TARGET_DIR"

PLATFORMS_LIST=$(echo "$PLATFORMS" | tr ',' ' ')

for PLATFORM in $PLATFORMS_LIST; do
    ARCH=$(echo "$PLATFORM" | sed 's|linux/||;s|/|-|g')
    IMAGE_NAME="wg0-obfuscator:$ARCH"
    TAR_NAME="wg0-obfuscator-$ARCH.tar"

    echo "===== Building for $PLATFORM ====="
    docker buildx build --platform $PLATFORM -t $IMAGE_NAME --output type=docker .
    docker save $IMAGE_NAME -o "$TARGET_DIR/$TAR_NAME"
    echo "===== Done: $TARGET_DIR/$TAR_NAME ====="
done

echo "===== Building multiarch image ====="
docker buildx build --platform $PLATFORMS -t $MULTIARCH_IMAGE --output type=docker .
docker save $MULTIARCH_IMAGE -o "$TARGET_DIR/wg0-obfuscator-multiarch.tar"
echo "===== Done! ====="
