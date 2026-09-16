# Stair Alignment

> Owner instruction, 2026-09-16: consecutive floors' stairs must occupy the exact same tile
> position, and arriving via stairs must place the player on that exact tile — not a fixed
> default spawn point. See `docs/story/STORY_DATA_SCHEMA.md` §5.3 for the narrative-data-schema
> framing; this document is the engineering reference for the generator and engine changes.

## The rule

For every pair of consecutive floors N and N+1 in the 70-floor chain (F01 → F70):

- Floor N's `stairs_up` tile and floor N+1's `stairs_down` tile are the **same (x, y)** tile
  coordinate.
- Walking up from floor N lands the player on floor N+1's `stairs_down` tile.
- Walking down from floor N+1 lands the player on floor N's `stairs_up` tile.

Floor F01 (the chain's head) has no floor below it, so it has no `stairs_down` tile at all — it
keeps the `'@'` (`player_start`) tile as its only spawn point. Every other floor's `stairs_down`
is real and aligned.

`stage_11.json` (the post-victory epilogue) is **not** part of this chain. It's reached only via
`Game::finishCombatWin()`'s boss-defeat warp, never by walking a staircase, so there is no
"position to align" for it.

## Why floors can't just share a fixed corner

A tempting simplification is "always put stairs_down at the same fixed corner on every floor" —
but that doesn't satisfy the rule: floor N's up-stair and floor N+1's down-stair need to match
*each other*, not a global constant, and a floor's own up-stair and down-stair are two different
physical locations (you don't enter and exit a floor in the same spot). Each floor genuinely has
its own down-stair position (matching the floor below) and its own, different up-stair position
(matching the floor above).

## How the data satisfies it: `tools/gen_stairs.py`

Floor sizes are unchanged (see schema §5.1b's `SIZE_RAMP`) and every floor's roster (monster/item/
NPC/event/door/key characters, and how many of each) is unchanged — this script only reshuffles
the maze *shape* and the stairs.

Floors are regenerated in strict order, F01 → F70, threading one piece of state forward: the cell
coordinate the previous floor's `stairs_up` landed on.

1. A maze cell's rendered tile position depends only on `(row, col, scale)` — never on the grid's
   overall width/height (see `cell_center_xy`/`cell_block` in `tools/gen_mazes.py`). So a `(row,
   col)` cell coordinate that was valid on a smaller floor is still a valid *tile position* on a
   same-or-larger floor.
2. Floor N's maze is a fresh Wilson's-algorithm spanning tree over its own (unchanged) cell grid.
   Its **entrance** cell is the previous floor's `stairs_up` cell (or a fixed corner for F01, which
   has no predecessor). `stairs_down` (if this floor has one) is placed exactly there.
3. The floor's own **goal** — the tree cell farthest from the entrance — gets `stairs_up` (or, on
   F70 only, the floor's actual boss monster instead, matching the existing convention: no floor
   sits above F70). That goal cell is handed forward as the *next* floor's forced entrance.
4. **Boss floors don't grow in lockstep with the procedural size ramp** (e.g. F06's cell grid is
   bigger than F07/`stage01.json` right after it) — so step 3's goal-cell candidates are filtered
   to whatever also fits inside the *next* floor's (possibly smaller) grid before picking the
   farthest one. Without this, a forced entrance could land out of bounds on the next floor.
5. Every other character already on the floor (monsters, items, NPCs, event markers, one door+key
   pair) is redistributed onto the new maze using the same rules `tools/gen_mazes.py` already
   established for the 11 hand-authored stages: the key must be reachable with the door still
   shut, and it's a real, provable gate (a tree has exactly one path between any two cells, so
   removing the door's edge genuinely splits the maze in two).
6. A pure spanning tree has exactly one path between any two cells — a single "big" (footprint >
   1×1, see `data/footprints.json`) monster fully occupying a corridor cell would seal off
   everything beyond it. The script adds a handful of extra, non-tree edges as alternate routes
   (never the door's own edge, which would defeat the key/door puzzle), and additionally keeps
   big-footprint monsters off any cell that would disconnect the entrance from the goal even with
   those loops in place (checked incrementally as each one is placed, since several big monsters
   together can seal a route that no single one of them would alone).
7. Every regenerated floor passes the same roster-preservation and solvability assertions
   `gen_mazes.py` already required, plus a new one: this floor's `stairs_down` tile position must
   equal the previous floor's `stairs_up` tile position exactly.

Run it from the repo root: `python3 tools/gen_stairs.py`. It's deterministic (fixed seed) and
idempotent-in-spirit (re-running produces a fresh but equally-valid layout each time, not
byte-identical output, since layouts are regenerated from a PRNG rather than derived purely from
each floor's own content the way `gen_floors.py`'s per-floor seeding is).

## How the engine satisfies it: `StageArrival`

`Game::loadStage()` takes a `StageArrival` (`game.h`): `Fresh`, `FromBelow`, or `FromAbove`.

- `Fresh` (the default) is every call site that isn't a stairs transition — a new game, rebirth,
  Stage Select, a debug jump (`jsGoStage`). It keeps the old behavior: use `'@'` if the floor has
  one (only F01 does now), else fall back to whichever stair tile the floor does have (never the
  old crude `(1, height-2)` guess, which could land on a wall for a floor with no `'@'`).
- `FromBelow` places the player on the new floor's `stairs_down` tile — used when climbing up
  (`confirmStageTransition()` passes this when `stairsConfirmIsUp_` is true): you're arriving at
  the new floor from the staircase that leads back down to where you came from.
- `FromAbove` places the player on the new floor's `stairs_up` tile — used when descending.

This is the piece that makes the data-side alignment actually visible to a player: without it,
the stairs could line up perfectly in the data and the player would still always land on `'@'`
(or the crude fallback) regardless.
