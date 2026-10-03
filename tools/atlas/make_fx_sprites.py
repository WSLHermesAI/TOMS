"""make_fx_sprites.py -- the particle sprites of assets/media/atlas/fx.atlasproj (docs/17_PARTICLES.md).

White shapes with soft alpha, so an emitter's colour tints them. Writes PNGs into OUT_DIR, then:

    python tools/atlas/make_fx_sprites.py <OUT_DIR>
    atlaspack add assets/media/atlas/fx.atlasproj <OUT_DIR> --replace
    atlaspack build assets/media/atlas/fx.atlasproj

Needs Pillow. Deterministic (fixed noise seed), so rebuilding gives the same pixels.
"""
import math
import os
import random
import sys

from PIL import Image


def save(img, out, name):
    img.save(os.path.join(out, name + ".png"))


def field(w, h, alpha_at):
    """A white image whose alpha is alpha_at(x, y) in 0..1, sampled at pixel centres."""
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            a = max(0.0, min(1.0, alpha_at(x + 0.5, y + 0.5)))
            px[x, y] = (255, 255, 255, int(round(a * 255)))
    return img


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def dot(s=32):   # soft round particle
    c = s / 2
    return field(s, s, lambda x, y: smooth(1 - math.hypot(x - c, y - c) / c) ** 1.2)


def glow(s=64):  # wide, very soft falloff (light halos)
    c = s / 2
    return field(s, s, lambda x, y: (1 - min(1, math.hypot(x - c, y - c) / c)) ** 2.5)


def spark(w=64, h=12):   # a streak along x, bright head on the right (use alignToVelocity)
    cy = h / 2

    def a(x, y):
        along = x / w                        # 0 tail .. 1 head
        across = 1 - abs(y - cy) / cy
        head = smooth((1 - along) * 8) if along > 0.85 else 1
        return smooth(across) ** 1.5 * (along ** 1.5) * head
    return field(w, h, a)


def star(s=48):  # four soft points plus a core
    c = s / 2

    def a(x, y):
        dx, dy = abs(x - c) / c, abs(y - c) / c
        arms = max(smooth(1 - dx) * smooth(1 - dy * 7), smooth(1 - dy) * smooth(1 - dx * 7))
        core = smooth(1 - math.hypot(dx, dy) * 2.5)
        return max(arms, core)
    return field(s, s, a)


def ring(s=64, width=0.12):
    c = s / 2
    return field(s, s, lambda x, y: smooth(1 - abs(math.hypot(x - c, y - c) / c - (1 - width * 1.2)) / width))


def smoke(s=64, seed=7):  # a lumpy puff: a few overlapping soft blobs, a little noise
    rnd = random.Random(seed)
    c = s / 2
    blobs = [(c + rnd.uniform(-0.25, 0.25) * s, c + rnd.uniform(-0.25, 0.25) * s, rnd.uniform(0.22, 0.34) * s) for _ in range(6)]
    noise = [[rnd.random() for _ in range(s)] for _ in range(s)]

    def a(x, y):
        v = 0.0
        for bx, by, r in blobs:
            v = max(v, smooth(1 - math.hypot(x - bx, y - by) / r))
        edge = smooth(1 - math.hypot(x - c, y - c) / c)
        return v * edge * (0.8 + 0.2 * noise[int(y)][int(x)]) * 0.9
    return field(s, s, a)


def flame(frame, w=32, h=48):  # a teardrop, tip up; frames sway the tip
    sway = [0.0, 0.12, 0.0, -0.12][frame]
    cx = w / 2

    def a(x, y):
        t = y / h                          # 0 top (tip) .. 1 bottom
        mid = cx + sway * w * (1 - t) ** 2
        r = t ** 0.9                       # widens from the tip...
        if t > 0.7:                        # ...to a round base
            r *= math.sqrt(max(0.0, 1 - ((t - 0.7) / 0.3) ** 2))
        half = w * 0.46 * r
        if half <= 0:
            return 0
        return smooth(1 - abs(x - mid) / half) * (0.55 + 0.45 * t)
    return field(w, h, a)


def square(s=8):  # hard-edged debris / pixel bits
    return Image.new("RGBA", (s, s), (255, 255, 255, 255))


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 3
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    save(dot(), out, "dot")
    save(glow(), out, "glow")
    save(spark(), out, "spark")
    save(star(), out, "star")
    save(ring(), out, "ring")
    save(smoke(), out, "smoke")
    for f in range(4):
        save(flame(f), out, "flame_%d" % f)
    save(square(), out, "square")
    print("wrote 11 sprites to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
