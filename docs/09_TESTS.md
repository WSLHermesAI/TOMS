# 09 — Tests (CTest)

By default, `tools\test.cmd` runs only the tests that the current changes can break. `all` runs every check: the game
logic, the screens and the web build.

```bat
tools\build.cmd windows-release          :: build first: the tests are programs in the build folder
tools\test.cmd                           :: only the tests for the uncommitted changes (the project rule, CLAUDE.md)
tools\test.cmd all                       :: everything, windows-release, web included (about 45 seconds)
tools\test.cmd --since main              :: the tests for what changed since a commit or branch
tools\test.cmd --list                    :: print which tests would run, and why; run nothing
tools\test.cmd windows-debug -L unit     :: a ctest filter runs as given (only the unit tests of another preset)
```

**How the selection works** (`tools/test_select.py`). It reads the changed files (`git diff HEAD` plus untracked
files). **Every area has its own tests, and an area that did not change is not tested.** A glTF change runs no game
test, and a game change runs no glTF test:

| Area | Its files | Its tests |
|---|---|---|
| Game | `src/core/game`, `src/game`, `assets/` (data, UI, fonts, sprites) | the game's unit tests (`src/core/game/**_test`) and the game screenshots on D3D11, narrowed by the file name: `player` / `saveload` → the menu scenes, `battle` / `combat` → battle, `store`, `dialog`, `title`, `stair` / `floor` / `stage` → the map scenes; any other name → all game scenes |
| glTF | glTF code, `gltf_viewer`, the mesh shaders, `tests/gltf/` | `unit.gltf_*` and the glTF screenshots |
| Particles | `particle_fx`, the particle editor, fx files and shaders | the particle checks, its unit test, `smoke.fx*` |
| Node animations | `anim_clip`, the anim editor, `.anim` files | the anim checks, its unit test, `smoke.anim` (`.anim` files also the game scenes) |
| Atlases | the atlas tool, atlas files | the atlas checks (atlas files also the game scenes) |
| Picture fonts | `tools/font`, `tests/font`, `src/core/engine/bitmap_font*` | `font.tool`, `font.editor_selftest` |
| Stage / event editor app | `tools/stage_event_editor` (and any `assets/data` file: `unit.stage_core_test`) | `unit.stage_core_test`, `stage_editor.app_selftest` |
| Qt editors' shared code | `tools/studio_common` (with the `i18n/` Chinese dictionaries) | the atlas / anim / particle / font editors' checks and `studio.*` |
| toms_editor | `src/editor` | no automated test; the desktop build compiles it |
| Resources | `src/core/engine/resource*`, `render_object*` (docs/25) | the engine's unit tests, the anim and particle checks, and the game area |
| Engine core | the rest of `src/core/engine` (conditions, nodes, events, …) | the engine's unit tests, and the game area |
| Renderer | `src/engine`: the shared files (`bgfx_*`, embedded shaders) | every screenshot on every backend |
| | `src/engine`: sprite, UI and ImGui files and shaders | the game screenshots on every backend |
| Web | `web/`, the web test | the web tests |
| Tests | a `*_test.cpp`; a `tests/golden/x.png`; a changed line of `tests/CMakeLists.txt` | exactly those tests (for a variable such as `_pm_give`, the tests that use it) |
| Global | the root `CMakeLists.txt`, presets, `cmake/`, `external/` | every desktop test |
| Nothing | docs, `*.md`, the HTML editors, `android/`, the test runner | none |

**Folders under `tests/`** are decided by [tests/test_folders.json](../tests/test_folders.json): it lists the folders that
are test data (`golden`, `smoke`, `tools`, `gltf`, `mpdi`, `stage`, `font`) and the tests each one feeds (ctest names,
`label:<label>`, or `@golden`). Any other folder under `tests/` is not test data and runs nothing (another project's
files, tool output); the run names it once as "not test data". Add a folder there when a new test reads it.

**Switching tests off:** `tools/test_switches.cmd` opens a window with every test as a tree (group > area > test) and a
checkbox each; Save writes the unticked ones to [tests/test_switches.json](../tests/test_switches.json), and
`test.cmd` never runs them (changed-only or `all`; a ctest filter given by hand runs as given). `test_switches.cmd
--list` prints the same tree as text. The run names the selected tests it skipped.

A file that no rule knows runs nothing, and is listed as **NOT MAPPED**: add a rule for it in `RULES`. The backend
variants (`stage_d3d12/vulkan/opengl`, `fx_cpu/compute/gpu`) run only when the renderer, their reference image or
the global setup changed. Without Python, `test.cmd` runs every test.

Visual Studio lists the same tests in **Test → Test Explorer** (from the CMake presets): run or debug one with a
click. Underneath is `ctest`, CMake's test runner (Visual Studio ships it; `tools\test.cmd` finds it), so
`ctest --preset windows-release` works too in a prompt that has it on `PATH`.

**All the unit tests are one program,** `bin\tests\toms_tests.exe`, so Visual Studio's target
list has one entry for them instead of thirty. Each test file keeps its own `main()`; the build
renames it to `toms_test_<name>`, and a generated driver runs the one named on the command line.
CTest still runs and reports every test on its own. Run it in `assets\`:

```
toms_tests save_test      one test (the names are the file names: save_test, particle_fx_test, ...)
toms_tests --list         every name
toms_tests --all          all of them in one go
```

**Editors on the game renderer** (not in CTest: they open a window). `atlas_editor`, `anim_editor`
and `particle_editor` each have `--selftest-gpu <file> <outdir>`. It checks the viewport runs on
toms_game's renderer, draws and edits there, and saves bgfx screenshots to compare. The offscreen
`--selftest` checks the QPainter fallback.

To debug one in Visual Studio, pick **"toms_tests (one unit test)"** in the target list and change
its `args` in `launch.vs.json`. Because every test is linked into the same program, a type or
helper a test defines for itself goes in an anonymous namespace. Two tests each had a different
`struct MockContext`, and linked together one silently replaced the other.

## What runs

| Label | Tests | What they check | Needs |
|---|---|---|---|
| `unit` | 41 | the game's own `*_test.cpp` programs next to the code in `src/core` (saves, title flow, conditions, battle bars, camera, equipment, skills, forge, hub, endings, cycles, floors, footprints, missions, node animations, particle effects, glTF models, …), including checks on the shipped `assets/data` | nothing |
| `atlas` | 3 | `atlas.core`: the atlas tool's packer, child sprites, every export format read back, and old `.pi` files rebuilt pixel for pixel; `atlas_sprites_test`: every sprite name the code, data and UI use is in the game's atlases; `atlas.assets_up_to_date`: `assets/media/atlas/game.atlasproj` opens from its packed atlas alone and its other files are current ([14](14_ATLAS_TOOL.md)); `atlas.fx_up_to_date`: the same for the particle sprites `fx.atlasproj` ([17](17_PARTICLES.md)) | nothing (all three are also in `unit`; `atlas_sprites_test` only there) |
| `particle` | 1 | `particle.check_recipes`: `particle_editor --headless check` on `docs/examples/fx_recipes.particle` (it parses, every sprite is in its atlases, curves / pools / bursts are sane; [17](17_PARTICLES.md)) | the Qt editor build (also in `unit`) |
| `anim` | 6 | `anim.check_*` (4): `anim_editor --headless check` on `tests/smoke/anim_preview.anim`, the recipe examples `docs/examples/anim_recipes.anim` and the game's `monster_idle` / `door_open` (they parse, every sprite is in the game atlas, no overlapping keys; [15](15_ANIMATION.md), [16](16_ANIMATION_RECIPES.md)); `anim.mpdi_convert`: old `.mpdi` files in `tests/mpdi/` → `.anim`; `anim.editor_selftest`: the editor offscreen (`-platform offscreen`, no window) -- selection, Properties and key list edits, viewport drags, undo, save | the Qt editor build (also in `unit`) |
| `font` | 2 | `font.tool`: the picture font runtime (`.fnt` parsing, layout) and the font tool: `tests/font/connect_score_pictures` (single pictures) rebuild the old project's `connect_score.fnt` byte for byte, and the test text drawn with it must match `tests/font/preview.png`; `font.editor_selftest`: `font_editor` offscreen ([22](22_FONT_TOOL.md)) | nothing (both also in `unit`; the selftest needs the Qt build) |
| `stage_editor` | 2 | `unit.stage_core_test`: the native stage editor's rules on the real floors (every floor written back byte for byte, painting / undo, the checks and story check, auto-place with the HTML editor's random numbers, the floor index byte for byte with `floor_index.json`); `stage_editor.app_selftest`: `stage_event_editor` offscreen, docked tabs ↔ floating ↔ split ([19](19_STAGE_EDITOR.md) section 6) | the Qt build for the selftest (both also in `unit`) |
| `studio` | 3 | `studio.canvas_coordinates`: the editors' canvas base (coordinate readout); `studio.language`: the editors' English / 中文 switch on a small window, offscreen ([24](24_EDITOR_LANGUAGE.md)); `studio.i18n_complete`: every `tr()` text of the Qt editors has its Chinese in `tools/studio_common/i18n/zh_TW` | the Qt build; `i18n_complete` needs Python (also in `unit`) |
| `smoke` | 21 | the real `toms_game` plays a scripted scene; its screenshot must match the reference image in `tests/golden/` (including `smoke.anim` and `smoke.fx`: an `.anim` clip and a particle effect over the map; `smoke.fx_cpu` / `fx_compute`: the same through the sprite batch's other paths, and `smoke.fx_gpu`: with every emitter simulated on the GPU -- all against the same image) | a GPU; each test opens a window for about 2 s |
| `smoke` + `gltf` | 24 | `gltf_viewer` renders a test model of `tests/gltf/` at a frozen time; the screenshot must match `tests/golden/gltf_*.png`: skinning (linear, cubic spline), morph targets (linear, step), `EXT_mesh_gpu_instancing`, PBR materials, the normals view, shadows (directional, point, spot, all); skin, PBR and shadows also on Vulkan and OpenGL ([18](18_GLTF.md)). `crowds (instanced skinning, [18](18_GLTF.md) §6) on D3D11, Vulkan and OpenGL, one of them against the `gltf_skin` image; physics (KHR_physics_rigid_bodies on Jolt, [18](18_GLTF.md) §7) at 2.5 s; the file's own lights (KHR_lights_punctual) through its cameras, `gltf_lights` (perspective) and `gltf_camera_side` (orthographic). `ctest -L gltf` runs only these | a GPU; about 1 s each |
| `web` | 5 | `tools/web_smoke_test.mjs` in headless Chrome: title → new game → save → reload → the save is still there, no page errors. Against the single-threaded build, the threaded build (served with the isolation headers), and the packaged page twice: on a server without the headers (it must become threaded through its service worker) and with `?nothreads` ([10](10_THREADS.md)), and once more on the **real GPU** (`web.page_gpu`, `--gpu`: ANGLE on Direct3D 11, stricter about shaders than the SwiftShader software GPU the others use). Every WebGL shader that fails to compile or link is logged and fails the test | the web builds / package (`build_web.bat`), Node 22+, Chrome or Edge; a missing one is **skipped**, not failed |

The smoke scenes are title, map (HUD + pad), the player menu's five tabs (`player_status`, `player_gear`, `player_items`,
`player_events`, `player_system`; [20](20_PLAYER_MENU.md)), dialogue, battle (after one attack), store, and a node animation
over the map (`--anim`, [15](15_ANIMATION.md)). The player-menu scenes start with `--give=<item ids>,gold:<n>`: the
player gets those items and gear when the title closes, so the grids, the gear comparison and the scrolling have
something to show. `unit.player_menu_test` checks the menu's rules and the item, store, stats and title data, and that every
event a floor lists has a pool record with English text. Two scenes show locked stairs ([05](event_editor/05_FLOOR_LINKS.md)
section 3.2): `stairs_locked` walks to F01's stair, locked by `--stair-lock=F01:ev_village_well` (the padlock and the
hint), and `stairs_unlock` finishes the event with `--events-done=` (the "Stairs unlocked" dialog). `bell_gate` starts a new
game on F04 (`--goto=F04`) with the bell-carved key, opens the key gate and fires the event in the room behind it
([event_editor/07](event_editor/07_KEY_GATES.md)); `unit.event_chain_test` checks event actions, gates and the bell chain in the data.
NPCs and their events ([event_editor/08](event_editor/08_NPC_DIALOGUE.md)): `talk_menu` steps onto the village elder on F02
(his topic menu), `talk_chain` plays *The Well at Night* and talks again (the ledger woken, the well listed as done), and
`talk_trusted` shows a `varChanged` event's notice, `talk_repeat` buys the repeatable potion three times, and
`unit.talk_system_test` checks the talk logic, saves and every NPC file, dialogue and action in the data.
`unit.stair_links_test` checks the stair list itself, and `unit.floor_index_test` that `assets/data/events/floor_index.json`
still matches the floors (when it fails: open the project folder in the stage or event editor, which rebuilds it).
They run on Direct3D 11, and the map scene also runs on Direct3D 12, Vulkan and OpenGL. The scene list is in
`tests/CMakeLists.txt`, one line per test.

## How the screenshot tests work

- `toms_game --fixed-dt=16 --frames=N --keys=… --clicks=… --screenshot=…`: every frame advances exactly 16 ms and
  real mouse/keyboard input is ignored, so a scene is the same pixels on every run.
- Each test runs in a fresh folder under `Build\<preset>\test-run\<name>\`, so no earlier save changes the
  title screen.
- `image_diff` (`tests/tools/image_diff.cpp`) counts pixels that differ by more than a small amount per colour
  channel. Limits: 0.1% of the screen on Direct3D 11, which matches exactly in practice, and 0.5% on the other
  backends (OpenGL differs on about 0.02% at edges). A changed HP bar is 0.3% of the screen and fails.
- On a HiDPI display the window is larger; the screenshot is scaled to the reference size before comparing.

**When a smoke test fails**, the test output names two files in its `test-run` folder: the screenshot and
`<name>.diff.png` (differing pixels in red over a dimmed reference). If the change was a mistake, fix it. If it was
intended (a new layout, a new colour), update the references and **look at them** before committing:

```bat
set TOMS_UPDATE_GOLDEN=1
tools\test.cmd windows-release -L smoke
set TOMS_UPDATE_GOLDEN=
```

Only a test named after its reference image writes it (`smoke.stage` writes `stage.png`; `smoke.stage_vulkan`
compares with it but never replaces it).

## Adding tests

- **A unit test:** write `src/core/…/xxx_test.cpp` with a `main()` that returns 0 on success, and add its path to
  `TOMS_UNIT_TESTS` in `tests/CMakeLists.txt`. It links the core library, and runs in `assets/`, so it reads data as
  `data/…`.
- **A screen:** add a `toms_smoke_test(name reference backend limit scene…)` line, run it once with
  `TOMS_UPDATE_GOLDEN=1`, check the new `tests/golden/<name>.png`, and commit it.

The shipping preset builds no tests (`TOMS_BUILD_TESTS=OFF`). Web and Android builds never do.
