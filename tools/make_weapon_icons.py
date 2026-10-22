#!/usr/bin/env python3
"""make_weapon_icons.py -- draws the sword and staff item icons (docs/30 phase 4): 32 x 32 pixel art in the game's style
(flat colours, a near-black outline), into art_raw/sprites/item_sword.png and item_staff.png.

    python tools/make_weapon_icons.py

Then bring them into the atlas (docs/14_ATLAS_TOOL.md, docs/27_AI_SPRITES.md):
    Build\\windows-release\\bin\\atlaspack.exe refs art_raw\\atlas\\game.atlasproj --import new
    Build\\windows-release\\bin\\atlaspack.exe build art_raw\\atlas\\game.atlasproj --all-variants
    python tools/refresh_meshes.py
"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'art_raw', 'sprites')

OUTLINE = (20, 18, 24, 255)


def outlined(px):
    """Adds a 1-pixel outline around every opaque pixel (4-neighbours)."""
    img = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
    for (x, y), c in px.items():
        img.putpixel((x, y), c)
    for (x, y) in list(px):
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < 32 and 0 <= ny < 32 and (nx, ny) not in px:
                img.putpixel((nx, ny), OUTLINE)
    return img


def line(px, x0, y0, x1, y1, color, width=1):
    """A diagonal / straight run of pixels from (x0, y0) to (x1, y1), `width` pixels across (perpendicular-ish)."""
    n = max(abs(x1 - x0), abs(y1 - y0))
    for i in range(n + 1):
        x = round(x0 + (x1 - x0) * i / max(1, n))
        y = round(y0 + (y1 - y0) * i / max(1, n))
        for w in range(width):
            px[(x + w, y)] = color


def sword():
    px = {}
    steel, edge, shine = (196, 206, 222, 255), (140, 152, 172, 255), (245, 248, 255, 255)
    gold, gold_dk = (232, 188, 72, 255), (176, 128, 40, 255)
    grip, pommel = (110, 66, 36, 255), (210, 60, 60, 255)
    # the blade: from the guard (lower left) up to the tip (upper right), 2 px wide with a bright edge
    line(px, 11, 20, 25, 6, edge, 2)
    line(px, 12, 19, 25, 6, steel, 1)
    line(px, 14, 16, 22, 8, shine, 1)
    px[(26, 5)] = steel
    # the cross guard, across the blade
    line(px, 7, 18, 13, 24, gold, 2)
    px[(9, 20)] = gold_dk
    # the grip and the pommel
    line(px, 6, 25, 9, 22, grip, 2)
    px[(4, 27)] = pommel
    px[(5, 27)] = pommel
    px[(4, 26)] = pommel
    return outlined(px)


def staff():
    px = {}
    wood, wood_dk = (150, 98, 54, 255), (104, 64, 34, 255)
    orb, orb_lt, orb_dk = (96, 170, 255, 255), (200, 236, 255, 255), (52, 104, 200, 255)
    prong = (220, 196, 120, 255)
    # the shaft: lower left to upper right
    line(px, 5, 28, 20, 13, wood_dk, 2)
    line(px, 6, 27, 20, 13, wood, 1)
    # the prongs holding the orb
    line(px, 19, 12, 18, 8, prong, 1)
    line(px, 21, 14, 25, 13, prong, 1)
    # the orb: a disc with a highlight
    cx, cy, r = 23, 8, 4
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            d = (x - cx) ** 2 + (y - cy) ** 2
            if d <= r * r:
                px[(x, y)] = orb_dk if d > (r - 1) ** 2 else orb
    px[(22, 6)] = orb_lt
    px[(21, 7)] = orb_lt
    px[(22, 7)] = orb_lt
    return outlined(px)


def main():
    for name, img in (('item_sword', sword()), ('item_staff', staff())):
        path = os.path.join(OUT, name + '.png')
        img.save(path)
        print(os.path.relpath(path, ROOT))


if __name__ == '__main__':
    main()
