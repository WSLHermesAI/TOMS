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


---

### 2026-09-25 — Phase 2 step 1 (web build), adapted to WSL: presets, prerequisites, and a working web configure

Owner asked to do Phase 2 step 1 here rather than on the Windows box ("1 and after finish write progress
to today's progress report file"). The step as written targets Windows (emsdk at `D:\Work\emsdk`,
`check_env.ps1`, Visual Studio F5), so this entry records what the WSL/Linux adaptation needed and what
it produced. TOMS-side tooling rule applies: nothing under `/mnt/c`, no `cmd.exe`/`powershell.exe`.

**Done**

1. **`tools/check_env.sh`** — a WSL/Linux twin of `tools/check_env.ps1`, same facts, no popups: one line
   per prerequisite with the exact fix for a failure, then a pass/fail count. It found two real gaps:
   `cmake` was not on PATH (it lives at `$HOME/opt/cmake/bin`) and **ninja was missing**. ninja 1.13.2
   was installed without root via `uv tool install ninja`. Now **6 ok, 0 missing**: cmake 3.30.5,
   ninja 1.13.2, git, node (also the `CMAKE_CROSSCOMPILING_EMULATOR`), python3, and the Emscripten
   toolchain at `$HOME/opt/emsdk` — **Emscripten 6.0.6** where the Windows box has 6.0.9.
2. **`web-debug` / `web-release` presets** in `CMakePresets.json`: Ninja, the Emscripten toolchain file
   from `$env{EMSDK}`, `CMAKE_CROSSCOMPILING_EMULATOR=node`, `CMAKE_EXECUTABLE_SUFFIX=.js`, `WEB=ON`,
   conditioned to Linux. The desktop presets are untouched.
   *First attempt failed, and the reason is worth keeping:* the web presets were cloned from
   `windows-release` and therefore **inherited its `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER=cl.exe`**.
   CMake then detected no compiler, `CMAKE_SIZEOF_VOID_P` came back 0, and the prerequisite check
   stopped the configure with "A 32-bit compiler is active". The web presets are now **standalone** —
   no `inherits` — so the Emscripten toolchain supplies the compiler.
3. **`cmake/TomsPrerequisites.cmake`**: the x64 pointer-size error and the MSVC-version error are now
   gated behind `if(NOT WEB)`, and the platform notice is web-aware. This is not a loophole — Emscripten
   targets **wasm32, where 32-bit pointers are correct**, so a desktop x64 rule must not fail a
   legitimate web build. Without this, Phase 2 could never configure on any machine, Windows included.
4. **The web configure now succeeds**: `cmake --preset web-release` → `Configuring done (51.0s)` →
   `Generating done` → build files in `build-web-release`. FetchContent pulled bgfx.cmake
   v1.161.9510-579 (+ bx/bimg/shaderc), SDL3 3.4.8, Dear ImGui 1.90.9 and glm 1.0.1 into
   `build-web-release/_deps`. Qt6 is absent here, so `toms_editor` is skipped — correct, the editor is
   desktop-only and never shipped.

**In flight at the time of writing** — `cmake --build --preset web-release` (bgfx + shaderc + SDL3 +
ImGui compiled for wasm; long). Its outcome is not recorded here because it is not yet known.

**Still to do to close Phase 2 step 1**

- `game/src/main_web.cpp`: SDL3's browser main loop, plus a port of the browser glue in
  `src/engine/emscripten_main.cpp` — IDBFS saves, the Canvas-2D system-font path, and the JS test hooks.
  (`game/CMakeLists.txt` currently has only `add_executable(toms_game src/main_sdl.cpp)`; there is no
  web branch and no `main_web.cpp` yet.)
- The acceptance check as written: stage 1 plays in Chrome/Edge from `http://localhost:8099/...`, and a
  save survives a page reload (that is the part that needs IDBFS, so it depends on `main_web.cpp`).

**Note for the Phase 2 mobile row (decision, not work):** it is written as `setDesignSize` /
`g_uiScale` — the *old* approach of shrinking the logical design. The TOMS side now has `UiRoot` and
`Game::setUiScale()` implementing the owner's stated rule (grow the UI **objects**, keep the game
resolution, never soften the render). When that row is picked up it should use `UiRoot`, not
`setDesignSize`.

### 2026-09-25 (addendum) — the wasm build's first real blocker, and where it stopped

The `cmake --build --preset web-release` run launched with the first entry **failed at 16/1083**, and the
reason is one any bgfx-on-Emscripten build hits:

    em++: error: passing any of -msse, -msse2, -msse3, -mssse3, -msse4.1, -msse4.2, ... -mavx2, -mfma,
    -mfpu=neon flags also requires passing -msimd128 (or -mrelaxed-simd)!

Cause: bgfx.cmake compiles its `bx` utility library with x86 SIMD flags because it looks at the **host**
as x86_64, while the target is wasm32, where Emscripten rejects x86 SIMD flags outright.

Fix (in the presets, not inside the fetched tree, so a fresh configure cannot undo it): `web-debug` and
`web-release` now pass **`-msimd128`** in `CMAKE_C_FLAGS`, `CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS`.
That is what the error itself asks for, and it lets Emscripten lower those SSE paths to `simd128`.

**Result:** the fix is correct — the build went from 16 to **389+ object files of 1083** (bgfx, bx, bimg,
spirv-cross, SDL3, ImGui all compiling for wasm) before stopping again in the shaderc/spirv-cross region.
The second failure's error text was NOT captured: the rebuild's output was long enough to hit the call
timeout, and the tail only showed spirv-cross `-Wdeprecated-this-capture` warnings (benign, third-party,
63 of them) before `ninja: build stopped: subcommand failed`.

**Next action to close this out:** re-run with output on disk so the error survives --

    cmake --build --preset web-release 2>&1 | tee /tmp/web_next_build.log
    grep -nE "error:|FAILED:" /tmp/web_next_build.log | head

Then fix the specific failure (likely another Emscripten flag/target issue rather than source). Only after
the build links does `game/src/main_web.cpp` become the gating item for Phase 2 step 1 -- and until a web
executable exists, the "plays in Chrome from localhost:8099" acceptance check cannot be run at all.

### 2026-09-25 (addendum 2) — the second failure was the OOM killer, not the compiler

Capturing the build's output on disk paid off immediately. The run ended with

    build exit=137

137 = 128 + 9 = SIGKILL, i.e. the process was killed rather than failing to compile. It died compiling
`glslang/SPIRV/doc.cpp` with `-j$(nproc)`: bgfx's shader toolchain builds **glslang, tint (Dawn's) and
spirv-cross** at once, and those are very large translation units, so a full-parallelism build exhausts
this machine's RAM before it ever reaches the game code.

**Workaround:** build at low parallelism (`-j2`). This is an environment limit, not a defect in the
project -- a machine with more RAM builds it at full parallelism. Recorded because "exit 137" looks like a
random failure and would otherwise be re-diagnosed as one.

### 2026-09-25 (addendum 3) — the third stop: etcpak's x86 intrinsics, and what to do about it

`-j2` got much further: **364/422 objects and `bin/shaderc.js` LINKED**, so the shader compiler -- the
heaviest piece and the one Phase 2 actually needs -- is built for wasm. The build then failed compiling
**bimg's `etcpak/Dither.cpp`** with 20 errors, all from Emscripten's own `ia32intrin.h`:

    error: use of undeclared identifier '__builtin_ia32_readeflags_u32'
    error: use of undeclared identifier '__builtin_ia32_writeeflags_u32'
    error: use of undeclared identifier '__builtin_ia32_crc32qi' / crc32hi / crc32si
    error: use of undeclared identifier '__builtin_ia32_rdpmc' / rdtscp

etcpak is the texture **encoder** inside bimg. It reaches x86 intrinsic paths that have no wasm
equivalent, and `-msimd128` (which fixed bx) does not cover the *integer* builtins listed above -- those
are x86-only by definition.

Also worth recording for the memory story: `nproc` is 16 here and the box has 15.8 GB RAM, so `-j16` on
glslang/tint/spirv-cross translation units is what produced the exit-137 SIGKILL. `-j2` completes them.

**Options for closing it (in preference order), none attempted yet:**

1. **Exclude `bimg_encode` for the web target.** The game's 2D sprite pipeline does not encode textures at
   runtime -- the old build pre-encodes assets, and `BGFX_BUILD_TOOLS_TEXTURE` is already OFF. If
   bgfx.cmake's wrapper exposes (or can be given) a bimg-encode toggle, turning it off for web removes the
   problem instead of working around it. Check `cmake/bgfx.cmake`'s own options, not just bimg's
   `CMakeLists.txt` (which only has `if(BGFX_BUILD_TOOLS_TEXTURE)`).
2. **A small compat shim** for the missing builtins (they are only used for CPU feature/flag probing and
   CRC path selection), compiled into etcpak for wasm.
3. **Patch the fetched `bimg` source** to skip etcpak on `__EMSCRIPTEN__`. Last resort: a fresh configure
   silently undoes anything done inside `_deps/`.

**Not yet possible:** `game/src/main_web.cpp` does not exist, so there is still no web executable and the
Chrome/Edge acceptance check (stage 1 plays; a save survives a reload) has not been run.

### 2026-09-26 — Phase 2 step 1 on WSL/Linux: the web build now completes (exit 0)

Owner: "I have build a windows web version, now try to build a WSL web version."

**Result:** `cmake --preset web-release && cmake --build --preset web-release -j2` finishes with **exit 0**
and links, producing in `build-web-release/bin/`:

    toms_game.html     3,265 B
    toms_game.js     258,223 B
    toms_game.wasm 2,935,852 B   (2.9 MB; the Windows build is 2.8 MB)
    toms_game.data   737,853 B   (the --preload-file assets)

`python3 -m http.server 8100 --directory build-web-release/bin` serves it and the bytes are right
(`toms_game.wasm` -> HTTP 200, 2,935,852). The build went from **1083 steps with a wall of toolchain
failures to 257 steps, exit 0.**

**What was actually wrong (each fixed in the project's own files, never inside `_deps/`, because a fresh
configure would silently undo anything done there):**

| # | Symptom | Cause | Fix |
|---|---|---|---|
| 1 | `[code=137]` at 16/1083 | SIGKILL/OOM: 16 parallel glslang+tint+spirv-cross TUs on a 16-core/15.8 GB box | build with `-j2` (baked into the preset's `jobs`) |
| 2 | `em++: -msse4.2 also requires -msimd128` | bgfx.cmake applies x86 SIMD flags because it inspects the HOST, while the target is wasm32 | `-msimd128` in both hosts' presets (Windows needs it too) |
| 3 | configure aborts: `Invalid character escape '\b'` | **a bug in our CMake**: `cmake/TomsPrerequisites.cmake:248` had a single backslash in a message string (`tools\build.cmd`). Only reachable when no host shaderc exists -- the branch Windows never takes | backslash doubled |
| 4 | `Unable to open file '.../vs_sprite.sc'` | the fallback's shaderc runs under node in Emscripten's VIRTUAL filesystem, so it cannot open real source paths | `-sNODERAWFS=1` (tool only) |
| 5 | `RuntimeError: memory access out of bounds` in shaderc.wasm | fixed-size emscripten heap overflowing on a real shader | `-sALLOW_MEMORY_GROWTH=1`, `-sINITIAL_MEMORY`, `-sSTACK_SIZE` |
| 6 | `--preload-file cannot be used with NODERAWFS` | our own mistake: flag 4 was in the preset's GLOBAL linker flags, so it hit the game too. The two needs are opposite -- the tool must touch real files, the game must use the virtual FS for its preloaded assets | scoped to the shaderc target only, in `cmake/TomsDependencies.cmake` (`if(EMSCRIPTEN AND TARGET shaderc)`), after bgfx.cmake defines it |

**Environment limits found here (not project defects):**
- The **preferred** route -- importing a host (desktop) shaderc, which is what `build_web.cmd` does on
  Windows and which is 348 steps instead of 1083 -- **cannot be used on this WSL box**: bgfx.cmake's Linux
  configure calls `find_package(X11)` and X11 dev headers need root. A Linux box with those headers, or the
  Windows flow, takes the fast route unchanged.
- Also needed once: `cmake` on PATH (`$HOME/opt/cmake/bin`) and **ninja** (`uv tool install ninja`, no root).

**NOT verified -- and this is the gap:** the build has **not been run in a browser**. The step's acceptance
check ("stage 1 plays in Chrome/Edge from `http://localhost:8099/...` and a save survives a page reload")
needs a browser: `tools/web_smoke_test.mjs` requires Chrome/Edge (absent on this box) and the
Firefox/Selenium fallback could not be used because this machine currently cannot reach pypi.org
(`invalid peer certificate: UnknownIssuer`; `github.io` likewise returns HTTP 000). So: build proven,
serving proven, **rendering unproven**. To close it, on a machine with Chrome:

    python3 -m http.server 8100 --directory build-web-release/bin
    node tools/web_smoke_test.mjs http://127.0.0.1:8100/toms_game.html /tmp/shots 1280,720

**Note for the Linux build:** `tools/check_env.sh` is the WSL/Linux twin of `check_env.ps1`; the web
presets are `web-debug`/`web-release` (Linux) and `web-debug-windows`/`web-release-windows` (Windows), all
reading the toolchain from `$env{EMSDK}`.
