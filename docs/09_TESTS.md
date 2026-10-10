# 09 — Tests (CTest)

One command runs every check: the game logic, the screens and the web build.

```bat
tools\build.cmd windows-release          :: build first: the tests are programs in the build folder
tools\test.cmd                           :: everything, windows-release (about 45 seconds)
tools\test.cmd windows-debug -L unit     :: only the unit tests of another preset (under a second)
```

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
| `unit` | 36 | the game's own `*_test.cpp` programs next to the code in `src/core` (saves, title flow, conditions, battle bars, camera, equipment, skills, forge, hub, endings, cycles, floors, footprints, missions, node animations, particle effects, glTF models, …), including checks on the shipped `assets/data` | nothing |
| `atlas` | 3 | `atlas.core`: the atlas tool's packer, child sprites, every export format read back, and old `.pi` files rebuilt pixel for pixel; `atlas_sprites_test`: every sprite name the code, data and UI use is in the game's atlases; `atlas.assets_up_to_date`: `assets/media/atlas/game.atlasproj` opens from its packed atlas alone and its other files are current ([14](14_ATLAS_TOOL.md)); `atlas.fx_up_to_date`: the same for the particle sprites `fx.atlasproj` ([17](17_PARTICLES.md)) | nothing (all three are also in `unit`; `atlas_sprites_test` only there) |
| `particle` | 1 | `particle.check_recipes`: `particle_editor --headless check` on `docs/examples/fx_recipes.particle` (it parses, every sprite is in its atlases, curves / pools / bursts are sane; [17](17_PARTICLES.md)) | the Qt editor build (also in `unit`) |
| `anim` | 2 | `anim.check_preview` / `anim.check_recipes`: `anim_editor --headless check` on `tests/smoke/anim_preview.anim` and on the recipe examples `docs/examples/anim_recipes.anim` (they parse, every sprite is in the game atlas, no overlapping keys; [15](15_ANIMATION.md), [16](16_ANIMATION_RECIPES.md)) | the Qt editor build (also in `unit`) |
| `smoke` | 18 | the real `toms_game` plays a scripted scene; its screenshot must match the reference image in `tests/golden/` (including `smoke.anim` and `smoke.fx`: an `.anim` clip and a particle effect over the map; `smoke.fx_cpu` / `fx_compute`: the same through the sprite batch's other paths, and `smoke.fx_gpu`: with every emitter simulated on the GPU -- all against the same image) | a GPU; each test opens a window for about 2 s |
| `smoke` + `gltf` | 17 | `gltf_viewer` renders a test model of `tests/gltf/` at a frozen time; the screenshot must match `tests/golden/gltf_*.png`: skinning (linear, cubic spline), morph targets (linear, step), `EXT_mesh_gpu_instancing`, PBR materials, the normals view, shadows (directional, point, spot, all); skin, PBR and shadows also on Vulkan and OpenGL ([18](18_GLTF.md)). `ctest -L gltf` runs only these | a GPU; about 1 s each |
| `web` | 5 | `tools/web_smoke_test.mjs` in headless Chrome: title → new game → save → reload → the save is still there, no page errors. Against the single-threaded build, the threaded build (served with the isolation headers), and the packaged page twice: on a server without the headers (it must become threaded through its service worker) and with `?nothreads` ([10](10_THREADS.md)), and once more on the **real GPU** (`web.page_gpu`, `--gpu`: ANGLE on Direct3D 11, stricter about shaders than the SwiftShader software GPU the others use). Every WebGL shader that fails to compile or link is logged and fails the test | the web builds / package (`build_web.bat`), Node 22+, Chrome or Edge; a missing one is **skipped**, not failed |

The smoke scenes are title, map (HUD + pad), the player menu's five tabs (`player_status`, `player_gear`, `player_items`,
`player_events`, `player_system`; [20](20_PLAYER_MENU.md)), dialogue, battle (after one attack), store, and a node animation
over the map (`--anim`, [15](15_ANIMATION.md)). The player-menu scenes start with `--give=<item ids>,gold:<n>`: the
player gets those items and gear when the title closes, so the grids, the gear comparison and the scrolling have
something to show. `unit.player_menu_test` checks the menu's rules and the item, store, stats and title data.
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
