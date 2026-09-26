# 02 — Install everything (Windows)

This page lists everything `toms_next` needs, where to get it, and how to check it. Items marked
**required** are needed for the game (`toms_game.exe`). **Qt** is only needed for the editor
(`toms_editor.exe`).

## 0. Check what you already have

Double-click **`toms_next\tools\check_env.cmd`** (or run it in a terminal).

- It checks every item on this page and prints `[ OK ]`, `[MISS]` (required) or `[WARN]` (optional).
- If anything is missing, a **popup** lists it with the fix and the download link, and offers to
  open the download pages in your browser.
- `tools\check_env.cmd -NoGui` prints the same report without the popup (for CI).
- Exit code 1 means something *required* is missing.

The same checks run again automatically when CMake configures the project (inside Visual Studio or
from `tools\build.cmd`). A missing **required** item stops the configure with a popup; a missing
**optional** item (Qt, the CJK font) shows a popup once and skips only the part that needs it.

## 1. Required

| # | What | Why | Get it | How the check finds it |
|---|---|---|---|---|
| 1 | **Windows 10 or 11, 64-bit** | target platform | — | OS version |
| 2 | **Visual Studio 2022 or newer** (Community is free) with the workload **"Desktop development with C++"** | MSVC compiler, debugger | https://visualstudio.microsoft.com/downloads/ | `vswhere` component `VC.Tools.x86.x64` |
| 3 | VS component **"C++ CMake tools for Windows"** | CMake + Ninja inside VS, *Open Folder* support | VS Installer → Modify → Individual components | `vswhere` component `VC.CMake.Project` |
| 4 | **Windows 11 SDK** (any 10.0.2xxxx) | Windows headers; `d3dcompiler_47.dll` for shader compiling | VS Installer → Individual components, or https://developer.microsoft.com/windows/downloads/windows-sdk/ | registry `Windows Kits\Installed Roots` |
| 5 | **CMake 3.24+** | build system | bundled with item 3; or https://cmake.org/download/ | VS copy first, then `PATH` |
| 6 | **Ninja** | build tool used by the presets | bundled with item 3; or https://github.com/ninja-build/ninja/releases | VS copy first, then `PATH` |
| 7 | **Git for Windows** | the first configure clones bgfx, SDL3, Dear ImGui and glm | https://git-scm.com/download/win | `git` on `PATH` |
| 8 | **Internet access to github.com** (first configure only) | same as 7 | — | HTTPS request to github.com |
| 9 | **The TOMS checkout** around `toms_next/` | the game code (`../src`), `../assets`, `../data`, `../external`, `../editor/src` are reused | `git clone https://github.com/WSLHermesAI/TOMS` | files exist |
| 10 | **~5 GB free disk** | bgfx + tools + two configurations | — | drive free space |

### Visual Studio: what to tick

In the **Visual Studio Installer** → *Modify*:

- Workloads tab: **Desktop development with C++**
- On the right, under *Installation details*, keep these ticked (they are on by default):
  - **MSVC v143 (or newer) – VS 2022 C++ x64/x86 build tools**
  - **Windows 11 SDK**
  - **C++ CMake tools for Windows**

Visual Studio 2026 (v18) also works: its bundled CMake 4.x and Ninja are used automatically.

## 2. Optional

| What | Needed for | Get it | Notes |
|---|---|---|---|
| **Qt 6.5+ — kit "MSVC 2022 64-bit"** (6.8 LTS recommended) | `toms_editor.exe` only | Qt Online Installer: https://www.qt.io/download-qt-installer-oss | Needs a free Qt account. See below. |
| **WenQuanYi Zen Hei** font (`wqy-zenhei.ttc`) | the shipped CJK glyph look | https://sourceforge.net/projects/wqy/files/wqy-zenhei/ | It is gitignored (16 MB). Without it the game uses Windows' Microsoft JhengHei (`msjh.ttc`) and says so in the console. |
| **Emscripten SDK (emsdk)** | the web build only ([06](06_BUILD_WEB.md)) | https://emscripten.org/docs/getting_started/downloads.html | `git clone` it, then `emsdk install latest` + `emsdk activate latest`. Auto-detected next to the TOMS checkout (`D:\Work\emsdk`), in `%USERPROFILE%\emsdk`, `C:\emsdk`, or via `EMSDK`. |
| **Vulkan runtime** | `--renderer=vulkan` | comes with current NVIDIA/AMD/Intel drivers | Direct3D 11/12 work without it. |
| **RenderDoc** | GPU frame capture of bgfx | https://renderdoc.org/ | |

### Qt: step by step

1. Download and run the **Qt Online Installer** (https://www.qt.io/download-qt-installer-oss).
   Log in or create a free account; choose **open-source use** (LGPLv3).
2. *Installation folder*: keep **`C:\Qt`**. The project also auto-detects `D:\Qt` and
   `%USERPROFILE%\Qt`.
3. *Select components* → **Qt → Qt 6.8.x** (or newer) → tick **MSVC 2022 64-bit**.
   Nothing else is needed (no MinGW, no Android, no sources).
4. If you installed Qt anywhere else, set an environment variable **`QTDIR`** to the kit folder,
   for example `E:\Tools\Qt\6.8.3\msvc2022_64`, then restart Visual Studio.
5. Run `tools\check_env.cmd` again: the Qt line should show the kit path.

**MinGW kits do not work.** They cannot link with MSVC. The check and the CMake configure both
warn if only a MinGW kit is found.

Qt licence note: the editor links Qt dynamically under the LGPLv3. It is an internal tool and is
never shipped to players, so the shipped game has no Qt obligations at all.

### CJK font: step by step

1. Download `wqy-zenhei-0.9.45.tar.gz` from https://sourceforge.net/projects/wqy/files/wqy-zenhei/
2. Extract it (Windows 11 can open `.tar.gz`; otherwise use 7-Zip).
3. Copy `wqy-zenhei.ttc` to `TOMS\assets\wqy-zenhei.ttc`.

Any other `.ttf` / `.ttc` with Traditional Chinese glyphs can be used through the environment
variable **`TOMS_FONT`** (full path to the font file).

## 3. Things you do NOT need to install

These are downloaded and built by CMake on the first configure (pinned versions, see
`cmake/TomsDependencies.cmake`):

| Library | Version | Used for |
|---|---|---|
| bgfx (+ bx, bimg, shaderc) via bgfx.cmake | v1.161.9510-579 | rendering; shaderc compiles `engine/shaders/*.sc` |
| SDL3 | release-3.4.8 | game window, input, main loop |
| Dear ImGui | v1.90.9 | the game's dev windows (same version as the old build) |
| glm | 1.0.1 | math in `node.h` (the old build needed a hand-installed copy via `GLM_DIR`) |

The Vulkan SDK is **not** needed (bgfx loads Vulkan at runtime). Install it only if you want the
Vulkan validation layers.

## 4. After installing

1. `tools\check_env.cmd` → everything required shows `[ OK ]`.
2. Open the folder in Visual Studio: [03_VISUAL_STUDIO.md](03_VISUAL_STUDIO.md).
3. Or build from a terminal: `tools\build.cmd windows-release`.
