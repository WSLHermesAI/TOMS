#!/usr/bin/env python3
"""maze_lib.py -- the maze helpers the floor generator uses (tools/gen_floors.py, tools/gen_stairs.py).

Restored 2026-10-12 from the old tools/gen_mazes.py (removed in 979bcf3); only the helpers, not its
old stage01..11 regenerator. Wilson's algorithm (a uniform spanning tree, a "perfect maze"), BFS over
the tree, and the tile geometry: every cell is an SxS block with one wall tile between neighbours.
"""
from collections import deque


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


def cell_block(cell, scale):
    """Returns (x0, y0) -- the top-left tile of this cell's SxS block, where S=scale.
    One tile of wall separates every block from its neighbors."""
    r, c = cell
    return c * (scale + 1) + 1, r * (scale + 1) + 1


def cell_center_xy(cell, scale):
    """The tile any single entity placed "in" this cell actually renders at -- the
    block's center-ish tile (scale//2 into it on both axes)."""
    x0, y0 = cell_block(cell, scale)
    off = scale // 2
    return x0 + off, y0 + off


def edge_strip(edge, scale):
    """The full-width connecting strip between two adjacent cells' blocks: a list of
    (x, y) tile positions, one per row (horizontal neighbors) or column (vertical
    neighbors) -- carving/gating all of them, not just one tile, is what makes a door
    actually block the passage (a single-tile door in an S-wide corridor could be
    walked around)."""
    (r1, c1), (r2, c2) = tuple(edge)
    if r1 == r2:  # horizontal neighbors -- the strip runs vertically, S tiles tall
        left, right = (c1, c2) if c1 < c2 else (c2, c1)
        x = left * (scale + 1) + 1 + scale  # the single wall column between them
        y0 = r1 * (scale + 1) + 1
        return [(x, y0 + i) for i in range(scale)]
    else:  # vertical neighbors -- the strip runs horizontally, S tiles wide
        top, bottom = (r1, r2) if r1 < r2 else (r2, r1)
        y = top * (scale + 1) + 1 + scale
        x0 = c1 * (scale + 1) + 1
        return [(x0 + i, y) for i in range(scale)]

