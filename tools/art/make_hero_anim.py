#!/usr/bin/env python3
"""make_hero_anim.py -- the player's battle clips from the painted key poses (docs/engine/20_CHARACTER_ART.md, option B).

    python tools/art/make_hero_anim.py --weapon bow --pick idle=55 --pick charge=44 --pick miss=44 --pick attack_big=44

Takes the picked pictures of tools/art/make_hero_poses.py (Build/art_out/hero_<weapon>/<pose>_<seed>_cut.png), crops
them all with ONE frame (the same picture pixels for every pose, so the feet stay on one line and the body does not jump
between poses), scales them down, packs them into assets/media/atlas/hero_<weapon>.atlas (+ .png, linear filtering),
and writes assets/media/anim/hero_<weapon>.anim with the clips the battle plays (Game::setBattleRigState: idle, charge,
attack, miss, and attack_big for the big hit; hurt for later). A pose that did not paint well is made from a good one:
the release is the full draw with a snap and a recoil; hurt is the ready pose tilted back.

The weapon is painted in the hands, so the item's battleAnim has no "held" file (items.json).
"""
import argparse
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ATLAS_DIR = ROOT / 'assets' / 'media' / 'atlas'
ANIM_DIR = ROOT / 'assets' / 'media' / 'anim'
DOWN = 0.5          # picture pixels -> atlas pixels (an HD sprite, about 500 px tall)
HEIGHT = 40.0       # the figure's height in clip units: the battle draws 32 units at the box's height (a bit bigger)
HIP_X = 420         # the skeletons' hips (picture x): the pivot's x, the same for every pose
GROUND_Y = 1150     # the feet (picture y) of the standing poses


def key(t, v, ease=None):
    k = {'t': round(t, 3), 'v': v}
    if ease:
        k['ease'] = ease
    return k


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--weapon', default='bow')
    ap.add_argument('--pick', action='append', default=[], help='pose=seed')
    a = ap.parse_args()
    picks = dict(p.split('=') for p in a.pick)
    work = ROOT / 'Build' / 'art_out' / f'hero_{a.weapon}'
    imgs = {pose: Image.open(work / f'{pose}_{seed}_cut.png').convert('RGBA') for pose, seed in picks.items()}

    # one frame for all: the union of the poses' opaque boxes (with a margin), so a pose keeps its place in the frame
    boxes = [im.getchannel('A').point(lambda v: 255 if v > 16 else 0).getbbox() for im in imgs.values()]
    x0 = max(0, min(b[0] for b in boxes) - 8)
    y0 = max(0, min(b[1] for b in boxes) - 8)
    x1 = min(832, max(b[2] for b in boxes) + 8)
    y1 = min(1216, max(b[3] for b in boxes) + 8)
    w, h = int((x1 - x0) * DOWN), int((y1 - y0) * DOWN)
    sprites = {pose: im.crop((x0, y0, x1, y1)).resize((w, h), Image.LANCZOS) for pose, im in imgs.items()}

    # pack in a grid (3 across), 2 px apart
    cols = 3
    names = list(sprites)
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new('RGBA', (cols * (w + 2) + 2, rows * (h + 2) + 2), (0, 0, 0, 0))
    lines = [f'hero_{a.weapon}.png', f'size: {sheet.width}, {sheet.height}', 'format: RGBA8888', 'filter: Linear, Linear',
             'repeat: none']
    for i, n in enumerate(names):
        x, y = 2 + (i % cols) * (w + 2), 2 + (i // cols) * (h + 2)
        sheet.paste(sprites[n], (x, y))
        lines += [n, f'  bounds: {x}, {y}, {w}, {h}']
    ATLAS_DIR.mkdir(parents=True, exist_ok=True)
    sheet.save(ATLAS_DIR / f'hero_{a.weapon}.png')
    (ATLAS_DIR / f'hero_{a.weapon}.atlas').write_text('\n'.join(lines) + '\n', encoding='utf-8')

    # the clips: one node showing a pose, its pivot at the hips' x on the ground line. Not named "body": the battle puts the
    # player's map sprite on a node "body" (Game::fitClipToCharacter, for the clips any character wears), and these are
    # the painted hero's own pictures.
    s = HEIGHT / float((GROUND_Y - y0) * DOWN)        # clip units per atlas pixel (the standing figure HEIGHT tall)
    pivot = [round((HIP_X - x0) / (x1 - x0), 4), round((GROUND_Y - y0) / (y1 - y0), 4)]
    atlas = f'hero_{a.weapon}'

    def body(tracks, sprite):
        return {'name': 'hero', 'children': [{'name': 'painted', 'sprite': f'{atlas}:{sprite}', 'pivot': pivot,
                                              'scale': [round(s, 5), round(s, 5), 1], 'tracks': tracks}]}

    def has(p):
        return p in sprites

    ready = 'idle' if has('idle') else names[0]
    draw = 'charge' if has('charge') else ready
    clips = []
    # idle: breathing
    clips.append({'name': 'idle', 'length': 1.6, 'playCount': -1, 'root': body({
        'scale': [key(0, [s, s, 1], 'sinusoidalInOut'), key(0.8, [s, s * 1.015, 1], 'sinusoidalInOut'), key(1.6, [s, s, 1])]},
        ready)})
    # charge: the full draw, held with a small tremble
    clips.append({'name': 'charge', 'length': 1.2, 'playCount': -1, 'root': body({
        'pos': [key(0, [0, 0, 0]), key(0.6, [0, 0, 0]), key(0.68, [0.25, 0, 0]), key(0.76, [-0.2, 0, 0]),
                key(0.84, [0.2, 0, 0]), key(0.92, [-0.15, 0, 0]), key(1.0, [0, 0, 0])]}, draw)})
    # attack: the draw, a snap forward (the release) and a recoil, then back to ready
    clips.append({'name': 'attack', 'length': 0.6, 'root': body({
        'sprite': [key(0, f'{atlas}:{draw}'), key(0.42, f'{atlas}:{ready}')],
        'pos': [key(0, [0, 0, 0]), key(0.06, [0.6, 0, 0], 'quadraticOut'), key(0.14, [-1.6, 0, 0], 'quadraticOut'),
                key(0.4, [0, 0, 0], 'quadraticInOut')],
        'rot': [key(0, [0, 0, 0]), key(0.14, [0, 0, 3]), key(0.4, [0, 0, 0])],
        'scale': [key(0, [s, s, 1]), key(0.06, [s * 1.04, s * 0.97, 1]), key(0.2, [s, s, 1])]}, draw)})
    # miss: the stumble, a wobble, then ready
    if has('miss'):
        clips.append({'name': 'miss', 'length': 0.7, 'root': body({
            'sprite': [key(0, f'{atlas}:miss'), key(0.55, f'{atlas}:{ready}')],
            'rot': [key(0, [0, 0, 0]), key(0.12, [0, 0, 4], 'quadraticOut'), key(0.3, [0, 0, -2]), key(0.5, [0, 0, 0])],
            'pos': [key(0, [0, 0, 0]), key(0.15, [-1.2, 0, 0], 'quadraticOut'), key(0.5, [0, 0, 0])]}, 'miss')})
    # attack_big: crouch, the leap into the painted power shot, and the landing -- 0.8 s like the other bow clips (the
    # battle's timers expect the bow lowered by then: tests/scenarios/battle_bow_idle)
    if has('attack_big'):
        clips.append({'name': 'attack_big', 'length': 0.8, 'root': body({
            'sprite': [key(0, f'{atlas}:{ready}'), key(0.12, f'{atlas}:attack_big'), key(0.64, f'{atlas}:{ready}')],
            'pos': [key(0, [0, 0, 0]), key(0.1, [0, 0.8, 0], 'quadraticIn'), key(0.28, [1.5, -7, 0], 'quadraticOut'),
                    key(0.5, [1.5, -7.5, 0]), key(0.64, [0, 0, 0], 'quadraticIn'), key(0.8, [0, 0, 0])],
            'scale': [key(0, [s, s, 1]), key(0.1, [s * 1.05, s * 0.92, 1]), key(0.18, [s * 0.97, s * 1.05, 1]),
                      key(0.28, [s, s, 1]), key(0.64, [s * 1.05, s * 0.93, 1]), key(0.74, [s, s, 1])]}, ready)})
    # hurt (for later): the ready pose knocked back
    clips.append({'name': 'hurt', 'length': 0.5, 'root': body({
        'rot': [key(0, [0, 0, 0]), key(0.08, [0, 0, 8], 'quadraticOut'), key(0.45, [0, 0, 0], 'quadraticInOut')],
        'pos': [key(0, [0, 0, 0]), key(0.08, [-2, 0, 0], 'quadraticOut'), key(0.45, [0, 0, 0])]}, ready)})

    anim = {'version': 2, 'atlases': [f'../atlas/hero_{a.weapon}.atlas'], 'clips': clips}
    out = ANIM_DIR / f'hero_{a.weapon}.anim'
    out.write_text(json.dumps(anim, indent=1) + '\n', encoding='utf-8')
    print(f'{ATLAS_DIR / f"hero_{a.weapon}.atlas"}: {len(names)} poses, {sheet.width} x {sheet.height}')
    print(f'{out}: {", ".join(c["name"] for c in clips)}')


if __name__ == '__main__':
    main()
