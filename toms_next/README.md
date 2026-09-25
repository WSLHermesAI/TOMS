# toms_next: TOMS on Qt6 + bgfx

The split of *Tower of the Sorcerer* (TOMS) into:

| Executable | Built from | Ships? |
|---|---|---|
| **`toms_game.exe`** | SDL3 window + **bgfx** renderer + the existing game code | **yes**: contains no Qt |
| **`toms_editor.exe`** | **Qt6** editor with an embedded bgfx view that runs the same game, plus the old stage editor | no, internal tool |

Everything new lives in this folder. The original project around it (`TOMS/src`, `assets`,
`data`, `editor`) is reused **without edits**, and the old Vulkan build keeps working.

## Quick start (Windows)

1. **Check prerequisites:** double-click `tools\check_env.cmd`. Anything missing is listed in a
   popup with its download link. Details: [docs/02_INSTALL_WINDOWS.md](docs/02_INSTALL_WINDOWS.md).
2. **Open in Visual Studio 2022 or newer:** *File → Open → Folder…* → this `toms_next` folder.
   The first configure downloads and builds bgfx, SDL3, Dear ImGui and glm (a few minutes).
3. Pick **Windows x64 Debug** (or Release), choose **toms_game** or **toms_editor** as the startup
   item, and press **F5**. Details: [docs/03_VISUAL_STUDIO.md](docs/03_VISUAL_STUDIO.md).

Without Visual Studio open: `tools\build.cmd windows-release`.

## Documents

| Doc | Contents |
|---|---|
| [01_ARCHITECTURE.md](docs/01_ARCHITECTURE.md) | targets, how the old code runs on bgfx unmodified, one frame, Qt + bgfx embedding, shaders |
| [02_INSTALL_WINDOWS.md](docs/02_INSTALL_WINDOWS.md) | every prerequisite with links, the checker and popups, Qt and font setup |
| [03_VISUAL_STUDIO.md](docs/03_VISUAL_STUDIO.md) | open, configure, F5 targets, command-line options, smoke tests, debugging |
| [04_MIGRATION_PLAN.md](docs/04_MIGRATION_PLAN.md) | phases from the Vulkan project to the full Qt + bgfx engine |
| [05_TROUBLESHOOTING.md](docs/05_TROUBLESHOOTING.md) | configure, build and runtime problems |

The long-term engine design is in `../docs/EngineBlueprint/`.

## Layout

```
toms_next/
  CMakeLists.txt  CMakePresets.json  launch.vs.json
  cmake/    TomsPrerequisites.cmake (checks + popups), TomsDependencies.cmake (pinned downloads)
  engine/   toms_bgfx: BgfxRenderer (IRenderer), bgfx host, ImGui on bgfx, shaders/*.sc
  game/     compat/ (renderer.h shim), GameSession, main_sdl.cpp -> toms_game.exe
  editor/   BgfxViewport (QWidget), EditorWindow -> toms_editor.exe
  tools/    check_env.cmd/.ps1, build.cmd, show_message.ps1
  docs/
```
