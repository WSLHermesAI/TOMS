# Event editor

**Status:** design. The data model and a read-only flow map exist in code
(`src/editor/src/event_graph.*`, `event_flow_view.*`) but are not compiled into `toms_editor` yet,
and nothing can be edited. Until the editor ships, events are edited by hand in JSON
([03_USER_GUIDE.md](03_USER_GUIDE.md), section 1).

The event editor is a tab in `toms_editor` for the random floor events: the relics, whispers,
caches, traps and rescues that each floor can roll. It shows which floors can roll
which events, lets you edit an event and its text in one place, and lists the data problems before
the game finds them.

## Documents

| File | What it covers |
|---|---|
| [01_DATA_MODEL.md](01_DATA_MODEL.md) | The three JSON files, how they link, the problem rules, and what the current data looks like |
| [02_EDITOR_DESIGN.md](02_EDITOR_DESIGN.md) | The window layout, each panel, saving, undo, and the build phases |
| [03_USER_GUIDE.md](03_USER_GUIDE.md) | How to do common jobs: by hand today, and in the editor once it ships |
| [05_FLOOR_LINKS.md](05_FLOOR_LINKS.md) | Several stairs per floor, stairs locked until an event is finished: format, game rules, checks |
| [06_EVENT_LOGIC.md](06_EVENT_LOGIC.md) | Event triggers (step, talk, variable change, link), conditions, actions, direct links, event variables, and the node-graph editor |
| [04_TUTORIAL.md](04_TUTORIAL.md) | Step by step: create an event, read the relationship diagram, find problems, fix them, save |
| [tools/web_editors/index.html](../../tools/web_editors/index.html) | The working event editor (with the tutorial) and the stage editor |

## The HTML editors

They are static HTML with a little JavaScript, and they run on a copy of the real event data
(`tools/web_editors/data_snapshot/`, refreshed by `tools/web_editors/refresh_data.cmd`).

**The editors live in [tools/web_editors/](../../tools/web_editors/index.html)** with the stage editor: open `index.html` (or run
`serve.cmd`) and switch between them with the **⇄** button. The event editor includes the **tutorial**, 29 steps in a working mini-editor:

1. **Create an event:** create it, name it, write its text, connect it to floors, watch the
   relationship diagram, and save.
2. **Find problems:** the Problems tab; an orphan event; a missing event id.
3. **Solve them:** connect the orphan, create the missing event in a new pool, fill all of ch_04 at
   once, and save.
4. **Connect floors:** on the Floor map, give F50 a second stair up to a new floor F96, lock it until
   `ev_common_relic_road` is done, and save ([05_FLOOR_LINKS.md](05_FLOOR_LINKS.md)).
5. **Event logic:** a variable two events add to, an event fired by talking to the villager once it
   reaches 2, a direct link to another event, and the **Logic graph** node editor (after
   imgui-node-editor), where dragging pins edits the same data ([06_EVENT_LOGIC.md](06_EVENT_LOGIC.md)).

Each step has *Show me*, **▶ Play demo** runs the whole thing by itself, and Save shows the exact
change to each JSON file. The tutorial page is in English or 繁體中文 (🌐 selector in the top bar).
[04_TUTORIAL.md](04_TUTORIAL.md) is the same walk-through as a document.

The HTML editors show the intended look and behaviour, not a promise of pixels: the real editor is Qt
Widgets in the same dark theme as the anim and particle editors.

## Next steps

1. **Phase 1:** add the four files to `src/editor/CMakeLists.txt` and open `EventFlowView` as a tab.
   That gives a read-only map and problem count with no new code.
2. **Phase 2:** the inspector, saving, and undo.
3. **Phase 3:** the problems panel, and creating or deleting events and pools.

[02_EDITOR_DESIGN.md](02_EDITOR_DESIGN.md) section 5 has the detail.
