"""simplify_gltf.py -- make a scanned glTF prop light enough for a battle stage's background (docs/gameplay/21 §6):
fewer triangles (vertex clustering, numpy only) and smaller textures. Used by make_moonlit_ruins.py.

    python tools/battle3d/simplify_gltf.py <model.gltf> <triangles> [--texture=512]

Vertex clustering: each mesh's vertices are grouped into cubic cells and every group becomes one vertex (the mean
position, UV and normal); triangles whose corners meet in one cell are dropped. A cell is also split by its UV
(a seam stays a seam: no texture smear) and by the normal's main direction (the two sides of a thin trunk never
merge). The cell size is searched until the model has about the triangles asked for. At a background prop's distance
this keeps the silhouette; the normal map keeps the detail. The model is rewritten in place: one .bin, POSITION,
NORMAL, TEXCOORD_0 (float), indices uint16 / uint32. Textures larger than --texture are scaled down (same names).
"""
import json
import struct
import sys
from pathlib import Path

import numpy as np

COMP = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def read_accessor(g, bins, idx):
    a = g["accessors"][idx]
    bv = g["bufferViews"][a["bufferView"]]
    dt = np.dtype(COMP[a["componentType"]])
    n = NCOMP[a["type"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride", 0) or dt.itemsize * n
    raw = bins[bv["buffer"]]
    if stride == dt.itemsize * n:
        arr = np.frombuffer(raw, dt, a["count"] * n, off).reshape(a["count"], n)
    else:
        arr = np.stack([np.frombuffer(raw, dt, n, off + i * stride) for i in range(a["count"])])
    return arr.copy()


def cluster(pos, nor, uv, tri, cell):
    """One vertex per (position cell, UV cell, normal direction); returns the new arrays."""
    pc = np.floor((pos - pos.min(0)) / cell).astype(np.int64)
    uc = np.floor(uv * 16.0).astype(np.int64)                       # a 1/16 UV cell: seams stay apart
    dc = np.argmax(np.abs(nor), 1) * 2 + (nor[np.arange(len(nor)), np.argmax(np.abs(nor), 1)] > 0)
    key = np.stack([pc[:, 0], pc[:, 1], pc[:, 2], uc[:, 0], uc[:, 1], dc], 1)
    _, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    m = inv.max() + 1
    cnt = np.bincount(inv, minlength=m).astype(np.float64)[:, None]
    def mean(v):
        out = np.zeros((m, v.shape[1]))
        np.add.at(out, inv, v)
        return out / cnt
    npos, nuv, nnor = mean(pos), mean(uv), mean(nor)
    nnor /= np.maximum(np.linalg.norm(nnor, axis=1, keepdims=True), 1e-8)
    t = inv[tri]
    keep = (t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2])
    t = t[keep]
    first = np.unique(np.sort(t, 1), axis=0, return_index=True)[1]   # one of each triangle (two the same: one)
    t = t[np.sort(first)]                                            # (their order and winding as they were)
    used = np.unique(t)                                            # drop vertices no triangle uses
    remap = np.full(m, -1, np.int64)
    remap[used] = np.arange(len(used))
    return npos[used].astype(np.float32), nnor[used].astype(np.float32), nuv[used].astype(np.float32), remap[t]


def simplify_primitive(pos, nor, uv, tri, target):
    if len(tri) <= target:
        return pos, nor, uv, tri
    diag = float(np.linalg.norm(pos.max(0) - pos.min(0)))
    lo, hi = diag / 2000.0, diag / 4.0                             # cell size: search between these
    best = None
    for _ in range(18):
        cell = (lo * hi) ** 0.5
        r = cluster(pos, nor, uv, tri, cell)
        if len(r[3]) > target:
            lo = cell
        else:
            hi = cell
            best = r
        if best is not None and abs(len(best[3]) - target) < target * 0.08:
            break
    return best if best is not None else cluster(pos, nor, uv, tri, hi)


def simplify(path: Path, target_tris: int, max_texture: int = 512):
    g = json.loads(path.read_text(encoding="utf-8"))
    bins = [(path.parent / b["uri"]).read_bytes() for b in g["buffers"]]
    prims = [p for me in g["meshes"] for p in me["primitives"]]
    total = sum(g["accessors"][p["indices"]]["count"] // 3 for p in prims)
    ratio = min(1.0, target_tris / max(1, total))
    out = bytearray()
    views, accs = [], []
    def put(arr, target, comp, typ, minmax=False):
        nonlocal out
        while len(out) % 4:
            out += b"\x00"
        b = arr.astype(COMP[comp]).tobytes()
        views.append({"buffer": 0, "byteOffset": len(out), "byteLength": len(b), "target": target})
        a = {"bufferView": len(views) - 1, "componentType": comp, "count": int(len(arr)), "type": typ}
        if minmax:
            a["min"] = [float(v) for v in arr.min(0)]
            a["max"] = [float(v) for v in arr.max(0)]
        accs.append(a)
        out += b
        return len(accs) - 1
    before = after = 0
    for p in prims:
        at = p["attributes"]
        pos = read_accessor(g, bins, at["POSITION"]).astype(np.float64)
        nor = read_accessor(g, bins, at["NORMAL"]).astype(np.float64) if "NORMAL" in at else np.tile([0.0, 1.0, 0.0], (len(pos), 1))
        uv = read_accessor(g, bins, at["TEXCOORD_0"]).astype(np.float64) if "TEXCOORD_0" in at else np.zeros((len(pos), 2))
        tri = read_accessor(g, bins, p["indices"]).reshape(-1, 3).astype(np.int64)
        before += len(tri)
        pos, nor, uv, tri = simplify_primitive(pos, nor, uv, tri, max(64, int(len(tri) * ratio)))
        after += len(tri)
        p["attributes"] = {"POSITION": put(pos, 34962, 5126, "VEC3", True), "NORMAL": put(nor, 34962, 5126, "VEC3"),
                           "TEXCOORD_0": put(uv, 34962, 5126, "VEC2")}
        p["indices"] = put(tri.reshape(-1), 34963, 5123 if len(pos) < 65536 else 5125, "SCALAR")
    g["accessors"], g["bufferViews"] = accs, views
    for b in g["buffers"][1:]:
        (path.parent / b["uri"]).unlink(missing_ok=True)
    old_bin = path.parent / g["buffers"][0]["uri"]
    g["buffers"] = [{"uri": old_bin.name, "byteLength": len(out)}]
    old_bin.write_bytes(bytes(out))
    path.write_text(json.dumps(g), encoding="utf-8")
    shrunk = 0
    if max_texture:
        from PIL import Image
        for im in g.get("images", []):
            f = path.parent / im["uri"]
            with Image.open(f) as pic:
                if max(pic.size) <= max_texture:
                    continue
                small = pic.convert("RGB").resize((max_texture, max_texture * pic.size[1] // pic.size[0]), Image.LANCZOS)
            small.save(f, quality=88)
            shrunk += 1
    return before, after, shrunk


if __name__ == "__main__":
    tex = next((int(a.split("=")[1]) for a in sys.argv if a.startswith("--texture=")), 512)
    b, a, s = simplify(Path(sys.argv[1]), int(sys.argv[2]), tex)
    print(f"{sys.argv[1]}: {b:,} -> {a:,} triangles, {s} textures scaled down")
