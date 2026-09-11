#!/usr/bin/env python3
"""subset_fonts.py — regenerate assets/fonts/NotoSans{JP,KR}-Regular.ttf as a subset
containing only the codepoints actually used by shipped content.

Why: the full Noto Sans JP/KR fonts are ~10MB each (Hangul/Kana + the full CJK Unified
Ideographs block most of which this game never uses). assets/fonts/wqy-zenhei.ttc covers
Han (Chinese); these two only need to cover Kana/Hangul + the specific glyphs used by the
game's ja/ko (and Latin/es) translations. Subsetting to exactly that shrinks each font from
~10MB to a few hundred KB with zero rendering difference for any text the game actually
ships (see src/engine/font_test.cpp's "content coverage" test, which fails loudly if a
future translation needs a codepoint that got left out of a regenerated subset).

Run this again whenever new translated text is added (a new language, new dialogue, new
item/stage text) so the shipped fonts stay covering everything Font::collectFromFiles()
will bake at runtime (see Game::loadAssets() in src/game/game.cpp).

Requires: `pip install fonttools`. Needs the FULL (non-subsetted) source fonts, which are
NOT kept in this repo (that would defeat the point of subsetting) -- download them fresh:
  https://github.com/google/fonts/raw/main/ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf
  https://github.com/google/fonts/raw/main/ofl/notosanskr/NotoSansKR%5Bwght%5D.ttf
(these are variable fonts; this script instantiates the Regular/400 static weight before
subsetting, since stb_truetype -- see src/engine/font.cpp -- doesn't do variable-font
interpolation, only the default/`glyf`-table outline.)

Usage:
    python3 tools/subset_fonts.py /path/to/NotoSansJP[wght].ttf /path/to/NotoSansKR[wght].ttf
"""
import glob, os, re, sys, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def collect_codepoints():
    """Mirrors Font::collectFromFiles(jsonFiles, hud) in src/engine/font.cpp: every
    codepoint in every data/*.json file, plus game.cpp's hardcoded `hud` literal."""
    chars = set()
    for f in glob.glob(os.path.join(ROOT, "data", "**", "*.json"), recursive=True):
        with open(f, encoding="utf-8") as fh:
            chars.update(fh.read())

    gamecpp = open(os.path.join(ROOT, "src", "game", "game.cpp"), encoding="utf-8").read()
    m = re.search(r'std::string hud = "((?:[^"\\]|\\.)*)";', gamecpp)
    if not m:
        raise SystemExit("could not find the `hud` string literal in game.cpp -- "
                          "has it moved or been renamed?")
    chars.update(m.group(1))

    return sorted(ord(c) for c in chars if ord(c) != 0)


def subset_one(src_path, dest_path, unicodes_str):
    with tempfile.TemporaryDirectory() as tmp:
        static_path = os.path.join(tmp, "static.ttf")
        subprocess.run([sys.executable, "-m", "fontTools.varLib.instancer",
                         "-q", "-o", static_path, src_path, "wght=400"], check=True)
        subprocess.run([sys.executable, "-m", "fontTools.subset", static_path,
                         "--unicodes=" + unicodes_str,
                         "--output-file=" + dest_path,
                         "--layout-features=", "--no-hinting",
                         "--desubroutinize", "--name-IDs="], check=True)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        raise SystemExit(1)
    jp_src, kr_src = sys.argv[1], sys.argv[2]

    codepoints = collect_codepoints()
    unicodes_str = ",".join("U+%04X" % cp for cp in codepoints)
    print(f"collected {len(codepoints)} codepoints from data/*.json + game.cpp's hud literal")

    out_dir = os.path.join(ROOT, "assets", "fonts")
    subset_one(jp_src, os.path.join(out_dir, "NotoSansJP-Regular.ttf"), unicodes_str)
    subset_one(kr_src, os.path.join(out_dir, "NotoSansKR-Regular.ttf"), unicodes_str)
    print("wrote assets/fonts/NotoSansJP-Regular.ttf and NotoSansKR-Regular.ttf")
    print("now run: cmake --build build --target font_test  &&  ./build/Debug/font_test.exe")
    print("(the 'content coverage' check fails loudly if any shipped codepoint is missing)")


if __name__ == "__main__":
    main()
