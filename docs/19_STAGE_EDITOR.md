# 19 — Stage editor (HTML, and the Windows app on the game engine)

**Status:** two editors on the same files.

- **HTML:** `tools/web_editors/stage_editor.html`, next to the event editor (open
  `tools/web_editors/index.html` and switch with the **⇄** button).
- **Windows app (2026-10-16):** `stage_event_editor.exe` (section 6). It is Qt, C++ and the game's bgfx renderer, so
  the map is drawn exactly as the game draws it, animations included. It holds both editors, natively: the stage
  editor (phase 1) and the event editor (phase 2).

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
| Double-click `stage_editor.html` | Everything except writing to disk. It loads the copy in `stage_bundle.js`; **Save → Download files**: one changed file downloads as itself; several download as **one zip** (`toms_stage_changes_<date>_<time>.zip`) that keeps each file's `assets/data/...` path. Extract it in the TOMS folder and let it replace the files |
| `tools\web_editors\serve.cmd`, then **Open project folder…** and pick the TOMS folder (Chrome or Edge) | Reads the live files, and **Save** writes them in place. Each file is backed up to `Build/editor_backups/<date-time>/` first. The event editor opens the same folder by itself (either editor can open it), and a floor's `events` list changed by both editors keeps both changes |

`serve.cmd` first refreshes both editors' copies of the data (it runs `make_data.py` and
`make_bundle.py`), so the editors always open on the current `assets/data`. Without the server, run
`tools/web_editors/refresh_data.cmd` after the data changes.

**Size.** Everything can be made bigger:

| What | How |
|---|---|
| Interface (text, buttons, panels) | 🔍 list in the top bar, or **Alt + =** bigger, **Alt + -** smaller, **Alt + 0** back to the default. The choice is remembered by the browser |
| Map zoom | **Mouse wheel over the map** zooms in and out around the cursor (the cell under the mouse stays put); also **+** / **−** next to the zoom % and the **+** / **-** keys |
| Map pan | **Right-mouse drag** (a right click without moving is still the eyedropper), **middle-mouse drag**, or hold **Space** and drag; **Shift + wheel** scrolls sideways; the scrollbars. The view can pan past the map's edges |
| Panels | Drag the edge between two panels: the palette / events column, the inspector, the checks below the map, and the line between the palette and the events list. The sizes are kept by the browser; a **double-click** on an edge resets it (`tools/web_editors/splitter.js`) |
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
- **An NPC's panel** (click an NPC on the map; [event_editor/08](event_editor/08_NPC_DIALOGUE.md)): its name, how many chat
  lines it has (⚠ when it has no NPC file), and its topics in the game's menu order, each marked *on every floor it
  stands on*, *only on this floor* or *only on F07* (faded when not here), with *has conditions*, ✓ *after-dialogue*
  and ↻ (repeatable). ↗ opens a topic in the event editor; **Edit in the event editor →** opens the NPC's NPCs tab.
  With the project folder open, the panel follows what the event editor saves.
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

## 2c. Types, objects and the game view (2026-10-16)

**Palette types.** A monster, item or NPC in the palette is a *type*: its record in `assets/data/enemies.json`,
`assets/data/items.json` or `assets/data/npcs/<id>.json`.
- **Double-click** a palette entry (or **Type details** in the inspector) opens its window:
  - **Look**: its default sprite (every sprite, with a search), an animation (any clip of `assets/media/anim/*.anim`),
    and a colour tint with opacity;
  - **Data**: a monster's name, HP, ATK, DEF, EXP and gold; an item's name, description, type, price, "important" and
    effect (stat rows); an NPC's name and greeting (its chat lines and topics are in the event editor's NPCs tab).
- **+** on the Monsters, Items or NPCs group adds a new type, copied from an existing one (a new NPC also gets a
  one-page dialogue to rewrite). **Delete this type** works only when no floor places it.
- A type's sprite is its record's `"sprite"`; the animation and tint go in `"look"`: `{ "anim": "anim/<file>.anim#<clip>",
  "tint": [r, g, b, a] }`. The tint multiplies the sprite's colours (white = unchanged).

**✎ Edit (O): one placed object's own settings.** Click an object on the map; its window has two tabs:
- **Look**: its own sprite / animation / tint (empty = its type's);
- **Events**: link or unlink event ids. A linked event fires when the player interacts with that object: defeats the
  monster, picks up the item, opens the door or gate, talks to the NPC, steps on the event tile. A talk event becomes
  one of the NPC's topics; the others run when their conditions hold (their title shows as a notice), else they are
  started.

They are saved in the floor's stage file, keyed by cell, and go with the object when it is moved (and away when it is
erased or painted over):

```json
"objects": { "9,4": { "look": { "sprite": "npc_king", "tint": [1, 1, 1, 0.5] }, "events": ["ev_common_relic_road"] } }
```

On the map, an object with settings shows ✎ (own look), ▶ (an animation) and ⚡n (linked events). The checks report
settings on an empty cell (⚠) and a linked event no pool defines (⛔).

**🎮 Game view (P)** draws the floor as the game does: every look and tint, without the grid, markers, letters, the
reachability outlines or the selection. The tools still work. Animations show their sprite here (the game plays them);
running the real game on the floor is planned.

**Saving:** the floors, and the type records that changed (`enemies.json`, `items.json`, `npcs/*.json`, a new NPC's
dialogue). Names kept in `text.json` (items, NPCs) and deleted type files are written only with the project folder open.

**In the game:** `Game::lookOf` (an object's look over its type's), `drawObjectAnim` (any clip, tinted, at the object's
phase), `fireLinkedEvents`; checked by `unit.talk_system_test` (every look and object setting in the data) and
`smoke.stage_objects` (`tests/stage/objects.stage.json`: a tinted slime, a bat playing another clip, a villager drawn
with another sprite, a potion whose linked event fires on pickup). `toms_game --goto=<path to a .json>` opens a stage
file directly (tests).

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
- **Floors are edited by hand now (2026-10-12).** The 70 `F??.stage.json` grids and their specs were first produced
  by a floor generator. For the owner's Q6 answer it was restored, with a dialog listing hand-edited floors before it
  replaced them. **Then the owner chose to edit floors only in this editor, so the generator was removed again**
  (`tools/gen_floors.py`, `gen_stairs.py`, `maze_lib.py`, `floor_guard.py`; they stay in git history). Nothing
  rewrites the floors behind your back any more. To build many floors automatically later, restore them from git.
- **The event index follows every save.** When this editor opens the project folder, and after each Save into it, it
  rebuilds `assets/data/events/floor_index.json` (`tools/web_editors/floor_index.js`; the event editor does the same).
  The index is written only when it changed.

## 4. Found while building it

- ~~**`@` is ignored by the game.**~~ Fixed 2026-10-12 (with the locked stairs, Q7): `parseStage` keeps the `@`
  cell as `Stage::startX` / `startY`, and a fresh load (a new game, a debug jump) starts there. A new game on F01
  now starts on its `@`, not on its stairs.
- Across all 70 floors the checks report 324 placed events that no pool defines (the chapters 4–10 ids
  in [event_editor/01_DATA_MODEL.md](event_editor/01_DATA_MODEL.md)), 75 listed events that are not
  placed, and one floor with a stairs-down tile but no `connect.down`.

## 5. Not in the HTML editor yet

- Several stairs per floor and stairs locked by events ([event_editor/05_FLOOR_LINKS.md](event_editor/05_FLOOR_LINKS.md)):
  the grid side (`"V": "stairs_up:F96"`) waits for the runtime work.
- Per-tile `footprints` and `encounter_overrides`: shown and respected, not editable.
- Play-testing: walking the floor with the game's rules. That belongs in the Qt version (section 6).

## 6. The Windows app: `stage_event_editor` (Qt / C++ / bgfx)

The stage editor and the event editor as one Windows program, on the game's own stack (Qt 6, C++, the game's
bgfx renderer) and no other library. **What the editor shows is how the game works.**

**Run it:** `Editor\StageEventEditor.cmd`, or `Build\windows-releasein\stage_event_editor.exe`. It finds the TOMS
folder from its own folder upwards (or `--root=<folder>`) and works on the live files: no bundle, no folder picker.

**Windows.** Both editors stay open together, as Visual Studio's document windows. They are two panes of one main
window, tabbed along its top: **Stage | Events**.

- **Float:** drag a tab out, or use **View → Float the stage / event editor**. That editor becomes a window of its own,
  which you can move, resize and put on another monitor. Its title bar has **□** (maximise / restore) and **⧉** (dock
  back).
- **Dock:** drag the floating window back over the main window, double-click its title, press **⧉**, or close it.
  It docks as a tab again, or beside the other editor if you drop it at a side (split). A docked editor that is not a
  tab has a slim bar with ⧉, to drag it by or float it.
- **View → Dock both as tabs** puts everything back.

The layout (docked or floating, sizes, places) is kept between runs; nothing open is lost when an editor moves.
**Ctrl+1** / **Ctrl+2** (the **Window** menu) or the **⇄** buttons bring the other editor to the front. Closing the main
window quits, after asking about unsaved changes. **中文** in the menu bar switches the language.

**The palette (native app).**
- **A click picks and opens:** one click on a monster, item, NPC, key or door paints with it and opens its details window
  (Look | Data). The window does not block painting, follows the next click, and keeps its place; **Apply** keeps it
  open. Changes not applied are asked about (Apply / Discard / Cancel) before it shows another type.
- **Every group has +.** Monsters, Items and NPCs copy an existing type. **Doors** and **Keys** make a new door with its
  own key: a key item of type `story`, and the door only that key opens (a gate). A new door appears in Doors, its key
  in Keys. Terrain, stairs and the start are the game's own (their + is greyed).
- **After Create:** the new type is the brush, the palette scrolls to it in the group whose + made it, and its details
  open on **Data**. The Look tab's grid is the game's pictures: click one to draw the type with it.
- **Ids:** lowercase letters, Chinese characters, digits and `_` (no spaces, capitals, `:` `#` `.` `/` `,`).
- **Your own categories:** **+ New category** at the top of the palette adds one (any name, Chinese too). Its header
  has **+** (a new monster, item, NPC, or door and its key, made into it), **✎** (rename) and **✕** (delete: its types
  go back to their usual groups, nothing is deleted). Right-click a type → **Move to category** (or back to its usual
  group). A type's category is a `"group"` field in its record; the list of categories (order, empty ones) is
  `assets/data/editor_palette.json`. **Save** writes both; the game ignores them.
- **A lost panel:** **View → Stage panels** lists Palette, Events, Inspector and Checks; **Reset stage layout** docks
  them all back where they start, also one left floating off screen.
- **View → Default layout** puts everything where it starts: the window's size and place, both editors docked as tabs,
  every panel of both editors docked at its starting size, and an open details window over the middle.

**The stage editor** is the HTML editor's layout and rules (sections 2–3), in docks you can resize and move:

- palette, events and inspector;
- checks / story below the map;
- the same tools and keys (B R E V O, G, P, [ ], Ctrl+Z / Y / S, Delete, Esc);
- ✎ Edit and the type windows (2c);
- auto-place with the same seeded results (2b);
- Save with the change list, backups to `Build/editor_backups/`, the events-list merge, and the floor index.

The map is drawn by the game's renderer (`GameCanvasView`, as in the atlas, anim and particle editors):

- sprites come from the game atlas;
- a look's **animation plays** (`anim/<file>.anim#<clip>`), and so do the monsters' idle clips;
- tints multiply the sprite, and gates get their key's colour;
- events are the game's story plates.

Mouse wheel zooms around the cursor. Right or middle drag pans (as does Space + drag); a right click without
moving is the eyedropper. Without a GPU the map is painted with QPainter (still sprites).

**The event editor** (phase 2) is the HTML event editor's model and rules (`core/event_project`):

- **Events tree:** every pool, plus the ids floors name but no pool defines. Find (id, text, kind, floor), a kind
  filter, and "only problems".
- **Inspector:** an event (id rename, kind, pool, floors with 📍 and ✕, the logic, its text in six languages, title,
  description, event log, preview), a floor (chapter, stairs with their locks, reached from, events) or a missing id
  (create it, create the whole chapter, or remove it from the floors).
- **Logic:**
  - fires when: on a tile, talking to an NPC (with its dialogue, after-dialogue, floor and menu order), a variable
    changing, or a link;
  - repeatable, with an optional limit;
  - **condition rows** (all of / any of / is not, `{ }` for JSON), action rows, and "Then fire" links.
- **Tabs below:**
  - **Problems:** the validator, with rule filters, per-chapter creation and fix buttons.
  - **Floor map:** chapters as rows, stairs as arrows, locked stairs gold.
  - **Variables:** with categories, several at once; a click opens a value tab.
  - **Kinds.**
  - **NPCs:** the file, chat lines with their conditions and actions, topics in menu order, and the **talk preview**.
  - **Dialogues:** nodes as cards, choices with conditions, actions and wakes.
- **History:** each edit is one undo step.
- **Save:** shows every file's change, backs each file up, and writes in the HTML editor's formats (`flags.json` and
  `counters.json` keep their hand-aligned lines).

Not ported (the inspector edits the same data): the relationship diagram, the values map and the logic node graph,
which the HTML editor still has. The tutorial is not ported either; it runs on its own frozen copy of the data.

**Together.** The two editors call each other directly:

- ⇄ shows the selected event's floor;
- **+ Add floor…** then 📍 opens the stage editor in place mode for that event;
- ↗ on a stage object, and an NPC's **Edit in the event editor**, select it in the event editor.

When one editor saves, the other rereads what it has not changed itself: the stage editor its events, NPC topics and
untouched floors; the event editor everything, if it has nothing unsaved. Each Save also merges onto the files as they
are on disk. The app also watches `assets/data/events`, `npcs`, `dialogue` and `text.json`, for edits made in the HTML
editors.

| Piece | Where |
|---|---|
| The rules, without Qt | `tools/stage_event_editor/core/`: `stage_project` (files, cells, placement, undo, what Save writes), `stage_checks` (checks, the story key/door check, safe cells, auto-place), `stage_save` (changed files, diffs, backups, the floor index), `event_project` (the event editor's state, validator, condition rows, Save, the talk preview). Files are read and written with nlohmann's `ordered_json`, so an untouched file is written back byte for byte |
| The window | `tools/stage_event_editor/app/`: `AppShell` (the docking main window), `StageEditor`, `StageCanvas` (the game renderer), `ObjectWindow`, `SpriteBank`, `EventEditor` (+ `EventForms`, `EventTabs`, `EventNpcs`, `EventWidgets`); strings in `tools/studio_common/i18n/zh_TW/stage.json` and `events.json` |
| Tests | `unit.stage_core_test` checks every floor written back byte for byte; painting, the reserved chars, object settings and undo; the checks and the story check on all floors; auto-place repeatable, with the HTML editor's random numbers; the floor index matching `floor_index.json` byte for byte. `stage_editor.app_selftest` is the window, offscreen: paint, undo, ✎ Edit, a type window, auto-place, the tables; the event editor's edits, undo and every tab; docked tabs ↔ floating ↔ split. The event checks in `unit.stage_core_test`: every pool file already in the save format, a text / logic / flag edit writing only its file (one line of `flags.json`), condition rows, the talk preview |

The ports were compared with the HTML editors on the real data (headless Chrome):

- Stage editor: `stage_core_test --dump` gives the same check counts, story issues and safe cells on all 70 floors, and
  the same auto-placed cells.
- Event editor: `--dump-events` gives the same 166 problems.
- Save: after the same three edits (an action, a text, a flag), `--dump-write` writes the same bytes as the HTML
  editor's Save, apart from line endings.

**Why its own program and not a tab in `toms_editor`.** `toms_editor`'s Play tab starts bgfx on its own. The studio
canvases (`GameCanvasView`) share one renderer per process. A separate exe keeps them apart; "Play from here" can
come back as a game canvas in this app.

**Next:**

1. **▶ Play from here:** the real `Game` on the edited floor, in the map view.
2. The event editor's drawn views: the relationship diagram, the values map and the logic node graph.
3. Multi-stair floors, once the runtime supports them; per-tile footprints editable.
