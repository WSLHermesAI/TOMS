#!/usr/bin/env python3
"""frames_to_gif.py -- merge recorded frames into one GIF.

    python tools/frames_to_gif.py <folder> <out.gif> [--fps 30] [--crop x,y,w,h] [--width 480] [--keep]

The frames come from one run of the game that records into a folder (no window on the desktop):

    toms_game.exe --scenario=tests/scenarios/battle_bow --scenario-test --fixed-dt=33 --frames=90 --record=<folder>

<folder>/frame_*.png in order become the GIF at --fps (the frame time of the run: --fixed-dt=33 is about 30 fps; with
--record-step=2, half that). --crop cuts a part of each frame (picture pixels), --width scales it down. The frames are
deleted afterwards unless --keep.
"""
import argparse
import shutil
from pathlib import Path

from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folder')
    ap.add_argument('out')
    ap.add_argument('--fps', type=float, default=30.0)
    ap.add_argument('--crop', default='', help='x,y,w,h in picture pixels')
    ap.add_argument('--width', type=int, default=0, help='scale each frame to this width')
    ap.add_argument('--keep', action='store_true', help='keep the frames')
    a = ap.parse_args()
    files = sorted(Path(a.folder).glob('frame_*.png'))
    if not files:
        raise SystemExit(f'no frame_*.png in {a.folder}')
    frames = []
    for f in files:
        im = Image.open(f).convert('RGB')
        if a.crop:
            x, y, w, h = (int(v) for v in a.crop.split(','))
            im = im.crop((x, y, x + w, y + h))
        if a.width and im.width != a.width:
            im = im.resize((a.width, round(im.height * a.width / im.width)), Image.LANCZOS)
        frames.append(im.quantize(colors=192, method=Image.Quantize.MEDIANCUT))
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(a.out, save_all=True, append_images=frames[1:], duration=round(1000 / a.fps), loop=0, optimize=True)
    if not a.keep:
        shutil.rmtree(a.folder, ignore_errors=True)
    print(f'{a.out}: {len(frames)} frames at {a.fps:g} fps')


if __name__ == '__main__':
    main()
