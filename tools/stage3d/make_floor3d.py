"""make_floor3d.py -- a floor's walls and ground as a 3D model, from its tile map (docs/gameplay/24_STAGE_3D.md §3).

    python tools/stage3d/make_floor3d.py F01 F02            build these floors' models
    python tools/stage3d/make_floor3d.py --all              every floor in assets/data/story/floors/
    python tools/stage3d/make_floor3d.py --paint wood       paint the look "wood" with ComfyUI (127.0.0.1:8188), then build
    python tools/stage3d/make_floor3d.py --paint wood --seeds 11,22   try seeds; the pictures stay in Build/art_out/stage3d/
    python tools/stage3d/make_floor3d.py --paint wood --parts top    paint only these parts again

One tile is one metre: x to the right (the tile's column), z down the map (its row), y up. A wall cell ('#') is a block
WALL_H tall: its top, and a side wherever the cell beside it is not a wall. The ground is one quad under the whole map.

Writes assets/media/stage3d/:
    floors/<id>.gltf + .bin        three materials: ground, wall, wall top; their textures are the floor's look's
    looks/<look>/<part>_c.jpg       the colour of each part (ground, wall, top): seamless, 512 px
    looks/<look>/<part>_n.png       its normal map, made from the colour's brightness
A look not painted yet is drawn here (a plain pattern of planks / bricks / slabs in the look's colours), so every floor
builds; --paint replaces it. A floor's look is its stage file's "theme" ("" = "castle").
data/stage3d.json says how the game shows the floors (the camera, the light, the sprites' heights).
"""
import argparse
import io
import json
import math
import random
import struct
import sys
import time
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
FLOORS = ROOT / "assets" / "data" / "story" / "floors"
OUT = ROOT / "assets" / "media" / "stage3d"
WORK = ROOT / "Build" / "art_out" / "stage3d"
WALL_H = 1.0          # metres (a tile is 1 m): low enough that the tilted camera sees the hero behind a wall
GROUND_REPEAT = 2.0   # the ground's texture once every this many metres
TEX = 512
PARTS = ("ground", "wall", "top")

# The looks: the words for each part (painted) and the colours of the plain pattern (drawn when not painted).
LOOKS = {
    "wood":    {"ground": "old wooden floor boards of a cosy village house, warm oak planks",
                "wall":   "rustic timber wall of a farmhouse, horizontal wooden logs and beams",
                "top":    "dark aged wooden planks seen from above, rough hewn weathered wood, no moss",
                "colors": {"ground": (122, 86, 52), "wall": (98, 66, 40), "top": (80, 70, 52)}, "pattern": "planks"},
    "forest":  {"ground": "forest floor, soft grass with a mossy dirt path and small leaves",
                "wall":   "dense wall of tangled tree roots and dark bark with moss",
                "top":    "dense green hedge and leafy bushes seen from above",
                "colors": {"ground": (78, 104, 52), "wall": (70, 54, 38), "top": (44, 82, 40)}, "pattern": "grass"},
    "castle":  {"ground": "grey castle flagstone floor, large worn stone slabs",
                "wall":   "medieval castle wall of grey stone bricks with mortar",
                "top":    "top of a castle wall, flat grey capstones",
                "colors": {"ground": (112, 110, 104), "wall": (96, 94, 90), "top": (126, 122, 114)}, "pattern": "bricks"},
    "cave":    {"ground": "cave floor of dark brown packed earth and pebbles",
                "wall":   "rough natural cave rock wall, dark brown stone",
                "top":    "rough dark boulders seen from above",
                "colors": {"ground": (88, 72, 58), "wall": (70, 60, 52), "top": (82, 72, 62)}, "pattern": "rock"},
    "crypt":   {"ground": "ancient crypt floor, cracked dark stone tiles with dust",
                "wall":   "crypt wall of old dark stone blocks with carved skull niches",
                "top":    "top of an old crypt wall, dark weathered stone",
                "colors": {"ground": (78, 76, 84), "wall": (66, 64, 74), "top": (88, 84, 92)}, "pattern": "bricks"},
    "crystal": {"ground": "floor of purple crystal stone tiles with glowing veins",
                "wall":   "wall of violet crystal rock with glowing gems",
                "top":    "clusters of purple crystals seen from above",
                "colors": {"ground": (96, 80, 120), "wall": (84, 66, 112), "top": (120, 96, 150)}, "pattern": "rock"},
    "ice":     {"ground": "frozen ice floor with snow dust, pale blue",
                "wall":   "wall of packed snow and blue ice blocks",
                "top":    "fresh snow on top of an ice wall",
                "colors": {"ground": (170, 196, 214), "wall": (140, 172, 200), "top": (220, 232, 240)}, "pattern": "slabs"},
    "lava":    {"ground": "black volcanic rock floor with thin glowing orange cracks",
                "wall":   "wall of dark basalt rock blocks",
                "top":    "top of a basalt wall, black rock with ash",
                "colors": {"ground": (58, 44, 40), "wall": (50, 40, 38), "top": (66, 52, 46)}, "pattern": "rock"},
    "sand":    {"ground": "desert temple floor of sandstone tiles with blown sand",
                "wall":   "sandstone brick wall of an ancient desert temple",
                "top":    "top of a sandstone wall, worn flat stones",
                "colors": {"ground": (196, 166, 116), "wall": (176, 144, 98), "top": (206, 178, 130)}, "pattern": "bricks"},
    "shadow":  {"ground": "dark obsidian floor tiles with faint purple runes",
                "wall":   "wall of black stone with faint violet glowing runes",
                "top":    "top of a black stone wall, smooth obsidian",
                "colors": {"ground": (50, 44, 62), "wall": (40, 36, 52), "top": (62, 54, 76)}, "pattern": "slabs"},
}
STYLE = ("seamless tileable texture, flat even lighting, top down orthographic, hand painted fantasy game texture, "
         "stylized, rich detail, no shadows")
NEGATIVE = ("perspective, horizon, objects, people, character, text, watermark, logo, frame, border, vignette, "
            "strong shadows, blurry, photo of a room")
CHECKPOINT = "Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors"


# ---------------------------------------------------------------- the textures

def tileable(img: Image.Image) -> Image.Image:
    """Seamless: the picture blended with itself moved by half, the moved copy at the edges (its seams are in the middle,
    where the picture itself is kept)."""
    a = np.asarray(img.convert("RGB"), dtype=np.float32)
    h, w = a.shape[:2]
    b = np.roll(np.roll(a, h // 2, axis=0), w // 2, axis=1)
    y = np.abs(np.linspace(-1, 1, h))[:, None]
    x = np.abs(np.linspace(-1, 1, w))[None, :]
    m = np.clip(1.0 - np.maximum(x, y), 0, 1) ** 0.6   # 1 in the middle, 0 at the edges
    m = (m / m.max())[..., None]
    return Image.fromarray(np.clip(a * m + b * (1 - m), 0, 255).astype(np.uint8))


def normal_map(img: Image.Image, strength=2.5) -> Image.Image:
    """A tangent-space normal map (OpenGL: green up) from the colour's brightness, wrapping at the edges."""
    g = np.asarray(img.convert("L").filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float32) / 255.0
    dx = (np.roll(g, -1, axis=1) - np.roll(g, 1, axis=1)) * strength
    dy = (np.roll(g, -1, axis=0) - np.roll(g, 1, axis=0)) * strength
    n = np.stack([-dx, dy, np.ones_like(g)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return Image.fromarray(((n * 0.5 + 0.5) * 255).astype(np.uint8))


def plain_pattern(part, color, pattern, seed):
    """The look drawn here (not painted): planks, bricks, slabs, rock or grass in its colour, with some noise."""
    rng = np.random.default_rng(seed)
    n = TEX
    base = np.array(color, dtype=np.float32)
    noise = rng.normal(0, 1, (n // 8, n // 8)).astype(np.float32)
    noise = np.asarray(Image.fromarray(noise).resize((n, n), Image.BICUBIC))
    fine = rng.normal(0, 1, (n, n)).astype(np.float32)
    shade = 1.0 + noise * 0.06 + fine * 0.03
    yy, xx = np.mgrid[0:n, 0:n]
    lines = np.ones((n, n), np.float32)
    kind = pattern if part != "top" else ("slabs" if pattern in ("bricks", "planks") else pattern)
    if kind == "planks":
        rows = 8
        r = yy * rows // n
        lines[(yy % (n // rows)) < 3] = 0.55
        off = (r * 97) % n
        lines[((xx + off) % (n // 2)) < 3] = 0.6
        shade *= 1.0 + ((r * 37 % 7) - 3) * 0.03
    elif kind == "bricks":
        rows = 8
        r = yy * rows // n
        lines[(yy % (n // rows)) < 4] = 0.5
        off = (r % 2) * (n // 8)
        lines[((xx + off) % (n // 4)) < 4] = 0.5
        shade *= 1.0 + (((r * 31 + (xx + off) // (n // 4) * 17) % 9) - 4) * 0.025
    elif kind == "slabs":
        lines[(yy % (n // 4)) < 4] = 0.55
        lines[(xx % (n // 4)) < 4] = 0.55
        shade *= 1.0 + ((((yy // (n // 4)) * 5 + (xx // (n // 4)) * 3) % 7) - 3) * 0.03
    elif kind == "rock":
        shade *= 1.0 + noise * 0.12
    elif kind == "grass":
        blades = rng.random((n, n)) < 0.08
        shade[blades] *= 1.25
    a = base[None, None, :] * (shade * lines)[..., None]
    return tileable(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)))


def workflow(prompt, seed, prefix):
    return {
        "1": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": CHECKPOINT}},
        "2": {"class_type": "CLIPTextEncode", "inputs": {"text": prompt + ", " + STYLE, "clip": ["1", 1]}},
        "3": {"class_type": "CLIPTextEncode", "inputs": {"text": NEGATIVE, "clip": ["1", 1]}},
        "4": {"class_type": "EmptyLatentImage", "inputs": {"width": 1024, "height": 1024, "batch_size": 1}},
        "5": {"class_type": "KSampler", "inputs": {"model": ["1", 0], "positive": ["2", 0], "negative": ["3", 0], "latent_image": ["4", 0],
                                                   "seed": seed, "steps": 28, "cfg": 5.5, "sampler_name": "dpmpp_2m",
                                                   "scheduler": "karras", "denoise": 1.0}},
        "6": {"class_type": "VAEDecode", "inputs": {"samples": ["5", 0], "vae": ["1", 2]}},
        "7": {"class_type": "SaveImage", "inputs": {"images": ["6", 0], "filename_prefix": prefix}},
    }


def wait_for_server(seconds=300):
    end = time.time() + seconds
    while time.time() < end:
        try:
            urllib.request.urlopen("http://127.0.0.1:8188/system_stats", timeout=3).read()
            return
        except OSError:
            time.sleep(3)
    sys.exit("ComfyUI did not answer at 127.0.0.1:8188 (AIGamestyle/_pipeline/tools/start_comfyui_with_flags.bat)")


def paint_look(look, seeds, parts=PARTS):
    sys.path.insert(0, str(ROOT / "tools" / "art"))
    from make_brawler_art import run  # noqa: E402  (ComfyUI's /prompt and /history)
    wait_for_server()
    WORK.mkdir(parents=True, exist_ok=True)
    words = LOOKS[look]
    for part in parts:
        for i, seed in enumerate(seeds):
            img = run(workflow(words[part], seed, f"stage3d_{look}_{part}_{seed}"))[0].convert("RGB")
            img.save(WORK / f"{look}_{part}_{seed}.png")
            print(f"  painted {WORK / f'{look}_{part}_{seed}.png'}")
            if i == 0:   # the first seed is used; the others are there to compare (copy one over it to use it)
                write_part(look, part, img)


def write_part(look, part, img):
    d = OUT / "looks" / look
    d.mkdir(parents=True, exist_ok=True)
    c = tileable(img.resize((TEX, TEX), Image.LANCZOS))
    c.save(d / f"{part}_c.jpg", quality=86)
    normal_map(c).save(d / f"{part}_n.png", optimize=True)


def ensure_look(look):
    d = OUT / "looks" / look
    words = LOOKS[look]
    for i, part in enumerate(PARTS):
        if (d / f"{part}_c.jpg").is_file():
            continue
        print(f"  {look}/{part}: not painted, drawn here")
        img = plain_pattern(part, words["colors"][part], words["pattern"], 1000 + i)
        d.mkdir(parents=True, exist_ok=True)
        img.save(d / f"{part}_c.jpg", quality=86)
        normal_map(img).save(d / f"{part}_n.png", optimize=True)


# ---------------------------------------------------------------- the model

class Mesh:
    def __init__(self):
        self.pos, self.nor, self.tan, self.uv, self.idx = [], [], [], [], []

    def quad(self, p, n, t, uv):
        """Four corners p (any order round the quad), facing n; turned so the triangles face n (glTF: counter-clockwise)."""
        b = len(self.pos)
        a, c = np.array(p[0]), np.array(p[2])
        if np.dot(np.cross(np.array(p[1]) - a, c - a), n) < 0:
            p, uv = [p[0], p[3], p[2], p[1]], [uv[0], uv[3], uv[2], uv[1]]
        for i in range(4):
            self.pos.append(tuple(map(float, p[i])))
            self.nor.append(tuple(map(float, n)))
            self.tan.append((float(t[0]), float(t[1]), float(t[2]), 1.0))
            self.uv.append(tuple(map(float, uv[i])))
        self.idx += [b, b + 1, b + 2, b, b + 2, b + 3]


def build_floor(fid):
    st = json.loads((FLOORS / f"{fid}.stage.json").read_text(encoding="utf-8"))
    tiles = st["tiles"]
    look = st.get("theme") or "castle"
    if look not in LOOKS:
        look = "castle"
    ensure_look(look)
    hgt, wid = len(tiles), max(len(r) for r in tiles)
    wall = lambda x, z: 0 <= z < hgt and 0 <= x < len(tiles[z]) and tiles[z][x] == "#"
    ground, side, top = Mesh(), Mesh(), Mesh()
    ground.quad([(0, 0, 0), (wid, 0, 0), (wid, 0, hgt), (0, 0, hgt)], (0, 1, 0), (1, 0, 0),
                [(0, 0), (wid / GROUND_REPEAT, 0), (wid / GROUND_REPEAT, hgt / GROUND_REPEAT), (0, hgt / GROUND_REPEAT)])
    H = WALL_H
    for z in range(hgt):
        for x in range(wid):
            if not wall(x, z):
                continue
            top.quad([(x, H, z), (x + 1, H, z), (x + 1, H, z + 1), (x, H, z + 1)], (0, 1, 0), (1, 0, 0),
                     [(x, z), (x + 1, z), (x + 1, z + 1), (x, z + 1)])
            # the sides where the cell beside is open (the map's edge too: the bottom row's front shows)
            if not wall(x, z - 1):   # north, facing -z
                side.quad([(x + 1, 0, z), (x, 0, z), (x, H, z), (x + 1, H, z)], (0, 0, -1), (-1, 0, 0),
                          [(-(x + 1), H), (-x, H), (-x, 0), (-(x + 1), 0)])
            if not wall(x, z + 1):   # south, facing +z (toward the camera)
                side.quad([(x, 0, z + 1), (x + 1, 0, z + 1), (x + 1, H, z + 1), (x, H, z + 1)], (0, 0, 1), (1, 0, 0),
                          [(x, H), (x + 1, H), (x + 1, 0), (x, 0)])
            if not wall(x - 1, z):   # west, facing -x
                side.quad([(x, 0, z), (x, 0, z + 1), (x, H, z + 1), (x, H, z)], (-1, 0, 0), (0, 0, 1),
                          [(z, H), (z + 1, H), (z + 1, 0), (z, 0)])
            if not wall(x + 1, z):   # east, facing +x
                side.quad([(x + 1, 0, z + 1), (x + 1, 0, z), (x + 1, H, z), (x + 1, H, z + 1)], (1, 0, 0), (0, 0, -1),
                          [(-(z + 1), H), (-z, H), (-z, 0), (-(z + 1), 0)])
    write_gltf(OUT / "floors" / f"{fid}.gltf", fid, look, [("ground", ground), ("wall", side), ("top", top)])
    tris = sum(len(m.idx) for m in (ground, side, top)) // 3
    print(f"[stage3d] {fid}: {wid} x {hgt} tiles, look {look}, {tris} triangles")


def write_gltf(path, fid, look, parts):
    path.parent.mkdir(parents=True, exist_ok=True)
    blob = bytearray()
    views, accessors, prims, materials, images = [], [], [], [], []

    def add(data: bytes, target, count, typ, comp, mn=None, mx=None):
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        acc = {"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": typ}
        if mn is not None:
            acc["min"], acc["max"] = mn, mx
        accessors.append(acc)
        return len(accessors) - 1

    for mi, (name, m) in enumerate(parts):
        if not m.idx:
            continue
        p = np.array(m.pos, np.float32)
        a_pos = add(p.tobytes(), 34962, len(m.pos), "VEC3", 5126, p.min(0).tolist(), p.max(0).tolist())
        a_nor = add(np.array(m.nor, np.float32).tobytes(), 34962, len(m.nor), "VEC3", 5126)
        a_tan = add(np.array(m.tan, np.float32).tobytes(), 34962, len(m.tan), "VEC4", 5126)
        a_uv = add(np.array(m.uv, np.float32).tobytes(), 34962, len(m.uv), "VEC2", 5126)
        a_idx = add(np.array(m.idx, np.uint32).tobytes(), 34963, len(m.idx), "SCALAR", 5125)
        images += [{"uri": f"../looks/{look}/{name}_c.jpg"}, {"uri": f"../looks/{look}/{name}_n.png"}]
        ti = len(images) - 2
        materials.append({"name": f"{look}_{name}",
                          "pbrMetallicRoughness": {"baseColorTexture": {"index": ti}, "metallicFactor": 0.0,
                                                   "roughnessFactor": 0.85 if name != "top" else 0.95},
                          "normalTexture": {"index": ti + 1, "scale": 1.0}})
        prims.append({"attributes": {"POSITION": a_pos, "NORMAL": a_nor, "TANGENT": a_tan, "TEXCOORD_0": a_uv},
                      "indices": a_idx, "material": len(materials) - 1})
    binname = path.stem + ".bin"
    (path.parent / binname).write_bytes(bytes(blob))
    gl = {
        "asset": {"version": "2.0", "generator": "tools/stage3d/make_floor3d.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": fid}],
        "meshes": [{"name": fid, "primitives": prims}],
        "buffers": [{"uri": binname, "byteLength": len(blob)}],
        "bufferViews": views, "accessors": accessors, "materials": materials,
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "images": images, "textures": [{"source": i, "sampler": 0} for i in range(len(images))],
    }
    path.write_text(json.dumps(gl, indent=1), encoding="utf-8", newline="\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("floors", nargs="*", help="floor ids (F01 ...)")
    ap.add_argument("--all", action="store_true", help="every floor")
    ap.add_argument("--paint", metavar="LOOK", help="paint this look with ComfyUI first (" + ", ".join(LOOKS) + ")")
    ap.add_argument("--seeds", default="7", help="the seeds to paint (the first is used)")
    ap.add_argument("--parts", default=",".join(PARTS), help="the parts to paint (ground, wall, top)")
    a = ap.parse_args()
    if a.paint:
        if a.paint not in LOOKS:
            sys.exit(f"no look '{a.paint}' (the looks: {', '.join(LOOKS)})")
        paint_look(a.paint, [int(s) for s in a.seeds.split(",")], [p for p in a.parts.split(",") if p in PARTS])
    ids = sorted(p.name[:-len(".stage.json")] for p in FLOORS.glob("F*.stage.json")) if a.all else a.floors
    if not ids and not a.paint:
        ap.print_help()
        return 1
    for fid in ids:
        build_floor(fid)
    return 0


if __name__ == "__main__":
    sys.exit(main())
