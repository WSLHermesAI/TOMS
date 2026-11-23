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

Then (2026-11-21): "make 3D looking as default" (the browser too), and "remove fallback to 2D and delete related useless
files, and make a loading circle while change to next scene too long".

**Built: every floor is 3D, there is no 2D map (§6). Steps 3–5 (§5) are not built.** The open questions are in §7 and in
[13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md).

## 1. What there was

- The walking scene drew the floor as flat 16-pixel tiles, top down, one picture per cell kind and per floor theme
  (`wall_wood`, `floor_wood`, ...), tinted by the chapter, with a 2D camera (Settings: follow or rooms) scrolling over it.
- The battle already had a 3D stage ([21_BATTLE_3D.md](21_BATTLE_3D.md)): a glTF PBR renderer, an HDR sky for light,
  a sun or moon with shadow maps, point lights, a background loader, and 2D sprites standing in the scene, lit by it
  (world groups). **The 3D floors reuse all of it**; nothing new was needed in the renderer.

## 2. How it works

```
the floor's tile map (FNN.stage.json / stageNN.json, any stage the editor makes) ─┐
data/stage3d.json (the look's light, the camera, the sprites' heights) ───────────┼─► Game::f_Walk3DStage
                                                                                  │     (the wall rows, the look)
the host's BattleStage ◄─ floor_mesh.cpp: walls + ground as a .glb in memory ◄────┘
        ▲                         + stage3d/looks/<look>/*.jpg / *_n.png (painted)
the map's objects (MapFloor) ──► one world group each, standing on its tile ──► lit in the same scene
```

- **One tile is one metre**: x is the column, z the row (south is +z), y is up.
- **The floor's model is made when the floor is entered** (`src/game/src/floor_mesh.cpp`), from its wall cells, on the
  loader's worker thread: no file per floor, so every floor, hand-made map and stage made in the editor is 3D. Only the
  walls count: a door opening or a monster's tile clearing does not make it again.
- **The host** (`GameSession`) has a second `BattleStage` for the floor. It loads the floor in the background when the
  floor is entered and draws it while the walking scene is on the screen. The battle's stage and the floor never draw in
  the same frame; they share the 3D views.
- **The game** (`game_walk3d.cpp`) gives the stage row, the camera and the shadows' places. Each map object goes into its
  own world group: its 2D look is drawn as before into its rect on a map of 100 pixels a tile, and the group stands that
  rect on its tile, facing the camera and leaning back a little. The ground's marks (stairs, door tiles, the locked
  stairs' padlocks, an opened stair's arrow, the walk target) lie flat on the floor; an opened stair off the screen gets a
  gold badge at the screen's edge pointing to it. Boss and roamer name plates sit over their sprites' heads.
- **The stairs** are in the model (the owner, 2026-11-22: "2D stair too flat, it is easy to ignore"): four steps walked
  onto from an open side (south first, facing the camera). **Up** climbs to the walls' top; **down** goes into a pit as
  deep. Treads in the look's ground texture darkened (`stairShade` 0.35: the owner, 2026-11-22, "too similar to the floor's
  colour"), risers and the pit in its wall texture. Each stair has its own light (`stairLights`: warm over a stair up, cool in a stair down's pit; brighter
  when just opened, dimmer when locked; the nearest to the hero first). Over each stair stands its **sign** (the owner, 2026-11-23: the 3D steps alone were
  still hard to read): three chevrons that keep **rising** over a stair up (gold, "^") or **sinking** over a stair down
  (sky blue, "v"), brighter, quicker and pulsing when the stair has just opened; a padlock instead while it is locked
  (`stairSign`: `size` 1 m, `speed`).
- **An event** waiting on its tile is a small swirl of light, "like memory thoughts" (the owner, 2026-11-22; it was a gold
  plate that stood up as a dark slab in 3D): comet-like motes on two arms spiral in toward a breathing glow, lavender and
  pale blue, each event at its own phase (`SwirlLook`, map_objects.cpp; the effects atlas's `glow` and `dot`). An event
  with its own look keeps it.
- **The hero** is his weapon's painted battle character (its idle clip), turned the way he last walked. The monsters,
  people and things are still their map sprites, larger (step 3).
- **Clicks** go through the camera: a point on the screen becomes a ray, the ray finds the tile on the ground.
- **Settings > View size** (the owner, 2026-11-22): ×1, ×1.5, ×2, ×3 (`viewScales`) multiplies the camera's distance:
  about 12, 18, 24 and 36 tiles across (a 19 × 16 floor fits whole at ×3); **×1.5 by default**. Saved in the settings (`viewScale`). A
  scenario uses ×1 unless it says `"viewScale"` (the scenario `walk_3d_far`: ×2).
- **No 2D map.** While the floor loads, nothing is drawn behind the HUD; a load longer than `loadingDelayMs` (250 ms)
  shows the **loading circle** (twelve dots, one turn a second, fading in) at the middle of the screen. The battle's 3D
  stage still on its way shows it too. Scripted runs (tests) wait for the load, so their pictures are the same every time.
- Everywhere: the Windows desktop, the browser (no sun shadows there, as in the 3D battle), the editors' game view, and
  Android (not tried: Android is only built when the owner asks).

## 3. The looks (`tools/stage3d/paint_looks.py`)

- **Three parts**: the ground (repeated every 2 m), the walls' sides, the walls' tops. Walls are 1 m tall (the camera
  sees over them: a hero behind a wall still shows).
- **The 10 looks** are painted: wood, forest, castle, cave, crypt, crystal, ice, lava, sand, shadow. Each part is painted
  with ComfyUI (SDXL, Juggernaut XL, 1024 px; the words in the tool's `LOOKS`), made seamless (blended with itself moved
  by half) and 512 px, with a normal map from its brightness. A floor uses its stage file's `"theme"`; a theme
  `stage3d.json` does not have uses `"defaultLook"` (castle).
- `--seeds` tries several; the first is used, the others stay in `Build/art_out/stage3d/` to compare. Wood's tops were
  repainted once (the first had loud green moss).

```
python tools/stage3d/paint_looks.py castle                    paint a look (ComfyUI at 127.0.0.1:8188)
python tools/stage3d/paint_looks.py --all                     every look
python tools/stage3d/paint_looks.py wood --parts top --seeds 5,8   repaint one part, two tries
```

## 4. `data/stage3d.json`

| Key | What |
|---|---|
| `defaultLook` | the look of a floor whose theme has none (castle) |
| `wallHeight`, `groundRepeat` | the walls' height (1 m), the ground texture's size (2 m) |
| `loadingDelayMs` | a scene still loading after this long shows the loading circle (250) |
| `viewScales` | the steps of Settings > View size, × the camera's distance (1, 1.5, 2, 3) |
| `shadow` | the soft disc under the hero, the monsters and the people (the battle's) |
| `stairShade` | how dark the stairs' treads are: the ground's texture × it (0.35) |
| `stairLights` | each stair's light: `up` / `down` colours, `intensity` (×1.6 just opened, ×0.5 locked), `range`, `off` |
| `camera` | `pitch` (56°), `distance` (11 m), `fov` (38°), `follow` (how fast it catches up), `lookAhead` |
| `sprites` | `glow`, `lean` (24°), `shadow` (the disc's radius), `heights` by kind (a one-tile sprite: player, monster, npc, item, door, event, other), `paintedHero`, `paintedHeroHeight` |
| `looks` | each look's `sky` (lights only: `"background": false`, the tower stands in the dark), `exposure`, `light` (the sun: dir, color, intensity, ambient) |

## 5. The plan

| Step | What | State |
|---|---|---|
| 1. F01 prototype | The look painter, the wood look, `stage3d.json`, the host's second stage, the follow camera, the map's objects standing in the scene, the painted hero turning, clicks on the 3D ground, the scenario `walk_3d` | **built 2026-11-21** |
| 2. Every floor, no 2D | All 10 looks painted; every floor's model made from its tile map when entered; the 2D map, its camera and its Settings row removed; the loading circle; the browser on; the stairs as steps with their lights (11-22) | **built 2026-11-21/22** (still open from the first plan: doors as frames in the model, a wall in front of the hero fading) |
| 3. Painted art on the map | Monsters as their battle art (larger, their idle clips), people as painted full-body art, items and keys as painted icons; the hero's walk clip and a smooth step between tiles (today he jumps a tile, as in 2D) | |
| 4. Atmosphere | Torches (point lights and their fire), light shafts, dust and embers, a little fog, a colour grade per chapter; the floor's banner and entry over a slow camera move | |
| 5. Polish | The camera's limits at the map's edges, a short shake for events, the speed on slow machines; Android when the owner asks | |

## 6. As built (2026-11-21)

- `src/game/src/floor_mesh.cpp` (+ `.h`): the floor's model from its wall rows, a `.glb` in memory. `BattleStage` reads a
  prop `{"floor": {"walls", "look", "wallHeight", "groundRepeat"}}` with it; a sky can light without being drawn
  (`"background": false`).
- `src/core/game/core/game_walk3d.cpp`: the stage row, the camera, the shadows, the standing objects, the painted hero,
  the ground's marks, the off-screen stair badge, the click on the ground, the loading circle.
  `Game::f_DrawGroundMarks` (game_scene_draw.cpp) draws the marks; `MapFloor::f_Render` puts each object in its own group.
- `src/game/src/game_session.cpp`: `m_upWalkStage`, drawn while the walking scene shows; `Game::f_SetSceneLoading`.
- `tools/stage3d/paint_looks.py`, `assets/media/stage3d/looks/<look>/` (10 looks × 3 parts: colour and normal map),
  `assets/data/stage3d.json` (key `data.stage3d`).
- The scenario `walk_3d`: F01 in 3D, and a click on the hero finds his tile; `walk_3d_far`: the same on F05 at view size ×2.
- Settings > View size (2026-11-22): `Game::f_CycleViewScale`, `GameSettings::m_fViewScale`, the text `settings.view_scale`.
- **Removed:** the 2D tile drawing, its chapter tint (`f_FloorThemeTint`), the 2D camera (`toms::Camera`,
  `systems/camera.*`, `camera_test`), Settings > Camera (follow / rooms) and its text, the settings' `cameraMode` and
  `viewCols`, the F1 overlay's "visible cells" slider, `--walk-3d`, a scenario's `"walk3d"`, and step 1's prebuilt
  `stage3d/floors/F01.gltf`. **Kept:** the 2D wall and floor pictures (`wall_<theme>`, `floor_<theme>` in the atlas):
  the stage editor draws its map with them.

## 7. Open questions

| Question | Default | Yours | Answer |
|---|---|---|---|
| **Q117** 3D floors for everyone, with 2D in Settings? | — | — | **3D for everyone, no 2D** (the owner, 2026-11-21: "remove fallback to 2D") |
| **Q118** The camera: fixed (north up), or can the player turn it 90°? | fixed | fixed: the maps are made top-down, and "up" stays up |  |
| **Q119** Wall height: 1 m (the camera sees over the walls) or taller (1.6 m) with a wall in front of the hero fading? | 1 m | 1 m now; try 1.6 m with the fade |  |
| **Q120** Monsters on the map: their battle art (one picture, the same as in the fight) or new small painted map art? | their battle art | their battle art |  |
| **Q121** The village and the hub screens: 3D too? | no, they keep their painted screens | no, until the floors are done |  |
