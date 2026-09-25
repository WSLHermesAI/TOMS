# 03 — Build, run and debug in Visual Studio

`toms_next` is a CMake project. Visual Studio opens it directly with **Open Folder**; there is no
`.sln` to generate or commit.

## 1. Open the project

1. Run `tools\check_env.cmd` once (see [02](02_INSTALL_WINDOWS.md)).
2. Visual Studio → **File → Open → Folder…** → select **`TOMS\toms_next`**.
   Open `toms_next`, not the TOMS root: the root folder is the old Vulkan project.
3. Visual Studio reads `CMakePresets.json` and starts configuring. Watch **View → Output →
   CMake**.
   - The **first configure downloads and builds bgfx, SDL3, Dear ImGui and glm** (a few minutes).
     Later configures are fast.
   - If a prerequisite is missing, a popup explains what and where to get it
     ([02](02_INSTALL_WINDOWS.md)). The same text is in the CMake output.

## 2. Pick a configuration

The toolbar's configuration dropdown lists the presets:

| Preset | Builds | Use it for |
|---|---|---|
| **Windows x64 Debug (game + editor)** | `toms_game`, `toms_editor` | stepping through code |
| **Windows x64 Release (game + editor)** | same, optimized with debug info | normal play testing |
| **Windows x64 Shipping (game only, no Qt)** | `toms_game` only; Qt is not searched for | what ships; works on machines without Qt |
| CI (no popups) | like Release, never shows popups | build servers |

Build outputs go to `toms_next\out\build\<preset>\bin\`.

## 3. Run and debug (F5)

Choose the startup item in the **Select Startup Item** dropdown (next to the green ▶):

| Startup item | What it runs |
|---|---|
| **toms_game (auto renderer)** | the game; bgfx picks the best backend (Direct3D 11/12 on Windows) |
| **toms_game (Direct3D 11)** | forces `--renderer=d3d11` |
| **toms_game (Vulkan)** | forces `--renderer=vulkan` |
| **toms_editor (Qt + bgfx)** | the editor; its *Play (bgfx)* tab runs the same game code |

These come from `launch.vs.json`. If the dropdown only shows `toms_game.exe` / `toms_editor.exe`,
that works too (default arguments).

Breakpoints in the old game code (`TOMS\src\game\...`) hit in **both** executables, because both
link the same `toms_legacy_game` library.

### Game controls

Arrows/WASD move · Enter/Space interact/attack · F defend · G super · H active · I inventory ·
B store · Tab stage select · F1 debug overlay · F2 styling spike · Esc menu/back.

### Editor

- **Play (bgfx)** tab: click the view to give it the keyboard, then play as in the game.
- **Stages** dock: double-click a stage to load it into the running game.
- **Session** dock: renderer name, fps, quad/draw-call counts, *Restart game*, the debug overlay
  and bgfx stats toggles.
- **Stage editor** tab: the existing Qt stage editor (`TOMS\editor\src`), embedded unchanged.

## 4. Command-line options (Debug → *Debug and Launch Settings*, or a terminal)

| Option | Meaning |
|---|---|
| `--renderer=auto\|d3d11\|d3d12\|vulkan\|opengl` | bgfx backend (editor: environment variable `TOMS_RENDERER`) |
| `--assets=<dir>` | the TOMS `assets` folder (default: environment `ASSET_DIR`, else the checkout's `assets`) |
| `--stage=<id>` | first stage, e.g. `stage03` |
| `--no-vsync` | uncapped frame rate |
| `--stats` | bgfx on-screen stats |
| `--frames=<n> --screenshot=<file.png>` | run n frames, save a PNG, quit (smoke tests) |
| `--keys=enter@30,enter@60` | press keys at given frames (smoke tests) |

Environment variables still honoured from the old build: `TOMS_FONT`, `TOMS_HIDE`,
`TOMS_SPLIT_NODE`, `TOMS_RENDER_DEBUG`.

## 5. Build without opening Visual Studio

```bat
toms_next\tools\build.cmd                 :: windows-release
toms_next\tools\build.cmd windows-debug
toms_next\tools\build.cmd windows-shipping
```

The script finds Visual Studio with `vswhere`, loads the MSVC x64 environment, and uses Visual
Studio's bundled CMake and Ninja.

Smoke tests (render the title screen, then stage 1, and write PNGs; the editor also writes
`<name>_window.png` with the whole window):

```bat
cd toms_next\out\build\windows-release\bin
toms_game.exe --frames=60 --screenshot=title.png
toms_game.exe --frames=120 --keys=enter@30 --screenshot=stage.png
toms_editor.exe --frames=200 --screenshot=editor.png
```

## 6. Debugging tips

- **GPU capture:** start RenderDoc, *Launch Application* → `toms_game.exe`. With `--renderer=d3d11`
  captures are the most reliable.
- **Which backend is running?** The console prints `bgfx ... ready: renderer=Direct3D 11`; the
  editor shows it in the *Session* dock.
- **Shaders:** edit `engine\shaders\*.sc` and build. shaderc recompiles them into headers that are
  embedded in the exe, so there are no shader files to copy.
- **Hot keys in the editor do nothing?** Click the game view first; it needs keyboard focus.
- Problems: [05_TROUBLESHOOTING.md](05_TROUBLESHOOTING.md).
