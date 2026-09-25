# Progress Report — Log Part 1 (2026-09-25)

> Chronological log entries, oldest first. The live **Next Step**, status table, waiting list and
> open questions are in [PROGRESS_REPORT.md](PROGRESS_REPORT.md).

---

### 2026-09-25 — Decision: bgfx for the game, Qt6 for the editor, ship bgfx only

Owner: rewrite with Qt + bgfx mixed; "use QT as editor and game content use bgfx, when publish
will be only bgfx".

**Evaluation before deciding.** The `EngineBlueprint` committed the day before had rejected
both parts: bgfx because it has no browser WebGPU (Dawn-native only), and Qt because the plan was
an ImGui editor inside the game that also runs on the web. Points raised:

- **bgfx:** one backend replaces the three hand-written renderers (Vulkan, WebGL2, a broken
  WebGPU). WebGL2 is enough for a 13×11 tile game. The costs are the `.sc` shader dialect +
  `shaderc`, no built-in 3D renderer, and no browser WebGPU.
- **Qt:** good for desktop tools. A bgfx viewport embedded in Qt makes the editor show exactly
  what the game renders. Qt is not suitable inside the shipped game (WebAssembly size, LGPL static
  linking, two event loops).
- The game logic (`src/game/systems/*`) was already renderer-independent behind `IRenderer`, so
  the change is a port, not a rewrite.

The owner chose the split. It is recorded as **decided** in EngineBlueprint.

### 2026-09-25 — EngineBlueprint updated for the decision

Edited `docs/EngineBlueprint/` README and docs 01, 02, 04, 05, 06, 07, 08, 09, 10, plus
`docs/README.md`:

- RHI card (09 §2): **bgfx chosen**, Diligent demoted, reasons and accepted costs listed; shaders
  now `.sc` → `shaderc` (Slang/SPIRV-Cross/Tint no longer needed); quality tier *Web-WebGPU*
  removed; the 3D/shadow/FX cards point at bgfx examples (`16-shadowmaps`, `18-ibl`,
  `05-instancing`, `50-headless`) and **efkbgfx** for Effekseer.
- Editor (04, 10): the Qt editor `toms_editor` with a `BgfxViewport` replaces the in-game ImGui
  editor; QtNodes for graphs, Qt Advanced Docking System, `QUndoStack`; the old Qt stage editor is
  **kept and grown** instead of retired.
- Layout and flags (01): two executables (`mt_app`, `toms_editor`), `ENGINE_DEBUGUI`, `ENGINE_RHI`
  values for bgfx, and a CI check that no shipped target links Qt.
- Resources (08): bgfx defers destroys itself; our layer adds refcounted `Ref` wrappers and checks
  live handle counts against `bgfx::getStats()`.
- Facts checked on the web: efkbgfx (community, no user-defined materials), bgfx.cmake's
  `bgfx_compile_shaders`, Qt ADS (LGPL-2.1), QtNodes (BSD-3), the bgfx example list.

**Not finished:** `11_BGFX_QT_ARCHITECTURE.md` is linked from several docs but not written, and
the UI-authoring-tool rows are missing. The background research agent for them stopped before
reporting (see PROGRESS_REPORT W5).

### 2026-09-25 — Research: glTF, skinning, morphing and instancing on bgfx

Owner questions: does bgfx render glTF; how to do skinning, morphing and instancing; is there a
reliable third-party implementation?

- **bgfx has no model loader.** `geometryc` converts OBJ and glTF/GLB (via cgltf) to bgfx's `.bin`
  mesh, but keeps **geometry only** (position, normal, UV0; no materials, textures, skins or
  animations). Verified in `tools/geometryc/geometryc.cpp`.
- **Pattern given:** skinning through `setTransform(mtx, num)` → `u_model[]` (limit
  `BGFX_CONFIG_MAX_BONES`, ~48 joints on WebGL2), or a joint texture; morph targets on the CPU, as
  vertex attributes, or through an RGBA32F morph texture (portable to WebGL2); instancing through
  `allocInstanceDataBuffer` (≤ 5 vec4 per instance).
- **No drop-in library for this on bgfx.** Babylon Native (full glTF on bgfx, but gameplay in JS,
  preview status), Ant engine (bgfx + ozz + glTF skinning, Lua, MIT; best code to read), Cluster
  (PBR, static), bae (PBR/CSM, stale).
- **Recommendation:** fastgltf + **ozz-animation's CPU `SkinningJob`** into a dynamic vertex
  buffer first (no custom shader; identical on every backend), GPU skinning later only if
  profiling asks for it; instancing with bgfx's own instance buffers.

### 2026-09-25 — toms_next: phase 1 built and verified

Owner: make a document for integrating the current project with Qt + bgfx, including how to
install everything, checks that pop up a warning with a link or keyword when something is missing,
and Visual Studio support on Windows; put everything new in a new folder and split the project.

**Approach: run the existing game unmodified on bgfx.** `Game::loadAssets` does
`ren = new Renderer();`, and the legacy includes are bare filenames. So `toms_next/game/compat/`,
first on the include path, provides its own `renderer.h` with `using Renderer =
toms::next::BgfxRenderer;` and an empty `vk_util.h`. Not a line of `TOMS/src` was edited.

**What was built** (all under `toms_next/`):

| Part | Files |
|---|---|
| `BgfxRenderer : IRenderer`: 1024×768 letterbox, sprites then text, sRGB atlases, point sampling, alpha blend, solid quads, real `savePNG` | `engine/src/bgfx_renderer.*` |
| bgfx host: single-threaded init, `SwapChain`, screenshot callback → PNG | `engine/src/bgfx_host.*` |
| ImGui drawn by bgfx (the F1/F2/Tab/toast dev windows) | `engine/src/imgui_bgfx.*` |
| Shaders, compiled by shaderc to DXBC / SPIR-V / GLSL / ESSL and embedded as headers | `engine/shaders/*.sc`, `engine/src/embedded_shaders.*` |
| `GameSession`: toolkit-neutral input, a line-by-line port of the old `main.cpp` controls | `game/src/game_session.*` |
| `toms_game.exe` (SDL3); test switches `--frames`, `--screenshot`, `--keys`, `--renderer` | `game/src/main_sdl.cpp` |
| `toms_editor.exe` (Qt6): `BgfxViewport` QWidget, Stages and Session docks, the old stage editor embedded as a tab | `editor/src/*` |
| Checks + popups + links: configure time, standalone checker, runtime message boxes | `cmake/TomsPrerequisites.cmake`, `tools/check_env.*`, `tools/show_message.ps1` |
| Pinned downloads: bgfx.cmake v1.161.9510-579, SDL release-3.4.8, ImGui v1.90.9, glm 1.0.1 | `cmake/TomsDependencies.cmake` |
| VS presets (Debug, Release, Shipping, CI), `launch.vs.json`, `tools/build.cmd` | root |
| Docs 01–05 | `docs/` |

**Problems found and fixed during the build:**

- **bx requires C++20**, but the legacy code must stay C++17 (its `u8"…"` literals change type
  in C++20). Only `toms_bgfx` is C++20, bgfx is linked PRIVATE, and `bgfx_host.h` /
  `bgfx_renderer.h` expose no bgfx types, so the game code never sees bx.
- **bgfx API change:** the window handle and size moved from `PlatformData` / `Init::resolution`
  into `Init::swapChain`; `reset()` now takes `(flags, SwapChain*)`; sRGB became
  `BGFX_SWAP_CHAIN_SRGB_BACKBUFFER`.
- **SDL3:** `SDL_SetMainReady` needs `SDL_main.h`; `SDL_MAIN_HANDLED` keeps our own `main()`.
- **Qt `emit` macro vs the legacy `Logger::emit()`** (`src/engine/log.h`): `QT_NO_EMIT` is set only
  on the new editor sources (which use `Q_EMIT`); the old stage editor keeps `emit`.
- **Hidden prerequisite:** the old build found glm through a hand-installed `GLM_DIR`; toms_next
  fetches glm.
- Dropped `/permissive-` so the legacy code compiles under the same rules as before.
- The editor's own window capture showed the bgfx view white: GDI cannot read a DXGI flip-model
  swap chain. The smoke test now grabs the composed desktop area (a troubleshooting row was added).

**Verified on this machine** (Windows 11, VS 2026 / MSVC 14.51, Qt 6.9.0 auto-detected in
`%USERPROFILE%\Qt`):

- `tools\check_env.ps1 -NoGui`: every required item OK; only RenderDoc (optional) missing.
- `toms_game`: title screen and stage 1 (after a scripted Enter) render correctly; CJK text,
  HUD, pad and dialogue as before. D3D12 and OpenGL match D3D11 except the animated cursor; Vulkan
  differs by ~1 % on glyph/panel edges.
- Font fallback: with `TOMS_FONT=msjh.ttc`, stage 1 renders completely in Microsoft JhengHei.
- `toms_editor`: the Play tab runs the game on D3D11 at HiDPI (1972×1634) next to the Qt docks.
- Both exit with `Object lifecycle: live objects = 0, no leaks`.
- `toms_game.exe` imports only Windows + MSVC runtime DLLs; the `windows-shipping` preset builds
  with no Qt search at all.

**Also answered:** how to run the editor (the built exe, F5 in VS, or `tools\build.cmd`), and how
to build the web version today (the old project's `-DWEB=ON` build, WebGL2; toms_next has no web
build yet, which is now the next step).
