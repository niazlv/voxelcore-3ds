#!/usr/bin/env python3
"""Minimal glTF-binary (.glb) reader/writer: enough to split a mesh and re-export."""
import json
import struct
import numpy as np

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16,
      5125: np.uint32, 5126: np.float32}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load(path):
    d = open(path, "rb").read()
    magic, ver, total = struct.unpack_from("<4sII", d, 0)
    assert magic == b"glTF", magic
    off, js, bin_ = 12, None, b""
    while off < total:
        clen, ctype = struct.unpack_from("<II", d, off)
        chunk = d[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:
            js = json.loads(chunk)
        elif ctype == 0x004E4942:
            bin_ = chunk
        off += 8 + clen + ((-clen) % 4)
    return js, bin_


def accessor(gltf, bin_, idx):
    a = gltf["accessors"][idx]
    n, ncomp = a["count"], NCOMP[a["type"]]
    dt = CT[a["componentType"]]
    if "bufferView" not in a:
        return np.zeros((n, ncomp), dt)
    bv = gltf["bufferViews"][a["bufferView"]]
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride") or ncomp * np.dtype(dt).itemsize
    itemsize = np.dtype(dt).itemsize
    if stride == ncomp * itemsize:
        out = np.frombuffer(bin_, dt, n * ncomp, base).reshape(n, ncomp)
    else:  # interleaved
        raw = np.frombuffer(bin_, np.uint8, n * stride, base).reshape(n, stride)
        out = raw[:, :ncomp * itemsize].copy().view(dt).reshape(n, ncomp)
    return out


def save(path, prim, images=None, nearest=False):
    """prim: dict with 'POSITION' (Nx3 f32), optional 'NORMAL', 'TEXCOORD_0',
    'indices' (Mx3 u32), and optional 'material' dict/'image' bytes+mime."""
    views, accs, blobs, offset = [], [], [], 0

    def add(arr, target=None, atype="VEC3", ctype=5126, minmax=False):
        nonlocal offset
        a = np.ascontiguousarray(arr)
        raw = a.tobytes()
        pad = (-len(raw)) % 4
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(raw),
                      **({"target": target} if target else {})})
        blobs.append(raw + b"\0" * pad)
        offset += len(raw) + pad
        acc = {"bufferView": len(views) - 1, "componentType": ctype,
               "count": int(a.shape[0]), "type": atype}
        if minmax:
            acc["min"] = [float(v) for v in a.min(axis=0)]
            acc["max"] = [float(v) for v in a.max(axis=0)]
        accs.append(acc)
        return len(accs) - 1

    attrs = {"POSITION": add(prim["POSITION"].astype(np.float32), 34962, "VEC3", 5126, True)}
    if "NORMAL" in prim:
        attrs["NORMAL"] = add(prim["NORMAL"].astype(np.float32), 34962)
    if "TEXCOORD_0" in prim:
        attrs["TEXCOORD_0"] = add(prim["TEXCOORD_0"].astype(np.float32), 34962, "VEC2")
    if "COLOR_0" in prim:
        attrs["COLOR_0"] = add(prim["COLOR_0"].astype(np.float32), 34962, "VEC3")
    idx = add(prim["indices"].astype(np.uint32).reshape(-1), 34963, "SCALAR", 5125)

    gltf = {"asset": {"version": "2.0", "generator": "split_glb.py"},
            "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
            "meshes": [{"primitives": [{"attributes": attrs, "indices": idx}]}],
            "bufferViews": views, "accessors": accs}

    if images:
        img_blob, mime = images
        pad = (-len(img_blob)) % 4
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(img_blob)})
        blobs.append(img_blob + b"\0" * pad)
        offset += len(img_blob) + pad
        gltf["images"] = [{"bufferView": len(views) - 1, "mimeType": mime}]
        gltf["samplers"] = [{"magFilter": 9728 if nearest else 9729,
                             "minFilter": 9728 if nearest else 9987,
                             "wrapS": 10497, "wrapT": 10497}]
        gltf["textures"] = [{"sampler": 0, "source": 0}]
        gltf["materials"] = [{"pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0}, "metallicFactor": 0.0,
            "roughnessFactor": 0.9}, "doubleSided": True}]
        gltf["meshes"][0]["primitives"][0]["material"] = 0

    elif "COLOR_0" in prim:
        gltf["materials"] = [{"pbrMetallicRoughness": {
            "baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0.0,
            "roughnessFactor": 0.9}, "doubleSided": False}]
        gltf["meshes"][0]["primitives"][0]["material"] = 0

    gltf["buffers"] = [{"byteLength": offset}]
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((-len(js)) % 4)
    binblob = b"".join(blobs)
    total = 12 + 8 + len(js) + 8 + len(binblob)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack("<II", len(binblob), 0x004E4942)); f.write(binblob)
    return total


def save_stl(path, verts, tris):
    """Binary STL — what slicers want."""
    v = verts[tris]                                  # M x 3 x 3
    n = np.cross(v[:, 1] - v[:, 0], v[:, 2] - v[:, 0])
    ln = np.linalg.norm(n, axis=1, keepdims=True)
    n = np.divide(n, ln, out=np.zeros_like(n), where=ln > 0)
    rec = np.zeros(len(tris), dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))
    rec["n"] = n
    rec["v"] = v.reshape(-1, 9)
    with open(path, "wb") as f:
        f.write(b"split_glb".ljust(80, b"\0"))
        f.write(struct.pack("<I", len(tris)))
        f.write(rec.tobytes())
