# 28 — The save system

How the game keeps a player's progress: the three save slots, what a slot file holds, when it is written, how it is
protected against loss, how old saves keep loading after the game changes, and how floor objects are identified in a
save. Code: `src/core/game/save/` (`save_system.*`: the schema and file I/O; `save_slots.*`: the slots), the game
side in `src/core/game/core/game_title_glue.cpp`.

## 1. Slots and files

| Platform | Where |
|---|---|
| desktop | `save/slot1.json` … `slot3.json` next to the game (`TOMS_SAVE_DIR` overrides) |
| web | `/save/` in the browser's IndexedDB (IDBFS) |
| Android | the app's private folder (`SDL_GetPrefPath`) |

Beside each slot: `slotN.json.bak` (the save before the last one, section 3) and, for a moment during a write,
`slotN.json.tmp`. Settings are separate (`save/settings.json`); they survive a new game.

**Continue** lists the slots; choosing one loads it (`Game::continueFromSlot`), and from then on the autosave writes
to that slot. An empty slot offers a new game in it.

## 2. What a slot holds

```json
{
  "saveVersion": 1, "savedAt": "2026-10-18 10:00:00", "playTimeSec": 3725, "stageName": "...",
  "meta": { "schemaVersion": 3, "storyFlags": [], "cycleIndex": 1, "endingsSeen": [], "forgeRecipesKnown": [] },
  "run": {
    "schemaVersion": 3, "currentStageId": "F03", "floor": "F03",
    "player": { "hp": 210, "atk": 18, "key_yellow": 2, "inv": ["treasure_map"], "weapon": "iron_sword" },
    "entityStatus": { "F03#o12": "Defeated", "F03#o7": "Collected", "F02#o3": "Opened" },
    "vars": { "elder_trust": 2 }, "flags": { "met_elder": true },
    "eventsStarted": ["e_lost_key"], "eventsFinished": ["e_intro"], "eventCounts": { "e_daily_gift": 3 },
    "npcTalk": { "elder": [1, 4, 0] }, "choices": {}, "counters": { "humanity": 1 }, "sideStories": {}
  }
}
```

- `meta`: progress that survives a rebirth (story flags, endings seen, permanent bonuses, forge recipes).
- `run`: this playthrough. **Defeated monsters, picked-up items, opened doors and gates, finished map events** are in
  `entityStatus` (section 5). **Event variables** are in `vars` (only the ones that changed; the rest are at their
  default from `story/vars.json`), with `flags`, the event log, repeat counts and each NPC's talk position.

## 3. When it is written, and the guards against loss

- **Autosave:** a change marks the run dirty; it is written at most every 3 s. A floor change, leaving a shop, a new
  game and a run's end write at once.
- **On quit and in the background** (`Game::flushSave`): the window closing (`SDL_EVENT_QUIT`), Android sending the app
  to the background, and the web page being hidden or closed (`pagehide`, `visibilitychange`) write the run at once,
  so the autosave's last seconds are not lost.
- **Atomic write, flushed to the disk** (`writeJsonAtomic`): the save is written to `.tmp`, flushed to the disk
  (`_commit` / `fsync`, so a power cut after the rename cannot leave an empty file), then renamed over the slot.
- **Backup:** before the rename, the current slot file becomes `.bak` -- only if it still reads, so a damaged file
  never replaces a good backup.
- **Loading falls back to the backup:** if the slot file is missing or unreadable, its `.bak` loads; the Continue page
  marks it "From backup" and a message says so after loading.
- **Damaged is not empty:** if neither file reads, the slot shows "Damaged". It is not loaded and not offered for a new
  game (that would write over a file that might still be recovered by hand); the in-game Save tab can still save over
  it on purpose.
- **A save from a newer game version** (a higher `saveVersion` or `schemaVersion`) still loads, but is never written
  over: the run plays without saving to that slot, the Continue page marks it "Newer version", a message says so, and
  the Save tab does not offer that slot. Its unknown fields are kept.
- **Deleting a slot** removes its backup too.

## 4. Changing the save format (versions and migrations)

Every slot carries versions: `saveVersion` (the slot header, `kSlotSaveVersion`), and `schemaVersion` in `meta` and
`run` (`kMetaSaveSchemaVersion`, `kRunSaveSchemaVersion`).

- **Adding a field** needs nothing but a default: the reader takes the default when an older file lacks it (every
  field so far was added this way). Write it in `toJson` and read it with `j.value("name", default)`.
- **Renaming, removing, splitting a field, or changing what it means** needs a migration: raise the schema version and
  add a step to `kMetaMigrations` / `kRunMigrations` in `save_system.cpp`. Steps run in order (v1 → v2 → v3 …) on the
  json before it is read, so a save from any older version comes up to date. A `static_assert` reminds you to add the
  step when the version is raised.
- **Never refuse an old save.** `save_test` loads a version-1 file to keep this true.

## 5. Floor objects in the save: stable ids

A floor object whose state is saved (monster, item, key, event, gate, door) has a stable id in its stage file:
`"objects": { "5,3": { "uid": "o12" } }`. The save keys its status by it: `"F03#o12": "Defeated"`.

- **Why:** keyed by position (`"F03|5,3"`, the old way), moving an enemy in the stage editor after players have saves
  would mark the wrong cell -- a new enemy there would start out defeated, and the moved one would come back.
- **The stage editor** gives every such object a uid (`o1`, `o2` … unique on the floor) on each committed edit; the uid
  moves with the object, a new object gets a new one, and a copied one is renumbered. The object settings window keeps
  it.
- **`tools/add_stage_uids.py`** gave the shipped stage files their uids (`--check` lists any file lacking them).
- **Old saves:** when a floor loads, a status saved under an object's old position key moves to its uid key
  (`Game::migrateStatusKeys`). An object without a uid (an old or hand-written stage file) is still keyed by position.

Do not renumber uids by hand on a released floor: a player's save refers to them.

## 6. Why JSON (not SQLite or MongoDB)

- **MongoDB** is a database server: for cloud saves behind a game server, not for a local save file.
- **SQLite** would work (transactions for free), but a save is one tree of state, so it would mostly store a JSON blob;
  schema changes still need a version and migrations (`PRAGMA user_version`), and it adds a dependency and a wasm build
  for the web.
- **Versioned JSON with an atomic write, a backup and migrations** is the common choice for single-player games and is
  what this is. If size ever matters, nlohmann/json writes the same structure as MessagePack or CBOR.

## 7. Tests

`save_test` (unit): round-trips, the backup after a second save, loading from the backup when the slot is cut off, a
damaged file never replacing the backup, damaged versus empty, a newer save loading but not being overwritten (and its
unknown fields kept), a version-1 file loading with defaults, uid keys, deleting a slot with its backup.
`stage_core_test`: uids are present, kept on a move, new for a new object, and unique.
