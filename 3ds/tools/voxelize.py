#!/usr/bin/env python3
"""Re-voxelise a blocky mesh into a clean solid voxel model, then greedy-mesh it.

Why not plain decimation: the input is a voxel island whose block faces were
subdivided into thousands of tiny quads with a little noise. Snapping it back onto
a voxel grid removes the noise instead of averaging it, and emitting only the
faces between a filled cell and outside air gives a watertight solid with an order
of magnitude fewer triangles.
"""
import numpy as np
from scipy import ndimage


def voxelize(V, F, C, cell, samples_per_cell=3.0):
    """Surface-voxelise, then flood outside air and fill everything else.

    Returns (occ, col) where occ is a bool grid and col holds mean colours.
    """
    lo = V.min(0) - cell
    dim = np.ceil((V.max(0) + cell - lo) / cell).astype(int) + 1

    tri = V[F]
    e1, e2 = tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]
    area = 0.5 * np.linalg.norm(np.cross(e1, e2), axis=1)
    n = np.clip(np.ceil(area / (cell * cell) * samples_per_cell), 2, 4096).astype(np.int64)
    idx = np.repeat(np.arange(len(F)), n)
    rng = np.random.default_rng(0)
    u = rng.random(len(idx))
    v = rng.random(len(idx))
    o = u + v > 1
    u[o], v[o] = 1 - u[o], 1 - v[o]
    pts = tri[idx, 0] + u[:, None] * e1[idx] + v[:, None] * e2[idx]
    cols = (C[F[idx, 0]] + u[:, None] * (C[F[idx, 1]] - C[F[idx, 0]])
            + v[:, None] * (C[F[idx, 2]] - C[F[idx, 0]]))
    # triangle corners too, so thin features never fall between samples
    pts = np.vstack([pts, tri.reshape(-1, 3)])
    cols = np.vstack([cols, C[F].reshape(-1, 3)])

    ijk = np.clip(np.floor((pts - lo) / cell).astype(np.int64), 0, dim - 1)
    flat = (ijk[:, 0] * dim[1] + ijk[:, 1]) * dim[2] + ijk[:, 2]
    ncell = int(np.prod(dim))
    surf = np.zeros(ncell, bool)
    surf[flat] = True

    acc = np.zeros((ncell, 3))
    cnt = np.zeros(ncell)
    for k in range(3):
        acc[:, k] = np.bincount(flat, cols[:, k], minlength=ncell)
    cnt = np.bincount(flat, minlength=ncell)

    occ = surf.reshape(dim)
    # outside air = empty cells connected to the padded border
    air = ~occ
    lab, _ = ndimage.label(air)                       # 6-connectivity by default
    outside = lab == lab[0, 0, 0]
    filled = ~outside                                  # solid = shell + interior
    col = np.zeros(tuple(dim) + (3,))
    with np.errstate(invalid="ignore"):
        c = np.divide(acc, cnt[:, None], out=np.zeros_like(acc), where=cnt[:, None] > 0)
    col[:] = c.reshape(tuple(dim) + (3,))
    return filled, col, occ, lo, cell


def _fill_interior_colors(filled, col, surf):
    """Interior cells got no samples; give them their nearest shell colour."""
    need = filled & ~surf
    if not need.any():
        return col
    idx = ndimage.distance_transform_edt(~surf, return_distances=False,
                                         return_indices=True)
    for k in range(3):
        ck = col[..., k]
        ck[need] = ck[tuple(i[need] for i in idx)]
        col[..., k] = ck
    return col


def greedy_faces(filled, col):
    """Exposed faces only, merged into maximal same-colour rectangles per plane."""
    quads, qcol = [], []
    dim = np.array(filled.shape)
    # quantise colour so merging is not defeated by float noise
    key = np.zeros(filled.shape, np.int64)
    q = np.clip((col * 63).round().astype(np.int64), 0, 63)
    key = (q[..., 0] << 12) | (q[..., 1] << 6) | q[..., 2]

    for axis in range(3):
        for direction in (-1, +1):
            shifted = np.roll(filled, -direction, axis=axis)
            if direction == +1:
                sl = [slice(None)] * 3
                sl[axis] = -1
                shifted[tuple(sl)] = False
            else:
                sl = [slice(None)] * 3
                sl[axis] = 0
                shifted[tuple(sl)] = False
            face = filled & ~shifted                   # exposed in this direction
            if not face.any():
                continue
            u_ax, v_ax = [a for a in range(3) if a != axis]
            for layer in np.unique(np.nonzero(face)[axis]):
                sl = [slice(None)] * 3
                sl[axis] = layer
                mask = face[tuple(sl)]                 # 2D in (u_ax, v_ax) order
                kk = key[tuple(sl)]
                if mask.shape != (dim[u_ax], dim[v_ax]):
                    mask, kk = mask.T, kk.T
                todo = mask.copy()
                H, W = todo.shape
                for a in range(H):
                    b = 0
                    while b < W:
                        if not todo[a, b]:
                            b += 1
                            continue
                        c0 = kk[a, b]
                        w = 1
                        while b + w < W and todo[a, b + w] and kk[a, b + w] == c0:
                            w += 1
                        h = 1
                        while a + h < H and todo[a + h, b:b + w].all() \
                                and (kk[a + h, b:b + w] == c0).all():
                            h += 1
                        todo[a:a + h, b:b + w] = False
                        quads.append((axis, direction, layer, a, b, h, w))
                        qcol.append(((c0 >> 12) / 63.0, ((c0 >> 6) & 63) / 63.0,
                                     (c0 & 63) / 63.0))
                        b += w
    return quads, np.array(qcol)


def build_mesh(quads, qcol, lo, cell):
    """Turn merged rectangles into a vertex-coloured triangle mesh."""
    V = np.zeros((len(quads) * 4, 3))
    C = np.zeros((len(quads) * 4, 3))
    F = np.zeros((len(quads) * 2, 3), np.int64)
    for i, (axis, direction, layer, a, b, h, w) in enumerate(quads):
        u_ax, v_ax = [x for x in range(3) if x != axis]
        p = layer + (1 if direction == +1 else 0)
        corners = []
        for du, dv in ((0, 0), (h, 0), (h, w), (0, w)):
            c = np.zeros(3)
            c[axis] = p
            c[u_ax] = a + du
            c[v_ax] = b + dv
            corners.append(c)
        quad = np.array(corners) * cell + lo
        V[i * 4:i * 4 + 4] = quad
        C[i * 4:i * 4 + 4] = qcol[i]
        o = i * 4
        # corners run u then v, so the cross product gives +axis only when
        # (axis, u_ax, v_ax) is an even permutation — it is not for axis == 1
        outward = (direction == +1) != (axis == 1)
        if outward:
            F[i * 2] = (o, o + 1, o + 2)
            F[i * 2 + 1] = (o, o + 2, o + 3)
        else:
            F[i * 2] = (o, o + 2, o + 1)
            F[i * 2 + 1] = (o, o + 3, o + 2)
    N = np.zeros_like(V)
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    for k in range(3):
        for j in range(3):
            N[:, j] += np.bincount(F[:, k], fn[:, j], minlength=len(V))
    ln = np.linalg.norm(N, axis=1, keepdims=True)
    N = np.divide(N, ln, out=np.zeros_like(N), where=ln > 1e-20)
    return {"V": V.astype(np.float32), "N": N.astype(np.float32),
            "C": C.astype(np.float32), "F": F}


def remesh(V, F, C, cell):
    filled, col, surf, lo, cell = voxelize(V, F, C, cell)
    col = _fill_interior_colors(filled, col, surf)
    quads, qcol = greedy_faces(filled, col)
    m = build_mesh(quads, qcol, lo, cell)
    m["voxels"] = int(filled.sum())
    m["quads"] = len(quads)
    m["dim"] = filled.shape
    return m
