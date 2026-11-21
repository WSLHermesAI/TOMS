"""paint_looks.py -- the 3D floors' looks, painted by AI (docs/gameplay/24_STAGE_3D.md §3).

    python tools/stage3d/paint_looks.py wood                 paint the look "wood" with ComfyUI (127.0.0.1:8188)
    python tools/stage3d/paint_looks.py --all                paint every look
    python tools/stage3d/paint_looks.py wood --seeds 11,22   try seeds; the pictures stay in Build/art_out/stage3d/
    python tools/stage3d/paint_looks.py wood --parts top     paint only these parts again

Each look has three parts: the ground, the walls' sides and the walls' tops. Each part is painted (SDXL, Juggernaut XL,
1024 px, the words in LOOKS), made seamless (blended with itself moved by half) and 512 px, with a normal map made from
its brightness. Writes assets/media/stage3d/looks/<look>/<part>_c.jpg and <part>_n.png.

The game makes each floor's walls and ground from its tile map when the floor is entered (src/game/src/floor_mesh.cpp);
a floor uses the look of its stage file's "theme" (data/stage3d.json "looks"; one it does not have uses "defaultLook").
data/stage3d.json also says how the floors are shown (the camera, the light of each look, the sprites' heights).
"""
import argparse
import sys
import time
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "assets" / "media" / "stage3d"
WORK = ROOT / "Build" / "art_out" / "stage3d"
TEX = 512
PARTS = ("ground", "wall", "top")

# The looks: the words for each part.
LOOKS = {
    "wood":    {"ground": "old wooden floor boards of a cosy village house, warm oak planks",
                "wall":   "rustic timber wall of a farmhouse, horizontal wooden logs and beams",
                "top":    "dark aged wooden planks seen from above, rough hewn weathered wood, no moss"},
    "forest":  {"ground": "forest floor, soft grass with a mossy dirt path and small leaves",
                "wall":   "dense wall of tangled tree roots and dark bark with moss",
                "top":    "dense green hedge and leafy bushes seen from above"},
    "castle":  {"ground": "grey castle flagstone floor, large worn stone slabs",
                "wall":   "medieval castle wall of grey stone bricks with mortar",
                "top":    "top of a castle wall, flat grey capstones"},
    "cave":    {"ground": "cave floor of dark brown packed earth and pebbles",
                "wall":   "rough natural cave rock wall, dark brown stone",
                "top":    "rough dark boulders seen from above"},
    "crypt":   {"ground": "ancient crypt floor, cracked dark stone tiles with dust",
                "wall":   "crypt wall of old dark stone blocks with carved skull niches",
                "top":    "top of an old crypt wall, dark weathered stone"},
    "crystal": {"ground": "floor of purple crystal stone tiles with glowing veins",
                "wall":   "wall of violet crystal rock with glowing gems",
                "top":    "clusters of purple crystals seen from above"},
    "ice":     {"ground": "frozen ice floor with snow dust, pale blue",
                "wall":   "wall of packed snow and blue ice blocks",
                "top":    "fresh snow on top of an ice wall"},
    "lava":    {"ground": "black volcanic rock floor with thin glowing orange cracks",
                "wall":   "wall of dark basalt rock blocks",
                "top":    "top of a basalt wall, black rock with ash"},
    "sand":    {"ground": "desert temple floor of sandstone tiles with blown sand",
                "wall":   "sandstone brick wall of an ancient desert temple",
                "top":    "top of a sandstone wall, worn flat stones"},
    "shadow":  {"ground": "dark obsidian floor tiles with faint purple runes",
                "wall":   "wall of black stone with faint violet glowing runes",
                "top":    "top of a black stone wall, smooth obsidian"},
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("looks", nargs="*", help="the looks to paint (" + ", ".join(LOOKS) + ")")
    ap.add_argument("--all", action="store_true", help="every look")
    ap.add_argument("--seeds", default="7", help="the seeds to paint (the first is used)")
    ap.add_argument("--parts", default=",".join(PARTS), help="the parts to paint (ground, wall, top)")
    a = ap.parse_args()
    looks = list(LOOKS) if a.all else a.looks
    if not looks:
        ap.print_help()
        return 1
    for look in looks:
        if look not in LOOKS:
            sys.exit(f"no look '{look}' (the looks: {', '.join(LOOKS)})")
        print(f"[stage3d] painting {look}")
        paint_look(look, [int(x) for x in a.seeds.split(",")], [x for x in a.parts.split(",") if x in PARTS])
    return 0


if __name__ == "__main__":
    sys.exit(main())
