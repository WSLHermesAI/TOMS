#!/usr/bin/env python3
"""gen_mazes.py -- Milestone 9: regenerate every stage's maze layout using Wilson's
algorithm (a loop-erased random walk that produces an unbiased uniform spanning tree,
i.e. a "perfect maze" with exactly one path between any two cells).

Surgical, not a full regenerate: this script only rewrites each stage JSON's
`tiles`/`legend`/`width`/`height` fields. Every other field authored in earlier
milestones (id/name/subtitle/index/story_note/preview/encounter_overrides/connect)
is read from the existing file and written back unchanged. Do NOT confuse this with
gen_content.py, the original from-scratch content generator (hardcoded to a different
machine's path) -- running that would wipe out all of Milestone 8's authored content.

Design (owner-approved, see docs/PROGRESS_REPORT.md Milestone 9 log):
  - Maze *shape* is generated fresh per floor; the *entity roster* (same monster
    types/counts, same NPC, same keys/doors, same items) is preserved exactly from
    today's files and auto-placed onto the new layout by rule:
      - player_start '@' at a fixed corner cell.
      - the floor's "forward" transition (stairs_up, or the boss on stage10, since
        that's the floor's actual goal) at the cell farthest from '@' in the tree --
        every other cell lies on the way there, so reaching it means having explored
        (or fought through) most of the floor.
      - the "backward" transition (stairs_down), if present, adjacent to '@'.
      - the NPC (if present), a few steps from '@'.
      - monsters spread evenly along the unique '@'-to-goal path.
      - a door/key pair (if present): the door sits on an edge of that same path, and
        the key is placed in the sub-tree on the '@' side of that edge -- guaranteed
        reachable without the door, and the door provably gates everything beyond it,
        since a tree has no alternate route around any edge.
      - remaining items (gems/potions/coins) scattered on whatever cells are left.
  - Maze size grows with floor index (owner asked for "higher level -> bigger maze"):
    cell grid is (6 + (floor-1)//2) columns by (5 + (floor-1)//2) rows, so floor 1
    is 6x5 cells (identical to today's fixed 13x11 tile size -- no visual change on
    the tutorial floor) growing to 11x10 cells (23x21 tiles) by floor 11.

Run from the repo root: python3 tools/gen_mazes.py
"""
import json
import os
import random
from collections import deque

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGES_DIR = os.path.join(REPO_ROOT, "data", "stages")

# Deterministic across runs -- reproducible content, not re-rolled on every invocation.
SEED = 20260909

# (filename, floor_index) -- floor_index drives both maze size and is cross-checked
# against the file's own "index" field.
FLOOR_FILES = [
    ("stage01.json", 1), ("stage02.json", 2), ("stage03.json", 3), ("stage04.json", 4),
    ("stage05.json", 5), ("stage06.json", 6), ("stage07.json", 7), ("stage08.json", 8),
    ("stage09.json", 9), ("stage10.json", 10), ("stage_11.json", 11),
]


def cell_dims(floor_index):
    cols = 6 + (floor_index - 1) // 2
    rows = 5 + (floor_index - 1) // 2
    return cols, rows


def neighbors(cell, cols, rows):
    r, c = cell
    out = []
    if r > 0: out.append((r - 1, c))
    if r < rows - 1: out.append((r + 1, c))
    if c > 0: out.append((r, c - 1))
    if c < cols - 1: out.append((r, c + 1))
    return out


def wilson_maze(cols, rows, rng):
    """Loop-erased random walk maze generation (Wilson's algorithm). Returns a set of
    frozenset({cellA, cellB}) edges forming a uniform spanning tree over the grid."""
    cells = [(r, c) for r in range(rows) for c in range(cols)]
    in_maze = {rng.choice(cells)}
    edges = set()
    remaining = set(cells) - in_maze
    while remaining:
        walk_start = rng.choice(tuple(remaining))
        path = [walk_start]
        pos_index = {walk_start: 0}
        pos = walk_start
        while pos not in in_maze:
            nxt = rng.choice(neighbors(pos, cols, rows))
            if nxt in pos_index:
                # Loop-erase: truncate the walk back to the earlier visit.
                cut = pos_index[nxt]
                for stale in path[cut + 1:]:
                    del pos_index[stale]
                path = path[:cut + 1]
            else:
                path.append(nxt)
                pos_index[nxt] = len(path) - 1
            pos = nxt
        for a, b in zip(path, path[1:]):
            edges.add(frozenset((a, b)))
        in_maze.update(path)
        remaining -= set(path)
    return edges


def build_adjacency(edges, cols, rows):
    adj = {(r, c): [] for r in range(rows) for c in range(cols)}
    for e in edges:
        a, b = tuple(e)
        adj[a].append(b)
        adj[b].append(a)
    return adj


def bfs(adj, start):
    """Returns (dist, parent) maps from start over the tree (a tree has a unique
    path to every cell, so plain BFS gives the one-and-only route)."""
    dist = {start: 0}
    parent = {start: None}
    q = deque([start])
    while q:
        u = q.popleft()
        for v in adj[u]:
            if v not in dist:
                dist[v] = dist[u] + 1
                parent[v] = u
                q.append(v)
    return dist, parent


def path_to(parent, start, target):
    path = [target]
    while path[-1] != start:
        path.append(parent[path[-1]])
    path.reverse()
    return path


def reachable_without_edge(adj, start, blocked_edge):
    """Flood fill from start, refusing to cross blocked_edge -- used to find which
    cells are on the '@'-side of a door before the matching key can be placed."""
    seen = {start}
    q = deque([start])
    while q:
        u = q.popleft()
        for v in adj[u]:
            if frozenset((u, v)) == blocked_edge:
                continue
            if v not in seen:
                seen.add(v)
                q.append(v)
    return seen


def cell_to_xy(cell):
    r, c = cell
    return 2 * c + 1, 2 * r + 1


def edge_to_xy(edge):
    (r1, c1), (r2, c2) = tuple(edge)
    return c1 + c2 + 1, r1 + r2 + 1


def build_tiles(cols, rows, edges):
    w, h = 2 * cols + 1, 2 * rows + 1
    grid = [['#'] * w for _ in range(h)]
    for r in range(rows):
        for c in range(cols):
            grid[2 * r + 1][2 * c + 1] = '.'
    for e in edges:
        x, y = edge_to_xy(e)
        grid[y][x] = '.'
    return grid, w, h


def generate_floor(filename, floor_index, rng):
    path = os.path.join(STAGES_DIR, filename)
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    assert data["index"] == floor_index, f"{filename}: index mismatch"

    # ---- read the existing roster: what has to be re-placed, unchanged in kind/count ----
    old_legend = data["legend"]
    from collections import Counter
    roster = Counter(ch for row in data["tiles"] for ch in row if ch not in ("#", ".", "@"))

    has_up = "U" in roster
    has_down = "D" in roster
    # Milestone 9 bugfix (caught before ever running this against real data): the same
    # legend char is reused for every instance of one monster type on a floor (e.g.
    # stage01's '1' is TWO slimes, not one) -- roster[ch] is the real instance count,
    # so monster/NPC placement must repeat the char that many times, not once per
    # distinct char. Items and doors/keys never repeat a char in any shipped floor, so
    # those stay singular.
    monster_instances = [ch for ch in sorted(roster) if old_legend.get(ch, "").startswith("monster:") and ch != "Z"
                          for _ in range(roster[ch])]
    boss_char = "Z" if "Z" in roster else None
    npc_chars = [ch for ch in sorted(roster) if old_legend.get(ch, "").startswith("npc:")
                 for _ in range(roster[ch])]
    item_chars = sorted(ch for ch in roster if old_legend.get(ch, "").startswith("item:"))
    door_char = next((ch for ch in roster if old_legend.get(ch, "").startswith("door:")), None)
    key_char = next((ch for ch in roster if old_legend.get(ch, "").startswith("key:")), None)

    cols, rows = cell_dims(floor_index)
    edges = wilson_maze(cols, rows, rng)
    adj = build_adjacency(edges, cols, rows)

    entrance = (rows - 1, 0)  # fixed bottom-left cell -- consistent across floors
    dist_from_entrance, parent_from_entrance = bfs(adj, entrance)
    goal = max(dist_from_entrance, key=lambda k: dist_from_entrance[k])
    main_path = path_to(parent_from_entrance, entrance, goal)  # entrance .. goal, inclusive

    used = {entrance}
    placements = {}  # cell -> tile char

    def place(cell, ch):
        assert cell not in used, f"{filename}: cell collision placing {ch!r} at {cell}"
        used.add(cell)
        placements[cell] = ch

    # Goal slot: the floor's own forward progression (stairs up), or the boss if this
    # floor has no stairs up at all (stage10 -- the boss IS the floor's goal).
    if boss_char:
        place(goal, boss_char)
    elif has_up:
        place(goal, "U")

    # Backward stairs, if present: right next to the entrance.
    if has_down:
        back_cell = adj[entrance][0]
        place(back_cell, "D")

    # NPC(s): a few steps down the main path from the entrance (falls back to the
    # nearest still-free path cell if the ideal spot is already taken). stage_11 has
    # two distinct NPCs (king + princess), so this places every one in npc_chars, not
    # just the first.
    for i, ch in enumerate(npc_chars):
        target_idx = min(3 + i, len(main_path) - 1)
        npc_cell = next((c for c in main_path[target_idx:] if c not in used), None)
        if npc_cell is None:
            npc_cell = next(c for c in adj if c not in used)
        place(npc_cell, ch)

    # Door/key: pick an edge on the main path (roughly the midpoint) to gate. The key
    # must land in the entrance-side component of the tree once that edge is removed.
    if door_char and key_char:
        path_edges = [frozenset((a, b)) for a, b in zip(main_path, main_path[1:])]
        door_edge = path_edges[len(path_edges) // 2]
        edges.discard(door_edge)  # replaced by an explicit door tile below
        near_side = reachable_without_edge(adj, entrance, door_edge)
        key_cell = next((c for c in near_side if c not in used), None)
        assert key_cell is not None, f"{filename}: no free cell for the key on the near side"
        place(key_cell, key_char)
        door_x, door_y = edge_to_xy(door_edge)
    else:
        door_x = door_y = None

    # Monsters: spread evenly along the interior of the main path (excluding the
    # entrance/goal endpoints), falling back to any free cell if the path is short.
    # monster_instances already has one entry per actual monster (not per distinct
    # char), so a floor with "2x slime" places two separate '1' tiles.
    interior = [c for c in main_path[1:-1] if c not in used]
    n = len(monster_instances)
    for i, ch in enumerate(monster_instances):
        cell = None
        if interior:
            idx = min(len(interior) - 1, (i * len(interior)) // max(1, n))
            cell = interior[idx] if interior[idx] not in used else None
        if cell is None or cell in used:
            cell = next((c for c in interior if c not in used), None)
        if cell is None:
            cell = next(c for c in adj if c not in used)
        place(cell, ch)

    # Remaining items: scatter on whatever's left, preferring path cells first.
    free_cells = [c for c in main_path if c not in used] + [c for c in adj if c not in used]
    seen_free = []
    for c in free_cells:
        if c not in seen_free:
            seen_free.append(c)
    rng.shuffle(seen_free)
    for ch in item_chars:
        cell = next((c for c in seen_free if c not in used), None)
        assert cell is not None, f"{filename}: ran out of free cells for item {ch!r}"
        place(cell, ch)

    # ---- render to a tile grid ----
    grid, w, h = build_tiles(cols, rows, edges)
    ex, ey = cell_to_xy(entrance)
    grid[ey][ex] = '@'
    for cell, ch in placements.items():
        x, y = cell_to_xy(cell)
        grid[y][x] = ch
    if door_x is not None:
        grid[door_y][door_x] = door_char

    tiles = ["".join(row) for row in grid]

    # ---- roster-preservation check: the new floor must carry exactly the same
    #      entities (same chars, same counts) as the one it replaces, just relocated ----
    new_roster = Counter(ch for row in tiles for ch in row if ch not in ("#", ".", "@"))
    assert new_roster == roster, f"{filename}: roster mismatch, old={roster} new={new_roster}"

    # ---- solvability sanity check before writing anything ----
    # 1) every placed entity is reachable from '@' by walking through '.', the entity's
    #    own tile, or (for the door specifically) the door tile itself.
    passable = lambda ch: ch not in ("#",)
    gh, gw = len(tiles), len(tiles[0])
    seen = {(ex, ey)}
    q = deque([(ex, ey)])
    while q:
        x, y = q.popleft()
        for nx, ny in ((x+1,y),(x-1,y),(x,y+1),(x,y-1)):
            if 0 <= nx < gw and 0 <= ny < gh and (nx, ny) not in seen and passable(tiles[ny][nx]):
                seen.add((nx, ny))
                q.append((nx, ny))
    for cell in placements:
        x, y = cell_to_xy(cell)
        assert (x, y) in seen, f"{filename}: placed entity at {cell} is unreachable"

    data["width"] = w
    data["height"] = h
    data["tiles"] = tiles
    # legend is unchanged (same chars mean the same things on every floor already);
    # write it back verbatim so key order/content stays exactly as authored.
    data["legend"] = old_legend

    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")

    return w, h, len(placements)


def main():
    rng = random.Random(SEED)
    for filename, floor_index in FLOOR_FILES:
        w, h, n = generate_floor(filename, floor_index, rng)
        print(f"{filename}: {w}x{h} tiles, {n} entities placed, OK")


if __name__ == "__main__":
    main()
