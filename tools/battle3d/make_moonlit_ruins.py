"""make_moonlit_ruins.py -- the 3D battle stage "moonlit ruins" (docs/gameplay/21_BATTLE_3D.md §6): Poly Haven assets
(CC0, https://polyhaven.com) plus a ground plane and a soft shadow made here.

    python tools/battle3d/make_moonlit_ruins.py            download what is missing, write the stage
    python tools/battle3d/make_moonlit_ruins.py --bounds   also print each model's size (for placing it in battle.json)
    python tools/battle3d/make_moonlit_ruins.py --loose    keep each model as a .gltf folder (not packed into one .glb)

Writes assets/media/battle3d/moonlit_ruins/:
    sky.jpg                         the moonlit night sky (Poly Haven qwantani_moonrise_puresky), as a JPG
    <model>/<model>.gltf (+ .bin, textures)   the props, 1k textures, as Poly Haven ships them
    ground.gltf (+ .bin)            a 40 x 40 m plane of grassy_cobblestone, the texture repeated every 2.5 m
    blob.gltf (+ .bin, blob.png)    a 1 x 1 m soft black disc, alpha-blended: the shadow under each 2D fighter
The downloads are cached in Build/art_cache/polyhaven (not in git); battle.json "stages3d" places the props.
"""
import io
import json
import math
import shutil
import struct
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from simplify_gltf import simplify  # noqa: E402
from pack_glb import pack  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "assets" / "media" / "battle3d" / "moonlit_ruins"
CACHE = ROOT / "Build" / "art_cache" / "polyhaven"
API = "https://api.polyhaven.com"
UA = {"User-Agent": "toms-stage-builder"}

SKY = "qwantani_moonrise_puresky"
GROUND = "grassy_cobblestone"
FORT_PIECES = ["wall_thin_gate_01", "tower_round", "wall_thick_end_01", "wall_thick_end_02", "wall_thin_straight_03",
               "wall_thick_corner_01"]
MODELS = ["modular_fort_01", "stone_fire_pit", "rock_moss_set_01", "rock_moss_set_02", "dead_tree_trunk", "tree_stump_01", "fern_02"]
# The scanned props are far more detailed than a background needs (100k triangles for a trunk): simplified to about
# this many triangles (simplify_gltf.py) and their textures to 512 px. None: the geometry as it is (the fern's
# cut-out leaves), textures still 512. The fort and the ground keep 1k textures: large surfaces near the fighters.
SIMPLIFY = {"dead_tree_trunk": 3000, "tree_stump_01": 2500, "rock_moss_set_01": 3000, "rock_moss_set_02": 3000, "fern_02": None, "stone_fire_pit": 2000}
FORT_TRIANGLES = 5000
# The whole stage under 3 MB (the owner, 2026-11-11): textures by kind, the longest side in pixels (colour, normal map,
# the rest: ambient occlusion / roughness / metal), as JPG at JPG_QUALITY. Background props are a few hundred pixels tall.
TEXTURES = {"fort_pieces": (512, 256, 256), "ground": (512, 256, 256), "fern_02": (256, 128, 128)}
TEXTURES_DEFAULT = (256, 256, 128)
JPG_QUALITY = 80
# The sky: a night sky has little range, so it is a JPG (stb_image makes it linear light again, as the .hdr was):
# this many pixels across, values above 1 (the horizon's glow) clipped.
SKY_WIDTH = 1536


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA)) as r:
        return json.load(r)


def fetch(url, dest: Path):
    if dest.is_file() and dest.stat().st_size > 0:
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"  download {url.rsplit('/', 1)[-1]}")
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA)) as r:
        data = r.read()
    dest.write_bytes(data)
    return dest


def fetch_model(name):
    """The 1k glTF of a Poly Haven model, its files at their relative paths; returns the folder."""
    files = get_json(f"{API}/files/{name}")["gltf"]["1k"]["gltf"]
    folder = CACHE / name
    fetch(files["url"], folder / files["url"].rsplit("/", 1)[-1])
    for rel, info in files.get("include", {}).items():
        fetch(info["url"], folder / rel)
    return folder


def png_bytes(w, h, rgba_rows):
    """A PNG from rows of RGBA bytes (no PIL needed)."""
    import zlib
    raw = b"".join(b"\x00" + row for row in rgba_rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_quad_gltf(path: Path, size, uv_repeat, material, images):
    """A flat square on the ground (XZ), centred, facing up: positions, normals, tangents, uvs, two triangles."""
    h = size / 2
    pos = [(-h, 0, -h), (h, 0, -h), (h, 0, h), (-h, 0, h)]
    nor = [(0, 1, 0)] * 4
    tan = [(1, 0, 0, 1)] * 4
    uv = [(0, 0), (uv_repeat, 0), (uv_repeat, uv_repeat), (0, uv_repeat)]
    idx = [0, 2, 1, 0, 3, 2]   # counter-clockwise seen from above
    blob = b"".join(struct.pack("<3f", *p) for p in pos)
    blob += b"".join(struct.pack("<3f", *n) for n in nor)
    blob += b"".join(struct.pack("<4f", *t) for t in tan)
    blob += b"".join(struct.pack("<2f", *t) for t in uv)
    blob += b"".join(struct.pack("<H", i) for i in idx) + b"\x00\x00"
    binname = path.stem + ".bin"
    (path.parent / binname).write_bytes(blob)
    views = [(0, 48, 34962), (48, 48, 34962), (96, 64, 34962), (160, 32, 34962), (192, 12, 34963)]
    gl = {
        "asset": {"version": "2.0", "generator": "tools/battle3d/make_moonlit_ruins.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": path.stem}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TANGENT": 2, "TEXCOORD_0": 3}, "indices": 4, "material": 0}]}],
        "buffers": [{"uri": binname, "byteLength": len(blob)}],
        "bufferViews": [{"buffer": 0, "byteOffset": o, "byteLength": n, "target": t} for o, n, t in views],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-h, 0, -h], "max": [h, 0, h]},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC4"},
            {"bufferView": 3, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 4, "componentType": 5123, "count": 6, "type": "SCALAR"},
        ],
        "materials": [material],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "images": [{"uri": u} for u in images],
        "textures": [{"source": i, "sampler": 0} for i in range(len(images))],
    }
    if "KHR_materials_unlit" in material.get("extensions", {}):
        gl["extensionsUsed"] = ["KHR_materials_unlit"]
    path.write_text(json.dumps(gl, indent=1), encoding="utf-8")


def model_bounds(gltf_path: Path):
    """Approximate size of a model: its POSITION accessors' min / max, through the nodes' scale and translation."""
    g = json.loads(gltf_path.read_text(encoding="utf-8"))
    lo, hi = [1e9] * 3, [-1e9] * 3
    for node in g.get("nodes", []):
        if "mesh" not in node:
            continue
        s = node.get("scale", [1, 1, 1])
        t = node.get("translation", [0, 0, 0])
        for prim in g["meshes"][node["mesh"]]["primitives"]:
            a = g["accessors"][prim["attributes"]["POSITION"]]
            for k in range(3):
                lo[k] = min(lo[k], a["min"][k] * s[k] + t[k])
                hi[k] = max(hi[k], a["max"][k] * s[k] + t[k])
    return lo, hi, [n.get("name", "?") for n in g.get("nodes", []) if "mesh" in n]


def read_hdr(path):
    """A Radiance .hdr (RGBE, new run-length rows) as linear float RGB (numpy)."""
    import numpy as np
    data = Path(path).read_bytes()
    i = data.index(b"\n\n") + 2
    j = data.index(b"\n", i)
    res = data[i:j].split()
    h, w = int(res[1]), int(res[3])
    pos = j + 1
    img = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        pos += 4
        for c in range(4):
            x = 0
            while x < w:
                n = data[pos]
                pos += 1
                if n > 128:
                    n -= 128
                    img[y, x:x + n, c] = data[pos]
                    pos += 1
                else:
                    img[y, x:x + n, c] = np.frombuffer(data[pos:pos + n], np.uint8)
                    pos += n
                x += n
    e = img[..., 3].astype(np.int32)
    f = np.where(e > 0, np.ldexp(1.0, e - 136), 0.0)
    return img[..., :3] * f[..., None]


def sky_jpg(hdr_path, out_path):
    """The HDR sky as a JPG: gamma 2.2 (stb_image's loadf undoes it), values clipped at 1, SKY_WIDTH across."""
    import numpy as np
    from PIL import Image
    rgb = np.clip(read_hdr(hdr_path), 0.0, 1.0) ** (1.0 / 2.2)
    pic = Image.fromarray((rgb * 255.0 + 0.5).astype(np.uint8))
    pic = pic.resize((SKY_WIDTH, SKY_WIDTH // 2), Image.LANCZOS)
    pic.save(out_path, quality=JPG_QUALITY + 5, optimize=True)


def shrink_textures(gltf_path: Path, sizes):
    """Each texture of the model at most its kind's size (colour / normal / the rest), as JPG; returns the bytes saved."""
    from PIL import Image
    g = json.loads(gltf_path.read_text(encoding="utf-8"))
    saved = 0
    for im in g.get("images", []):
        f = gltf_path.parent / im["uri"]
        name = f.name.lower()
        limit = sizes[0] if "diff" in name or "col" in name else sizes[1] if "nor" in name else sizes[2]
        before = f.stat().st_size
        with Image.open(f) as pic:
            mode = "RGBA" if pic.mode in ("RGBA", "LA") else "RGB"
            pic = pic.convert(mode)
            if max(pic.size) > limit:
                k = limit / max(pic.size)
                pic = pic.resize((max(1, round(pic.size[0] * k)), max(1, round(pic.size[1] * k))), Image.LANCZOS)
            if f.suffix.lower() in (".jpg", ".jpeg"):
                pic.save(f, quality=JPG_QUALITY, optimize=True)
            else:
                pic.save(f, optimize=True)
        saved += before - f.stat().st_size
    return saved


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    print("[stage] the sky")
    hdr = get_json(f"{API}/files/{SKY}")["hdri"]["2k"]["hdr"]["url"]
    sky_jpg(fetch(hdr, CACHE / SKY / "sky_2k.hdr"), OUT / "sky.jpg")
    (OUT / "sky.hdr").unlink(missing_ok=True)   # (the .hdr of before: 4.7 MB)

    print("[stage] the props")
    for name in MODELS:
        src = fetch_model(name)
        dst = OUT / name
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(src, dst)
        gltf = next(dst.glob("*.gltf"))
        if gltf.name != f"{name}.gltf":
            gltf.rename(dst / f"{name}.gltf")
        if name in SIMPLIFY:
            target = SIMPLIFY[name] or 10 ** 9
            before, after, _ = simplify(dst / f"{name}.gltf", target, 0)
            print(f"  {name}: {before:,} -> {after:,} triangles")
    for gone in ("moss_01",):   # (no longer used: removed from earlier builds)
        if (OUT / gone).exists():
            shutil.rmtree(OUT / gone)

    # The fort is a modular kit: 22 pieces spread over 40 x 70 m. The pieces used go in one trimmed model,
    # fort_pieces.gltf: only their meshes, materials and textures, each piece a root node at the origin. It is loaded once;
    # battle.json places each piece as a prop with "node" (BattleStage draws only that node).
    print("[stage] the fort's pieces")
    fort_dir = OUT / "modular_fort_01"
    g = json.loads((fort_dir / "modular_fort_01.gltf").read_text(encoding="utf-8"))
    keep = [i for i, n in enumerate(g["nodes"]) if n.get("name", "").replace("modular_fort_01_", "") in FORT_PIECES]
    meshes = sorted({g["nodes"][i]["mesh"] for i in keep})
    mats = sorted({pr["material"] for m in meshes for pr in g["meshes"][m]["primitives"] if "material" in pr})
    def tex_refs(mat):
        out = []
        def walk(o):
            if isinstance(o, dict):
                if "index" in o and len(o) <= 4 and any(k in o for k in ("index",)) and isinstance(o["index"], int):
                    out.append(o)
                for v in o.values():
                    walk(v)
            elif isinstance(o, list):
                for v in o:
                    walk(v)
        walk(mat)
        return out
    texs = sorted({r["index"] for m in mats for r in tex_refs(g["materials"][m])})
    imgs = sorted({g["textures"][t]["source"] for t in texs})
    mesh_map = {m: k for k, m in enumerate(meshes)}
    mat_map = {m: k for k, m in enumerate(mats)}
    tex_map = {t: k for k, t in enumerate(texs)}
    img_map = {im: k for k, im in enumerate(imgs)}
    out = json.loads(json.dumps(g))
    out["nodes"] = []
    for i in keep:
        n = json.loads(json.dumps(g["nodes"][i]))
        n["mesh"] = mesh_map[n["mesh"]]
        n["name"] = n["name"].replace("modular_fort_01_", "")
        n["translation"] = [0, 0, 0]
        out["nodes"].append(n)
    out["scenes"] = [{"nodes": list(range(len(keep)))}]
    out["scene"] = 0
    out["meshes"] = []
    for m in meshes:
        mm = json.loads(json.dumps(g["meshes"][m]))
        for pr in mm["primitives"]:
            if "material" in pr:
                pr["material"] = mat_map[pr["material"]]
        out["meshes"].append(mm)
    out["materials"] = []
    for m in mats:
        mm = json.loads(json.dumps(g["materials"][m]))
        for r in tex_refs(mm):
            r["index"] = tex_map[r["index"]]
        out["materials"].append(mm)
    out["textures"] = []
    for t in texs:
        tt = json.loads(json.dumps(g["textures"][t]))
        tt["source"] = img_map[tt["source"]]
        out["textures"].append(tt)
    out["images"] = [g["images"][im] for im in imgs]
    (fort_dir / "fort_pieces.gltf").write_text(json.dumps(out), encoding="utf-8")
    for old in fort_dir.glob("*.gltf"):   # (the one-piece files of before: each loaded the whole fort)
        if old.name not in ("fort_pieces.gltf", "modular_fort_01.gltf"):
            old.unlink()
    for im in set(range(len(g["images"]))) - set(imgs):   # textures no piece uses
        f = fort_dir / g["images"][im]["uri"]
        if f.is_file():
            f.unlink()
    (fort_dir / "modular_fort_01.gltf").unlink()   # the whole kit: not drawn (its unused textures are gone)
    print(f"  {len(keep)} pieces, {len(mats)} materials, {len(imgs)} textures")
    before, after, _ = simplify(fort_dir / "fort_pieces.gltf", FORT_TRIANGLES, 0)   # (also keeps only the used pieces' geometry)
    print(f"  fort pieces: {before:,} -> {after:,} triangles")

    print("[stage] the ground")
    tex = get_json(f"{API}/files/{GROUND}")
    gdir = OUT / "ground"
    gdir.mkdir(exist_ok=True)
    for key, fname in (("Diffuse", "ground_diff.jpg"), ("nor_gl", "ground_nor.jpg"), ("arm", "ground_arm.jpg")):
        shutil.copyfile(fetch(tex[key]["1k"]["jpg"]["url"], CACHE / GROUND / fname), gdir / fname)
    write_quad_gltf(gdir / "ground.gltf", 40.0, 16.0, {
        "name": "grassy_cobblestone",
        "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 2},
                                 "metallicFactor": 1.0, "roughnessFactor": 1.0},
        "normalTexture": {"index": 1}, "occlusionTexture": {"index": 2}}, ["ground_diff.jpg", "ground_nor.jpg", "ground_arm.jpg"])

    print("[stage] the shadow blob")
    bdir = OUT / "blob"
    bdir.mkdir(exist_ok=True)
    n = 64
    rows = []
    for y in range(n):
        row = bytearray()
        for x in range(n):
            d = math.hypot((x + 0.5) / n * 2 - 1, (y + 0.5) / n * 2 - 1)
            a = max(0.0, 1.0 - d) ** 1.6   # soft to the edge
            row += bytes((0, 0, 0, int(round(a * 255))))
        rows.append(bytes(row))
    (bdir / "blob.png").write_bytes(png_bytes(n, n, rows))
    write_quad_gltf(bdir / "blob.gltf", 1.0, 1.0, {
        "name": "shadow", "alphaMode": "BLEND", "doubleSided": False,
        "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "baseColorFactor": [0, 0, 0, 0.75], "metallicFactor": 0, "roughnessFactor": 1},
        "extensions": {"KHR_materials_unlit": {}}}, ["blob.png"])

    print("[stage] the textures, by kind")
    for d in sorted(p for p in OUT.iterdir() if p.is_dir()):
        for gl in d.glob("*.gltf"):
            saved = shrink_textures(gl, TEXTURES.get(gl.stem, TEXTURES_DEFAULT))
            print(f"  {gl.stem}: {saved / 1e6:.2f} MB less")
    total = sum(p.stat().st_size for p in OUT.rglob("*") if p.is_file())
    print(f"[stage] done: {OUT.relative_to(ROOT)} ({total / 1e6:.1f} MB)")
    if "--bounds" in sys.argv:
        for name in MODELS:
            lo, hi, nodes = model_bounds(OUT / name / f"{name}.gltf")
            print(f"  {name:18s} size {hi[0]-lo[0]:6.2f} x {hi[1]-lo[1]:5.2f} x {hi[2]-lo[2]:6.2f} m  min {[round(v,2) for v in lo]}  "
                  f"nodes {len(nodes)}: {', '.join(nodes[:6])}{' ...' if len(nodes) > 6 else ''}")


def pack_all():
    """Each model folder made into one .glb beside it (the JSON, the buffer and the textures in one file), the folder
    removed: battle.json names battle3d/moonlit_ruins/<name>.glb."""
    for d in sorted(p for p in OUT.iterdir() if p.is_dir()):
        gltfs = sorted(d.glob("*.gltf"))
        if not gltfs:
            continue
        g = d / "fort_pieces.gltf" if (d / "fort_pieces.gltf").exists() else gltfs[0]
        out = pack(g, OUT / f"{g.stem}.glb")
        shutil.rmtree(d)
        print(f"  {out.name} {out.stat().st_size / 1e6:.2f} MB")
    total = sum(p.stat().st_size for p in OUT.rglob("*") if p.is_file())
    print(f"[stage] packed: {OUT.relative_to(ROOT)} ({total / 1e6:.1f} MB, {sum(1 for _ in OUT.iterdir())} files)")


if __name__ == "__main__":
    main()
    if "--loose" not in sys.argv:   # (--loose: keep the .gltf folders, to look at them in a viewer)
        print("[stage] one .glb per model")
        pack_all()
