#!/usr/bin/env python3
"""make_weapon_art.py -- a weapon painted on its own, as a part the hero's hand holds (docs/engine/20 §5).

    python tools/art/make_weapon_art.py --weapon sword --seeds 11,22,33

The hero's poses are painted with an empty, gripping hand (make_hero_poses.py --empty-hands); the weapon is one
painting, upright (the grip at the bottom, the tip up), that make_hero_rig2d.py puts on the hand bone and turns per
pose: the same sword in every frame, and a slash is a real swing. Output: Build/art_out/weapon_<weapon>/<seed>.png and
<seed>_cut.png (the background removed), and sheet.png.

Needs the ComfyUI server at 127.0.0.1:8188 (docs/engine/20_CHARACTER_ART.md §2).
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_brawler_art as base   # noqa: E402  (the ComfyUI helpers)

ROOT = base.ROOT
ITEMS = {
    'sword': 'a single steel longsword, straight double-edged blade, brown leather wrapped grip, simple brass crossguard '
             'and round pommel, standing perfectly vertical, the tip pointing straight up, the pommel at the bottom',
    'staff': 'a single tall wooden mage staff, gnarled dark wood, a glowing blue crystal held in twisted roots at the top, '
             'standing perfectly vertical, the crystal at the top, the foot at the bottom',
}
STYLE = ('game item art, painterly digital painting, dark fantasy, visible brush strokes, soft rim light, clean '
         'silhouette, the whole object in the picture, centered, plain white background')
NEGATIVE = ('photo, 3d render, blurry, text, watermark, frame, hand, person, character, two objects, multiple weapons, '
            'tilted, diagonal, cropped, shadow on the floor, background scenery')


def workflow(prompt, seed, prefix):
    return {
        '1': {'class_type': 'CheckpointLoaderSimple', 'inputs': {'ckpt_name': base.CHECKPOINT}},
        '2': {'class_type': 'CLIPTextEncode', 'inputs': {'text': prompt, 'clip': ['1', 1]}},
        '3': {'class_type': 'CLIPTextEncode', 'inputs': {'text': NEGATIVE, 'clip': ['1', 1]}},
        '8': {'class_type': 'EmptyLatentImage', 'inputs': {'width': 512, 'height': 1536, 'batch_size': 1}},
        '9': {'class_type': 'KSampler', 'inputs': {'model': ['1', 0], 'positive': ['2', 0], 'negative': ['3', 0],
                                                   'latent_image': ['8', 0], 'seed': seed, 'steps': 30, 'cfg': 6.5,
                                                   'sampler_name': 'dpmpp_2m', 'scheduler': 'karras', 'denoise': 1.0}},
        '10': {'class_type': 'VAEDecode', 'inputs': {'samples': ['9', 0], 'vae': ['1', 2]}},
        '11': {'class_type': 'SaveImage', 'inputs': {'images': ['10', 0], 'filename_prefix': prefix}},
        '12': {'class_type': 'InspyrenetRembg', 'inputs': {'image': ['10', 0], 'torchscript_jit': 'default'}},
        '13': {'class_type': 'SaveImage', 'inputs': {'images': ['12', 0], 'filename_prefix': prefix + '_cut'}},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--weapon', choices=sorted(ITEMS), required=True)
    ap.add_argument('--seeds', default='11,22,33')
    a = ap.parse_args()
    work = ROOT / 'Build' / 'art_out' / f'weapon_{a.weapon}'
    work.mkdir(parents=True, exist_ok=True)
    seeds = [int(s) for s in a.seeds.split(',')]
    prompt = f'{ITEMS[a.weapon]}, {STYLE}'
    for seed in seeds:
        imgs = base.run(workflow(prompt, seed, f'weapon_{a.weapon}'))
        imgs[0].convert('RGB').save(work / f'{seed}.png')
        imgs[1].save(work / f'{seed}_cut.png')
        print(f'{a.weapon} seed {seed}')
    sheet = Image.new('RGB', (200 * len(seeds), 600), (40, 40, 44))
    for i, seed in enumerate(seeds):
        sheet.paste(Image.open(work / f'{seed}.png').convert('RGB').resize((200, 600)), (i * 200, 0))
    sheet.save(work / 'sheet.png')
    print(work / 'sheet.png')


if __name__ == '__main__':
    main()
