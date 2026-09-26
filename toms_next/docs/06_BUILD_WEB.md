# 06 — Build the web (browser) version

The web version is the same `toms_game` as the desktop game: SDL3 + bgfx + the unchanged TOMS game
code, compiled to WebAssembly with Emscripten. In the browser, bgfx draws with **WebGL2**. No
plugin or install is needed; it runs in current Chrome, Edge, Firefox and Safari.

## Quick start (Windows)

```bat
cd TOMS\toms_next
tools\build_web.cmd            :: builds build-web-release-windows\bin\toms_game.html
tools\serve_web.cmd            :: serves it and opens http://localhost:8099/toms_game.html
```

Run both from **cmd, PowerShell or Explorer** (double-click). Not from Git Bash: launched from
there, the emsdk setup does not take effect and the script stops with "emsdk not found".

The first `build_web.cmd` can take a while (see §2, step 3). Later builds are incremental.

## 1. What you need

| What | Why | Get it | Checked by |
|---|---|---|---|
| Everything for the desktop build ([02](02_INSTALL_WINDOWS.md) §1) | Visual Studio's CMake + Ninja; a desktop build supplies the host `shaderc.exe` | see 02 | `tools\check_env.cmd` |
| **Emscripten SDK (emsdk)** | the C++ → WebAssembly compiler | https://emscripten.org/docs/getting_started/downloads.html | `check_env.cmd` ("Emscripten SDK"), `build_web.cmd` |
| A browser | to play it | Chrome / Edge / Firefox | — |

**Installing emsdk** (once):

```bat
cd D:\Work
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
emsdk install latest
emsdk activate latest
```

`build_web.cmd` and `check_env.cmd` look for emsdk in this order: the `EMSDK` environment
variable, `..\..\emsdk` next to the TOMS checkout (on this machine `D:\Work\emsdk`),
`%USERPROFILE%\emsdk`, `C:\emsdk`, `D:\emsdk`. If yours is elsewhere, set `EMSDK` to its folder.
If none is found, a popup explains how to install it and links to the download page.

Tested with **Emscripten 6.0.9**. The configure warns if Emscripten is older than 3.1.60.

You do **not** need Node.js or Python separately: emsdk contains both, and `serve_web.cmd` uses
emsdk's Python for the local web server.

## 2. What `build_web.cmd` does

1. **Finds and loads emsdk** (`emsdk_env.bat`), so `emcc` and emsdk's `node` are on PATH.
2. **Finds CMake and Ninja**: Visual Studio's copies (through `vswhere`), otherwise PATH.
3. **Finds a host `shaderc.exe`** in `out\build\*\bin\`. Shaders are compiled on *this* machine
   at build time, so the web build needs a Windows `shaderc.exe`, the same one the desktop build
   uses. **If none exists yet, it first builds the `windows-shipping` desktop preset** to get one.
   That is the slow part of a first run.
4. **Configures and builds** the preset `web-release-windows` (or `web-debug-windows`), passing
   the shaderc path.

Build options:

| Command | Preset | Output folder | Use |
|---|---|---|---|
| `tools\build_web.cmd` or `tools\build_web.cmd release` | `web-release-windows` (Release) | `build-web-release-windows\bin\` | what you publish (~3.8 MB total) |
| `tools\build_web.cmd debug` | `web-debug-windows` (Debug, DWARF) | `build-web-debug-windows\bin\` | stepping through C++ in Chrome DevTools |

Set `TOMS_NO_POPUPS=1` before running it to suppress the prerequisite popups (build servers).

### Output

| File | Size (release) | What |
|---|---|---|
| `toms_game.html` | 3 KB | the page (from `game/web/shell.html`) |
| `toms_game.js` | 0.25 MB | Emscripten's loader |
| `toms_game.wasm` | 2.8 MB | the game |
| `toms_game.data` | 0.7 MB | `TOMS/assets` + `TOMS/data`, mounted at `/assets` and `/data` (fonts and old `.spv` shaders left out) |

These four files are the whole game. Copy them to any static web host.

## 3. Play it

```bat
tools\serve_web.cmd              :: release build, port 8099
tools\serve_web.cmd debug 8100   :: debug build on another port
```

Browsers refuse to run WebAssembly from `file://`, so opening `toms_game.html` by double-click
does not work. Use the server. Stop it with Ctrl+C.

URL options (the web equivalent of the desktop command line):

| URL | Effect |
|---|---|
| `toms_game.html?stage=stage03` | start on another stage |
| `toms_game.html?stats` | bgfx on-screen stats |
| `toms_game.html?keys=enter@60` | press Enter at frame 60 (automated tests) |

Controls are the same as desktop (arrows/WASD, Enter, I, B, Tab, F1, Esc). Touch and mouse work
through the on-screen pad; holding an arrow plate keeps walking. The page also has a
fullscreen button and a 背包 (backpack) button.

## 4. How the web build works

| Topic | Web behaviour | Where |
|---|---|---|
| Entry point | the same `main_sdl.cpp`; the browser calls one frame per display refresh (`emscripten_set_main_loop_arg`) instead of a while-loop | `game/src/main_sdl.cpp` |
| Window | SDL3 `SDL_WINDOW_FILL_DOCUMENT`: the canvas fills the page; the game letterboxes 1024×768 inside it | `main_sdl.cpp` |
| Renderer | bgfx `OpenGL ES 3.0` = WebGL2; the canvas selector (`#canvas`) is passed as the window handle | `main_sdl.cpp`, `engine/src/bgfx_host.cpp` |
| Shaders | only the ESSL (WebGL2) profile is compiled, by the host `shaderc.exe`, and embedded | `engine/CMakeLists.txt`, `cmake/TomsDependencies.cmake` |
| Colours | no sRGB (WebGL has no sRGB backbuffer); same look as the old WebGL build, darker than desktop | `bgfx_renderer.cpp`, `bgfx_host.cpp` |
| Text | the browser's own fonts through Canvas 2D (`Font::buildFromCanvas`), so no font file ships | legacy `src/engine/font.cpp` |
| Old renderer | `compat/renderer_webgl.h` makes the legacy `new WebGLRenderer()` build the bgfx renderer | `game/compat/` |
| Saves | IndexedDB mounted at `/save` (IDBFS); the legacy save code syncs after each write; slots re-read when the initial load finishes | `main_sdl.cpp` (`jsRefreshSlots`) |
| Phones | viewport < 900×560 css px: pad plates ×1.2, UI ×1.5 (game resolution unchanged); portrait shows "rotate your device" | `main_sdl.cpp`, `game/web/shell.html` |
| JS hooks | `jsRefreshSlots`, `jsInventory`, `jsFrameCount`, `jsTitleOpen` (callable with `Module.ccall`) | `main_sdl.cpp` |

Why the build uses a host `shaderc.exe`: otherwise bgfx.cmake compiles glslang, tint,
spirv-cross and shaderc themselves to WebAssembly and runs them under node. That is roughly three
times as much to compile, it ran out of memory at full parallelism (WSL, 2026-09-25), and it pulls
in `bimg_encode`, whose etcpak code does not compile for wasm. With a host shaderc the web build
has 348 steps. `bimg_encode` is also excluded from the default build, since nothing we ship
encodes textures.

## 5. Testing and debugging

**Automated smoke test** (headless Chrome or Edge, no clicking): start the server, then

```bat
tools\serve_web.cmd
D:\Work\emsdk\node\24.19.0_64bit\node.exe tools\web_smoke_test.mjs http://127.0.0.1:8099/toms_game.html out\web_smoke
```

It loads the page, presses Enter (new game), opens the in-game menu and saves, reloads the page,
and checks that the save came back from IndexedDB. It prints the game state after each step and
writes screenshots to the given folder. Exit code 1 if the game never started. An optional third
argument sets the window size, e.g. `844,390` for a phone in landscape. Page code and tests must
wait for `window.tomsReady` before calling `Module.ccall`: debug builds abort if C++ is called
before the runtime is initialised.

- **C++ breakpoints in the browser:** build `tools\build_web.cmd debug`, serve it with
  `tools\serve_web.cmd debug`, and install the Chrome extension *C/C++ DevTools Support (DWARF)*.
  The C++ sources then appear under DevTools → Sources.
- **Console output** (`printf`, the game log, bgfx messages) goes to the browser console (F12).
  A fatal error is also shown on the page.
- **Harmless console noise:** `WebGL: INVALID_ENUM: getInternalformatParameter` (bgfx probing
  texture formats at startup), `ScriptProcessorNode is deprecated` and "AudioContext was not
  allowed to start" (audio starts after the first click or key, as browsers require).
- Almost everything can be debugged in the desktop build in Visual Studio instead. Only
  browser-specific issues (IndexedDB, touch, canvas size) need DevTools.

## 6. Linux / WSL

The presets `web-debug` / `web-release` are the Linux twins (`tools/check_env.sh` checks the
prerequisites). On Linux there is usually no host `shaderc`, so the build falls back to compiling
shaderc to wasm and running it with node: build with few jobs (`--parallel 2`) on a 16 GB machine.
Pass `-DTOMS_HOST_SHADERC=<path>` to use a native Linux shaderc instead. **This path has not been
re-run since the `bimg_encode` fix**; the Windows path is the tested one.

## 7. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Popup / console "Emscripten SDK (emsdk) not found" | emsdk not installed, not in a searched folder, or the script was started from Git Bash | install emsdk (§1); set `EMSDK`; run from cmd/PowerShell/Explorer |
| `build_web.cmd` jumps to a wrong label or error | the `.cmd` file lost its CRLF line endings (cmd.exe misparses LF-only batch files after a `call`) | `toms_next/.gitattributes` keeps `*.cmd` as CRLF; re-checkout the file |
| First build takes long | no host `shaderc.exe` yet, so the desktop shipping preset is built first | expected once |
| Configure warning "No host shaderc found" | building without a desktop build | run `tools\build.cmd windows-shipping` once, or pass `-DTOMS_HOST_SHADERC=` |
| Page stays on "Loading…" or is black | opened from `file://`, or the `.data` file is stale in the browser cache | use `serve_web.cmd`; hard-reload (Ctrl+F5) |
| Continue shows no saves after reload | private browsing (no IndexedDB): saves last for the session only | use a normal window |
| No sound | browsers block audio until the first user input | click or press a key |
| `This version of cmake does not support emscripten shared libraries` warnings | Emscripten's toolchain file talking about shared libs, which we do not use | ignore |
