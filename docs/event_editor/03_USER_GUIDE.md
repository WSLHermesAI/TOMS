# 03 — User guide

Section 1 is how to work today, by hand. Section 2 is how the same jobs will go in the editor; the
[mockups](mockups/index.html) show each screen, and [the tutorial](mockups/04_tutorial.html) lets you
do sections 2.2 to 2.5 step by step.

## 1. Today: editing the JSON by hand

### Add a new event

1. Choose a pool in `assets/data/events/`: `pool_common.json` for any floor, or `pool_actNN.json`
   for one chapter.
2. Add a record to its `"events"` array:
   ```json
   { "eventId": "ev_village_scarecrow", "kind": "whisper", "text": "ev_village_scarecrow.text" }
   ```
3. Add the text to `assets/data/text.json` under `"strings"`, one entry per language:
   ```json
   "ev_village_scarecrow.text": { "zh_TW": "稻草人轉過頭來看你。", "en": "The scarecrow turns its head to watch you.", ... }
   ```
4. List the id in the `"events"` array of each floor that can roll it, for example
   `assets/data/story/floors/F02.json`.

### Check your work

There is no checker you can run yet. Check these by hand, or with a short script:

- The `eventId` is not already in another pool.
- The `text` key exists in `text.json`.
- At least one floor lists the id; otherwise it never appears.
- Every id a floor lists exists in a pool.

## 2. In the editor (planned)

Open `toms_editor` and choose the **Events** tab. It loads every pool, floor spec and
`text.json` from the project root.

### Find out where an event appears

1. Type part of the id in the events tree, or pick its pool in **章節**.
2. Click the event. The map highlights its pool, every floor that can roll it, and its text key.
   The inspector lists the floors as chips.

*Mockup: [01_flow_map.html](mockups/01_flow_map.html). Click `ev_common_cache_wall`.*

### Add a new event

1. Right-click a pool in the events tree and choose **New event**.
2. In the inspector, type the id; the text key fills in as `<id>.text`.
3. Choose the **kind**.
4. Under **Floors**, click **Add floor…** and tick the floors.
5. Type the text for each language. Leave **todo** ticked for languages you have not written.
6. Press **Ctrl+S**. The pool file, the floor files and `text.json` are saved together.

*Mockup: [02_event_inspector.html](mockups/02_event_inspector.html).*

### Rename an event

Change `eventId` in the inspector. Every floor that lists it and its text key change with it, in one
undo step.

### Fix problems

1. Tick **只看有問題的**, or open the **Problems** dock.
2. Double-click a row to select it.
3. Use the quick fix on the row, or edit by hand in the inspector.

*Mockup: [03_problems.html](mockups/03_problems.html), which shows the real problems in today's data.*

### Fill in chapters 4–10

Floors F22–F70 already name 92 event ids that have no pool. In the Problems dock, select the
*Missing event id* rows for one chapter and choose **Create events in pool…**, then pick (or
create) `pool_act04`. The editor makes one record per id and links each one to its placeholder text.
Then set each event's kind and write its text.

## 3. Shortcuts (planned)

| Key | Action |
|---|---|
| Ctrl+S | Save changed files |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+F | Focus the events tree search |
| F5 | Reload from disk |
| Del | Delete the selected event (asks first if floors use it) |
| Drag / wheel | Pan / zoom the map |
