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

**The event and stage editors work in HTML** (2026-10-05 → 10-08, [6_PROGRESS_REPORT.md](6_PROGRESS_REPORT.md)):
- **One folder:** [tools/web_editors/](../../tools/web_editors/index.html). Run `serve.cmd`, then switch
  editors with **⇄**.
- **Event editor:**
  - events, floor links with locked stairs, event logic, a node graph, a values map;
  - event **kinds** (new `assets/data/events/kinds.json`);
  - a 29-step tutorial with Play demo.
- **Stage editor:** paints the floors, places events, and has a story check and safe auto-place. It
  saves the live files.

**Owner, next:**
- **Confirm the event editor** by trying it on `http://localhost:8000/tools/web_editors/index.html#event`.
  Confirming starts the Qt/C++ version on the game renderer ([../19_STAGE_EDITOR.md](../19_STAGE_EDITOR.md) §6).
- **Answer Q6–Q10** below (generated floors, stair locks, rebirth, the `@` start, event lists).
- **Commit** the 10-08 work (the folder move, Kinds, the deleted mockups).

**3D is in the engine** (2026-10-05, [5_PROGRESS_REPORT.md](5_PROGRESS_REPORT.md), [../18_GLTF.md](../18_GLTF.md)):
- **glTF 2.0:** skins, morph targets, instancing, PBR, glTF cameras and lights, and shadow maps for
  directional, point and spot lights.
- **Viewer:** `gltf_viewer`, plus 17 screenshot tests.
- **In the game:** the title screen plays `VirtualCity.glb` behind its menu.

**Owner, first:**
- **Publish:** run `publish_web.bat nobuild` and answer **y** (W18). The web package is built and
  tested; only the push needs your GitHub sign-in.
- **Still due:** the **legacy import bridge** (`.prt`/`.prtg` → `.particle`, `.pi`/`.mpdi` →
  atlas/`.anim`).

**The content tools are in** (2026-10-03, [4_PROGRESS_REPORT.md](4_PROGRESS_REPORT.md)):
- **Three editors:** `atlas_editor`, `anim_editor` and `particle_editor`. All three preview with
  toms_game's own renderer.
- **Particles:** the new `.particle` JSON format. Particles simulate on the GPU when an emitter
  asks for it or is over the `particleGpuThreshold` setting (5000 desktop, 3000 phone).
- **Docs:** [../14_ATLAS_TOOL.md](../14_ATLAS_TOOL.md), [../15_ANIMATION.md](../15_ANIMATION.md),
  [../16_ANIMATION_RECIPES.md](../16_ANIMATION_RECIPES.md), [../17_PARTICLES.md](../17_PARTICLES.md).
- **Due now:** the **legacy import bridge** (`.prt`/`.prtg` → `.particle`, `.pi`/`.mpdi` →
  atlas/`.anim`).

**Phase 2 is done** (2026-09-30): `tools\test.cmd` runs 40 tests in ~80 s: 26 unit tests, 10 screenshot
smoke tests (7 scenes on D3D11, the map on D3D12/Vulkan/OpenGL), and 4 web tests (single- and multithreaded
builds, the published page with and without threads). Visual Studio's Test Explorer lists the same ones.
Guide: [../09_TESTS.md](../09_TESTS.md). Run it before committing.

**Threads are ready for heavier work** (2026-09-30, [../10_THREADS.md](../10_THREADS.md)): a job system
(8 workers here, 4 in the browser, none in the single-threaded web build) and two web builds; the page picks
the threaded one where the browser allows it, and on GitHub Pages a service worker makes that possible.

**Android runs on the emulator** (2026-09-30, [../07_BUILD_ANDROID.md](../07_BUILD_ANDROID.md)):
`tools\build_android.cmd` builds the APK, `tools\run_android.cmd` installs and starts it. Checked on the
Android Studio emulator (Pixel Tablet, API 35): title, new game, touch pad and tap-to-walk, a battle, saves
that survive a force-stop, loading, background → foreground, the Back key. The arm64 phone build compiles.

**Next, the owner picks one track:**

- **A. Android on a real phone**: install `tools\build_android.cmd debug` on an arm64 phone and check it
  as a player (button sizes, text). Also open: audio pause while in the background.
- **B. Phase 3, the rest** ([../04_MIGRATION_PLAN.md](../04_MIGRATION_PLAN.md)): drop
  `src/game/compat/renderer.h` (the core code creates the bgfx renderer directly) and split the
  `Game` class.

Before either, two quick owner checks: play the web build on a real phone (W9) and press F5 once in
the Visual Studio IDE (W1).

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
| 2 | Tests in CTest: 25 unit, 10 screenshot smoke (4 backends), web smoke; `tools\test.cmd`, doc 09 | ✅ 2026-09-30, 36/36 pass; a changed HP bar is caught |
| 2 | Mobile scaling on web (`setUiScale` / `setPadScale`, the owner's rule) | ◐ wired for small viewports; the RmlUi pad and dialogue scale, other screens do not yet |
| 3 | Edit `src/core` directly: drop `src/game/compat/`, split `Game`, game UI on RmlUi | ◐ all UI on RmlUi (desktop + web) 2026-09-27 ([08](../08_RMLUI.md)); compat shim and `Game` split still open |
| 2 | Job system + multithreaded web build (the page chooses; `coi-serviceworker` for GitHub Pages), doc 10 | ✅ 2026-09-30, verified in headless Chrome; not on a phone or Safari |
| A | Android (`07_BUILD_ANDROID.md`): file access through `vfs.h`, saves, logcat, audio, shaders, linking, the APK (`android/`, `tools\build_android.cmd` / `run_android.cmd`) | ◐ 2026-09-30 runs on the emulator (x86_64); arm64 compiles; not yet on a real phone; audio pause in the background open |
| 4 | Editor: data-driven stage editor on the bgfx viewport, docking, inspector, undo, event editor | ◐ 2026-10-08 HTML versions done ([tools/web_editors](../../tools/web_editors/index.html), [19](../19_STAGE_EDITOR.md), [event_editor](../event_editor/README.md)); Qt/bgfx version waits for the owner to confirm the event editor |
| T | Atlas tool (`.atlasproj` → `.atlas`/`.plist`/XML; Qt + web), doc 14 | ✅ 2026-10-03 |
| T | Node animations (`.anim`, per-channel keys, `anim_editor`), docs 15–16 | ✅ 2026-10-03 |
| T | Particles (`.particle` JSON, `particle_editor`, GPU simulation, CPU/GPU threshold), doc 17 | ◐ 2026-10-03 editor + runtime done; legacy import, multiply/screen blend, effects on `.anim` nodes open |
| T | Editors preview with toms_game's `BgfxRenderer`; right-drag pans | ✅ 2026-10-03 on D3D11 |
| T | Anim editor: per-node playback (once / hide / loop), solo, clip switching with discard prompt; editors: History dock + automatic backups | ✅ 2026-10-05 |
| 5 | glTF 2.0 on bgfx (`gltf_model`, `GltfRenderer`, `gltf_viewer`): skins, morphs, instancing, PBR, cameras, lights, shadow maps; the title's 3D background | ◐ 2026-10-05 desktop D3D11/Vulkan/OpenGL + web verified; not on a phone; no IBL / cascades yet |
| 5 | 3D on bgfx (glTF, ozz skinning, instancing, shadows, Effekseer) | ⬜ researched only |

---

## Waiting to finish (known gaps and unverified items)

| # | Item | Why it is open | How to close it |
|---|---|---|---|
| W1 | **F5 inside the Visual Studio IDE** | builds used the same presets from `tools\build.cmd`; `launch.vs.json` startup items not tried in the IDE | open the `TOMS` root folder in VS, F5 `toms_game` and `toms_editor` |
| W2 | **Popups not clicked through** | they block unattended runs; only the console path and the logic were run | run `tools\check_env.cmd` on a machine missing Qt/font; configure once without Qt |
| W3 | **Fresh-machine test** | this machine already had everything (Qt 6.9.0, VS 2026, SDK); the font no longer needs installing | clone on a clean PC/VM, follow `02_INSTALL_WINDOWS.md` only |
| W4 | **Editor frame rate 26–37 fps** in the smoke test (1972×1634 viewport) | not investigated; may be startup frames, the `QTimer(0)` + vsync interplay, or HiDPI fill | measure with `--stats` over a longer run; compare with `toms_game` at the same size |
| W5 | **`11_BGFX_QT_ARCHITECTURE.md`** in EngineBlueprint + UI-tool rows in docs 02/04/07 | the UI-editor research agent stopped before finishing | redo the research: UI editors whose output bgfx can use (RmlUi, NoesisGUI, Rive, Lottie/ThorVG, …); add the glTF/skinning findings |
| ~~W6~~ | ~~"???????" label on stage 1~~ | closed 2026-09-27: it was an ImGui notification without CJK glyphs; notifications are RmlUi now and show their text | — |
| ~~W7~~ | ~~Vulkan ~1 % pixel difference~~ | closed 2026-09-30: with `--fixed-dt` the backends match the D3D11 reference (Vulkan/D3D12 0.000%, OpenGL 0.022%); smoke tests allow 0.5% | — |
| ~~W8~~ | ~~Commits~~ | closed: the root move, the RmlUi UI and the Android work are committed (up to `d7be854`) | — |
| W9 | **Web build on a real phone/tablet**, Firefox, Safari | only headless Chrome was run (incl. an 844×390 viewport) | open `serve_web.cmd`'s URL from a phone on the same network (serve with `--bind 0.0.0.0`) |
| W10 | **Linux/WSL web build in a browser** | builds and links on WSL (exit 0, 2026-09-26, `817627c`) but was never opened in a browser there, and not re-run since the move to the root | run `tools/web_smoke_test.mjs` against it on a machine with Chrome |
| W13 | **Web resize while the tab is throttled** | in a visible window at DPR 2.4, one resize (1600×800, wider than the screen) kept the old canvas size while Chrome drew ~9 frames; the next resize caught up | resize the real browser window by hand a few times; if it sticks, also react to the browser `resize` event in `shell.html` |
| ~~W14~~ | ~~CJK font `wqy-zenhei.ttc` lost~~ | closed 2026-09-27: the UI is RmlUi with a committed font (`NotoSansCJKtc-TOMS.otf`); no font download any more | — |
| W12 | **Headless tests miss display-scale bugs** | the black-canvas bug (2026-09-26) only showed in a visible window at DPR 2.4 | extend `web_smoke_test.mjs` with a visible-window / `--force-device-scale-factor=2.4` run and a canvas-size check |
| W15 | **Two docs numbered 07** | `07_BUILD_ANDROID.md` and `07_PUBLISH_GITHUB_PAGES.md` | renumber one (links in README / docs point at both) |
| W16 | **`smoke.stage*` fail** (4 of 50 CTest) | already failing before 2026-10-03; not investigated | run `tools	est.cmd`, compare the diff images, refresh the references or fix the cause |
| W17 | **GPU particles off D3D11** | measured and tested on D3D11 only; the web (GLES 3.0) has no compute and uses the CPU | run `toms_game --fx=docs/examples/fx_recipes.particle#… --fps` on D3D12/Vulkan/OpenGL and an Android phone |
| W18 | **Web build not published yet** | the push to `gh-pages` needs the owner's GitHub sign-in (Git Credential Manager), which a non-interactive session cannot do | run `publish_web.bat nobuild`, sign in once, answer **y**; check https://wslhermesai.github.io/TOMS/ after 1–2 min |
| W19 | **3D title on phones** | built for Android and checked in headless Chrome only; shadows are off there, the model adds 3 MB | run the APK / the web page on a real phone; watch the frame rate on the title |
| ~~W20~~ | ~~**Event editor cannot write files**~~ closed 2026-10-08: *Open project folder* + *Write to project*, with backups, merged with the stage editor's saves ([event_editor/README](../event_editor/README.md)) | *Save…* only shows the changes; the stage editor writes through *Open project folder* | give the event editor the same folder access, sharing files with the stage editor |
| W21 | **Event editor features not in the game yet** | the game ignores triggers / conditions / actions / `next` / `vars.json` / stair locks / `kind`; the "event finished" flag is set only for whispers and rescues | runtime work in [06](../event_editor/06_EVENT_LOGIC.md) §6 and [05](../event_editor/05_FLOOR_LINKS.md) |
| W22 | **Event data gaps** | 92 event ids that F22–F70 list are not defined by any pool; 4 orphan events | write the chapter 4–10 pools (the editor's batch fix makes placeholders) |
| W11 | **Web presets inside the VS IDE** | built only through `build_web.cmd` (which loads emsdk first) | select `web-release-windows` in VS after running `emsdk_env` in a developer prompt, or keep using the script |

---

## Log

| Part | Dates | Contents |
|---|---|---|
| [1_PROGRESS_REPORT.md](1_PROGRESS_REPORT.md) | 2026-09-25 | Qt + bgfx decision; EngineBlueprint update; bgfx glTF / skinning / morph / instancing research; toms_next phase 1 built and verified; WSL web presets and the etcpak stop |
| [3_PROGRESS_REPORT.md](3_PROGRESS_REPORT.md) | 2026-09-27 | the project moved to the repository root; old code/content restored and organised as `assets/` (content) + `src/` (code); desktop, editor and web rebuilt and verified from there |
| [6_PROGRESS_REPORT.md](6_PROGRESS_REPORT.md) | 2026-10-05 evening – 2026-10-08 | HTML event editor (tutorial, floor links, event logic, node graph, values map, kinds) and stage editor (story check, auto-place, overlap rule); both in `tools/web_editors/`; old mockups deleted |
| [5_PROGRESS_REPORT.md](5_PROGRESS_REPORT.md) | 2026-10-03 evening, 2026-10-05 | editor coordinate hints; anim editor per-node playback, solo, clip switching; History + auto backups; glTF 2.0 loader, renderer, viewer, shadows, cameras; the 3D title; web build ready to publish |
| [4_PROGRESS_REPORT.md](4_PROGRESS_REPORT.md) | 2026-10-03 | atlas tool; `.anim` runtime + editor + recipes; `.particle` format, runtime, editor; GPU sprite paths and GPU particle simulation; the editors on the game renderer; merged test runner |
| [2_PROGRESS_REPORT.md](2_PROGRESS_REPORT.md) | 2026-09-26 | web build done on Windows (host shaderc, `bimg_encode` excluded, one entry point for desktop + web, IDBFS saves); five bugs fixed; doc 06; `web_smoke_test.mjs` |

Short dated bullets: [WORKING_LOG.md](WORKING_LOG.md).

---

## Open Questions (need the owner)

| # | Question | Default if no answer |
|---|---|---|
| Q1 | ~~Commit the 2026-09-27 move?~~ Answered: committed (`ea386d6` and later) | — |
| Q2 | ~~Keep the folder name `toms_next`?~~ Answered 2026-09-27: no folder, the project is the root | — |
| Q3 | Is browser **WebGPU** definitely not needed? (bgfx only does WebGL2 in the browser) | WebGL2 only, as decided 2026-09-25 |
| Q4 | ~~Game UI library for phase 3?~~ Answered 2026-09-27: RmlUi, every screen, desktop and web ([08](../08_RMLUI.md)) | — |
| Q6 | A generated floor (`F??.stage.json`) edited in the stage editor: should it become hand-authored, or should the generator keep hand edits? ([19](../19_STAGE_EDITOR.md) §3) | do not re-run the generator on edited floors |
| Q7 | Locked stairs: invisible until unlocked, or visible but closed with a hint? ([05](../event_editor/05_FLOOR_LINKS.md) §6) | invisible |
| Q8 | Do finished events reset on rebirth (`run` or `meta` scope)? Can a stair wait for several events or a flag? | reset on rebirth; one event per lock |
| Q9 | The game ignores `@` (player start) in stage files (`parseStage`). Fix it (one line)? | not changed; arrival stays stairs-down → stairs-up |
| Q10 | Event lists: keep each floor's explicit list? Are `_v2`…`_v5` ids variants of one event? Which languages are required before release? ([02](../event_editor/02_EDITOR_DESIGN.md) §6) | explicit lists; separate events; untranslated stays a warning |
| Q5 | The bgfx web build now passes its check (2026-09-26). Replace the old `build_web.sh` output (`web/`, `web-gl/`) with it, and where is it published? | keep both until the owner has played the new one (W9) |
