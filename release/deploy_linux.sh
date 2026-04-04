#!/bin/bash
set -e # Exit immediately if a command fails

TARGET_NAME="ModemBridgeTray"
APP_NAME="modembridge" # Debian packages use lowercase
VERSION="1.0.0"

# This dynamically grabs 'amd64' when run on an AMD/x86_64 Debian system
ARCH=$(dpkg --print-architecture) 
BUILD_DIR="../build_linux"
PKG_NAME="${APP_NAME}_${VERSION}_${ARCH}"

echo "==> Building for Linux ($ARCH)..."
cmake -S .. -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Release
cmake --build $BUILD_DIR --config Release -j$(nproc)

# Create DEB structure
mkdir -p ${PKG_NAME}/usr/local/bin
mkdir -p ${PKG_NAME}/DEBIAN

# Copy the exact target built by CMake
cp $BUILD_DIR/$TARGET_NAME ${PKG_NAME}/usr/local/bin/$TARGET_NAME

# Generate the control file
cat <<EOF > ${PKG_NAME}/DEBIAN/control
Package: ${APP_NAME}
Version: ${VERSION}
Architecture: ${ARCH}
Maintainer: Paul <your.email@example.com>
Description: Atari 8-bit to Modern Network Bridge for Qt6
EOF

# Build the package
dpkg-deb --build ${PKG_NAME}
mv ${PKG_NAME}.deb ../release/

# Cleanup only the packaging folder, leave the CMake build cache intact
rm -rf ${PKG_NAME}
# rm -rf $BUILD_DIR

echo "Done! ${PKG_NAME}.deb is in release/."