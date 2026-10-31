# TOMS: Tower of the Sorcerer on Qt6 + bgfx

*Tower of the Sorcerer* (TOMS) built from:

| Executable | Built from | Ships? |
|---|---|---|
| **`toms_game.exe`** | SDL3 window + **bgfx** renderer + the existing game code | **yes**: contains no Qt |
| **`ui_editor.exe`**, **`battle_editor.exe`** | **Qt6** editors with an embedded bgfx view that runs the same game (the UI screens, the battles) | no, internal tools |
| **`stage_event_editor.exe`** | **Qt6** stage (floor map) and event editors | no, internal tool |

All game content is in `assets/`, all source code in `src/`. The original game code
(`src/core`) is compiled **without edits**; the old Vulkan build itself was removed on 2026-09-27
and is in git history before commit `979bcf3`.

## Make a release: double-click `TOMS_Launcher.bat`

One window (English or 繁體中文, the box at its top right) lists every build, deploy, test and setup action by group
(Windows, Web, Android, Tests, Setup and checks), says what each does and where its result goes, and runs the one you
pick in its own console window. The list at its bottom shows what ran and whether it finished OK.

The release actions are these scripts in `tools\`; they also run on their own (double-click or a command prompt):

| File | Builds | Result |
|---|---|---|
| **`tools\release_windows.cmd`** | the shipping Windows game (Release, no editor, no Qt, no console window) | `Build\dist\TOMS-windows\` (+ `.zip`): `toms_game.exe`, `assets\`, the MSVC runtime DLLs. Copy the folder anywhere and double-click `toms_game.exe`. |
| **`tools\release_editors.cmd`** | every editor (`windows-release`: MSVC, Qt 6) | `Editor\bin\`: all editor `.exe`s, `atlaspack`/`fontpack`, Qt and runtime DLLs; `Editor\BUILD_INFO.md`. Start them with the launchers in `Editor\`. (`tools\release_editors.cmd nobuild` only copies the last build.) |
| **`tools\release_web.cmd`** | the web game (Release, WebGL2) | `Build\dist\TOMS-web\` (+ `.zip`): `index.html`, `toms_game.<stamp>.js/.wasm/.data`, `.htaccess`, `web.config`. Copy the files to any web server and open its URL. |
| **`tools\publish_web.cmd`** | `tools\release_web.cmd` + publish to GitHub Pages (asks before pushing) | live at https://wslhermesai.github.io/TOMS/ ([docs/project/07](docs/project/07_PUBLISH_GITHUB_PAGES.md)) |

They check the prerequisites first (popups with download links) and print where the result is.

## Quick start for development (Windows)

1. **Check prerequisites:** double-click `tools\check_env.cmd`. Anything missing is listed in a
   popup with its download link. Details: [docs/project/01_INSTALL_WINDOWS.md](docs/project/01_INSTALL_WINDOWS.md).
2. **Open in Visual Studio 2022 or newer:** *File → Open → Folder…* → this `TOMS` folder (the repository root).
   The first configure downloads and builds bgfx, SDL3, Dear ImGui and glm (a few minutes).
3. Pick **Windows x64 Debug** (or Release), choose **toms_game** (or an editor) as the startup
   item, and press **F5**. Details: [docs/project/02_VISUAL_STUDIO.md](docs/project/02_VISUAL_STUDIO.md).

Without Visual Studio open: `tools\build.cmd windows-release`.

**Web version:** `tools\build_web.cmd`, then `tools\serve_web.cmd` (needs the Emscripten SDK).
Details: [docs/project/05_BUILD_WEB.md](docs/project/05_BUILD_WEB.md).

## Documents

All documents: **[docs/README.md](docs/README.md)**, in three folders, each with its Traditional Chinese copies in
its own `zh_TW/`:

| Folder | What belongs there |
|---|---|
| [docs/gameplay/](docs/README.md#gameplay) | the game and its content: battle, stages, events, UI (RmlUi), saves, the stage/event and UI editors |
| [docs/engine/](docs/README.md#engine) | the core engine and its tools: rendering, resources, animation, atlas, particles, fonts, glTF |
| [docs/project/](docs/README.md#project) | setup, building, tests, the team, progress reports ([status board](docs/project/progress_report/PROGRESS_REPORT.md)) |

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
    editor/       BgfxViewport -> ui_editor.exe, battle_editor.exe (the Qt editors beside the game)
    core/         the core game code (engine/ + game/): gameplay, story, combat, saves, UI
    third_party/  json, stb, miniaudio headers
```
