#!/bin/bash
# Package the built 3DS port as an installable CIA.
#
# Inputs, all produced elsewhere in this tree:
#   3ds/build/voxelcore3ds.elf   - cmake --build (see 3ds/build.sh)
#   3ds/build/voxelcore3ds.smdh  - generated from 3ds/icon.png by smdhtool
#   3ds/cia/banner.bnr           - rebuilt from banner.png by 3ds/tools/mkbanner.py
#   3ds/cia/app.rsf              - makerom spec
#
# makerom is NOT part of devkitPro's devkitARM image and is not installed by
# build.sh; install it yourself (Project_CTR) and put it on PATH.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
BUILD="$REPO/3ds/build"

if ! command -v makerom >/dev/null 2>&1; then
    echo "makerom not found on PATH." >&2
    echo "Get it from https://github.com/3DSGuy/Project_CTR (releases) and re-run." >&2
    exit 1
fi

for f in "$BUILD/voxelcore3ds.elf" "$BUILD/voxelcore3ds.smdh" "$HERE/banner.bnr"; do
    [ -f "$f" ] || { echo "missing $f — build first" >&2; exit 1; }
done

# Deliberately does NOT rebuild banner.bnr: it is produced by whichever tool you
# chose — tools/mkbanner.py for the flat quad, tools/mkbanner3d.py for the layered
# 3D one — and mkbanner.py would overwrite the 3D banner's texture (palette strip
# and all) with the plain artwork.
if [ "$HERE/banner.png" -nt "$HERE/banner.bnr" ]; then
    echo "note: banner.png is newer than banner.bnr — rebuild it with" >&2
    echo "      tools/mkbanner3d.py (3D) or tools/mkbanner.py (flat)" >&2
fi

makerom -f cia -o "$HERE/voxelcore3ds.cia" \
    -rsf "$HERE/app.rsf" \
    -target t \
    -elf "$BUILD/voxelcore3ds.elf" \
    -icon "$BUILD/voxelcore3ds.smdh" \
    -banner "$HERE/banner.bnr" \
    -DAPP_ROMFS="$REPO/3ds/romfs"

ls -l "$HERE/voxelcore3ds.cia"
