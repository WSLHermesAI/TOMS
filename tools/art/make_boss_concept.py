#!/usr/bin/env python3
"""make_boss_concept.py -- a painted picture for a boss that has none yet, in the style of make_monster_concepts.py.

    python tools/art/make_boss_concept.py vorkath                 # paints seeds 11, 22, 33: Build/art_out/monster_concepts/vorkath_<seed>.png
    python tools/art/make_boss_concept.py vorkath --pick 22       # cuts that one out: .../cut/vorkath.png (make_monster_rig.py reads it)

Vorkath, the demon lord of floor 10, was a 32 px pixel picture; the owner (2026-11-13) asked for painted art like the
other bosses. The look follows the pixel one: a crimson hooded figure with glowing yellow eyes. Needs the ComfyUI
server (AIGamestyle/_pipeline/tools/start_comfyui_with_flags.bat) at 127.0.0.1:8188.
"""
import argparse
import sys
import time
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_monster_concepts as mc   # noqa: E402

BOSSES = {
    'vorkath': ('Vorkath', 'a stout imposing chibi demon lord sorcerer in a deep crimson hooded robe with a high collar, two '
                           'glowing yellow eyes shining in the shadow of the hood, small curved black horns through the hood, '
                           'a golden crown band and golden trim, dark purple magic swirling between its clawed hands, '
                           'maroon, crimson and black colors, menacing but cute'),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('boss', choices=sorted(BOSSES))
    ap.add_argument('--seeds', default='11,22,33')
    ap.add_argument('--pick', type=int, default=0, help='the seed to cut out (after looking at the paintings)')
    a = ap.parse_args()
    name, prompt = BOSSES[a.boss]
    mc.WORK.mkdir(parents=True, exist_ok=True)
    if a.pick:
        src = Image.open(mc.WORK / f'{a.boss}_{a.pick}.png').convert('RGB')
        (mc.WORK / 'cut').mkdir(exist_ok=True)
        out = mc.WORK / 'cut' / f'{a.boss}.png'
        mc.run(mc.cut_workflow(mc.upload(src, f'concept_{a.boss}.png'), 'monster_cut_' + a.boss))[0].save(out)
        print(out)
        return
    for seed in (int(s) for s in a.seeds.split(',')):
        t0 = time.time()
        mc.run(mc.workflow(prompt, seed, 'monster_' + a.boss))[0].convert('RGB').save(mc.WORK / f'{a.boss}_{seed}.png')
        print(f'{a.boss} {name} seed {seed}: {time.time() - t0:.0f} s', flush=True)


if __name__ == '__main__':
    main()
