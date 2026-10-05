#!/bin/bash
# Configure and build the Linux targets. Run from anywhere:
#   wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build.sh [target]
# Default target builds everything Linux currently supports.
set -e
cd "$(dirname "$0")/../.." || exit 1

TARGET="${1:-aae_headless}"

cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --target "$TARGET"

echo "--- built: $TARGET ---"
ls -l "build-linux/$TARGET" 2>/dev/null || true
