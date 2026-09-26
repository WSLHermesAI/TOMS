# 04 — Migration plan: from TOMS (Vulkan) to toms_next (Qt + bgfx)

The rule for every phase: **the game stays playable**, and each phase ends with a check you can
run. Code moves from `TOMS/src` into `toms_next/` only when a phase needs to change it; until
then it is compiled from where it is.

## Phase 1 — The existing game on bgfx, and a Qt editor around it (this folder, done)

| Item | Where |
|---|---|
| `BgfxRenderer` implements the existing `IRenderer`; the compat `renderer.h` makes `new Renderer()` build it | `engine/src/bgfx_renderer.*`, `game/compat/` |
| SDL3 game executable replaces GLFW + Vulkan `main.cpp` (same controls) | `game/src/main_sdl.cpp`, `game/src/game_session.*` |
| ImGui dev windows (F1, F2, Tab stage select, toasts) drawn by bgfx | `engine/src/imgui_bgfx.*` |
| Qt editor: bgfx *Play* view running the real game, stage list, session panel, old stage editor as a tab | `editor/` |
| One shader source per program, compiled by shaderc for D3D11/12, Vulkan, GL, GLES | `engine/shaders/` |
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
  with the stage list, session panel and legacy stage editor tab.
- Both executables exit with "no leaks" from the old `Object::DumpLeaks` report.
- `toms_game.exe` imports only Windows and MSVC runtime DLLs (no Qt); `toms_editor.exe` imports
  Qt6Core/Gui/Widgets.

**Not done in phase 1 (known gaps):**

- **Web build.** Done in phase 2 (2026-09-26): see [06_BUILD_WEB.md](06_BUILD_WEB.md).
- **Tests.** The 30 `*_test.cpp` files still build only in the old project. Phase 2 registers
  them with CTest here.
- **Mobile layout switches:** the web build applies the owner's rule (grow UI objects through
  `Game::setUiScale` / `setPadScale`, keep the game resolution), not the retired `setDesignSize`.
  Only screens that read `UiRoot` grow; migrating the rest is game-side work.

## Phase 2 — Own the platform layer and the tests

1. Register the old unit tests (`src/**/*_test.cpp`) as CTest targets in `toms_next/tests/`, so
   Visual Studio's *Test Explorer* lists them. Add a smoke test that runs `toms_game --frames` and
   compares the screenshot with a stored golden PNG (tolerance for GPU differences).
2. ✅ **Web build** (2026-09-26): `web-*-windows` / `web-*` presets, `tools\build_web.cmd` +
   `serve_web.cmd`; the browser glue (IDBFS saves, slot refresh, mobile scale, page buttons) is
   in `main_sdl.cpp` + `game/web/shell.html` rather than a separate `main_web.cpp`. Verified in
   headless Chrome: title → new game → save → reload restores the save. See [06](06_BUILD_WEB.md).
3. Move `src/game/core/main.cpp`'s remaining behaviour (none after phase 1) and delete the GLFW
   path from the old project, or freeze the old project as read-only.

**Check:** CTest green in VS and CI; the web build plays stage 1 in Chrome and Edge.

## Phase 3 — Break the `Game` god object and drop the compat shim

1. Move `src/game/**` into `toms_next/game/` (history preserved with `git mv`).
2. Replace `new Renderer()` inside `Game::loadAssets` with a renderer passed in by the host; then
   delete `game/compat/`.
3. Split `Game` into scenes and services as designed in `docs/EngineBlueprint/03_GAME_LAYER.md`.
4. Replace the immediate-mode UI and the ~30 hit-rect fields with a real UI layer (RmlUi or own
   widgets; see EngineBlueprint 02 §L5).

**Check:** all ported tests pass; `game/compat/` is gone; the old `TOMS/src` is no longer compiled.

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

## What happens to the old project

| Old | Status after phase 1 | Removed in |
|---|---|---|
| `src/engine/renderer.cpp`, `renderer_webgl.cpp`, `renderer_webgpu.cpp`, `vk_util.h`, `batch_renderer.h`, `texture.*` | not compiled by toms_next | phase 3 (old build retired) |
| `src/engine/imgui_layer.*`, `imgui_web.*` | replaced by `engine/src/imgui_bgfx.*` | phase 2 / 3 |
| `src/game/core/main.cpp`, `src/engine/emscripten_main.cpp` | replaced by `game/src/main_sdl.cpp` (desktop and web) + `game/web/shell.html` | phase 2 |
| `assets/shaders/*.spv`, `.vert`, `.frag` | replaced by `engine/shaders/*.sc` | phase 3 |
| `editor/` (Qt stage editor) | compiled into `toms_editor` as a tab | code moves in phase 4 |
| Root `CMakeLists.txt` (Vulkan/WebGL) | still works, independent | phase 3 |
