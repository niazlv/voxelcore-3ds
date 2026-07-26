#!/bin/bash
# Cross-compile LuaJIT as a static library for the 3DS (devkitARM).
#
# The exact configuration matters:
# - TARGET_SYS=Other: bare-metal newlib, no mmap/dlopen
# - LUAJIT_DISABLE_JIT: no RWX pages on the 3DS, interpreter only
# - LUAJIT_USE_SYSMALLOC: the port provides malloc (src/platform/heap_3ds.c)
# - LUAJIT_SECURITY_PRNG=0: no OS entropy source on TARGET_SYS=Other
# - HOST_CC must produce 32-bit binaries: buildvm's pointer size has to
#   match the target's (needs gcc-multilib + libc6-dev-i386 on the host)
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
LJ="$DIR/external/luajit"
LUAJIT_SHA=a471ab78c7b670b4f92dae111fc3c96fb824c768

# normally provided by the git submodule; clone directly as a fallback
if [ ! -f "$LJ/src/luajit.h" ]; then
    git clone https://github.com/LuaJIT/LuaJIT.git "$LJ"
    git -C "$LJ" checkout "$LUAJIT_SHA"
fi

export PATH=/opt/devkitpro/devkitARM/bin:$PATH
make -C "$LJ/src" -j"$(nproc)" libluajit.a \
    HOST_CC="gcc -m32" \
    CROSS=arm-none-eabi- \
    TARGET_SYS=Other \
    TARGET_CFLAGS="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft" \
    XCFLAGS="-DLUAJIT_SECURITY_PRNG=0 -DLUAJIT_DISABLE_JIT -DLUAJIT_USE_SYSMALLOC" \
    BUILDMODE=static
