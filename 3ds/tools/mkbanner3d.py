#!/usr/bin/env python3
"""Put a real 3D model into the 3DS home-menu banner.

The banner is a CGFX scene that the Home Menu renders with a stereo camera, so
depth has to come from geometry. bannertool's template ships one flat quad, which
is why homebrew banners look 2D. But the template's mesh descriptor turns out to
be perfectly ordinary:

    0x0e08  index format   5121 = u8      -> 5123 = u16
    0x0e10  index count    6              -> ours
    0x0e14  index data     rel. offset    -> ours
    0x0e48  vertex bytes   80             -> ours
    0x0e4c  vertex data    rel. offset    -> ours
    0x0e58  stride         20             (unchanged)
    attributes: float3 position @0, float2 texcoord @12   (unchanged)

which is exactly the layout the voxel island already has. Both blobs live in the
CGFX's IMAG section at the end of the file, so writing the new ones *after* the
texture means no existing offset moves: only five fields plus two sizes change.

Layers, the way retail banners do it:
  * the background quad stays at z = 0 (screen depth) and carries the artwork
  * the island sits in front of it and pops out of the display
  * both sample the one 256x128 texture the material already has - the bottom
    strip of it holds a colour palette for the island, the rest is the artwork

Usage: python3 3ds/tools/mkbanner3d.py [--depth 1.0] [--height 9.0] [--x 7.0]
       python3 3ds/tools/mkbanner3d.py --flat        restore the plain quad
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glb
from ctr_tex import encode_rgba4444, decode_rgba4444
from mkbanner import (BNR, PNG, ROOT, TEX_OFF, TEX_LEN, TEX_W, TEX_H,
                      CBMD_HDR, CWAV_OFF_FIELD, lz11_compress, lz11_decompress)

MODEL = os.path.join(ROOT, "3ds", "tools", "art", "island_vox12.glb")
# backdrop without a painted island - the model is the island now
ART = os.path.join(ROOT, "3ds", "tools", "art", "banner_backdrop.png")

# template fields
F_FILESIZE = 0x0c
F_IDX_FORMAT, F_IDX_COUNT, F_IDX_DATA = 0x0e08, 0x0e10, 0x0e14
F_VTX_BYTES, F_VTX_DATA, F_VTX_STRIDE = 0x0e48, 0x0e4c, 0x0e58
IMAG_AT = 0x14f8            # "IMAG" block header; its size field counts itself
F_IMAG_SIZE = 0x14fc
GL_UNSIGNED_SHORT = 5123

# the template quad, i.e. the frame the Home Menu camera looks at
FRAME_X, FRAME_Y0, FRAME_Y1 = 13.0, -7.5, 5.5
PALETTE_ROWS = 8           # bottom strip: 4x4 blocks -> 128 slots, art loses 6%
BLOCK = 4

SHADE = {(0, 1, 0): 1.00, (0, -1, 0): 0.42,
         (1, 0, 0): 0.74, (-1, 0, 0): 0.74,
         (0, 0, 1): 0.88, (0, 0, -1): 0.60}


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


# ------------------------------------------------------------------ model
def load_model():
    g, b = glb.load(MODEL)
    p = g["meshes"][0]["primitives"][0]
    V = glb.accessor(g, b, p["attributes"]["POSITION"]).astype(np.float64)
    C = glb.accessor(g, b, p["attributes"]["COLOR_0"]).astype(np.float64)
    F = glb.accessor(g, b, p["indices"]).reshape(-1, 3).astype(np.int64)

    # albedo * per-face light, quantised so it fits a 256-slot palette
    shade = np.ones(len(V))
    tri = V[F]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    ln = np.linalg.norm(n, axis=1, keepdims=True)
    n = np.divide(n, ln, out=np.zeros_like(n), where=ln > 1e-20)
    for face, a in enumerate(np.round(n).astype(np.int64)):
        shade[F[face]] = SHADE.get(tuple(a), 0.8)
    lit = np.clip(linear_to_srgb(C) * shade[:, None], 0, 1)
    return V, lit, F


def quantise(colors, slots):
    """Fold the per-vertex colours down to at most `slots` palette entries."""
    for bits in (5, 4, 3):
        q = np.clip((colors * ((1 << bits) - 1)).round(), 0, (1 << bits) - 1)
        keys, index = np.unique(q, axis=0, return_inverse=True)
        if len(keys) <= slots:
            return (keys / ((1 << bits) - 1)), index, bits
    raise SystemExit("cannot fit the palette")


# ------------------------------------------------------------------ texture
def build_texture(art_png, pal_rgb):
    """Artwork on top, palette blocks in the bottom strip."""
    art = Image.open(art_png).convert("RGB")
    art_h = TEX_H - PALETTE_ROWS
    tex = Image.new("RGB", (TEX_W, TEX_H), (0, 0, 0))
    tex.paste(art.resize((TEX_W, art_h), Image.LANCZOS), (0, 0))
    px = tex.load()
    per_row = TEX_W // BLOCK
    uvs = []
    for i, rgb in enumerate(pal_rgb):
        bx = (i % per_row) * BLOCK
        by = art_h + (i // per_row) * BLOCK
        if by + BLOCK > TEX_H:
            raise SystemExit("palette overflows the strip")
        col = tuple(int(round(c * 255)) for c in rgb)
        for y in range(by, by + BLOCK):
            for x in range(bx, bx + BLOCK):
                px[x, y] = col
        # texture v runs bottom-up, the image rows run top-down
        uvs.append(((bx + BLOCK / 2) / TEX_W,
                    1.0 - (by + BLOCK / 2) / TEX_H))
    return tex, np.array(uvs), art_h


# ------------------------------------------------------------------ geometry
def build_geometry(depth, height, cx, art_h):
    V, lit, F = load_model()
    pal_rgb, index, bits = quantise(lit, (TEX_W // BLOCK) * (PALETTE_ROWS // BLOCK))
    tex, pal_uv, art_h = build_texture(ART, pal_rgb)

    lo, hi = V.min(0), V.max(0)
    scale = height / (hi[1] - lo[1])
    mid = (lo + hi) * 0.5
    P = (V - mid) * scale
    P[:, 0] += cx
    P[:, 1] += (FRAME_Y0 + FRAME_Y1) * 0.5
    P[:, 2] += depth

    # background quad: same corners as the template, but its v stops where the
    # palette strip starts so the swatches never show through
    v_top = 1.0 - 0.0
    v_bot = 1.0 - art_h / TEX_H
    quad_pos = np.array([[-FRAME_X, FRAME_Y0, 0.0], [FRAME_X, FRAME_Y0, 0.0],
                         [-FRAME_X, FRAME_Y1, 0.0], [FRAME_X, FRAME_Y1, 0.0]])
    quad_uv = np.array([[0.0, v_bot], [1.0, v_bot], [0.0, v_top], [1.0, v_top]])
    quad_idx = np.array([[0, 1, 2], [1, 3, 2]])

    pos = np.vstack([quad_pos, P])
    uv = np.vstack([quad_uv, pal_uv[index]])
    idx = np.vstack([quad_idx, F + 4])
    return pos, uv, idx, tex, len(pal_rgb), bits


def flat_geometry():
    quad_pos = np.array([[-FRAME_X, FRAME_Y0, 0.0], [FRAME_X, FRAME_Y0, 0.0],
                         [-FRAME_X, FRAME_Y1, 0.0], [FRAME_X, FRAME_Y1, 0.0]])
    quad_uv = np.array([[0.0, 0.0], [1.0, 0.0], [0.0, 1.0], [1.0, 1.0]])
    idx = np.array([[0, 1, 2], [1, 3, 2]])
    return quad_pos, quad_uv, idx, Image.open(PNG).convert("RGB"), 0, 0


# ------------------------------------------------------------------ CGFX
def patch_cgfx(cgfx, pos, uv, idx, tex):
    verts = np.zeros(len(pos), dtype=np.dtype([("p", "<3f4"), ("t", "<2f4")]))
    verts["p"] = pos
    verts["t"] = uv
    vblob = verts.tobytes()
    iblob = idx.reshape(-1).astype("<u2").tobytes()
    assert len(vblob) % 4 == 0

    # Cut everything past the texture: in the template nothing lives there, and
    # in our own output that is exactly the blobs we appended last time. So the
    # tool can be re-run over its own result without piling up dead data.
    out = bytearray(cgfx[:TEX_OFF + TEX_LEN])
    raw = encode_rgba4444(tex.convert("RGBA"))
    assert len(raw) == TEX_LEN
    out[TEX_OFF:TEX_OFF + TEX_LEN] = raw

    vtx_at = len(out)
    out += vblob
    while len(out) % 4:
        out.append(0)
    idx_at = len(out)
    out += iblob
    while len(out) % 4:
        out.append(0)

    struct.pack_into("<I", out, F_IDX_FORMAT, GL_UNSIGNED_SHORT)
    struct.pack_into("<I", out, F_IDX_COUNT, idx.size)
    struct.pack_into("<I", out, F_IDX_DATA, idx_at - F_IDX_DATA)
    struct.pack_into("<I", out, F_VTX_BYTES, len(vblob))
    struct.pack_into("<I", out, F_VTX_DATA, vtx_at - F_VTX_DATA)

    # both sizes are absolute, so re-runs cannot drift: the IMAG block runs from
    # its own header to the end of the file and its size counts that header
    struct.pack_into("<I", out, F_FILESIZE, len(out))
    struct.pack_into("<I", out, F_IMAG_SIZE, len(out) - IMAG_AT)
    return bytes(out), vtx_at, idx_at


def describe(cgfx):
    stride = struct.unpack_from("<I", cgfx, F_VTX_STRIDE)[0]
    vbytes = struct.unpack_from("<I", cgfx, F_VTX_BYTES)[0]
    voff = F_VTX_DATA + struct.unpack_from("<I", cgfx, F_VTX_DATA)[0]
    ioff = F_IDX_DATA + struct.unpack_from("<I", cgfx, F_IDX_DATA)[0]
    fmt = struct.unpack_from("<I", cgfx, F_IDX_FORMAT)[0]
    cnt = struct.unpack_from("<I", cgfx, F_IDX_COUNT)[0]
    return {"vertices": vbytes // stride, "stride": stride,
            "vertex_data": hex(voff), "indices": cnt,
            "index_format": {5121: "u8", 5123: "u16"}.get(fmt, fmt),
            "index_data": hex(ioff), "file_size_field":
            struct.unpack_from("<I", cgfx, F_FILESIZE)[0], "size": len(cgfx)}


def main():
    args = sys.argv[1:]

    def opt(name, default):
        return float(args[args.index(name) + 1]) if name in args else default

    old = open(BNR, "rb").read()
    assert old[:4] == b"CBMD"
    cgfx_off = struct.unpack_from("<I", old, 0x08)[0]
    cwav_off = struct.unpack_from("<I", old, CWAV_OFF_FIELD)[0]
    cgfx, _ = lz11_decompress(old, cgfx_off)
    cwav = old[cwav_off:]
    print("template:", describe(cgfx))

    if "--flat" in args:
        pos, uv, idx, tex, ncol, bits = flat_geometry()
    else:
        pos, uv, idx, tex, ncol, bits = build_geometry(
            opt("--depth", 1.0), opt("--height", 9.0), opt("--x", 7.0),
            TEX_H - PALETTE_ROWS)
    new_cgfx, vtx_at, idx_at = patch_cgfx(cgfx, pos, uv, idx, tex)
    print(f"built: {len(pos)} vertices, {idx.size} indices, "
          f"{ncol} palette colours ({bits} bits/channel)")
    print("result:  ", describe(new_cgfx))

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
    print(f"banner.bnr: {len(body)} bytes (CGFX {len(new_cgfx)} -> LZ11 {len(packed)})")

    # read our own output back through the same path
    chk = open(BNR, "rb").read()
    cg2, _ = lz11_decompress(chk, struct.unpack_from("<I", chk, 0x08)[0])
    assert cg2 == new_cgfx, "readback mismatch"
    d2 = describe(cg2)
    assert d2["file_size_field"] == d2["size"], "fileSize field disagrees"
    v = np.frombuffer(cg2, np.dtype([("p", "<3f4"), ("t", "<2f4")]),
                      d2["vertices"], vtx_at)
    i = np.frombuffer(cg2, "<u2", d2["indices"], idx_at)
    assert i.max() < d2["vertices"], "index out of range"
    print(f"verified: bbox {np.round(v['p'].min(0), 2)} .. "
          f"{np.round(v['p'].max(0), 2)}, uv {np.round(v['t'].min(0), 3)} .. "
          f"{np.round(v['t'].max(0), 3)}, max index {i.max()}")


if __name__ == "__main__":
    main()
