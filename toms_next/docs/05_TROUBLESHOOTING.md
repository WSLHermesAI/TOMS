# 05 — Troubleshooting

Start with `tools\check_env.cmd`: most problems are a missing prerequisite, and the checker names
it and links the download. Web-build problems are in [06 §7](06_BUILD_WEB.md#7-troubleshooting).

## Configure (CMake) problems

| Symptom | Cause | Fix |
|---|---|---|
| Popup "required tools are missing" | an item from [02 §1](02_INSTALL_WINDOWS.md#1-required) is missing | install it, then *Project → Delete Cache and Reconfigure* |
| `The C++ compiler is not MSVC` | the folder was configured outside a VS developer environment | open it in Visual Studio, or use `tools\build.cmd` |
| `Could not find git` / clone fails | Git missing or no network on the first configure | install Git; check proxy settings; retry the configure |
| `Filename too long` while cloning bgfx | deep third-party paths on a long checkout path | enable long paths (see check_env's advice) and `git config --global core.longpaths true`, or move TOMS nearer the drive root |
| Popup "optional tools are missing: Qt" | no Qt 6 MSVC kit found | install it ([02 §2](02_INSTALL_WINDOWS.md#qt-step-by-step)) or set `QTDIR`; the game still builds |
| `Qt kit ... is not an MSVC build` | only a MinGW kit is installed | add the *MSVC 2022 64-bit* kit in the Qt Maintenance Tool |
| The popup does not come back after I fixed nothing | it shows once per distinct problem list | the text is always in *Output → CMake*; delete the cache to see the popup again |
| Popups on a build server | — | use the `ci-windows` preset (`TOMS_POPUP_WARNINGS=OFF`) |

## Build problems

| Symptom | Cause | Fix |
|---|---|---|
| shaderc error in `*.sc` | shader syntax | the message shows the file and line; bgfx shader rules: https://bkaradzic.github.io/bgfx/tools.html#shader-compiler-shaderc |
| `d3dcompiler_47.dll` not found while compiling shaders | Windows SDK missing | install the Windows 11 SDK |
| C4819 / garbled CJK strings | a target compiled without `/utf-8` | every toms target calls `toms_target_defaults()`; add it to new targets |
| `renderer.h: vulkan/vulkan.h not found` | `game/compat` is not first on the include path of a new target that includes the legacy game headers | link `toms_legacy_game` (it carries the include order) instead of adding `../src` include paths by hand |
| `windeployqt not found` warning | Qt `bin` folder not found | add `<Qt kit>\bin` to `PATH`, or copy the Qt DLLs next to `toms_editor.exe` |

## Runtime problems

| Symptom | Cause | Fix |
|---|---|---|
| Popup "graphics could not start" | the chosen bgfx backend failed | update the GPU driver; try `--renderer=d3d11` or `--renderer=opengl` |
| Popup "Game assets were not found" | the exe cannot find `TOMS\assets` | keep toms_next inside TOMS, or pass `--assets=<path>` / set `ASSET_DIR` |
| Popup "No CJK font is available" | `wqy-zenhei.ttc` missing and no `msjh.ttc` | [02 §2](02_INSTALL_WINDOWS.md#cjk-font-step-by-step), or set `TOMS_FONT` |
| Console: "Microsoft JhengHei (msjh.ttc) is used instead" | `wqy-zenhei.ttc` missing | fine for development; install the font for the shipped look |
| Text missing, console: "atlas ... exceeds the GPU limit" | the glyph atlas is bigger than the GPU's max texture size | reduce the glyph set / cell size (the old renderers failed silently here) |
| Editor starts, then "toms_editor.exe - Qt6Core.dll was not found" | Qt DLLs not next to the exe | rebuild (windeployqt runs after every link), or add `<Qt kit>\bin` to `PATH` |
| Editor: keys do nothing | the game view has no keyboard focus | click the *Play (bgfx)* view |
| Editor: black view after undocking or moving the view | the native window was recreated | keep the viewport as the central widget/tab ([01 §5](01_ARCHITECTURE.md#5-qt--bgfx-in-the-editor)) |
| Saves are not where the old build put them | the working directory differs | saves go to `save\` in the working directory (`out\build\<preset>\bin` under F5) |
| Screenshot or screen-recording tool shows the editor's game view as white or black | tools that capture a window through GDI cannot read a DXGI flip-model swap chain | capture the screen or desktop region instead (the editor's own `--screenshot` does this), or use `toms_game --screenshot` / RenderDoc |
| Text edges look one pixel different between `--renderer=vulkan` and Direct3D | backend rasterisation rules | expected; compare golden images per backend |
