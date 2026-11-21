# 24. Walking the floors in 3D (2026-11-21)

中文：[zh_TW/24_STAGE_3D.md](zh_TW/24_STAGE_3D.md).

The owner (2026-11-20, with a picture of a stage in a 3D style): walking a floor "looks so 80s style, too old, like it was
made 40 years ago"; the stage's look should be revamped. Their answers to the four questions:

| Question | Answer |
|---|---|
| How far? | **Real 3D floors**: each floor's walls and ground as a 3D model, lit, with shadows |
| The camera? | **Tilted, following the hero** (about 55° down), north still up |
| The characters? | **Painted art, larger**: the battle characters' painted art on the map, not the 16-pixel sprites |
| The textures? | **AI-painted, one set per look** (wood, castle, cave, crypt, ...), seamless |

**This is the plan. Step 1 (an F01 prototype, §6) is built; the other steps are not.** The open questions are in §7 and in
[13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md).

## 1. What there was

- The walking scene drew the floor as flat 16-pixel tiles, top down (`Game::f_DrawTileLayer`), one picture per cell kind
  and per floor theme (`wall_wood`, `floor_wood`, ...), with the map's objects on top (`MapFloor::f_Render`).
- The battle already has a 3D stage ([21_BATTLE_3D.md](21_BATTLE_3D.md)): a glTF PBR renderer, an HDR sky for light,
  a sun or moon with shadow maps, point lights, a background loader, and 2D sprites standing in the scene, lit by it
  (world groups). **The 3D floors reuse all of it**; nothing new is needed in the renderer.

## 2. How it works

```
stage file (FNN.stage.json: tiles, theme) ──tools/stage3d/make_floor3d.py──► stage3d/floors/FNN.gltf
                                                                              (+ looks/<theme>/*.jpg, *_n.png)
data/stage3d.json (camera, light per look, sprite heights) ──► Game::f_Walk3DStage ──► the host's BattleStage
the map's objects (MapFloor) ──► one world group each, standing on its tile ──► lit in the same scene
```

- **One tile is one metre**: x is the column, z the row (south is +z), y is up. The model is in those units, so a tile
  of the map is a square metre of the model, and nothing else needs converting.
- **The host** (`GameSession`) has a second `BattleStage` for the floor. It loads the floor's model in the background when
  the floor is entered and draws it while the walking scene is on the screen (no battle, dialogue, store, title or
  ending over it). The battle's stage and the floor never draw in the same frame; they share the 3D views.
- **The game** (`game_walk3d.cpp`) gives the stage row (the model as a prop, so its walls cast the sun's shadows), the
  camera, and the shadows' places. In its draw, each map object goes into its own world group: its 2D look is drawn as
  before into its rect on a map of 100 pixels a tile, and the group stands that rect on its tile, facing the camera and
  leaning back a little. The ground's marks (stairs, the locked stairs' padlocks, the walk target) lie flat on the floor.
- **The hero** is his weapon's painted battle character (the same clips as the battle, its idle clip), turned the way he
  last walked. The monsters, people and things are still their map sprites, larger (§5).
- **Clicks** go through the camera: a point on the screen becomes a ray, and the ray finds the tile on the ground
  (`Game::f_Walk3DScreenToTile`). Keys work as before.
- **The 2D map stays**: a floor without a model, `stage3d.json` `"on": false`, `--walk-3d=off`, or a renderer without
  mesh shaders draws the old map. Scripted runs (tests) draw 2D unless they ask (`--walk-3d=on`, a scenario's
  `"walk3d": true`), so their reference pictures do not change.

## 3. The floor's model (`tools/stage3d/make_floor3d.py`)

- **Walls**: each `#` cell is a block 1 m tall (the camera sees over it: a hero behind a wall still shows). Only its top
  and the sides beside an open cell are made (the map's edge included: the bottom row's front shows). F01 is 734
  triangles.
- **The ground**: one quad under the whole map, its texture once every 2 m.
- **Three materials**: ground, wall, wall top. Their textures are the floor's look's (its `"theme"`; none = castle),
  shared by every floor of that look (`../looks/<look>/` in the `.gltf`).
- **The looks** (wood, forest, castle, cave, crypt, crystal, ice, lava, sand, shadow): `--paint <look>` paints each
  part with ComfyUI (SDXL, Juggernaut XL, 1024 px; the words in the tool's `LOOKS`), makes it seamless (blended with
  itself moved by half) and 512 px, and makes a normal map from its brightness. `--seeds` tries several; the first is
  used, the others stay in `Build/art_out/stage3d/` to compare. A look not painted yet is drawn by the tool (a plain
  pattern in its colours), so every floor builds.
- **Wood** (chapter 1) is painted: oak boards (ground), log walls, dark weathered planks on top (repainted once: the first
  top had loud green moss).

```
python tools/stage3d/make_floor3d.py F01 F02           build these floors
python tools/stage3d/make_floor3d.py --all             every floor
python tools/stage3d/make_floor3d.py --paint castle    paint a look (ComfyUI at 127.0.0.1:8188), then build
python tools/stage3d/make_floor3d.py --paint wood --parts top --seeds 5,8   repaint one part
```

## 4. `data/stage3d.json`

| Key | What |
|---|---|
| `on` | false: every floor in 2D |
| `model` | the floor's model, `{id}` = its id (`stage3d/floors/{id}.gltf`) |
| `shadow` | the soft disc under the hero, the monsters and the people (the battle's) |
| `camera` | `pitch` (55°), `distance` (11 m), `fov` (38°), `follow` (how fast it catches up), `lookAhead` |
| `sprites` | `glow`, `lean` (24°), `shadow` (the disc's radius), `heights` by kind (a one-tile sprite: player, monster, npc, item, door, event, other), `paintedHero`, `paintedHeroHeight` |
| `looks` | by theme, `default` for the rest: `sky` (lights only: `"background": false`, the tower stands in the dark), `exposure`, `light` (the sun: dir, color, intensity, ambient) |

## 5. The plan

| Step | What | State |
|---|---|---|
| **1. F01 prototype** | The tool, the wood look painted, `stage3d.json`, the host's second stage, the follow camera, the map's objects standing in the scene, the painted hero turning, clicks on the 3D ground, `--walk-3d`, the scenario `walk_3d` | **built 2026-11-21** |
| 2. Every floor | Paint the other 9 looks; build F01–F35; Settings > 3D floors (on / light / off, like the 3D battle); stairs as steps and doors as frames in the model; a wall between the camera and the hero fades | |
| 3. Painted art on the map | Monsters as their battle art (larger, their idle clips), people as painted full-body art, items and keys as painted icons; the hero's walk clip and a smooth step between tiles (today he jumps a tile, as in 2D) | |
| 4. Atmosphere | Torches (point lights and their fire), light shafts, dust and embers, a little fog, a colour grade per chapter; the floor's banner and entry over a slow camera move | |
| 5. Polish | The camera's limits at the map's edges, a short shake for events, the speed on slow machines; web and Android only when the owner asks | |

## 6. Step 1, as built (2026-11-21)

- `tools/stage3d/make_floor3d.py`: the model builder and the look painter (§3). `assets/media/stage3d/floors/F01.gltf`
  (+ `.bin`), `assets/media/stage3d/looks/wood/` (ground, wall, top: colour and normal map).
- `assets/data/stage3d.json` (key `data.stage3d`).
- `src/core/game/core/game_walk3d.cpp`: the stage row, the camera, the shadows, the standing objects, the painted hero,
  the ground's marks, the click on the ground. `MapFloor::f_Render` puts each object in its own group when 3D is on.
- `src/game/src/game_session.cpp`: `m_upWalkStage`, drawn while the walking scene shows. `BattleStage`: a sky can light
  without being drawn (`"background": false`).
- `--walk-3d=on|off` (`main_sdl.cpp`): on in normal play on the Windows desktop; off in scripted runs; the web and
  Android only when asked.
- The scenario `walk_3d`: F01 in 3D, and a click on the hero finds his tile.

**Still 2D-looking in step 1:** the monsters', people's and items' sprites (16-pixel art, larger); the stairs (a flat
picture on the floor); the doors (a standing sprite). Every floor but F01 has no model yet, so it is drawn in 2D.

## 7. Open questions

| Question | Default | Yours | Answer |
|---|---|---|---|
| **Q117** 3D floors for everyone by default, with 2D in Settings? | 3D on (desktop), 2D when there is no model | yes; Settings > 3D floors in step 2 |  |
| **Q118** The camera: fixed (north up), or can the player turn it 90°? | fixed | fixed: the maps are made top-down, and "up" stays up |  |
| **Q119** Wall height: 1 m (the camera sees over the walls) or taller (1.6 m) with a wall in front of the hero fading? | 1 m | 1 m now; try 1.6 m with the fade in step 2 |  |
| **Q120** Monsters on the map: their battle art (one picture, the same as in the fight) or new small painted map art? | their battle art | their battle art |  |
| **Q121** The village and the hub screens: 3D too? | no, they keep their painted screens | no, until the floors are done |  |
