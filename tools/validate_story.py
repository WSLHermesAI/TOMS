#!/usr/bin/env python3
"""validate_story.py — validators over the generated 70-floor data set.

Implements the checks STORY_DATA_SCHEMA.md section 13 assigns to the generation phase (its M1 /
this project's S2): **V7** (floor <-> act mapping), **V8** (maze connected, at least one way down),
**V9** (floor table matches STORY_BIBLE.md section 5), plus the two invariants this phase's data can
already be held to: **V12** (every floor has >= 1 relic/whisper event and >= 1 cache) and the
side-story placement from STORY_BIBLE.md section 6 (each act's flagship story on that act's 5th
floor).

It also re-checks the things the runtime will care about once floors are played:
  * exactly one player start, stairs up (and stairs down on floors above the first);
  * with doors passable, the exit is reachable from the start (the maze is connected);
  * with doors blocked, every door's key is reachable from the start -- i.e. a key can never sit
    behind its own door, which would softlock the floor;
  * every entity's whole footprint (ART_AND_ABILITY_DESIGN.md section 1.7) stands on floor tiles,
    inside the grid, overlapping nothing else.

The remaining validators from section 10 (V1-V6, V10, V11, V13, V14, V16) belong to the phases that
create the data they check -- skills/forge/hub (S4-S7), endings, cycles, i18n -- and are listed as
`not yet applicable` at the end so the gap is visible rather than implied.

Usage: python3 tools/validate_story.py [--quiet]      (exit code 0 = all checks passed)
"""
import argparse
import glob
import json
import os
import sys
from collections import deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLOORS_DIR = os.path.join(ROOT, 'data', 'story', 'floors')
FOOTPRINTS = os.path.join(ROOT, 'data', 'footprints.json')

sys.path.insert(0, os.path.join(ROOT, 'tools'))
from gen_floors import ACTS, BOSS_STAGE, FLOOR_TABLE, tile_counts, CELL_SCALE   # the table itself

FAILS = []
CHECKS = [0]


def check(cond, msg):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(msg)
    return cond


def load_footprints():
    with open(FOOTPRINTS, encoding='utf-8') as f:
        raw = json.load(f)
    out = {}
    for kind, spec in raw.items():
        if kind.startswith('_') or not isinstance(spec, dict):
            continue
        out[kind] = (int(spec.get('w', 1)), int(spec.get('h', 1)))
    return out


def validate_spec(path, tiers):
    with open(path, encoding='utf-8') as f:
        spec = json.load(f)
    fid = spec['id']
    floor_no = int(fid[1:])
    tier = min((floor_no - 1) // 7, 9)
    cols, rows, rooms, loops, n_events, emin, emax, elites, n_items = FLOOR_TABLE[tier]
    act_id, seal, boss_id, ss_id, theme = ACTS[tier]
    index_in_act = (floor_no - 1) % 7 + 1

    # ---- V7: floor <-> act mapping ----
    check(spec['act'] == act_id, 'V7 %s act=%s expected %s' % (fid, spec['act'], act_id))
    check(spec['indexInAct'] == index_in_act,
          'V7 %s indexInAct=%s expected %d' % (fid, spec['indexInAct'], index_in_act))
    expect_next = ('F%02d' % (floor_no + 1)) if floor_no < 70 else None
    check(spec['nextFloor'] == expect_next,
          'V7 %s nextFloor=%s expected %s' % (fid, spec['nextFloor'], expect_next))
    expect_role = ('boss-stage' if floor_no % 7 == 0
                   else ('side-story-stage' if index_in_act == 5 else 'normal'))
    check(spec['role'] == expect_role, 'V7 %s role=%s expected %s' % (fid, spec['role'], expect_role))
    # section 3.2: every boss floor maps to its hand-authored stage
    if floor_no % 7 == 0:
        check(spec.get('handAuthoredStage') == BOSS_STAGE[floor_no],
              'V7 %s handAuthoredStage=%s expected %s'
              % (fid, spec.get('handAuthoredStage'), BOSS_STAGE[floor_no]))
        check(spec['enemies']['boss'] == boss_id,
              'V7 %s boss=%s expected %s' % (fid, spec['enemies']['boss'], boss_id))

    # ---- V9: the floor table ----
    m = spec['maze']
    check((m['cols'], m['rows']) == (cols, rows),
          'V9 %s dims %sx%s expected %sx%s' % (fid, m['cols'], m['rows'], cols, rows))
    check((m['cols'], m['rows']) == tile_counts(floor_no),
          'V9 %s dims disagree with the section 5 formula' % fid)
    check(m['rooms'] == rooms and m['loops'] == loops,
          'V9 %s rooms/loops %s/%s expected %s/%s' % (fid, m['rooms'], m['loops'], rooms, loops))
    check(len(spec['events']) == n_events,
          'V9 %s events %d expected %d' % (fid, len(spec['events']), n_events))
    lo, hi = spec['enemies']['count']
    check((lo, hi) == (emin, emax),
          'V9 %s enemy range %s expected [%d, %d]' % (fid, spec['enemies']['count'], emin, emax))
    check(spec['enemies']['elites'] == elites,
          'V9 %s elites %s expected %d' % (fid, spec['enemies']['elites'], elites))
    check(spec['items']['count'] == n_items,
          'V9 %s items %d expected %d' % (fid, spec['items']['count'], n_items))
    check(spec['meta']['difficultyTier'] == tier + 1,
          'V9 %s difficultyTier %s expected %d' % (fid, spec['meta']['difficultyTier'], tier + 1))
    check(len(set(spec['events'])) == len(spec['events']),
          'V9 %s has a duplicate eventId' % fid)

    # ---- V12: relic/whisper + cache on every floor ----
    kinds = spec['meta'].get('eventKinds', [])
    check(len(kinds) == len(spec['events']), 'V12 %s eventKinds length mismatch' % fid)
    check(any(k in ('relic', 'whisper') for k in kinds), 'V12 %s missing relic/whisper' % fid)
    check(any(k == 'cache' for k in kinds), 'V12 %s missing cache' % fid)

    # ---- flagship side story on the act's 5th floor ----
    if index_in_act == 5:
        hooks = spec['sideStoryHooks']
        check(len(hooks) == 1 and hooks[0]['sideStoryId'] == ss_id,
              'ss %s expected hook %s' % (fid, ss_id))
        check(hooks[0]['cell'] is not None, 'ss %s hook has no cell' % fid)
    elif spec['sideStoryHooks']:
        check(False, 'ss %s carries a hook but is not an act 5th floor' % fid)

    check(spec['enemies']['mix'], 'V9 %s has an empty enemy mix' % fid)
    return spec


def bfs_reach(tiles, start, passable_doors=True, door_chars='ybr'):
    rows, cols = len(tiles), len(tiles[0])
    seen = {start}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if not (0 <= nx < cols and 0 <= ny < rows):
                continue
            if (nx, ny) in seen:
                continue
            c = tiles[ny][nx]
            if c == '#':
                continue
            if c in door_chars and not passable_doors:
                continue
            seen.add((nx, ny))
            q.append((nx, ny))
    return seen


def validate_stage(path, tiers):
    with open(path, encoding='utf-8') as f:
        st = json.load(f)
    fid = st['id']
    tiles = st['tiles']
    cols, rows = st['width'], st['height']
    legend = st['legend']
    check(len(tiles) == rows and all(len(r) == cols for r in tiles),
          'shape %s tiles are not %dx%d' % (fid, cols, rows))

    # ---- V8: player start, stairs, connectivity ----
    starts = [(x, y) for y in range(rows) for x in range(cols) if tiles[y][x] == '@']
    check(len(starts) == 1, 'V8 %s has %d player starts' % (fid, len(starts)))
    if not starts:
        return
    exit_tiles = [(x, y) for y in range(rows) for x in range(cols) if tiles[y][x] == 'U']
    check(len(exit_tiles) >= 1, 'V8 %s has no stairs up' % fid)
    if int(fid[1:]) > 1:
        check(any(tiles[y][x] == 'D' for y in range(rows) for x in range(cols)),
              'V8 %s has no stairs down' % fid)

    # connected: with doors open, the exit is reachable and all floor tiles are reachable
    open_reach = bfs_reach(tiles, starts[0], passable_doors=True)
    check(all(e in open_reach for e in exit_tiles), 'V8 %s unreachable stairs' % fid)
    floor_tiles = {(x, y) for y in range(rows) for x in range(cols) if tiles[y][x] != '#'}
    check(floor_tiles <= open_reach, 'V8 %s has %d unreachable floor tile(s)'
          % (fid, len(floor_tiles - open_reach)))

    # anti-softlock: each door's key must be reachable with the doors still shut
    shut_reach = bfs_reach(tiles, starts[0], passable_doors=False)
    for ch, kind in legend.items():
        if not kind.startswith('key:'):
            continue
        color = kind.split(':', 1)[1]
        key_tiles = [(x, y) for y in range(rows) for x in range(cols) if tiles[y][x] == ch]
        has_door = any(kind2 == 'door:' + color for kind2 in legend.values())
        if has_door and not any(k in shut_reach for k in key_tiles):
            check(False, 'V8 %s key %s sits behind its own door' % (fid, color))

    # ---- footprints: every occupied tile walkable, inside the grid, overlapping nothing ----
    ents = []
    for y in range(rows):
        for x in range(cols):
            kind = legend.get(tiles[y][x])
            if not kind:
                continue
            head = kind.split(':', 1)[0]
            if head not in ('monster', 'npc', 'item'):
                continue
            w, h = (1, 1)
            if head == 'monster':
                w, h = tiers.get(kind, (1, 1))
                # a stage-file override wins (F8 order: floor > type table > 1x1)
                ov = st.get('footprints', {}).get(tiles[y][x])
                if isinstance(ov, list) and len(ov) == 2:
                    w, h = int(ov[0]), int(ov[1])
            ents.append((x, y, w, h, kind))

    for (x, y, w, h, kind) in ents:
        check(w in (1, 2) and h in (1, 2) and (w, h) in ((1, 1), (2, 1), (1, 2), (2, 2)),
              'fp %s illegal footprint %dx%d for %s' % (fid, w, h, kind))
        check(x + w <= cols and y + h <= rows, 'fp %s %s sticks out of the grid' % (fid, kind))
        for yy in range(y, min(y + h, rows)):
            for xx in range(x, min(x + w, cols)):
                check(tiles[yy][xx] != '#',
                      'fp %s %s occupies wall tile (%d,%d)' % (fid, kind, xx, yy))
    occupied = {}
    for (x, y, w, h, kind) in ents:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if (xx, yy) in occupied:
                    check(False, 'fp %s %s overlaps %s at (%d,%d)'
                          % (fid, kind, occupied[(xx, yy)], xx, yy))
                occupied[(xx, yy)] = kind


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args()

    tiers = load_footprints()
    specs = sorted(glob.glob(os.path.join(FLOORS_DIR, 'F*.json')))
    specs = [p for p in specs if not p.endswith('.stage.json')]
    stages = sorted(glob.glob(os.path.join(FLOORS_DIR, 'F*.stage.json')))

    check(len(specs) == 70, 'expected 70 floor specs, found %d' % len(specs))
    check(len(stages) == 60, 'expected 60 generated grids (70 - 10 boss floors), found %d' % len(stages))

    boss_specs = 0
    for p in specs:
        spec = validate_spec(p, tiers)
        if spec['role'] == 'boss-stage':
            boss_specs += 1
    check(boss_specs == 10, 'expected 10 boss floors, found %d' % boss_specs)

    for p in stages:
        validate_stage(p, tiers)

    if not args.quiet:
        print('checked %d specs + %d generated grids' % (len(specs), len(stages)))
    if FAILS:
        print('validate_story: FAILED (%d of %d checks)' % (len(FAILS), CHECKS[0]))
        for f in FAILS[:20]:
            print('  ' + f)
        if len(FAILS) > 20:
            print('  ... and %d more' % (len(FAILS) - 20))
        sys.exit(1)
    print('validate_story: ALL PASS (V7, V8, V9, V12 + footprint/softlock checks; %d checks)'
          % CHECKS[0])
    print('not yet applicable (arrive with their own phase): V1-V6 flags/choices/grants/endings, '
          'V10 text width, V11 side-story rewards, V13 ending fallback, V14 cycle maths, '
          'V16 event-pool depth -- see STORY_DATA_SCHEMA.md section 10/13')


if __name__ == '__main__':
    main()
