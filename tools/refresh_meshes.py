"""Refresh the sprite meshes beside every atlas the clips use (docs/engine/16_MESH_ANIMATION.md).

Run it after the atlases change: a rebuild (`atlaspack build art_raw/atlas/game.atlasproj --all-variants`), new AI
sprites (docs/engine/14_AI_SPRITES.md) or a restyled art style (docs/engine/03_ART_STYLES.md). The atlas tool never touches meshes;
this cuts them again from the new pixels with the anim editor's headless command:

  - every .anim of the game (assets/), of the tests (tests/smoke/) and of the docs' examples is read;
  - the meshes their skinned and deformed nodes need go into <atlas>.mesh.json (+ .mesh.bin) beside each atlas they
    use, with --prune: what no clip uses any more goes;
  - each art style (assets/media/styles/<id>/atlas/game.atlas) gets its own game.mesh.json, cut from its own pixels,
    for the clips that use the game atlas (the game draws a style with it).

    python tools/refresh_meshes.py            # write
    python tools/refresh_meshes.py --check    # exit 1 when a mesh file is out of date (a test)
    python tools/refresh_meshes.py --anim-editor <path to anim_editor.exe>

A clip that names no atlas is drawn by the game with its sprite atlas, so it counts as using assets/media/atlas/game.atlas.
"""
import argparse
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_ATLAS = os.path.join(ROOT, "assets", "media", "atlas", "game.atlas")
CLIP_DIRS = [os.path.join(ROOT, "assets"), os.path.join(ROOT, "tests", "smoke"), os.path.join(ROOT, "docs", "examples")]


def find_anim_editor(given):
    if given:
        return given
    for rel in ("Build/windows-release/bin/anim_editor.exe", "Build/windows-debug/bin/anim_editor.exe",
                "Editor/bin/anim_editor.exe", "Build/linux-release/bin/anim_editor"):
        p = os.path.join(ROOT, rel)
        if os.path.isfile(p):
            return p
    return None


def clip_files():
    out = []
    for d in CLIP_DIRS:
        out += glob.glob(os.path.join(d, "**", "*.anim"), recursive=True)
    return sorted(set(os.path.normpath(p) for p in out))


def style_atlases():
    return sorted(glob.glob(os.path.join(ROOT, "assets", "media", "styles", "*", "atlas", "game.atlas")))


def run(cmd):
    print("> " + " ".join(os.path.relpath(c, ROOT) if os.path.isabs(c) else c for c in cmd), flush=True)
    r = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    sys.stdout.write(r.stdout)   # anim_editor is a GUI program: its output comes back through the pipe
    return r.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="write nothing; exit 1 when a mesh file is out of date")
    ap.add_argument("--anim-editor", help="the anim_editor executable (default: the build's)")
    a = ap.parse_args()
    editor = find_anim_editor(a.anim_editor)
    if not editor:
        print("refresh_meshes: anim_editor not found; build it (tools\\build.cmd) or pass --anim-editor", file=sys.stderr)
        return 2
    clips = clip_files()
    if not clips:
        print("refresh_meshes: no .anim files found", file=sys.stderr)
        return 2
    extra = ["--prune"] + (["--check"] if a.check else [])
    base = [editor, "--headless", "meshes"] + clips + ["--default-atlas", "game=" + GAME_ATLAS] + extra
    worst = run(base)
    for style in style_atlases():   # the game atlas swapped for the style's copy: its own pixels, its own mesh file
        code = run(base + ["--map", GAME_ATLAS + "=" + style])
        worst = max(worst, code)
    if worst == 0:
        print("refresh_meshes: " + ("every mesh file is up to date" if a.check else "done"))
    return worst


if __name__ == "__main__":
    sys.exit(main())
