# Working Log — TOMS on Qt6 + bgfx

Track what was done, by date. Details: the numbered `*_PROGRESS_REPORT.md` parts. Entries before
2026-09-27 say `toms_next/`: the project's folder then, now the repository root.

- **2026-09-25** — **Decision:** game runtime on bgfx, editor on Qt6, shipped builds contain bgfx
  only (no Qt). Recorded as decided in `docs/EngineBlueprint/` (README, 01, 02, 04–10); Diligent
  demoted, the in-game web editor replaced by the Qt editor, the old Qt stage editor kept.
  `11_BGFX_QT_ARCHITECTURE.md` still to write.
- **2026-09-25** — **Research:** bgfx has no glTF loader (`geometryc` keeps geometry only);
  skinning / morph / instancing patterns on bgfx; no reliable drop-in library, so the plan is
  fastgltf + ozz-animation CPU `SkinningJob` first.
- **2026-09-25** — **toms_next created** (new folder; nothing in `TOMS/src` edited): `BgfxRenderer`
  behind the existing `IRenderer` via a compat `renderer.h`, SDL3 `toms_game.exe`, Qt6
  `toms_editor.exe` with a `BgfxViewport` and the old stage editor as a tab, embedded shaders,
  prerequisite checks with popups + download links, VS presets, `build.cmd`, docs 01–05.
- **2026-09-25** — **Build fixes:** bx needs C++20 (isolated to `toms_bgfx`), the new bgfx
  `SwapChain` init/reset API, `SDL_MAIN_HANDLED`, `QT_NO_EMIT` vs `Logger::emit`, glm fetched
  instead of `GLM_DIR`.
- **2026-09-25** — **Verified:** game on D3D11/D3D12/Vulkan/OpenGL (title + stage 1 screenshots),
  editor on D3D11 at HiDPI, msjh font fallback, no leaks, no Qt DLLs in `toms_game.exe`, shipping
  preset builds without Qt. Not yet: F5 in the IDE, clicking the popups, a fresh machine.
- **2026-09-25** — **Next:** web build in toms_next (bgfx WebGL2 + SDL3 on Emscripten).
- **2026-09-26** — **Web build done (Windows):** `tools\build_web.cmd [release|debug]` +
  `tools\serve_web.cmd`; guide `docs/06_BUILD_WEB.md`. The web build uses the desktop build's
  `shaderc.exe` (1083 → 348 build steps) and excludes `bimg_encode` (the etcpak blocker). One entry
  point (`main_sdl.cpp`) for desktop and web; IDBFS saves, phone scaling, hold-to-repeat taps,
  `game/web/shell.html`. Release `.wasm` 2.8 MB.
- **2026-09-26** — **Fixed:** LF-only `.cmd` files (cmd.exe `call` bug; now CRLF + `.gitattributes`),
  an `EM_ASM` regex that lost its backslash, `bgfx::renderFrame()` asserting on single-threaded
  (web debug) bgfx, `ccall` before runtime init aborting debug builds (`window.tomsReady`), the 33 MB
  RelWithDebInfo web "release".
- **2026-09-26** — **Verified** in headless Chrome, release and debug: title → new game → save →
  reload restores the save; phone-size viewport runs. `tools/web_smoke_test.mjs` kept as the test.
  Desktop build + smoke test still pass. Not yet: a real phone, Firefox/Safari, WSL web presets.
- **2026-09-26** — **Next:** Phase 2 step 2, the old unit tests + golden-image smoke tests in CTest.
- **2026-09-26** — **Fixed the black web page** the owner saw: at fractional display scaling (240%)
  SDL's fill-document probe failed and the canvas stayed 1×1. `shell.html` now sizes the canvas
  with CSS, fill-document is gone, and the game resets bgfx whenever the drawable size changes.
  Verified in a visible Chrome window at DPR 2.4 plus the headless and desktop smoke tests.
- **2026-09-27** — **Project moved to the repository root** (`git mv`; no `toms_next/` folder). The old
  project's game code and content restored from `979bcf3^` into `legacy/` (`TOMS_LEGACY_ROOT`). Fresh builds
  from the root: desktop game + editor and web release all pass their smoke tests. `wqy-zenhei.ttc` is
  lost (never committed); desktop uses the JhengHei fallback.
- **2026-09-27** — **Layout:** `assets/` = all content (`media/`, `data/`), `src/` = all code (`engine`, `game`,
  `editor` + `editor/stage`, `legacy`, `third_party`). Fixed a `.gitignore` rule (`save/`) that hid 7 save-system
  source files. Desktop, editor and web rebuilt and smoke-tested.
- **2026-09-27** — **Renamed `src/legacy` → `src/core`** (target `toms_core`, `TOMS_CORE_SRC`): it is the game's
  own code and stays. Desktop, editor and web rebuilt and smoke-tested.
- **2026-09-27** — **Release scripts:** `build_windows.bat` → `dist/TOMS-windows/` (exe + `assets/` + MSVC runtime DLLs;
  no console window; finds `assets/media` next to itself) and `build_web.bat` → `dist/TOMS-web/` (`index.html` + js/wasm/data +
  `.htaccess`/`web.config`), both via `tools/package.ps1`. Verified from copies outside the repo: the exe runs
  and uses its own assets; the web folder served by a static server passes the smoke test.
- **2026-09-27** — **GitHub Pages:** `publish_web.bat` + `tools/publish_pages.ps1` (temporary gh-pages worktree,
  keeps the previous version, asks before pushing; `dryrun` / `nobuild`); web package files
  version-stamped (`toms_game.<commit>-<time>.*`); doc `docs/07_PUBLISH_GITHUB_PAGES.md`. Dry run
  against the real gh-pages OK; not pushed.
- **2026-09-27** — **RmlUi store (test):** RmlUi 6.3 + FreeType on bgfx (`src/engine/src/rml_ui.*`, view 2) and the
  store rewritten as `assets/media/ui/store.rml`/`.rcss` + a data model (`src/game/src/store_screen.*`);
  `Game` keeps the rules (one small external-UI API in `game.h`). F4 old/new store, F5 reload UI, F8 RmlUi
  debugger; `--clicks=x:y@f` for scripted mouse tests. Verified on D3D11/D3D12/Vulkan/OpenGL: open, arrow
  select, buy → toast, tab click, hover select, close; web build still passes (RmlUi off there, old store).
  Doc `docs/08_RMLUI.md`.
- **2026-09-27** — **Removed the dead old-renderer code** (19 files, ~3,550 lines): the Vulkan/WebGL/WebGPU renderers,
  `texture.*`, `vk_util.h`, `batch_renderer.h`, both old ImGui layers, the old GLFW and Emscripten entry points,
  `texture_test.cpp`, and the `compat/vk_util.h` / `compat/renderer_webgl.h` shims. The core code now includes one
  `renderer.h` (= `BgfxRenderer`) and does `new Renderer()` on every platform; comments and docs 01/04/06 updated.
  Desktop game + editor and web release rebuilt; game, RmlUi store, editor and the web smoke test pass.
- **2026-09-27** — **Click-to-move:** a click/tap on a map tile walks the player there (BFS shortest path over plain floor;
  the clicked tile itself may be a monster/door/item/NPC/stairs and the last step goes into it like an arrow step),
  one step per 110 ms, gold marker on the destination; keys/d-pad, a new click, any modal or a stage change cancel it.
  Also fixed: a hidden virtual pad still caught taps where its buttons had been. Verified with scripted clicks:
  short walk, long walk around walls into a slime fight.
- **2026-09-27** — **All UI on RmlUi** (owner: "make all UI with RmlUi"; decisions: ship a cut-down font, keep the F1/F2
  ImGui dev tools, delete the old UI). 12 documents in `assets/media/ui` (title, HUD, pad, battle, dialogue, inventory,
  store, menu, dialogs, stage select, ending, overlay) bound to one data model: `Game::buildUiState()` fills
  `toms::UiState` (`src/core/game/ui/ui_state.h`), buttons call `Game::uiEvent()` (`game_ui.cpp`), `GameUi` binds/shows
  (`src/game/src/game_ui.*`). Font: Noto Sans CJK TC cut to the game's 2152 characters (793 KB,
  `tools/make_ui_font.py`). Removed: every hand-drawn UI function and hit-test rect, `font.cpp`/`font.h`,
  `game_text_draw.cpp`, `stb_truetype.h`, `ui_root`/`ui_layout`/`dialogue_layout` (+ tests), the renderer's text path,
  the JP/KR font subsets, the wqy-zenhei/msjh font prerequisite, the F4 old-store toggle. RmlUi now also runs on the
  web build (wasm 5.0 MB, data 1.6 MB). Verified: desktop screenshots of every screen reachable by keys/clicks (title
  pages + confirm, HUD, pad show/hide, battle incl. attack + cooldown, dialogue, inventory, store, menu + settings +
  village, stage select, save toast), editor, web smoke test. Not verified: the ending screen and the stairs dialog
  (not reachable by scripted input), a real phone.
- **2026-09-27** — **Web text from the browser; per-language fonts.** New RmlUi font engine for the web build
  (`src/engine/src/rml_canvas_font.*`): measureText() for widths, each distinct string drawn whole with fillText() into
  a cached texture at the screen's pixel density, so the browser does shaping/bidi/fallback for every script. The web
  build has no FreeType and no font file any more: download about 2.6 MB -> 1.7 MB gzip (.data 0.84 -> 0.18 MB,
  .wasm 1.72 -> 1.45 MB). text.json languages gained `web_font` (CSS font list; set for all 6 languages) and `font`
  (desktop font file, used when it exists; the bundled font stays the fallback). Verified: web smoke test + the canvas
  resolves the zh_TW list to Microsoft JhengHei in Chrome; desktop switch to 日本語 with a test `font` entry loads
  Yu Gothic, without it the default. **Also fixed:** on Windows every save and settings write after the first one failed
  (`std::rename` does not overwrite there) -- now `std::filesystem::rename`; saves and the language setting persist.
- **2026-09-30** — **Phase 2 step 2 done: tests in CTest** (`tests/CMakeLists.txt`, doc 09, `tools\test.cmd`, test
  presets; Visual Studio Test Explorer). 25 unit tests (the core's `*_test.cpp`, linked against `toms_core`, run in
  `assets/`), 10 screenshot smoke tests (`toms_game --fixed-dt=16` + scripted keys/clicks + `tests/tools/image_diff`
  against `tests/golden/*.png`: title, map, menu, inventory, dialogue, battle, store on D3D11, the map on D3D12/Vulkan/
  OpenGL), and the web smoke test (`web_smoke_test.mjs` now serves a folder itself, asserts, exits 77 = skipped). New
  `--fixed-dt` makes a run deterministic and ignores real input. Result: 36/36 pass in ~45 s; D3D11/D3D12/Vulkan match
  the references exactly, OpenGL 0.022%; a deliberately changed HP bar colour fails with a diff image. Shipping preset
  builds no tests. Also: board refreshed (W6/W7/W8/Q1 closed, Android track added, two docs numbered 07 noted).
- **2026-09-30** — **Threads: job system + multithreaded web build** (doc 10). `toms::JobSystem` (`src/core/engine`):
  `parallelFor` over hardware-1 workers (8 here), inline with zero workers; used for the sprite PNG decode; unit
  test (fixed two bugs it found: a nested call re-locking with zero workers, and the pool's threads aborting the
  process at exit). Web: a second build `web-release-mt-windows` (`-pthread`, 5 pre-started workers, 4 for jobs);
  `build_web.bat` builds both; the packaged `index.html` loads the threaded one when `crossOriginIsolated`, else the
  single-threaded one; `coi-serviceworker.js` (v0.1.7, MIT) adds the headers on GitHub Pages; `?nothreads` opts out.
  `serve_web.cmd mt|dist` + `tools/serve_web.py` (sends COOP/COEP). Verified in headless Chrome: threaded build with
  headers -> 4 workers; the package on a server without headers -> threaded after the service worker's reload; with
  `?nothreads` -> 0 workers; the threaded build without isolation does not start (why the fallback exists). 40/40
  tests pass (`tools\test.cmd`). Not tried: a real phone, Safari, the live GitHub Pages site (not published).
- **2026-09-30** — **Android: first build, runs on the emulator** (doc 07 appendix). `android/` Gradle project from
  SDL3's template (`TomsActivity` loads `libmain.so`; the repo's `assets/` are the APK's assets); `toms_game` is a shared
  `libmain.so` on Android; preset `android-x86_64-debug` for the emulator; `tools\build_android.cmd` / `run_android.cmd`.
  Fixes the first run needed: the host shaderc for Android (bgfx tried to build an Android shaderc); no `100_es` profile
  in this shaderc (Android: `300_es` + `spirv`) and no D3D/GL headers in `embedded_shaders.cpp`; link `EGL`; the
  ANativeWindow for bgfx; the `getAssets` JNI signature (`android/content/res/AssetManager`); `vfsListDir` + `..`
  resolution, and every remaining `std::filesystem` / `stbi_load` / `ifstream` read moved to vfs; stderr piped to
  logcat; quick taps/keys latched for a frame and a touch hovers one frame before it presses; resume after background
  (bgfx's `GlContext::resize` reused the old window: patched at configure time in `TomsDependencies.cmake`, plus
  `bgfxHostSetWindow`); Back = Esc. Verified on the Pixel Tablet API 35 emulator: title, new game, pad + tap-to-walk,
  battle, save survives force-stop, load, 3x background/foreground, Back. arm64 compiles. Desktop 36/36 and web 4/4
  tests still pass. Not tried: a real phone.
- **2026-10-03** — **Atlas tool** (doc 14, commit `c8b9b38`).
  - `tools/atlas`: a `.atlasproj` project, a packer, `.atlas`/`.plist`/XML export.
  - `atlas_editor` (Qt) and a web build of the same core; a command line and tests.
  - The game loads several atlases.
- **2026-10-03** — **Node animations** (docs 15–16, commit `904cdaa`).
  - `.anim` + `AnimPlayer`. A `PoseCache` updates only animated channels; keys hold only their own
    channels.
  - `anim_editor`: Add Atlas takes several files; dropping several sprites on the timeline inserts
    sprite keys with a time gap.
  - Doc 16 is a recipe book for hand- or AI-written clips (fade, move, the battle attack example).
- **2026-10-03** — **Particles** (doc 17, commit `b8cf970` and later).
  - A `.particle` JSON format with every src/dst blend factor; the `toms::fx` runtime: deterministic
    1/60 s steps, a seeded RNG, seek.
  - `particle_editor` with presets and headless check/render; 11 example effects; an `fx` atlas.
  - `toms_game --fx=` preview and an F3 HUD.
- **2026-10-03** — **GPU** (not committed).
  - The sprite batch has instancing, compute and CPU paths on persistent buffers.
  - GPU particle simulation in compute shaders, with no readback: 300 000 particles at 78–85 FPS
    against 33–35 on the CPU.
  - Game setting `particleGpuThreshold` (5000 desktop / 3000 phone) and a per-emitter
    Auto/CPU/GPU flag in the editor; the GPU is used only where compute is supported.
- **2026-10-03** — **Editors on the game renderer** (not committed).
  - `GameCanvasView`: the atlas, anim and particle views draw with toms_game's `BgfxRenderer`, with
    a QPainter overlay and a QPainter fallback.
  - Right-drag pans every editor view.
  - Each editor has `--selftest-gpu`, and all three pass on D3D11.
  - The unit tests are merged into `toms_tests`; `launch.vs.json` was trimmed.
  - CTest: 46/50; the 4 `smoke.stage*` failures already failed before (W16).
- **2026-10-03** — **Editor coordinate hints** (evening, `0115a6a`): x/y values along the canvas edges, with
  a step that follows the zoom (1/2/5 × 10ⁿ); View > Show Coordinates.
- **2026-10-05** — **Anim editor** (`fedbd88`).
  - **Per-node playback:** once and stay / once then hide / loop (`loop`, `stayAtLastFrame`).
    Looping nodes keep playing after the clip ends.
  - **Clips dock:** double-click opens a clip and asks before discarding changes.
  - **Nodes tree:** Playback and Solo columns.
  - Test file `docs/examples/anim_child_timing.anim`.
- **2026-10-05** — **Editors: History dock and automatic backups.**
  - **Backups:** every 5 min if changed and every 20 edits, keeping 20, in `%LOCALAPPDATA%`.
  - **Atlas backups:** a project + packed atlas folder.
- **2026-10-05** — **glTF 2.0** (`306fb0f`, doc 18).
  - **Loader:** cgltf; skins, morphs, animations, cameras, lights, instancing, PBR extensions.
  - **Renderer:** PBR, GPU skinning, CPU morph, instanced draws, glass, up to 4 lights, a shadow
    atlas (directional / spot 1 tile, point 6).
  - **Viewer:** `gltf_viewer` (donmccurdy-style panel, keyboard camera, file cameras with
    look-around while riding).
  - **Bug:** D3D lit every model from below (`gl_FrontFacing`); fixed.
  - **Tests:** 52 unit checks, 17 `smoke.gltf_*` (incl. Vulkan / OpenGL); 12 Khronos samples render.
- **2026-10-05** — **3D title** (`8f26862`).
  - `VirtualCity.glb` behind the title menu (`TitleScene`, `BgfxRenderer::setSceneViews`).
  - Same on D3D11 / Vulkan / OpenGL, and in the web build.
  - `smoke.title` reference updated.
  - **Web package built and tested; publishing waits for the owner's GitHub sign-in (W18).**

- **2026-10-05** — **Event editor: design and tutorial** (`6f24ba6`, part 6).
  - **Docs:** `docs/event_editor/` 01–04.
  - **Tutorial:** an interactive tutorial on a copy of the real event data, with Play demo, a console
    and a spotlight, in English and 繁體中文.
  - **Found:** 92 undefined event ids on F22–F70; 4 orphan events.
- **2026-10-07** — **Event logic and the HTML stage editor** (`f7c8b8e`, `cf2a771`).
  - **Event editor:**
    - multi-stair floors with event locks (doc 05);
    - triggers / conditions / actions / `next` and `vars.json` (doc 06);
    - a node-graph editor, a values map, variable categories.
  - **Stage editor:**
    - grid painting and event placing;
    - live save;
    - story check (exact key simulation) and safe auto-place;
    - the overlap rule, wheel zoom, right-drag pan, UI size (doc 19).
- **2026-10-08** — **Editors folder, kinds, cleanup** (not committed).
  - **Folder:** both editors in `tools/web_editors/`, switched by **⇄** in `index.html`.
  - **Tutorial:** shows only on the first visit, then opens from 📘 Tutorial.
  - **Top menu:** the fake one is removed.
  - **Kinds tab:** `assets/data/events/kinds.json` (add, rename, colour, names; move the ticked events
    to another kind; delete when unused).
  - **Mockups:** `docs/event_editor/mockups/` deleted.
  - **Verified:** Play demo 29/29 in both languages.
