# 05 — Floor links: several stairs per floor, locked by events

**Status:** design and editor mockup ([tools/web_editors/event_editor.html](../../tools/web_editors/event_editor.html), part 4).
The game does not read this format yet; section 5 lists the runtime work.

## 1. What changes

Today every floor has exactly one stair up and one stair down:

- each floor spec has `"nextFloor": "F51"`, and the chain F01 → F02 → … → F70 is the whole map;
- the game copies that into `Stage::up` / `Stage::down` (`src/core/game/core/stage.h`,
  `game_assets.cpp`), and each grid has one `U` and one `D` tile.

The new rule:

- **A floor can have any number of stairs, up or down, to any floor.** For example, F50 goes up to
  F51 *and* to F96.
- **A stair can be locked by an event.** The F50 → F96 stair stays hidden until the player has
  finished a named event, for example `ev_common_relic_road`. Then its stair tile appears.

## 2. Data format

A floor spec gets a `stairs` list:

```json
// assets/data/story/floors/F50.json
{
  "id": "F50", "act": "ch_08", ...
  "nextFloor": "F51",
  "stairs": [
    { "dir": "up",   "to": "F51" },
    { "dir": "down", "to": "F49" },
    { "dir": "up",   "to": "F96", "unlock": { "eventDone": "ev_common_relic_road" } }
  ]
}
```

| Field | Meaning |
|---|---|
| `dir` | `up` or `down`: which stair tile and arrival side to use |
| `to` | The floor id it leads to. Any floor, in any chapter, including floors past F70 such as F96 |
| `unlock.eventDone` | Optional. The stair is hidden until this event has been finished in the current run |

**Compatibility.** A floor with no `stairs` list keeps today's behaviour: up to its `nextFloor`, and
down to the floor whose `nextFloor` is this one. So the 70 existing floors need no change, and the
editor writes a `stairs` list only into floors you edit. `nextFloor` stays in the file for older
builds; when both are present, `stairs` wins.

**A new floor** is just a new spec file. F96 can belong to any chapter (the editor copies the
chapter of the floor it was created from). Its grid, `F96.stage.json`, is generated from the spec like
every other floor's.

## 3. Rules for the game (when implemented)

1. **Event finished** means the player completed that event in this run: the same moment its tile
   is cleared today (`game_input.cpp`, the `event:` branch). The run keeps a set of finished event ids,
   saved with the rest of the run, and reset by a new game.
2. **Hidden, not just closed.** A locked stair's tile is drawn as floor until its event is done; then
   the stair appears, ideally with a short toast ("A stair appeared"). If the player is already on the
   floor when the event finishes, the stair appears without leaving the floor.
3. **Arrival.** Going through a stair from A to B lands on B's stair that leads back to A. If B has
   no such stair (a one-way link), the player lands on B's first stair of the opposite direction, or on
   `player_start`.
4. **The unlock event must be able to appear.** A floor's `events` list is what it *can* roll; the
   generator must always place an event that some stair waits for, or the stair could never open.
5. **Grids.** Each stair needs its own tile. The legend gives one letter per stair, for example
   `"U": "stairs_up:F51"`, `"V": "stairs_up:F96"`, `"D": "stairs_down:F49"`. The generator places
   one tile per entry in `stairs`.

## 4. What the editor checks

| Rule | Severity | Meaning |
|---|---|---|
| Missing floor | ⛔ error | A stair leads to a floor id with no spec. Fix: **Create floor** (or retarget the stair) |
| Unknown unlock event | ⛔ error | `unlock.eventDone` names an id no pool defines |
| Unreachable floor | ⚠ warning | No path from F01 reaches this floor through open stairs, or locked stairs whose event can be rolled on a floor reached before them. Catches a lock that waits for an event only on the far side, or on no floor at all |

The reachability check starts at F01. It follows open stairs, and follows a locked stair once some
floor already reached can roll its event. It repeats until nothing new is reached.

In the mockup:

- **Floor map** (bottom tab): one row per chapter, one arrow per stair. A locked stair is gold and
  dashed, labelled 🔒 with its event. A stair to a missing floor ends at a red ⛔ placeholder. An
  unreachable floor has an orange dashed border.
- **Floor inspector** (click a floor): chapter, the stairs table (direction, target, *unlock when
  done*, ✕), **+ Add stair**, *Reached from*, and the events it can roll.
- **Save** writes the `stairs` list into each edited floor, and new floors as new files.

## 5. Runtime work (not done)

| Where | Change |
|---|---|
| `stage.h` | Replace `up` / `down` with a list of `{ dir, to, unlockEvent, x, y }` |
| `game_assets.cpp` | Build that list from `stairs`, or from `nextFloor` when absent |
| Stage generator | Place one tile per stair; always place every event a stair waits for |
| `game_input.cpp` | Use the stair under the player, not a single `U`/`D`; skip hidden stairs; record finished events |
| Run state + save | Store the set of finished event ids |
| `game_scene_draw.cpp` | Draw a locked stair as floor; show it (and toast) when its event finishes |
| Arrival | Land on the stair that leads back (section 3.3) |

## 6. Open questions

- Should a locked stair be **invisible**, or **visible but closed** (with a hint)? This design hides it.
- Should finished events reset on **rebirth**? (`flags.json` has `run` and `meta` scopes for flags.)
- Should one stair wait for **several** events (all of them), or allow a flag as well? The format
  leaves room: `unlock` is an object, so `{ "eventsDone": [...] }` or `{ "flag": ... }` can be added
  later.
