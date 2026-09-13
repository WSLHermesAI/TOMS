#!/usr/bin/env python3
"""gen_floors.py — S2: generate the 70-floor data set (docs/STORY_DATA_SCHEMA.md section 5-6).

Inputs (the design contract):
  * docs/STORY_DATA_SCHEMA.md section 5.1 + docs/STORY_BIBLE.md section 5 -- the floor table
    (maze size, rooms, loops, event nodes, enemy range, elites, items, difficulty tier).
  * docs/STORY_BIBLE.md section 3 -- the 10 acts (seal, boss, act theme) and section 6 -- the ten
    flagship side stories, one per act on that act's 5th floor.
  * docs/SIDE_STORIES.md section 12 -- the seven event kinds and the per-act pool seeds.
  * docs/STORY_DATA_SCHEMA.md section 3.2 -- which hand-authored stage is each act's boss floor.

Outputs, per document section 1.2:
  * data/story/floors/F01.json .. F70.json          -- the floor SPEC (section 4 schema): maze
    parameters, enemy mix/count/elites/boss, item table, sampled events, side-story hooks, the
    narrative keys, nextFloor, meta. This is what the story systems read.
  * data/story/floors/F01.stage.json .. (60 files)  -- the PLAYABLE grid for every non-boss floor,
    in the existing stage JSON schema (legend/tiles/connect/...) so the current engine loads it
    through the unchanged parseStage() path. Boss floors have no generated grid: they are the
    hand-authored data/stages/stageNN.json files (section 3.2), and their spec records that.

Two things are deliberately provisional until the phase that owns them (both are read from data when
it exists, so nothing here blocks that phase):
  * enemy mixes and item tables per act -- the chapters (data/story/chapters/*.json, S3) own these.
  * the event pools (data/events/pool_*.json, S3) -- until they exist this script samples from a
    built-in pool built from the documented pool seeds in SIDE_STORIES.md section 12.3 plus a
    documented common pool. Every generated floor still satisfies section 5.2's constraints
    (no repeated eventId on a floor, at least one relic/whisper, at least one cache).

Determinism: every floor is generated from a seed derived from its number (`<floor>:toms-s2`), so
re-running the script reproduces the same floors byte-for-byte and a save can name a floor.

Usage:
    python3 tools/gen_floors.py                 # generate all 70 floors (+ their grids)
    python3 tools/gen_floors.py --floors 1-14   # a range, for spot checks
Then: python3 tools/validate_story.py           # V7/V8/V9/V12 over the generated data
"""
import argparse
import json
import os
import random
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

# The maze algorithm and the BFS reachability check are REUSED from tools/gen_mazes.py, as
# STORY_DATA_SCHEMA.md section 5 requires ("沿用 tools/gen_mazes.py 的 Wilson 演算法與 BFS 可達性檢查").
# One implementation, one set of conventions (cells are SxS tiles with a single wall row/column
# between neighbours, so every passage is 2 tiles wide -- which is also what makes a 2x2 enemy able
# to traverse the maze, per ART_AND_ABILITY_DESIGN.md F3).
from gen_mazes import (cell_block, cell_center_xy, edge_strip, neighbors, wilson_maze,
                       build_adjacency, bfs, path_to, reachable_without_edge)

FLOORS_DIR = os.path.join(ROOT, 'data', 'story', 'floors')
STAGES_DIR = os.path.join(ROOT, 'data', 'stages')
FOOTPRINTS = os.path.join(ROOT, 'data', 'footprints.json')
EVENTS_DIR = os.path.join(ROOT, 'data', 'events')

CELL_SCALE = 2                      # SxS tiles per maze cell; matches gen_mazes.py's CELL_SCALE_BY_FLOOR

# ---------------------------------------------------------------------------------------------
# Section 5.1 floor table (identical to STORY_BIBLE.md section 5, which validator V9 checks against)
# ---------------------------------------------------------------------------------------------
# tier -> (cols, rows, rooms, loops, events, enemy_min, enemy_max, elites, items)
FLOOR_TABLE = [
    (19, 16,  3,  2,  3,  4,  6, 0,  4),   # F01-F07
    (23, 18,  4,  3,  4,  6,  8, 0,  5),   # F08-F14
    (27, 20,  5,  4,  5,  8, 11, 1,  6),   # F15-F21
    (31, 22,  6,  5,  6, 10, 14, 1,  7),   # F22-F28
    (35, 24,  7,  6,  7, 12, 16, 2,  8),   # F29-F35
    (39, 26,  8,  7,  8, 14, 19, 2,  9),   # F36-F42
    (45, 30,  9,  8,  9, 16, 22, 3, 10),   # F43-F49
    (51, 34, 10,  9, 10, 18, 25, 3, 11),   # F50-F56
    (57, 38, 11, 10, 11, 20, 28, 4, 12),   # F57-F63
    (63, 42, 12, 11, 12, 22, 32, 4, 13),   # F64-F70
]


def _stable_hash(text):
    """FNV-1a over the string's UTF-8 bytes: stable across processes and Python versions, unlike
    the built-in hash() (randomized per process since 3.3)."""
    h = 2166136261
    for b in text.encode('utf-8'):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


# ---------------------------------------------------------------------------------------------
# Acts (STORY_BIBLE.md section 3) -- seal, boss id, flagship side story (section 6)
# ---------------------------------------------------------------------------------------------
ACTS = [
    # id,      seal,              boss id,                    side story, floor-of-ss, theme key
    ("ch_01", "memory",  "slime_king",             "ss_01", "village"),
    ("ch_02", "qi",      "bat_matron",             "ss_02", "forest"),
    ("ch_03", "daoji",   "golem",                  "ss_03", "gate"),
    ("ch_04", "jindan",  "skeleton_scholar_boss",  "ss_04", "library"),
    ("ch_05", "soul",    "wraith_choir",           "ss_05", "crypt"),
    ("ch_06", "yuanying", "twin_golem",            "ss_06", "cavern"),
    ("ch_07", "fire",    "traitor_captain",        "ss_07", "barracks"),
    ("ch_08", "shenshi", "bell_warden",            "ss_08", "sanctum"),
    ("ch_09", "xinmo",   "inner_qingxiao",         "ss_09", "antechamber"),
    ("ch_10", "digu",    "demonlord_vorkath",      "ss_10", "peak"),
]

# section 3.2: which hand-authored stage is each act's boss floor (F07/F14/.../F70)
BOSS_STAGE = {
    7: "stage01.json", 14: "stage02.json", 21: "stage03.json", 28: "stage04.json",
    35: "stage05.json", 42: "stage06.json", 49: "stage07.json", 56: "stage08.json",
    63: "stage09.json", 70: "stage10.json",
}

# ---------------------------------------------------------------------------------------------
# PROVISIONAL content tables (see the module docstring): replaced by chapter data (S3) and by the
# 50-enemy roster (S8) when those land. They only need to be plausible and monotone.
# ---------------------------------------------------------------------------------------------
ENEMY_ROSTER = ["slime", "bat", "golem", "skeleton", "wraith", "demon"]     # data/enemies.json
MIX_BY_ACT = [
    ["slime", "bat"], ["slime", "bat"], ["bat", "golem"], ["skeleton", "bat"],
    ["skeleton", "wraith"], ["golem", "wraith"], ["skeleton", "demon"], ["wraith", "demon"],
    ["demon", "wraith"], ["demon", "skeleton"],
]
ITEM_TABLE_BY_ACT = [
    ["potion_red", "coin"], ["potion_red", "coin", "gem_def"],
    ["potion_red", "potion_blue", "gem_atk"], ["potion_red", "potion_blue", "exp_up"],
    ["potion_red", "potion_blue", "gem_atk", "scroll"], ["gem_atk", "gem_def", "potion_blue"],
    ["gem_atk", "gem_def", "exp_up", "scroll"], ["gem_atk", "gem_def", "scroll"],
    ["gem_atk", "gem_def", "exp_up"], ["gem_atk", "gem_def", "exp_up", "scroll"],
]

# Event pool seeds, verbatim from SIDE_STORIES.md section 12.3 (three documented ids per act) --
# extended with generated variants in the same namespace/kind so a floor can sample `events` of
# them; the real authored pools (data/events/pool_actNN.json, S3) take over automatically.
POOL_SEEDS = {
    "village": [("ev_village_well", "whisper"), ("ev_village_chimney", "relic"),
                ("ev_village_grave_new", "rescue")],
    "forest": [("ev_forest_broken_arrow", "relic"), ("ev_forest_snare", "trap"),
               ("ev_forest_old_wizard_mark", "whisper")],
    "gate": [("ev_gate_soldier_plate", "relic"), ("ev_gate_seal_rubbing", "whisper"),
             ("ev_gate_forge_first", "cache")],
    "library": [("ev_library_torn_page", "relic"), ("ev_library_scholar_whisper", "whisper"),
                ("ev_library_shelf_collapse", "trap")],
    "crypt": [("ev_crypt_ten_year_candle", "relic"), ("ev_crypt_nameless_brick", "whisper"),
              ("ev_crypt_choir_echo", "whisper")],
    "cavern": [("ev_cavern_twin_crystal", "relic"), ("ev_cavern_echo_wall", "whisper"),
               ("ev_cavern_miner_cache", "cache")],
    "barracks": [("ev_barracks_night_fire", "rescue"), ("ev_barracks_forge_t3", "cache"),
                 ("ev_barracks_deserter", "merchant_echo")],
    "sanctum": [("ev_sanctum_seventh_bell", "relic"), ("ev_sanctum_warden_oath", "whisper"),
                ("ev_sanctum_crack_prayer", "whisper")],
    "antechamber": [("ev_antechamber_mirror_self", "relic"),
                    ("ev_antechamber_old_sword_rack", "cache"),
                    ("ev_antechamber_liora_voice", "whisper")],
    "peak": [("ev_peak_bone_field", "relic"), ("ev_peak_star_crack", "whisper"),
             ("ev_peak_wushang_memory", "relic")],
}
COMMON_POOL = [                     # section 12.1's kinds, none of them act-specific
    ("ev_common_relic_road", "relic"), ("ev_common_whisper_old_road", "whisper"),
    ("ev_common_cache_wall", "cache"), ("ev_common_trap_rubble", "trap"),
    ("ev_common_rescue_lost_soldier", "rescue"),
    ("ev_common_merchant_echo_ash", "merchant_echo"),
    ("ev_common_shard_wandering", "memory_shard"),
]


def load_footprint_tiers():
    """kind -> (w, h) from data/footprints.json (S1's character-level table). The generator must
    reserve a whole footprint (ART_AND_ABILITY_DESIGN.md F7), so it needs the same tiers the game
    applies at load time."""
    try:
        with open(FOOTPRINTS, encoding='utf-8') as f:
            raw = json.load(f)
    except FileNotFoundError:
        return {}
    tiers = {}
    for kind, spec in raw.items():
        if kind.startswith('_') or not isinstance(spec, dict):
            continue
        tiers[kind] = (int(spec.get('w', 1)), int(spec.get('h', 1)))
    return tiers


def load_chapter(act_no):
    """The authored chapter file, when the story phase has written it (S3 writes ch_01..ch_03).

    STORY_DATA_SCHEMA.md section 6.2's pseudocode reads act["enemyMix"] / act["itemTable"] from the
    chapter, so a chapter that exists overrides the provisional tables below -- the same pattern as
    the event pools. Returns {} when the chapter is absent."""
    path = os.path.join(ROOT, 'data', 'story', 'chapters', 'ch_%02d.json' % act_no)
    if not os.path.exists(path):
        return {}
    try:
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    except Exception:
        return {}


def event_pool(theme):
    """Act pool (seeded from the doc) + generated variants + the common pool, each tagged by kind."""
    pool = []
    for eid, kind in POOL_SEEDS[theme]:
        pool.append((eid, kind))
    for n in range(2, 5):                       # variants so a floor of 12 events can fill out
        for eid, kind in POOL_SEEDS[theme]:
            pool.append(("%s_v%d" % (eid, n), kind))
    for eid, kind in COMMON_POOL:
        pool.append((eid, kind))
    for n in range(2, 6):
        for eid, kind in COMMON_POOL:
            pool.append(("%s_v%d" % (eid, n), kind))
    return pool


def load_event_pools(theme, act_no):
    """Prefer the authored pools when the S3 phase has created them."""
    act_pool = os.path.join(EVENTS_DIR, 'pool_act%02d.json' % act_no)
    common = os.path.join(EVENTS_DIR, 'pool_common.json')
    if os.path.exists(act_pool) and os.path.exists(common):
        out = []
        for path in (act_pool, common):
            with open(path, encoding='utf-8') as f:
                for entry in json.load(f).get('events', []):
                    out.append((entry['eventId'], entry['kind']))
        if out:
            return out, 'authored'
    return event_pool(theme), 'provisional'


# ---------------------------------------------------------------------------------------------
# Maze assembly
# ---------------------------------------------------------------------------------------------


def _size_ramp():
    """Tile dims for EVERY floor (owner, 2026-09-13: "each level should have different grid size ...
    higher level should have more grids, only boss level not follow this rule").

    44 column steps + 26 row steps = 70 increments spread over the 69 steps from F01 to F70, so every
    floor gets at least one increment (one floor gets two) and NO two floors share a size: F01 19x16,
    F02 20x16, F03 20x17, ... F70 63x42 -- the same tower span the design already had. The generator
    pads a grid whose target is off its 3c+1 x 3r+1 cell lattice (at most ~2 tiles of unused wall at
    the far edge), which is what makes an arbitrary monotonic ramp legal.

    Boss floors are exempt by construction: the ten act-boss floors have no generated grid at all,
    they load their hand-authored data/stages/stageNN.json map.
    """
    sizes = [(19, 16)]
    cols, rows, left_c, left_r, acc = 19, 16, 44, 26, 0
    for _ in range(69):
        acc += 70
        while acc >= 69 and (left_c or left_r):
            acc -= 69
            if left_c and (not left_r or left_c * 26 >= left_r * 44):
                cols += 1; left_c -= 1
            else:
                rows += 1; left_r -= 1
        sizes.append((cols, rows))
    return sizes


SIZE_RAMP = _size_ramp()
assert len(SIZE_RAMP) == 70 and SIZE_RAMP[0] == (19, 16) and SIZE_RAMP[-1] == (63, 42), SIZE_RAMP[-1]
assert len(set(SIZE_RAMP)) == 70, 'every floor must have its own size'
assert all(SIZE_RAMP[i][0] >= SIZE_RAMP[i-1][0] and SIZE_RAMP[i][1] >= SIZE_RAMP[i-1][1]
           and SIZE_RAMP[i] != SIZE_RAMP[i-1] for i in range(1, 70)), 'must strictly grow'


def tile_counts(floor_no):
    """Per-floor tile dims -- replaces the per-act table (all seven floors of an act shared one size,
    which the owner rejected). FLOOR_TABLE's cols/rows stay as the act's reference size; its other
    columns (rooms/loops/events/enemy mix/items) are unchanged."""
    return SIZE_RAMP[min(max(floor_no - 1, 0), 69)]


def cell_dims_for(cols, rows):
    """Largest cell grid whose tile footprint fits the table's dims. Cells are SxS with a single
    wall between neighbours, so W = cells*(S+1)+1. The remainder is wall padding on the right/bottom
    edge, which keeps the table's dims exact (validator V9) without touching the maze interior."""
    ccols = max(3, (cols - 1) // (CELL_SCALE + 1))
    crows = max(3, (rows - 1) // (CELL_SCALE + 1))
    return ccols, crows


def carve_rooms(grid, cols, rows, room_cells):
    """Open a room: for every cell in the set, open the wall strips to its in-room neighbours."""
    for cell in room_cells:
        r, c = cell
        for nr, nc in neighbors(cell, cols, rows):
            if (nr, nc) not in room_cells:
                continue
            strip = edge_strip(frozenset((cell, (nr, nc))), CELL_SCALE)
            for x, y in strip:
                grid[y][x] = '.'


def open_loops(grid, edges, cells, cols, rows, count, rng):
    """Open `count` extra walls between adjacent cells that the spanning tree did NOT connect:
    pure perfect mazes are miserable over 70 floors (section 5.1's 'loops' rule)."""
    candidates = []
    for r in range(rows):
        for c in range(cols):
            for nr, nc in neighbors((r, c), cols, rows):
                e = frozenset(((r, c), (nr, nc)))
                if e not in edges:
                    candidates.append(e)
    rng.shuffle(candidates)
    opened = []
    for e in candidates[:count]:
        for x, y in edge_strip(e, CELL_SCALE):
            grid[y][x] = '.'
        opened.append(e)
    return opened


def make_grid(cols, rows, rooms, loops, seed):
    """Returns (grid, tiles_dims, entrance_cell, goal_cell, room_cells, edges, adj)."""
    ccols, crows = cell_dims_for(cols, rows)
    rng = random.Random(seed)
    edges = wilson_maze(ccols, crows, rng)
    adj = build_adjacency(edges, ccols, crows)

    # tile grid: SxS cell blocks + 1-tile walls, then padded with wall rows/cols to the exact dims
    w0 = ccols * (CELL_SCALE + 1) + 1
    h0 = crows * (CELL_SCALE + 1) + 1
    grid = [['#'] * cols for _ in range(rows)]
    for r in range(crows):
        for c in range(ccols):
            x0, y0 = cell_block((r, c), CELL_SCALE)
            for dy in range(CELL_SCALE):
                for dx in range(CELL_SCALE):
                    grid[y0 + dy][x0 + dx] = '.'
    for e in edges:
        for x, y in edge_strip(e, CELL_SCALE):
            grid[y][x] = '.'

    entrance = (crows - 1, 0)
    dist, parent = bfs(adj, entrance)
    goal = max(dist, key=lambda k: dist[k])

    # rooms: grow `rooms` clusters of 2x2 cells (disjoint), each carved open -- they are the
    # "event containers" the floor table counts.
    all_cells = [(r, c) for r in range(crows) for c in range(ccols)]
    rng.shuffle(all_cells)
    room_cells = []
    used_room_cells = set()
    for start in all_cells:
        if len(room_cells) >= rooms:
            break
        r, c = start
        if r + 1 >= crows or c + 1 >= ccols:
            continue
        cluster = {(r, c), (r, c + 1), (r + 1, c), (r + 1, c + 1)}
        if cluster & used_room_cells:
            continue
        room_cells.append(cluster)
        used_room_cells |= cluster
    for cluster in room_cells:
        carve_rooms(grid, ccols, crows, cluster)

    open_loops(grid, edges, cells=all_cells, cols=ccols, rows=crows, count=loops, rng=rng)
    return grid, (w0, h0), entrance, goal, room_cells, edges, adj


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--floors', default='1-70', help='e.g. 1-70 or 33 or 7,14,70')
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args()

    wanted = []
    for part in args.floors.split(','):
        if '-' in part:
            a, b = part.split('-')
            wanted.extend(range(int(a), int(b) + 1))
        else:
            wanted.append(int(part))
    wanted = [f for f in wanted if 1 <= f <= 70]

    tiers = load_footprint_tiers()
    os.makedirs(FLOORS_DIR, exist_ok=True)
    summary = Counter()
    for floor_no in wanted:
        tier = min((floor_no - 1) // 7, 9)
        cols, rows, rooms, loops, n_events, emin, emax, elites, n_items = FLOOR_TABLE[tier]
        cols, rows = tile_counts(floor_no)   # per-floor size; the row's dims are the act's reference
        act_id, seal, boss_id, ss_id, theme = ACTS[tier]
        index_in_act = (floor_no - 1) % 7 + 1
        is_boss = (floor_no % 7 == 0)
        role = 'boss-stage' if is_boss else ('side-story-stage' if index_in_act == 5 else 'normal')
        seed = '%02d:toms-s2' % floor_no
        # NOT python's hash(): it is randomized per process (PYTHONHASHSEED), which silently made
        # every run produce different floors -- the opposite of what the doc's "seed": "auto" is for
        # (a save must name a floor and get the same maze back). FNV-1a, the same stable hash the
        # roamer seeds use on the C++ side.
        rng = random.Random(_stable_hash(seed))

        assert (cols, rows) == tile_counts(floor_no), floor_no

        chapter = load_chapter(tier + 1)
        act_mix = chapter.get('enemyMix') or MIX_BY_ACT[tier]
        act_items = chapter.get('itemTable') or ITEM_TABLE_BY_ACT[tier]

        spec = {
            "id": "F%02d" % floor_no,
            "act": act_id,
            "indexInAct": index_in_act,
            "name": "story.f%02d.name" % floor_no,
            "seal": seal,
            "role": role,
            "maze": {"cols": cols, "rows": rows, "rooms": rooms, "loops": loops, "seed": seed},
            "enemies": {
                "mix": act_mix,
                "count": [emin, emax],
                "elites": elites,
                "boss": boss_id if is_boss else None,
            },
            "items": {"count": n_items, "table": act_items},
            "events": [],
            "sideStoryHooks": [],
            "story": {
                "chapter": act_id,
                "intro": "story.f%02d.intro" % floor_no,
                "ambient": ["story.f%02d.ambient.1" % floor_no, "story.f%02d.ambient.2" % floor_no],
                "beats": ["b_%s_f%02d" % (theme, floor_no)],
                "exitUnlocks": {"counters": {"insight": 1}} if index_in_act == 3 else {},
            },
            "nextFloor": ("F%02d" % (floor_no + 1)) if floor_no < 70 else None,
            "meta": {
                "targetMinutes": [2, 4] if floor_no <= 21 else ([4, 7] if floor_no <= 49 else [6, 10]),
                "difficultyTier": tier + 1,
            },
        }
        if is_boss:
            spec['handAuthoredStage'] = BOSS_STAGE[floor_no]

        # ---- event sampling (section 5.2): no repeats, >=1 relic/whisper, >=1 cache ----
        pool, pool_source = load_event_pools(theme, tier + 1)
        picks = []
        def add(kinds_wanted, n):
            for _ in range(n):
                for _try in range(200):
                    cand = pool[rng.randrange(len(pool))]
                    if cand[0] in [p[0] for p in picks]:
                        continue
                    if kinds_wanted and cand[1] not in kinds_wanted:
                        continue
                    picks.append(cand)
                    break
        add({"relic", "whisper"}, 1)
        add({"cache"}, 1)
        add(None, max(0, n_events - len(picks)))
        spec['events'] = [p[0] for p in picks]
        spec['meta']['eventKinds'] = [p[1] for p in picks]
        spec['meta']['eventPoolSource'] = pool_source
        spec['meta']['contentSource'] = 'chapter' if chapter else 'provisional'
        spec['meta']['enemyMix'] = act_mix

        # ---- side story hook: this act's flagship story sits on its 5th floor (bible section 6) ----
        if index_in_act == 5:
            spec['sideStoryHooks'] = [{
                "sideStoryId": ss_id,
                "cell": None,           # filled below from the generated grid
                "trigger": "proximity",
                "requires": {"type": "storyBeatAtLeast", "value": act_id},
            }]

        grid = None
        if not is_boss:
            grid, _dims, entrance, goal, room_cells, edges, adj = make_grid(
                cols, rows, rooms, loops, seed)

            # ---- entity placement, footprint-aware (ART_AND_ABILITY_DESIGN.md F7) ----
            used = set()
            placements = {}      # cell -> char

            def free_cluster(cells_taken, cell, need_w, need_h):
                """A big enemy must fit entirely on floor tiles inside the OTHER cells it needs;
                with 2-tile passages a 2x1 needs its cell plus one neighbour of room, which the
                room carving guarantees for room cells."""
                return cell not in cells_taken

            def cell_room(grid, cell):
                """Tile rect of a cell, and whether it is entirely floor (room cells are)."""
                x0, y0 = cell_block(cell, CELL_SCALE)
                ok = all(grid[y0 + dy][x0 + dx] == '.'
                         for dy in range(CELL_SCALE) for dx in range(CELL_SCALE))
                return (x0, y0), ok

            main_path = path_to(bfs(adj, entrance)[1], entrance, goal)
            order = [c for c in main_path] + [c for c in sorted(adj) if c not in main_path]

            def take_cell(prefer_room=True):
                if prefer_room:
                    for cluster in room_cells:
                        for c in sorted(cluster):
                            if c not in used:
                                return c
                for c in order:
                    if c not in used:
                        return c
                return None

            def tile_char_for(cell, ch):
                x0, y0 = cell_block(cell, CELL_SCALE)
                return x0, y0

            # entrance marker + upstairs (goal) + downstairs (next to the entrance)
            placements[entrance] = '@'
            used.add(entrance)
            placements[goal] = 'U'
            used.add(goal)
            back = next(c for c in adj[entrance] if c not in used)
            placements[back] = 'D'
            used.add(back)

            roster = []
            mix = act_mix
            n_enemies = emin + (floor_no % (emax - emin + 1))
            for i in range(n_enemies):
                roster.append(mix[i % len(mix)])
            for i in range(elites):
                roster.append(mix[(i + 1) % len(mix)])

            # char codes: reuse the shipped legend convention (1..6 monsters, Z boss, a/h items...)
            MON_CHARS = ["1", "2", "3", "4", "5", "6"]
            mon_by_id = {}
            legend = {
                "#": "wall", ".": "floor", "@": "player_start",
                "U": "stairs_up", "D": "stairs_down",
            }
            for idx, mid in enumerate(sorted(set(roster))):
                ch = MON_CHARS[idx % len(MON_CHARS)]
                mon_by_id[mid] = ch
                legend[ch] = "monster:" + mid
            ITEM_CHARS = ["a", "d", "h", "H", "c", "x", "X"]
            item_table = act_items
            item_by_id = {}
            for idx, iid in enumerate(sorted(set(item_table))):
                ch = ITEM_CHARS[idx % len(ITEM_CHARS)]
                item_by_id[iid] = ch
                legend[ch] = "item:" + iid

            # monsters first (they are the ones with footprint constraints), then items/events
            big_placed = 0
            for mid in roster:
                cell = take_cell(prefer_room=False)
                if cell is None:
                    break
                placements[cell] = mon_by_id[mid]
                used.add(cell)
                if tiers.get('monster:' + mid, (1, 1)) != (1, 1):
                    big_placed += 1
            for i in range(n_items):
                iid = item_table[i % len(item_table)]
                cell = take_cell(prefer_room=True)
                if cell is None:
                    break
                placements[cell] = item_by_id[iid]
                used.add(cell)

            # events occupy room cells: they are the "event containers" the table counts
            # S3.5 (d): each event slot becomes a real marker the engine can see (legend kind
            # "event:<id>"), so a floor's story content is actually in the world instead of being a
            # list of ids in the spec. One char per slot -- the legend is per floor, so slots can
            # share nothing and every cell keeps its own event id.
            EVENT_CHARS = ["E", "F", "G", "I", "J", "K", "L", "M", "N", "O", "P", "Q"]
            event_cells = []
            for idx, ev_id in enumerate(spec['events']):
                cell = take_cell(prefer_room=True)
                if cell is None:
                    break
                event_cells.append(cell)
                used.add(cell)
                ch = EVENT_CHARS[idx % len(EVENT_CHARS)]
                placements[cell] = ch
                legend[ch] = "event:" + ev_id
            spec['meta']['eventCells'] = [list(c) for c in event_cells]
            # one door + its key, gating the goal side of the main path (same idea as gen_mazes.py)
            if len(main_path) >= 4:
                door_edge = frozenset((main_path[len(main_path) // 2], main_path[len(main_path) // 2 + 1]))
                # The key MUST be reachable from the entrance with the door still shut, otherwise the
                # floor is a softlock. Same rule (and same helper) as gen_mazes.py: take the
                # entrance-side component of the spanning tree once the door edge is removed.
                near_side = reachable_without_edge(adj, entrance, door_edge)
                key_cell = next((c for c in main_path if c in near_side and c not in used), None)
                if key_cell is None:
                    key_cell = next((c for c in sorted(near_side) if c not in used), None)
                if key_cell is not None:
                    placements[key_cell] = 'Y'
                    used.add(key_cell)
                    for x, y in edge_strip(door_edge, CELL_SCALE):
                        grid[y][x] = 'y'
                    legend['Y'] = 'key:yellow'
                    legend['y'] = 'door:yellow'

            if spec['sideStoryHooks']:
                cs = spec['sideStoryHooks'][0]
                hook_cell = event_cells[0] if event_cells else (next(iter(used)))
                cs['cell'] = [cell_center_xy(hook_cell, CELL_SCALE)[0],
                              cell_center_xy(hook_cell, CELL_SCALE)[1]]

            # stamp entity chars into the tile grid
            for cell, ch in placements.items():
                x0, y0 = cell_block(cell, CELL_SCALE)
                grid[y0][x0] = ch

            tiles = [''.join(r) for r in grid]
            assert all(len(t) == cols for t in tiles), floor_no
            assert len(tiles) == rows, floor_no

            stage = {
                "id": spec['id'],
                "name": {"zh_TW": "第 %d 層" % floor_no, "en": "Floor %d" % floor_no},
                "subtitle": "F%02d" % floor_no,
                "index": floor_no,
                "width": cols, "height": rows,
                "legend": legend,
                "tiles": tiles,
                "story_note": {"zh_TW": "第 %d 層（%s）" % (floor_no, act_id),
                               "en": "Floor %d (%s)" % (floor_no, act_id)},
                "preview": {"zh_TW": "第 %d 層 · 難度階 %d" % (floor_no, tier + 1),
                            "en": "Floor %d - tier %d" % (floor_no, tier + 1)},
                "connect": {"up": spec['nextFloor'],
                            "down": ("F%02d" % (floor_no - 1)) if floor_no > 1 else None},
                "footprints": {ch: [tiers['monster:' + mid][0], tiers['monster:' + mid][1]]
                               for mid, ch in mon_by_id.items()
                               if 'monster:' + mid in tiers
                               and tiers['monster:' + mid] != (1, 1)},
            }
            with open(os.path.join(FLOORS_DIR, spec['id'] + '.stage.json'), 'w', encoding='utf-8') as f:
                json.dump(stage, f, ensure_ascii=False, indent=2)
                f.write('\n')
            summary['stage_files'] += 1
            summary['big_enemies'] += big_placed
            if args.verbose:
                print('  %s %s %dx%d rooms=%d loops=%d events=%d enemies=%d(%d big) items=%d%s'
                      % (spec['id'], role, cols, rows, rooms, loops, len(spec['events']),
                         n_enemies, big_placed, n_items, ' [boss: hand-authored]' if is_boss else ''))
        else:
            summary['boss_floors'] += 1

        with open(os.path.join(FLOORS_DIR, spec['id'] + '.json'), 'w', encoding='utf-8') as f:
            json.dump(spec, f, ensure_ascii=False, indent=2)
            f.write('\n')
        summary['specs'] += 1

    print('gen_floors: %d spec(s), %d generated grid(s) (+%d boss floors -> hand-authored stages), '
          '%d multi-grid enemy placements'
          % (summary['specs'], summary['stage_files'], summary['boss_floors'], summary['big_enemies']))
    print('next: python3 tools/validate_story.py')


if __name__ == '__main__':
    main()
