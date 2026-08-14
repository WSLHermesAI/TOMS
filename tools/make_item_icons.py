#!/usr/bin/env python3
"""Generate two 32x32 RGBA item-icon sprites for Tower of the Sorcerer.

- exp_up.png  : green gem with a white "up arrow"  (EXP bonus item)
- scroll.png  : a magic scroll / teleport item   (jump to target floor)

Deterministic, dependency-free (stdlib only). Output goes to assets/sprites/.
Run:  python3 tools/make_item_icons.py
"""
import struct, zlib, os

W = H = 32

def px(x, y, r, g, b, a, buf):
    if 0 <= x < W and 0 <= y < H:
        i = (y * W + x) * 4
        buf[i] = r; buf[i+1] = g; buf[i+2] = b; buf[i+3] = a

def fill(buf, r, g, b, a=255):
    for y in range(H):
        for x in range(W):
            px(x, y, r, g, b, a, buf)

def rect(buf, x0, y0, x1, y1, r, g, b, a=255):
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            px(x, y, r, g, b, a, buf)

def circle(buf, cx, cy, rad, r, g, b, a=255):
    for y in range(H):
        for x in range(W):
            if (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad:
                px(x, y, r, g, b, a, buf)

def png_bytes(buf):
    def chunk(typ, data):
        c = typ + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        raw += bytes(buf[y*W*4:(y+1)*W*4])
    head = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 9)
    return head + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")

# ---------- EXP UP: green gem + up arrow ----------
exp = bytearray(W * H * 4)
fill(exp, 0, 0, 0, 0)
circle(exp, 16, 16, 12, 60, 200, 90, 255)          # green gem body
circle(exp, 13, 13, 4, 150, 255, 170, 255)         # highlight
# white up arrow
arrow = [(16,8),(12,16),(15,16),(15,22),(17,22),(17,16),(20,16)]
for (x, y) in arrow:
    rect(exp, x, y, x+1, y+1, 255, 255, 255, 255)

# ---------- SCROLL: parchment roll + ribbon ----------
scr = bytearray(W * H * 4)
fill(scr, 0, 0, 0, 0)
rect(scr, 7, 9, 25, 23, 222, 198, 150, 255)        # parchment
rect(scr, 7, 9, 25, 11, 170, 140, 95, 255)          # top roller
rect(scr, 7, 21, 25, 23, 170, 140, 95, 255)         # bottom roller
# magic glyph (star-ish) in the middle
for (x, y) in [(16,13),(14,15),(18,15),(15,18),(17,18),(16,16)]:
    rect(scr, x, y, x+1, y+1, 120, 70, 200, 255)
rect(scr, 16, 12, 17, 20, 120, 70, 200, 255)        # vertical spark
rect(scr, 13, 16, 19, 17, 120, 70, 200, 255)        # horizontal spark

out_dir = os.path.join(os.path.dirname(__file__), "..", "assets", "sprites")
os.makedirs(out_dir, exist_ok=True)
with open(os.path.join(out_dir, "exp_up.png"), "wb") as f:
    f.write(png_bytes(exp))
with open(os.path.join(out_dir, "scroll.png"), "wb") as f:
    f.write(png_bytes(scr))
print("wrote", os.path.join(out_dir, "exp_up.png"), "and scroll.png")
