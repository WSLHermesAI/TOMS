# 07 — Key gates and event chains

A **key gate** opens only for one particular item: not a colour key, but a story item that an event gives. The room
behind it can hold an event, an NPC or an item to pick up. Event **actions** hand out that item. Together with
`requires` and `next` ([06_EVENT_LOGIC.md](06_EVENT_LOGIC.md)), a long event can wake further events across
several floors. 中文：[zh_TW/event_editor/07_KEY_GATES.md](../zh_TW/event_editor/07_KEY_GATES.md).

## 1. The gate

In a floor's map (`F??.stage.json`), a legend entry `gate:<item id>`:

```json
"legend": { "Q": "gate:key_bellward" },
```

| | Rule |
|---|---|
| Opens | when the player bumps it while holding that item; the item is used up, the gate is gone |
| Locked | otherwise: the player stays put and reads *"This gate is carved with a pattern. Only the {item} opens it."* |
| Stays open | yes, after leaving and coming back (entity status *Opened*, like doors) |
| Blocks | walking and click-to-walk until opened |
| Looks | a door with the key's colour washed over it: the item's `tint` in `items.json` (default violet), so it never looks like a yellow / blue / red key door |
| Any char | the gate does not need a fixed char (unlike `y` / `b` / `r` doors) |

The key is an item of type `story` in `data/items.json`. It goes into the inventory (not a counted colour key), cannot
be *used* there (that would only lose it), and `"tint"` colours its gate:

```json
"key_bellward": { "type": "story", "sprite": "key_red.png", "tint": [0.62, 0.3, 0.92],
                  "name": "item.key_bellward.name", "desc": "item.key_bellward.desc", "important": true }
```

**Stage editor:** the palette has **Gates (one key)**, one gate per story item, and **Gate keys** (the story items,
to put a key on the map instead of giving it by an event). The inspector shows which key opens a gate.

## 2. Event actions (now run by the game)

An event's `actions` run once, when it fires:

| Action | Does |
|---|---|
| `{ "type": "give", "itemId": "key_bellward" }` | gives the item; the player reads *"Received: {item}"* |
| `{ "type": "addCounter", "counter": "insight", "delta": 1 }` | adds to a story counter (clamped) |
| `{ "type": "setFlag", "flag": "flag_x" }` | sets a run flag |

`addVar` / `setVar` (event variables) are not run yet ([06](06_EVENT_LOGIC.md) section 6).

A finished event's tile no longer comes back when the player returns to the floor, so an event cannot give its items
twice.

## 3. Example: the bell chain (chapter 1)

Six events in `pool_act01.json`, low floors → high → low again:

| # | Floor | Event | Waits for (`requires`) | Then (`next`) | Actions |
|---|---|---|---|---|---|
| 1 | F03 | `ev_bell_cracked` — The Cracked Bell | — | 2 | |
| 2 | F05 | `ev_bell_ringer_key` — The Bell-ringer's Key | 1 done | 3 | give `key_bellward` |
| 3 | F04, behind the bell gate | `ev_bell_ringer_room` — The Bell-ringer's Room | 2 done | 4 and 5 | |
| 4 | F06 | `ev_bell_rope` — The Frayed Bell Rope | 3 done | 6 | |
| 5 | F02 | `ev_bell_clapper` — The Lost Clapper | 3 done | 6 | |
| 6 | F03 | `ev_bell_rings` — The Bell Rings Again | 4 **and** 5 done | — | +1 insight, give `gem_atk` |

```
F03 cracked bell ─▶ F05 bell-ringer ─▶ (key) ─▶ F04 gate ▶ room ─┬▶ F06 rope    ─┐
                                                                 └▶ F02 clapper ─┴▶ F03 the bell rings
```

- "Waits for" is `{ "type": "runFlagSet", "flag": "event_<id>" }` (set when an event fires); the last one is an `all` of
  two. Stepping on an event that still waits only *starts* it: it stays on its tile, and the player's event log lists it.
- On F04 the bell-ringer's room is the 2 × 2 pocket at x 16–17, y 13–14; the gate `Q` at (16, 12) is its only way in.
- Text: titles, descriptions and lines in zh_TW and en in `text.json` (`ev_bell_*`, `item.key_bellward.*`,
  `gate.locked`, `gate.opened`, `event.gotItem`).

**In the event editor** each event shows *Active only when … is done* and *Then fire …*; the Logic graph and the
relationship diagram draw the chain. **In the stage editor** the floors show the events, and F04 the gate.

## 4. Checks

- `unit.event_chain_test`: every action names a real item / counter, every "waits for" a real event; every gate
  names a story item some event gives or some floor places; the bell chain's links and floors; on F04 the room is
  out of reach while the gate is shut and reached once it is open.
- `smoke.bell_gate`: the game itself, on F04 with the key: walks to the gate, opens it, walks into the room, fires the
  event (`tests/golden/bell_gate.png`).

## 5. Making your own

1. Add a `story` item to `items.json` with its `name` / `desc` text and a `tint`.
2. In the stage editor, paint the gate (**Gates**) where the room's only opening is; put the room's event, NPC or item
   inside.
3. In the event editor, give the item from an event (**Then do → give**), and link the events with *Active only when*
   and *Then fire*.
4. Save. Without the editors, rebuild the event index with `node tools/web_editors/sync_floor_index.mjs`, and the
   editors' data copies with `tools\web_editors\refresh_data.cmd`.

Test option: `toms_game --goto=F04 --give=key_bellward --events-done=ev_bell_cracked,ev_bell_ringer_key` starts a new
game on F04 with the key and the chain done up to the room.
