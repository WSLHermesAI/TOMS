# 02 — Editor design

The event editor is a tab inside `toms_editor` (Qt6 Widgets), built like the anim and particle
editors: docks around a central view, the same dark theme, a History dock, and automatic backups.
The HTML event editor in [tools/web_editors/](../../tools/web_editors/index.html) shows each part working.

## 1. Layout

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ File  Edit  View                                                    [Save]   │
├──────────────────────────────────────────────────────────────────────────────┤
│ 章節:[(全部)▾]  類型:[(全部)▾]  樓層:[ 1]-[70]  ☐只看有問題的   34 events · 4 ⚠ │
├────────────────┬────────────────────────────────────────┬────────────────────┤
│ Events (tree)  │  Flow map                              │ Inspector          │
│ ▾ pool_common  │                                        │ eventId            │
│   relic_road   │  pool ──▶ event ──▶ text key            │ kind     [relic▾]  │
│   cache_wall   │  F01 ───▶ event                        │ pool     [common▾] │
│ ▸ pool_act01   │                                        │ floors   F01 F03…  │
│ ▸ pool_act02   │                                        │ text zh_TW / en …  │
├────────────────┴────────────────────────────────────────┴────────────────────┤
│ Problems (4)  │  History                                                     │
└──────────────────────────────────────────────────────────────────────────────┘
```

## 2. Panels

### Filter bar (exists in `EventFlowView`)

| Control | Effect |
|---|---|
| 章節 (pool) | Show one pool and the floors and text connected to it |
| 類型 (kind) | Show one event kind |
| 樓層 (floors) | A floor range, 1–70 |
| 只看有問題的 (problems only) | Hide everything that has no problem |
| Summary | Node counts and the number of problems |

### Flow map (exists, read-only)

Columns from left to right: **Floors · Pools · Events · Text keys**. Floors connect to events from
the left, and pools connect to events from the left as well, so every edge into an event points
right. Colours: pools blue, events by kind, floors grey, text keys green; anything with a problem
gets an orange border.

- Drag to pan, wheel to zoom.
- Click a node: highlight it and every edge that touches it, and select it in the inspector.
- Double-click a floor: open the floor spec in the stage editor tab.

### Events tree (new)

Pools as folders, events inside. It is the quickest way to find something by name. Right-click
offers *New event*, *Duplicate*, *Delete*, *Move to pool…*.

### Inspector (new)

For an **event**:

| Field | Editing |
|---|---|
| `eventId` | Text box. Renaming updates every floor that lists it and renames the `.text` key, in one undo step |
| `kind` | Drop-down of the known kinds |
| Pool | Drop-down; changing it moves the record to that pool file |
| Floors | Chips for each floor that lists the event, plus *Add floor…*. This edits the floor files |
| Text | One multi-line box per language, with a *todo* tick, and a preview in the game font |

For a **floor**: its `act`, and its event list in order, with *Add* and *Remove*. For a **text key**:
the per-language boxes only.

### Problems (new)

One row per problem: an icon for the rule, the id, and where it was found. Double-click selects the
node. Some rules have a quick fix:

| Rule | Quick fix |
|---|---|
| Missing event id | *Create event in pool…* (pre-fills the id, and the text from the placeholder key) |
| Orphan event | *Add to floors…* or *Delete* |
| Missing text key | *Create text* |

## 3. Saving

- The editor holds the whole graph in memory. **Save** (Ctrl+S) writes only the files that changed:
  the touched pool files, floor specs and `text.json`.
- It writes JSON with 2-space indentation and keeps key order, so diffs stay small. `_comment`
  blocks are kept.
- Before overwriting, it copies each file to the backup folder, as the other editors do.
- If a file changed on disk since it was loaded, Save asks before overwriting.
- `reload()` re-reads from disk; it asks first if there are unsaved changes.

## 4. Undo

Every edit is one `QUndoCommand` and shows in the History dock. Edits that touch several files
(rename an id, move an event to another pool) are one command, so one Ctrl+Z restores all of them.

## 5. Build phases

| Phase | Work | Size |
|---|---|---|
| 1 | Add `event_graph.*` and `event_flow_view.*` to `src/editor/CMakeLists.txt` (with `QT_NO_EMIT` like the other files). Create the tab in `editor_window.cpp`. Add a headless test for `loadEventGraph` against a small fixture. | Small |
| 2 | Click selection and highlighting. The inspector. A writer that saves pools, floors and `text.json`. Undo. | Medium |
| 3 | The events tree and the problems panel with quick fixes. The extra rules from 01 section 3. Zoom, and a layout that keeps 70 floors readable. | Medium |
| 4 | Batch work: create `pool_act04` … `pool_act10` from the ids floors already use; CSV export and import of text for translators. | Small |

## 6. Open questions

- Should a floor's event list stay explicit, or should floors only name their pools and leave the
  roll to the generator? Today it is explicit, and the editor follows the data.
- Do `_v2` … `_v5` ids mean variants of one event (same kind, different text)? If so, the
  inspector could group them under one event.
- Which languages are required before release? That decides whether *untranslated* is a warning or
  an error.
