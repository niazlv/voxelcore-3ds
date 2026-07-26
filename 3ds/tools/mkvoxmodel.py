#!/usr/bin/env python3
"""Convert a vertex-coloured voxel GLB into the 3DS port's model asset.

Vertex colours are what the voxel remesher produces, but they are awkward twice
over: many glTF viewers ignore COLOR_0 and show the model white, and the port's
world shader spends the vertex colour on lighting. So the colours move into a
tiny padded palette texture and the vertex colour carries per-face shading:

    final pixel = palette texel (albedo) * vertex colour (light)

which is exactly what the existing world pipeline already computes — no new
shader, no new vertex format.

Outputs:
  3ds/romfs/models/island.vcm3ds                     asset for the port
  3ds/tools/art/island_vox12_textured.glb            same model, viewable anywhere
"""
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glb

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "3ds", "tools", "art", "island_vox12.glb")
OUT_BIN = os.path.join(ROOT, "3ds", "romfs", "models", "island.vcm3ds")
OUT_GLB = os.path.join(ROOT, "3ds", "tools", "art", "island_vox12_textured.glb")

PALETTE = 64          # texture side, power of two for the PICA200
BLOCK = 4             # texels per colour: padding keeps linear filtering safe
PER_ROW = PALETTE // BLOCK

# Minecraft-style directional light baked per face. Voxel quads do not share
# vertices, so a flat per-face value is exact.
SHADE = {(0, 1, 0): 1.00, (0, -1, 0): 0.42,
         (1, 0, 0): 0.74, (-1, 0, 0): 0.74,
         (0, 0, 1): 0.88, (0, 0, -1): 0.60}


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def load(src):
    g, b = glb.load(src)
    p = g["meshes"][0]["primitives"][0]
    V = glb.accessor(g, b, p["attributes"]["POSITION"]).astype(np.float64)
    C = glb.accessor(g, b, p["attributes"]["COLOR_0"]).astype(np.float64)
    F = glb.accessor(g, b, p["indices"]).reshape(-1, 3).astype(np.int64)
    return V, C, F


def build(V, C, F):
    # one palette entry per distinct colour
    q = np.clip((C * 255).round().astype(np.int32), 0, 255)
    keys, index = np.unique(q, axis=0, return_inverse=True)
    if len(keys) > PER_ROW * PER_ROW:
        raise SystemExit(f"{len(keys)} colours exceed the {PER_ROW * PER_ROW}-slot palette")

    pal = np.zeros((PALETTE, PALETTE, 4), np.uint8)
    pal[..., 3] = 255
    uv = np.zeros((len(V), 2), np.float32)
    for i, key in enumerate(keys):
        bx, by = (i % PER_ROW) * BLOCK, (i // PER_ROW) * BLOCK
        rgb = (linear_to_srgb(key / 255.0) * 255).round().astype(np.uint8)
        pal[by:by + BLOCK, bx:bx + BLOCK, :3] = rgb
    # Palette rows stay top-down, the way a PNG and the engine's ImageData are
    # laid out; v is flipped in the coordinate instead. glTF viewers and the
    # PICA upload path (texture_3ds.cpp flips Y itself) then agree.
    for i in range(len(keys)):
        bx, by = (i % PER_ROW) * BLOCK, (i // PER_ROW) * BLOCK
        cx, cy = bx + BLOCK / 2, PALETTE - (by + BLOCK / 2)
        sel = index == i
        uv[sel] = (cx / PALETTE, cy / PALETTE)

    # Per-face shading. The 3DS asset carries albedo * shade in the vertex
    # colour (see write_bin): the world shader feeds the vertex colour in as
    # light, so one attribute is enough and no texture is involved at all.
    # The GLB twin keeps shade alone, with albedo coming from the palette.
    shade = np.ones(len(V), np.float32)
    tri = V[F]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    ln = np.linalg.norm(n, axis=1, keepdims=True)
    n = np.divide(n, ln, out=np.zeros_like(n), where=ln > 1e-20)
    axis = np.round(n).astype(np.int64)
    for face, a in enumerate(axis):
        shade[F[face]] = SHADE.get(tuple(a), 0.8)

    albedo = linear_to_srgb(np.clip(C, 0.0, 1.0))
    lit = np.zeros((len(V), 4), np.uint8)
    lit[:, :3] = np.clip(albedo * shade[:, None] * 255, 0, 255).astype(np.uint8)
    lit[:, 3] = 0                       # alpha 0 keeps the shader's sky term out
    grey = np.zeros((len(V), 4), np.uint8)
    grey[:, :3] = np.clip(shade[:, None] * 255, 0, 255).astype(np.uint8)
    return uv, lit, grey, pal, len(keys)


def write_bin(path, V, uv, col, F):
    """paletteSide 0 tells the port the vertex colour is already albedo*light."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    lo, hi = V.min(0), V.max(0)
    verts = np.zeros(len(V), dtype=np.dtype([
        ("pos", "<3f4"), ("uv", "<2f4"), ("col", "u1", 4), ("nrm", "u1", 4)]))
    verts["pos"] = V
    verts["uv"] = uv
    verts["col"] = col
    verts["nrm"] = (128, 255, 128, 0)
    idx = F.reshape(-1).astype("<u2")
    with open(path, "wb") as f:
        f.write(b"VCM3")
        f.write(struct.pack("<4I", 1, len(V), len(idx), 0))
        f.write(struct.pack("<6f", *lo, *hi))
        f.write(verts.tobytes())
        f.write(idx.tobytes())
    return os.path.getsize(path)


def write_glb(path, V, uv, col, F, pal):
    import io as _io
    from PIL import Image
    buf = _io.BytesIO()
    Image.fromarray(pal, "RGBA").save(buf, "PNG", optimize=True)
    # vertex colour also kept, so viewers that do read COLOR_0 get the shading
    shade = col[:, :3].astype(np.float32) / 255.0
    glb.save(path, {"POSITION": V.astype(np.float32), "TEXCOORD_0": uv,
                    "COLOR_0": shade, "indices": F},
             images=(buf.getvalue(), "image/png"), nearest=True)
    return os.path.getsize(path)


if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else SRC
    V, C, F = load(src)
    uv, lit, grey, pal, ncol = build(V, C, F)
    n1 = write_bin(OUT_BIN, V, uv, lit, F)
    n2 = write_glb(OUT_GLB, V, uv, grey, F, pal)
    print(f"{len(V)} vertices, {len(F)} triangles, {ncol} palette colours")
    print(f"  {os.path.relpath(OUT_BIN, ROOT)}  {n1 / 1024:.0f} KiB")
    print(f"  {os.path.relpath(OUT_GLB, ROOT)}  {n2 / 1024:.0f} KiB")
