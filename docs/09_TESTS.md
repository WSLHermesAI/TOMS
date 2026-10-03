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

## What runs

| Label | Tests | What they check | Needs |
|---|---|---|---|
| `unit` | 26 | the game's own `*_test.cpp` programs next to the code in `src/core` (saves, title flow, conditions, battle bars, camera, equipment, skills, forge, hub, endings, cycles, floors, footprints, missions, …), including checks on the shipped `assets/data` | nothing |
| `atlas` | 3 | `atlas.core`: the atlas tool's packer, child sprites, every export format read back, and old `.pi` files rebuilt pixel for pixel; `atlas_sprites_test`: every sprite name the code, data and UI use is in the game's atlases; `atlas.assets_up_to_date`: `assets/media/atlas/game.atlasproj` opens from its packed atlas alone and its other files are current ([14](14_ATLAS_TOOL.md)) | nothing (all three are also in `unit`; `atlas_sprites_test` only there) |
| `smoke` | 10 | the real `toms_game` plays a scripted scene; its screenshot must match the reference image in `tests/golden/` | a GPU; each test opens a window for about 2 s |
| `web` | 4 | `tools/web_smoke_test.mjs` in headless Chrome: title → new game → save → reload → the save is still there, no page errors. Against the single-threaded build, the threaded build (served with the isolation headers), and the packaged page twice: on a server without the headers (it must become threaded through its service worker) and with `?nothreads` ([10](10_THREADS.md)) | the web builds / package (`build_web.bat`), Node 22+, Chrome or Edge; a missing one is **skipped**, not failed |

The smoke scenes are title, map (HUD + pad), in-game menu, inventory, dialogue, battle (after one attack) and store.
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
