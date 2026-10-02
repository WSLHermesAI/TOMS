#!/usr/bin/env python3
"""make_ui_font.py -- build the game's UI font: Noto Sans CJK TC cut down to the characters TOMS uses.

On desktop RmlUi draws the game's text from font files; this one,
assets/media/fonts/NotoSansCJKtc-TOMS.otf, is the default and the fallback for every language (a
language can name its own font in data/text.json, see docs/08_RMLUI.md). The web build does not use
it: the browser draws the text there. Shipping the full Noto Sans CJK TC (16 MB) would be wasteful,
so this keeps only the characters that appear in the game's text:
every string in assets/data/**/*.json and assets/media/styles/styles.json (art style names), every
.rml/.rcss file in assets/media/ui, printable ASCII
and a few symbols the code itself writes. Noto Sans CJK covers Traditional and Simplified Chinese,
Japanese kana and Korean hangul, so one file serves every language in data/text.json.

Run it again after adding or changing text (a new dialogue line with a character not yet used
would otherwise show as an empty box):

    python tools/make_ui_font.py          # rebuild the subset
    python tools/make_ui_font.py --check  # only report characters the current subset lacks

Needs Python 3 with fontTools (pip install fonttools). The full source font is downloaded once
into Build/font-cache/ (not committed). License: SIL Open Font License 1.1 (assets/media/fonts/OFL.txt).
"""
import json
import pathlib
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE_URL = ("https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/TraditionalChinese/"
              "NotoSansCJKtc-Regular.otf")
SOURCE = ROOT / "Build" / "font-cache" / "NotoSansCJKtc-Regular.otf"
OUTPUT = ROOT / "assets" / "media" / "fonts" / "NotoSansCJKtc-TOMS.otf"

# Written by C++ code rather than data files (HUD separators, arrows, check marks).
EXTRA = "▶◀▲▼←→↑↓•·—–…「」『』（）：，。！？、；《》〈〉×✓★☆♥"


def json_strings(value, out):
    if isinstance(value, str):
        out.append(value)
    elif isinstance(value, dict):
        for k, v in value.items():
            out.append(k)
            json_strings(v, out)
    elif isinstance(value, list):
        for v in value:
            json_strings(v, out)


def wanted_codepoints():
    texts = [EXTRA, "".join(chr(c) for c in range(0x20, 0x7F))]
    json_files = sorted((ROOT / "assets" / "data").rglob("*.json"))
    json_files += sorted((ROOT / "assets" / "media" / "styles").glob("*.json"))   # art style names
    for path in json_files:
        try:
            json_strings(json.loads(path.read_text(encoding="utf-8")), texts)
        except (ValueError, UnicodeDecodeError) as e:
            print(f"[make_ui_font] skipped {path.relative_to(ROOT)}: {e}", file=sys.stderr)
    for path in sorted((ROOT / "assets" / "media" / "ui").glob("*.r*")):
        texts.append(path.read_text(encoding="utf-8"))
    cps = set()
    for t in texts:
        cps.update(ord(ch) for ch in t if ord(ch) >= 0x20 and not 0xD800 <= ord(ch) <= 0xDFFF)
    return cps


def font_codepoints(path):
    from fontTools.ttLib import TTFont
    return set(TTFont(str(path), lazy=True).getBestCmap().keys())


def main():
    for stream in (sys.stdout, sys.stderr):   # Windows consoles default to a code page without CJK
        stream.reconfigure(encoding="utf-8", errors="replace")
    try:
        from fontTools import subset  # noqa: F401
    except ImportError:
        sys.exit("[make_ui_font] fontTools is missing: pip install fonttools")
    cps = wanted_codepoints()

    if "--check" in sys.argv:
        if not OUTPUT.exists():
            sys.exit(f"[make_ui_font] {OUTPUT.relative_to(ROOT)} does not exist; run without --check")
        missing = sorted(cp for cp in cps - font_codepoints(OUTPUT) if cp > 0x20)
        if missing:
            print(f"[make_ui_font] {len(missing)} character(s) used by the game are not in the font:")
            print("  " + "".join(chr(c) for c in missing[:200]))
            print("  run: python tools/make_ui_font.py")
            sys.exit(1)
        print(f"[make_ui_font] OK: all {len(cps)} characters are in {OUTPUT.name}")
        return

    if not SOURCE.exists():
        SOURCE.parent.mkdir(parents=True, exist_ok=True)
        print(f"[make_ui_font] downloading {SOURCE_URL}")
        urllib.request.urlretrieve(SOURCE_URL, SOURCE)

    have = font_codepoints(SOURCE)
    absent = sorted(cp for cp in cps - have if cp > 0x20)
    if absent:   # emoji and the like: RmlUi draws nothing for them, so say which
        print(f"[make_ui_font] {len(absent)} character(s) are not in Noto Sans CJK at all: "
              + " ".join(f"U+{c:04X}" for c in absent))

    from fontTools import subset
    options = subset.Options()
    options.layout_features = ["*"]      # keep kerning / vertical metrics features
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.hinting = False              # RmlUi/FreeType renders unhinted at game sizes; saves space
    font = subset.load_font(str(SOURCE), options)
    sub = subset.Subsetter(options)
    sub.populate(unicodes=sorted(cps & have))
    sub.subset(font)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    subset.save_font(font, str(OUTPUT), options)
    print(f"[make_ui_font] wrote {OUTPUT.relative_to(ROOT)}: {len(cps & have)} characters, "
          f"{OUTPUT.stat().st_size / 1024:.0f} KB")


if __name__ == "__main__":
    main()
