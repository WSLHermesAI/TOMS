# Progress Report — Log Part 6 (2026-10-05 evening to 2026-10-10)

> Chronological log entries, oldest first. The live **Next Step**, status table, waiting list and
> open questions are in [PROGRESS_REPORT.md](PROGRESS_REPORT.md).

---

### Summary

The **event editor** and the **stage editor** now exist as working HTML tools, in one folder:
[tools/web_editors/](../../tools/web_editors/index.html). Both run on a copy of the real game data.
They come first, so the design can be confirmed before the Qt/C++ editor on the game engine is built.
That Qt editor's goal: what the editor shows is how the game works.

| Day | Commit | What |
|---|---|---|
| 2026-10-05 | `6f24ba6` | Event editor docs and mockups; the interactive tutorial on a copy of the real data |
| 2026-10-07 | `f7c8b8e` | Floor links, event logic, node-graph editor, values map; the HTML stage editor with story check and auto-place |
| 2026-10-07 | `cf2a771` | Stage editor: object overlap rule, UI size settings, wheel zoom, right-drag pan |
| **2026-10-08** | not committed | Editors: one folder with a switch button; tutorial collapses; top menu removed; **Kinds** tab; old mockups deleted; the event editor **saves into assets/data**; free (Chinese) names; Variables tab categories and multi-select; stage editor saves many floors as one zip. Docs: Traditional Chinese versions in `docs/zh_TW/`. Game: **monster idle animations**; **battle settings file**; fixed **invisible keys**; **doors open** with an animation |
| **2026-10-10** | not committed | The **player menu** (P1): one ≡ button, five tabs (Status, Equipment, Items, Events, System); the all-item `items.json`; `stats.json` with STR / DEX / AGI / VIT / INT / LUK |

---

### 2026-10-05 — event editor: design, docs, tutorial (`6f24ba6`)

The owner asked what the event editor is, then for documents, HTML examples, and a full tutorial on
a copy of the current event data.

- **Docs** in [../event_editor/](../event_editor/README.md):
  - data model (01);
  - editor design (02);
  - user guide (03);
  - tutorial walk-through (04).
- **Data copy:** `make_data.py` copies these into `data_snapshot/` and `data.js`, read-only:
  - `assets/data/events/pool_*.json`;
  - the floor specs;
  - `text.json` strings.
- **Tutorial page:** a working mini-editor with guided steps:
  - create an event, save it, connect it, read the relationship diagram;
  - find problems and solve them.
- **Play demo:**
  - runs every step by itself;
  - a console shows what happens next, and a spotlight marks the area being used.
- **Languages:** English and 繁體中文.
- **Also:**
  - `\n` line breaks in the dialog preview;
  - an event filter;
  - collapsible pools;
  - full `ev_` ids everywhere.
- **Found in today's data:**
  - 92 event ids that floors F22–F70 list but no pool defines;
  - 4 orphan events.

### 2026-10-07 — floor links, event logic, node graph, the stage editor (`f7c8b8e`, `cf2a771`)

**Event editor** ([05](../event_editor/05_FLOOR_LINKS.md), [06](../event_editor/06_EVENT_LOGIC.md)):

- **Events are not bound to a chapter.** A floor can hold events of several characters, so the
  "wrong chapter" rule was dropped.
- **Several stairs per floor**, each locked until an event is finished (e.g. F50 → F51 and F96; F96's
  stair appears once an event is done). Design and editor only; the runtime work is listed in 05.
- **Event logic:**
  - **triggers:** step, talk to an NPC, a variable changes, a link from another event;
  - **conditions:** the game's existing `condition.cpp` format, plus `varAtLeast` / `varEquals`;
  - **actions:** set or add to variables and counters, set flags, give items;
  - **links:** `next` fires other events directly;
  - **variables:** a new `story/vars.json`.
- **Logic graph:** a node editor in the style of thedmd/imgui-node-editor, in plain HTML/SVG:
  - drag a pin to another pin to make a link; the link edits the same data as the inspector;
  - menus on drop and right-click; box select, zoom, pan.
- **Values map:**
  - every variable, counter and flag, with what changes it and what reads it;
  - hover counts;
  - closable value tabs that edit the value's data.
- **Variable categories** and a filter.
- **+ Add floor…** opens the stage editor ready to place the event.

**Stage editor** ([../19_STAGE_EDITOR.md](../19_STAGE_EDITOR.md)):

- **Painting:**
  - paints `F??.stage.json` grids and the boss stages, with the game's sprites;
  - places event objects.
- **Saving:** writes the live files through *Open project folder*, in the game's file format
  (key order, CRLF kept).
- **Checks:**
  - per-floor checks;
  - **Story check** over all floors: an exact key/door simulation, so no floor needs more keys than
    the player can have.
- **🎲 Auto-place:**
  - one click places events on safe cells only, so placement can never block the way up;
  - seeded, and each floor is one undo step.
- **Navigation and size:**
  - mouse-wheel zoom;
  - right / middle / Space-drag pan;
  - interface size (`settings.js`, Alt + = / - / 0).
- **Overlap rule:** an NPC, monster or object over a wall or another object shows red and is not
  placed. Big monsters need every cell of their footprint free.
- **Found in the game code:**
  - `parseStage` skips `@`, so the player start in a stage file is ignored;
  - the "event finished" flag is set only for whispers and rescues;
  - trap and cache effects come from words in the event id.

### 2026-10-08 — one editors folder, tutorial and menu cleanup, event kinds (not committed)

**1. Both editors in one folder, switched by a button**

- **Move:** everything moved (`git mv`) to [tools/web_editors/](../../tools/web_editors/):
  `event_editor.html`, `stage_editor.html`, `make_data.py`, `make_bundle.py`, `data_snapshot/`,
  `settings.js`, `serve.cmd`, `refresh_data.cmd`.
- **Shell:** `index.html` holds both editors, and the **⇄** button switches between them.
  - `#event` / `#stage` in the URL picks the editor.
  - *+ Add floor…* and the 📍 buttons jump straight into the stage editor, ready to place the event.
- **Run:**
  - `tools\web_editors\serve.cmd` opens `http://localhost:8000/tools/web_editors/index.html`;
  - `refresh_data.cmd` refreshes both data copies.

**2. Tutorial collapses**

- **First visit:** the tutorial shows once.
- **After that:** it stays closed, and the **📘 Tutorial** button opens it.

**3. The top menu is gone**

- The fake File / Edit / View menu did nothing.
- **What's left:** the editor's title, an unsaved-changes mark, Tutorial, ⇄ Stage editor, Undo, Save.

**4. Kinds tab: edit event kinds**

Kinds were hard-coded in the editor, and no data file defined them. The game does not read `kind` yet.

- **New file** `assets/data/events/kinds.json`: id → names (zh_TW / en), colour, description. It is
  created on the first save.
  - Until then, the editor starts from the seven kinds the pools use today: relic, whisper, cache,
    trap, rescue, merchant_echo, memory_shard.
- **Kinds tab:**
  - add a kind;
  - rename one (its events follow);
  - edit colour, names and description;
  - see and open the events of each kind.
- **Moving events:** tick events, or **Select all**, then *Move N selected to…*. Only the ticked
  events move, in one undo step.
- **Delete:** only once a kind has no events left.
- **Connected:**
  - the inspector's kind drop-down and the kind filter read the list, and the drop-down has
    *+ New kind…*;
  - a new ⚠ problem: an event kind not in the list;
  - the stage editor's kind colours come from `kinds.json`.
- **Save…** shows the new `kinds.json` with the other file changes.

**5. Old mockups deleted**

- **Deleted:** `docs/event_editor/mockups/` (the first three static mockups, their frozen data and a
  redirect page). The working editor replaces them.
- **Links:** every doc link now points at `tools/web_editors/`.
- **Also removed:** the mockup-only code in `make_data.py`.

**6. Small fixes**

- **Bottom tab row:** stays on one line in narrow windows instead of wrapping and shrinking the panel.
- **Paths:** the data-copy paths in the tutorial doc were corrected.
- **Fresh data on every start:** `serve.cmd` now runs `make_data.py` and `make_bundle.py` before it
  starts the server. The unused `data_snapshot/` copy is gone; the editors read only `data.js` and
  `stage_bundle.js`.

**7. The event editor saves into assets/data** ([../event_editor/README.md](../event_editor/README.md))

- **Open project folder…** in the event editor, as in the stage editor. Opening it in either editor
  opens it in both (`index.html` passes the folder handle across).
- **Write to project** writes only the files that change, each built from the file as it is on disk
  at that moment: pools (other fields kept), floor specs (only `events`, `act`, `stairs`), only the
  changed strings of `text.json`, `vars.json` / `kinds.json`, and only the changed lines of the
  hand-aligned `flags.json` / `counters.json`.
- **Both editors back up** every file to `Build/editor_backups/<date-time>/` before overwriting, and
  the stage editor now also merges a floor's `events` list with what is on disk, so neither editor
  undoes the other's save.
- In project-folder mode the tutorial and Play demo are off.
- **Tested** in headless Chrome on a copy of `assets/data` in the browser's private file system: no
  edits → nothing written; six kinds of edit → exactly those six files, each changed only where
  expected; both editors changing F06 → both changes kept; backups equal the files before the
  write; saving again writes nothing.

**8. Names can be anything, Traditional Chinese included**

- **Where:** event ids, event variables, kinds and floor names. The old rules (`ev_` prefix, lowercase
  letters, digits, `_`) are gone.
- **Still refused:** an empty name, spaces at the ends, and `" ' < > & | \ /`, which would break the
  editor's pages or its node graph. A floor's name is also its file name, so `: * ?` are refused there too.
- **Floor files:** the data scripts and the project-folder reader now read every `*.json` in
  `story/floors` (except `.stage.json`), as the game does, instead of only `F??.json`.

**9. The Variables tab**

- **Layout:** the *add variable* row is at the top; a new variable shows highlighted.
- **All categories** lists the values flat (variables, counters, flags, by name); picking a category
  shows category headings.
- **Categories:**
  - **Create** (an empty category is kept in `vars.json` as `"_categories"` until a value uses it);
  - **Rename**, in a dialog (a name that exists merges the two);
  - **Delete**, in a dialog: *Delete the N event variables* or *Make them uncategorized*. Counters
    and flags are never deleted, only uncategorized.
- **Several at once:** tick rows (Shift+click for a range, the header box for all shown), then
  *Move to category* or *Make uncategorized*, as one undo step.
- **Automatic groups** (*Chapter ch_05*, *Story counters*) come from the data and cannot be
  deleted; the editor says so.

**10. Stage editor: saving many floors**

- **Download:** one changed file downloads as itself; several download as **one zip** that keeps
  each file's `assets/data/...` path (extract it in the TOMS folder). The zip is written in the page,
  so it works offline and from `file://`.
- **Save dialog:** more than 3 files shows only the list of files, not each change; its buttons stay
  visible however short the window.
- **After auto-place:** the Story panel starts with how many floors changed and which, and marks them
  ●. (Reported as "70 floors changed but the zip has 10": the panel lists all 70 floors; only the
  changed ones are saved. On today's data the default auto-place changes 8 floors.)

**11. Docs in two languages** (`docs/zh_TW/`)

- **Chinese:** every document in `docs/` except this progress report folder has a Traditional
  Chinese version in `docs/zh_TW/`, with the same file names and folders. Links inside it point to
  the Chinese versions where they exist.
- **English:** the five documents that were only in Chinese (`07_BUILD_ANDROID`,
  `11_NEW_MACHINE_SETUP`, `12_SSH_KEY_SETUP`, `13_ART_STYLES`, `docs/PROGRESS_REPORT.md`) now have
  English versions under their original names; the Chinese originals moved to `docs/zh_TW/`.
- The root README links to `docs/zh_TW/`.

### 2026-10-08 (later) — the game: monster idle animations, battle settings, keys, doors (not committed)

**12. Every monster on the map has an idle animation** ([../15_ANIMATION.md](../15_ANIMATION.md))

- **The clips:** `assets/media/anim/monster_idle.anim`, one looping clip per enemy, `idle_<enemy id>`,
  in anim_editor's format. Sillier with the enemy's level:

  | Monster | Idle |
  |---|---|
  | Slime | slow squash-and-stretch breathing |
  | Bat | quick hover with wing-beat squeezes |
  | Skeleton | sways, shivers, chatters its teeth |
  | Golem | heavy breaths, then a stomp that kicks up dust |
  | Wraith | floats, flickers, leans in a purple glow |
  | Demon | a little dance: hops, looks left and right, sparks |
  | Vorkath | a belly laugh going red, a smug lean, dizzy stars, a red aura |

- **In the game** (`Game::drawIdleAnim`): each monster is drawn with its clip, stretched over its
  footprint like the static sprite was, at its own phase. Missing file or clip, or the runtime sprite
  grid: the static sprite, as before.
- **Checked** on a showcase floor with all seven monsters in the real game; CTest
  `anim.check_monster_idle`.

**13. Battle settings file: `assets/data/battle.json`**

- **Values:**
  - `bar_slow_speed_scale`: the bars' slow start;
  - `bar_fast_speed_scale`: the top speed they ramp up to;
  - `bar_cooldown_ms`: the wait after a tap.
- **Read at start-up.** Without the file: 1, 1 and 1500 (the old behaviour).
- **The owner's current values:** 0.5, 10 and 1000.
- No tuning numbers are in the code.

**14. Fixed: keys were invisible and could not be picked up; items were drawn as floor**

- **Found from F03** (two yellow doors and "no key"): the generated floors write a key as `key:yellow`,
  which the game neither drew nor collected (it only knew `item:key_yellow`). All 60 generated floors
  and 2 stages were affected, so their doors could become dead ends. The editors' story check counted
  these keys, so it said the floors were fine.
- **Fix:** `parseStage` loads `key:<colour>` as the item `key_<colour>` (`items.json`: +1 key), and
  items draw with their own sprites.
- **Guard:** `footprint_test` now checks that every key and item on all 71 stage files loads as
  something the game picks up (3427 checks).

**15. Doors open properly, with an animation**

- **The bug:** walking into a door with its key used the key but left the door, so every later step
  onto it cost another key (a known open item in the code).
- **Now** (`Game::openDoor`):
  - the key is used once and the player stays put that turn;
  - `door_open_<colour>` plays from the new `assets/media/anim/door_open.anim` (the key turns, the
    door sinks into the floor with a flash and sparkles);
  - the tile becomes floor and is recorded as opened (`EntityStatus::Opened`), so it stays open on
    later visits.
- **Doors and stairs** no longer get a floor-coloured square drawn over them (that was the
  "yellow block" look).
- **Checked** in the game on a test floor: `Y1` → the animation (player still, `Y0`) → floor → walking
  through the doorway and back took no more keys. CTest `anim.check_door_open`.

### 2026-10-10 — the player menu, P1 (not committed)

**16. One button, five tabs** ([../20_PLAYER_MENU.md](../20_PLAYER_MENU.md) §9)

- **The walking screen keeps only ≡** (with a dot when something new is inside). The HUD's store button,
  the clickable item line and the web page's fullscreen and backpack buttons are gone.
- **≡ / Esc opens the player menu:**
  - **Status:** level, HP and EXP bars, ATK / DEF with their bonus, gold, keys, floor, chapter, gear, skills,
    memory shards, and the attributes STR, DEX, AGI, VIT, INT, LUK (click one for its description);
  - **Equipment:** what is worn, the owned weapons or armor as a grid, and a comparison with what is worn
    (ATK / DEF, charge time, hit zone, top power, attributes, skills) before **Equip**;
  - **Items:** everything owned as a scrolling 3-per-row grid (copies stacked as x2), filters All / Use /
    Gear / Keys; the right side shows the description, effect, price and **Use / Equip / Unequip / Drop**.
    Keys and important items cannot be dropped; Drop asks first;
  - **Events:** chapters and missions, Unfinished / All, title and description;
  - **System:** Store, Skills, Forge, Village, Save, Settings, **Fullscreen**, Back to title (the old ≡ list;
    a page opens on the right).
- **Keys:** C Status, I Items, B store, Q / E / Tab tabs, arrows, Enter.

**17. The data behind it**

- **`assets/data/items.json` is the all-item file:** 23 items, each with type, sprite, name and description
  keys, stats, price and `important`. `equipment.json` merged into it and is gone; `store.json` lists item
  ids and how prices grow; item names moved into `text.json`.
- **`assets/data/stats.json`** (new): the six attributes and the level-up numbers, moved out of the code
  (the three copies of the level-up loop are one function now). The attributes change nothing in battle yet.
- **Chapters 1–4 and the three missions** got a title and a description.
- **Saves** now keep the attributes, the owned gear and the worn gear (worn gear was not saved before).
  Older saves load.
- **The UI font** was rebuilt for the new characters (`tools/make_ui_font.py`, 2240 characters).

**18. Save / Load with confirmations** ([../20_PLAYER_MENU.md](../20_PLAYER_MENU.md) §10)

- The System tab's Save row opens a **Save / Load** screen: a Save and a Load tab over one card per slot (floor, level,
  HP, gold, saved at, play time; the run's own slot is marked).
- **Every save and every load asks** (Save / Load or Cancel); overwriting a used slot and loading preselect Cancel.
  Saving into a slot makes it the run's slot; empty slots cannot be loaded.
- Checked on desktop (save to an empty slot, overwrite, load, cancel) and in headless Chrome; `smoke.player_system`'s
  reference refreshed for the renamed row.

**19. P2: story events in the Events tab** ([../20_PLAYER_MENU.md](../20_PLAYER_MENU.md) §11)

- The game tracks every event: **started** and **finished**, in order, saved with the run; every event that fires sets
  `event_<id>` (before, only whispers and rescues did). An event whose `requires` does not hold yet stays on its tile as
  "started"; a finished event's `next` events become started.
- The Events tab lists story events with their title, description and **connected** events (？？？ until met); chapters
  list their story events.
- The 34 pool events got drafted titles and descriptions (zh_TW + en) to edit in the event editor, which now has Title /
  Description fields, "in the event log" switches per event and per kind, and a "No title / description" warning.
- Toasts wait while the menu is open.
- Checked: F01's three events walked in the game; the editor in headless Chrome incl. the 29-step demo; 39/39 unit
  (`player_menu_test` extended), screenshot references refreshed for the hidden toasts; 57/62 (W16, W23 as before).

**20. P3: what the attributes do** ([../20_PLAYER_MENU.md](../20_PLAYER_MENU.md) §12)

- `stats.json` gives each attribute effects per point above its base (so Lv 1 plays as before): STR ATK +0.5, DEX
  attack hit zone +0.4, AGI bar wait −15 ms, VIT max HP +3, INT skill bonuses +2 %, LUK battle gold +3 %. All tunable.
- The Status tab shows each attribute's effect per point and in total; the HUD's ATK / DEF are the totals now.
- `levelUp.freePoints` (0 by default) turns on hand-placed points (+ buttons, Enter). Saved with the run.
- Checked at Lv 10 (`--give=exp:1500,points:3`): ATK, max HP, the wider green zone in battle, points placed; tests 57/62
  (W16, W23 as before), `smoke.player_status` reference refreshed.

**Verified (P1):**

- Desktop: every tab, the store round trip from the System tab, buying, equipping, the comparison, the
  grid's scrolling with the keyboard, dropping with its confirmation, keys that cannot be dropped, no RmlUi
  warnings.
- Web (headless Chrome): the menu, no buttons on the page, Fullscreen from the System tab with real key
  presses (`document.fullscreenElement` set, the row shows On).
- Tests: 39 of 39 unit tests (new `player_menu_test`: the rules and the item / store / stats / title data);
  screenshot tests 57 of 62 with the new `smoke.player_status / gear / items / events / system`; references
  refreshed for anim, fx and store after checking that only the HUD and the store's item names changed.
  Still failing as before: `smoke.stage` ×4 (W16) and `smoke.battle` (W23).

**Not verified (P1):** a real phone (the menu is the 1024x768 screen scaled down; the list-then-detail phone
layout is not built); Android; Fullscreen in Safari.

**Verified (2026-10-05 to 10-08):**

- **Play demo:** 29 of 29 steps, in English and 繁體中文.
- **Kinds tab:**
  - add, bad id rejected, rename (7 events follow), colour;
  - move 2 of 5, then Select all and move the rest, then delete;
  - unknown-kind warning, save preview, undo, Chinese labels.
- **Node editor probe:** passes.
- **Editors (headless Chrome):** project-folder save, Chinese names, categories and multi-select, the
  stage editor's zip (read back by Python's zip reader).
- **Game:** build OK; 38 of 38 unit tests; 27 of 32 screenshot tests. The menu, anim and fx references
  were refreshed after checking that only the intended changes differ (items now drawn, the slime's
  idle, the doors' real look). Still failing: `smoke.stage` ×4 (W16) and `smoke.battle` (W23).

**Not verified:**

- Clicking through the shell in a real, visible browser window.
- The stage editor's kind colours after `kinds.json` exists: the file has not been written yet.
- The idle and door animations on the web build and Android (the files are in `assets/media`, so
  they are packaged).
- A full play-through of the generated floors with the key fix.

**Still open:**

- ~~**The event editor cannot write files.**~~ Done later the same day: see section 7.
- **Owner decisions** (see Open Questions Q6–Q10 in [PROGRESS_REPORT.md](PROGRESS_REPORT.md)).
- **Then the Qt/C++ editors** on the game renderer ([../19_STAGE_EDITOR.md](../19_STAGE_EDITOR.md)
  section 6), once the event editor is confirmed.
- **Runtime work** for event logic, stair locks and kinds ([06](../event_editor/06_EVENT_LOGIC.md)
  section 6, [05](../event_editor/05_FLOOR_LINKS.md)).
- **Offered, not started:**
  - *Download all floors* (a zip of every floor, changed or not);
  - letting the stage editor give a new floor (made in the event editor) its grid;
  - a ramp-time setting for the battle bars;
  - a "you need a yellow key" hint at a locked door;
  - floors without automatic category groups.
- **Docs in two languages:** a change to an English document needs its `docs/zh_TW/` version
  updated too.
