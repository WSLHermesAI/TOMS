#!/usr/bin/env python3
"""make_hero_rig.py -- option C, step 2: the 3D hero with a skeleton and clips (docs/engine/20_CHARACTER_ART.md §4).

    python tools/art/make_hero_rig.py [--model Build/art_out/hero_3d/hero_1.glb] [--tris 20000]

1. The joints: the concept was painted on an OpenPose skeleton (make_brawler_art.JOINTS, picture pixels), and the mesh
   was made from the concept, so each joint's place on the mesh is known: x and y from the picture (the same mapping
   as make_hero_3d.front_samples), the depth from the mesh (the middle of the nearest surface's slab there). A chain
   for the cape follows the cape's back-most points. Written to Build/art_out/hero_3d/joints.json.
2. Blender (headless, tools/art/hero_rig_blender.py) cuts the mesh down to --tris triangles, builds the skeleton,
   skins the mesh to it (automatic weights), adds a bow in the left hand and its string, makes the clips (idle, draw)
   and exports Build/art_out/hero_3d/hero_rigged.glb, for gltf_viewer (--anim=draw).

Needs Blender (docs/engine/20_CHARACTER_ART.md §4: winget install BlenderFoundation.Blender).
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_brawler_art as base   # noqa: E402  (JOINTS: the concept's skeleton)
from make_hero_3d import read_glb   # noqa: E402

ROOT = base.ROOT


def find_blender():
    """blender.exe: TOMS_BLENDER, else the newest under Program Files\\Blender Foundation."""
    if os.environ.get('TOMS_BLENDER'):
        return os.environ['TOMS_BLENDER']
    found = sorted(Path(os.environ.get('ProgramFiles', r'C:\Program Files'), 'Blender Foundation').glob('Blender */blender.exe'))
    if not found:
        sys.exit('Blender is not installed: winget install BlenderFoundation.Blender (docs/engine/20_CHARACTER_ART.md §4)')
    return str(found[-1])


def joints_on_mesh(pos, cut):
    """The concept's joints in the mesh's space (glTF: y up, the front toward +z)."""
    a = np.asarray(cut.getchannel('A')) > 16
    ys, xs = np.nonzero(a)
    fx0, fx1, fy0, fy1 = xs.min(), xs.max(), ys.min(), ys.max()
    lo, hi = pos.min(0), pos.max(0)
    s = (fy1 - fy0) / (hi[1] - lo[1])                    # picture pixels per model unit
    cx = (lo[0] + hi[0]) / 2

    def place(px, py, slab):
        x, y = (px - (fx0 + fx1) / 2) / s + cx, hi[1] - (py - fy0) / s
        near = pos[np.hypot(pos[:, 0] - x, pos[:, 1] - y) < 0.03]
        if len(near) == 0:
            return [x, y, 0.0]
        front = near[:, 2].max()
        z = near[near[:, 2] > front - slab][:, 2]        # the nearest slab: the limb, not the cape behind it
        return [float(x), float(y), float((front + z.min()) / 2)]

    J = base.JOINTS
    j = {}
    for name in ('neck', 'nose', 'r_hip', 'l_hip'):
        j[name] = place(*J[name], 0.30)
    for name in ('r_shoulder', 'l_shoulder', 'r_elbow', 'l_elbow', 'r_wrist', 'l_wrist', 'r_knee', 'l_knee', 'r_ankle',
                 'l_ankle'):
        j[name] = place(*J[name], 0.12)
    j['head_top'] = [j['nose'][0], float(hi[1]), j['neck'][2]]
    j['ground'] = float(lo[1])
    # the cape: its back-most points from the shoulders down to its hem, in 4 steps
    chest_z = (j['neck'][2] + (j['r_hip'][2] + j['l_hip'][2]) / 2) / 2
    cape = pos[pos[:, 2] < chest_z - 0.12]
    top, bottom = j['neck'][1] - 0.05, float(cape[:, 1].min()) + 0.05
    chain = []
    for t in np.linspace(0, 1, 4):
        y = top + (bottom - top) * t
        band = cape[np.abs(cape[:, 1] - y) < 0.04]
        if len(band) == 0:
            band = cape[np.argsort(np.abs(cape[:, 1] - y))[:50]]
        chain.append([float(band[:, 0].mean()), float(y), float(band[:, 2].mean())])
    j['cape'] = chain
    return j


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--model', default=str(ROOT / 'Build' / 'art_out' / 'hero_3d' / 'hero_1.glb'))
    ap.add_argument('--ref', default=str(ROOT / 'Build' / 'art_out' / 'hero_m' / 'concept_606_cut.png'))
    ap.add_argument('--tris', type=int, default=20000)
    ap.add_argument('--out', default=str(ROOT / 'Build' / 'art_out' / 'hero_3d' / 'hero_rigged.glb'))
    a = ap.parse_args()
    pos, _ = read_glb(Path(a.model).read_bytes())
    joints = joints_on_mesh(pos, Image.open(a.ref).convert('RGBA'))
    jfile = Path(a.out).with_name('joints.json')
    jfile.write_text(json.dumps(joints, indent=1), encoding='utf-8')
    print(jfile)
    cmd = [find_blender(), '-b', '--factory-startup', '-P', str(Path(__file__).with_name('hero_rig_blender.py')), '--',
           '--model', a.model, '--joints', str(jfile), '--tris', str(a.tris), '--out', a.out]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    for line in (r.stdout + r.stderr).splitlines():
        if line.startswith('[rig]') or 'Error' in line or 'Traceback' in line or 'failed' in line.lower():
            print(line)
    if r.returncode != 0 or not Path(a.out).exists():
        print((r.stdout + r.stderr)[-3000:])
        sys.exit(f'Blender failed ({r.returncode})')


if __name__ == '__main__':
    main()
