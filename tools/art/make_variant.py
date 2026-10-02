#!/usr/bin/env python3
"""Builds a playable art variant of TOMS from picked reskin results (tools/art/reskin_sprites.py).

  python tools/art/make_variant.py --run Build/art_out/dark16 --name dark16
  python tools/art/make_variant.py --run Build/art_out/dark16 --name dark16 --pick slime=Build/art_out/trial2:1003 --pick wall=1001

Picks live in <run>/picks.json (sprite id -> seed). The first call writes it with the first seed of every
sprite; edit it, or pass --pick id=seed (from --run) / --pick id=<other run folder>:seed, which also updates
the file. Sprites without a pick keep the original art.

Result: Build/assets_variants/<name>/media (a copy of assets/media with the picked sprites) and
Build/assets_variants/<name>/data (a copy of assets/data: the game reads <assets>/../data). assets/ is not touched.
Play it with:  toms_game.exe --assets=Build/assets_variants/<name>/media

--install instead adds the picks to the game as a selectable art style (Settings > art style; the
choice applies after a restart): assets/media/styles/<name>/sprites/ + an entry in
assets/media/styles/styles.json, with the display name from --title. Sprites without a pick keep the
original art, which the game scales to the style's size by itself.

  python tools/art/make_variant.py --run Build/art_out/dark16 --name dark16 --size 64 --install \
      --title "zh_TW=暗黑奇幻 16-bit,en=Dark fantasy 16-bit"

--size picks the sprite size (default: the game's 32; 64 shows much more of the AI detail). The engine takes
the size from the PNGs themselves, so all sprites of a variant must share it -- sprites without a pick
(e.g. the floor) are scaled up from the original with nearest-neighbour, which keeps their hard pixels.
"""
import argparse
import json
import shutil
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
MEDIA = ROOT / "assets" / "media"
DATA = ROOT / "assets" / "data"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--run", required=True, help="reskin run folder, e.g. Build/art_out/dark16")
    p.add_argument("--name", required=True, help="variant name -> Build/assets_variants/<name>")
    p.add_argument("--pick", action="append", default=[], help="id=seed or id=<run folder>:seed (repeatable)")
    p.add_argument("--size", type=int, default=None, help="sprite size in px (default: assets/media's, i.e. 32)")
    p.add_argument("--install", action="store_true",
                   help="add it to the game as a selectable art style (assets/media/styles/<name>)")
    p.add_argument("--title", default=None,
                   help='display name per language for --install, e.g. "zh_TW=暗黑奇幻,en=Dark fantasy"')
    a = p.parse_args()

    run = Path(a.run)
    if not run.is_absolute():
        run = ROOT / run
    run_info = json.loads((run / "run.json").read_text())
    manifest = json.loads((MEDIA / "sprites" / "manifest.json").read_text())
    size = a.size or manifest["tile"]   # the engine's atlas cell = the sprites' own size (game_assets.cpp)

    picks_file = run / "picks.json"
    if picks_file.exists():
        picks = json.loads(picks_file.read_text())
    else:
        picks = {sid: run_info["seed_base"] for sid in run_info["sprites"]}
    for item in a.pick:
        sid, _, value = item.partition("=")
        if not value:
            sys.exit(f"--pick {item}: expected id=seed or id=<run folder>:seed")
        picks[sid] = int(value) if value.isdigit() else value
    picks_file.write_text(json.dumps(picks, indent=2))

    # resolve every pick to a sprite file of the engine's size
    chosen = {}
    for sid, value in picks.items():
        src_run, seed = (run, value) if isinstance(value, int) else (Path(value.rsplit(":", 1)[0]), int(value.rsplit(":", 1)[1]))
        if not src_run.is_absolute():
            src_run = ROOT / src_run
        f = src_run / f"sprites_{size}" / f"{sid}_s{seed}.png"
        if not f.exists():
            sys.exit(f"{sid}: {f} does not exist (run reskin_sprites.py with --sizes including {size}, or fix the pick)")
        im = Image.open(f)
        if im.size != (size, size) or im.mode != "RGBA":
            sys.exit(f"{sid}: {f} is {im.size} {im.mode}, the engine needs {size}x{size} RGBA")
        chosen[sid] = f

    if a.install:
        install_style(a, chosen, size)
        return

    out = ROOT / "Build" / "assets_variants" / a.name
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(MEDIA, out / "media")
    shutil.copytree(DATA, out / "data")
    for sid, f in chosen.items():
        shutil.copyfile(f, out / "media" / "sprites" / f"{sid}.png")
    upscaled = []
    if size != manifest["tile"]:
        # every sprite must have the same size: scale the ones without a pick (nearest keeps the pixels)
        for sid in manifest["sprites"]:
            if sid in chosen:
                continue
            f = out / "media" / "sprites" / f"{sid}.png"
            Image.open(f).convert("RGBA").resize((size, size), Image.NEAREST).save(f)
            upscaled.append(sid)
        (out / "media" / "sprites" / "manifest.json").write_text(json.dumps({**manifest, "tile": size}))
    (out / "variant.json").write_text(json.dumps(
        {"run": str(run.relative_to(ROOT)) if run.is_relative_to(ROOT) else str(run),
         "style": run_info.get("style"), "size": size, "picks": picks, "upscaled_originals": upscaled,
         "original": sorted(set(json.loads((MEDIA / "sprites" / "manifest.json").read_text())["sprites"]) - set(chosen))},
        indent=2))

    print(f"[variant] {size}px: {len(chosen)} sprites replaced, {len(upscaled)} original(s) scaled up, the rest original -> {out}")
    print(f"[variant] picks: {picks_file}")
    builds = [ROOT / "Build" / b / "bin" / "toms_game.exe" for b in
              ("windows-release", "windows-shipping", "windows-debug", "ci-windows")]
    exe = next((b for b in builds if b.exists()), builds[0])
    print(f'[variant] play:  "{exe}" --assets="{out / "media"}"')


def install_style(a, chosen, size):
    """assets/media/styles/<name>/sprites/<id>.png for every pick + its styles.json entry."""
    if not a.name.replace("_", "").replace("-", "").isalnum():
        sys.exit(f"--name {a.name}: use letters, digits, '-' or '_' only (it is a folder name)")
    styles_dir = MEDIA / "styles"
    target = styles_dir / a.name / "sprites"
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    for sid, f in chosen.items():
        shutil.copyfile(f, target / f"{sid}.png")
    name = {}
    for part in (a.title or "").split(","):
        lang, _, text = part.partition("=")
        if text.strip():
            name[lang.strip()] = text.strip()
    if not name:
        name = {"zh_TW": a.name, "en": a.name}
    index_file = styles_dir / "styles.json"
    index = json.loads(index_file.read_text(encoding="utf-8")) if index_file.exists() else {"styles": []}
    index["styles"] = [s for s in index.get("styles", []) if s.get("id") != a.name] + [{"id": a.name, "name": name}]
    index_file.write_text(json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"[style] installed '{a.name}' ({size}px, {len(chosen)} sprites) -> {target}")
    print(f"[style] listed in {index_file}: {[s['id'] for s in index['styles']]}")
    print("[style] pick it in the game: Settings > art style, then restart the game")


if __name__ == "__main__":
    main()
