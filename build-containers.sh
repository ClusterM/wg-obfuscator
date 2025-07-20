#!/bin/sh

PLATFORMS="linux/amd64,linux/arm64,linux/arm/v7,linux/arm/v6,linux/386,linux/ppc64le,linux/s390x"
MULTIARCH_IMAGE="clustermeerkat/wg-obfuscator:multiarch"
TARGET_DIR=containers

rm -rf "$TARGET_DIR"
mkdir -p "$TARGET_DIR"

PLATFORMS_LIST=$(echo "$PLATFORMS" | tr ',' ' ')

for PLATFORM in $PLATFORMS_LIST; do
    ARCH=$(echo "$PLATFORM" | sed 's|linux/||;s|/|-|g')
    IMAGE_NAME="clustermeerkat/wg-obfuscator:$ARCH"
    TAR_NAME="wg-obfuscator-$ARCH.tar"

    echo "===== Building for $PLATFORM ====="
    docker buildx build --platform $PLATFORM -t $IMAGE_NAME --output type=docker .
    docker save $IMAGE_NAME -o "$TARGET_DIR/$TAR_NAME"
    echo "===== Done: $TARGET_DIR/$TAR_NAME ====="
done

echo "===== Building multiarch image ====="
docker buildx build --platform $PLATFORMS -t $MULTIARCH_IMAGE --output type=docker .
docker save $MULTIARCH_IMAGE -o "$TARGET_DIR/wg-obfuscator-multiarch.tar"
echo "===== Done! ====="
