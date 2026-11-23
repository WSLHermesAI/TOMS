"""pack_webp.py -- the web build's atlas pages as WebP (the owner, 2026-11-23: "how about use webp instead png?").

    python tools/web/pack_webp.py <png folder> <out folder> [--quality 90] [--skip brawler*] [--stamp <file>]

Each <png folder>/*.png becomes <out folder>/<name>.webp (lossy, alpha kept at full quality). The web build packs the
.webp files at the PNGs' place and leaves the PNGs out; the game asks for "game.png", finds "game.webp" instead
(src/core/engine/vfs.h f_VfsWebpTwin) and decodes it with libwebp (image_decode.cpp). The PNGs in the repo stay the
source: the desktop game, the editors and the atlas tools never see a WebP.

A page is converted again only when its PNG is newer than its WebP; a WebP whose PNG is gone is deleted.
Needs Pillow (pip install pillow).
"""
import argparse
import fnmatch
import os
import sys


def main():
    l_oAp = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    l_oAp.add_argument("src")
    l_oAp.add_argument("out")
    l_oAp.add_argument("--quality", type=int, default=90)
    l_oAp.add_argument("--skip", action="append", default=[], help="file name pattern to leave out (repeatable)")
    l_oAp.add_argument("--stamp", help="file touched when done (the build's output)")
    l_oArgs = l_oAp.parse_args()
    try:
        from PIL import Image
    except ImportError:
        print("[pack_webp] needs Pillow: python -m pip install pillow", file=sys.stderr)
        return 1
    os.makedirs(l_oArgs.out, exist_ok=True)
    l_vNames = sorted(n for n in os.listdir(l_oArgs.src) if n.lower().endswith(".png")
                      and not any(fnmatch.fnmatch(n, p) for p in l_oArgs.skip))
    l_setWant = set()
    l_iPng = l_iWebp = l_iDone = 0
    for l_strName in l_vNames:
        l_strSrc = os.path.join(l_oArgs.src, l_strName)
        l_strDst = os.path.join(l_oArgs.out, l_strName[:-4] + ".webp")
        l_setWant.add(os.path.basename(l_strDst))
        if not os.path.exists(l_strDst) or os.path.getmtime(l_strDst) < os.path.getmtime(l_strSrc):
            l_oIm = Image.open(l_strSrc)
            l_oIm = l_oIm.convert("RGBA" if ("A" in l_oIm.getbands() or "transparency" in l_oIm.info) else "RGB")
            l_strTmp = l_strDst + ".tmp"
            l_oIm.save(l_strTmp, "WEBP", quality=l_oArgs.quality, method=6, alpha_quality=100)
            os.replace(l_strTmp, l_strDst)
            l_iDone += 1
        l_iPng += os.path.getsize(l_strSrc)
        l_iWebp += os.path.getsize(l_strDst)
    for l_strName in os.listdir(l_oArgs.out):
        if l_strName.endswith(".webp") and l_strName not in l_setWant:
            os.remove(os.path.join(l_oArgs.out, l_strName))
    print("[pack_webp] %d pages (%d converted): PNG %.1f MB -> WebP %.1f MB" %
          (len(l_vNames), l_iDone, l_iPng / 1e6, l_iWebp / 1e6))
    if l_oArgs.stamp:
        with open(l_oArgs.stamp, "w") as l_oF:
            l_oF.write("%d\n" % len(l_vNames))
    return 0


if __name__ == "__main__":
    sys.exit(main())
