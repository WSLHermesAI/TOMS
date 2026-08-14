#!/usr/bin/env python3
"""Generate 2D sprite textures for Tower of the Sorcerer (Vulkan C++ game).
All sprites are 32x32 RGBA PNGs written to assets/sprites/. Also builds a
32x32 tile grid sheet (not strictly needed; we upload per-sprite). A simple
pixel-art style is drawn procedurally (no external downloads).
"""
import os, math, struct, zlib
from PIL import Image, ImageDraw, ImageFont

OUT = "/home/fatming/tower_vulkan/assets/sprites"
os.makedirs(OUT, exist_ok=True)
S = 32

def new():
    return Image.new("RGBA", (S, S), (0, 0, 0, 0))

def save(im, name):
    p = os.path.join(OUT, name)
    im.save(p)
    return p

# ---- tiny drawing helpers (pixel art) ----
def rect(d, x, y, w, h, c):
    d.rectangle([x, y, x+w-1, y+h-1], fill=c)

def px(d, x, y, c):
    d.point((x, y), fill=c)

# Palette
SKIN = (240, 200, 160, 255)
SKIN_D = (200, 150, 110, 255)
ROBE = (60, 90, 180, 255)
ROBE_D = (40, 60, 130, 255)
METAL = (180, 180, 200, 255)
METAL_D = (120, 120, 140, 255)
WOOD = (120, 80, 40, 255)
STONE = (110, 110, 120, 255)
STONE_D = (70, 70, 80, 255)
GOLD = (240, 200, 40, 255)
RED = (200, 40, 40, 255)
GREEN = (40, 180, 60, 255)
BLUE = (40, 120, 220, 255)
PURP = (150, 60, 200, 255)
DARK = (20, 20, 30, 255)
WHITE = (240, 240, 245, 255)
EYE = (20, 20, 20, 255)

def floor_tile():
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 0, 0, S, S, (60, 55, 70, 255))
    rect(d, 1, 1, S-2, S-2, (78, 72, 88, 255))
    # subtle checker
    for i in range(0, S, 8):
        for j in range(0, S, 8):
            if ((i + j) // 8) % 2 == 0:
                rect(d, i+1, j+1, 6, 6, (84, 78, 94, 255))
    return im

def wall_tile():
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 0, 0, S, S, STONE_D)
    rect(d, 1, 1, S-2, S-2, STONE)
    # brick lines
    rect(d, 0, 15, S, 1, STONE_D)
    rect(d, 15, 0, 1, 16, STONE_D)
    rect(d, 7, 16, 1, 16, STONE_D)
    rect(d, 23, 16, 1, 16, STONE_D)
    return im

def stairs(up=True):
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 0, 0, S, S, (40, 40, 50, 255))
    for i in range(6):
        y = 4 + i*4
        rect(d, 4, y, S-8 - i*2, 3, (120, 120, 140, 255) if up else (90, 90, 110, 255))
    return im

def door(color):
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 2, 1, S-4, S-2, (50, 30, 20, 255))
    rect(d, 3, 2, S-6, S-4, color)
    rect(d, 3, 2, S-6, S-4, color)
    # handle
    px(d, S-7, S//2, METAL)
    return im

def key_gem(color):
    im = new(); d = ImageDraw.Draw(im)
    # diamond
    pts = [(S//2, 4), (S-5, S//2), (S//2, S-4), (5, S//2)]
    d.polygon(pts, fill=color, outline=WHITE)
    return im

def potion(color):
    im = new(); d = ImageDraw.Draw(im)
    rect(d, S//2-3, 4, 6, 6, (220, 220, 220, 255))  # cork
    d.ellipse([S//2-7, 12, S//2+7, S-4], fill=color, outline=WHITE)
    rect(d, S//2-2, 16, 4, 8, WHITE)
    return im

def coin():
    im = new(); d = ImageDraw.Draw(im)
    d.ellipse([5, 8, S-5, S-8], fill=GOLD, outline=(180, 140, 20, 255))
    rect(d, S//2-1, 12, 2, S-20, (180, 140, 20, 255))
    return im

def warrior():
    im = new(); d = ImageDraw.Draw(im)
    # head
    rect(d, 11, 3, 10, 9, SKIN)
    rect(d, 11, 3, 10, 3, (90, 60, 40, 255))  # hair
    px(d, 13, 7, EYE); px(d, 18, 7, EYE)
    # body robe
    rect(d, 9, 12, 14, 14, ROBE)
    rect(d, 9, 12, 14, 3, ROBE_D)
    # arms
    rect(d, 6, 13, 3, 9, SKIN); rect(d, 23, 13, 3, 9, SKIN)
    # legs
    rect(d, 11, 26, 4, 5, (40, 40, 60, 255)); rect(d, 17, 26, 4, 5, (40, 40, 60, 255))
    # sword
    rect(d, 25, 8, 2, 16, METAL); rect(d, 23, 22, 6, 2, METAL_D)
    return im

def slime():
    im = new(); d = ImageDraw.Draw(im)
    d.ellipse([3, 10, S-3, S-2], fill=GREEN, outline=(20, 120, 40, 255))
    rect(d, 3, 10, S-6, 8, (80, 220, 100, 255))
    px(d, 11, 18, EYE); px(d, 20, 18, EYE)
    rect(d, 11, 22, 9, 2, (20, 80, 30, 255))
    return im

def bat():
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 14, 10, 4, 8, SKIN_D)  # body
    # wings
    d.polygon([(2, 8), (14, 12), (14, 18), (4, 20)], fill=(120, 80, 140, 255))
    d.polygon([(S-2, 8), (18, 12), (18, 18), (S-4, 20)], fill=(120, 80, 140, 255))
    px(d, 13, 13, EYE); px(d, 18, 13, EYE)
    return im

def golem():
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 6, 6, S-12, S-10, STONE)
    rect(d, 6, 6, S-12, 3, STONE_D)
    rect(d, 8, 10, S-16, 10, (90, 90, 100, 255))
    px(d, 12, 14, (240, 120, 40, 255)); px(d, 19, 14, (240, 120, 40, 255))  # glowing eyes
    rect(d, 10, 24, 5, 6, STONE_D); rect(d, 17, 24, 5, 6, STONE_D)
    return im

def skeleton():
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 12, 3, 8, 8, WHITE)  # skull
    px(d, 14, 6, EYE); px(d, 18, 6, EYE)
    rect(d, 13, 12, 6, 12, (220, 220, 220, 255))  # ribs
    for y in range(14, 24, 3):
        rect(d, 13, y, 6, 1, (160, 160, 160, 255))
    rect(d, 11, 24, 3, 6, WHITE); rect(d, 18, 24, 3, 6, WHITE)
    return im

def wraith():
    im = new(); d = ImageDraw.Draw(im)
    d.polygon([(S//2, 2), (4, 14), (10, S-2), (S-10, S-2), (S-4, 14)], fill=(80, 40, 120, 200))
    px(d, 13, 9, (200, 200, 255, 255)); px(d, 18, 9, (200, 200, 255, 255))
    return im

def demon():
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 10, 4, 12, 10, (180, 40, 50, 255))  # head
    # horns
    d.polygon([(10, 4), (6, 0), (12, 4)], fill=(220, 200, 180, 255))
    d.polygon([(22, 4), (26, 0), (20, 4)], fill=(220, 200, 180, 255))
    px(d, 13, 9, GOLD); px(d, 18, 9, GOLD)
    rect(d, 7, 14, S-14, 14, (150, 30, 40, 255))  # body
    # wings
    d.polygon([(2, 12), (10, 16), (8, 28)], fill=(90, 20, 30, 255))
    d.polygon([(S-2, 12), (S-10, 16), (S-8, 28)], fill=(90, 20, 30, 255))
    return im

def sorcerer_npc():
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 11, 3, 10, 9, SKIN)  # face
    px(d, 13, 7, EYE); px(d, 18, 7, EYE)
    # pointed hat
    d.polygon([(S//2, 0), (8, 12), (S-8, 12)], fill=PURP)
    rect(d, 9, 12, S-18, 16, PURP_D := (110, 40, 160, 255))  # robe
    rect(d, 14, 14, 4, 12, (200, 160, 240, 255))  # staff glow
    return im

def villager():
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 11, 3, 10, 9, SKIN)
    rect(d, 10, 2, 12, 4, (120, 80, 50, 255))  # hair
    px(d, 13, 7, EYE); px(d, 18, 7, EYE)
    rect(d, 9, 12, 14, 14, (160, 120, 60, 255))  # tunic
    rect(d, 11, 26, 4, 5, (60, 50, 40, 255)); rect(d, 17, 26, 4, 5, (60, 50, 40, 255))
    return im

def princess():
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 11, 3, 10, 9, SKIN)
    px(d, 13, 7, EYE); px(d, 18, 7, EYE)
    d.polygon([(S//2, 0), (8, 10), (S-8, 10)], fill=(230, 120, 200, 255))  # hair/crown area
    rect(d, 13, 1, 6, 3, GOLD)  # crown
    rect(d, 9, 12, 14, 16, (240, 160, 220, 255))  # dress
    return im

def king():
    im = new(); d = ImageDraw.Draw(im)
    rect(d, 11, 3, 10, 9, SKIN)
    rect(d, 10, 1, 12, 4, (200, 200, 220, 255))  # beard/hair
    px(d, 13, 7, EYE); px(d, 18, 7, EYE)
    rect(d, 9, 12, 14, 16, (60, 60, 140, 255))
    rect(d, 13, 0, 6, 3, GOLD)  # crown
    return im

# ---- build all ----
sprites = {
    "floor.png": floor_tile(),
    "wall.png": wall_tile(),
    "stairs_up.png": stairs(True),
    "stairs_down.png": stairs(False),
    "door_yellow.png": door(GOLD),
    "door_blue.png": door(BLUE),
    "door_red.png": door(RED),
    "gem_atk.png": key_gem(RED),
    "gem_def.png": key_gem(BLUE),
    "potion_red.png": potion(RED),
    "potion_blue.png": potion(BLUE),
    "coin.png": coin(),
    "key_yellow.png": key_gem(GOLD),
    "key_blue.png": key_gem(BLUE),
    "key_red.png": key_gem(RED),
    "player.png": warrior(),
    "slime.png": slime(),
    "bat.png": bat(),
    "golem.png": golem(),
    "skeleton.png": skeleton(),
    "wraith.png": wraith(),
    "demon.png": demon(),
    "boss_demonlord.png": demon(),  # boss is bigger version conceptually
    "npc_sorcerer.png": sorcerer_npc(),
    "npc_villager.png": villager(),
    "npc_princess.png": princess(),
    "npc_king.png": king(),
}
for name, im in sprites.items():
    save(im, name)

print("generated", len(sprites), "sprites in", OUT)

# Build a simple atlas info for the engine (names + a 1xN atlas optional).
# We keep per-file sprites; engine uploads each. Also write a manifest.
import json
manifest = {"tile": 32, "sprites": sorted(sprites.keys())}
with open(os.path.join(OUT, "manifest.json"), "w") as f:
    json.dump(manifest, f, indent=2)
print("manifest written")
