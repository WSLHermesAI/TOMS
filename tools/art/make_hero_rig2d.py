#!/usr/bin/env python3
"""make_hero_rig2d.py -- the player made like Spine: painted parts in several poses on one skeleton (docs/engine/20 §5).

    python tools/art/make_hero_rig2d.py [--weapon bow] [--pick idle=55 --pick charge=44 --pick miss=44 --pick attack_big=44]

The owner (2026-11-02): one picture with a little shaking is not enough, 3D looked bad; make it like Spine -- parts in
different directions, assembled in the anim editor.

1. Each painted key pose (make_hero_poses.py: the same person, painted on a known OpenPose skeleton) is cut into 11
   parts along its own skeleton: head, torso, cape, and near/far upper arm, forearm (the near one holds the bow), thigh
   and shin. So every part exists as several paintings, one per pose: the arm straight, bent, raised; the head turned.
2. The parts of all poses are packed into assets/media/atlas/hero_<weapon>.atlas (+ .png).
3. One skeleton (hips, chest, neck, shoulders, elbows, hips, knees) carries them in assets/media/anim/hero_<weapon>.anim.
   A clip moves from pose A to pose B the Spine way: the bones turn A's parts toward B's joint angles (the limbs really
   move), then B's paintings take over (a sprite key). Breathing, the tremble at full draw, the cape's sway, the
   crouch, leap and landing are bone keys on top.

Picture labels (where each part was cut) are written to Build/art_out/hero_<weapon>/parts/labels_<pose>.png.
The .anim opens in the anim editor (Build/windows-release/bin/anim_editor.exe) for hand tuning.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_hero_poses import POSES   # noqa: E402  (each pose's skeleton)

ROOT = Path(__file__).resolve().parents[2]
ATLAS_DIR = ROOT / 'assets' / 'media' / 'atlas'
ANIM_DIR = ROOT / 'assets' / 'media' / 'anim'
DOWN = 0.5            # picture pixels -> atlas pixels
HEIGHT = 40.0         # the standing figure's height in clip units (the battle draws 32 at the box's height)
GROUND_Y = 1150       # the feet's line in the standing pictures
X0 = 427              # the idle picture's middle of the hips: x = 0 in the clip
OVERLAP = 14          # picture pixels a part reaches into its parent, so a turned joint shows no gap
LIMB_SLACK = 10       # picture pixels beyond its half width that a limb may still claim
EASE = 'quadraticInOut'

# part -> (from joint, to joint, half width, parent part, the joint it turns about)
PARTS = {
    'torso': ('mid_hip', 'neck', 78, None, 'mid_hip'),
    'head': ('neck', 'head_top', 58, 'torso', 'neck'),
    'upperarm_far': ('r_shoulder', 'r_elbow', 24, 'torso', 'r_shoulder'),
    'forearm_far': ('r_elbow', 'r_hand', 24, 'upperarm_far', 'r_elbow'),
    'upperarm_near': ('l_shoulder', 'l_elbow', 24, 'torso', 'l_shoulder'),
    'forearm_near': ('l_elbow', 'l_hand', 24, 'upperarm_near', 'l_elbow'),
    'thigh_far': ('r_hip', 'r_knee', 34, 'torso', 'r_hip'),
    'shin_far': ('r_knee', 'r_foot', 30, 'thigh_far', 'r_knee'),
    'thigh_near': ('l_hip', 'l_knee', 34, 'torso', 'l_hip'),
    'shin_near': ('l_knee', 'l_foot', 30, 'thigh_near', 'l_knee'),
}
LABEL_COLORS = {'torso': (200, 60, 60), 'head': (240, 200, 60), 'cape': (120, 70, 30), 'upperarm_far': (60, 120, 220),
                'forearm_far': (60, 200, 220), 'upperarm_near': (60, 200, 90), 'forearm_near': (170, 230, 60),
                'thigh_far': (150, 60, 200), 'shin_far': (220, 90, 200), 'thigh_near': (240, 140, 40),
                'shin_near': (250, 220, 150)}


def joints_of(weapon, pose):
    """The pose's skeleton plus the points the parts need (the head's top, the hands, the feet, the hips' middle)."""
    j = {k: np.array(v, float) for k, v in POSES[weapon][pose][0].items()}
    j['mid_hip'] = (j['r_hip'] + j['l_hip']) / 2
    j['head_top'] = j['nose'] + (j['nose'] - j['neck']) * 1.15
    for s in ('r', 'l'):
        j[f'{s}_hand'] = j[f'{s}_wrist'] + (j[f'{s}_wrist'] - j[f'{s}_elbow']) * 0.35
        j[f'{s}_foot'] = j[f'{s}_ankle'] + np.array([20.0, 45.0])
        j[f'{s}_fist'] = j[f'{s}_wrist'] + (j[f'{s}_wrist'] - j[f'{s}_elbow']) * 0.15
    return j


def seg_dist(px, a, b):
    ab = b - a
    t = np.clip(((px - a) @ ab) / max(ab @ ab, 1e-6), 0, 1)
    return np.linalg.norm(px - (a + t[:, None] * ab), axis=1)


def cut(pose, img, j, reach=420, drop_blades=False):
    """Each opaque pixel's part: the nearest capsule (its bone, its half width); the bow goes with the near hand; what is
    far from every limb behind the body is the cape."""
    rgba = np.asarray(img).copy()
    alpha = rgba[..., 3]
    solid = alpha >= 250   # the soft edge takes the nearest solid colour (no pale halo where parts overlap)
    _, (iy, ix) = ndimage.distance_transform_edt(~solid, return_indices=True)
    rgba[~solid, :3] = rgba[iy[~solid], ix[~solid], :3]
    ys, xs = np.nonzero(alpha > 12)
    px = np.stack([xs, ys], 1).astype(float)
    names = list(PARTS)
    d = np.stack([seg_dist(px, j[f], j[t]) - w for f, t, w, _, _ in PARTS.values()], 1)
    lab = np.array(names, dtype=object)[np.argmin(d, 1)]
    dmin = d.min(1)
    # a limb keeps only what is close to its bone: the tunic or the cape painted beside an arm is not the arm (it
    # would fly out with it); it goes to the torso when near the body, else to the cape below
    limb = ~np.isin(lab, ['torso', 'head'])
    loose = limb & (dmin > LIMB_SLACK)
    torso_d = d[:, names.index('torso')]
    spine_x = (j['mid_hip'][0] + j['neck'][0]) / 2
    front = xs > spine_x + 40   # the cape hangs behind: in front, loose pixels are the bow's or the torso's
    lab[loose & ((torso_d < 70) | front)] = 'torso'
    lab[loose & (torso_d >= 70) & ~front] = 'cape'
    # the bow (and its string): thin things near the bow hand -- the local half width (the distance to the background,
    # its largest in a small window) is small along a bow or a string, large inside cloth
    edt = ndimage.distance_transform_edt(alpha > 12)
    thin = ndimage.maximum_filter(edt, size=15)[ys, xs] < 11
    near_hand = np.linalg.norm(px - j['l_wrist'], axis=1)
    bow = (((dmin > 18) & thin) | (near_hand < 45)) & (near_hand < reach) & (lab != 'head')
    lab[bow] = 'forearm_near'
    if drop_blades:   # the weapon is its own part: a blade the painter added anyway (thin, grey, away from the body) goes
        rgbf = rgba[ys, xs, :3].astype(float) / 255.0
        mx, mn = rgbf.max(1), rgbf.min(1)
        grey = (mx - mn < 0.18 * np.maximum(mx, 1e-3)) & (mx > 0.45)
        blade = thin & (dmin > 18) & grey & ~bow
        alpha[ys[blade], xs[blade]] = 0
        lab[blade] = ''
    # the cape: far from every limb, behind the body (left of the spine line)
    cape = (dmin > 22) & ~bow & (xs < spine_x + 20) & (ys > j['neck'][1] - 10)
    lab[cape] = 'cape'
    full = np.full(alpha.shape, '', dtype=object)
    full[ys, xs] = lab
    return rgba, full


def painted_fist(lab, j):
    """The weapon hand as painted: the middle of the near forearm's far end (its last 8 % along elbow -> wrist, within
    40 px of that line). The skeleton's wrist is only where the painting was asked to put it."""
    ys, xs = np.nonzero(lab == 'forearm_near')
    if len(xs) < 50:
        return j['l_fist']
    px = np.stack([xs, ys], 1).astype(float)
    e, w = j['l_elbow'], j['l_wrist']
    d = (w - e) / max(np.linalg.norm(w - e), 1e-6)
    along = (px - e) @ d
    across = np.abs((px - e) @ np.array([-d[1], d[0]]))
    near = across < 40
    if near.sum() < 50:
        return j['l_fist']
    tip = near & (along >= np.percentile(along[near], 92))
    return px[tip].mean(0)


def part_sprites(pose, rgba, lab, j):
    parents = {k: v[3] for k, v in PARTS.items()}
    parents['cape'] = 'torso'
    pivots = {k: v[4] for k, v in PARTS.items()}
    pivots['cape'] = 'neck'
    k = OVERLAP * 2 + 1
    out = {}
    for name, parent in parents.items():
        mask = lab == name
        if not mask.any():
            continue
        if parent:   # reach into the parent: the joint is covered when it turns
            grown = np.asarray(Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(k))) > 0
            mask |= grown & (lab == parent)
        # Cropped centred on its joint: the pivot is the sprite's middle in every pose. (A node has one pivot; the
        # poses' paintings of a part swap on it, so they must all turn about the same point of the sprite.)
        ys, xs = np.nonzero(mask)
        p = j[pivots[name]]
        hw = int(math.ceil(max(p[0] - xs.min(), xs.max() + 1 - p[0]))) + 2
        hh = int(math.ceil(max(p[1] - ys.min(), ys.max() + 1 - p[1]))) + 2
        cx, cy = int(round(p[0])), int(round(p[1]))
        part = np.zeros((2 * hh, 2 * hw, 4), np.uint8)
        sx0, sy0 = max(cx - hw, 0), max(cy - hh, 0)
        sx1, sy1 = min(cx + hw, rgba.shape[1]), min(cy + hh, rgba.shape[0])
        piece = rgba[sy0:sy1, sx0:sx1].copy()
        piece[..., 3] = np.where(mask[sy0:sy1, sx0:sx1], piece[..., 3], 0)
        part[sy0 - (cy - hh):sy1 - (cy - hh), sx0 - (cx - hw):sx1 - (cx - hw)] = piece
        im = Image.fromarray(part).resize((max(2, round(2 * hw * DOWN)), max(2, round(2 * hh * DOWN))), Image.LANCZOS)
        out[name] = (im, [0.5, 0.5])
    return out


def pack(sprites, width=2048, pad=2):
    """Shelf packing: tallest first."""
    order = sorted(sprites, key=lambda n: -sprites[n].height)
    x = y = shelf = 0
    places = {}
    for n in order:
        w, h = sprites[n].size
        if x + w + pad > width:
            x, y, shelf = 0, y + shelf + pad, 0
        places[n] = (x + pad, y + pad)
        x += w + pad
        shelf = max(shelf, h)
    sheet = Image.new('RGBA', (width, y + shelf + 2 * pad), (0, 0, 0, 0))
    for n, (px_, py_) in places.items():
        sheet.paste(sprites[n], (px_, py_))
    return sheet, places


# ---- the rig ----

# bone node -> (part on it, parent node, its joint, the bone's direction (from, to) for the angle, order)
BONES = {
    'hips': (None, None, 'mid_hip', None, 0),
    'cape': ('cape', 'hips', 'neck', None, -3),
    'hip_far': ('thigh_far', 'hips', 'r_hip', ('r_hip', 'r_knee'), -2),
    'knee_far': ('shin_far', 'hip_far', 'r_knee', ('r_knee', 'r_ankle'), 0),
    'hip_near': ('thigh_near', 'hips', 'l_hip', ('l_hip', 'l_knee'), -1),
    'knee_near': ('shin_near', 'hip_near', 'l_knee', ('l_knee', 'l_ankle'), 0),
    'chest': ('torso', 'hips', 'mid_hip', ('mid_hip', 'neck'), 0),
    'shoulder_far': ('upperarm_far', 'chest', 'r_shoulder', ('r_shoulder', 'r_elbow'), -1),
    'elbow_far': ('forearm_far', 'shoulder_far', 'r_elbow', ('r_elbow', 'r_wrist'), 0),
    'neck': ('head', 'chest', 'neck', ('neck', 'nose'), 1),
    'shoulder_near': ('upperarm_near', 'chest', 'l_shoulder', ('l_shoulder', 'l_elbow'), 2),
    'elbow_near': ('forearm_near', 'shoulder_near', 'l_elbow', ('l_elbow', 'l_wrist'), 0),
    'weapon': ('weapon', 'elbow_near', 'l_fist', None, -1),   # only with WEAPON_PARTS (else removed)
}


def angle(j, a, b):
    d = j[b] - j[a]
    return math.degrees(math.atan2(d[1], d[0]))   # screen: y down, clockwise positive (the .anim's z rotation)


def wrap(a):
    return (a + 180.0) % 360.0 - 180.0


class Rig:
    def __init__(self, atlas, joints, pivots, weapon=None):
        self.atlas, self.J, self.pivots, self.weapon = atlas, joints, pivots, weapon

    def sprite(self, part, pose):
        """The part's painting in the pose; a part the pose's cut has none of (forearms over the head) takes the ready
        pose's: every part is centred on its joint, so any pose's painting of it fits its bone."""
        name = f'{part}_{pose}'
        return name if name in self.available else f'{part}_idle'

    def state(self, s, g=None, rot=None, root=(0.0, 0.0), squash=(1.0, 1.0), f=1.0):
        """The nodes' values: the paintings of pose s, the bones turned toward pose g's joint angles, plus extra
        rotations (degrees per bone), a root offset (atlas px) and a squash of the hips. f: how far toward g (a big
        change of pose only goes part of the way before g's paintings take over: further, the parts show gaps)."""
        g = g or s
        rot = rot or {}
        J, G = self.J[s], self.J[g]
        world = {}
        out = {}
        for n, (part, parent, jt, seg, _) in BONES.items():
            if n == 'weapon':   # its own angle per pose (absolute: no painting of it sits in the pose)
                if not self.weapon:
                    continue
                a0, a1 = self.weapon['angles'][s], self.weapon['angles'][g]
                world[n] = a0 + f * (a1 - a0) + rot.get(n, 0.0)
                local = world[n] - world[parent]
                p = (J[jt] - J[BONES[parent][2]]) * DOWN
                out[n] = {'pos': [round(float(p[0]), 2), round(float(p[1]), 2), 0], 'rot': [0, 0, round(local, 2)],
                          'sprite': f'{self.atlas}:weapon', 'scale': None, 'fixed': True}
                continue
            d = f * wrap(angle(G, *seg) - angle(J, *seg)) if seg else 0.0
            world[n] = d + rot.get(n, 0.0) + (world[parent] if parent else 0.0)
            local = world[n] - (world[parent] if parent else 0.0)
            if parent is None:   # the hips: where the pose g puts them, from the idle picture's ground point
                hip = J['mid_hip'] + f * (G['mid_hip'] - J['mid_hip'])
                p = (hip - np.array([X0, GROUND_Y])) * DOWN + np.array(root)
            else:
                p = (J[jt] - J[BONES[parent][2]]) * DOWN
            out[n] = {'pos': [round(float(p[0]), 2), round(float(p[1]), 2), 0], 'rot': [0, 0, round(local, 2)],
                      'sprite': f'{self.atlas}:{self.sprite(part, s)}' if part else None,
                      'scale': [squash[0], squash[1], 1] if parent is None else None}
        return s, out

    def clip(self, name, length, keys, loop=False):
        """keys: [(t, state)]. A change of paintings (sprite keys) holds the old values to just before the key."""
        names = [n for n in BONES if n in keys[0][1][1]]
        tracks = {n: {'pos': [], 'rot': [], 'sprite': [], 'scale': []} for n in names}
        prev = None
        for t, (s, vals) in keys:
            if prev is not None and prev[0] != s and t - 0.02 > prev[2] + 1e-6:
                for n, v in prev[1].items():   # hold the old paintings to the swap
                    tracks[n]['pos'].append({'t': round(t - 0.02, 3), 'v': v['pos'], 'ease': EASE})
                    tracks[n]['rot'].append({'t': round(t - 0.02, 3), 'v': v['rot'], 'ease': EASE})
            for n, v in vals.items():
                tracks[n]['pos'].append({'t': round(t, 3), 'v': v['pos'], 'ease': EASE})
                tracks[n]['rot'].append({'t': round(t, 3), 'v': v['rot'], 'ease': EASE})
                if v['sprite'] and (prev is None or prev[0] != s) and not v.get('fixed'):
                    tracks[n]['sprite'].append({'t': round(t, 3), 'v': v['sprite']})
                if v['scale']:
                    tracks[n]['scale'].append({'t': round(t, 3), 'v': v['scale'], 'ease': EASE})
            prev = (s, vals, t)

        def node(n):
            part, parent, jt, seg, order = BONES[n]
            first = keys[0][1][1][n]
            nd = {'name': n, 'pos': first['pos'], 'rot': first['rot']}
            if part:
                nd['sprite'] = first['sprite']
                nd['pivot'] = [0.5, 0.5] if part != 'weapon' else self.weapon['pivot']
            if order:
                nd['order'] = order
            tr = {k: v for k, v in tracks[n].items() if v and not (k == 'sprite' and len(v) < 2)}
            if tr:
                nd['tracks'] = tr
            kids = [m for m, b in BONES.items() if b[1] == n and m in names]
            if kids:
                nd['children'] = [node(m) for m in kids]
            return nd

        c = {'name': name, 'length': length, 'root': {'name': 'hero', 'scale': [self.s, self.s, 1],
                                                       'children': [node('hips')]}}
        if loop:
            c['playCount'] = -1
        return c


# the picked seed of each painted pose (make_hero_poses.py); attack and hurt are optional (made by bones without them)
DEFAULT_PICKS = {
    'bow': {'idle': '55', 'charge': '44', 'attack': '73', 'miss': '44', 'attack_big': '44'},
    'sword': {'idle': '81', 'charge': '81', 'attack': '83', 'miss': '81', 'attack_big': '82', 'hurt': '81'},
    'staff': {'idle': '87', 'charge': '81', 'attack': '83', 'miss': '83', 'attack_big': '81', 'hurt': '81'},
}
REACH = {'bow': 420, 'sword': 60, 'staff': 60}   # picture pixels from the weapon hand a painted weapon may reach
# A weapon painted on its own (make_weapon_art.py, upright: grip low, tip up), a part on the near hand. Per pose, its
# angle at rest (screen degrees: 0 = up, 90 = toward the enemy, clockwise). Between poses it turns by the plain
# difference (no shortest way): the angles are chosen so the swing goes the right way (raised -60 -> slash 120: forward
# over the top). length: picture pixels from end to end; grip: where the fist holds it (0 = the bottom end).
WEAPON_PARTS = {
    'sword': {'seed': '44', 'length': 480, 'width': 1.7, 'grip': 0.1, 'flip': True,   # (painted tip down; widened to read)
              'angles': {'idle': 40, 'charge': -60, 'attack': 120, 'miss': -25, 'attack_big': -15, 'hurt': 150}},
    'staff': {'seed': '44', 'length': 860, 'grip': 0.42,
              'angles': {'idle': 0, 'charge': 15, 'attack': 80, 'miss': -25, 'attack_big': 0, 'hurt': 20}},
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--weapon', default='bow')
    ap.add_argument('--pick', action='append', default=[], help='pose=seed')
    a = ap.parse_args()
    picks = dict(p.split('=') for p in a.pick) or DEFAULT_PICKS[a.weapon]
    work = ROOT / 'Build' / 'art_out' / f'hero_{a.weapon}'
    (work / 'parts').mkdir(parents=True, exist_ok=True)
    atlas = f'hero_{a.weapon}'
    sprites, pivots, joints = {}, {}, {}
    for pose, seed in picks.items():
        img = Image.open(work / f'{pose}_{seed}_cut.png').convert('RGBA')
        j = joints_of(a.weapon, pose)
        joints[pose] = j
        rgba, lab = cut(pose, img, j, REACH.get(a.weapon, 420), drop_blades=a.weapon in WEAPON_PARTS)
        if a.weapon in WEAPON_PARTS:   # the weapon in the painted fist, not where the skeleton guessed it
            j['l_fist'] = painted_fist(lab, j)
        # the labels, to check the cut
        vis = Image.new('RGB', img.size, (25, 25, 30))
        arr = np.asarray(vis).copy()
        for name, col in LABEL_COLORS.items():
            arr[lab == name] = col
        vis = Image.blend(Image.fromarray(arr), img.convert('RGB'), 0.35)
        dr = ImageDraw.Draw(vis)
        for f, t, _, _, _ in PARTS.values():
            dr.line([tuple(j[f]), tuple(j[t])], fill=(255, 255, 255), width=3)
        vis.save(work / 'parts' / f'labels_{pose}.png')
        for name, (im, pv) in part_sprites(pose, rgba, lab, j).items():
            sprites[f'{name}_{pose}'] = im
            pivots[(name, pose)] = pv
    weapon = WEAPON_PARTS.get(a.weapon)
    if weapon:   # the weapon part: cropped to the painting, scaled to its length, the pivot at the grip
        wim = Image.open(ROOT / 'Build' / 'art_out' / f'weapon_{a.weapon}' / f"{weapon['seed']}_cut.png").convert('RGBA')
        wim = wim.crop(wim.getchannel('A').point(lambda v: 255 if v > 16 else 0).getbbox())
        if weapon.get('flip'):
            wim = wim.transpose(Image.FLIP_TOP_BOTTOM)
        h = max(2, round(weapon['length'] * DOWN))
        sprites['weapon'] = wim.resize((max(2, round(wim.width * h / wim.height * weapon.get('width', 1.0))), h),
                                       Image.LANCZOS)
        weapon['pivot'] = [0.5, round(1.0 - weapon['grip'], 4)]
    sheet, places = pack(sprites)
    ATLAS_DIR.mkdir(parents=True, exist_ok=True)
    sheet.save(ATLAS_DIR / f'{atlas}.png')
    lines = [f'{atlas}.png', f'size: {sheet.width}, {sheet.height}', 'format: RGBA8888', 'filter: Linear, Linear',
             'repeat: none']
    for n in sorted(places):
        x, y = places[n]
        lines += [n, f'  bounds: {x}, {y}, {sprites[n].width}, {sprites[n].height}']
    (ATLAS_DIR / f'{atlas}.atlas').write_text('\n'.join(lines) + '\n', encoding='utf-8')

    rig = Rig(atlas, joints, pivots, weapon)
    rig.available = set(sprites)
    top = joints['idle']['head_top'][1]
    rig.s = round(HEIGHT / ((GROUND_Y - top) * DOWN), 5)
    st = rig.state
    strike = 'attack' if 'attack' in joints else 'charge'   # (no painting of the strike: the drawn pose, moved by bones)

    def to_strike(t, pose, root):   # the Spine way: the bones turn toward the strike, then its painting takes over
        return [(t, st(pose, strike, root=root, f=0.5)), (t + 0.01, st(strike, pose, root=root, f=0.5))]

    clips = [
        # idle: breathing -- the chest leans in, the head and arms follow, the cape sways
        rig.clip('idle', 1.6, [
            (0.0, st('idle')),
            (0.8, st('idle', rot={'chest': -1.5, 'neck': 2.0, 'shoulder_far': 3.0, 'shoulder_near': -2.5,
                                  'elbow_near': 2.0, 'cape': 4.0}, squash=(1.0, 1.012))),
            (1.6, st('idle'))], loop=True),
        # charge: the full draw held, a small tremble in the bow arm, the cape stirring
        rig.clip('charge', 1.2, [
            (0.0, st('charge')),
            (0.3, st('charge', rot={'shoulder_near': 0.8, 'elbow_near': -0.6, 'weapon': 2.0, 'cape': 3.0})),
            (0.6, st('charge', rot={'shoulder_near': -0.5, 'weapon': -1.5, 'cape': -1.5, 'chest': 0.4})),
            (0.9, st('charge', rot={'shoulder_near': 0.6, 'elbow_near': 0.5, 'weapon': 1.5, 'cape': 2.0})),
            (1.2, st('charge'))], loop=True),
        # attack: the strike. With a painting of it: toward it, its painting, the follow-through, back to ready; without
        # (the bow): the drawing hand snaps back by bones
        rig.clip('attack', 0.6, [
            (0.0, st('charge')),
            (0.05, st('charge', rot={'weapon': -12.0, 'chest': -3.0, 'shoulder_near': -6.0, 'cape': 3.0})),
            (0.1, st('charge', 'attack', f=0.5)),
            (0.11, st('attack', 'charge', f=0.5)),
            (0.16, st('attack', rot={'weapon': 8.0, 'chest': 2.0, 'cape': -4.0}, root=(3.0, 0.0))),
            (0.24, st('attack', root=(2.0, 0.0))),
            (0.36, st('attack', rot={'chest': 1.0, 'cape': -7.0}, root=(2.0, 0.0))),
            (0.46, st('attack', 'idle', f=0.4)),
            (0.48, st('idle', 'attack', f=0.6)),
            (0.6, st('idle'))]) if 'attack' in joints else
        rig.clip('attack', 0.6, [
            (0.0, st('charge')),
            (0.05, st('charge', rot={'shoulder_far': -14.0, 'elbow_far': -22.0, 'shoulder_near': -1.5, 'chest': -1.5,
                                     'cape': -6.0})),
            (0.16, st('charge', rot={'shoulder_far': -16.0, 'elbow_far': -26.0, 'shoulder_near': -1.0, 'cape': -8.0})),
            (0.36, st('charge', 'idle', f=0.4)),
            (0.38, st('idle', 'charge', f=0.6)),
            (0.6, st('idle'))]),
        # the combo's later hits, each bigger than the last (docs/gameplay/17 §19; weapon_play.json "comboTiers")
        # attack_2: a harder strike with a lunge
        rig.clip('attack_2', 0.55, [
            (0.0, st('charge')),
            (0.05, st('charge', rot={'weapon': -18.0, 'chest': -5.0, 'shoulder_near': -8.0, 'cape': 5.0}, root=(-6.0, 2.0))),
            *to_strike(0.1, 'charge', root=(6.0, 0.0)),
            (0.15, st(strike, rot={'weapon': 14.0, 'chest': 4.0, 'cape': -8.0}, root=(14.0, 0.0))),
            (0.26, st(strike, rot={'cape': -10.0}, root=(12.0, 0.0))),
            (0.38, st(strike, rot={'chest': 1.0, 'cape': -8.0}, root=(8.0, 0.0))),
            (0.46, st(strike, 'idle', root=(4.0, 0.0), f=0.4)),
            (0.48, st('idle', strike, root=(3.0, 0.0), f=0.6)),
            (0.55, st('idle'))]),
        # attack_3: a crouch, a hop and two strikes in the air, a landing
        rig.clip('attack_3', 0.7, [
            (0.0, st('charge')),
            (0.05, st('charge', rot={'hip_far': -10.0, 'knee_far': 18.0, 'hip_near': -12.0, 'knee_near': 20.0, 'chest': -4.0, 'weapon': -16.0},
                      root=(-8.0, 8.0), squash=(1.03, 0.95))),
            *to_strike(0.1, 'charge', root=(4.0, -6.0)),
            (0.14, st(strike, rot={'weapon': 12.0, 'cape': -8.0}, root=(12.0, -12.0))),
            (0.22, st(strike, rot={'weapon': -20.0, 'chest': -3.0, 'cape': 4.0}, root=(12.0, -16.0))),
            (0.3, st(strike, rot={'weapon': 22.0, 'chest': 5.0, 'cape': -12.0}, root=(22.0, -18.0), squash=(1.03, 0.98))),
            (0.42, st(strike, rot={'cape': -10.0}, root=(18.0, -10.0))),
            (0.52, st(strike, 'idle', root=(8.0, -2.0), f=0.4)),
            (0.54, st('idle', strike, root=(6.0, 0.0), f=0.6)),
            (0.6, st('idle', rot={'hip_far': -8.0, 'knee_far': 14.0, 'hip_near': -10.0, 'knee_near': 16.0}, root=(2.0, 8.0), squash=(1.04, 0.95))),
            (0.7, st('idle'))]),
        # attack_4: three strikes, each further forward and higher, a heavy landing
        rig.clip('attack_4', 0.82, [
            (0.0, st('charge')),
            (0.05, st('charge', rot={'hip_far': -14.0, 'knee_far': 24.0, 'hip_near': -16.0, 'knee_near': 26.0, 'chest': -6.0, 'weapon': -20.0, 'cape': 6.0},
                      root=(-10.0, 12.0), squash=(1.05, 0.93))),
            *to_strike(0.1, 'charge', root=(4.0, -8.0)),
            (0.13, st(strike, rot={'weapon': 14.0, 'cape': -8.0}, root=(10.0, -14.0))),
            (0.2, st(strike, rot={'weapon': -22.0, 'chest': -3.0, 'cape': 4.0}, root=(12.0, -20.0))),
            (0.27, st(strike, rot={'weapon': 20.0, 'chest': 4.0, 'cape': -10.0}, root=(20.0, -26.0))),
            (0.34, st(strike, rot={'weapon': -24.0, 'chest': -4.0, 'cape': 6.0}, root=(22.0, -32.0))),
            (0.42, st(strike, rot={'weapon': 26.0, 'chest': 7.0, 'cape': -14.0}, root=(32.0, -36.0), squash=(1.04, 0.97))),
            (0.54, st(strike, rot={'cape': -12.0}, root=(26.0, -20.0))),
            (0.64, st(strike, 'idle', root=(12.0, -4.0), f=0.4)),
            (0.66, st('idle', strike, root=(10.0, 0.0), f=0.6)),
            (0.72, st('idle', rot={'hip_far': -12.0, 'knee_far': 20.0, 'hip_near': -14.0, 'knee_near': 22.0}, root=(4.0, 12.0), squash=(1.06, 0.92))),
            (0.82, st('idle'))]),
        # the fight's end (docs/gameplay/17 §19; battle.json feel): quick, and they stay on their last pose
        # win: a quick victory pose
        rig.clip('win', 0.45, [
            (0.0, st('idle')),
            (0.06, st('idle', rot={'knee_far': 10.0, 'knee_near': 12.0, 'hip_far': -6.0, 'hip_near': -7.0}, root=(0.0, 6.0), squash=(1.02, 0.97))),
            (0.14, st('idle', 'attack_big', root=(0.0, -8.0), f=0.5)),
            (0.15, st('attack_big', 'idle', root=(0.0, -10.0), f=0.5)),
            (0.24, st('attack_big', rot={'cape': 6.0}, root=(0.0, -6.0))),
            (0.34, st('attack_big', rot={'cape': -3.0}, root=(0.0, 0.0), squash=(1.02, 0.98))),
            (0.45, st('attack_big', rot={'cape': 2.0}))]),
        # win_big: no damage taken: a deep crouch, a high jump with the weapon up, a landing that bounces once
        rig.clip('win_big', 0.7, [
            (0.0, st('idle')),
            (0.07, st('idle', rot={'hip_far': -16.0, 'knee_far': 28.0, 'hip_near': -18.0, 'knee_near': 30.0, 'chest': 5.0, 'cape': -5.0},
                      root=(0.0, 16.0), squash=(1.05, 0.93))),
            (0.15, st('idle', 'attack_big', root=(0.0, -40.0), f=0.5)),
            (0.16, st('attack_big', 'idle', root=(0.0, -50.0), f=0.5)),
            (0.27, st('attack_big', rot={'weapon': 18.0, 'cape': 9.0}, root=(0.0, -92.0), squash=(0.98, 1.03))),
            (0.36, st('attack_big', rot={'weapon': -14.0, 'cape': 7.0}, root=(0.0, -78.0))),
            (0.46, st('attack_big', rot={'cape': -4.0}, root=(0.0, 0.0), squash=(1.05, 0.93))),
            (0.54, st('attack_big', rot={'weapon': 8.0, 'cape': 4.0}, root=(0.0, -24.0))),
            (0.62, st('attack_big', rot={'cape': -2.0}, root=(0.0, 0.0), squash=(1.02, 0.98))),
            (0.7, st('attack_big', rot={'cape': 2.0}))]),
        # win_par: almost beaten: bent over, the weapon hanging low, panting
        rig.clip('win_par', 0.6, [
            (0.0, st('idle')),
            (0.14, st('idle', rot={'chest': 16.0, 'neck': 10.0, 'shoulder_near': 22.0, 'shoulder_far': 18.0, 'elbow_near': 10.0, 'weapon': 75.0, 'knee_far': 18.0, 'knee_near': 20.0, 'hip_far': -10.0, 'hip_near': -12.0, 'cape': -4.0},
                      root=(0.0, 14.0), squash=(1.03, 0.96))),
            (0.28, st('idle', rot={'chest': 12.0, 'neck': 6.0, 'shoulder_near': 20.0, 'shoulder_far': 16.0, 'elbow_near': 8.0, 'weapon': 72.0, 'knee_far': 14.0, 'knee_near': 16.0, 'hip_far': -8.0, 'hip_near': -9.0},
                      root=(0.0, 10.0))),
            (0.42, st('idle', rot={'chest': 18.0, 'neck': 12.0, 'shoulder_near': 23.0, 'shoulder_far': 19.0, 'elbow_near': 10.0, 'weapon': 78.0, 'knee_far': 19.0, 'knee_near': 21.0, 'hip_far': -11.0, 'hip_near': -12.0},
                      root=(0.0, 15.0), squash=(1.03, 0.96))),
            (0.6, st('idle', rot={'chest': 15.0, 'neck': 9.0, 'shoulder_near': 22.0, 'shoulder_far': 18.0, 'elbow_near': 9.0, 'weapon': 75.0, 'knee_far': 17.0, 'knee_near': 19.0, 'hip_far': -10.0, 'hip_near': -11.0},
                     root=(0.0, 13.0)))]),
        # lose: HP gone: the knees give, the body falls backward and stays down
        rig.clip('lose', 0.8, [
            (0.0, st('idle')),
            (0.08, st('idle', 'miss', rot={'chest': -6.0, 'neck': -8.0}, root=(-6.0, 0.0), f=0.5)),
            (0.09, st('miss', 'idle', rot={'chest': -6.0, 'neck': -8.0}, root=(-6.0, 0.0), f=0.5)),
            (0.2, st('miss', rot={'chest': 6.0, 'neck': 8.0, 'hip_far': -14.0, 'knee_far': 26.0, 'hip_near': -16.0, 'knee_near': 30.0, 'weapon': -30.0},
                     root=(-8.0, 16.0), squash=(1.03, 0.95))),
            (0.34, st('miss', rot={'hips': -22.0, 'chest': 4.0, 'neck': 6.0, 'hip_far': -24.0, 'knee_far': 40.0, 'hip_near': -26.0, 'knee_near': 44.0, 'weapon': -45.0, 'cape': 8.0},
                      root=(-16.0, 30.0))),
            (0.52, st('miss', rot={'hips': -70.0, 'neck': 10.0, 'hip_far': -12.0, 'knee_far': 22.0, 'hip_near': -8.0, 'knee_near': 14.0, 'shoulder_far': 25.0, 'shoulder_near': 30.0, 'weapon': -60.0, 'cape': 14.0},
                      root=(-30.0, 62.0))),
            (0.62, st('miss', rot={'hips': -84.0, 'neck': 6.0, 'hip_far': -6.0, 'knee_far': 12.0, 'hip_near': -4.0, 'knee_near': 8.0, 'shoulder_far': 30.0, 'shoulder_near': 36.0, 'weapon': -70.0, 'cape': 10.0},
                      root=(-34.0, 74.0))),
            (0.7, st('miss', rot={'hips': -80.0, 'neck': 8.0, 'hip_far': -7.0, 'knee_far': 14.0, 'hip_near': -5.0, 'knee_near': 10.0, 'shoulder_far': 28.0, 'shoulder_near': 34.0, 'weapon': -66.0, 'cape': 8.0},
                     root=(-35.0, 72.0))),
            (0.8, st('miss', rot={'hips': -84.0, 'neck': 6.0, 'hip_far': -6.0, 'knee_far': 12.0, 'hip_near': -4.0, 'knee_near': 8.0, 'shoulder_far': 30.0, 'shoulder_near': 36.0, 'weapon': -70.0, 'cape': 6.0},
                     root=(-36.0, 74.0)))]),
        # miss: the stumble -- toward the stumbling pose, its painting, a wobble, back to ready
        rig.clip('miss', 0.7, [
            (0.0, st('idle')),
            (0.12, st('idle', 'miss', f=0.5)),
            (0.14, st('miss', 'idle', f=0.5)),
            (0.2, st('miss')),
            (0.32, st('miss', rot={'chest': 3.0, 'neck': -4.0, 'cape': 6.0})),
            (0.5, st('miss', 'idle', f=0.4)),
            (0.52, st('idle', 'miss', f=0.6)),
            (0.7, st('idle'))]),
        # attack_big: crouch, leap into the painted power shot, release in the air, land
        rig.clip('attack_big', 0.8, [
            (0.0, st('idle')),
            (0.1, st('idle', rot={'hip_far': -14.0, 'knee_far': 24.0, 'hip_near': -18.0, 'knee_near': 28.0,
                                  'chest': 4.0, 'cape': -4.0}, root=(0.0, 14.0), squash=(1.03, 0.95))),
            (0.18, st('idle', 'attack_big', root=(2.0, -16.0), f=0.4)),
            (0.2, st('attack_big', 'idle', root=(4.0, -26.0), f=0.6)),
            (0.27, st('attack_big', root=(4.0, -30.0))),
            (0.4, st('attack_big', rot={'cape': 6.0}, root=(5.0, -36.0))),
            (0.46, st('attack_big', rot={'shoulder_far': -30.0, 'elbow_far': -35.0, 'cape': 8.0}, root=(5.0, -34.0))),
            (0.6, st('attack_big', 'idle', root=(2.0, -10.0), f=0.4)),
            (0.62, st('idle', 'attack_big', root=(2.0, -4.0), f=0.6)),
            (0.7, st('idle', rot={'hip_far': -8.0, 'knee_far': 14.0, 'hip_near': -10.0, 'knee_near': 16.0},
                     root=(0.0, 8.0), squash=(1.04, 0.95))),
            (0.8, st('idle'))]),
        # hurt: knocked back -- with a painting of it: toward it, its painting, back to ready; without: by bones
        rig.clip('hurt', 0.5, [
            (0.0, st('idle')),
            (0.05, st('idle', 'hurt', f=0.5)),
            (0.07, st('hurt', 'idle', f=0.5)),
            (0.12, st('hurt', root=(-8.0, 0.0))),
            (0.3, st('hurt', rot={'chest': -2.0, 'cape': 6.0}, root=(-6.0, 0.0))),
            (0.4, st('hurt', 'idle', f=0.4)),
            (0.42, st('idle', 'hurt', f=0.6)),
            (0.5, st('idle'))]) if 'hurt' in joints else
        rig.clip('hurt', 0.5, [   # (no painting of it looked hurt: the bow) the hit snaps the body back, knees buckle
            (0.0, st('idle')),
            (0.06, st('idle', rot={'chest': -12.0, 'neck': -10.0, 'shoulder_far': -25.0, 'elbow_far': 20.0,
                                   'shoulder_near': 10.0, 'elbow_near': 15.0, 'hip_far': -6.0, 'knee_far': 10.0,
                                   'hip_near': -8.0, 'knee_near': 14.0, 'cape': 12.0},
                      root=(-12.0, 4.0), squash=(1.03, 0.96))),
            (0.14, st('idle', rot={'chest': -8.0, 'neck': -6.0, 'shoulder_far': -18.0, 'shoulder_near': 7.0,
                                   'knee_far': 8.0, 'knee_near': 10.0, 'cape': 8.0}, root=(-8.0, 3.0))),
            (0.22, st('idle', rot={'chest': -10.0, 'neck': -8.0, 'shoulder_far': -20.0, 'shoulder_near': 8.0,
                                   'knee_far': 9.0, 'knee_near': 12.0, 'cape': 10.0}, root=(-10.0, 3.0))),
            (0.4, st('idle', rot={'chest': -3.0, 'neck': -2.0, 'cape': 4.0}, root=(-3.0, 0.0))),
            (0.5, st('idle'))]),
    ]
    anim = {'version': 2, 'atlases': [f'../atlas/{atlas}.atlas'], 'clips': clips}
    out = ANIM_DIR / f'{atlas}.anim'
    out.write_text(json.dumps(anim, indent=1) + '\n', encoding='utf-8')
    print(f'{ATLAS_DIR / (atlas + ".atlas")}: {len(sprites)} parts, {sheet.width} x {sheet.height}')
    print(f'{out}: {", ".join(c["name"] for c in clips)}; labels in {work / "parts"}')


if __name__ == '__main__':
    main()
