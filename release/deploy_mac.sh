#!/bin/bash
set -e # Exit immediately if a command fails

TARGET_NAME="ModemBridgeTray"
APP_NAME="ModemBridge"
BUILD_DIR="../build_mac"

echo "==> Building $TARGET_NAME for macOS (Universal)..."
cmake -S .. -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build $BUILD_DIR --config Release -j$(sysctl -n hw.ncpu)

echo "==> Packaging DMG..."
# macdeployqt requires the exact name of the compiled .app bundle
macdeployqt $BUILD_DIR/${TARGET_NAME}.app -dmg

# Move deliverable to root and rename to the desired app name
mv $BUILD_DIR/${TARGET_NAME}.dmg ./${APP_NAME}.dmg

# Optional: Leave the build directory intact for faster incremental builds
# rm -rf $BUILD_DIR

echo "Done! DMG is in the current directory."