"""Give each chapter of the tower its own look and cast with the AI-made sprites (docs/gameplay/06_FLOOR_REDESIGN.md).

    python tools/redesign_floors.py [--dry-run]

For the 10 chapters (7 floors each: 6 generated, 1 hand-made boss floor):
  * a wall / floor / stairs theme per chapter (stage "theme": the game swaps in wall_<t>, floor_<t>, stairs_*_<t>);
  * generated floor k of a chapter introduces that chapter's k-th new monster: on the floor, the newest one is the
    most common, the chapter's earlier ones keep appearing, and about a third of the monsters stay the original
    types (the missions count slimes, golems and wraiths). Monsters keep their cells and save ids -- only the type
    changes -- so keys, doors and events stay where the story check expects them;
  * every generated floor except the side-story one gets one new NPC, on a reachable cell that blocks nothing;
  * a new monster's stats come from the original monsters of the floor it is introduced on (their average, a little
    more on each later floor), so difficulty follows the old curve;
  * the floor spec's enemies.mix lists what the floor now has.
Boss floors keep their maps and get the chapter's theme. A floor that already has a theme is skipped (run once).
"""
import glob
import json
import os
import random
import re
import sys
from collections import deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "assets", "data")
FLOORS = os.path.join(DATA, "story", "floors")

THEME = {1: "wood", 2: "forest", 3: "cave", 4: "crypt", 5: "shadow", 6: "lava", 7: "sand", 8: "ice", 9: "crystal",
         10: "castle"}
MONSTERS = {
    1: ["mossling", "sprout_imp", "toadstool", "bear_cub", "vine_snake", "pixie"],
    2: ["forest_wolf", "thorn_boar", "horn_beetle", "bandit", "kobold", "red_bat"],
    3: ["rock_crab", "mole_miner", "gem_slime", "crystal_spider", "stone_lizard", "cave_troll"],
    4: ["grave_rat", "glow_worm", "zombie", "mummy", "bone_archer", "ghoul"],
    5: ["ghost_maiden", "vampire_bat", "shadow_assassin", "banshee", "skull_knight", "lich"],
    6: ["salamander", "magma_slime", "fire_imp", "ember_wolf", "lava_golem", "flame_spirit"],
    7: ["cinder_beetle", "ash_wraith", "phoenix_chick", "fire_drake", "ore_golem", "gargoyle"],
    8: ["ice_slime", "frost_wolf", "penguin_knight", "frost_fairy", "yeti", "ice_golem"],
    9: ["blizzard_owl", "ice_wraith", "crystal_serpent", "mammoth", "dark_mage", "chimera"],
    10: ["dark_knight", "demon_guard", "minotaur", "hellhound", "armored_ogre", "demon_general"],
}
NPCS = {
    1: ["farmer", "farmer_wife", "shepherd_girl", "old_woman", "village_boy"],
    2: ["hunter", "lumberjack", "ranger", "fisherman", "village_girl"],
    3: ["miner", "dwarf", "town_guard", "gatekeeper", "archer_scout"],
    4: ["scholar", "cartographer", "priest", "nun", "monk"],
    5: ["oracle", "fortune_teller", "pilgrim", "healer", "wandering_mage"],
    6: ["blacksmith", "armorer", "alchemist", "potion_seller", "explorer"],
    7: ["merchant", "peddler", "jeweler", "sailor", "bard"],
    8: ["innkeeper", "baker", "weaver", "tailor", "old_hero"],
    9: ["elf", "miller", "fletcher", "knight_errant", "jester"],
    10: ["duke", "duchess", "young_lord", "advisor", "guard_captain"],
}
KEEP_ORIGINAL = 0.35
RESERVED = set("#.@yrbYRBUD")
FREE_CHARS = "ACEFGHIJKLMNOPQSTVWXZacefgijklmnopqstuwxz0123456789!$%&*+=?^~"


def read_json(path):
    raw = open(path, encoding="utf-8", newline="").read()
    return json.loads(raw), raw


def write_json(path, j, raw):
    text = json.dumps(j, indent=2, ensure_ascii=False) + ("\n" if raw.endswith("\n") else "")
    if "\r\n" in raw:
        text = text.replace("\n", "\r\n")
    open(path, "w", encoding="utf-8", newline="").write(text)


def free_char(legend):
    for c in FREE_CHARS:
        if c not in legend and c not in RESERVED:
            return c
    raise RuntimeError("no free legend character")


def char_for(legend, sem):
    for c, v in legend.items():
        if v == sem:
            return c
    c = free_char(legend)
    legend[c] = sem
    return c


def reachable(grid, legend, start, blocked=None):
    h, w = len(grid), len(grid[0])
    seen = {start}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if not (0 <= ny < h and 0 <= nx < len(grid[ny])) or (nx, ny) in seen or (nx, ny) == blocked:
                continue
            ch = grid[ny][nx]
            if ch == "#" or legend.get(ch, "").startswith("npc:"):
                continue
            seen.add((nx, ny))
            q.append((nx, ny))
    return seen


def start_cell(grid, legend):
    for y, row in enumerate(grid):
        for x, ch in enumerate(row):
            if ch == "@" or legend.get(ch, "") in ("player_start", "stairs_down", "stairs_up") or \
                    legend.get(ch, "").startswith(("stairs_down", "stairs_up")):
                return (x, y)
    for y, row in enumerate(grid):
        for x, ch in enumerate(row):
            if ch == ".":
                return (x, y)
    return None


def place_npc(grid, legend, rng):
    """A reachable floor cell whose blocking cuts nothing off, away from stairs / start; alcoves first."""
    start = start_cell(grid, legend)
    if not start:
        return None
    base = reachable(grid, legend, start)
    specials = [(x, y) for y, row in enumerate(grid) for x, ch in enumerate(row)
                if ch == "@" or legend.get(ch, "").startswith(("stairs", "player_start"))]
    cands = []
    for (x, y) in base:
        if grid[y][x] != "." or any(abs(x - sx) + abs(y - sy) < 3 for sx, sy in specials):
            continue
        if len(reachable(grid, legend, start, blocked=(x, y))) != len(base) - 1:
            continue
        open_n = sum(1 for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1))
                     if 0 <= ny < len(grid) and 0 <= nx < len(grid[ny]) and grid[ny][nx] != "#")
        cands.append((0 if open_n <= 2 else 1, rng.random(), (x, y)))
    return min(cands)[2] if cands else None


def set_tile(grid, x, y, ch):
    row = grid[y]
    grid[y] = row[:x] + ch + row[x + 1:]


def stats_line_update(text, mid, stats):
    """Replace hp / atk / def / exp / gold inside enemies.json's hand-formatted block for mid."""
    m = re.search(r'\n  "' + re.escape(mid) + r'": \{(.*?)\n  \}', text, re.S)
    if not m:
        raise KeyError(mid)
    block = m.group(1)
    for k, v in stats.items():
        block = re.sub(r'("' + k + r'": )-?\d+', r"\g<1>" + str(v), block)
    return text[:m.start(1)] + block + text[m.end(1):]


def main():
    dry = "--dry-run" in sys.argv
    enemies_path = os.path.join(DATA, "enemies.json")
    enemies_raw = open(enemies_path, encoding="utf-8", newline="").read()
    enemies = json.loads(enemies_raw)
    new_stats = {}
    report = []
    # each chapter's baseline: its original monsters' average, but always at least 10 % above the chapter before
    # (late chapters reuse weaker original pairs, e.g. demon + skeleton after demon + wraith)
    base, prev = {}, None
    for c in range(1, 11):
        mix = None
        for p in sorted(glob.glob(os.path.join(FLOORS, "F??.json"))):
            sp = json.load(open(p, encoding="utf-8"))
            if int(sp["act"][3:]) == c and not sp.get("handAuthoredStage"):
                mix = [m for m in sp.get("enemies", {}).get("mix", []) if m in enemies and not enemies[m].get("draft")]
                break
        avg = {s: sum(enemies[o][s] for o in mix) / len(mix) for s in ("hp", "atk", "def", "exp", "gold")}
        if prev:
            avg = {s: max(v, prev[s] * 1.10) for s, v in avg.items()}
        base[c] = prev = avg
    npc_index = {c: 0 for c in NPCS}

    for spec_path in sorted(glob.glob(os.path.join(FLOORS, "F??.json"))):
        spec, spec_raw = read_json(spec_path)
        fid, ch = spec["id"], int(spec["act"][3:])
        k = spec.get("indexInAct", 1)
        if spec.get("handAuthoredStage"):
            stage_path = os.path.join(DATA, "stages", spec["handAuthoredStage"])
        else:
            stage_path = os.path.join(FLOORS, fid + ".stage.json")
        st, st_raw = read_json(stage_path)
        if st.get("theme"):
            report.append(f"{fid}: already has a theme, skipped")
            continue
        st["theme"] = THEME[ch]
        line = f"{fid} ch{ch:02d} {THEME[ch]:8}"

        if not spec.get("handAuthoredStage"):
            rng = random.Random(f"redesign:{fid}")
            legend, grid = st["legend"], list(st["tiles"])
            cells = [(x, y, legend[c][8:]) for y, row in enumerate(grid) for x, c in enumerate(row)
                     if legend.get(c, "").startswith("monster:")]
            originals = sorted({t for _, _, t in cells})
            intro = MONSTERS[ch][:min(k, 6)]
            newest = intro[-1]
            # the newest monster's stats: from this floor's original types
            if newest not in new_stats and originals:
                f = 0.92 + 0.04 * k
                new_stats[newest] = {s: max(1, round(v * f)) for s, v in base[ch].items()}
            # which cells keep their original type: at least one of each, about a third in all
            rng.shuffle(cells)
            keep = []
            for t in originals:
                keep.append(next(c for c in cells if c[2] == t))
            want = max(len(originals), round(len(cells) * KEEP_ORIGINAL))
            for c in cells:
                if len(keep) >= want:
                    break
                if c not in keep:
                    keep.append(c)
            # the rest: the chapter's monsters so far, the newest twice as often
            pool = [m for m in intro for _ in range(2 if m == newest else 1)]
            changed = [c for c in cells if c not in keep]
            for i, (x, y, _) in enumerate(changed):
                mid = newest if i == 0 else pool[rng.randrange(len(pool))]
                set_tile(grid, x, y, char_for(legend, "monster:" + mid))
                o = st.get("objects", {}).get(f"{x},{y}")
                if isinstance(o, dict):
                    o.pop("look", None)   # a look made for the old monster
            # one new NPC, except on the side-story floor
            npc = None
            if not str(spec.get("role", "")).startswith("side-story") and npc_index[ch] < len(NPCS[ch]):
                npc = NPCS[ch][npc_index[ch]]
                cell = place_npc(grid, legend, rng)
                if cell:
                    set_tile(grid, cell[0], cell[1], char_for(legend, "npc:" + npc))
                    npc_index[ch] += 1
                else:
                    npc = None
            st["tiles"] = grid
            used = sorted({legend[c][8:] for row in grid for c in row if legend.get(c, "").startswith("monster:")})
            spec.setdefault("enemies", {})["mix"] = used
            line += f" +{newest:15} npc {npc or '-':14} monsters {len(cells)} (kept {len(keep)} original)"
        else:
            line += " boss floor: theme only"
        report.append(line)
        if not dry:
            write_json(stage_path, st, st_raw)
            if not spec.get("handAuthoredStage"):
                write_json(spec_path, spec, spec_raw)

    # stats of the newly introduced monsters
    text = enemies_raw
    for mid, s in new_stats.items():
        text = stats_line_update(text, mid, s)
    json.loads(text)
    if not dry:
        open(enemies_path, "w", encoding="utf-8", newline="").write(text)
    print("\n".join(report))
    print(f"{len(new_stats)} monsters got their floor's stats; {sum(npc_index.values())} NPCs placed")


if __name__ == "__main__":
    main()
