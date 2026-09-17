#!/usr/bin/env python3
"""make_sprites.py -- regenerates every sprite the game actually loads (see SPRITE_ORDER in
src/game/core/game_helpers.h) with a shared "sprite kit": a 1px dark outline around each
silhouette (readability against the floor/wall tiles, which sit in a similar dark tonal range)
and a simple top-lit shade pass (a highlight band and a shadow band within the shape) instead of
the flat single-tone fills every sprite had before. Also fixes a real content bug found while
looking at the sheet: `boss_demonlord.png` was a byte-for-byte copy of `demon.png` -- the final
boss looked exactly like a regular trash mob. It now has its own larger, spikier, darker-red
silhouette with a horn crown.

This does NOT touch the pipeline (renderer/atlas loading, sprite ids, tile size) -- only what's
INSIDE each already-shipped 32x32 PNG. It replaces tools/make_item_icons.py and
tools/make_missing_sprites.py's outputs too (exp_up/scroll/npc_handmaiden), so those two scripts
are now superseded by this one; kept only as historical reference.

Requires Pillow (not stdlib, unlike the two scripts this supersedes -- there is no other Python
dependency anywhere else in this repo's tooling, so this is a new, deliberate exception: hand-
plotting every pixel for 30 sprites' worth of outline/shading logic in raw PNG bytes is exactly
the kind of thing a real drawing library exists for).

Run from the repo root: python3 tools/make_sprites.py
"""
import os
from PIL import Image, ImageDraw

W = H = 32
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "sprites")

OUTLINE = (18, 16, 22, 255)


def canvas():
    return Image.new("RGBA", (W, H), (0, 0, 0, 0))


def outline(img):
    """Stamps a 1px dark border on every opaque pixel adjacent to a transparent one -- a cheap,
    uniform readability pass so every silhouette reads clearly against the floor/wall tiles
    (which sit in a similar dark tonal range as several monsters)."""
    px = img.load()
    edges = []
    for y in range(H):
        for x in range(W):
            if px[x, y][3] > 0:
                continue
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if 0 <= nx < W and 0 <= ny < H and px[nx, ny][3] > 0:
                    edges.append((x, y))
                    break
    for x, y in edges:
        px[x, y] = OUTLINE
    return img


def shade(img, light=(255, 255, 255, 70), shadow=(0, 0, 0, 70), split=0.42):
    """A simple top-lit volume pass: brightens the top `split` fraction of each opaque shape's
    own bounding box and darkens the bottom, using the shape's own alpha as a mask so the effect
    never bleeds past the silhouette or over the outline."""
    px = img.load()
    ys = [y for y in range(H) for x in range(W) if px[x, y][3] > 0 and px[x, y] != OUTLINE]
    if not ys:
        return img
    top, bottom = min(ys), max(ys)
    band = top + (bottom - top) * split
    overlay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    opx = overlay.load()
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            if a == 0 or (r, g, b, a) == OUTLINE:
                continue
            opx[x, y] = light if y < band else shadow
    return Image.alpha_composite(img, overlay)


def finish(img):
    return outline(shade(img))


def save(img, name):
    img.save(os.path.join(OUT_DIR, name + ".png"))


# ============================================================ monsters ====

def make_slime():
    img = canvas(); d = ImageDraw.Draw(img)
    d.ellipse([6, 14, 26, 28], fill=(40, 180, 60, 255))
    d.ellipse([9, 10, 23, 20], fill=(60, 200, 80, 255))
    d.ellipse([11, 17, 15, 21], fill=(15, 20, 15, 255))
    d.ellipse([18, 17, 22, 21], fill=(15, 20, 15, 255))
    d.arc([12, 19, 21, 25], start=20, end=160, fill=(15, 20, 15, 255), width=1)
    return finish(img)


def make_bat():
    img = canvas(); d = ImageDraw.Draw(img)
    body = (60, 40, 75, 255)
    wing = (95, 65, 120, 255)
    d.polygon([(16, 10), (1, 6), (9, 15), (2, 20), (13, 19), (16, 26)], fill=wing)
    d.polygon([(16, 10), (31, 6), (23, 15), (30, 20), (19, 19), (16, 26)], fill=wing)
    d.ellipse([12, 8, 20, 18], fill=body)
    d.polygon([(13, 9), (14, 4), (16, 9)], fill=body)
    d.polygon([(19, 9), (18, 4), (16, 9)], fill=body)
    d.ellipse([13, 11, 15, 13], fill=(230, 60, 70, 255))
    d.ellipse([17, 11, 19, 13], fill=(230, 60, 70, 255))
    return finish(img)


def make_golem():
    img = canvas(); d = ImageDraw.Draw(img)
    stone = (100, 100, 112, 255)
    dark = (70, 70, 82, 255)
    d.rectangle([8, 6, 23, 13], fill=stone)          # head block
    d.rectangle([5, 14, 26, 26], fill=dark)          # torso block
    d.rectangle([3, 16, 6, 24], fill=stone)          # left arm
    d.rectangle([25, 16, 28, 24], fill=stone)        # right arm
    d.rectangle([9, 26, 14, 30], fill=stone)         # left leg
    d.rectangle([17, 26, 22, 30], fill=stone)        # right leg
    d.rectangle([11, 9, 14, 12], fill=(240, 150, 40, 255))  # left eye, glowing
    d.rectangle([17, 9, 20, 12], fill=(240, 150, 40, 255))  # right eye, glowing
    d.line([8, 17, 23, 17], fill=(50, 50, 60, 255), width=1)
    return finish(img)


def make_skeleton():
    img = canvas(); d = ImageDraw.Draw(img)
    bone = (232, 230, 220, 255)
    d.ellipse([11, 3, 21, 13], fill=bone)             # skull
    d.rectangle([13, 7, 15, 9], fill=(20, 20, 20, 255))   # eye sockets
    d.rectangle([17, 7, 19, 9], fill=(20, 20, 20, 255))
    d.line([14, 11, 18, 11], fill=(20, 20, 20, 255), width=1)  # jaw
    d.rectangle([15, 13, 17, 20], fill=bone)          # spine
    for ry in (15, 17, 19):
        d.line([10, ry, 22, ry], fill=bone, width=2)   # ribs
    d.line([9, 21, 15, 26], fill=bone, width=2)        # left leg
    d.line([23, 21, 17, 26], fill=bone, width=2)       # right leg
    d.line([9, 15, 4, 22], fill=bone, width=2)         # left arm
    d.line([23, 15, 28, 22], fill=bone, width=2)       # right arm
    return finish(img)


def make_wraith():
    img = canvas(); d = ImageDraw.Draw(img)
    veil = (70, 45, 110, 210)
    d.ellipse([8, 4, 24, 20], fill=veil)
    d.polygon([(6, 18), (26, 18), (24, 30), (20, 24), (16, 30), (12, 24), (8, 30)], fill=veil)
    d.ellipse([12, 9, 15, 13], fill=(210, 200, 255, 255))
    d.ellipse([17, 9, 20, 13], fill=(210, 200, 255, 255))
    d.ellipse([13, 10, 14, 12], fill=(40, 20, 60, 255))
    d.ellipse([18, 10, 19, 12], fill=(40, 20, 60, 255))
    return finish(img)


def _horned_demon(scale=1.0, base=(150, 25, 35, 255), dark=(95, 15, 25, 255), horn=(230, 210, 180, 255)):
    img = canvas(); d = ImageDraw.Draw(img)
    cx = 16
    r = 10 * scale
    top = 16 - r * 0.9
    bot = 16 + r * 1.5
    d.polygon([(cx - r, top + r * 0.6), (cx - r * 0.3, top - r * 0.9), (cx, top + r * 0.3)], fill=horn)
    d.polygon([(cx + r, top + r * 0.6), (cx + r * 0.3, top - r * 0.9), (cx, top + r * 0.3)], fill=horn)
    d.ellipse([cx - r, top, cx + r, top + 2 * r], fill=base)
    d.polygon([(cx - r * 1.3, bot - r * 0.4), (cx - r * 0.5, top + r * 1.3),
               (cx + r * 0.5, top + r * 1.3), (cx + r * 1.3, bot - r * 0.4),
               (cx, bot)], fill=dark)
    eye_y = top + r * 0.85
    d.ellipse([cx - r * 0.55, eye_y - 1.5, cx - r * 0.15, eye_y + 1.5], fill=(255, 220, 80, 255))
    d.ellipse([cx + r * 0.15, eye_y - 1.5, cx + r * 0.55, eye_y + 1.5], fill=(255, 220, 80, 255))
    return img


def make_demon():
    return finish(_horned_demon(scale=1.0))


def make_boss_demonlord():
    # Real content fix: this used to be byte-identical to demon.png. A darker, richer red (not
    # the same hue as the trash-mob demon), black obsidian horns (kept SMALL relative to the
    # body so they read as accents, not a pale wash over the whole silhouette -- an earlier pass
    # here made the horns dominate and the boss looked washed-out pink instead of imposing), a
    # third center horn, and shoulder spikes so the outline itself is spikier at a glance.
    horn = (25, 20, 30, 255)
    img = canvas(); d = ImageDraw.Draw(img)
    base = _horned_demon(scale=1.2, base=(105, 10, 45, 255), dark=(55, 8, 30, 255), horn=horn)
    img = Image.alpha_composite(img, base)
    d = ImageDraw.Draw(img)
    d.polygon([(14, 4), (16, -1), (18, 4), (16, 7)], fill=horn)              # center horn
    d.polygon([(5, 15), (9, 10), (10, 17)], fill=horn)                       # left shoulder spike
    d.polygon([(27, 15), (23, 10), (22, 17)], fill=horn)                     # right shoulder spike
    d.ellipse([12, 12, 15, 15], fill=(255, 210, 60, 255))                    # eyes redrawn on top
    d.ellipse([17, 12, 20, 15], fill=(255, 210, 60, 255))                    # (spikes must not cover them)
    return finish(img)


# ============================================================ people ====

def _person(skin, hair, outfit, accent, crown=None, cone_hat=None):
    img = canvas(); d = ImageDraw.Draw(img)
    d.ellipse([9, 21, 23, 31], fill=outfit)
    d.rectangle([9, 22, 23, 27], fill=outfit)
    d.ellipse([11, 4, 21, 15], fill=skin)
    d.polygon([(11, 8), (11, 4), (21, 4), (21, 8), (16, 6)], fill=hair)
    if cone_hat:
        band, tip = cone_hat
        d.polygon([(9, 8), (16, -3), (23, 8)], fill=tip)
        d.rectangle([9, 6, 23, 9], fill=band)
    if crown:
        # A plain band + one center spike -- a self-intersecting "zigzag" polygon here rendered
        # as barely a sliver (PIL's fill winding canceled most of it out), so this is deliberately
        # two simple, non-self-intersecting shapes instead of one clever one.
        d.rectangle([11, 5, 21, 8], fill=crown)
        d.polygon([(14, 5), (16, 0), (18, 5)], fill=crown)
    d.ellipse([13, 8, 14, 9], fill=(30, 20, 20, 255))
    d.ellipse([18, 8, 19, 9], fill=(30, 20, 20, 255))
    d.rectangle([14, 16, 18, 20], fill=accent)
    return finish(img)


def make_player():
    return _person((235, 195, 160, 255), (70, 45, 30, 255), (55, 85, 175, 255), (230, 200, 90, 255))


def make_npc_villager():
    return _person((225, 185, 150, 255), (90, 60, 35, 255), (150, 110, 65, 255), (110, 80, 45, 255))


def make_npc_king():
    return _person((225, 185, 150, 255), (230, 220, 210, 255), (55, 65, 150, 255), (230, 195, 60, 255),
                   crown=(235, 200, 70, 255))


def make_npc_princess():
    return _person((235, 195, 165, 255), (235, 200, 80, 255), (225, 130, 175, 255), (255, 235, 245, 255))


def make_npc_sorcerer():
    return _person((215, 175, 150, 255), (200, 200, 205, 255), (120, 70, 195, 255), (230, 200, 90, 255),
                   cone_hat=((95, 55, 160, 255), (75, 40, 130, 255)))


def make_npc_handmaiden():
    # Was a plain lavender blob with a gold dot -- didn't match the rest of the cast's
    # actual person silhouette. Rebuilt on the shared _person() rig so it reads as one of the
    # same cast, just her own palette.
    return _person((240, 220, 235, 255), (215, 195, 230, 255), (190, 165, 220, 255), (230, 205, 235, 255))


# ============================================================ items ====

def make_gem(color, hi):
    img = canvas(); d = ImageDraw.Draw(img)
    d.polygon([(16, 5), (26, 16), (16, 27), (6, 16)], fill=color)
    d.polygon([(16, 5), (21, 16), (16, 27), (16, 16)], fill=hi)
    d.line([16, 5, 6, 16], fill=(255, 255, 255, 90), width=1)
    return finish(img)


def make_gem_atk():
    return make_gem((205, 45, 45, 255), (235, 90, 85, 255))


def make_gem_def():
    return make_gem((45, 100, 205, 255), (85, 140, 235, 255))


def make_potion(liquid):
    img = canvas(); d = ImageDraw.Draw(img)
    d.rectangle([13, 6, 19, 11], fill=(210, 210, 220, 255))
    d.ellipse([9, 10, 23, 27], fill=(235, 235, 240, 230))
    d.pieslice([10, 14, 22, 27], start=0, end=180, fill=liquid)
    d.rectangle([10, 17, 22, 24], fill=liquid)
    d.ellipse([12, 13, 16, 16], fill=(255, 255, 255, 150))
    return finish(img)


def make_potion_red():
    return make_potion((205, 45, 45, 255))


def make_potion_blue():
    return make_potion((45, 110, 205, 255))


def make_coin():
    img = canvas(); d = ImageDraw.Draw(img)
    d.ellipse([6, 8, 26, 24], fill=(190, 145, 25, 255))
    d.ellipse([8, 9, 24, 23], fill=(240, 200, 50, 255))
    d.ellipse([11, 12, 21, 20], fill=(255, 225, 110, 255))
    d.line([13, 16, 19, 16], fill=(190, 145, 25, 255), width=2)
    d.line([16, 13, 16, 19], fill=(190, 145, 25, 255), width=2)
    return finish(img)


def make_key(color):
    img = canvas(); d = ImageDraw.Draw(img)
    d.ellipse([6, 6, 17, 17], outline=color, width=3)
    d.rectangle([15, 14, 27, 17], fill=color)
    d.rectangle([21, 17, 24, 21], fill=color)
    d.rectangle([24, 17, 27, 20], fill=color)
    return finish(img)


def make_key_yellow():
    return make_key((235, 195, 50, 255))


def make_key_blue():
    return make_key((60, 130, 220, 255))


def make_key_red():
    return make_key((215, 60, 60, 255))


def make_door(color):
    img = canvas(); d = ImageDraw.Draw(img)
    d.rectangle([3, 3, 28, 28], fill=(60, 55, 65, 255))
    d.rectangle([5, 5, 26, 26], fill=color)
    d.rectangle([8, 8, 23, 23], outline=(255, 255, 255, 60), width=1)
    d.ellipse([19, 14, 23, 18], fill=(60, 55, 65, 255))
    return finish(img)


def make_door_yellow():
    return make_door((225, 180, 45, 255))


def make_door_blue():
    return make_door((55, 120, 205, 255))


def make_door_red():
    return make_door((200, 55, 55, 255))


def make_exp_up():
    img = canvas(); d = ImageDraw.Draw(img)
    d.ellipse([4, 4, 28, 28], fill=(55, 190, 90, 255))
    d.ellipse([7, 7, 25, 25], fill=(75, 210, 110, 255))
    d.polygon([(16, 8), (11, 17), (14, 17), (14, 23), (18, 23), (18, 17), (21, 17)], fill=(240, 250, 240, 255))
    return finish(img)


def make_scroll():
    img = canvas(); d = ImageDraw.Draw(img)
    d.rectangle([7, 9, 25, 23], fill=(224, 200, 152, 255))
    d.rectangle([6, 7, 26, 10], fill=(165, 130, 85, 255))
    d.rectangle([6, 21, 26, 24], fill=(165, 130, 85, 255))
    d.line([10, 13, 22, 13], fill=(110, 80, 55, 255), width=1)
    d.line([10, 16, 22, 16], fill=(110, 80, 55, 255), width=1)
    d.line([10, 19, 18, 19], fill=(110, 80, 55, 255), width=1)
    return finish(img)


def make_stairs(going_up):
    img = canvas(); d = ImageDraw.Draw(img)
    steps = range(4) if going_up else range(3, -1, -1)
    for i, s in enumerate(steps):
        y0 = 26 - s * 5
        w = 26 - i * 4
        x0 = 16 - w // 2
        shade_c = (150, 150, 165, 255) if i % 2 == 0 else (120, 120, 135, 255)
        d.rectangle([x0, y0 - 4, x0 + w, y0], fill=shade_c)
    return finish(img)


def make_stairs_up():
    return make_stairs(True)


def make_stairs_down():
    return make_stairs(False)


def make_wall():
    img = canvas(); d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 31, 31], fill=(96, 96, 108, 255))
    for row, off in ((4, 0), (14, 8), (24, 0)):
        for x in range(-8 + off, 32, 16):
            d.rectangle([x, row, x + 14, row + 8], outline=(60, 60, 72, 255), width=1)
    return img   # deliberately no outline/shade pass -- a tiling texture, not a silhouette


def make_floor():
    img = canvas(); d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 31, 31], fill=(70, 65, 80, 255))
    for x in range(0, 32, 8):
        for y in range(0, 32, 8):
            if (x // 8 + y // 8) % 2 == 0:
                d.rectangle([x, y, x + 7, y + 7], fill=(76, 71, 86, 255))
    return img   # tiling texture, same reasoning as make_wall


SPRITES = {
    "slime": make_slime, "bat": make_bat, "golem": make_golem, "skeleton": make_skeleton,
    "wraith": make_wraith, "demon": make_demon, "boss_demonlord": make_boss_demonlord,
    "player": make_player, "npc_villager": make_npc_villager, "npc_king": make_npc_king,
    "npc_princess": make_npc_princess, "npc_sorcerer": make_npc_sorcerer,
    "npc_handmaiden": make_npc_handmaiden,
    "gem_atk": make_gem_atk, "gem_def": make_gem_def,
    "potion_red": make_potion_red, "potion_blue": make_potion_blue,
    "coin": make_coin, "key_yellow": make_key_yellow, "key_blue": make_key_blue, "key_red": make_key_red,
    "door_yellow": make_door_yellow, "door_blue": make_door_blue, "door_red": make_door_red,
    "exp_up": make_exp_up, "scroll": make_scroll,
    "stairs_up": make_stairs_up, "stairs_down": make_stairs_down,
    "wall": make_wall, "floor": make_floor,
}


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, fn in SPRITES.items():
        save(fn(), name)
    print("wrote %d sprites to %s" % (len(SPRITES), OUT_DIR))


if __name__ == "__main__":
    main()
