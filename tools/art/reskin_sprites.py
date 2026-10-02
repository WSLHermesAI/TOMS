#!/usr/bin/env python3
"""Regenerate TOMS sprites with ComfyUI (SDXL + Pixel Art XL LoRA + ControlNet Union).

Each 32x32 sprite in assets/media/sprites is scaled up (nearest) to 1024, redrawn by SDXL in pixel-art style
while ControlNet keeps its outline, its background removed (InspyrenetRembg), then scaled back down to the
game's sprite size: each sprite is reduced to a small palette first and every output pixel takes the most
common colour of its block (keeps pixel-art edges crisp, unlike averaging). Outputs never touch assets/: they go
to Build/art_out/<run>/ together with contact sheets for picking the best seed.

  python tools/art/reskin_sprites.py --sprites slime,wall,player
  python tools/art/reskin_sprites.py --sprites all --seeds 2 --style "bright cartoon, Game Boy Advance style"

Server: --server, else the COMFYUI_URL environment variable (set by tools/setup_ai_art.cmd), else
http://127.0.0.1:8188. Needs Pillow. Models (tools/setup_ai_art.ps1 downloads them): sd_xl_base_1.0,
pixel-art-xl, controlnet-union-sdxl-promax, sdxl.vae; custom node comfyui-inspyrenet-rembg.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from io import BytesIO
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
SPRITES = ROOT / "assets" / "media" / "sprites"

# What each sprite shows (prompt subject). Tiles are opaque and fill the whole cell.
SUBJECTS = {
    "floor": "dungeon floor of a few large worn stone slabs, top-down view",
    "wall": "wall of a few large grey stone blocks, front view",
    "stairs_up": "stone staircase leading up",
    "stairs_down": "stone staircase leading down into darkness",
    "door_yellow": "closed golden yellow door with iron frame and keyhole",
    "door_blue": "closed blue door with iron frame and keyhole",
    "door_red": "closed red door with iron frame and keyhole",
    "key_yellow": "golden yellow key",
    "key_blue": "blue key",
    "key_red": "red key",
    "gem_atk": "red attack crystal gem",
    "gem_def": "blue defense crystal gem",
    "potion_red": "red health potion in a round glass bottle",
    "potion_blue": "blue mana potion in a round glass bottle",
    "coin": "shiny gold coin",
    "exp_up": "glowing golden experience star",
    "scroll": "rolled magic parchment scroll",
    "player": "young hero adventurer with brown hair and a blue tunic, full body",
    "slime": "cute round green jelly slime blob with big eyes, gooey and shiny",
    "bat": "purple shadow bat with spread wings",
    "golem": "hulking stone golem",
    "skeleton": "skeleton soldier",
    "wraith": "ghostly wraith in a tattered dark cloak",
    "demon": "red horned demon",
    "boss_demonlord": "demon lord boss with crown, horns and dark armor",
    "npc_sorcerer": "old sorcerer with long white beard, robe and staff",
    "npc_villager": "friendly villager in simple clothes",
    "npc_princess": "princess in a pink gown with a tiara",
    "npc_king": "old king with crown, red cape and white beard",
    "npc_handmaiden": "handmaiden in a maid dress",
}
TILES = {"floor", "wall"}

NEGATIVE = ("blurry, photo, realistic, 3d render, smooth shading, gradient background, text, watermark, signature, "
            "frame, border, multiple characters, cropped, cut off, noisy, jpeg artifacts")

BG = (190, 190, 190)   # flat backdrop for the img2img source (the prompt asks for a plain background too)


# ---------------------------------------------------------------- ComfyUI HTTP API
class Comfy:
    def __init__(self, url):
        self.url = url.rstrip("/")
        self.client_id = uuid.uuid4().hex

    def _get(self, path, raw=False):
        with urllib.request.urlopen(self.url + path, timeout=60) as r:
            data = r.read()
        return data if raw else json.loads(data)

    def check(self):
        stats = self._get("/system_stats")
        dev = (stats.get("devices") or [{}])[0]
        return f"{dev.get('name', '?')}, ComfyUI {stats['system'].get('comfyui_version', '?')}"

    def models(self, folder):
        return self._get(f"/models/{folder}")

    def upload(self, png_bytes, name):
        boundary = uuid.uuid4().hex
        body = b"".join([
            f'--{boundary}\r\nContent-Disposition: form-data; name="overwrite"\r\n\r\ntrue\r\n'.encode(),
            f'--{boundary}\r\nContent-Disposition: form-data; name="image"; filename="{name}"\r\n'
            f"Content-Type: image/png\r\n\r\n".encode(), png_bytes, f"\r\n--{boundary}--\r\n".encode()])
        req = urllib.request.Request(self.url + "/upload/image", data=body,
                                     headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
        with urllib.request.urlopen(req, timeout=60) as r:
            info = json.loads(r.read())
        return (info["subfolder"] + "/" if info.get("subfolder") else "") + info["name"]

    def run(self, workflow, timeout=600):
        """Queues a workflow, waits for it, returns {node_id: [PIL images]}."""
        req = urllib.request.Request(self.url + "/prompt", headers={"Content-Type": "application/json"},
                                     data=json.dumps({"prompt": workflow, "client_id": self.client_id}).encode())
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                prompt_id = json.loads(r.read())["prompt_id"]
        except urllib.error.HTTPError as e:
            raise RuntimeError(f"ComfyUI rejected the workflow: {e.read().decode(errors='replace')}") from None
        start = time.time()
        while True:
            hist = self._get(f"/history/{prompt_id}")
            if prompt_id in hist:
                entry = hist[prompt_id]
                status = entry.get("status", {})
                if status.get("status_str") == "error":
                    msgs = [m for m in status.get("messages", []) if m[0] == "execution_error"]
                    raise RuntimeError(f"ComfyUI execution error: {json.dumps(msgs)[:2000]}")
                if status.get("completed", True):
                    break
            if time.time() - start > timeout:
                raise RuntimeError(f"timed out after {timeout}s")
            time.sleep(1.0)
        out = {}
        for node_id, node_out in entry.get("outputs", {}).items():
            for im in node_out.get("images", []):
                q = urllib.parse.urlencode({"filename": im["filename"], "subfolder": im["subfolder"], "type": im["type"]})
                out.setdefault(node_id, []).append(Image.open(BytesIO(self._get("/view?" + q, raw=True))))
        return out


def build_workflow(a, image_name, positive, seed, tile):
    """API-format workflow: SDXL + LoRA, ControlNet Union on the upscaled sprite, img2img, rembg unless a tile.
    Tiles follow the source outline more strictly so their block layout (and size of the bricks) survives."""
    cn_strength, cn_end, denoise = (a.tile_cn_strength, 1.0, a.tile_denoise) if tile else (a.cn_strength, a.cn_end, a.denoise)
    wf = {
        "ckpt": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": a.checkpoint}},
        "lora": {"class_type": "LoraLoader", "inputs": {"model": ["ckpt", 0], "clip": ["ckpt", 1], "lora_name": a.lora,
                                                         "strength_model": a.lora_strength, "strength_clip": a.lora_strength}},
        "vae": {"class_type": "VAELoader", "inputs": {"vae_name": a.vae}},
        "pos": {"class_type": "CLIPTextEncode", "inputs": {"text": positive, "clip": ["lora", 1]}},
        "neg": {"class_type": "CLIPTextEncode", "inputs": {"text": NEGATIVE, "clip": ["lora", 1]}},
        "src": {"class_type": "LoadImage", "inputs": {"image": image_name}},
        "cn": {"class_type": "ControlNetLoader", "inputs": {"control_net_name": a.controlnet}},
        "cn_type": {"class_type": "SetUnionControlNetType", "inputs": {"control_net": ["cn", 0], "type": a.cn_type}},
        "edges": {"class_type": "Canny", "inputs": {"image": ["src", 0], "low_threshold": 0.2, "high_threshold": 0.5}},
        "apply": {"class_type": "ControlNetApplyAdvanced", "inputs": {
            "positive": ["pos", 0], "negative": ["neg", 0], "control_net": ["cn_type", 0],
            "image": ["edges", 0] if a.cn_type.startswith("canny") else ["src", 0],
            "strength": cn_strength, "start_percent": 0.0, "end_percent": cn_end, "vae": ["vae", 0]}},
        "latent": {"class_type": "VAEEncode", "inputs": {"pixels": ["src", 0], "vae": ["vae", 0]}},
        "sample": {"class_type": "KSampler", "inputs": {
            "model": ["lora", 0], "seed": seed, "steps": a.steps, "cfg": a.cfg, "sampler_name": "dpmpp_2m",
            "scheduler": "karras", "positive": ["apply", 0], "negative": ["apply", 1], "latent_image": ["latent", 0],
            "denoise": denoise}},
        "decode": {"class_type": "VAEDecode", "inputs": {"samples": ["sample", 0], "vae": ["vae", 0]}},
        "save_rgb": {"class_type": "SaveImage", "inputs": {"images": ["decode", 0], "filename_prefix": "toms_reskin/rgb"}},
    }
    if not tile:
        wf["rembg"] = {"class_type": "InspyrenetRembg", "inputs": {"image": ["decode", 0], "torchscript_jit": "default"}}
        wf["mask_img"] = {"class_type": "MaskToImage", "inputs": {"mask": ["rembg", 1]}}
        wf["save_mask"] = {"class_type": "SaveImage", "inputs": {"images": ["mask_img", 0], "filename_prefix": "toms_reskin/mask"}}
    return wf


# ---------------------------------------------------------------- image helpers
def source_for_comfy(sprite_id, size=1024):
    """The original sprite on a flat backdrop, scaled up with nearest-neighbour (keeps hard pixel edges)."""
    im = Image.open(SPRITES / f"{sprite_id}.png").convert("RGBA")
    flat = Image.new("RGBA", im.size, BG + (255,))
    flat.alpha_composite(im)
    big = flat.convert("RGB").resize((size, size), Image.NEAREST)
    buf = BytesIO()
    big.save(buf, "PNG")
    return buf.getvalue()


def mode_downscale(img, size, colors, block=8):
    """Pixel-art downscale: reduce to `colors` (per image), scale to size*block with nearest, then give every
    output pixel the most common palette entry of its block x block cell. RGB in, RGB out."""
    pal_img = img.convert("RGB").quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    idx = np.asarray(pal_img.resize((size * block, size * block), Image.NEAREST))
    cells = idx.reshape(size, block, size, block).transpose(0, 2, 1, 3).reshape(size, size, block * block)
    counts = np.stack([(cells == c).sum(axis=2) for c in range(colors)], axis=2)
    mode = counts.argmax(axis=2).astype(np.uint8)
    palette = np.asarray(pal_img.getpalette()[: colors * 3], dtype=np.uint8).reshape(-1, 3)
    return Image.fromarray(palette[mode], "RGB")


def mask_downscale(mask, size, block=8):
    """Binary alpha by majority vote per block (the game uses 0/255 alpha only)."""
    m = np.asarray(mask.convert("L").resize((size * block, size * block), Image.NEAREST)) >= 128
    vote = m.reshape(size, block, size, block).mean(axis=(1, 3)) >= 0.5
    return Image.fromarray((vote * 255).astype(np.uint8), "L")


def to_sprite(rgb, mask, size, opaque, colors):
    """Scales a 1024 result down to the game sprite size. Characters/items: crop to the subject and fit it in
    the cell with a small margin, standing on the bottom edge. Tiles: the whole image, opaque."""
    rgb = rgb.convert("RGB")
    if opaque or mask is None:
        return mode_downscale(rgb, size, colors).convert("RGBA")
    m = mask.convert("L").point(lambda v: 255 if v >= 128 else 0)
    box = m.getbbox()
    if not box:
        return Image.new("RGBA", (size, size))
    x0, y0, x1, y1 = box
    side = max(x1 - x0, y1 - y0)
    cx = (x0 + x1) // 2
    sq = (cx - side // 2, y1 - side, cx - side // 2 + side, y1)
    # Only the visible subject goes into the palette (background painted black, then cut by the mask).
    crop_rgb, crop_m = rgb.crop(sq), m.crop(sq)
    flat = Image.new("RGB", crop_rgb.size, (0, 0, 0))
    flat.paste(crop_rgb, mask=crop_m)
    margin = max(1, size // 16)
    inner = size - 2 * margin
    small = mode_downscale(flat, inner, colors).convert("RGBA")
    small.putalpha(mask_downscale(crop_m, inner))
    out = Image.new("RGBA", (size, size))
    out.alpha_composite(small, (margin, size - margin - inner))
    return out


def contact_sheet(rows, cell, title_h=18):
    """rows: [(label, [(caption, RGBA image), ...])] -> one image; sprites drawn over the game's floor tile."""
    floor = Image.open(SPRITES / "floor.png").convert("RGBA").resize((cell, cell), Image.NEAREST)
    ncols = max(len(r[1]) for r in rows)
    sheet = Image.new("RGBA", (ncols * cell, len(rows) * (cell + title_h)), (32, 32, 36, 255))
    draw = ImageDraw.Draw(sheet)
    for r, (label, cells) in enumerate(rows):
        y = r * (cell + title_h)
        for c, (caption, im) in enumerate(cells):
            x = c * cell
            sheet.alpha_composite(floor, (x, y + title_h))
            sheet.alpha_composite(im.resize((cell, cell), Image.NEAREST), (x, y + title_h))
            draw.text((x + 4, y + 3), f"{label} {caption}", fill=(230, 230, 230, 255))
    return sheet


# ---------------------------------------------------------------- main
def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--sprites", default="slime,wall,player", help="comma list of sprite ids, or 'all'")
    p.add_argument("--style", default="dark fantasy dungeon crawler, 16-bit SNES JRPG style",
                   help="style words added to every prompt")
    p.add_argument("--seeds", type=int, default=4, help="candidates per sprite")
    p.add_argument("--seed-base", type=int, default=1000)
    p.add_argument("--sizes", default="32,64", help="output sprite sizes")
    p.add_argument("--colors", type=int, default=16, help="colours per sprite")
    p.add_argument("--reprocess", default=None,
                   help="redo only the downscale/sheets of an earlier run folder (no generation)")
    p.add_argument("--denoise", type=float, default=0.8, help="how much SDXL may change the source (0-1)")
    p.add_argument("--cn-strength", type=float, default=0.6, help="how strictly the outline is kept")
    p.add_argument("--cn-end", type=float, default=0.7, help="stop ControlNet after this fraction of the steps")
    p.add_argument("--tile-cn-strength", type=float, default=0.8, help="ControlNet strength for floor/wall tiles")
    p.add_argument("--tile-denoise", type=float, default=0.7, help="denoise for floor/wall tiles (keeps the block layout)")
    p.add_argument("--cn-type", default="canny/lineart/anime_lineart/mlsd", help="Union type: canny/... or tile")
    p.add_argument("--steps", type=int, default=28)
    p.add_argument("--cfg", type=float, default=6.0)
    p.add_argument("--lora-strength", type=float, default=1.0)
    p.add_argument("--checkpoint", default="sd_xl_base_1.0.safetensors")
    p.add_argument("--lora", default="pixel-art-xl.safetensors")
    p.add_argument("--controlnet", default="controlnet-union-sdxl-promax.safetensors")
    p.add_argument("--vae", default="sdxl.vae.safetensors")
    p.add_argument("--server", default=os.environ.get("COMFYUI_URL") or "http://127.0.0.1:8188")
    p.add_argument("--out", default=None, help="output folder (default Build/art_out/<date-time>)")
    a = p.parse_args()

    if a.reprocess:
        out = Path(a.reprocess)
        run = json.loads((out / "run.json").read_text())
        results = {}
        for sid in run["sprites"]:
            for k in range(run["seeds"]):
                seed = run["seed_base"] + k
                mpath = out / "raw" / f"{sid}_s{seed}_mask.png"
                results.setdefault(sid, []).append(
                    (seed, Image.open(out / "raw" / f"{sid}_s{seed}.png"), Image.open(mpath) if mpath.exists() else None))
        postprocess(out, results, [int(x) for x in a.sizes.split(",")], a.colors)
        return

    ids = list(SUBJECTS) if a.sprites == "all" else [s.strip() for s in a.sprites.split(",") if s.strip()]
    unknown = [s for s in ids if s not in SUBJECTS or not (SPRITES / f"{s}.png").exists()]
    if unknown:
        sys.exit(f"unknown sprite id(s): {', '.join(unknown)}  (known: {', '.join(SUBJECTS)})")
    sizes = [int(s) for s in a.sizes.split(",")]
    out = Path(a.out) if a.out else ROOT / "Build" / "art_out" / time.strftime("%Y%m%d-%H%M%S")
    (out / "raw").mkdir(parents=True, exist_ok=True)

    comfy = Comfy(a.server)
    try:
        print(f"[reskin] server {a.server}: {comfy.check()}")
    except Exception as e:
        sys.exit(f"[reskin] no ComfyUI at {a.server} ({e}). Start it (AIGamestyle\\_pipeline\\tools\\start_comfyui.bat) "
                 "or set COMFYUI_URL / --server.")
    for folder, name in (("checkpoints", a.checkpoint), ("loras", a.lora), ("controlnet", a.controlnet), ("vae", a.vae)):
        have = comfy.models(folder)
        if name not in have:
            sys.exit(f"[reskin] the server has no {folder}/{name} (it has: {have}). Run tools\\setup_ai_art.cmd.")

    (out / "run.json").write_text(json.dumps({**vars(a), "sprites": ids}, indent=2))
    results = {}   # sprite id -> [(seed, rgb, mask)]
    t0 = time.time()
    for sid in ids:
        tile = sid in TILES
        name = comfy.upload(source_for_comfy(sid), f"toms_{sid}.png")
        if tile:
            positive = f"pixel art, {SUBJECTS[sid]}, seamless game tile texture, {a.style}, flat lighting, crisp pixels"
        else:
            positive = (f"pixel art, {SUBJECTS[sid]}, single game sprite, centered, {a.style}, clean dark outline, "
                        "limited color palette, crisp pixels, plain light grey background")
        for k in range(a.seeds):
            seed = a.seed_base + k
            t = time.time()
            imgs = comfy.run(build_workflow(a, name, positive, seed, tile))
            rgb = imgs["save_rgb"][0]
            mask = imgs["save_mask"][0] if "save_mask" in imgs else None
            rgb.save(out / "raw" / f"{sid}_s{seed}.png")
            if mask:
                mask.save(out / "raw" / f"{sid}_s{seed}_mask.png")
            results.setdefault(sid, []).append((seed, rgb, mask))
            print(f"[reskin] {sid} seed {seed}: {time.time() - t:.1f}s")

    postprocess(out, results, sizes, a.colors)
    print(f"[reskin] done in {time.time() - t0:.0f}s -> {out}")
    print(f"[reskin] pick a seed per sprite from sheet_{sizes[0]}.png / sheet_raw.png")


def postprocess(out, results, sizes, colors):
    """Writes sprites_<size>/<id>_s<seed>.png and the contact sheets for every size."""
    for size in sizes:
        d = out / f"sprites_{size}"
        d.mkdir(exist_ok=True)
        rows = []
        for sid, lst in results.items():
            cells = [("original", Image.open(SPRITES / f"{sid}.png").convert("RGBA"))]
            for seed, rgb, mask in lst:
                im = to_sprite(rgb, mask, size, sid in TILES, colors)
                im.save(d / f"{sid}_s{seed}.png")
                cells.append((f"seed {seed}", im))
            rows.append((sid, cells))
        contact_sheet(rows, 256).save(out / f"sheet_{size}.png")
    raw_rows = [(sid, [(f"seed {seed}", rgb.convert("RGBA")) for seed, rgb, _ in lst]) for sid, lst in results.items()]
    contact_sheet(raw_rows, 256).save(out / "sheet_raw.png")
    print(f"[reskin] sheets written to {out}")


if __name__ == "__main__":
    main()
