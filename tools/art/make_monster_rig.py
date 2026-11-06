#!/usr/bin/env python3
"""make_monster_rig.py -- painted monsters in battle: the moss team of make_monster_concepts.py, with their clips.

    python tools/art/make_monster_rig.py

Reads Build/art_out/monster_concepts/cut/<concept>.png (the owner's pick, 2026-11-06: the three of the reference pictures) and writes
    assets/media/atlas/monster_moss.atlas + .png
    assets/media/anim/monster_moss.anim    for each monster: idle_<id>, attack_<id>, hurt_<id>, die_<id>, special_<id>, transform_<id>
                                           (opens in the anim editor)
Each monster is two parts on one picture: its top (leaves, ears, head) turns on its own over the body; the two overlap
in a soft band, so no seam shows at these small angles. A clip is authored 32 units on the picture's longer side: the
battle fills a monster's box with 32 units (its enemies.json "sprite" is 32 px).
"""
import json
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[2]
CUT = ROOT / 'Build' / 'art_out' / 'monster_concepts' / 'cut'
ATLAS = ROOT / 'assets' / 'media' / 'atlas' / 'monster_moss'
ANIM = ROOT / 'assets' / 'media' / 'anim' / 'monster_moss.anim'
LONGER = 620       # the picture's longer side in the atlas
BAND = 44          # the overlap of the two parts (atlas pixels)
UNIT = LONGER / 32.0
# id: concept, where the top part ends (a share of the height), how far the top turns, a sticker outline to take off
MONSTERS = {
    'sproutle': {'concept': 'moss_1', 'cut': 0.30, 'turn': 7.0, 'sticker': True},
    'mossbun': {'concept': 'moss_2', 'cut': 0.31, 'turn': 6.0, 'sticker': False},
    'thicketoad': {'concept': 'moss_3', 'cut': 0.46, 'turn': 2.2, 'sticker': False},
}


def strip_sticker(img):
    """Takes off the white outline around a cut-out, and the flat ground shadow under it: near-white pixels connected to the outside."""
    a = np.array(img.convert('RGBA')).astype(np.uint8)
    white = (a[..., :3].min(axis=2) > 205) | (a[..., 3] < 40)
    labels, _ = ndimage.label(white)
    outside = labels == labels[0, 0]
    a[..., 3][outside] = 0
    a[..., 3] = (ndimage.gaussian_filter((a[..., 3] > 128).astype(np.float32), 1.0).clip(0, 1) * 255).astype(np.uint8)
    return Image.fromarray(a, 'RGBA')


def parts(m):
    img = Image.open(CUT / f"{m['concept']}.png").convert('RGBA')
    if m['sticker']:
        img = strip_sticker(img)
    img = img.crop(img.getchannel('A').getbbox())
    k = LONGER / max(img.size)
    img = img.resize((round(img.width * k), round(img.height * k)), Image.LANCZOS)
    a = np.array(img).astype(np.float32)
    h = img.height
    cut = int(h * m['cut'])
    ys = np.arange(h, dtype=np.float32)[:, None]
    fade_top = np.clip((cut + BAND - ys) / BAND, 0, 1)      # the top: whole above the cut, gone a band below it
    fade_body = np.clip((ys - (cut - 14)) / 14, 0, 1)       # the body: gone above the cut, whole from the cut down
    top, body = a.copy(), a.copy()
    top[..., 3] *= fade_top
    body[..., 3] *= fade_body
    top_img = Image.fromarray(top[:cut + BAND].astype(np.uint8), 'RGBA')
    body_y = max(0, cut - 14)
    body_img = Image.fromarray(body[body_y:].astype(np.uint8), 'RGBA')
    return {'w': img.width, 'h': h, 'cut': cut, 'top': top_img, 'body': body_img}


def key(t, v, ease='quadraticInOut'):
    return {'t': round(t, 3), 'v': v, 'ease': ease}


def track3(keys):
    return [key(t, [x, y, 0]) for t, x, y in keys]


def clip(name, length, p, motion, head=None, color=None, loop=False):
    """motion: tracks of the whole monster (pos in units, rot, scale), about its feet. head: the top's turn, [(t, degrees)]. color: [(t, rgba)]."""
    tr = {}
    if 'pos' in motion:
        tr['pos'] = track3([(t, x * UNIT, y * UNIT) for t, x, y in motion['pos']])
    if 'rot' in motion:
        tr['rot'] = [key(t, [0, 0, d]) for t, d in motion['rot']]
    if 'scale' in motion:
        tr['scale'] = [key(t, [x, y, 1]) for t, x, y in motion['scale']]
    color_tr = {'color': [key(t, c) for t, c in color]} if color else {}
    body = {'name': 'torso', 'pos': [0, 0, 0], 'sprite': f"monster_moss:{p['id']}_body", 'pivot': [0.5, 1.0], 'order': 0, 'tracks': dict(color_tr)}
    top_h = p['top'].height
    pivot_y = p['cut'] / top_h
    head_node = {'name': 'top', 'pos': [0, -(p['h'] - p['cut']), 0], 'sprite': f"monster_moss:{p['id']}_top", 'pivot': [0.5, round(pivot_y, 4)], 'order': 1,
                 'tracks': dict(color_tr)}
    if head:
        head_node['tracks']['rot'] = [key(t, [0, 0, d]) for t, d in head]
    for n in (body, head_node):
        if not n['tracks']:
            del n['tracks']
    root = {'name': 'monster', 'scale': [round(32.0 / LONGER, 5)] * 2 + [1], 'children': [
        {'name': 'motion', 'children': [body, head_node], 'tracks': tr}]}
    c = {'name': f"{name}_{p['id']}", 'length': length, 'root': root}
    if loop:
        c['playCount'] = -1
    return c


def clips_of(p, turn):
    t = turn
    white, red = [1, 1, 1, 1], [1, 0.45, 0.45, 1]
    glow = [1.5, 1.5, 1.2, 1]
    return [
        clip('idle', 1.6, p, {'scale': [(0, 1, 1), (0.8, 1.025, 0.97), (1.6, 1, 1)], 'pos': [(0, 0, 0), (1.6, 0, 0)]},
             head=[(0, -t * 0.5), (0.8, t * 0.5), (1.6, -t * 0.5)], loop=True),
        clip('attack', 0.55, p, {'pos': [(0, 0, 0), (0.12, 3, 0.4), (0.22, -13, -4), (0.32, -9, 0), (0.55, 0, 0)],
                                 'rot': [(0, 0), (0.12, 5), (0.22, -9), (0.55, 0)],
                                 'scale': [(0, 1, 1), (0.12, 1.08, 0.9), (0.22, 0.95, 1.08), (0.34, 1.05, 0.95), (0.55, 1, 1)]},
             head=[(0, 0), (0.12, t), (0.22, -t * 1.6), (0.55, 0)]),
        clip('hurt', 0.4, p, {'pos': [(0, 0, 0), (0.06, 4, 0), (0.4, 0, 0)], 'rot': [(0, 0), (0.06, 7), (0.4, 0)],
                              'scale': [(0, 1, 1), (0.06, 0.94, 1.04), (0.4, 1, 1)]},
             head=[(0, 0), (0.08, t * 1.8), (0.4, 0)], color=[(0, red), (0.25, red), (0.4, white)]),
        clip('die', 0.7, p, {'pos': [(0, 0, 0), (0.15, 2, -2), (0.7, 3, 1)], 'rot': [(0, 0), (0.7, 14)],
                             'scale': [(0, 1, 1), (0.15, 1.05, 0.95), (0.7, 1.15, 0.3)]},
             head=[(0, 0), (0.7, t * 2)], color=[(0, white), (0.35, white), (0.7, [1, 1, 1, 0])]),
        clip('special', 0.9, p, {'pos': [(0, 0, 0), (0.35, 0, -3), (0.9, 0, 0)], 'scale': [(0, 1, 1), (0.35, 1.1, 1.1), (0.9, 1, 1)]},
             head=[(0, 0), (0.2, t), (0.4, -t), (0.6, t), (0.9, 0)], color=[(0, white), (0.35, glow), (0.9, white)]),
        clip('transform', 1.0, p, {'scale': [(0, 1, 1), (0.4, 0.75, 1.2), (0.65, 1.2, 0.85), (1.0, 1, 1)], 'rot': [(0, 0), (1.0, 0)]},
             head=[(0, 0), (0.4, t * 1.5), (0.7, -t * 1.5), (1.0, 0)], color=[(0, white), (0.45, [2, 2, 2, 1]), (1.0, white)]),
    ]


def main():
    all_parts = {}
    for mid, m in MONSTERS.items():
        p = parts(m)
        p['id'] = mid
        all_parts[mid] = p
    sheet = Image.new('RGBA', (2048, 2048), (0, 0, 0, 0))
    lines = [ATLAS.name + '.png', 'size: 2048, 2048', 'format: RGBA8888', 'filter: Linear, Linear', 'repeat: none']
    x, y, row = 2, 2, 0
    for mid, p in all_parts.items():
        for part in ('top', 'body'):
            im = p[part]
            if x + im.width + 2 > sheet.width:
                x, y, row = 2, y + row + 2, 0
            sheet.paste(im, (x, y))
            lines += [f'{mid}_{part}', f'  bounds: {x}, {y}, {im.width}, {im.height}']
            x += im.width + 2
            row = max(row, im.height)
    if y + row > sheet.height:
        raise SystemExit('the atlas is too small')
    sheet.save(str(ATLAS) + '.png')
    Path(str(ATLAS) + '.atlas').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    clips = []
    for mid, p in all_parts.items():
        clips += clips_of(p, MONSTERS[mid]['turn'])
    ANIM.write_text(json.dumps({'version': 2, 'atlases': ['../atlas/monster_moss.atlas'], 'clips': clips}, indent=1) + '\n', encoding='utf-8', newline='\n')
    print(f'{ATLAS}.atlas; {ANIM}: {len(clips)} clips')


if __name__ == '__main__':
    main()
