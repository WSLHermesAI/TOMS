# TOMS: Tower of the Sorcerer on Qt6 + bgfx

*Tower of the Sorcerer* (TOMS) built from:

| Executable | Built from | Ships? |
|---|---|---|
| **`toms_game.exe`** | SDL3 window + **bgfx** renderer + the existing game code | **yes**: contains no Qt |
| **`toms_editor.exe`** | **Qt6** editor with an embedded bgfx view that runs the same game, plus the old stage editor | no, internal tool |

All game content is in `assets/`, all source code in `src/`. The original game code
(`src/core`) is compiled **without edits**; the old Vulkan build itself was removed on 2026-09-27
and is in git history before commit `979bcf3`.

## Make a release (two double-clicks)

| File | Builds | Result |
|---|---|---|
| **`build_windows.bat`** | the shipping Windows game (Release, no editor, no Qt, no console window) | `dist\TOMS-windows\` (+ `.zip`): `toms_game.exe`, `assets\`, the MSVC runtime DLLs. Copy the folder anywhere and double-click `toms_game.exe`. |
| **`build_web.bat`** | the web game (Release, WebGL2) | `dist\TOMS-web\` (+ `.zip`): `index.html`, `toms_game.<stamp>.js/.wasm/.data`, `.htaccess`, `web.config`. Copy the files to any web server and open its URL. |
| **`publish_web.bat`** | `build_web.bat` + publish to GitHub Pages (asks before pushing) | live at https://wslhermesai.github.io/TOMS/ ([docs/07](docs/07_PUBLISH_GITHUB_PAGES.md)) |

Both check the prerequisites first (popups with download links) and print where the result is.

## Quick start for development (Windows)

1. **Check prerequisites:** double-click `tools\check_env.cmd`. Anything missing is listed in a
   popup with its download link. Details: [docs/02_INSTALL_WINDOWS.md](docs/02_INSTALL_WINDOWS.md).
2. **Open in Visual Studio 2022 or newer:** *File → Open → Folder…* → this `TOMS` folder (the repository root).
   The first configure downloads and builds bgfx, SDL3, Dear ImGui and glm (a few minutes).
3. Pick **Windows x64 Debug** (or Release), choose **toms_game** or **toms_editor** as the startup
   item, and press **F5**. Details: [docs/03_VISUAL_STUDIO.md](docs/03_VISUAL_STUDIO.md).

Without Visual Studio open: `tools\build.cmd windows-release`.

**Web version:** `tools\build_web.cmd`, then `tools\serve_web.cmd` (needs the Emscripten SDK).
Details: [docs/06_BUILD_WEB.md](docs/06_BUILD_WEB.md).

## Documents

| Doc | Contents |
|---|---|
| [01_ARCHITECTURE.md](docs/01_ARCHITECTURE.md) | targets, how the old code runs on bgfx unmodified, one frame, Qt + bgfx embedding, shaders |
| [02_INSTALL_WINDOWS.md](docs/02_INSTALL_WINDOWS.md) | every prerequisite with links, the checker and popups, Qt and font setup |
| [03_VISUAL_STUDIO.md](docs/03_VISUAL_STUDIO.md) | open, configure, F5 targets, command-line options, smoke tests, debugging |
| [04_MIGRATION_PLAN.md](docs/04_MIGRATION_PLAN.md) | phases from the Vulkan project to the full Qt + bgfx engine |
| [05_TROUBLESHOOTING.md](docs/05_TROUBLESHOOTING.md) | configure, build and runtime problems |
| [06_BUILD_WEB.md](docs/06_BUILD_WEB.md) | the browser version: emsdk, `build_web.cmd`, `serve_web.cmd`, how it works, debugging |
| [07_PUBLISH_GITHUB_PAGES.md](docs/07_PUBLISH_GITHUB_PAGES.md) | publishing the web version to GitHub Pages: setup, `publish_web.bat`, cache-safe file names, rollback |
| [progress_report/](docs/progress_report/PROGRESS_REPORT.md) | **status board**: next step, phase status, what is still open, dated log |

The long-term engine design (`docs/EngineBlueprint/`) was removed with the old project; read it
from git history: `git show 979bcf3^:docs/EngineBlueprint/README.md`.

## Layout

```
TOMS/
  CMakeLists.txt  CMakePresets.json  launch.vs.json
  cmake/          TomsPrerequisites.cmake (checks + popups), TomsDependencies.cmake (pinned downloads)
  tools/          check_env.cmd/.ps1/.sh, build.cmd, build_web.cmd, serve_web.cmd, web_smoke_test.mjs
  docs/
  assets/         all game content
    media/        sprites, sfx, fonts
    data/         stages, story, dialogue, items (JSON)
  src/            all source code
    engine/       toms_bgfx: BgfxRenderer (IRenderer), bgfx host, ImGui on bgfx, shaders/*.sc
    game/         compat/ (renderer shims), GameSession, main_sdl.cpp -> toms_game (desktop + web), web/
    editor/       BgfxViewport, EditorWindow -> toms_editor.exe; stage/ = the old stage editor
    core/         the core game code (engine/ + game/): gameplay, story, combat, saves, UI
    third_party/  json, stb, miniaudio headers
```
