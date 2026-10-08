# 04 — Tutorial: create events, read relationships, find and fix problems

This walks through the planned editor in three parts, on a copy of the real TOMS event data:

| Part | Steps | What you do |
|---|---|---|
| 1. Create an event | 1–6 | Add `ev_village_scarecrow`, connect it to F02 and F04, read the diagram, save |
| 2. Find problems | 7–9 | Open the Problems tab; inspect an orphan event and a missing event id |
| 3. Solve them | 10–13 | Connect the orphan, create the missing event, fill a whole chapter, save |
| 4. Connect floors | 14–20 | Give F50 a second stair up to a new floor F96, lock it behind an event, save |
| 5. Event logic | 21–29 | A variable, an event fired by talking once it reaches 2, a direct link, the node graph, save |

To try it yourself, open [tools/web_editors/index.html](../../tools/web_editors/index.html) (the event editor; ⇄ switches to the stage editor). The page is in English or
繁體中文: pick one with the 🌐 selector in the top bar. The choice is remembered, and
`event_editor.html?lang=zh_TW` or `?lang=en` opens it in that language. Every step has a
**Show me** button that does it for you. **Reset tutorial** starts again.

The tutorial panel opens by itself only the first time. After that it stays hidden so the editor has
the room; **📘 Tutorial** in the top bar opens or closes it, and **◀ Hide** in the panel closes it.
`event_editor.html?tutorial=1` always opens it.

**▶ Play demo** runs all remaining steps by itself, through the real controls: it types into the
fields, ticks the floors and clicks the buttons and diagram nodes. While it plays:

- the **demo console** (bottom right) shows **NEXT**: what the next action is and where, with a bar
  counting down to it. Below that is a timed log of every action done (✓) and every step finished.
  The *log* button folds the log away.
- a yellow **spotlight** dims the rest of the window and frames the control the next action uses,
  labelled with its area (for example *Floor picker · ch_01* or *Inspector · kind*).
- the speed box sets 0.5×, 1× or 2×. **■ Stop** (or Esc) pauses after the current action, and
  ▶ Play demo continues from that step.

The data is a copy, made by [tools/web_editors/make_data.py](../../tools/web_editors/make_data.py) (see *The data copy* at
the end). Nothing is written to `assets/`.

The editor window has these parts:

| Part | Where | Used for |
|---|---|---|
| Events tree | left | Pools and their events; **+ New event**; a red *No pool* group lists missing ids. Click any group header to fold it, or use *Expand all* / *Collapse all*. A filter on top (Ctrl+F) finds events by id, text in any language, kind, pool or floor, with a kind drop-down and *only problems* |
| Inspector | centre | The selected event's fields, floors and text, or the fixes for a missing id |
| Relationship diagram | bottom tab | What connects to what; *Focus*, or *Full map* by chapter |
| Problems | bottom tab | The validator's list, with a fix button on every row |
| Floor map | bottom tab | One row per chapter, one arrow per stair; locked stairs are gold and dashed. Click a floor to edit its stairs |
| History | right | Every edit, so you can Undo |

# Part 1 — Create an event

## Step 1 — Create a new event

Click **+ New event** at the top of the Events tree, or the **+** beside a pool name. Use the **+**
beside `pool_act01`.

A blank event, `ev_new_event_1`, appears in `pool_act01` with a green **NEW** tag, and the inspector
selects it. It shows ⚠ because no floor can roll it yet.

> **Which pool?** A pool only groups related events in one file: `pool_act01` holds the village
> events, `pool_act02` the forest, `pool_act03` the gate, `pool_common` the shared ones. It does not
> limit where an event appears. The floors you connect in step 4 decide that, from any chapter.

## Step 2 — Name it and choose a kind

1. In **eventId**, type `ev_village_scarecrow` and press Tab.
2. In **kind**, choose `whisper`.

The **text key** follows the id by itself: `ev_village_scarecrow.text`.

The id is checked as you type:

| You type | You see |
|---|---|
| `a<b` | *Any name works, Chinese too (e.g. ev_village_scarecrow or 稻草人), except empty, spaces at the ends, or " ' < > & \| \ /* |
| `ev_village_well` | *ev_village_well already exists in pool_act01* |

The kinds are `relic`, `whisper`, `cache`, `trap`, `rescue`, `merchant_echo` and `memory_shard`.
The kind sets the colour in the tree and the diagram.

## Step 3 — Write the text

Type the line the player reads:

| Language | Text |
|---|---|
| zh_TW | 稻草人轉過頭來看你。 |
| en | The scarecrow turns its head to watch you. |

The **Preview** box shows the line in the game's dialog style as you type. Languages you leave
empty count as *untranslated* in the Problems list; they do not stop you saving.

## Step 4 — Connect it to floors

1. Under **Floors that can roll it**, click **+ Add floor…**.
2. The list is grouped by chapter, so floors are easy to find. Any floor can be ticked.
3. Tick **F02** and **F04**, then click **Connect**.

Two chips appear, `F02 ch_01` and `F04 ch_01`. In the diagram, two green edges grow from F02 and F04
to the new event. The ⚠ in the tree goes away.

- To disconnect a floor, click ✕ on its chip.
- Floors from different chapters can roll the same event. The editor does not warn about it,
  because one floor can host several characters' stories.

## Step 5 — See the relationship diagram

The diagram reads left to right:

```
FLOORS      POOLS        EVENTS                 TEXT KEYS
 F02 ─────────────────▶ ev_village_scarecrow ──▶ ev_village_scarecrow.text
 F04 ─────────────────▶      ▲
          pool_act01 ────────┘
```

- A **floor → event** edge means the floor can roll the event (the floor file lists it).
- A **pool → event** edge means the event is defined in that pool file.
- An **event → text key** edge means its text lives under that key in `text.json`.

It has two modes:

| Mode | Shows |
|---|---|
| **Focus: selected event** | The selected event, its pool, its floors and its text key. The other events those floors roll are faded, so you can see what it shares a floor with |
| **Full map** | Every pool and event, and floors F01–F21 (the chapters that have pools). The selected event glows; new events are marked **NEW** |

Switch to **Full map**, then click **F02**. Everything F02 can roll lights up:
`ev_village_chapel_dust`, `ev_village_ledger`, `ev_village_well` and your new event. The status
bar lists the same. Click an event node to select it in the inspector.

## Step 6 — Save

Press **Save** or Ctrl+S. A dialog shows each file that will change. Added lines are green, removed
lines are red:

**`assets/data/events/pool_act01.json`**
```diff
   "events": [
     ...
     { "eventId": "ev_village_old_plough", "kind": "cache", "text": "ev_village_old_plough.text" },
+    { "eventId": "ev_village_scarecrow", "kind": "whisper", "text": "ev_village_scarecrow.text" }
   ]
```

**`assets/data/story/floors/F02.json`** (and the same for `F04.json`)
```diff
   "events": [
     "ev_village_chapel_dust",
     "ev_village_ledger",
     "ev_village_well",
+    "ev_village_scarecrow"
   ],
```

**`assets/data/text.json`**
```diff
   "strings": {
     ...
+    "ev_village_scarecrow.text": { "zh_TW": "稻草人轉過頭來看你。", "en": "The scarecrow turns its head to watch you." }
   }
```

Click **Write 4 files**. The editor copies each file to the backup folder first, then writes it.
The History dock shows *Saved 4 file(s)*, and the • on the Events tab clears.

If problems remain, the dialog says how many but still lets you save.

# Part 2 — Find problems

## Step 7 — Open the Problems tab

Click **Problems** above the diagram. The validator runs after every edit. On today's data it shows:

| Rule | Count | Meaning |
|---|---|---|
| ⛔ Missing event id | 92 | A floor lists an id that no pool defines |
| ⚠ Orphan event | 4 | A pool defines an event that no floor lists |
| ⓘ Untranslated | 34 | `en` is empty or the same as `zh_TW` (hidden until you click its chip) |

The other rules (Missing text key, No kind, Placeholder text) are at 0 for now; they
appear as you work. The chips at the top turn each rule on or off. The bar below them groups the
missing ids by chapter: `ch_04: create 23`, `ch_05: create 16` … `ch_10: create 11`.

Click any row and the editor selects that id and shows it in the diagram, in *Focus* mode.

## Step 8 — Inspect an orphan event

Click the row **ev_village_bell_rope · Orphan event**.

- The inspector shows an orange banner: *no floor can roll this event, so it never appears in game*.
- The diagram shows `pool_act01 → ev_village_bell_rope → ev_village_bell_rope.text`, and nothing in
  the FLOORS column. An orphan is a node with no edge coming in from a floor.

## Step 9 — Inspect a missing event id

Click the row **ev_cavern_echo_wall · Missing event id**.

```
FLOORS                      EVENTS                         TEXT KEYS
 F38 ┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄▶ ⛔ ev_cavern_echo_wall          ev_cavern_echo_wall (placeholder)
 F41 ┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄▶        (red: no pool)
```

- The red dashed edges come from F38 and F41 (`ch_06`). They point at an id that no pool defines, so
  when either floor rolls it there is no event to show.
- `text.json` has only a placeholder under the bare id, marked `_todo`, whose text is the id itself.
- The inspector shows the fixes: **Create in pool_act06**, **Create all 14 missing ids of ch_06**,
  and **Remove from floors**.

This is the biggest problem in the data: floors F22–F70 (ch_04 to ch_10) list 92 such ids,
because those chapters have no pool files yet.

# Part 3 — Solve them

## Step 10 — Fix the orphan: connect it

1. Select `ev_village_bell_rope`.
2. Click **+ Add floor…**.
3. Tick **F06**, then click **Connect**.

A green edge grows from F06, and the orphan row leaves the Problems list (4 → 3). If the event
were not wanted, **Delete** on its row would be the other fix.

## Step 11 — Fix a missing id: create the event

1. Select `ev_cavern_echo_wall` and click **Create in pool_act06**. The editor:
   - creates `pool_act06` (marked NEW in the tree), because ch_06 has no pool file yet;
   - adds the record `{ "eventId": "ev_cavern_echo_wall", "kind": "", "text": "ev_cavern_echo_wall" }`.
     The text key is the existing placeholder, so floors, pool and text line up with no new key.
2. The red node becomes a normal event, and two warnings point at it: ⚠ *No kind* and
   ⚠ *Placeholder text*.
3. Choose kind **whisper**.
4. Replace the placeholder zh_TW text with `洞壁傳回的回聲，比你的聲音晚了一拍。`, and en with
   `The cavern wall echoes you back, one beat too late.`

Missing event ids go from 92 to 91.

**Remove from floors** is the right fix instead when the id is a typo, or when the event was cut.

## Step 12 — Fix a whole chapter at once

In **Problems**, click **ch_04: create 23**. In one undo step, the editor creates every id that
ch_04's floors list:

| Ids | Go to | Kind |
|---|---|---|
| `ev_common_*_v2` … `_v5` (15) | `pool_common` | Copied from the original, e.g. `ev_common_cache_wall_v3` → `cache` |
| `ev_library_*` (8) | new `pool_act04` | Guessed from the name (`…_whisper` → whisper); otherwise left empty |

Missing event ids go from 91 to 68. Two warnings appear in their place: 23 *Placeholder text* and
5 *No kind*. That is the writing work left. Every row has a **Write text** or **Choose kind**
button that jumps straight to the field.

## Step 13 — Save and check

Press **Save**. This time the dialog lists 5 files:

- `pool_act04.json (new file)` and `pool_act06.json (new file)`, whole files in green;
- `pool_common.json`, with 15 added variant records;
- `F06.json`, which now lists `ev_village_bell_rope`;
- `text.json`, where the placeholder for `ev_cavern_echo_wall` becomes the real line.

Write them. The status bar shows what is left: 68 errors (ch_05 to ch_10, fixed the same way as
ch_04) and the warnings for the new events' kinds and text.

# Part 4 — Connect floors

The format and game rules are in [05_FLOOR_LINKS.md](05_FLOOR_LINKS.md).

## Step 14 — Open the Floor map

Click **Floor map** above the diagram. Each row is a chapter (ch_01 … ch_10), each box a floor, each
arrow a stair. Today every floor has one stair up and one down, so the map is one chain F01 → F70.

## Step 15 — Select floor F50

Click **F50**. The floor inspector shows its chapter, its **Stairs** (↑ F51, ↓ F49, taken from the
old `nextFloor` chain), *Reached from*, and the events it can roll (red = id no pool defines).

## Step 16 — Add a stair up to F96

Click **+ Add stair**, leave **↑ up**, type `F96` as the target and press Tab. F96 does not exist,
so the row says *⛔ F96 does not exist yet*, the map draws a red ⛔ F96 stand-in, and Problems
lists ⛔ *Missing floor*.

## Step 17 — Create floor F96

Click **Create floor F96**. The new floor gets F50's chapter (`ch_08`), no events, and a stair ↓
back to F50. The *Missing floor* error is gone.

## Step 18 — Lock the stair behind an event

In the F96 row, set **unlock when done** to `ev_common_relic_road`. The list shows the events F50
can roll first. In the game, the stair stays hidden until the player finishes that event.

If you pick an event that no reachable floor can roll, for example the orphan
`ev_village_grave_new`, F96 turns ⚠ *Unreachable*: the stair could never open. An id no pool
defines gives ⛔ *Unknown unlock event*.

## Step 19 — Watch the floor map

F50 → F96 is a gold dashed arrow labelled 🔒 `ev_common_relic_road`; F50 → F51 is unchanged. Click
**F96**: *Reached from* shows ↑ F50 🔒.

## Step 20 — Save the floors

**Save** shows two files:

```diff
 assets/data/story/floors/F50.json
   "nextFloor": "F51",   (kept for older builds; stairs wins)
+  "stairs": [
+    { "dir": "up", "to": "F51" },
+    { "dir": "down", "to": "F49" },
+    { "dir": "up", "to": "F96", "unlock": { "eventDone": "ev_common_relic_road" } }
+  ],

 assets/data/story/floors/F96.json  (new file)
+{
+  "id": "F96", "act": "ch_08", "role": "normal",
+  "events": [],
+  "stairs": [
+    { "dir": "down", "to": "F50" }
+  ]
+}
```

Floors you never touched keep only their `nextFloor` and are not rewritten.

# Part 5 — Event logic

The format, rules and runtime work are in [06_EVENT_LOGIC.md](06_EVENT_LOGIC.md).

| Step | What you do | What it writes |
|---|---|---|
| 21 | Open **Variables** | — |
| 22 | Add `well_clues` (int, starts at 0, per run) | `story/vars.json` (new file) |
| 23–24 | On `ev_village_well` and `ev_village_ledger`: *Then do* → add to variable `well_clues` 1 | `"actions": [{ "type": "addVar", … }]` |
| 25 | New event `ev_village_elder_thanks`: fires when the player talks to `villager` (F07, F35), active when `well_clues ≥ 2`, adds 1 to counter `insight` | `trigger`, `requires`, `actions` |
| 26 | *Then fire* → `ev_village_grave_new`, whose trigger becomes *another event links to it* | `"next": [...]`, `"trigger": { "on": "chain" }`; the grave is no longer an orphan |
| 27 | Open **Logic graph**: the node editor. Click the elder's node to animate its links | — |
| 28 | Right-click empty space → add `ev_village_dog_bowl`; drag the elder's **Then ▶** to its **▶ Fire** | another `next` link |
| 29 | Save | `pool_act01.json`, `vars.json`, `text.json` |

In the node graph: drag a pin to a pin to connect (pins that fit turn green), drop a link on empty
space for a menu of nodes that fit, click a link and press Delete to remove it, right-drag to pan,
wheel to zoom, F to fit, ⛶ to maximize.

## Undo

Every step is one entry in History. Ctrl+Z undoes the last one, including multi-file edits: undoing
*Connect ev_village_scarecrow to F02, F04* removes it from both floors. Renaming an id also renames it
in every floor that lists it, in one step.

## What to try next

- Change **pool** to `pool_common`. Save then shows a line removed from `pool_act01.json` and added
  to `pool_common.json`.
- Rename the event after connecting it, and see that F02 and F04 follow.
- Create ch_05 to ch_10 the same way, and watch the error count reach 0.
- Pick a chapter in **Full map** (the drop-down next to it) to see that chapter's floors, pools and
  red missing ids together.

## The data copy

The editors use a copy of the event data, never `assets/` itself:

```
python tools/web_editors/make_data.py
```

It reads `assets/data/events/pool_*.json`, the floor specs `assets/data/story/floors/F??.json`, the event
strings from `text.json`, and the variables, counters, flags and kinds, and writes them into
`tools/web_editors/data.js`, the file the page loads. `tools\web_editors\serve.cmd` runs it every time it
starts, so the editor always opens on the current data; without the server, run
`tools\web_editors\refresh_data.cmd` after the data changes. The counts in this
document are from the copy made on 2026-10-05.
