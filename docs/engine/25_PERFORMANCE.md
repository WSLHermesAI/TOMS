# 25 — The performance checker: what each frame costs, on screen and in a file

中文：[zh_TW/25_PERFORMANCE.md](zh_TW/25_PERFORMANCE.md).

The owner, 2026-11-26: the game should say where a frame's time goes, per major update and render, with the draw calls
and triangles, live on screen; and when it runs slower than 30 fps for a whole second, write down what took the time
(2026-11-26: 30 fps, then 30 s quiet, and not the same cause twice).

## 1. Using it

| | |
|---|---|
| **F4** | on every screen, the title too: the **Performance** window (ImGui): fps and frame time over the last second (red under 30 fps), the GPU's time, draw calls, triangles, compute calls, the last 300 frames as a graph, each section's average and worst (Update, Render, Frame), the counters, the GPU's time per view, **Dump now**, and the switch for the automatic dump |
| **The automatic dump** | when fewer than 30 frames ended in a whole second (or one frame took more than a second), after the first 3 s: `perf_dumps/perf_<date>_<time>_<n>.txt` next to `toms.log`, and a line on the console naming the cause (the two costliest sections, `frame.present` left out: "render.game + render.walk3d"). Then none for 30 s, and none until the game runs at speed again; a slowdown with a cause already dumped this run is not dumped again (the console says so) |
| **Settings > Performance checker: on / off** | the same window without a keyboard (a phone, the web on a phone): the in-game menu, System, Settings, its last row; the window's ✕ closes it too |
| **Ctrl +** / **Ctrl −** | the F4 window's text a step (10%) larger / smaller, 50% to 300% (the `=`/`+` and `-` keys, or the keypad's); the window grows with it; also its **A−** / **A+** buttons |
| `--perf-dump=on` / `off` | the automatic dump; default **on**, **off** in scripted runs (`--frames`, `--fixed-dt`): a test's slowness is not a dump |
| `--perf-dump=exit` | a dump of the last 2 s when the run ends: to measure a scene, e.g. a scenario run |

A dump holds: why and where (the renderer, the game's state, the stage), the frame rate, each section's average and
worst, the counters, the GPU's time per view, and every frame of the last 2 s with its time, GPU time, draws, triangles
and its three costliest sections.

## 2. What is measured

**Sections** (CPU time, ms, summed over a frame; they nest: `render.particles` is also inside `render.battle` and
`render.game`):

| Section | Where |
|---|---|
| `update.game` | `Game::f_Update`, all of it |
| `update.map_objects`, `update.battle_rig`, `update.monster_rig`, `update.battle_fx`, `update.backdrop`, `update.battle_logic` | its parts |
| `update.particles` | **every** particle effect's update (`EffectInstance::f_Update`, the engine: battle, backdrop, the stage's fires, previews) |
| `update.anim` | every clip player (`AnimPlayer::f_Update`) |
| `update.stage3d` | the 3D stages' requests and loading (`BattleStage::f_Request` / `f_Poll`) |
| `update.ui` | the UI, all of it: `update.ui_build` the game makes the UI's state (`Game::f_BuildUiState`), `update.ui_sync` it into the data model, `update.ui_rml` RmlUi's update (the bindings that changed, styles, layout) |
| `render.game` | `Game::f_Draw`, all of it; `render.walk3d`, `render.battle` its parts; `render.sprite_submit` the sprite batches to bgfx |
| `render.particles` | **every** particle effect's draw (`EffectInstance::f_Draw`) |
| `render.stage3d_battle`, `render.stage3d_walk`, `render.stage3d_sprites`, `render.title_scene` | the 3D scenes (glTF) and the sprites standing in them |
| `render.ui`, `render.imgui` | RmlUi's documents, the debug windows |
| `frame.present` | `bgfx::frame()`: the wait for the GPU and the screen (with vsync on, the time left over in a 60 Hz frame shows here) |

**Counters** (a frame): `particles.live` (particles alive in the effects drawn), `particles.effects`,
`particles.gpu_emitters`, `anim.players`, `sprites.quads`.

**The GPU** (bgfx's statistics, the last frame it finished): draw calls, triangles (lists and strips), compute calls,
the frame's GPU time, and each view's GPU and CPU time (bgfx's profiler, on while the F4 window shows or the automatic
dump is armed). The views are named: `clear`, `game`, `ui`, `top`, `imgui`, `3d shadow` / `3d opaque` / `3d blended`
(the title's, the battle's or the floor's 3D scene), `capture`, `layer`. The frame's GPU time includes the wait for
vsync; the views' times are the work itself.

## 3. Adding a measurement

```cpp
#include "perf.h"
{ toms::perf::Scope l_oPerf(toms::perf::section::g_pszRenderParticles); ... }   // a block's time into its section
toms::perf::f_Count(toms::perf::counter::g_pszParticlesLive, l_iN);             // added to a counter this frame
```

A new section or counter: a line in `src/core/engine/perf.h` (`section` / `counter`; no names as strings, docs/project/12
§7), then a `Scope` or `f_Count` where it runs. Its name's start (`update.`, `render.`, `frame.`) puts it in the F4
window's table. `perf.h` is a header only: the particle and anim tools, which build `particle_fx.cpp` and
`anim_player.cpp` without the game, need nothing more. Main thread only. A scope costs two clock reads.

The host (`main_sdl.cpp`) ends each frame with `GameSession::f_EndPerfFrame()` after `bgfx::frame()`.

## 4. The first reading (2026-11-26)

The F30 boss fight, Direct3D 11, vsync on: 57 fps over 2 s, 102 draw calls, about 80 000 triangles; the game's own CPU
work under 1 ms a frame (the UI 0.4 ms the most), the GPU about 0.9 ms (the 3D stage's opaque view 0.5 ms). One frame
of 43 ms: `render.ui` 39 ms, RmlUi drawing the boss's name banner the first time (its glyphs). The time in
`frame.present` (15 ms) is vsync's wait. 

**The UI (the owner's reading, F30's map: `update.ui` 13 ms, 32 fps).** That was the **Debug** build (Visual Studio's F5):
every section was 15 to 40 times its Release time (`render.walk3d` 9.9 ms against 0.25, `update.ui` 6.1 ms against 0.42 on
this machine). Most of the UI's time was RmlUi re-checking every binding of every document each frame, the hidden screens
too (`GameUi::f_Sync` called `DirtyAllVariables`). Now only the screens on show are re-checked, and a screen once more the
frame it closes: F30's map, `update.ui` 0.42 → 0.21 ms in Release, 6.1 → 1.6 ms in Debug (48 → 60 fps). In Debug the
largest left is `render.walk3d` (10 ms). Judge the speed in Release (`tools\build.cmd`, or Visual Studio's
`windows-release` configuration); Debug is for stepping through the code.

## 5. Files

| What | Where |
|---|---|
| The checker (sections, counters, history, the dump) | `src/core/engine/perf.h` |
| The window (F4), the view names, the frame's GPU numbers | `src/game/src/game_session.cpp` (`f_DrawPerfWindow`, `f_NameViews`, `f_EndPerfFrame`) |
| bgfx's statistics | `src/engine/src/bgfx_host.cpp` (`f_BgfxHostFrameStats`, `f_BgfxHostSetProfiler`, `f_BgfxHostSetViewName`) |
| The options, `frame.present`, the dump at exit | `src/game/src/main_sdl.cpp` |
