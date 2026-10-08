# Progress Report — Log Part 6 (2026-10-05 evening to 2026-10-08)

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
| **2026-10-08** | not committed | One folder for both editors with a switch button; tutorial collapses; useless top menu removed; **Kinds** tab; old mockups deleted |

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

**Verified** (headless Chrome):

- **Play demo:** 29 of 29 steps, in English and 繁體中文.
- **Kinds tab:**
  - add, bad id rejected, rename (7 events follow), colour;
  - move 2 of 5, then Select all and move the rest, then delete;
  - unknown-kind warning, save preview, undo, Chinese labels.
- **Node editor probe:** passes.

**Not verified:**

- Clicking through the shell in a real, visible browser window.
- The stage editor's kind colours after `kinds.json` exists: the file has not been written yet.

**Still open:**

- **The event editor cannot write files.** *Save…* shows the changes to copy by hand; only the stage
  editor writes, through *Open project folder*. A real save for the event editor was offered, not
  started.
- **Owner decisions** (see Open Questions Q6–Q10 in [PROGRESS_REPORT.md](PROGRESS_REPORT.md)).
- **Then the Qt/C++ editors** on the game renderer ([../19_STAGE_EDITOR.md](../19_STAGE_EDITOR.md)
  section 6), once the event editor is confirmed.
- **Runtime work** for event logic, stair locks and kinds ([06](../event_editor/06_EVENT_LOGIC.md)
  section 6, [05](../event_editor/05_FLOOR_LINKS.md)).
