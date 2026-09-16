#!/usr/bin/env python3
"""gen_stairs.py -- regenerate every floor's maze SHAPE (not its content) so that consecutive
floors' stairs physically line up, and wire the engine to spawn the player at that exact tile
when arriving via stairs instead of always jumping to a fixed default spot.

Why this exists (owner, 2026-09-16): today each of the 70 floors' up/down stairs are placed
independently -- floor N's "stairs_up" tile has no relationship to floor N+1's "stairs_down"
tile, so a player who climbs the stairs and a player who walks in fresh land on the exact same
spot only by coincidence. This script makes them the SAME (x, y) tile on purpose: floor N's
up-stair position becomes the forced entry point for floor N+1's maze, whose own down-stair is
placed exactly there. See docs/story/STAIR_ALIGNMENT.md for the full rule this implements.

Scope (owner-confirmed):
  - ALL 70 floors: the 60 procedural grids (data/story/floors/F01.stage.json..F70.stage.json,
    skipping the 10 boss floors) AND the 10 hand-authored boss stages
    (data/stages/stage01.json..stage10.json) that sit at F07/F14/.../F70. `stage_11.json` is
    NOT part of this chain -- it's a standalone epilogue reached only via a boss-defeat warp
    (Game::finishCombatWin), never by walking stairs, so it has no "position to align" at all.
  - Floor SIZES are UNCHANGED (owner: "keep current growth curve, just fix stairs") -- this
    script reads each file's existing width/height and regenerates a maze that exactly fills it.
  - Each floor's ROSTER is UNCHANGED (owner: "keep same roster per floor, only reshuffle
    layout") -- every monster/item/NPC/event/door/key character already in the file, and how
    many of each, is preserved exactly; only WHERE they sit (and where the stairs are) changes.

Algorithm, in one paragraph: floors are regenerated in strict order F01 -> F70. Floor N's maze is
a fresh Wilson's-algorithm spanning tree over its own (unchanged) cell grid. Its "entrance" cell
is normally a fixed corner, UNLESS a previous floor handed down a forced cell (the exact coordinate
its own up-stair landed on) -- entrance cells are just (row, col) tuples and a cell's rendered
tile position depends only on (row, col, scale), never on the grid's overall size, so a coordinate
that was valid on a smaller floor is still valid on this (same-or-larger) one. The stairs_down
tile (if this floor has one -- every floor except F01) goes exactly at that entrance. The floor's
own "goal" -- the tree cell farthest from the entrance -- gets stairs_up (or, on F70 only, the
floor's actual boss monster instead, matching the existing convention), and that goal cell is
handed forward as the NEXT floor's forced entrance. Every other character already on the floor
(monsters, items, NPCs, event markers, one door+key pair) is redistributed onto the new maze by
the same rules tools/gen_mazes.py already established: the key must be reachable with the door
still shut, everything else spreads across the remaining free cells.

Run from the repo root: python3 tools/gen_stairs.py
"""
import json
import os
import random
import sys
from collections import Counter, deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from gen_mazes import (cell_block, cell_center_xy, edge_strip, wilson_maze, neighbors,
                       build_adjacency, bfs, path_to, reachable_without_edge)

FLOORS_DIR = os.path.join(ROOT, "data", "story", "floors")
STAGES_DIR = os.path.join(ROOT, "data", "stages")
FOOTPRINTS_PATH = os.path.join(ROOT, "data", "footprints.json")

SEED = 20260916  # deterministic, distinct from gen_mazes.py's/gen_floors.py's own seeds
SCALE = 2        # tiles per maze-cell side; matches every existing generator

with open(FOOTPRINTS_PATH, encoding="utf-8") as _f:
    _FOOTPRINT_TIERS = {k: (v["w"], v["h"]) for k, v in json.load(_f).items() if not k.startswith("_")}


def footprint_of(kind):
    """(w, h) for a legend kind (e.g. "monster:golem") -- 1x1 unless footprints.json says
    otherwise. Every legal size (1x1/2x1/1x2/2x2, per that file's own comment) fits inside one
    maze cell (SCALE x SCALE = 2x2), so placing a "big" entity at its cell's own top-left corner
    (cell_block) rather than the center tile (cell_center_xy) keeps its WHOLE footprint on
    guaranteed floor, never spilling into the wall between cells or a neighboring cell's own
    entity -- exactly what footprint_test.cpp's F7 checks require."""
    return _FOOTPRINT_TIERS.get(kind, (1, 1))

BOSS_STAGE = {
    7: "stage01.json", 14: "stage02.json", 21: "stage03.json", 28: "stage04.json",
    35: "stage05.json", 42: "stage06.json", 49: "stage07.json", 56: "stage08.json",
    63: "stage09.json", 70: "stage10.json",
}


def path_for(floor_no):
    if floor_no in BOSS_STAGE:
        return os.path.join(STAGES_DIR, BOSS_STAGE[floor_no])
    return os.path.join(FLOORS_DIR, "F%02d.stage.json" % floor_no)


def cell_dims_for(width, height):
    """Inverse of cell_block's WxH formula (see gen_floors.py's own cell_dims_for docstring): the
    largest cell grid whose SxS-block footprint fits inside an already-declared width/height,
    tolerating the couple of tiles of wall padding a non-lattice-exact size leaves on the far
    edge."""
    ccols = max(3, (width - 1) // (SCALE + 1))
    crows = max(3, (height - 1) // (SCALE + 1))
    return ccols, crows


def regenerate(path, forced_entry, rng, is_first, is_last, next_dims=None):
    """Rebuilds one stage file's maze in place. `forced_entry` is the (row, col) cell this
    floor's stairs_down must land on (None for the very first floor, which has none). Returns
    the (row, col) cell this floor's stairs_up/boss landed on, to become the next floor's
    forced_entry -- or None for the last floor (no floor above it).

    `next_dims`, if given, is the next floor's own (ccols, crows) -- the hand-authored boss
    stages do NOT grow in lockstep with the procedural size ramp (e.g. F06 is a bigger cell grid
    than F07/stage01 right after it), so the candidate for THIS floor's own goal/up-stair cell
    must be restricted to whatever also fits inside the SMALLER of the two floors, or the forced
    hand-off to the next floor would land out of its bounds."""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    width, height = data["width"], data["height"]
    old_legend = data["legend"]
    old_tiles = data["tiles"]

    # ---- read the existing roster, minus the structural chars this script itself decides ----
    roster = Counter(ch for row in old_tiles for ch in row if ch not in ("#", ".", "@", "U", "D"))
    boss_char = "Z" if "Z" in roster else None
    if boss_char:
        del roster[boss_char]
    door_char = next((ch for ch in roster if old_legend.get(ch, "").startswith("door:")), None)
    key_char = next((ch for ch in roster if old_legend.get(ch, "").startswith("key:")), None)
    if door_char:
        del roster[door_char]
    if key_char:
        del roster[key_char]
    # everything left (monsters/items/npc/event markers) -- one entry per actual instance, same
    # repeat-aware handling gen_mazes.py already established (a char can mean N>1 instances).
    other_instances = [ch for ch in sorted(roster) for _ in range(roster[ch])]

    ccols, crows = cell_dims_for(width, height)
    edges = wilson_maze(ccols, crows, rng)
    adj = build_adjacency(edges, ccols, crows)

    entrance = forced_entry if forced_entry is not None else (crows - 1, 0)
    assert 0 <= entrance[0] < crows and 0 <= entrance[1] < ccols, \
        "%s: inherited entry cell %r doesn't fit this floor's %dx%d cell grid" % (path, entrance, ccols, crows)

    dist, parent = bfs(adj, entrance)
    if next_dims is not None:
        next_ccols, next_crows = next_dims
        candidates = [c for c in dist if c[0] < next_crows and c[1] < next_ccols]
        assert candidates, "%s: no cell fits inside the next floor's smaller %dx%d grid" % (path, next_ccols, next_crows)
        goal = max(candidates, key=lambda k: dist[k])
    else:
        goal = max(dist, key=lambda k: dist[k])
    main_path = path_to(parent, entrance, goal)

    used = {entrance, goal}
    placements = {}

    if not is_first:
        placements[entrance] = "D"
    else:
        placements[entrance] = "@"
    if boss_char:
        placements[goal] = boss_char
    elif not is_last:
        placements[goal] = "U"
    # else (is_last, no boss): goal stays a plain floor cell -- F70's own case never reaches
    # here (stage10 always has 'Z'), kept only so a future all-floors-generic floor doesn't crash.

    # Door/key: gate an edge on the main path, key on the entrance-side sub-tree (same rule as
    # both existing generators -- the key must be winnable with the door still shut).
    door_strip = []
    door_edge = None
    if door_char and key_char and len(main_path) >= 2:
        path_edges = [frozenset((a, b)) for a, b in zip(main_path, main_path[1:])]
        door_edge = path_edges[len(path_edges) // 2]
        edges = set(edges)
        edges.discard(door_edge)
        near_side = reachable_without_edge(adj, entrance, door_edge)
        key_cell = next((c for c in main_path if c in near_side and c not in used), None)
        if key_cell is None:
            key_cell = next((c for c in sorted(near_side) if c not in used), None)
        if key_cell is not None:
            placements[key_cell] = key_char
            used.add(key_cell)
            door_strip = edge_strip(door_edge, SCALE)

    # Loops: a pure spanning tree has exactly ONE path between any two cells, so a single 2-wide
    # "big" monster (footprint.json) sitting on a corridor cell would seal off everything beyond
    # it -- gen_floors.py's own "loops" rule (section 5.1) exists for exactly this reason. Adds a
    # handful of extra, non-tree edges as alternate routes, never the door's own edge (a loop
    # bypassing the door would defeat the whole key/door puzzle).
    edges = set(edges)
    loop_candidates = []
    for r in range(crows):
        for c in range(ccols):
            for nr, nc in neighbors((r, c), ccols, crows):
                e = frozenset(((r, c), (nr, nc)))
                if e not in edges and e != door_edge:
                    loop_candidates.append(e)
    rng.shuffle(loop_candidates)
    n_big = sum(1 for ch in other_instances if footprint_of(old_legend.get(ch, "")) != (1, 1))
    loop_count = max(2, (ccols * crows) // 10) + 3 * n_big
    edges.update(loop_candidates[:loop_count])

    # Everything else: spread along the main path first (falls back to any free cell), same
    # distribution gen_mazes.py already uses for its own monster/item placement. A "big"
    # (footprint > 1x1) entity fully occupies whichever cell it lands on -- if that cell is an
    # articulation point of the loop-augmented graph (its only route to the goal), the floor
    # softlocks, so those get an extra reachability check the small (1x1) ones don't need.
    full_adj = build_adjacency(edges, ccols, crows)
    blocked_big_cells = set()   # every big cell placed SO FAR -- their combined removal must
                                 # still leave entrance able to reach goal, not just each alone

    def would_disconnect(cell):
        blocked = blocked_big_cells | {cell}
        seen = {entrance}
        dq = deque([entrance])
        while dq:
            u = dq.popleft()
            if u == goal:
                return False
            for v in full_adj[u]:
                if v in blocked or v in seen:
                    continue
                seen.add(v)
                dq.append(v)
        return True

    interior = [c for c in main_path[1:-1] if c not in used]
    all_cells = [(r, c) for r in range(crows) for c in range(ccols)]
    big_instances = [ch for ch in other_instances if footprint_of(old_legend.get(ch, "")) != (1, 1)]
    small_instances = [ch for ch in other_instances if footprint_of(old_legend.get(ch, "")) == (1, 1)]

    for ch in big_instances:
        ordered = [c for c in interior if c not in used] + [c for c in all_cells if c not in used]
        cell = next((c for c in ordered if not would_disconnect(c)), None)
        if cell is None:
            cell = next((c for c in ordered), None)   # no safe cell at all -- fall back, let the
                                                        # solvability check below catch it loudly
        assert cell is not None, "%s: ran out of free cells placing %r" % (path, ch)
        placements[cell] = ch
        used.add(cell)
        blocked_big_cells.add(cell)

    n = len(small_instances)
    for i, ch in enumerate(small_instances):
        cell = None
        if interior:
            idx = min(len(interior) - 1, (i * len(interior)) // max(1, n))
            if interior[idx] not in used:
                cell = interior[idx]
        if cell is None:
            cell = next((c for c in interior if c not in used), None)
        if cell is None:
            cell = next((c for c in all_cells if c not in used), None)
        assert cell is not None, "%s: ran out of free cells placing %r" % (path, ch)
        placements[cell] = ch
        used.add(cell)

    # ---- render ----
    grid = [["#"] * width for _ in range(height)]
    for r in range(crows):
        for c in range(ccols):
            x0, y0 = cell_block((r, c), SCALE)
            for dy in range(SCALE):
                for dx in range(SCALE):
                    grid[y0 + dy][x0 + dx] = "."
    for e in edges:
        for x, y in edge_strip(e, SCALE):
            grid[y][x] = "."
    for x, y in door_strip:
        grid[y][x] = door_char
    for cell, ch in placements.items():
        w, h = footprint_of(old_legend.get(ch, ""))
        x, y = (cell_block(cell, SCALE) if (w, h) != (1, 1) else cell_center_xy(cell, SCALE))
        grid[y][x] = ch

    tiles = ["".join(row) for row in grid]
    assert len(tiles) == height and all(len(t) == width for t in tiles), path

    # ---- roster-preservation check ----
    expected = Counter(other_instances)
    if boss_char:
        expected[boss_char] = 1
    if door_char:
        expected[door_char] = SCALE
    if key_char:
        expected[key_char] = 1
    if not is_first:
        expected["D"] = 1
    else:
        expected["@"] = 1
    if boss_char is None and not is_last:
        expected["U"] = 1
    new_roster = Counter(ch for row in tiles for ch in row if ch not in ("#", "."))
    assert new_roster == expected, "%s: roster mismatch\n old(expected)=%r\n new=%r" % (path, expected, new_roster)

    # ---- solvability: every placement reachable from the entrance tile ----
    ex, ey = cell_center_xy(entrance, SCALE)
    seen = {(ex, ey)}
    q = deque([(ex, ey)])
    while q:
        x, y = q.popleft()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < width and 0 <= ny < height and (nx, ny) not in seen and tiles[ny][nx] != "#":
                seen.add((nx, ny))
                q.append((nx, ny))
    for cell in placements:
        cx, cy = cell_center_xy(cell, SCALE)
        assert (cx, cy) in seen, "%s: placement at cell %r (tile %d,%d) unreachable" % (path, cell, cx, cy)

    data["tiles"] = tiles
    data["legend"] = old_legend
    data["width"], data["height"] = width, height
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")

    up_x, up_y = cell_center_xy(goal, SCALE)
    return (None if (boss_char is None and is_last) else goal), (up_x, up_y)


def main():
    rng = random.Random(SEED)
    # Precompute every floor's own (unchanged) cell-grid dims up front: the hand-authored boss
    # stages don't grow in lockstep with the procedural ramp, so generating floor N needs to know
    # floor N+1's size in advance (see regenerate()'s `next_dims` docstring).
    dims = {}
    for floor_no in range(1, 71):
        with open(path_for(floor_no), encoding="utf-8") as f:
            d = json.load(f)
        dims[floor_no] = cell_dims_for(d["width"], d["height"])

    forced_entry = None
    prev_up_tile = None
    for floor_no in range(1, 71):
        path = path_for(floor_no)
        is_first = (floor_no == 1)
        is_last = (floor_no == 70)
        next_dims = dims.get(floor_no + 1)
        goal_cell, up_tile = regenerate(path, forced_entry, rng, is_first, is_last, next_dims)
        if forced_entry is not None:
            down_x, down_y = cell_center_xy(forced_entry, SCALE)
            assert (down_x, down_y) == prev_up_tile, \
                "F%02d: down-stair %r != previous floor's up-stair %r" % (floor_no, (down_x, down_y), prev_up_tile)
        print("F%02d (%s): entry->stairs_down %s, exit->stairs_up/boss tile %s" %
              (floor_no, os.path.basename(path), forced_entry, up_tile))
        forced_entry = goal_cell
        prev_up_tile = up_tile


if __name__ == "__main__":
    main()
