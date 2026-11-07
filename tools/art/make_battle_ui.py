#!/usr/bin/env python3
"""make_battle_ui.py -- draws the defence UI's pictures (docs/gameplay/19 §2.1), from code, no AI:

    assets/media/ui/battle/cell_normal.png, cell_danger.png   a cell's glowing frame, cyan or red
    assets/media/ui/battle/cell_here.png                      the gold marker on the hero's cell
    assets/media/ui/battle/btn_guard.png, btn_attack.png      the thumbs' frames (the inside stays clear: the state's colour shows)
    assets/media/ui/battle/icon_guard.png, icon_attack.png    their icons

    python tools/art/make_battle_ui.py
"""
import math
import random
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parents[2] / 'assets' / 'media' / 'ui' / 'battle'
SS = 2   # drawn larger, then scaled down: smooth edges


def over(a, b):
    return Image.alpha_composite(a, b)


def glow_line(size, box, radius, width, colour, blur, strength=1.0):
    """A rounded outline blurred into a glow."""
    layer = Image.new('RGBA', size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).rounded_rectangle(box, radius, outline=colour + (255,), width=width)
    layer = layer.filter(ImageFilter.GaussianBlur(blur))
    if strength != 1.0:
        layer.putalpha(layer.getchannel('A').point(lambda v: min(255, int(v * strength))))
    return layer


def cell(name, edge, core, inside, grid):
    w, h = 300 * SS, 130 * SS
    pad = 14 * SS
    box = (pad, pad, w - pad, h - pad)
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    fill = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(fill).rounded_rectangle(box, 12 * SS, fill=inside)
    mask = fill.getchannel('A')
    # a faint pattern inside, as on an old stone or a screen
    rng = random.Random(7)
    pattern = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(pattern)
    for _ in range(90):
        x, y = rng.randint(pad, w - pad), rng.randint(pad, h - pad)
        d.line((x, y, x + rng.randint(-40, 40) * SS // 2, y + rng.randint(-12, 12) * SS // 2), fill=grid + (rng.randint(20, 70),), width=SS)
    pattern.putalpha(ImageChops.multiply(pattern.getchannel('A'), mask))
    img = over(over(img, fill), pattern)
    img = over(img, glow_line((w, h), box, 12 * SS, 14 * SS, edge, 8 * SS, 2.0))
    img = over(img, glow_line((w, h), box, 12 * SS, 9 * SS, edge, 1.5 * SS, 1.3))
    img = over(img, glow_line((w, h), box, 12 * SS, 5 * SS, core, 0.5 * SS))
    img.resize((w // SS, h // SS), Image.LANCZOS).save(OUT / name)


def marker():
    s = 160 * SS
    c = s / 2
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    halo = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(halo).ellipse((c - 44 * SS, c - 44 * SS, c + 44 * SS, c + 44 * SS), fill=(255, 150, 30, 190))
    img = over(img, halo.filter(ImageFilter.GaussianBlur(17 * SS)))
    rng = random.Random(11)
    rays = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(rays)
    for i in range(14):
        a = i / 14 * math.tau + rng.uniform(-0.12, 0.12)
        r0, r1 = 24 * SS, rng.uniform(46, 72) * SS
        d.line((c + math.cos(a) * r0, c + math.sin(a) * r0, c + math.cos(a) * r1, c + math.sin(a) * r1), fill=(255, 200, 80, 220), width=2 * SS)
    img = over(img, rays.filter(ImageFilter.GaussianBlur(1.2 * SS)))
    ring = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(ring)
    d.ellipse((c - 30 * SS, c - 30 * SS, c + 30 * SS, c + 30 * SS), outline=(255, 215, 110, 255), width=4 * SS)
    d.ellipse((c - 17 * SS, c - 17 * SS, c + 17 * SS, c + 17 * SS), fill=(255, 226, 122, 255))
    d.ellipse((c - 9 * SS, c - 9 * SS, c + 9 * SS, c + 9 * SS), fill=(255, 250, 225, 255))
    img = over(img, ring)
    img.resize((s // SS, s // SS), Image.LANCZOS).save(OUT / 'cell_here.png')


def button(name, edge, core):
    w, h = 380 * SS, 240 * SS
    pad = 8 * SS
    box = (pad, pad, w - pad, h - pad)
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    img = over(img, glow_line((w, h), box, 22 * SS, 9 * SS, edge, 5 * SS, 1.3))
    img = over(img, glow_line((w, h), box, 22 * SS, 7 * SS, (46, 50, 60), 0.5 * SS))     # a metal rim
    img = over(img, glow_line((w, h), box, 22 * SS, 2 * SS, core, 0.5 * SS))
    inner = (pad + 11 * SS, pad + 11 * SS, w - pad - 11 * SS, h - pad - 11 * SS)
    img = over(img, glow_line((w, h), inner, 14 * SS, 2 * SS, edge, 0.8 * SS))
    d = ImageDraw.Draw(img)   # small corner studs
    for x, y in ((pad + 13 * SS, pad + 13 * SS), (w - pad - 13 * SS, pad + 13 * SS), (pad + 13 * SS, h - pad - 13 * SS), (w - pad - 13 * SS, h - pad - 13 * SS)):
        d.ellipse((x - 5 * SS, y - 5 * SS, x + 5 * SS, y + 5 * SS), fill=core + (255,))
    img.resize((w // SS, h // SS), Image.LANCZOS).save(OUT / name)


def icon_guard():
    s = 128 * SS
    c = s / 2
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    halo = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(halo).ellipse((c - 46 * SS, c - 46 * SS, c + 46 * SS, c + 46 * SS), fill=(60, 150, 255, 150))
    img = over(img, halo.filter(ImageFilter.GaussianBlur(12 * SS)))
    d = ImageDraw.Draw(img)
    d.ellipse((c - 46 * SS, c - 46 * SS, c + 46 * SS, c + 46 * SS), outline=(150, 210, 255, 255), width=3 * SS)
    bar = [(c - 12 * SS, c - 40 * SS), (c + 12 * SS, c - 40 * SS), (c + 6 * SS, c + 14 * SS), (c - 6 * SS, c + 14 * SS)]
    d.polygon(bar, fill=(255, 216, 80, 255))
    d.ellipse((c - 9 * SS, c + 22 * SS, c + 9 * SS, c + 40 * SS), fill=(255, 216, 80, 255))
    img.resize((s // SS, s // SS), Image.LANCZOS).save(OUT / 'icon_guard.png')


def icon_attack():
    s = 128 * SS
    c = s / 2
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    halo = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(halo).ellipse((c - 44 * SS, c - 44 * SS, c + 44 * SS, c + 44 * SS), fill=(255, 110, 20, 200))
    img = over(img, halo.filter(ImageFilter.GaussianBlur(12 * SS)))
    d = ImageDraw.Draw(img)
    points = []
    for i in range(20):   # a burst: long and short points
        a = i / 20 * math.tau - math.pi / 2
        r = (54 if i % 2 == 0 else 22) * SS
        points.append((c + math.cos(a) * r, c + math.sin(a) * r))
    d.polygon(points, fill=(255, 150, 40, 255))
    inner = [(c + (x - c) * 0.55, c + (y - c) * 0.55) for x, y in points]
    d.polygon(inner, fill=(255, 235, 150, 255))
    img.resize((s // SS, s // SS), Image.LANCZOS).save(OUT / 'icon_attack.png')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cell('cell_normal.png', (40, 200, 240), (170, 240, 255), (8, 44, 62, 190), (70, 190, 230))
    cell('cell_danger.png', (255, 50, 40), (255, 190, 170), (130, 16, 14, 210), (255, 110, 80))
    marker()
    button('btn_guard.png', (60, 150, 255), (170, 215, 255))
    button('btn_attack.png', (255, 120, 40), (255, 205, 130))
    icon_guard()
    icon_attack()
    print('written:', ', '.join(sorted(p.name for p in OUT.glob('*.png'))))


if __name__ == '__main__':
    main()
