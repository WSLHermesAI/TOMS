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
