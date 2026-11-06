#!/usr/bin/env python3
"""make_battle_backdrop.py -- a battle's animated background, as data (docs/gameplay/17 §19).

    python tools/art/make_battle_backdrop.py --paint 11,22,33      # paint these seeds; pictures in Build/art_out/backdrop/
    python tools/art/make_battle_backdrop.py --use 22              # build the game's files from a painted seed

--use writes:
    assets/media/atlas/battle_forest.atlas + .png   the painting, two light rays, two ferns
    assets/media/anim/battle_forest.anim            clip "idle": the rays breathe and sway, the ferns move in the wind
                                                    (opens in the anim editor)
    assets/media/fx/battle.particle                 forest_fireflies, forest_motes, looping (opens in the particle editor);
                                                    the file's other effects are kept
data/battle.json "backdrops" says which floors use it.

--paint needs the ComfyUI server (AIGamestyle/_pipeline/tools/start_comfyui_with_flags.bat) at 127.0.0.1:8188.
"""
import argparse
import json
import math
import random
import sys
import time
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_brawler_art import CHECKPOINT, run  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'Build' / 'art_out' / 'backdrop'
ATLAS = ROOT / 'assets' / 'media' / 'atlas' / 'battle_forest'
ANIM = ROOT / 'assets' / 'media' / 'anim' / 'battle_forest.anim'
PARTICLE = ROOT / 'assets' / 'media' / 'fx' / 'battle.particle'
BG_W, BG_H = 1792, 1024     # the painting in the atlas; the game covers the screen with it (battle.json "size")
PROMPT = ('dark enchanted forest clearing at night, huge ancient mossy trees on both sides, a low moss covered mound of earth '
          'in the middle foreground, empty open space in the centre, soft god rays of moonlight falling from the upper right, '
          'glowing blue and purple flowers, ferns, faint mist, tiny glowing spores, deep teal and violet shadows, '
          'fantasy game battle background, digital painting, painterly, rich detail, cinematic lighting, wide shot')
NEGATIVE = ('person, people, character, animal, creature, monster, face, text, watermark, logo, signature, frame, border, '
            'user interface, blurry, photo, bright daylight, flat lighting')


def workflow(seed, prefix):
    return {
        '1': {'class_type': 'CheckpointLoaderSimple', 'inputs': {'ckpt_name': CHECKPOINT}},
        '2': {'class_type': 'CLIPTextEncode', 'inputs': {'text': PROMPT, 'clip': ['1', 1]}},
        '3': {'class_type': 'CLIPTextEncode', 'inputs': {'text': NEGATIVE, 'clip': ['1', 1]}},
        '4': {'class_type': 'EmptyLatentImage', 'inputs': {'width': 1344, 'height': 768, 'batch_size': 1}},
        '5': {'class_type': 'KSampler', 'inputs': {'model': ['1', 0], 'positive': ['2', 0], 'negative': ['3', 0], 'latent_image': ['4', 0],
                                                   'seed': seed, 'steps': 30, 'cfg': 6.0, 'sampler_name': 'dpmpp_2m',
                                                   'scheduler': 'karras', 'denoise': 1.0}},
        '6': {'class_type': 'VAEDecode', 'inputs': {'samples': ['5', 0], 'vae': ['1', 2]}},
        '7': {'class_type': 'SaveImage', 'inputs': {'images': ['6', 0], 'filename_prefix': prefix}},
    }


def wait_for_server(seconds=240):
    end = time.time() + seconds
    while time.time() < end:
        try:
            urllib.request.urlopen('http://127.0.0.1:8188/system_stats', timeout=3).read()
            return
        except OSError:
            time.sleep(3)
    sys.exit('ComfyUI did not answer at 127.0.0.1:8188')


def cmd_paint(seeds):
    wait_for_server()
    WORK.mkdir(parents=True, exist_ok=True)
    for seed in seeds:
        img = run(workflow(seed, f'backdrop_{seed}'))[0].convert('RGB')
        img.save(WORK / f'forest_{seed}.png')
        print(f'painted {WORK / f"forest_{seed}.png"}')


def make_ray():
    """A soft shaft of light, narrow at the top and fading downward."""
    w, h = 360, 900
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    t = ys / (h - 1)
    half = 26 + 130 * t
    across = np.exp(-(((xs - w / 2) / half) ** 2))
    along = np.clip(t / 0.08, 0, 1) * (1 - t) ** 1.4
    a = across * along
    rgba = np.zeros((h, w, 4), np.uint8)
    rgba[..., 0], rgba[..., 1], rgba[..., 2] = 205, 232, 255
    rgba[..., 3] = (a * 255).astype(np.uint8)
    return Image.fromarray(rgba, 'RGBA')


def make_fern(seed):
    """A dark fern: fronds from one foot at the bottom centre."""
    w, h = 520, 520
    img = Image.new('RGBA', (w * 2, h * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rnd = random.Random(seed)
    for k in range(7):
        ang = math.radians(-90 + (k - 3) * 22 + rnd.uniform(-6, 6))
        length = rnd.uniform(620, 900)
        steps = 34
        x, y = float(w), float(h * 2)
        bend = rnd.uniform(0.012, 0.022) * (1 if k >= 3 else -1)
        for s in range(steps):
            f = s / steps
            nx, ny = x + math.cos(ang) * length / steps, y + math.sin(ang) * length / steps
            ang += bend
            col = (14 + int(16 * f), 42 + int(26 * f), 38 + int(14 * f), 255)
            d.line([(x, y), (nx, ny)], fill=col, width=max(2, int(9 * (1 - f))))
            leaf = 100 * math.sin(math.pi * min(1, f * 1.15)) * (1 - f * 0.4)
            for side in (-1, 1):
                a2 = ang + side * 1.05
                tip = (nx + math.cos(a2) * leaf, ny + math.sin(a2) * leaf)
                back = (x + math.cos(ang) * -8, y + math.sin(ang) * -8)
                d.polygon([(x, y), tip, (nx, ny), back], fill=col)
            x, y = nx, ny
    return img.resize((w, h), Image.LANCZOS).filter(ImageFilter.GaussianBlur(1.2))


def key(t, v):
    return {'t': t, 'v': v, 'ease': 'quadraticInOut'}


def ray_node(name, x, y, rot, peak, length, phase):
    low = [0.65, 0.82, 1, peak * 0.45]
    high = [0.8, 0.92, 1, peak]
    a, b = (low, high) if phase == 0 else (high, low)
    return {'name': name, 'pos': [x, y, 0], 'rot': [0, 0, rot], 'sprite': 'battle_forest:ray', 'pivot': [0.5, 0.0], 'order': 1, 'blend': 'add',
            'scale': [1.7, 1.25, 1],
            'tracks': {'color': [key(0.0, a), key(length / 2, b), key(length, a)],
                       'rot': [key(0.0, [0, 0, rot - 1.5]), key(length / 2, [0, 0, rot + 1.5]), key(length, [0, 0, rot - 1.5])]}}


def fern_node(name, sprite, x, y, flip, length, swing, phase):
    s = -1.25 if flip else 1.25
    keys = [key(i * length / 4, [0, 0, swing * math.sin(math.pi / 2 * i + phase)]) for i in range(5)]
    return {'name': name, 'pos': [x, y, 0], 'rot': keys[0]['v'], 'scale': [s, 1.25, 1], 'sprite': f'battle_forest:{sprite}', 'pivot': [0.5, 1.0], 'order': 2,
            'tracks': {'rot': keys}}


def cmd_use(seed):
    src = WORK / f'forest_{seed}.png'
    if not src.exists():
        sys.exit(f'{src} is not painted yet: --paint {seed}')
    bg = Image.open(src).convert('RGB').resize((BG_W, BG_H), Image.LANCZOS)
    ray, f1, f2 = make_ray(), make_fern(5), make_fern(9)
    sheet = Image.new('RGBA', (2048, 2048), (0, 0, 0, 0))
    places = {'bg': (2, 2, bg), 'ray': (2, 1030, ray), 'fern_a': (366, 1030, f1), 'fern_b': (890, 1030, f2)}
    lines = [ATLAS.name + '.png', f'size: {sheet.width}, {sheet.height}', 'format: RGBA8888', 'filter: Linear, Linear', 'repeat: none']
    for name, (x, y, im) in places.items():
        sheet.paste(im.convert('RGBA'), (x, y))
        lines += [name, f'  bounds: {x}, {y}, {im.width}, {im.height}']
    sheet.save(str(ATLAS) + '.png')
    Path(str(ATLAS) + '.atlas').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')

    length = 8.0
    root = {'name': 'forest', 'pos': [0, 0, 0], 'children': [
        {'name': 'painting', 'pos': [0, 0, 0], 'sprite': 'battle_forest:bg', 'pivot': [0.5, 0.5], 'order': 0},
        ray_node('ray_a', 420, -560, 20, 0.34, length, 0),
        ray_node('ray_b', 120, -560, 24, 0.24, length, 1),
        fern_node('fern_left', 'fern_a', -760, 520, False, length, 2.6, 0.0),
        fern_node('fern_right', 'fern_b', 780, 520, True, length, 2.2, 1.3),
    ]}
    anim = {'version': 2, 'atlases': ['../atlas/battle_forest.atlas'], 'clips': [{'name': 'idle', 'length': length, 'root': root, 'playCount': -1}]}
    ANIM.write_text(json.dumps(anim, indent=1) + '\n', encoding='utf-8', newline='\n')

    fx = json.loads(PARTICLE.read_text(encoding='utf-8'))
    fx['effects'] = [e for e in fx['effects'] if e['name'] not in ('forest_fireflies', 'forest_motes')]
    fx['effects'].append({'name': 'forest_fireflies', 'duration': 6, 'loop': True, 'emitters': [{
        'name': 'fireflies', 'maxParticles': 60, 'emission': {'rate': 7}, 'shape': {'type': 'box', 'size': [1800, 1500]},
        'life': [3.5, 6.5], 'speed': [6, 22], 'size': [7, 15], 'color': [[0.75, 1, 0.55, 1], [1, 0.9, 0.45, 1]], 'sprite': 'fx:glow',
        'direction': -90, 'spread': 180, 'forces': {'gravity': [0, -3]},
        'overLife': {'color': [{'t': 0, 'v': [1, 1, 1, 0]}, {'t': 0.2, 'v': [1, 1, 1, 1]}, {'t': 0.5, 'v': [1, 1, 1, 0.35]},
                               {'t': 0.75, 'v': [1, 1, 1, 1]}, {'t': 1, 'v': [1, 1, 1, 0]}]},
        'render': {'blend': 'add', 'order': 0}}]})
    fx['effects'].append({'name': 'forest_motes', 'duration': 6, 'loop': True, 'emitters': [{
        'name': 'motes', 'maxParticles': 90, 'emission': {'rate': 13}, 'shape': {'type': 'box', 'size': [1900, 1500]},
        'life': [4, 7], 'speed': [8, 26], 'size': [2, 5], 'color': [[0.75, 0.9, 1, 0.7], [0.8, 0.7, 1, 0.6]], 'sprite': 'fx:glow',
        'direction': 75, 'spread': 35,
        'overLife': {'color': [{'t': 0, 'v': [1, 1, 1, 0]}, {'t': 0.25, 'v': [1, 1, 1, 1]}, {'t': 1, 'v': [1, 1, 1, 0]}]},
        'render': {'blend': 'add', 'order': 0}}]})
    PARTICLE.write_text(json.dumps(fx, indent=1, ensure_ascii=False) + '\n', encoding='utf-8', newline='\n')
    print(f'{ATLAS}.atlas, {ANIM}, forest_fireflies + forest_motes in {PARTICLE}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--paint', help='seeds, comma separated')
    ap.add_argument('--use', type=int, help='the painted seed to build from')
    a = ap.parse_args()
    if a.paint:
        cmd_paint([int(s) for s in a.paint.split(',')])
    if a.use is not None:
        cmd_use(a.use)
    if not a.paint and a.use is None:
        ap.print_help()


if __name__ == '__main__':
    main()
