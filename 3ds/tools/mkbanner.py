#!/usr/bin/env python3
"""Rebuild 3ds/cia/banner.bnr from 3ds/cia/banner.png — a tiny local bannertool.

bannertool is not needed (and is not installed anywhere here): the CBMD container
already in the tree carries a ready banner model, so we only swap its texture.

  banner.bnr = CBMD header (0x88) + LZ11(CGFX) + pad + CWAV(audio)

The CGFX holds one 256x128 RGBA4444 texture at offset 0x1580, stored in the 3DS
8x8-tile Morton layout. We decode/encode exactly that and leave every other byte
of the model untouched, so the result is byte-identical to a bannertool build.

Usage: python3 3ds/tools/mkbanner.py [--check]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from ctr_tex import decode_rgba4444, encode_rgba4444

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BNR = os.path.join(ROOT, "3ds", "cia", "banner.bnr")
PNG = os.path.join(ROOT, "3ds", "cia", "banner.png")

TEX_OFF, TEX_LEN, TEX_W, TEX_H = 0x1580, 0x10000, 256, 128
CBMD_HDR = 0x88          # common CGFX starts right after the header
CWAV_OFF_FIELD = 0x84

# The banner model is a single quad, and its vertex buffer sits in the CGFX as
# plain interleaved float32 (x, y, z, u, v) — 26 x 13 units, 20 bytes apart:
#     (-13, -7.5, 0) uv 0,0     (13, -7.5, 0) uv 1,0
#     (-13,  5.5, 0) uv 0,1     (13,  5.5, 0) uv 1,1
# The Home Menu draws banners with a stereo camera, so depth has to come from
# this geometry: at z = 0 the banner sits exactly on the screen plane and looks
# flat no matter where the 3D slider is. Leaning the quad gives it real
# stereoscopic depth without changing a single offset in the file.
QUAD_XY = ((-13.0, -7.5), (13.0, -7.5), (-13.0, 5.5), (13.0, 5.5))
QUAD_Y_MID, QUAD_Y_HALF = -1.0, 6.5


# ------------------------------------------------------------------ LZ11
def lz11_decompress(src, start=0):
    p = start
    assert src[p] == 0x11, f"not LZ11 at 0x{start:x}"
    size = src[p + 1] | (src[p + 2] << 8) | (src[p + 3] << 16)
    p += 4
    if size == 0:
        size = struct.unpack_from("<I", src, p)[0]
        p += 4
    dst = bytearray()
    while len(dst) < size:
        flags = src[p]; p += 1
        for bit in range(7, -1, -1):
            if len(dst) >= size:
                break
            if not (flags >> bit) & 1:
                dst.append(src[p]); p += 1
                continue
            b = src[p]; p += 1
            ind = b >> 4
            if ind == 0:
                b2 = src[p]; p += 1
                count = (((b & 0xF) << 4) | (b2 >> 4)) + 0x11
                disp = ((b2 & 0xF) << 8) | src[p]; p += 1
            elif ind == 1:
                b2 = src[p]; p += 1
                b3 = src[p]; p += 1
                count = (((b & 0xF) << 12) | (b2 << 4) | (b3 >> 4)) + 0x111
                disp = ((b3 & 0xF) << 8) | src[p]; p += 1
            else:
                count = ind + 1
                disp = ((b & 0xF) << 8) | src[p]; p += 1
            for _ in range(count):
                dst.append(dst[-(disp + 1)])
    return bytes(dst), p


MAX_DISP = 0x1000
MIN_MATCH = 3
MAX_MATCH = 0x10110


def lz11_compress(data):
    n = len(data)
    out = bytearray(struct.pack("<I", 0x11 | (n << 8)) if n < (1 << 24)
                    else b"\x11\x00\x00\x00" + struct.pack("<I", n))
    index = {}
    pos = 0
    while pos < n:
        flags = 0
        chunk = bytearray()
        for bit in range(7, -1, -1):
            if pos >= n:
                break
            best_len, best_disp = 0, 0
            key = data[pos:pos + MIN_MATCH]
            if len(key) == MIN_MATCH:
                for cand in reversed(index.get(key, ())):
                    disp = pos - cand
                    if disp > MAX_DISP:
                        break
                    limit = min(MAX_MATCH, n - pos)
                    ln = 0
                    while ln < limit and data[cand + ln] == data[pos + ln]:
                        ln += 1
                    if ln > best_len:
                        best_len, best_disp = ln, disp
                        if ln >= limit:
                            break
            if best_len >= MIN_MATCH:
                d = best_disp - 1
                if best_len <= 0x10:
                    chunk += bytes((((best_len - 1) << 4) | (d >> 8), d & 0xFF))
                elif best_len <= 0x110:
                    c = best_len - 0x11
                    chunk += bytes((c >> 4, ((c & 0xF) << 4) | (d >> 8), d & 0xFF))
                else:
                    c = best_len - 0x111
                    chunk += bytes((0x10 | (c >> 12), (c >> 4) & 0xFF,
                                    ((c & 0xF) << 4) | (d >> 8), d & 0xFF))
                flags |= 1 << bit
                step = best_len
            else:
                chunk.append(data[pos])
                step = 1
            for k in range(pos, pos + step):
                key = data[k:k + MIN_MATCH]
                if len(key) == MIN_MATCH:
                    lst = index.setdefault(key, [])
                    lst.append(k)
                    if len(lst) > 24:          # keep the search bounded
                        del lst[:-24]
            pos += step
        out.append(flags)
        out += chunk
    return bytes(out)


# ------------------------------------------------------------------ geometry
def find_quad(cgfx):
    """Offset of the 4-vertex quad buffer, or None."""
    for off in range(0, len(cgfx) - 80, 4):
        v = struct.unpack_from("<20f", cgfx, off)
        if not all(abs(v[i * 5] - QUAD_XY[i][0]) < 1e-3 and
                   abs(v[i * 5 + 1] - QUAD_XY[i][1]) < 1e-3 for i in range(4)):
            continue
        uvs = sorted((v[i * 5 + 3], v[i * 5 + 4]) for i in range(4))
        if uvs == [(0.0, 0.0), (0.0, 1.0), (1.0, 0.0), (1.0, 1.0)]:
            return off
    return None


def set_depth(cgfx, tilt, pop):
    """Lean the quad: bottom edge +tilt towards the viewer, top edge -tilt."""
    off = find_quad(cgfx)
    if off is None:
        raise SystemExit("quad vertex buffer not found — template changed?")
    out = bytearray(cgfx)
    zs = []
    for i in range(4):
        y = struct.unpack_from("<f", out, off + i * 20 + 4)[0]
        t = (y - QUAD_Y_MID) / QUAD_Y_HALF        # -1 at the bottom, +1 at the top
        z = pop - tilt * t
        struct.pack_into("<f", out, off + i * 20 + 8, z)
        zs.append(round(z, 3))
    return bytes(out), off, zs


def read_depth(cgfx):
    off = find_quad(cgfx)
    if off is None:
        return None, None
    return off, [round(struct.unpack_from("<f", cgfx, off + i * 20 + 8)[0], 3)
                 for i in range(4)]


# ------------------------------------------------------------------ banner
def main():
    old = open(BNR, "rb").read()
    assert old[:4] == b"CBMD", "not a CBMD banner"
    cgfx_off = struct.unpack_from("<I", old, 0x08)[0]
    cwav_off = struct.unpack_from("<I", old, CWAV_OFF_FIELD)[0]
    cgfx, _ = lz11_decompress(old, cgfx_off)
    cwav = old[cwav_off:]
    assert cwav[:4] == b"CWAV", "CWAV not found"

    if "--check" in sys.argv:
        tex = decode_rgba4444(cgfx[TEX_OFF:TEX_OFF + TEX_LEN], TEX_W, TEX_H)
        out = os.path.join(ROOT, "3ds", "cia", "banner_current.png")
        tex.convert("RGB").save(out)
        qoff, zs = read_depth(cgfx)
        print("current banner texture ->", out)
        print(f"quad at 0x{qoff:x}, vertex z = {zs}" if qoff is not None
              else "quad not found")
        return

    tilt = pop = 0.0
    for i, arg in enumerate(sys.argv):
        if arg == "--tilt" and i + 1 < len(sys.argv):
            tilt = float(sys.argv[i + 1])
        if arg == "--pop" and i + 1 < len(sys.argv):
            pop = float(sys.argv[i + 1])

    art = Image.open(PNG).convert("RGBA")
    assert art.size == (TEX_W, TEX_H), f"banner.png must be {TEX_W}x{TEX_H}, got {art.size}"
    tex = encode_rgba4444(art)
    assert len(tex) == TEX_LEN
    new_cgfx = cgfx[:TEX_OFF] + tex + cgfx[TEX_OFF + TEX_LEN:]
    assert len(new_cgfx) == len(cgfx)

    if tilt or pop:
        new_cgfx, qoff, zs = set_depth(new_cgfx, tilt, pop)
        assert len(new_cgfx) == len(cgfx)
        print(f"quad at 0x{qoff:x}: tilt={tilt} pop={pop} -> vertex z = {zs}")

    packed = lz11_compress(new_cgfx)
    assert lz11_decompress(packed)[0] == new_cgfx, "LZ11 roundtrip failed"

    body = bytearray(old[:CBMD_HDR])
    body += packed
    while len(body) % 16:
        body.append(0)
    struct.pack_into("<I", body, 0x08, CBMD_HDR)
    struct.pack_into("<I", body, CWAV_OFF_FIELD, len(body))
    body += cwav

    open(BNR, "wb").write(body)
    print(f"banner.bnr rebuilt: {len(body)} bytes "
          f"(CGFX {len(new_cgfx)} -> LZ11 {len(packed)}, CWAV at 0x{len(body) - len(cwav):x})")

    # end-to-end proof: read the file we just wrote back through the same path
    chk = open(BNR, "rb").read()
    cg2, _ = lz11_decompress(chk, struct.unpack_from("<I", chk, 0x08)[0])
    assert cg2 == new_cgfx, "readback mismatch"
    dec = decode_rgba4444(cg2[TEX_OFF:TEX_OFF + TEX_LEN], TEX_W, TEX_H)
    ref = art.convert("RGB")
    err = max(abs(a - b) for pa, pb in zip(dec.convert("RGB").getdata(), ref.getdata())
              for a, b in zip(pa, pb))
    print(f"verified: texture readback matches banner.png "
          f"(max channel error {err}: RGBA4444 quantisation + ordered dither)")


if __name__ == "__main__":
    main()
