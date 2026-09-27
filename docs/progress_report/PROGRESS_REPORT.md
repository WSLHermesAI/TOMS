# Progress Report — TOMS on Qt6 + bgfx

> **Purpose:** the living status board for TOMS on Qt6 + bgfx and its migration plan
> ([../04_MIGRATION_PLAN.md](../04_MIGRATION_PLAN.md)). Read the **Next Step** line first when
> resuming work: it says what to do next. Below it are the phase status table, the **Waiting to
> finish** list, a **Log** index (the dated entries live in `1_PROGRESS_REPORT.md`,
> `2_PROGRESS_REPORT.md`, …, oldest first), and **Open Questions**, meaning decisions that need
> the project owner before work continues.
>
> The game's code is `src/core` (compiled unmodified until phase 3) and its content is `assets/`.
> Until 2026-09-27 this project lived in a `toms_next/` subfolder; it is now the repository root
> and that folder no longer exists (older dated entries still mention it).

---

## ▶ Next Step

**Next action: Phase 2, step 2: tests.** Since 2026-09-27 ([part 3](3_PROGRESS_REPORT.md)) this project is the repository root with two top-level folders, `assets/` (all content: `media/`, `data/`) and `src/` (all code, the core game code in `src/core`), and desktop, editor and web all build and run from there. The web build (2026-09-26, [part 2](2_PROGRESS_REPORT.md)):
`tools\build_web.cmd` + `tools\serve_web.cmd`, guide in [../06_BUILD_WEB.md](../06_BUILD_WEB.md).

1. Register the old unit tests (`src/core/**/*_test.cpp`, ~30 files) as CTest targets in
   `tests/`, so Visual Studio's Test Explorer lists them.
2. Add smoke tests to CTest: `toms_game --frames --screenshot` compared with a stored golden PNG per
   backend (tolerance; see W7), and `tools/web_smoke_test.mjs` against the web build.
3. **Check:** CTest green from VS and from the command line.

Before that, two quick owner checks would be worth more than more code: play the web build on a real
phone (W9), and press F5 in the Visual Studio IDE once (W1).

How to build the web version now: `tools\build_web.cmd` (Windows; cmd/PowerShell/Explorer, not Git
Bash). The old project's web build (`TOMS/docs/building/BUILD_WEB.md`) still works and is independent.

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
| 2 | Web build (bgfx WebGL2): presets, `build_web.cmd`, `serve_web.cmd`, IDBFS saves, page shell, doc 06 | ✅ 2026-09-26, verified in headless Chrome (release + debug) |
| 2 | Old unit tests registered with CTest; golden-image smoke test | ⬜ **next** |
| 2 | Mobile scaling on web (`setUiScale` / `setPadScale`, the owner's rule) | ◐ wired for small viewports; only `UiRoot` screens grow (game-side work) |
| 3 | Edit `src/core` directly: drop `src/game/compat/`, split `Game`, game UI on RmlUi | ◐ RmlUi store test done 2026-09-27 ([08](../08_RMLUI.md)) |
| 4 | Editor: data-driven stage editor on the bgfx viewport, docking, inspector, undo, event editor | ⬜ |
| 5 | 3D on bgfx (glTF, ozz skinning, instancing, shadows, Effekseer) | ⬜ researched only |

---

## Waiting to finish (known gaps and unverified items)

| # | Item | Why it is open | How to close it |
|---|---|---|---|
| W1 | **F5 inside the Visual Studio IDE** | builds used the same presets from `tools\build.cmd`; `launch.vs.json` startup items not tried in the IDE | open the `TOMS` root folder in VS, F5 `toms_game` and `toms_editor` |
| W2 | **Popups not clicked through** | they block unattended runs; only the console path and the logic were run | run `tools\check_env.cmd` on a machine missing Qt/font; configure once without Qt |
| W3 | **Fresh-machine test** | this machine already had everything (Qt 6.9.0, VS 2026, SDK, font) | clone on a clean PC/VM, follow `02_INSTALL_WINDOWS.md` only |
| W4 | **Editor frame rate 26–37 fps** in the smoke test (1972×1634 viewport) | not investigated; may be startup frames, the `QTimer(0)` + vsync interplay, or HiDPI fill | measure with `--stats` over a longer run; compare with `toms_game` at the same size |
| W5 | **`11_BGFX_QT_ARCHITECTURE.md`** in EngineBlueprint + UI-tool rows in docs 02/04/07 | the UI-editor research agent stopped before finishing | redo the research: UI editors whose output bgfx can use (RmlUi, NoesisGUI, Rive, Lottie/ThorVG, …); add the glTF/skinning findings |
| W6 | **"???????" label on stage 1** | shows with both fonts, so it comes from data rather than a missing glyph, but not confirmed | find the source string in `data/` |
| W7 | **Vulkan ~1 % pixel difference** (glyph/panel edges) | one-pixel rasterisation offset; accepted for now | golden images per backend in phase 2 |
| W8 | **Commits** | web build (`e1bd8d8`) and black-canvas fix (`be50e16`) committed; the 2026-09-27 move to the root and the `assets/` + `src/` layout are not yet | owner decides when to commit |
| W9 | **Web build on a real phone/tablet**, Firefox, Safari | only headless Chrome was run (incl. an 844×390 viewport) | open `serve_web.cmd`'s URL from a phone on the same network (serve with `--bind 0.0.0.0`) |
| W10 | **Linux/WSL web build in a browser** | builds and links on WSL (exit 0, 2026-09-26, `817627c`) but was never opened in a browser there, and not re-run since the move to the root | run `tools/web_smoke_test.mjs` against it on a machine with Chrome |
| W13 | **Web resize while the tab is throttled** | in a visible window at DPR 2.4, one resize (1600×800, wider than the screen) kept the old canvas size while Chrome drew ~9 frames; the next resize caught up | resize the real browser window by hand a few times; if it sticks, also react to the browser `resize` event in `shell.html` |
| W14 | **CJK font `wqy-zenhei.ttc` lost** | gitignored, deleted with the old folder; desktop uses the JhengHei fallback | download it into `assets/media/` |
| W12 | **Headless tests miss display-scale bugs** | the black-canvas bug (2026-09-26) only showed in a visible window at DPR 2.4 | extend `web_smoke_test.mjs` with a visible-window / `--force-device-scale-factor=2.4` run and a canvas-size check |
| W11 | **Web presets inside the VS IDE** | built only through `build_web.cmd` (which loads emsdk first) | select `web-release-windows` in VS after running `emsdk_env` in a developer prompt, or keep using the script |

---

## Log

| Part | Dates | Contents |
|---|---|---|
| [1_PROGRESS_REPORT.md](1_PROGRESS_REPORT.md) | 2026-09-25 | Qt + bgfx decision; EngineBlueprint update; bgfx glTF / skinning / morph / instancing research; toms_next phase 1 built and verified; WSL web presets and the etcpak stop |
| [3_PROGRESS_REPORT.md](3_PROGRESS_REPORT.md) | 2026-09-27 | the project moved to the repository root; old code/content restored and organised as `assets/` (content) + `src/` (code); desktop, editor and web rebuilt and verified from there |
| [2_PROGRESS_REPORT.md](2_PROGRESS_REPORT.md) | 2026-09-26 | web build done on Windows (host shaderc, `bimg_encode` excluded, one entry point for desktop + web, IDBFS saves); five bugs fixed; doc 06; `web_smoke_test.mjs` |

Short dated bullets: [WORKING_LOG.md](WORKING_LOG.md).

---

## Open Questions (need the owner)

| # | Question | Default if no answer |
|---|---|---|
| Q1 | Commit the 2026-09-27 move (root, `assets/` + `src/`) on `main`? | nothing committed until asked |
| Q2 | ~~Keep the folder name `toms_next`?~~ Answered 2026-09-27: no folder, the project is the root | — |
| Q3 | Is browser **WebGPU** definitely not needed? (bgfx only does WebGL2 in the browser) | WebGL2 only, as decided 2026-09-25 |
| Q4 | Game UI library for phase 3: RmlUi, or own widgets? Depends on W5's research | decide after W5 |
| Q5 | The bgfx web build now passes its check (2026-09-26). Replace the old `build_web.sh` output (`web/`, `web-gl/`) with it, and where is it published? | keep both until the owner has played the new one (W9) |
