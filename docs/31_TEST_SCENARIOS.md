# 31 — Test scenarios: start any scene from a setting file

A **scenario** starts the game straight in one scene from setting files, so each part of the game can be tested
alone: a battle, a dialogue, the store, a floor at a saved moment. You don't have to walk there through the title and
the maze. Every scenario is also an automated test.

```
toms_game --scenario=tests/scenarios/battle_slime          # play it (a folder means its scenario.json)
toms_game --scenario=tests/scenarios/battle_slime --scenario-test   # run its test: its frames and keys, then the check
toms_game --arena=slime,bat:wand                           # the weapon test arena: these enemies in turn, with the wand
tools\test.cmd -L scenario                                  # every scenario test
tools\test.cmd -R scenario.store_buy                        # one
```

**In `toms_editor`:** the **Scenarios** dock (a tab beside Stages) lists every scenario of `tests/scenarios/` with
its scene. **Double-click one to play it** in the Play tab: a fresh game, straight in that scene. The `_comment` is
its tooltip. Right-click a scenario to play it, open its folder or edit its `scenario.json`; right-click anywhere to
reload the list.
- **File > Open scenario folder...** plays one scenario folder, or lists a whole folder of them as another group.
- Downloaded scenarios (`tests/scenarios/_cloud/`, [32](32_DOWNLOADS_AND_DLC.md)) get a group of their own.
- `toms_editor --scenario=<folder>` starts the editor in a scenario. A scenario folder (or its `scenario.json`)
  dropped onto `toms_editor.exe` does the same.

A scenario **never writes a save**. Its run has no save slot, and a save it starts from is read from the scenario's
folder.

## 1. The folder

Each scenario is a folder in `tests/scenarios/`, holding a `scenario.json` and any other files it uses: a save, a
dialogue, a stage. A new folder becomes a new test, `scenario.<folder name>`, after the next CMake configure. Files
that several scenarios share go in `tests/scenarios/_shared/`, and a scenario pulls them in with `"include"`.

```
tests/scenarios/
  _shared/mid_game.json         shared: "save": "save_mid_game.json"
  _shared/save_mid_game.json    a save (the game's own slot format)
  stage_from_save/scenario.json
  stage_from_save/save.json     this scenario's own save
  dialogue_from_file/scenario.json
  dialogue_from_file/dialogue.json
  ...
```

## 2. The setting file

Everything except `scene` is optional. The game applies the fields in this order:

```json
{
  "_comment": "what this scenario shows",
  "include": ["../_shared/mid_game.json"],
  "scene": "battle",
  "save": "save.json",
  "stage": "F04",
  "stairLocks": [ { "floor": "F01", "event": "ev_village_well" } ],
  "flags": ["hub.village"],
  "eventsDone": ["ev_elder_north_road"],
  "player": { "x": 3, "y": 5, "hp": 200, "maxhp": 300, "atk": 30, "def": 20, "gold": 500, "exp": 0, "lv": 8,
              "keys": { "yellow": 2, "blue": 1, "red": 0 }, "attrs": { "str": 3, "agi": 2 },
              "inventory": ["potion_red"], "give": ["wand"], "equip": ["wand"] },
  "battle": { "enemy": "slime", "hp": 300 },
  "test": { "frames": 60, "keys": "enter@20" },
  "expect": { "scene": "store", "inventoryHas": ["potion_red"] }
}
```

| Field | What it does |
|---|---|
| `include` | Other setting files, read first. This file's fields go on top: objects merge key by key, anything else is replaced. Includes may include others. |
| `scene` | The scene to open (section 3). |
| `save` | Start from this save file, in the game's slot format (a copy of `save/slotN.json` works). Without it, a new game. |
| `stage` | Go to this floor (`F04`), stage id (`stage02`) or stage file (`my_floor.stage.json`, next to the setting file). |
| `stairLocks` | Lock a floor's first stair up behind events: `{ "floor": "F01", "event": "ev_a" }`, or `"events": [...]` for several (all must be done, W38). |
| `flags` | Run flags to set (e.g. `hub.village` unlocks the village). |
| `eventsDone` | Events to finish as the player would. Their rewards, unlocks and notices happen. |
| `player` | `give` and `equip` first (items and gear by `items.json` id; `gold:<n>`, `exp:<n>` and `points:<n>` work in `give`), then the numbers as given. `inventory` replaces the backpack. |
| `test` | For the automated test: how many frames to run after the scene opens, and keys to press (`key@frame`, counted from the scene's first frame; the keys are those of `--keys`). |
| `expect` | What the test checks at the end (section 4). Without it, only that the scene is showing. |

A file name in a setting file is relative to **that** file. A shared file's save is found next to the shared file.

## 3. The scenes

| `scene` | Opens | Its own fields | Example folder |
|---|---|---|---|
| `title` | the title screen, as the game starts | — | `title` |
| `stage` | the walking scene: the floor with the player, at the saved or given moment | — | `stage_new_game`, `stage_from_save`, `stage_from_events` |
| `battle` | a battle with an enemy from `enemies.json` (not on the map) | `battle`: `enemy`, and optional `hp`, `atk`, `def`, `atk_interval_ms` overrides | `battle_slime`, `battle_strong_player`, `battle_bow` (the bow's battle clips; also `battle_bow_charge`, `battle_bow_idle`) |
| `battleWon` | the victory screen after that battle | as `battle` | `battle_won` |
| `arena` | **the weapon test arena** ([30](30_GAMEPLAY_PLAN.md) phase 0): fight after fight against the listed enemies in turn, full HP each time, no rewards and no story effects. After a win, Enter starts the next fight; after a loss it starts by itself. The weapon is the player's (`player.equip`), unarmed without one. `toms_game --arena=<enemy,...>[:<weapon>]` is the same without a file | `battle`: `enemies` (or `enemy`), and the same overrides as `battle`, for every fight | `arena_unarmed`, `arena_wand`, `arena_restart` |
| `dialogue` | a dialogue: one of the game's (`id`), or a file next to the setting file (`file`); optional `node` to start at | `dialogue`: `id` or `file`, `node` | `dialogue_by_id`, `dialogue_from_file` |
| `talk` | talking to an NPC: their chat line, topic or topic menu, by the events finished | `talk`: `npc` (an `npcs/` id) | `talk_elder` |
| `store` | the store (unlocked for the scenario) | — | `store_buy`, `store_from_save` |
| `menu` | the player menu on a tab | `menu`: `tab` = `status`, `gear`, `items`, `events`, `cards` or `system` | `menu_status` … `menu_system`, `menu_cards`, `events_stairs_where` |
| `saveLoad` | the Save / Load screen | `saveLoad`: `mode` = `save` or `load` | `save_load` |
| `stageSelect` | the stage select (floors reached so far) | — | `stage_select` |
| `stairsConfirm` | the "go up / down?" question on a stair | `stairs`: `to` (a floor), `up` (true / false) | `stairs_confirm` |
| `notice` | a notice made by the setup, e.g. "stairs unlocked" after a locked stair's event finishes | — | `notice_stair_unlock` |
| `ending` | an ending screen | `ending`: `id` (`e_01` … in `story/endings.json`) | `ending` |
| `hub` | a village hub location (its dialogue); its unlock flag must be in `flags` | `hub`: `location` | `hub_village` |

The node-animation and particle previews already have their own options (`--anim=<file>#<clip>`,
`--fx=<file>#<effect>`; [15](15_ANIMATION.md), [17](17_PARTICLES.md)).

**New scenes from the gameplay plan** ([30](30_GAMEPLAY_PLAN.md)) get a scene name here when they are built:
- the weapon test arena: built (`arena`, above). Each new weapon's play is tried there first;
- the card collection page: `menu` with a `cards` tab;
- the other modes, each as its own scene.

Each phase of the plan ends with its scenarios.

## 4. What the test checks

At the end of the `test` frames:
- **The scene** in `expect.scene` (default: `scene`) must be showing. For example, `battle` means a battle is still
  running, and `stage` means the floor with no window over it.
- **Optional values:**

  | Field | Check |
  |---|---|
  | `menuTab` | the tab the player menu is on |
  | `gold`, `hp`, `lv` | the player's value |
  | `arenaFights`, `arenaWins` | fights started / won in the arena |
  | `battleAnim` | the clip the battle picture shows for a weapon with battle clips: `charge`, `attack`, `miss` or `idle` ([15](15_ANIMATION.md), "Characters") |
  | `cards` | `{ "slime": 1 }`: copies of each monster's card held |
  | `stairUpLocked`, `stairUpMarked` | the current floor's first stair up: locked or open; marked with the gold arrow (just opened) |
  | `stage` | the current floor |
  | `inventoryHas` | items in the backpack |
  | `flags` | run flags that must be set |
  | `condition` | any condition in the game's own format, as in events' `requires` |

A failed check prints, for example, `[scenario] <name>: FAILED  gold is 350, expected 999`. The game exits with code
3, and the test fails. An unreadable setting file or an unknown enemy, dialogue, item or hub place fails in the same
way, at the start.

The test keeps the last frame as `Build/<preset>/test-run/scenario/<name>/<name>.png` to look at. Nothing is compared
with a reference image, so a visual change does not break a scenario. The screenshot tests (`smoke.*`) are for that.

## 5. Which scenarios a change runs

`tools\test.cmd` (changed-only, [09](09_TESTS.md)) picks scenarios like the screenshots:
- a change in `tests/scenarios/<name>/` runs `scenario.<name>`;
- a change in `_shared/` or the runner runs every scenario;
- game code is narrowed by its file name: `game_combat.cpp` runs `scenario.battle_*`, `game_store.cpp` runs
  `scenario.store_*`, a dialogue file runs `scenario.dialogue_*` and `scenario.talk_*`, and so on;
- game code that matches no name runs every scenario.

The code is `src/core/game/core/game_scenario.cpp` (`Game::startScenario`, `Game::scenarioCheck`),
`toms::readSaveFile` (`save_slots.h`), `--scenario` in `src/game/src/main_sdl.cpp`, and
`tests/scenarios/run_scenario.cmake`.
