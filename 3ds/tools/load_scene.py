#!/usr/bin/env python3
"""Flatten a multi-mesh glTF scene (node transforms applied) into one coloured mesh."""
import numpy as np
import glb


def node_matrix(n):
    if "matrix" in n:
        return np.array(n["matrix"], np.float64).reshape(4, 4).T
    M = np.eye(4)
    if "scale" in n:
        M[:3, :3] = np.diag(n["scale"])
    if "rotation" in n:
        x, y, z, w = n["rotation"]
        R = np.array([
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
        M[:3, :3] = R @ M[:3, :3]
    if "translation" in n:
        M[:3, 3] = n["translation"]
    return M


def load(path, apply_root=True):
    g, b = glb.load(path)
    nodes = g["nodes"]
    world = {}

    def walk(i, parent):
        M = parent @ node_matrix(nodes[i])
        world[i] = M
        for c in nodes[i].get("children", []):
            walk(c, M)

    roots = g["scenes"][g.get("scene", 0)]["nodes"]
    for r in roots:
        walk(r, np.eye(4) if apply_root else np.eye(4))

    Vs, Ns, Cs, Fs, groups = [], [], [], [], []
    base = 0
    for i, n in enumerate(nodes):
        if "mesh" not in n:
            continue
        M = world[i]
        R = M[:3, :3]
        for prim in g["meshes"][n["mesh"]]["primitives"]:
            at = prim["attributes"]
            V = glb.accessor(g, b, at["POSITION"]).astype(np.float64)
            V = V @ R.T + M[:3, 3]
            N = (glb.accessor(g, b, at["NORMAL"]).astype(np.float64) @ R.T
                 if "NORMAL" in at else np.zeros_like(V))
            if "COLOR_0" in at:
                C = glb.accessor(g, b, at["COLOR_0"]).astype(np.float64)
                if C.dtype != np.float64 or C.max() > 1.5:
                    C = C / 255.0
                C = C[:, :3]
            else:
                C = np.ones((len(V), 3))
            mat = g["materials"][prim["material"]] if "material" in prim else {}
            f = mat.get("pbrMetallicRoughness", {}).get("baseColorFactor", [1, 1, 1, 1])
            C = C * np.array(f[:3])
            F = glb.accessor(g, b, prim["indices"]).reshape(-1, 3).astype(np.int64) + base
            Vs.append(V); Ns.append(N); Cs.append(C); Fs.append(F)
            groups.append({"name": n.get("name", f"node{i}"), "tris": len(F),
                           "color": np.array(f[:3])})
            base += len(V)
    return {"V": np.vstack(Vs).astype(np.float32), "N": np.vstack(Ns).astype(np.float32),
            "C": np.vstack(Cs).astype(np.float32), "F": np.vstack(Fs)}, groups
