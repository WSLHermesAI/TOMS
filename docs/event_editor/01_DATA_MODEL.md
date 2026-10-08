# 01 — Data model

The event editor has no file format of its own. It reads and writes three kinds of file that the game
already uses. The model lives in `src/editor/src/event_graph.h`, which has no Qt in it, so tests can
check the rules on every platform.

## 1. The files

### Pools: `assets/data/events/pool_*.json`

One file per pool. A pool groups related events (`pool_act01` holds the village events); it does
not decide where they appear. Each floor's own `events` list decides that, from any pool, because
one floor can host several characters' stories. The game does not read pool files at run time.

```json
{
  "_comment": ["S3 — the common pool ..."],
  "events": [
    { "eventId": "ev_common_relic_road", "kind": "relic", "text": "ev_common_relic_road.text" }
  ]
}
```

| Field | Meaning |
|---|---|
| `eventId` | Unique across every pool. The convention is `ev_<place>_<name>`, for example `ev_village_well`, but any name works, Traditional Chinese included (the editor refuses only `" ' < > & \| \ /`). The same goes for event variables, kinds and floor names; a floor's name is also its file name (`第一層.json`), so `: * ?` are refused there too |
| `kind` | A kind from `assets/data/events/kinds.json` (today `relic`, `whisper`, `cache`, `trap`, `rescue`, `merchant_echo`, `memory_shard`); edited in the event editor's **Kinds** tab ([06](06_EVENT_LOGIC.md)) |
| `text` | A key in `text.json`. The convention is `<eventId>.text` |

The pools that exist today:

| Pool | Events | Kinds |
|---|---|---|
| `pool_common` | 10 | relic 2, whisper 2, cache 2, trap, rescue, merchant_echo, memory_shard |
| `pool_act01` (village) | 8 | whisper 2, rescue 2, cache 2, relic, trap |
| `pool_act02` (forest) | 8 | relic 2, trap 2, cache 2, whisper, rescue |
| `pool_act03` (gate) | 8 | cache 3, relic 2, whisper, trap, rescue |

### Floors: `assets/data/story/floors/F01.json` … `F70.json`

Each floor spec lists the events it can roll in `"events"`, along with its `act` (`ch_01` …
`ch_10`), maze, enemies and story. `F01.stage.json` next to it is the generated grid; it has no
events, and the editor skips it.

```json
{ "id": "F01", "act": "ch_01", "events": ["ev_village_well", "ev_common_cache_wall", "ev_village_ledger"], ... }
```

### Text: `assets/data/text.json`

```json
{ "strings": { "ev_village_well.text": { "zh_TW": "井水映出一個不是你的人影。", "en": "...", ... } } }
```

Each key has one string per language (`zh_TW`, `en`, `zh_CN`, `ja`, `ko`, `es`). A string with
`"_todo": true` is a placeholder.

## 2. The graph

```
pool ──membership──▶ event ──text──▶ text key
floor ──can roll──▶ event
```

| Node | id | Shown as |
|---|---|---|
| Pool | `pool_common` | the file name |
| Event | `ev_village_well` | the id and its kind |
| Floor | `F01` | the floor id; also sorted and filtered by number |
| TextKey | `ev_village_well.text` | the key |

## 3. Problem rules

The loader reports three kinds of problem. Each one is something the game would otherwise hit at
run time or never show:

| Rule | Meaning | What happens in game |
|---|---|---|
| **Orphan event** | An event is in a pool, but no floor lists it | It can never appear |
| **Missing event id** | A floor lists an id that no pool defines | The roll finds nothing for that slot |
| **Missing text key** | An event's `text` key has no entry in `text.json` | The player sees the raw key |

Rules worth adding in the editor (not in `event_graph.cpp` yet):

- **Untranslated text:** every language has the same string, or the string has `"_todo": true`.
- **Duplicate id:** the same `eventId` in two pools. Today the second one is silently skipped.
- **Pool mix:** a floor whose events break the generator's rule of at least one relic or whisper and
  at least one cache (the constraint noted in `pool_common.json`'s comment).

## 4. What the current data shows

Run against the repo on 2026-10-05:

- **4 orphan events:** `ev_village_grave_new`, `ev_village_dog_bowl`, `ev_village_bell_rope`,
  `ev_gate_broken_ram`.
- **376 missing event references, to 92 different ids, on 49 floors (F22–F70, chapters ch_04 to
  ch_10).** There are no pool files past act 3. The ids are already named, for example
  `ev_cavern_echo_wall` and `ev_common_relic_road_v3`, and each has a placeholder in `text.json`
  under the bare id (no `.text` suffix), marked `"_todo": true`, whose text is the id itself.
- **0 missing text keys** for events that are in pools.
- **No translations yet:** the existing event strings hold the same Chinese text in every language.

So the editor's first real job is to create `pool_act04` … `pool_act10` from the ids the floors
already use, and to write their text.
