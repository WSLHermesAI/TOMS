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
  - **Event editor saves:** *Open project folder* + *Write to project* write the changed pools, floors,
    strings, vars, kinds, flags and counters into `assets/data`, merged with what is on disk, with a backup
    of each file in `Build/editor_backups/`; the stage editor backs up and merges too, and opening the
    folder in one editor opens it in both. `serve.cmd` refreshes the data on every start; `data_snapshot/` removed.
- **2026-10-08** — **Monster idle animations** (not committed).
  - `assets/media/anim/monster_idle.anim` (anim_editor format): one looping `idle_<enemy id>` clip per enemy, sillier
    with level (slime breathes … Vorkath laughs with dizzy stars); fx sprites for dust, glow, sparks, stars.
  - The game draws every map monster with its clip (`Game::drawIdleAnim`), stretched over its footprint, each at its own
    phase; static sprite as fallback. CTest `anim.check_monster_idle`; doc 15.
  - Tests: 69/74; `smoke.stage*` (W16, the pad is missing from the screenshot; the slime now moves too) and
    `web.page_coi` fail, as before this change.
- **2026-10-08** — **Battle settings file** `assets/data/battle.json` (not committed): `bar_slow_speed_scale` (the bars'
  slow start, 1), `bar_fast_speed_scale` (the top speed they ramp up to, 10) and `bar_cooldown_ms` (the wait after a tap,
  1000; was 1500). Read at start-up; without the file the old values apply. `smoke.battle` depends on these values.
- **2026-10-08** — **Fixed: keys and items on the generated floors were invisible, and keys could not be picked up**
  (not committed). The floors write keys as `key:yellow`, which the game neither drew nor collected (it only knew
  `item:key_yellow`), so every floor's door could become a dead end (reported on F03); and `entSprite()` drew every item
  as floor. `parseStage` now loads `key:<colour>` as the item `key_<colour>`, and items draw with their own sprites.
  `footprint_test` checks every key/item on all 71 stage files loads as something the game picks up (3427 checks).
  References refreshed for menu/anim/fx (F01's items now visible); `smoke.stage*` (W16) and `smoke.battle` (battle.json)
  still fail.
- **2026-10-08** — **Doors open properly** (not committed). Walking into a door with its key used the key but left the door,
  so each later step onto it cost another key. Now `Game::openDoor` uses the key once, plays `door_open_<colour>` from the new
  `assets/media/anim/door_open.anim` (key turns, door sinks into the floor with a flash and sparkles), clears the tile and
  records it as opened (`EntityStatus::Opened`, kept on revisits); the player stays put that turn. Doors and stairs no longer
  get a floor-coloured square drawn over them. CTest `anim.check_door_open`; menu/anim/fx references refreshed.
- **2026-10-08** — **Editors and docs, later the same day** (not committed; part 6 §7–11).
  - **Event editor:** saves into `assets/data` (*Open project folder*, merged with disk, backups in
    `Build/editor_backups/`); names in any characters (Chinese too).
  - **Variables tab:** create / rename / delete categories (with a dialog), multi-select to change
    category.
  - **Stage editor:** many changed floors download as one zip; more than 3 files lists them only;
    the Story panel marks the changed floors.
  - **Data and docs:** `serve.cmd` refreshes the data on every start (`data_snapshot/` removed);
    Traditional Chinese versions of every doc in `docs/zh_TW/`, English versions of the 5
    Chinese-only docs.
- **2026-10-08** — **Fixed: the dialogue box ran off both screen edges on small web windows** (not committed). Below
  900x560 css px the web build scales the UI x1.5, and the full-width dialogue box (scaled about its bottom centre) became
  1.5 screens wide. It is now narrowed by the same factor first (`UiDialogue::side`), so it keeps its width on screen.
  New desktop option `--ui-scale=<x>` to test the small-screen layout; `smoke.dialogue` unchanged.
- **2026-10-10** — **Planned the player menu** (docs only, not built): [../20_PLAYER_MENU.md](../20_PLAYER_MENU.md) (Status,
  Items with list + detail + price, Events with next steps; phases P1–P3; tests), the event log rules in
  [06_EVENT_LOGIC.md](../event_editor/06_EVENT_LOGIC.md) §7, the `log` field in 01, a pointer in 08; zh_TW versions.
  Open questions Q11–Q14.
- **2026-10-10** — **Player menu plan revised** per the owner: a fourth tab **Equipment** (change weapon / armor with a
  stat and battle-bar comparison); Items as a scrollable 3-per-row grid with Use / Equip / Drop and an `important` flag
  that blocks dropping; Events laid out like Items, listing only started and finished entries, with an Unfinished / All
  switch and a detail panel. Q13 answered, Q15 added. Docs: 20, 06 §7, 08 (+ zh_TW).
- **2026-10-10** — **Player menu: one button.** Per the owner, every button (HUD store, the clickable item line, the web
  page's fullscreen and backpack buttons, the old ≡ list) moves into the player menu; a fifth tab **System** holds
  them, and the walking screen keeps only ≡ with a news dot. Fullscreen from inside the game uses a press-then-release
  hand-off to the page, since browsers only allow it in a gesture. Docs: 20, 08, 06_BUILD_WEB (+ zh_TW).
- **2026-10-10** — **Player menu: the owner's answers written in.** Q11: attributes STR, DEX, AGI, VIT, INT, LUK from a
  new settings file `stats.json` (with the level-up numbers moved out of code); they do nothing until later gameplay.
  Q12: one all-item file `items.json` (id, type, stats, price, important, text keys; `equipment.json` merges in, the
  store lists ids). Q14: every event, mission and chapter has a title and description; the Events tab shows only those
  and the connected events' names (？？？ when unmet). Q15 explained, still open. Docs: 20, 06 §7, 01 (+ zh_TW).
- **2026-10-10** — **Q15 answered:** the Equipment tab only changes worn gear for now; upgrading gear is parked as W26
  (later). All player-menu questions are answered; P1 can start. Docs: 20 (+ zh_TW), progress report.
- **2026-10-10** — **Player menu P1 built** (not committed). One ≡ button on the walking screen; five tabs (Status with
  STR/DEX/AGI/VIT/INT/LUK, Equipment with a comparison, Items as a 3-per-row grid with Use/Equip/Drop and undroppable
  important items, Events with chapters and missions, System with the old ≡ list + Store + Fullscreen). Data: the
  all-item `items.json` (`equipment.json` merged), `store.json` by item id, new `stats.json`, chapter / mission titles;
  saves keep attributes and gear. Store button, item-line click and the web page's buttons removed. New
  `player_menu_test`, `smoke.player_*` (5), `--give=` test hook; UI font rebuilt. 39/39 unit, 57/62 smoke (W16, W23 as
  before); web checked in headless Chrome incl. Fullscreen. Docs: 20 §9, 06_BUILD_WEB, 08, 09, 13 (+ zh_TW).
- **2026-10-10** — **Save / Load screen** (not committed): the System tab's Save row opens `saveload.rml` (Save and Load
  tabs, one card per slot); every save and load asks first (Cancel preselected for overwrite and load); saving into a slot
  makes it the run's slot. Checked on desktop and web; `smoke.player_system` reference refreshed; 57/62 tests (W16, W23 as
  before). Docs: 20 §10 (+ zh_TW).
- **2026-10-10** — **Player menu P2** (not committed): story events in the Events tab with title, description and
  connected events (？？？ until met); run state tracks every event as started / finished (saved); every fired event sets
  `event_<id>`; `requires` not met = started; `next` starts the linked events. Drafted titles / descriptions for the 34
  pool events; event editor: Title / Description fields, in-log switches (event, kind), "No title / description" warning.
  Toasts hidden while the menu is open. Tests: player_menu_test extended, 4 references refreshed; 57/62 (W16, W23).
- **2026-10-10** — **Player menu P3** (not committed): attribute effects from `stats.json`, per point above base (STR ATK,
  DEX hit zone, AGI bar wait, VIT max HP, INT skill power, LUK battle gold); Status tab shows per point / now; HUD shows
  total ATK / DEF; optional hand-placed points (`levelUp.freePoints`, saved). `--give=exp:/points:` test hooks;
  player_menu_test checks effect stats; `smoke.player_status` refreshed; 57/62 (W16, W23). Docs: 20 §12 (+ zh_TW).
- **2026-10-12** — **Crowds: instanced skinning** (not committed). A skin's clips baked into one RGBA32F texture
  (rotation quaternion + translation/scale per joint, one row per frame) shared by every copy; `gltf::Crowd` gives each
  copy its own clip and time, cross-fades and held two-clip blends; `GltfRenderer::drawCrowd` draws them all in one
  instanced draw per primitive (`vs_mesh_animtex.sc`, shadows included); `.animtex` save / load with a stale check;
  `gltf_viewer --crowd=<n>`. One copy = the `Pose` path pixel for pixel; 4 desktop backends agree. New
  `gltf_anim_texture_test` (35 checks), 4 new glTF screenshot tests. Web builds; `web.page_coi` fails (W30, older package).
  Docs: 18 §6, 09 (+ zh_TW).
- **2026-10-12** — **W16 and W23 closed; Q7, Q9, Q10 answered.** `smoke.stage` (4 backends): the reference still had the
  on-screen pad (removed on purpose in `3df014b`) -- refreshed. `smoke.battle`: new option `--battle-tuning=default`
  (built-in bar values), so `battle.json` no longer breaks it -- refreshed. Desktop tests 79 / 79. Owner's answers: Q7
  locked stairs visible with a lock icon, an unlock dialog naming the floor, and a "go to the stairs" follow-up event;
  Q8 later; Q9 becomes a proposal: stage maps as images (one integer id per pixel); Q10 a per-floor event index, English
  only for now (text still through keys). Q6 explained, waiting.
- **2026-10-12** — **Q6, Q10 (a), English, Q7, the event index, Q9 design.**
  - Generator restored with a hand-edit guard: dialog, `--yes` / `--keep-edits` / `--dry-run`, manifest.
  - 92 new events (pools act04–act10 + common variants), so W22 is closed.
  - English is the default (settings v2), and 197 Chinese "English" strings were translated.
  - Locked stairs (`stair_links.h`):
    - padlock, hint, unlock dialog;
    - a "Go to the stairs on floor N" event, plus the arrival rule.
  - The `@` start is fixed.
  - `floor_index.json` and its test; [21_IMAGE_STAGES.md](../21_IMAGE_STAGES.md).
  - The event-editor tutorial is on frozen data. ● / ◆ were added to the font.
  - Desktop 83 / 83.
- **2026-10-12** — **Changed-only testing (project rule).** `tools\test.cmd` now runs only the tests the uncommitted
  changes can break (`tools/test_select.py`: file → test rules, `--since`, `--list`); `tools\test.cmd all` runs every
  test. The rule is in `CLAUDE.md` (must follow); docs 09 (+ zh_TW). Checked: 83 of 88 tests selected for today's diff, all pass.
- **2026-10-12** — **Testing by area.** `test_select.py` rewritten: game / glTF / particles / anims / atlas / engine core /
  renderer / web each run only their own tests (a game change runs no glTF test and the reverse); `tests/CMakeLists.txt`
  runs only the tests on its changed lines; unmapped files run nothing and are listed. Docs 09 (+ zh), CLAUDE.md.
- **2026-10-12** — **Generator removed; the index moved into the editors.** The owner edits floors only in the web stage
  editor, so `gen_floors.py`, `gen_stairs.py`, `maze_lib.py`, `floor_guard.py`, the manifest and `make_event_index.py`
  are deleted (git history keeps them). `tools/web_editors/floor_index.js` builds the same index (byte-identical output,
  checked with Node) and both editors write it on opening the project folder and after each Save. W33 closed.
- **2026-10-12** — **glTF physics (KHR_physics_rigid_bodies) on Jolt.**
  - Jolt Physics v5.6.0 via FetchContent, built deterministic, SSE4.2 only.
  - `Model::physics` reader; `gltf::PhysicsWorld` in the new `toms_physics` library.
  - Viewer: Physics panel and collider boxes.
  - Test scene in `make_test_models.py`; `gltf_physics_test` (35 checks); `smoke.gltf_physics`.
  - Docs: 18 §7 and 09 (+ zh). Changed-only run: 85 of 90 tests, all pass.
  - The author's 7 samples load and simulate.
- **2026-10-14** — **gltf_viewer: node tree and editing.**
  - Scene tree with tags; click-to-pick in the view (a second click picks the node behind).
  - Node inspector: translation, rotation, scale, uniform scale, reset.
  - Handles in the view: move (T), rotate (E), scale (S).
  - Edits apply after the animations and restart the physics.
  - Test options `--select`, `--tool`, `--pick`, `--drag`; checked by pick and drag runs.
  - Docs: 18 §4 (+ zh). Changed-only run: 85 of 90 tests, all pass.
- **2026-10-14** — **Atlas tool: import `.plist`.**
  - `atlas::importPlist` (Cocos / TexturePacker formats 0–3) reads:
    - rotated frames, turned back upright;
    - `sourceColorRect` / offset trims;
    - aliases;
    - frame names, with `.png` dropped;
    - our `x_` fields.
  - Wired into `importAtlasFile`, so the Qt editor, the web editor (wasm rebuilt) and `atlaspack import` all take it.
  - Tests: our own `.plist` round trip, plus a hand-made Cocos format-2 sheet (rotated, trimmed, aliased).
    `atlas_core_test`: 25725 checks, all pass.
  - Real TexturePacker sheets (13 and 19 sprites) import correctly; a particle plist is refused with a clear message.
  - Changed-only run: 15 of 90 tests, all pass.
- **2026-10-14** — **Anim editor: Convert → MPDI Folder to .anim.**
  - `tools/anim/core/mpdi_convert.*`: `.mpdi` (all formats: old lists, the `<Version>` header, 1.0–1.3) → `.anim`.
    Each `.pi` → `.atlasproj` + `.atlas` + `.png` + `.plist`, once.
  - It follows the engine's own rules: centre vs top-left positions, Chaikin LOD, trimmed scale, mirroring, start
    gates, additive blending, anchors and rotation offsets, hint events.
  - Also `anim_editor --headless convert-mpdi`.
  - Old engine data: 47 of 48 converted (312 clips, 3,220 paths, 76 atlases); 44 pass `--headless check`, the other
    3 have images missing in their old `.pi`.
  - New test `anim.mpdi_convert` (23 checks) on hand-made files in `tests/mpdi/`. Changed-only run: 22 of 91 tests,
    all pass.
  - Found: `anim_editor --selftest` crashes at start-up here with or without this change (an existing problem).
- **2026-10-14** — **Anim and atlas editors: the mouse position on the canvas.**
  - `CanvasView` (studio_common): with View > Show Coordinates on, "x, y" next to the cursor, guide lines through
    it and its x / y tagged on the bottom and left edges. The top strip is kept free of y values (the clip label
    overlapped them).
  - Double-clicking a clip plays it (Clips dock).
  - New test `studio.canvas_coordinates` (offscreen, 6 checks). Its target copies Qt's `qoffscreen.dll` into
    `bin/platforms` (windeployqt copies only `qwindows`).
  - That missing plugin was why `anim_editor --selftest` crashed (0xC0000409).
  - Changed-only run: 24 of 92 tests, all pass.
- **2026-10-14** — **Anim editor: View > Show Grid removed** (the 16 px grid, its G shortcut and saved setting); View > Show Coordinates covers it. The origin cross stays. Changed-only run: 24 of 92 tests, all pass.
- **2026-10-14** — **Anim: position, rotation and scale are x, y, z; editing no longer stalls.**
  - **Runtime** (`anim_clip.*`): `Node::pos` / `rot` / `scale` and their keys are `vec3`, and `NodePose::world`
    is a 4×4 matrix, ready for a 3D camera.
    - Rotation order is Rx · Ry · Rz (z first), the old engine's `cMatrix44::RotationMatrix`.
    - The game and editor draw through `flatten()` (orthographic): z does not move a sprite, an x / y tilt
      foreshortens it. Z-only clips draw exactly as before (the screenshots match).
  - **Files:** version 2 writes `[x, y, z]`. Version 1 (`[x, y]`, a single rot number = z) still loads; a
    newer version is refused.
  - **Editor:**
    - Properties shows Pos / Rot / Scale as x, y, z.
    - Key list cells read and write "x, y, z" (one rot number = z, one scale number = uniform).
    - The rotate handle turns z.
  - **MPDI:** `Angles` x / y are now converted (they were dropped with a warning).
  - **The freeze:** every edit serialized the whole file to JSON about six times to compare undo states. It was
    not specific to rotation: on a 1.3 MB converted file each spin-box step took ~200 ms, so a held arrow
    queued up. The comparison is now `operator==` on the data: ~25 ms per step on that file.
  - **Tests:**
    - `anim_clip_test` gains `testXyz` (165 checks): the rotation order against the old engine's matrix,
      tilts, z depth, version 1 reading, ==.
    - `mpdi_convert` 24 checks (Angles).
    - New ctest `anim.editor_selftest` (offscreen, 122 checks, 0.8 s). Its "24 failures" earlier came from
      giving it the wrong file; its fixture is `tests/smoke/anim_preview.anim`.
- **2026-10-14** — **Font tool** (`tools/font`, docs 22 / 23, not committed):
  - `fontpack` and `font_editor`: picture fonts (BMFont text `.fnt` + one `.png` page) from one picture per
    character. The editor adds pictures and takes each one's character (a name such as `dot` or `U+00D7` works too),
    shows a test text drawn by the game's own layout, and the combined picture with the selected glyph highlighted.
  - Page layouts: one row (the old convert2fnt-web layout; rebuilds `connect_score.fnt` byte for byte), tight and
    tight power of 2 (`atlas::packPages`). Space width defaults to the `1` picture (else 5), line height to the
    tallest picture; both can be set.
  - Runtime: `BitmapFont` / `FontText` in `src/core/engine/bitmap_font.*`, now in `toms_core`.
  - Tests: `font.tool`, `font.editor_selftest`; fixtures in `tests/font` (the old 10085 fonts moved there).
- **2026-10-14** — **Editors in English / 中文** (`tools/studio_common/Language.*`, docs 24):
  - A corner button in every Qt editor's menu bar; the window re-translates in place; QSettings TOMS/studio keeps the
    choice. 1,097 `tr()` texts in `i18n/zh_TW/*.json`, built in as a Qt resource; `i18n_check.py` lists missing ones.
  - Selftests, headless and screenshot runs start in English; `TOMS_LANGUAGE` forces a language without saving it.
  - The toggle was first invisible (a child added to a shown window stays hidden): fixed, and the test checks it.
  - `toms_editor` now builds `studio_common` before testing was enabled, which dropped the studio tests: the root
    CMakeLists enables testing first.
  - Tests: `studio.language`, `studio.i18n_complete`; the atlas selftest switches the real window and back.
- **2026-10-14** — **Atlas editor:** the page view dims each sprite's extruded ring (the copies outside the frame);
  new projects default to extrude 1 (0–16).
- **2026-10-14** — **Docs are local files only** (the owner): never upload project data online; English in `docs/`,
  Traditional Chinese in `docs/zh_TW/` (rule in `CLAUDE.md`). The two documents published earlier were deleted.
- **2026-10-14** — **Event chain "The Silent Bell" and key gates** (docs event_editor/07, not committed):
  - Six events in `pool_act01` across F03 → F05 → F04 → F06 + F02 → F03, linked by `requires` (event done) and
    `next`; the last one needs two. F05 gives `key_bellward` (a `story` item); F04's bell-ringer's room is behind a
    `gate:key_bellward`.
  - Runtime: gates (open only for their item, which they take; stay open; drawn as a door washed in the item's
    `tint`), event `actions` (give / addCounter / setFlag), finished event tiles stay gone after a reload, story
    items cannot be used up.
  - Editors: Gates / Gate keys in the stage editor; the event editor reads the item list from `items.json`;
    `tools/web_editors/sync_floor_index.mjs` rebuilds `floor_index.json` with Node; `toms_game --goto=<floor>`.
  - Played in the game (scripted): the key given on F05, the gate refusing and then opening on F04, the room's event.
  - Tests: `unit.event_chain_test` (36 checks: actions, gates, the chain, F04's room reached only through the gate),
    `smoke.bell_gate`.
- **2026-10-14** — **glTF viewer: lights, cameras, Unicode paths:**
  - With the file's `KHR_lights_punctual` the hemisphere ambient drops 0.7 → 0.1, so the file's lights light the
    scene. Camera buttons (free, each file camera, ◀ / ▶) next to the drop-down.
  - `tests/gltf/lights_cameras.gltf` (point, spot, sun; front / top perspective, side orthographic); tests
    `smoke.gltf_lights`, `smoke.gltf_camera_side`.
  - `Unicode❤♻Test` failed to open: argv was ANSI and the loader opened files with `std::ifstream(string)` and
    cgltf's `fopen`. Now the command line is read wide and every read (incl. cgltf's buffers) goes through
    `std::filesystem::u8path`. `gltf_model_test` checks a non-ASCII `.gltf` + `.bin`.
  - Changed-only run: 96 of 101 tests, all pass. Full suite and web not run.
- **2026-10-16** — **Editor release folder** (committed `090736d`, without its `.gitattributes` LFS rule):
  - `Editor/`: 6 editors + `atlaspack` + `fontpack` with Qt, dxcompiler and the VC145 runtime (33 files, 101.6 MB,
    Git LFS), launchers, README (en, zh_TW), `AI_GUIDE.md` (formats, templates, check commands tried with the packaged
    tools), `BUILD_INFO.md`. `tools/package_editors.py`: build, copy, `--check`.
  - The particle editor finds `assets/media/atlas/fx.atlas` relative to `Editor/bin`.
  - All tests: 101 of 101, desktop and web.
- **2026-10-16** — **NPCs and their events** ([../event_editor/08_NPC_DIALOGUE.md](../event_editor/08_NPC_DIALOGUE.md)):
  - Design and 11 owner decisions, then built: `talk_system.h` (NPC files, chat rotation, topics, menu rule,
    conditional start), `story_actions.h` (one action list; `checkActions` for data checks), conditions
    `varAtLeast` / `varEquals` / `talkCountAtLeast` / `onStage`, run state `vars` / `npcTalk` / `eventCounts`.
  - `Game::talkTo` replaces the NPC → dialogue table in `game_input.cpp`; dialogue choices take `actions`,
    `finish`, `fire`, `id`; the dialogue box shows the speaker's name.
  - Repeatable events (`once: false`, `maxTimes`), `varChanged` events, a title notice for events with no tile / NPC,
    an NPC file's `sprite`.
  - The village elder on F02: seven talk events, 12 dialogues, `story/vars.json`.
  - Tests: `unit.talk_system_test` (154), `smoke.talk_menu`, `smoke.talk_chain`, `smoke.talk_trusted`,
    `smoke.talk_repeat`; `event_chain_test` uses `checkActions`.
- **2026-10-16** — **Web editors for NPCs and dialogues:**
  - Event editor: talk fields (Dialogue, After finishing, Talked to on, Menu order), Repeat; NPCs tab with a talk
    preview (the game's rules); Dialogues tab (node cards); one condition-row editor for events, chat lines, choices and
    start rows (ten kinds, all / any, not, JSON fallback; the tutorial's condition step still works); new fields kept
    by both save paths; NPC and dialogue files saved.
  - Stage editor: an NPC's panel (topics, "only on F07", a jump to the NPCs tab); events / NPCs reloaded live; the
    story check counts dialogue actions.
  - Checked headless in Chrome; no automated tests.
- **2026-10-16** — **Fonts:**
  - `NotoSansCJKtc-TOMS.otf` rebuilt (30 missing characters); test `data.ui_font_covers_text`
    (`make_ui_font.py --check`, 77 = no fontTools).
  - `rml_ui.cpp` loads a system CJK font as the last fallback face (Windows `msjh.ttc` …, Android
    `NotoSansCJK-Regular.ttc` face 3; `TOMS_SYSTEM_FONT`). Proved on Windows with the old subset; the Android APK builds.
  - Changed-only run: 69 of 107 tests, all pass. Full suite and web build not run after the NPC work.
- **2026-10-16** — **HTML editors** (committed `4067852` / `149856a`): resizable panels (`splitter.js`), the event
  editor's text size; stage editor palette types with detail windows, ✎ Edit (an object's own look and linked events,
  the stage file's `objects`), 🎮 game view; the game draws object looks and fires linked events (`smoke.stage_objects`).
- **2026-10-16** — **Windows app `stage_event_editor.exe`** (committed `149856a`; [../19_STAGE_EDITOR.md](../19_STAGE_EDITOR.md) section 6):
  - The owner chose the game's own stack (Qt / C++ / bgfx, no other library); a WebView2 host was started and dropped.
  - `tools/stage_event_editor/core`: the rules without Qt (`stage_project`, `stage_checks`, `stage_save`,
    `event_project`), read and written with `ordered_json` (untouched files byte for byte).
  - `app`: `AppShell` (tabs or separate windows, View menu), `StageEditor` + `StageCanvas` (the game renderer through
    `GameCanvasView`: `.anim` clips play), `ObjectWindow`, `EventEditor` (+ forms, tabs, NPCs / talk preview,
    dialogues). The editors call each other directly; after a save the other rereads what it has not changed.
  - Shared code: `GameCanvasView` follows a canvas to a new native window; the language toggle is not added to a
    `QMainWindow` hosted in a tab (also fixes `toms_editor`'s duplicate).
  - Checked against the HTML editors in headless Chrome (stage checks, story, safe cells, auto-place; 166 event
    problems; Save bytes); the check found a read of a destroyed temporary in the pool merge, fixed.
  - Tests: `unit.stage_core_test` (192), `stage_editor.app_selftest`; 401 + 237 new zh_TW strings. Changed-only:
    23 of 110, all pass.
- **2026-10-17** — **Editors, resource manager, objects by class** (not committed; [DAILY_2026-10-17.md](DAILY_2026-10-17.md)):
  - `AppShell`: one `QMainWindow`, two `QDockWidget`s (stage / events) tabbed on top; float (□, ⧉), dock back, split;
    layout saved with a version. View: Default layout, Dock both as tabs, Stage panels, Reset stage layout.
  - Stage editor speed: `StageDoc::ver` + cached `isDirty`, `Project::dataVer` + cached `dataDirty`, `placedEvents`
    (one legend look-up per char), the Story table built only when shown and only when its key changes, the story
    check deferred 30 ms after an edit, per-frame `Looks` in `StageCanvas`; the move ghost drawn on the GPU with
    `lookAt(moveFrom)`.
  - `src/core/engine/resource*`: `ResourceManager` (cache by type + absolute folded path, `provide`, path mapper,
    budget / LRU, stamps, `checkFiles` / `refresh`, `report`, root + manifest + missing handler + `preload`),
    `TextureRes` / `AtlasRes` / `AnimRes` / `ParticleRes` / `FontRes` / `JsonRes`, `AtlasList`. `render_object.*`:
    `RenderObject` (a `GameObject`), the factory, six types, `lookToRenderJson`. `vfs.h`: `vfsOpen` / `vfsPath` /
    `vfsUtf8`.
  - Game: `res_` with `@sprite-atlas` provided from the loaded atlas; previews, idle / door / object clips through it;
    `resources.json`; `res.missing` message held until the map shows; `SessionOptions::hotReload`, `--hot-reload`.
  - `map_objects.*`: `MapObject` + kinds, `MapFloor` (built in `loadStage`), looks per status as `LookChain`s
    (`SpriteLook`, `PlateLook`, `ClipLook`); `Game::draw` → `drawTileLayer` + `floor_.render`; `movePlayer` /
    `interact` / `openDoor` / `bumpGate` dispatch to the objects.
  - Palette: click → brush + non-modal type window that follows; + on every group (story items for doors / keys,
    brush and scroll after Create, Data tab first); categories (`group` field, `editor_palette.json`); Chinese ids
    (`\p{Ll}\p{Lo}\p{Nd}_`); `typeRec("key:<c>")` → `item:key_<c>`; screenshot mode `--create=<cat>:<id>`, shows
    every panel and does not save the layout.
  - Tests: `unit.resource_test`, `unit.render_object_test`, `test_select.py` rules `ENGINE_RES` and the manifest;
    new self-test checks. Changed-only: 75 of 112, all pass. Full suite / web build not run.
- **2026-10-18** — **AI restyles and sprites, save-system guards, stable object ids** (not committed in TOMS;
  [DAILY_2026-10-18.md](DAILY_2026-10-18.md)):
  - 10085 restyled as Christmas (`tests/10085_christmas`, 2026-10-17 evening) and Zeus (`tests/10085_zeus`): 63 of 69
    pictures each, Spine part by part, `check_restyle.py` passes. Zeus: objects at 0.8, a `badge` kind for the
    ×-badges, plain Spine discs described alone.
  - `fix_edges` (restyle_pilot `finish`): soft edges from the nearest solid pixel with the original's rim; wheel-rim
    near-white edge pixels 30 % → 0 %; `repair_edges.py` repaired both outputs.
  - Docs moved to `AIGamestyle/Doc/Reskin_Game_Art(.zh-TW).md` and `Reskin_Setup_New_PC(.zh-TW).md`;
    `check_reskin_setup.py`; `docs/26` a pointer. The plan's `"also"` is not read (documented).
  - `make_sprites.py` / `sprite_sheet.py` / `make_toms_sprites_plan.py` / `add_toms_types.py` (not run): 230 TOMS
    sprites made and reviewed, about 50 redone; `--only` takes a list. Game: every atlas page-0 region gets a layer,
    gate `"door"` picture, stage `"theme"`, `playerLook`; editor Floor theme combo, gate keys by `"opens"`.
  - Save: `writeJsonAtomic` fsync + `.bak`, `readSlotJson` backup fallback, `SlotSummary::damaged / fromBackup /
    newer`, `SlotLoadInfo`, `slotIsNewer` write guard, `slotReadOnly_`, `Game::flushSave` on quit / background /
    `pagehide` (`jsFlushSave`), migration tables `kMetaMigrations` / `kRunMigrations`; text keys `continue.damaged`,
    `continue.from_backup`, `continue.newer`, `save.restored`, `save.newer`; UI font rebuilt.
  - Object ids: `Entity::uid`, `entityStatusKey(..., uid)`, `Game::statusKeyAt`, `Game::migrateStatusKeys` (also
    fixes `curStage` vs `st.id` keys); `Project::ensureUids` in `commitEdit`; ObjectWindow and the HTML editor keep the
    uid; `tools/add_stage_uids.py` (71 stage files; mapped in `test_select.py`). Doc `docs/28_SAVE_SYSTEM.md`.
  - `AIGamestyle/App/make_vs_solution.bat` (vswhere, VS's CMake 4.3, VS 2026 `.slnx`; builds).
  - Tests: `save_test` and `stage_core_test` extended. Changed-only: 70 of 112, all pass. Full suite / web not run.
- **2026-10-19** — **Floors per chapter, raw art split, logic graph, reskin PMA fix, body swap stopped, gameplay plan**
  ([DAILY_2026-10-19.md](DAILY_2026-10-19.md)):
  - Tests: 10085 out of the TOMS tests; `tests/test_folders.json`; `tools/test_switches.cmd/.ps1` tree tool and
    `tests/test_switches.json` (all off, the owner's choice); `test_select.py` rules for `art_raw`, `tools/art`,
    `redesign_floors.py`, `make_idle_clips.py`, the test folders and switches.
  - Sprites in `assets` via the atlas, `tools/make_idle_clips.py`; `tools/redesign_floors.py` and `docs/29`.
  - `art_raw/` (sprites, `styles/dark16/sprites`, `atlas/*.atlasproj` writing `assets/media/atlas`); `game_assets.cpp`
    atlas only; `game_session.cpp` and `cmake/TomsPrerequisites.cmake` check the atlas (owner commit `4e44076`).
  - Native event editor: `EventGraph.h/.cpp`, the "logic" tab (owner commit `4f338cb`).
  - AIGamestyle: `restyle_pilot.keep_skin`, `is_premultiplied / premultiply / unpremultiply`; PMA-aware
    `restyle_game.py`, `repair_edges.py`; new `spine_render.py` (`--pma`), `spine_project.py`, `character_swap.py`
    (guide, concept, projection with outline and trust masks, `cloth_parts`, `restyle_parts`, knee breeches,
    `still_animations`, `rebind_breeches`), plan `man_10085.json`. Swap stopped by the owner; conclusion in
    `Reskin_Game_Art(.zh-TW).md` §11.
  - `docs/12` §1 hint and §4.5 TortoiseGit → OpenSSH (owner confirmed; commit `f04809c`).
  - `docs/30_GAMEPLAY_PLAN.md` (+ zh_TW) from `docs/魔塔.txt`; open questions Q16–Q21.
  - Tests: changed-only selections 0–1 of 113 per change; full suite / web not run; the smoke screenshot, `mpdi_convert`
    and font tests need the folders removed in `795385a`.

