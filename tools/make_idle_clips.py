"""Give every monster and NPC that has no idle animation a simple looping one (docs/engine/14_AI_SPRITES.md).

    python tools/make_idle_clips.py [--check] [--all-npcs]

Monsters: assets/media/anim/monster_idle.anim, clip "idle_<enemy id>" (map_objects.cpp plays it on the map). NPCs:
assets/media/anim/npc_idle.anim, clip "idle_<npc id>". A clip already there is never touched, so hand-made ones in
anim_editor stay; only missing ones are added. The motion is picked from the name:

    hover    flying things (bat, pixie, ghost, drake ...)   bob up and down, wings squeeze
    squash   slimes, worms, jelly                           squash and stretch
    breathe  big beasts (golem, troll, mammoth ...)         slow heavy breathing
    sway     undead and fighters (skeleton, knight ...)     lean left and right
    hop      anything else                                  a small hop
    npc      every NPC                                      a gentle breath

Each clip's length is nudged by its id, so a room of monsters does not move in step. --check lists what is missing
(exit 1 if anything is). NPCs get one only while they are drafts ("draft": true, the AI-made ones), so the shipped
NPCs (and the screenshots they are in) stay still; --all-npcs gives every NPC one.
"""
import glob
import json
import os
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "assets", "data")
ANIM = os.path.join(ROOT, "assets", "media", "anim")
ATLAS = os.path.join(ROOT, "assets", "media", "atlas", "game.atlas")

KINDS = [
    ("hover", ("bat", "pixie", "fairy", "owl", "wraith", "ghost", "banshee", "spirit", "phoenix", "drake", "gargoyle",
               "wisp", "fire_imp")),
    ("squash", ("slime", "jelly", "worm", "blob")),
    ("breathe", ("golem", "troll", "ogre", "mammoth", "yeti", "minotaur", "boar", "bear", "chimera", "hellhound",
                 "serpent", "lizard", "salamander")),
    ("sway", ("skeleton", "knight", "zombie", "mummy", "ghoul", "lich", "archer", "mage", "assassin", "guard",
              "general", "bandit", "kobold", "miner", "demon", "warrior")),
]


def kind_of(mid):
    for kind, words in KINDS:
        if any(w in mid for w in words):
            return kind
    return "hop"


def nudge(name, base):
    """base length +- 15 %, fixed per id."""
    return round(base * (0.85 + (zlib.crc32(name.encode()) % 31) / 100.0), 2)


def key(t, v, ease=None):
    k = {"t": round(t, 3), "v": v}
    if ease:
        k["ease"] = ease
    return k


def tracks(kind, L):
    s = "sinusoidalInOut"
    if kind == "hover":
        flap = []
        n = 4
        for i in range(n * 2 + 1):
            t = L * i / (n * 2)
            flap.append(key(t, [1, 1] if i % 2 == 0 else [0.85, 1.05], None if i == n * 2 else ("quadraticOut" if i % 2 == 0 else "quadraticIn")))
        return {"pos": [key(0, [0, 0], s), key(L / 2, [0, -3], s), key(L, [0, 0])], "scale": flap}
    if kind == "squash":
        return {"scale": [key(0, [1, 1], s), key(L / 2, [1.1, 0.9], s), key(L, [1, 1])]}
    if kind == "breathe":
        return {"scale": [key(0, [1, 1], s), key(L / 2, [1.04, 0.96], s), key(L, [1, 1])]}
    if kind == "sway":
        return {"rot": [key(0, -3, s), key(L / 2, 3, s), key(L, -3)]}
    if kind == "npc":
        return {"scale": [key(0, [1, 1], s), key(L * 0.5, [1.0, 1.025], s), key(L, [1, 1])]}
    # hop: a quick small jump, then a rest
    return {"pos": [key(0, [0, 0]), key(L * 0.6, [0, 0], "quadraticOut"), key(L * 0.72, [0, -2], "quadraticIn"),
                    key(L * 0.84, [0, 0]), key(L, [0, 0])],
            "scale": [key(0, [1, 1]), key(L * 0.84, [1, 1], "quadraticOut"), key(L * 0.9, [1.06, 0.94], "quadraticIn"),
                      key(L, [1, 1])]}


BASE = {"hover": 0.8, "squash": 1.4, "breathe": 2.4, "sway": 2.0, "hop": 1.6, "npc": 2.2}


def clip(name, sprite, kind):
    L = nudge(name, BASE[kind])
    return {"name": name, "length": L, "playCount": -1,
            "root": {"name": "monster" if kind != "npc" else "npc",
                     "children": [{"name": "body", "sprite": "game:" + sprite, "pivot": [0.5, 1.0], "tracks": tracks(kind, L)}]}}


def atlas_sprites():
    names = set()
    try:
        for line in open(ATLAS, encoding="utf-8"):
            line = line.rstrip("\r\n")
            if line and not line.startswith((" ", "\t")) and ":" not in line and not line.endswith(".png"):
                names.add(line.strip())
    except OSError:
        pass
    return names


def load_anim(path):
    if os.path.exists(path):
        raw = open(path, encoding="utf-8", newline="").read()
        return json.loads(raw), "\r\n" in raw
    return {"version": 1, "atlases": ["../atlas/game.atlas", "../atlas/fx.atlas"], "clips": []}, False


def save_anim(path, j, crlf):
    text = json.dumps(j, indent=2, ensure_ascii=False) + "\n"
    if crlf:
        text = text.replace("\n", "\r\n")
    open(path, "w", encoding="utf-8", newline="").write(text)


def main():
    check = "--check" in sys.argv
    all_npcs = "--all-npcs" in sys.argv
    have = atlas_sprites()
    missing = 0

    # monsters
    enemies = json.load(open(os.path.join(DATA, "enemies.json"), encoding="utf-8"))
    mpath = os.path.join(ANIM, "monster_idle.anim")
    mj, mcrlf = load_anim(mpath)
    known = {c["name"] for c in mj["clips"]}
    added = []
    for mid, e in enemies.items():
        sprite = str(e.get("sprite", mid)).removesuffix(".png")
        if "idle_" + mid in known or (have and sprite not in have):
            continue
        added.append(clip("idle_" + mid, sprite, kind_of(mid)))

    # NPCs
    npath = os.path.join(ANIM, "npc_idle.anim")
    nj, ncrlf = load_anim(npath)
    nknown = {c["name"] for c in nj["clips"]}
    nadded = []
    for f in sorted(glob.glob(os.path.join(DATA, "npcs", "*.json"))):
        n = json.load(open(f, encoding="utf-8"))
        nid = n.get("npcId") or os.path.splitext(os.path.basename(f))[0]
        sprite = n.get("sprite") or "npc_" + nid
        if "idle_" + nid in nknown or (have and sprite not in have) or not (all_npcs or n.get("draft")):
            continue
        nadded.append(clip("idle_" + nid, sprite, "npc"))

    if check:
        for c in added + nadded:
            print("missing", c["name"])
        print(f"{len(added)} monster and {len(nadded)} NPC idle clips missing")
        sys.exit(1 if added or nadded else 0)
    if added:
        mj["clips"] += added
        save_anim(mpath, mj, mcrlf)
    if nadded:
        nj["clips"] += nadded
        save_anim(npath, nj, ncrlf)
    kinds = {}
    for c in added:
        k = kind_of(c["name"][5:])
        kinds[k] = kinds.get(k, 0) + 1
    print(f"monster_idle.anim: {len(added)} added {kinds}; npc_idle.anim: {len(nadded)} added")


if __name__ == "__main__":
    main()
