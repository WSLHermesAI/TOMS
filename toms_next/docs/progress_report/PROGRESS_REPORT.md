# Progress Report — toms_next (Qt6 + bgfx split)

> **Purpose:** the living status board for `toms_next` and its migration plan
> ([../04_MIGRATION_PLAN.md](../04_MIGRATION_PLAN.md)). Read the **Next Step** line first when
> resuming work: it says what to do next. Below it are the phase status table, the **Waiting to
> finish** list, a **Log** index (the dated entries live in `1_PROGRESS_REPORT.md`,
> `2_PROGRESS_REPORT.md`, …, oldest first), and **Open Questions**, meaning decisions that need
> the project owner before work continues.
>
> The game-content track (chapters, story, balance) is tracked separately in
> `TOMS/docs/progress_report/`. It is unaffected by this split, because toms_next compiles the same `src/`.

---

## ▶ Next Step

**Next action: finish Phase 2 step 1 on the WSL side — write `game/src/main_web.cpp`.** The presets and
prerequisites are done and the web configure succeeds (see the 2026-09-25 entry); what is missing is the
browser entry point:

1. `game/src/main_web.cpp`: SDL3's browser main loop plus a port of the glue in
   `src/engine/emscripten_main.cpp` (IDBFS saves, the Canvas-2D system-font path, the JS test hooks), and
   a web branch in `game/CMakeLists.txt` (today it only has `add_executable(toms_game src/main_sdl.cpp)`).
2. Then the acceptance check: stage 1 plays in Chrome/Edge from `http://localhost:8099/...`, and a save
   survives a page reload.

On a Linux/WSL machine the build is:

    export PATH="$HOME/opt/cmake/bin:$HOME/.local/bin:$PATH"; source ~/opt/emsdk/emsdk_env.sh
    cmake --preset web-release && cmake --build --preset web-release

Prerequisites: `tools/check_env.sh` (the WSL/Linux twin of `check_env.ps1`) — 6 ok, 0 missing as of
2026-09-25. Windows/Visual Studio remains the path for the desktop and editor presets; the web presets
are conditioned to Linux and use `$env{EMSDK}` on either platform.

Until step 1 lands, the browser version is still built from the old project
(`emcmake cmake -S . -B build-web -G Ninja -DWEB=ON` in the TOMS root; see `TOMS/docs/building/BUILD_WEB.md`).

---

## Status

| Phase | What | Status |
|---|---|---|
| 0 | Decision: bgfx runtime, Qt6 editor, shipped builds contain bgfx only | ✅ 2026-09-25 |
| 0 | EngineBlueprint docs updated for the decision | ◐ 10 of 11 docs updated; `11_BGFX_QT_ARCHITECTURE.md` not written yet (already linked) |
| 1 | Existing game on bgfx through the compat `renderer.h` (no edits to `src/`) | ✅ verified on D3D11, D3D12, Vulkan, OpenGL |
| 1 | SDL3 game executable `toms_game.exe` (no Qt) | ✅ verified; imports no Qt DLLs |
| 1 | Qt editor `toms_editor.exe`: bgfx Play view, stage list, session panel, old stage editor tab | ✅ verified on D3D11 |
| 1 | Prerequisite checks with popups + links (`check_env`, CMake configure, runtime) | ✅ console output verified; popups not clicked through |
| 1 | Visual Studio presets, `launch.vs.json`, `build.cmd` | ◐ presets built from the command line; not yet pressed F5 in the IDE |
| 1 | Docs 01–05 | ✅ |
| 2 | Web build (bgfx WebGL2) | ⬜ **next** |
| 2 | Old unit tests registered with CTest; golden-image smoke test | ⬜ |
| 2 | Mobile layout switches (`setDesignSize`, `g_uiScale`) | ⬜ |
| 3 | Move `src/game` into toms_next, drop `game/compat/`, split `Game` | ⬜ |
| 4 | Editor: data-driven stage editor on the bgfx viewport, docking, inspector, undo, event editor | ⬜ |
| 5 | 3D on bgfx (glTF, ozz skinning, instancing, shadows, Effekseer) | ⬜ researched only |

---

## Waiting to finish (known gaps and unverified items)

| # | Item | Why it is open | How to close it |
|---|---|---|---|
| W1 | **F5 inside the Visual Studio IDE** | builds used the same presets from `tools\build.cmd`; `launch.vs.json` startup items not tried in the IDE | open `toms_next` in VS, F5 `toms_game` and `toms_editor` |
| W2 | **Popups not clicked through** | they block unattended runs; only the console path and the logic were run | run `tools\check_env.cmd` on a machine missing Qt/font; configure once without Qt |
| W3 | **Fresh-machine test** | this machine already had everything (Qt 6.9.0, VS 2026, SDK, font) | clone on a clean PC/VM, follow `02_INSTALL_WINDOWS.md` only |
| W4 | **Editor frame rate 26–37 fps** in the smoke test (1972×1634 viewport) | not investigated; may be startup frames, the `QTimer(0)` + vsync interplay, or HiDPI fill | measure with `--stats` over a longer run; compare with `toms_game` at the same size |
| W5 | **`11_BGFX_QT_ARCHITECTURE.md`** in EngineBlueprint + UI-tool rows in docs 02/04/07 | the UI-editor research agent stopped before finishing | redo the research: UI editors whose output bgfx can use (RmlUi, NoesisGUI, Rive, Lottie/ThorVG, …); add the glTF/skinning findings |
| W6 | **"???????" label on stage 1** | shows with both fonts, so it comes from data rather than a missing glyph, but not confirmed | find the source string in `data/` |
| W7 | **Vulkan ~1 % pixel difference** (glyph/panel edges) | one-pixel rasterisation offset; accepted for now | golden images per backend in phase 2 |
| W8 | **Nothing is committed** | toms_next/ and the EngineBlueprint edits are uncommitted working-tree changes | owner decides branch/commit (see Q1) |

---

## Log

| Part | Dates | Contents |
|---|---|---|
| [1_PROGRESS_REPORT.md](1_PROGRESS_REPORT.md) | 2026-09-25 | Qt + bgfx decision; EngineBlueprint update; bgfx glTF / skinning / morph / instancing research; toms_next phase 1 built and verified |

Short dated bullets: [WORKING_LOG.md](WORKING_LOG.md).

---

## Open Questions (need the owner)

| # | Question | Default if no answer |
|---|---|---|
| Q1 | Commit `toms_next/` + the EngineBlueprint edits on `main`, or on a branch (e.g. `bgfx-qt`)? | nothing committed until asked |
| Q2 | Keep the folder name `toms_next`? | keep |
| Q3 | Is browser **WebGPU** definitely not needed? (bgfx only does WebGL2 in the browser) | WebGL2 only, as decided 2026-09-25 |
| Q4 | Game UI library for phase 3: RmlUi, or own widgets? Depends on W5's research | decide after W5 |
| Q5 | Should the web build in toms_next replace the old `build_web.sh` output (`web/`, `web-gl/`) when it works? | keep the old one until the new one passes its check |
