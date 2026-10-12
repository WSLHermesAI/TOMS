# 21 — Stage maps as images (design)

**Status:** design, 2026-10-12 (the owner's Q9 proposal). Nothing is built yet. Today's
character grids keep working until phase 3 below.

## 1. Why

Today a floor's grid is a list of strings, one character per cell. The meaning of each
character comes from the file's own `legend` (for example `"1": "monster:bat"`). This has three
limits:

- **Too few characters.** A floor can only name as many different things as there are
  free, typeable characters. The 70 floors already use 149 different things (120 of them
  events), up to 24 on one floor, and every new monster, item, event or stair needs another
  character.
- **The same character means different things on different floors.** `1` is a bat on one
  floor and a slime on another. You cannot search for "every floor with a slime" without
  reading every legend.
- **Hard to see.** A 63 × 42 grid of letters is hard to read, and hard to edit by hand.

The owner's proposal: **a stage map is a 2D image. Each pixel is one cell, and the pixel's
value is an integer id** from one shared table. A parser reads the pixels into the stage.

## 2. The format

### 2.1 One PNG per floor

- `assets/data/story/floors/F01.png` sits next to `F01.stage.json` (boss floors:
  `assets/data/stages/stage01.png`).
- **Image size = grid size.** F01 is 19 × 16 pixels; F70 is 63 × 42. One pixel is one cell.
- **PNG only, RGBA 8-bit, saved without a colour profile.** A lossy format (JPEG, WebP lossy)
  would change the values, and so would colour management.
- Alpha must be 255. A pixel with any other alpha is an error, which catches anti-aliased
  brush strokes.

### 2.2 Pixel → id

```
id = R × 65536 + G × 256 + B        (0 … 16 777 215)
```

That gives 16.7 million ids, far more than the game will ever need. There is no limit per
floor. The `legend` disappears: the id table is the same for every floor.

### 2.3 The id table: `assets/data/tile_ids.json`

```json
{
  "ids": {
    "0":       "floor",
    "1":       "wall",
    "16":      "door:yellow",   "17": "door:blue",   "18": "door:red",
    "32":      "player_start",
    "48":      "stairs:0",      "49": "stairs:1",     "50": "stairs:2",
    "65536":   "monster:slime", "65537": "monster:bat",
    "131072":  "item:coin",     "131073": "item:gem_atk",
    "196608":  "npc:villager",
    "262144":  "event:ev_village_well", "262145": "event:ev_common_cache_wall"
  }
}
```

| Range | Red byte | Holds |
|---|---|---|
| 0 – 65 535 | 0 | Terrain and structure: floor, wall, doors, start, stairs |
| 65 536 – 131 071 | 1 | Monsters (`monster:<enemy id>`) |
| 131 072 – 196 607 | 2 | Items and keys (`item:<id>`) |
| 196 608 – 262 143 | 3 | NPCs |
| 262 144 – … | 4 and up | Events (`event:<id>`), one id per pool event |

Rules:

- **Ids are never renumbered or reused.** The table only grows. A removed thing keeps its row,
  marked `"retired": true`. Otherwise an old image would silently change meaning.
- The red byte shows the category, so a quick look at an image in any paint program tells
  terrain (black-ish) from monsters (dark red) from events (brighter red).
- **Stairs** are `stairs:<n>`: the *n*-th entry of the floor spec's `stairs` list
  ([05_FLOOR_LINKS](event_editor/05_FLOOR_LINKS.md)). This replaces both today's `U` / `D`
  and the `stairs_up:<to>` legend entries. A floor with no `stairs` list uses `stairs:0` for
  up and `stairs:1` for down.
- **Big monsters** (2 × 1, 2 × 2 …) take one pixel, their top-left cell, as today. The other
  cells of their footprint are floor.

### 2.4 What stays in JSON

Everything that is not "what is on which cell" stays in `F01.stage.json`: `id`, `name`,
`connect`, `footprints`, `encounter_overrides`, and the floor spec's `stairs` and `events`.
The `tiles` and `legend` keys are replaced by one key:

```json
{ "id": "F01", "image": "F01.png", "width": 19, "height": 16, ... }
```

`width` / `height` stay so tools can check them against the image.

## 3. The parser

- `parseStage` (`src/core/game/core/stage.h`) gets a second path. When the JSON has `image`,
  it decodes the PNG with the `stb_image` the engine already ships (`stb_image_impl.cpp`), and
  looks every pixel up in the id table.
- **Phase 1 keeps the rest of the game unchanged.** The parser builds the same `Stage` as today:
  it gives every distinct id on the floor an internal character and fills `tiles` + `legend`
  from them. Walking, drawing, saving and the editors' checks need no change. The 24-things-per-
  floor maximum is far below the characters available.
- An unknown id, or an alpha below 255, fails the load with the pixel's position
  (`F01.png (7, 3): id 65601 is not in tile_ids.json`) instead of silently drawing floor.
- **Phase 3 (later):** `Stage` holds the integer ids directly, and the characters disappear.

## 4. Tools

| Tool | Change |
|---|---|
| `tools/stage_image.py` (new) | Converts both ways: `F01.stage.json` (tiles + legend) ↔ `F01.png` + the id table. It adds missing ids to the table, and checks that a round trip gives the same cells |
| Stage editor ([19](19_STAGE_EDITOR.md)) | Reads and writes the PNG (the browser's canvas `getImageData` / `toBlob`). The palette shows names from the id table, never raw numbers |
| Event index (`tools/web_editors/floor_index.js`) | Reads event pixels, so the per-floor index stays correct |
| Viewer | A debug colouring for people: each category gets a readable colour, with the id in a tooltip, because the stored colours are ids, not art |

## 5. Plan

| Phase | Work | Done when |
|---|---|---|
| 1 | The id table; `stage_image.py`; the PNG path in `parseStage`; a unit test that loads every floor from both its JSON and its PNG and compares the `Stage` cell by cell | All 70 floors load identically from either format |
| 2 | The stage editor, the event index and the event editor read and write PNGs | One floor edited end to end in the PNG format |
| 3 | Switch the shipped floors to PNG, and drop `tiles` / `legend` from the files. Later, integer cells in `Stage` | The character grids are gone |

## 6. Open questions for the owner

1. **Packed RGB ids (this design) or an indexed palette PNG?** A palette PNG is easier to
   paint in an ordinary editor (pick colour 37 = slime), but it holds at most 256 values per
   image. Packed RGB has no limit, but you need the stage editor (or the debug colouring) to see
   what a pixel means. *Proposal:* packed RGB, edited in the stage editor.
2. **One id per event**, as above (120 events today, more later), **or a few "event slot" ids**
   whose event comes from the floor spec's list? One id per event lets an image say exactly
   which event is where, and keeps the per-floor index exact. *Proposal:* one id per event.
3. **Convert all 70 floors at once** in phase 3, or floor by floor as they are edited? The
   parser reads both formats, so either works. *Proposal:* all at once, by the converter, with
   the unit test proving nothing changed.
