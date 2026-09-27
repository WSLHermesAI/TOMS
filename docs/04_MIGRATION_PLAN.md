# 04 — Migration plan: from TOMS (Vulkan) to TOMS on Qt + bgfx

The rule for every phase: **the game stays playable**, and each phase ends with a check you can
run. The core game code in `src/core` is compiled unmodified until a phase needs to change it; until
then it is compiled from where it is.

## Phase 1 — The existing game on bgfx, and a Qt editor around it (done)

| Item | Where |
|---|---|
| `BgfxRenderer` implements the existing `IRenderer`; the compat `renderer.h` makes `new Renderer()` build it | `src/engine/src/bgfx_renderer.*`, `src/game/compat/` |
| SDL3 game executable replaces GLFW + Vulkan `main.cpp` (same controls) | `src/game/src/main_sdl.cpp`, `src/game/src/game_session.*` |
| ImGui dev windows (F1, F2, Tab stage select, toasts) drawn by bgfx | `src/engine/src/imgui_bgfx.*` |
| Qt editor: bgfx *Play* view running the real game, stage list, session panel, old stage editor as a tab | `src/editor/` |
| One shader source per program, compiled by shaderc for D3D11/12, Vulkan, GL, GLES | `src/engine/shaders/` |
| Prerequisite checks with popups and download links; VS presets; `build.cmd` | `cmake/`, `tools/`, `CMakePresets.json` |
| glm fetched instead of the hand-installed `GLM_DIR` copy | `cmake/TomsDependencies.cmake` |

**Check:** `toms_game.exe --frames=120 --keys=enter@30 --screenshot=stage.png` shows stage 1 the
same as the Vulkan build; `toms_editor.exe` plays it in the *Play* tab.

**Verified 2026-09-25** (Windows 11, Visual Studio 2026 / MSVC 14.51, Qt 6.9.0):

- `toms_game` renders the title screen and stage 1 on **Direct3D 11, Direct3D 12, Vulkan and
  OpenGL 4.3**. D3D12 and OpenGL match D3D11 pixel for pixel, except the animated title cursor.
  On Vulkan about 1 % of pixels differ, all on glyph and panel edges: a one-pixel rasterisation
  offset, not a missing element.
- `toms_editor` runs the game in the *Play* tab on Direct3D 11 at HiDPI (1972×1634 device pixels)
  with the stage list, session panel and old stage editor tab.
- Both executables exit with "no leaks" from the old `Object::DumpLeaks` report.
- `toms_game.exe` imports only Windows and MSVC runtime DLLs (no Qt); `toms_editor.exe` imports
  Qt6Core/Gui/Widgets.

**Not done in phase 1 (known gaps):**

- **Web build.** Done in phase 2 (2026-09-26): see [06_BUILD_WEB.md](06_BUILD_WEB.md).
- **Tests.** The 30 `*_test.cpp` files (in `src/core`) are not built yet: the old project that
  built them is gone. Phase 2 registers them with CTest.
- **Mobile layout switches:** the web build applies the owner's rule (grow UI objects through
  `Game::setUiScale` / `setPadScale`, keep the game resolution), not the retired `setDesignSize`.
  Only screens that read `UiRoot` grow; migrating the rest is game-side work.

## Phase 2 — Own the platform layer and the tests

1. Register the old unit tests (`src/**/*_test.cpp`) as CTest targets in `tests/`, so
   Visual Studio's *Test Explorer* lists them. Add a smoke test that runs `toms_game --frames` and
   compares the screenshot with a stored golden PNG (tolerance for GPU differences).
2. ✅ **Web build** (2026-09-26): `web-*-windows` / `web-*` presets, `tools\build_web.cmd` +
   `serve_web.cmd`; the browser glue (IDBFS saves, slot refresh, mobile scale, page buttons) is
   in `main_sdl.cpp` + `src/game/web/shell.html` rather than a separate `main_web.cpp`. Verified in
   headless Chrome: title → new game → save → reload restores the save. See [06](06_BUILD_WEB.md).
3. ✅ Deleted the GLFW/Vulkan and Emscripten entry points (2026-09-27); their behaviour is in
   `game_session.cpp` / `main_sdl.cpp` since phase 1.

**Check:** CTest green in VS and CI; the web build plays stage 1 in Chrome and Edge.

## Phase 3 — Break the `Game` god object and drop the compat shim

1. Start editing `src/core` directly (it stays the home of the game code; the "unmodified" rule ends here).
2. Replace `new Renderer()` inside `Game::loadAssets` with a renderer passed in by the host; then
   delete `src/game/compat/`.
3. Split `Game` into scenes and services as designed in `docs/EngineBlueprint/03_GAME_LAYER.md`.
4. Replace the immediate-mode UI and the ~30 hit-rect fields with a real UI layer (RmlUi or own
   widgets; see EngineBlueprint 02 §L5).

**Check:** all ported tests pass; `src/game/compat/` is gone; `src/core` talks to the engine directly.

## Phase 4 — Grow the editor

1. The stage editor reads the game's data registry instead of the hard-coded `catalog.h`
   (the drift EngineBlueprint 04 §1 describes), and draws stages through the bgfx viewport, so
   what you edit is exactly what the game renders.
2. Docking with Qt Advanced Docking System; a reflection-driven inspector; `QUndoStack`.
3. The event / flow editor (QtNodes) from `docs/EventSystem/`, with live preview in the viewport.
4. More viewports (prefab preview, sprite atlas viewer) through `bgfx::createFrameBuffer` on their
   own native windows (see [01 §5](01_ARCHITECTURE.md#5-qt--bgfx-in-the-editor)).

**Check:** a stage edited in the editor loads in `toms_game.exe` with no manual step.

## Phase 5 — 3D and effects on bgfx

The plan is in EngineBlueprint `09_RENDERING_2D_3D.md` and `11_BGFX_QT_ARCHITECTURE.md`: glTF via
fastgltf, ozz-animation (start with its CPU `SkinningJob`, GPU skinning later), bgfx instancing,
cascaded shadow maps from bgfx's `16-shadowmaps` example, Effekseer through efkbgfx.

## What happens to the old project's code

Since 2026-09-27 the old Vulkan build is gone and this project is the repository root. The old
game code lives in `src/core` and is compiled by `toms_core`. Every file there that was not
compiled any more was deleted on 2026-09-27 (git history has them).

| Old | Status | Removed in |
|---|---|---|
| `src/core/engine/renderer.*`, `renderer_webgl.*`, `renderer_webgpu.*`, `vk_util.h`, `batch_renderer.h`, `texture.*` (+ `texture_test.cpp`) | deleted; replaced by `BgfxRenderer` (via `src/game/compat/renderer.h`) | done |
| `src/core/engine/imgui_layer.*`, `imgui_web.*` | deleted; replaced by `src/engine/src/imgui_bgfx.*` | done |
| `src/core/game/core/main.cpp`, `src/core/engine/emscripten_main.cpp` | deleted; replaced by `src/game/src/main_sdl.cpp` + `game_session.cpp` + `src/game/web/shell.html` | done |
| `src/game/compat/renderer.h` (`Renderer` = `BgfxRenderer`) | still used by the core code's `new Renderer()` | phase 3 |
| Vulkan/GLSL shaders (`.spv`, `.vert`, `.frag`) | deleted with the old build; `src/engine/shaders/*.sc` replace them | done |
| `src/editor/stage/` (Qt stage editor) | compiled into `toms_editor` as a tab | code moves in phase 4 |
| Old root `CMakeLists.txt` (Vulkan/WebGL) | deleted 2026-09-27; the root `CMakeLists.txt` is now this project's | done |
