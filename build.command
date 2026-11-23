#!/bin/bash
# ==========================================================================
#  Crate XL - macOS build script (double-click in Finder, or run in Terminal)
#  Requires: Xcode or the Command Line Tools (xcode-select --install),
#            CMake 3.22+ (brew install cmake, or the cmake.org .dmg), Git.
# ==========================================================================
set -e
cd "$(dirname "$0")"

# CMake.app from cmake.org is not on PATH by default
if ! command -v cmake >/dev/null 2>&1; then
    if [ -x "/Applications/CMake.app/Contents/bin/cmake" ]; then
        export PATH="/Applications/CMake.app/Contents/bin:$PATH"
    else
        echo "[ERROR] CMake not found. Install it with 'brew install cmake' or from https://cmake.org/download/"
        read -n 1 -s -r -p "Press any key to close..."
        exit 1
    fi
fi
if ! xcode-select -p >/dev/null 2>&1; then
    echo "[ERROR] Xcode Command Line Tools not found. Run: xcode-select --install"
    read -n 1 -s -r -p "Press any key to close..."
    exit 1
fi

GEN=""
if command -v ninja >/dev/null 2>&1; then GEN="-G Ninja"; fi

echo "=== Configuring (first run downloads JUCE, this can take a few minutes) ==="
cmake -S . -B build $GEN -DCMAKE_BUILD_TYPE=Release

echo "=== Building Release ==="
cmake --build build --config Release --parallel

# Newest .app wins, so an app built under an older name is never picked up
APP=$(ls -td build/Crate_artefacts/*.app build/Crate_artefacts/*/*.app 2>/dev/null | head -n 1)
if [ -z "$APP" ]; then echo "[ERROR] The app was not found after the build."; read -n 1 -s -r -p "Press any key..."; exit 1; fi
mkdir -p dist
NAME=$(basename "$APP")
rm -rf "dist/$NAME"
cp -R "$APP" dist/
# Ad-hoc sign so Gatekeeper lets a locally built app run
codesign --force --deep -s - "dist/$NAME" >/dev/null 2>&1 || true
xattr -dr com.apple.quarantine "dist/$NAME" 2>/dev/null || true

echo ""
echo "=== Done ==="
echo "The app is at: $(pwd)/dist/$NAME"
open dist 2>/dev/null || true
read -n 1 -s -r -p "Press any key to close..."
