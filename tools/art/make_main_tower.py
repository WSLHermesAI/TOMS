#!/usr/bin/env python3
"""make_main_tower.py -- the main screen's painting: the tower at night (docs/gameplay/27 §6).

    python tools/art/make_main_tower.py --paint 11,22,33     # paint these seeds; pictures in Build/art_out/main_tower/
    python tools/art/make_main_tower.py --use 22             # the game's picture from a painted seed

--use writes assets/media/ui/main/tower_bg.png (1024 x 768, the main screen's design size; main.rcss #sky shows it).
The owner's reference picture (docs/gameplay/main_ui/reference_2026-11-28.jpg) is only a guide to the layout and feel:
the painting is our own. --paint needs the ComfyUI server at 127.0.0.1:8188.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageEnhance

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_brawler_art import CHECKPOINT, run  # noqa: E402
from make_battle_backdrop import wait_for_server  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'Build' / 'art_out' / 'main_tower'
OUT = ROOT / 'assets' / 'media' / 'ui' / 'main' / 'tower_bg.png'
PROMPT = ('a colossal ancient dark stone wizard tower rising into a starry night sky, seen from below, in the centre of the picture, '
          'many floors with glowing golden windows and torches, spiral stairs around it, floating rocky islands with waterfalls '
          'on both sides, moonlit clouds, distant mountains, deep blue and violet night, warm gold light, '
          'fantasy game main menu background, digital painting, painterly, rich detail, cinematic, symmetrical composition')
NEGATIVE = ('person, people, character, hero, creature, monster, dragon, face, text, letters, watermark, logo, signature, frame, '
            'border, user interface, buttons, blurry, photo, daylight, flat lighting')


def workflow(seed, prefix):
    return {
        '1': {'class_type': 'CheckpointLoaderSimple', 'inputs': {'ckpt_name': CHECKPOINT}},
        '2': {'class_type': 'CLIPTextEncode', 'inputs': {'text': PROMPT, 'clip': ['1', 1]}},
        '3': {'class_type': 'CLIPTextEncode', 'inputs': {'text': NEGATIVE, 'clip': ['1', 1]}},
        '4': {'class_type': 'EmptyLatentImage', 'inputs': {'width': 1152, 'height': 896, 'batch_size': 1}},
        '5': {'class_type': 'KSampler', 'inputs': {'model': ['1', 0], 'positive': ['2', 0], 'negative': ['3', 0], 'latent_image': ['4', 0],
                                                   'seed': seed, 'steps': 30, 'cfg': 6.0, 'sampler_name': 'dpmpp_2m',
                                                   'scheduler': 'karras', 'denoise': 1.0}},
        '6': {'class_type': 'VAEDecode', 'inputs': {'samples': ['5', 0], 'vae': ['1', 2]}},
        '7': {'class_type': 'SaveImage', 'inputs': {'images': ['6', 0], 'filename_prefix': prefix}},
    }


def cmd_paint(seeds):
    wait_for_server()
    WORK.mkdir(parents=True, exist_ok=True)
    for seed in seeds:
        img = run(workflow(seed, f'main_tower_{seed}'))[0].convert('RGB')
        img.save(WORK / f'tower_{seed}.png')
        print(f'painted {WORK / f"tower_{seed}.png"}')


def cmd_use(seed):
    src = WORK / f'tower_{seed}.png'
    if not src.exists():
        sys.exit(f'{src} is missing: paint it first (--paint {seed})')
    img = Image.open(src).convert('RGB')
    # 4:3, the design size; a little darker so the chapter bands and buttons read over it
    w, h = img.size
    tw = int(h * 4 / 3)
    if tw < w:
        img = img.crop(((w - tw) // 2, 0, (w - tw) // 2 + tw, h))
    img = img.resize((1024, 768), Image.LANCZOS)
    img = ImageEnhance.Brightness(img).enhance(0.8)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    img.save(OUT, optimize=True)
    print(f'wrote {OUT}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--paint', help='seeds to paint, comma separated')
    ap.add_argument('--use', type=int, help='the seed whose painting the game uses')
    a = ap.parse_args()
    if a.paint:
        cmd_paint([int(s) for s in a.paint.split(',')])
    if a.use is not None:
        cmd_use(a.use)
    if not a.paint and a.use is None:
        ap.print_help()


if __name__ == '__main__':
    main()
