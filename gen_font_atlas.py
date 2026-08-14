#!/usr/bin/env python3
"""Build a bitmap font atlas (PNG) + map.json for the Vulkan game's text renderer.
Covers ASCII + every CJK/char used in data/*.json and HUD labels. Each glyph is
CELL x CELL px in a grid; atlas map gives UV rects (0..1).
"""
import os, json, glob, re
from PIL import Image, ImageDraw, ImageFont

OUT = "/home/fatming/tower_vulkan/assets"
os.makedirs(OUT, exist_ok=True)
CELL = 32
COLS = 32
FONT = "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"
fnt = ImageFont.truetype(FONT, 24)

# Collect characters
chars = set()
for f in glob.glob("/home/fatming/tower_vulkan/data/**/*.json", recursive=True):
    try:
        s = open(f, encoding="utf-8").read()
    except Exception:
        continue
    for ch in s:
        if ch in " \t\n\r{}[]\",:.":  # skip pure punctuation we don't need as glyphs? keep all
            pass
        # include everything printable
        if ch.isprintable() and ch not in "﻿":
            chars.add(ch)
# HUD labels (ensure present)
for s in ["HP", "ATK", "DEF", "LV", "EXP", "GOLD", "KEY", "▶", "（", "）", "：", "！", "？", "、", "。", "，", "「", "」", "『", "』", "—", "·", "+", "-", "/", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"]:
    chars.add(s)
chars = sorted(chars)
n = len(chars)
rows = (n + COLS - 1) // COLS

atlas_w = COLS * CELL
atlas_h = rows * CELL
atlas = Image.new("RGBA", (atlas_w, atlas_h), (0, 0, 0, 0))
d = ImageDraw.Draw(atlas)
charmap = {}
for i, ch in enumerate(chars):
    cx = (i % COLS) * CELL
    cy = (i // COLS) * CELL
    # draw centered
    bb = d.textbbox((0, 0), ch, font=fnt)
    tw, th = bb[2]-bb[0], bb[3]-bb[1]
    x = cx + (CELL - tw) // 2 - bb[0]
    y = cy + (CELL - th) // 2 - bb[1]
    d.text((x, y), ch, font=fnt, fill=(255, 255, 255, 255))
    charmap[ch] = {
        "u0": cx / atlas_w, "v0": cy / atlas_h,
        "u1": (cx + CELL) / atlas_w, "v1": (cy + CELL) / atlas_h,
    }

atlas.save(os.path.join(OUT, "font_atlas.png"))
with open(os.path.join(OUT, "font_atlas.json"), "w", encoding="utf-8") as f:
    json.dump({"cell": CELL, "cols": COLS, "rows": rows,
               "width": atlas_w, "height": atlas_h, "chars": charmap}, f, ensure_ascii=False, indent=2)
print("atlas:", atlas_w, "x", atlas_h, "glyphs:", n, "rows:", rows)
