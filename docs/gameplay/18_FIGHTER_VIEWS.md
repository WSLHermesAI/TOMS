# 18 — Fighter views: 2D or 3D behind one interface, and recording frames

中文：[zh_TW/18_FIGHTER_VIEWS.md](zh_TW/18_FIGHTER_VIEWS.md).

The owner (2026-11-02): the 3D hero looked bad, so the next try is 2D again, made like Spine (parts painted in
several directions, assembled in the anim editor). 3D may be tried later, so the game needs an interface that
switches between 2D and 3D for the player and the monsters, with little code to change. Also: capturing pictures must
not flash windows on the desktop; the game records its frames to a folder and they are merged into a GIF.

## 1. The interface: FighterView

`src/core/game/core/fighter_view.h`. The battle asks a fighter's view for a clip by name and draws it in the
fighter's box; how it is animated and drawn belongs to the view.

| Call | What it does |
|---|---|
| `f_Has(clip)` | whether the look has that clip |
| `f_Play(clip, restart)` | plays it (from the start when `restart`); false when there is no such clip |
| `f_Stop()`, `f_Clip()`, `f_Finished()` | stop; the clip playing; a one-shot clip has played out |
| `f_Update(dtMs)` | advances it |
| `f_Draw(ren, box)` | draws it in the box (screen pixels), the feet at the box's bottom centre |

Clip names are the battle's states: `idle`, `charge`, `attack`, `attack_big`, `miss`, `hurt`, `die`, a monster's
own `<state>_<enemy id>`, and the weapon's or look's renamed clips.

| View | Draws | Status |
|---|---|---|
| `AnimFighterView` | 2D: a character clip and a held clip at its hand, from .anim files (the anim editor's) | built; the player and every monster use it |
| a glTF view | 3D: a .glb's animations by the same clip names, with the glTF renderer | not built: a look set to 3D says so once and uses its 2D files |

The game makes a view with `Game::makeFighterView(look, fit, unitsTall, boxSide)`:
- the player's from the equipped weapon's `battleAnim` (`loadBattleRig`);
- each monster's from its look (`monsterLook`), made again when the monster changes (`transformInto`).

## 2. The data that switches it

The player (items.json, a weapon):

```json
"battleAnim": {"render": "2d", "character": "anim/hero_bow.anim", "held": "", "model": "", "clips": {}}
```

A monster (looks.json, a look; `base` passes on what a look leaves out):

```json
"slime": {"base": "monster", "render": "2d", "character": "anim/slime_battle.anim", "model": "", "states": {}}
```

| Field | Meaning |
|---|---|
| `render` | `"2d"` (default) or `"3d"` |
| `character`, `held` | 2D: the .anim files (`held` is drawn at the character's `hand` node). A monster without one uses `anim/monster_battle.anim` |
| `model` | 3D: the .glb |

Switching a fighter between 2D and 3D is a change in the data; the battle code does not change.

## 3. What changed in the code

- `Game::battleRig_`, `battleCharacterAnim_`, `battleHeldAnim_` and `battleFitted_` became `playerView_`.
- Each `FoeRig`'s rig and fitted clip became its `view`.
- `monsterAnim_` is gone: each monster's view loads its look's file (the resource manager shares it).
- The places that asked the .anim file for a clip (the big attack, a special move's clip) ask the view (`f_Has`).

A manual check (2026-11-02, the game run directly on the scenarios): `battle_bow`, `battle_bow_idle`,
`battle_enemy_hurt`, `battle_enemy_die` and `battle_transform` pass and draw as before.

## 4. Recording frames without a window

```
toms_game.exe --scenario=tests/scenarios/battle_bow --scenario-test --fixed-dt=33 --design=1024x768 ^
              --frames=150 --record=<folder> --record-from=20 --record-step=2
python tools/frames_to_gif.py <folder> out.gif --fps 15 [--crop x,y,w,h] [--width 640] [--keep]
```

- `--record=<folder>` saves `frame_00000.png`, `frame_00001.png`, ... (every frame from `--record-from`, or every
  `--record-step`th, up to `--frames`).
- A run with `--screenshot` or `--record` draws into a **hidden window**: nothing flashes on the desktop. The same
  holds for `gltf_viewer --screenshot`.
- `tools/frames_to_gif.py` merges the frames (deleted afterwards unless `--keep`). With `--fixed-dt=33` and
  `--record-step=2` the GIF runs at 15 fps.
