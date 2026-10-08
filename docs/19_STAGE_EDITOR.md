# 19 — Stage editor (HTML now, Qt + game engine next)

**Status:** the HTML editor works: `tools/web_editors/stage_editor.html`, next to the event editor (open
`tools/web_editors/index.html` and switch with the **⇄** button). Section 6 plans the
Qt/C++ version, which will draw with the game's own renderer, so the editor shows exactly what the
game shows.

The stage editor paints a floor's grid: walls, stairs, doors, keys, monsters, NPCs, items, the player
start, and **event objects**. It reads and writes the same files the game loads:

| File | What the editor changes |
|---|---|
| `assets/data/story/floors/F??.stage.json` | `tiles` (one char per cell), `legend`, `name`, `connect`, `width` / `height` |
| `assets/data/stages/stageNN.json` | The same, for the ten hand-made boss stages (`handAuthoredStage` in the floor spec) |
| `assets/data/story/floors/F??.json` | `events`: placing an event adds it to the floor's list, so the floor can roll it |

## 1. Run it

| Way | What works |
|---|---|
| Double-click `stage_editor.html` | Everything except writing to disk. It loads the copy in `stage_bundle.js`; **Save** downloads the changed files for you to copy into `assets/data` |
| `tools\web_editors\serve.cmd`, then **Open project folder…** and pick the TOMS folder (Chrome or Edge) | Reads the live files, and **Save** writes them in place |

`tools/web_editors/refresh_data.cmd` refreshes both editors' copies after the data changes (it runs
`make_data.py` and `make_bundle.py`).

**Size.** Everything can be made bigger:

| What | How |
|---|---|
| Interface (text, buttons, panels) | 🔍 list in the top bar, or **Alt + =** bigger, **Alt + -** smaller, **Alt + 0** back to the default. The choice is remembered by the browser |
| Map zoom | **Mouse wheel over the map** zooms in and out around the cursor (the cell under the mouse stays put); also **+** / **−** next to the zoom % and the **+** / **-** keys |
| Map pan | **Right-mouse drag** (a right click without moving is still the eyedropper), **middle-mouse drag**, or hold **Space** and drag; **Shift + wheel** scrolls sideways; the scrollbars. The view can pan past the map's edges |
| Defaults | [`tools/web_editors/settings.js`](../tools/web_editors/settings.js): `uiScale` (1.25 = 125 %), `mapZoom` (2 = 64 px tiles), `language`. Edit, save, press F5 |

A size picked in the editor wins over `settings.js` until **Alt + 0** (or *settings.js* in the 🔍 list).
The browser's own zoom (**Ctrl + =** / **Ctrl + -**) also works.

## 2. Using it

- **Floor:** pick from the list, or press `[` / `]`. A ● marks floors with unsaved changes, and ★
  marks the hand-made boss stages.
- **Palette:** terrain, stairs, player start, doors, keys, monsters, NPCs, items, drawn with the game's
  sprites (`assets/media/sprites`). The small letter is the char this floor uses for it.
- **Events:** the list on the left. *This floor can roll* comes first (✓ n = placed n times), then every
  event by pool. Search matches id, kind and text. A placed event looks like the game's story plate
  (gold with a slate inset), plus a dot in its kind colour. A red dot means no pool defines it.
- **Tools:** 🖌 Paint (B), ▭ Rectangle (R), ⌫ Erase (E), ↖ Select / move (V: drag an object to move
  it). Right-click (without dragging) picks the cell under the cursor as the brush; right-drag pans the view. Grid (G), zoom (+ / −), Undo / Redo
  (Ctrl+Z / Ctrl+Y), Save (Ctrl+S), Delete clears the selected cell.
- **Objects need free floor.** Monsters, NPCs, items, keys, doors, stairs, events and the player start
  go only on empty floor, and a big monster (golem and demon 2×1, Vorkath 2×2) needs every cell of its
  footprint free. While an object brush hovers the map, a preview follows the cursor: green outline where
  it fits, red where a wall, another object (including the extra cells of a big monster) or the map
  edge is in the way. Red cells are not painted, the rectangle tool skips them (*N cells skipped*), a
  drag-move onto them is refused, and the status bar says what is in the way. Walls and floor still paint
  over anything; to replace an object, erase or move it first.
- **Inspector:** the selected cell: its object and legend char, a monster's HP / ATK / DEF and size,
  an item's effect, or an event's kind, pool, text (zh_TW and en) and the floors that can roll it, with
  *Replace with*. Below: the floor's name, `connect.up` / `connect.down`, size, and its events list.
- **Checks** (bottom), click one to show its cells:

| Check | Severity |
|---|---|
| A char with no legend entry | ⛔ |
| An event placed that no pool defines | ⛔ |
| No stairs and no `@`: the player would appear at (1, height − 2) | ⛔ |
| An event placed but missing from the floor's `events` list (fix: *Add to list*) | ⚠ |
| The same event placed twice (one floor should not repeat an event) | ⚠ |
| `connect.up` set but no `U` tile, or a `U` tile with no `connect.up` (same for down) | ⚠ |
| Objects that cannot be reached from where the player arrives (walls block; doors and monsters can be passed) | ⚠ (orange dashed outline on the map) |
| A 2-wide or 2-tall monster without free cells beside it | ⚠ |
| An event in the `events` list but not placed; open cells on the outer edge | ⓘ |

## 2b. Placing events: by hand, from the event editor, or automatically

**From the event editor.** *+ Add floor…* has *Then open the stage editor to place it on the map* (on
by default), and every floor chip has a 📍 button. Either opens this editor with
`?floor=F02&floors=F02,F04&place=<eventId>&kind=<kind>`: the floor is open, the event is the brush, the
safe cells are tinted green, and a bar offers **Suggest a cell**, **Place it for me** and **Next: F04 ▶**.
An event that exists only in the event editor so far is placed all the same and marked as not yet saved
in a pool.

**Story check (bottom tab *Story · all floors*).** It walks the floors in climbing order (`nextFloor`
from F01) with the game's rules: a `y` / `b` / `r` door takes one key of its colour, keys are picked up
on contact, `U` / `D` end the floor. Every order of opening doors is tried, so the check is exact
(monsters are treated as beatable). Per floor it shows the keys carried in, the keys on the floor, the
doors on the fewest-doors way up, and the most keys the player can carry out. It reports:

| Problem | Severity |
|---|---|
| The way up needs more keys of a colour than were carried in plus found, e.g. *needs 3 yellow; 0 carried in + 0 on this floor* | ⛔ |
| The stairs up are walled off | ⛔ |
| An event needs a key that is not available by this floor, or is walled off | ⛔ |
| An event is behind a door that is not on the way up: spending the key there could cost the way up | ⚠ |
| An event needs another event done (`requires`) that first appears on a later floor, or nowhere | ⚠ |
| An event needs `var ≥ N`, but the events up to its floor can only reach less | ⚠ |
| A locked stair ([05](event_editor/05_FLOOR_LINKS.md)) waits for an event that cannot be done by its floor | ⛔ |

Talk events count from the floor their NPC stands on; linked and variable-fired events from the floor of
what fires them. On today's data the check finds nothing blocking: every generated floor holds one yellow
key, and where the way up needs a door (F05, F06, …) the keys found by then are enough. Taking the
yellow keys off F01–F05 shows the problem it is for: F03 and F05 turn ⛔ *needs 1 yellow key; 0 carried
in + 0 on this floor*.

**🎲 Auto-place events…** places the events in each floor's `events` list, for this floor or all floors in
one click:
- only on **safe cells**: free floor reached on the fewest-doors way up (the top floor: without any door),
  never next to stairs, a door or the start, so an event can never cost a key the way up needs;
- spread out (as far as possible from other objects, rooms before corridors), with a seed: the same seed
  gives the same cells, 🎲 picks another;
- only events that fire on a tile (talk / linked / variable events need no cell); ids no pool defines are
  left out and reported;
- *only events not placed yet*, or *move every event to a new cell*.

Each floor gets one undo step, every placed event can be moved or erased like any other, and Save shows
the changes before writing. ◇ **Safe cells** shows the same cells at any time.

## 3. Data rules the editor follows

- **Doors and stairs keep their chars.** The game reads `y` `b` `r` `U` `D` directly (`movePlayer`,
  `walkPassable`), not through the legend, so the editor always gives a door or stair that char (moving
  anything else that used it), and flags a file that maps one elsewhere.

- **Chars are per file.** Placing something reuses the char the file already has for it, else its usual
  char (`1` slime, `y` yellow door, …), else a free one (`E F G I J …`, as the generator does). On save,
  a char this session added and no cell uses is dropped; legend entries the file already had stay.
- **Where the player arrives**, in the game's order: the stairs down, else `@`, else the stairs up.
  Generated floors have no `@`.
- **Files are written as the game's tools write them:** 2-space JSON, the same key order, and the file's
  own line endings (the boss stages use CRLF). Opening a floor and saving with no edits writes nothing.
- **Generated grids:** `F??.stage.json` is produced by the floor generator. Running it again would
  replace hand edits; a floor that is edited by hand should become hand-authored, or the generator
  should keep its edits. This needs a decision.

## 4. Found while building it

- **`@` is ignored by the game.** `parseStage` skips `@` cells (`stage.h`, `c == '@'` → `continue`),
  so the `@` branch in `game_assets.cpp`'s arrival code never finds one. A new game on F01 starts on
  F01's stairs, not on its `@`. The fix is one line; not changed here.
- Across all 70 floors the checks report 324 placed events that no pool defines (the chapters 4–10 ids
  in [event_editor/01_DATA_MODEL.md](event_editor/01_DATA_MODEL.md)), 75 listed events that are not
  placed, and one floor with a stairs-down tile but no `connect.down`.

## 5. Not in the HTML editor yet

- Several stairs per floor and stairs locked by events ([event_editor/05_FLOOR_LINKS.md](event_editor/05_FLOOR_LINKS.md)):
  the grid side (`"V": "stairs_up:F96"`) waits for the runtime work.
- Per-tile `footprints` and `encounter_overrides`: shown and respected, not editable.
- Play-testing: walking the floor with the game's rules. That belongs in the Qt version (section 6).

## 6. Plan: the Qt / C++ editor on the game engine

Goal: **what the editor shows is how the game works.** The same renderer, the same rules, and the
ability to play the floor from inside the editor.

| Piece | How |
|---|---|
| Window | A **Stage** tab in `toms_editor` (Qt6 Widgets), replacing the old `src/editor/stage` editor, laid out like this HTML version |
| Viewport | `bgfx_viewport` (already embeds the game's bgfx view) drawing the floor with toms_game's `BgfxRenderer`: the same atlas, the per-act tint (`floorThemeTint`), footprints, and event plates |
| Model | One C++ stage model shared with the game: `parseStage` to load, plus a writer that keeps the format rules of section 3. No second parser |
| Editing | Paint / rectangle / erase / move on the viewport; palette, events list, inspector and checks as Qt docks; `QUndoStack` with the History dock and automatic backups, like the anim and particle editors |
| Checks | The same checks, moved into a small Qt-free C++ library (like `event_graph`) with headless tests, so CI can check every floor |
| Play | **▶ Play from here**: run the real `Game` on the edited floor inside the viewport, with the player's stats and keys set in a side panel; **■** returns to editing with the floor unchanged |
| Links | Clicking an event opens it in the event editor; stairs show and edit their target floors (and later the `stairs` list from 05) |

Phases: (1) the read-only viewport with the game renderer; (2) painting, saving, and undo; (3) the
checks library and tests; (4) Play from here; (5) multi-stair floors once the runtime supports them.
