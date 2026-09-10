#!/usr/bin/env python3
"""Build a bitmap font atlas (PNG) + map.json for the Vulkan game's text renderer.
Covers ASCII + every CJK/char used in data/*.json and HUD labels. Each glyph is
CELL x CELL px in a grid; atlas map gives UV rects (0..1).
"""
import os, json, glob, re
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
os.makedirs(OUT, exist_ok=True)
CELL = 32
COLS = 32
FONT = "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"
fnt = ImageFont.truetype(FONT, 24)

# Collect characters
chars = set()
# Repo-relative (was a hardcoded absolute path into another checkout, which silently baked the
# WRONG project's glyphs into the shipped atlas -- including no glyphs at all for text that only
# exists here, e.g. the title phase's data/text.json strings).
ROOT = os.path.dirname(os.path.abspath(__file__))
for f in glob.glob(os.path.join(ROOT, "data/**/*.json"), recursive=True):
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
# HUD labels (ensure present) — add each CHARACTER individually
for s in ["HP", "ATK", "DEF", "LV", "EXP", "GOLD", "KEY", "▶", "（", "）", "：", "！", "？", "、", "。", "，", "「", "」", "『", "』", "—", "·", "+", "-", "/", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"]:
    for ch in s:
        chars.add(ch)
# Hardcoded HUD/UI strings in src/game.cpp (not present in data/*.json) — ensure their chars exist
HUD_STRINGS = [
    "魔法塔", "戰鬥", "你", "敵人", "鑰匙", "對話", "選擇", "繼續", "道具",
    "使用", "離開", "是", "否", "背包", "方向鍵", "丟棄", "關閉", "任意鍵",
    "勝利", "獲得", "倒下", "返回", "本層", "起點", "擊敗", "安穩", "之星",
    "重燃", "王國", "黎明", "公主", " rescued", "沃卡司", "封印",
    # Title phase (New Game / Continue / Settings) — data/text.json also carries these, but the
    # explicit list keeps the atlas correct even if that file is edited or missing.
    "新遊戲", "繼續遊戲", "設定", "存檔", "空", "語言", "繁體中文", "English",
    "遊玩時間", "已儲存進度", "沒有存檔時請先開始新遊戲", "關卡", "等級",
]
for s in HUD_STRINGS:
    for ch in s:
        if ch.isprintable():
            chars.add(ch)
# Hardcoded UI strings in the C++ sources are NOT in data/*.json, and a hand-maintained list goes
# stale the moment new text is added -- which showed up on the live page as blank gaps where glyphs
# were missing. Scan the sources instead: every printable non-ASCII character in a .cpp/.h gets a
# glyph, so any hardcoded label drawn with drawText() is covered (now and in future).
src_files = (glob.glob(os.path.join(ROOT, "src/**/*.cpp"), recursive=True) +
             glob.glob(os.path.join(ROOT, "src/**/*.h"), recursive=True))
for _path in src_files:
    with open(_path, encoding="utf-8", errors="replace") as _f:
        for _ch in _f.read():
            if ord(_ch) > 0x7F and _ch.isprintable():
                chars.add(_ch)
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
    # key as "U+XXXX" (hex codepoint) so the C++ loader can map it unambiguously
    charmap["U%04X" % ord(ch)] = {
        "u0": cx / atlas_w, "v0": cy / atlas_h,
        "u1": (cx + CELL) / atlas_w, "v1": (cy + CELL) / atlas_h,
    }

atlas.save(os.path.join(OUT, "font_atlas.png"))
with open(os.path.join(OUT, "font_atlas.json"), "w", encoding="utf-8") as f:
    json.dump({"cell": CELL, "cols": COLS, "rows": rows,
               "width": atlas_w, "height": atlas_h, "chars": charmap}, f, ensure_ascii=False, indent=2)
print("atlas:", atlas_w, "x", atlas_h, "glyphs:", n, "rows:", rows)
