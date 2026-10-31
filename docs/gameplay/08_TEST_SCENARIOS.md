# 08 — Test scenarios: start any scene from a setting file

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

Scenarios run in `toms_game` (`--scenario=<folder>`, above).

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
              "keys": { "yellow": 2, "blue": 1, "red": 0 }, "attrs": { "str": 3, "agi": 2 }, "attrPoints": 3,
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
| `cards` | Monster cards held: `{ "slime": 2 }` (the training ground, [16](16_SKILLS_AND_PLAYS.md) §5). |
| `player` | A new game starts with the bow equipped (the starting weapon); `"unarmed": true` takes it off. Then `give` and `equip` (items and gear by `items.json` id; `gold:<n>`, `exp:<n>` and `points:<n>` work in `give`), then the numbers as given. `inventory` replaces the backpack. `weaponUses` (`{ "<weapon id>": <wins> }`) sets each weapon's wins, so its level ([07](07_GAMEPLAY_PLAN.md) §13). |
| `test` | For the automated test: how many frames to run after the scene opens, and keys to press (`key@frame`, counted from the scene's first frame; the keys are those of `--keys`), and `clicks`: left clicks at design points (`x:y@frame`, 1024 x 768, as `--clicks`; a click hovers 2 frames, presses 2, then lets go). |
| `expect` | What the test checks at the end (section 4). Without it, only that the scene is showing. |

A file name in a setting file is relative to **that** file. A shared file's save is found next to the shared file.

## 3. The scenes

| `scene` | Opens | Its own fields | Example folder |
|---|---|---|---|
| `title` | the title screen, as the game starts | — | `title` |
| `stage` | the walking scene: the floor with the player, at the saved or given moment | — | `stage_new_game`, `stage_from_save`, `stage_from_events` |
| `battle` | a battle with an enemy from `enemies.json` (not on the map) | `battle`: `enemy`, and optional `hp`, `atk`, `def`, `atk_interval_ms`, `moves`, `look`, `break`, `events` overrides; `party`: up to two more (ids, or `{ "enemy": id, ...overrides }`); `pace`: `cooldown` / `stamina`; `super`: `true` fills the super charge (the ultimate ready; [16](16_SKILLS_AND_PLAYS.md) §6); `play`: the attack's play, `track` (the default in a scenario), `bow`, `slice`, `circles` or `weapon` (the worn weapon's own, `weapon_play.json` `"play"`; [15](15_BATTLE_REVAMP.md) §3.3); `rules`: `data/battle.json` values for this battle (`{ "stamina": {...}, "break": {...} }`) ([13](13_BATTLE_EDITOR.md)) | `battle_slime`, `battle_strong_player`, `battle_bow` (the bow's battle clips; also `battle_bow_charge`, `battle_bow_idle`); `battle_party`, `battle_party_target` (three monsters); `battle_parry`, `battle_jabs`, `battle_spell_break`, `battle_break`, `battle_transform`, `battle_stamina`, `battle_stamina_short`, `battle_attack_in_combo`, `battle_combo_holds` (the battle's rules); `battle_guard_early`, `battle_guard_bar`, `battle_result` (the revamp: the guard's timing, the result's grade); `battle_play_bow`, `battle_play_slice`, `battle_play_circles` (each weapon's own play); `battle_guard_warn`, `battle_guard_warn_early`, `battle_rhythm`, `battle_special_guard`, `battle_ultimate_star` (16: the "!" guard, a monster's rhythm, a special guard play, a weapon's move) |
| `battleWon` | the victory screen after that battle | as `battle` | `battle_won` |
| `arena` | **the weapon test arena** ([07](07_GAMEPLAY_PLAN.md) phase 0): fight after fight against the listed enemies in turn, full HP each time, no rewards and no story effects. After a win, Enter starts the next fight; after a loss it starts by itself. The weapon is the player's: the starting bow, another with `player.equip`, none with `player.unarmed`. `toms_game --arena=<enemy,...>[:<weapon>]` is the same without a file (no weapon given: unarmed) | `battle`: `enemies` (or `enemy`), and the same overrides as `battle`, for every fight | `arena_unarmed`, `arena_wand`, `arena_restart` |
| `weaponTrial` | **the start story** ([07](07_GAMEPLAY_PLAN.md) §15): the weapon master's dialogue (`dialogue/weapon_master.json`), as the title's New Game opens it; its choices try a weapon in a trial fight (the arena) and keep one | — | `trial_start`, `trial_try_sword`, `trial_choose_staff` |
| `miniGame` | **a reward event** ([07](07_GAMEPLAY_PLAN.md) §16): `"miniGame": "<id in data/minigames.json>"` opens it; Enter starts it and taps the points | — | `minigame_chest`, `minigame_crystal` |
| `dialogue` | a dialogue: one of the game's (`id`), or a file next to the setting file (`file`); optional `node` to start at | `dialogue`: `id` or `file`, `node` | `dialogue_by_id`, `dialogue_from_file` |
| `talk` | talking to an NPC: their chat line, topic or topic menu, by the events finished | `talk`: `npc` (an `npcs/` id) | `talk_elder` |
| `store` | the store (unlocked for the scenario) | — | `store_buy`, `store_from_save` |
| `menu` | the player menu on a tab | `menu`: `tab` = `status`, `gear`, `items`, `events`, `cards` or `system` | `menu_status` … `menu_system`, `menu_cards`, `events_stairs_where` |
| `saveLoad` | the Save / Load screen | `saveLoad`: `mode` = `save` or `load` | `save_load` |
| `menu` + `cards` | the training ground ([16](16_SKILLS_AND_PLAYS.md) §5): Enter on the Cards tab trains with the selected card | — | `training_cards` |
| `stageSelect` | the stage select (floors reached so far) | — | `stage_select` |
| `stairsConfirm` | the "go up / down?" question on a stair | `stairs`: `to` (a floor), `up` (true / false) | `stairs_confirm` |
| `notice` | a notice made by the setup, e.g. "stairs unlocked" after a locked stair's event finishes | — | `notice_stair_unlock` |
| `ending` | an ending screen | `ending`: `id` (`e_01` … in `story/endings.json`) | `ending` |
| `hub` | a village hub location (its dialogue); its unlock flag must be in `flags` | `hub`: `location` | `hub_village` |

The node-animation and particle previews already have their own options (`--anim=<file>#<clip>`,
`--fx=<file>#<effect>`; [05](../engine/05_ANIMATION.md), [07](../engine/07_PARTICLES.md)).

**New scenes from the gameplay plan** ([07](07_GAMEPLAY_PLAN.md)) get a scene name here when they are built:
- the weapon test arena: built (`arena`, above). Each new weapon's play is tried there first;
- the card collection page: `menu` with a `cards` tab;
- the other modes, each as its own scene.

Each phase of the plan ends with its scenarios.

## 4. What the test checks

At the end of the `test` frames:
- **The scene** in `expect.scene` (default: `scene`) must be showing. For example, `battle` means a battle is still
  running, and `stage` means the floor with no window over it.
- **Optional values** (the battle's are in the table too; the keys `f`, `g`, `h` guard, super and use the active):

  | Field | Check |
  |---|---|
  | `menuTab` | the tab the player menu is on |
  | `gold`, `hp`, `lv` | the player's value |
  | `arenaFights`, `arenaWins` | fights started / won in the arena |
  | `enemyAnim` | the monster's status on the battle screen: `idle`, `attack`, `hurt`, `die` ([05](../engine/05_ANIMATION.md) "Monsters in battle") |
  | `enemyClip` | the clip it plays for that status (`attack`, or a monster's own `attack_<id>`) |
  | `battleAnim` | the clip the battle picture shows for a weapon with battle clips: `charge`, `attack`, `miss` or `idle` ([05](../engine/05_ANIMATION.md), "Characters") |
  | `foesDown`, `target` | how many of the battle's monsters have fallen; the monster the attacks go to (0 = the first) |
  | `battleEvents` | `{ "parry": 1, "hit": 0 }`: how many battle events of each type the log has (`toms_game --battle-log` prints them) |
  | `pace` | the battle's pace: `cooldown` or `stamina` |
  | `grade` | the won battle's grade on the result screen: `S`, `A`, `B` or `C` ([15](15_BATTLE_REVAMP.md) §3.6) |
  | `crystals` | the crystals the player has ([07](07_GAMEPLAY_PLAN.md) §16; `give` also takes `crystals:<n>`) |
  | `equipped` | the worn weapon's id ("" for none) |
  | `gearOwned` | `["<id>", ...]`: exactly the gear owned, in any order |
  | `dialogueNode` | a dialogue is open at this node |
  | `battleClip` | the clip that status plays: the weapon's own name for it (`"battleAnim"` `"clips"`, e.g. the sword's `raise`) |
  | `weaponLevel` | `{ "<weapon id>": <level> }`: each weapon's level, from its wins ([07](07_GAMEPLAY_PLAN.md) §13) |
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

`tools\test.cmd` (changed-only, [08](../project/08_TESTS.md)) picks scenarios like the screenshots:
- a change in `tests/scenarios/<name>/` runs `scenario.<name>`;
- a change in `_shared/` or the runner runs every scenario;
- game code is narrowed by its file name: `game_combat.cpp` runs `scenario.battle_*`, `game_store.cpp` runs
  `scenario.store_*`, a dialogue file runs `scenario.dialogue_*` and `scenario.talk_*`, and so on;
- game code that matches no name runs every scenario.

The code is `src/core/game/core/game_scenario.cpp` (`Game::startScenario`, `Game::scenarioCheck`),
`toms::readSaveFile` (`save_slots.h`), `--scenario` in `src/game/src/main_sdl.cpp`, and
`tests/scenarios/run_scenario.cmake`.
