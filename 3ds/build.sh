#!/bin/bash
# Build VoxelCore 3DS port inside devkitPro docker image.
# Usage: ./build.sh [clean]
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"

if [ "$1" = "clean" ]; then
    rm -rf "$REPO/3ds/build"
fi

docker run --rm -v "$REPO:/work" devkitpro/devkitarm:latest bash -c '
set -e
cd /work/3ds
if [ ! -f external/luajit/src/libluajit.a ]; then
    # 32-bit host toolchain for LuaJIT buildvm
    apt-get update -qq && apt-get install -y -qq gcc-multilib libc6-dev-i386
    ./build_luajit.sh
fi
/opt/devkitpro/portlibs/3ds/bin/arm-none-eabi-cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
cmake --build build -- -j$(nproc) 2>&1
'
