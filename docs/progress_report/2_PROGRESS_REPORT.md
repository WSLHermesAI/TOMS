# Progress Report — Log Part 2 (2026-09-26)

> Chronological log entries, oldest first. The live **Next Step**, status table, waiting list and
> open questions are in [PROGRESS_REPORT.md](PROGRESS_REPORT.md). Part 1 (2026-09-25) ended with the
> WSL web build stopped at etcpak.

---

### 2026-09-26 — Phase 2 step 1 done on Windows: the web build builds, runs and saves

Owner: "check toms_next\doc\progress_report then build a web version and make a document for how to
build a web version" (emsdk is at `D:\Work\emsdk`).

**Starting point (from part 1's WSL entries):** web presets existed and the configure passed, but
the build compiled bgfx's shader tools (glslang, tint, spirv-cross, shaderc) to wasm and then stopped
in etcpak (`bimg_encode`) on x86-only intrinsics; there was no web entry point yet.

**Two decisions that removed the blockers instead of patching around them:**

1. **Host shaderc.** shaderc is a build tool, so it has to run on the build machine. The web build
   now declares `bgfx::shaderc` as an IMPORTED executable pointing at the desktop build's
   `shaderc.exe` (before bgfx.cmake is processed, because bgfx.cmake only defines
   `bgfx_compile_shaders()` when that target exists) and turns bgfx's tools off. The web build
   went from 1083 steps (WSL) to **348**. `cmake/TomsPrerequisites.cmake` finds the host shaderc
   under `out/build/*/bin/`; `tools\build_web.cmd` builds the desktop shipping preset first if
   there is none. Without one (Linux/WSL), it falls back to the old shaderc-as-wasm route.
2. **`bimg_encode` excluded from the default build.** bgfx.cmake always defines it (texture
   encoders for texturec), so `cmake --build` compiled it. Nothing we ship encodes textures, so it
   is `EXCLUDE_FROM_ALL` now. That fixes the etcpak failure on every host.

**Web entry point.** `main_sdl.cpp` is restructured into `appInit` / `appFrame` / `appShutdown`:
a while-loop on desktop, `emscripten_set_main_loop_arg` in the browser (state on the heap, since
main() returns). Ported from `src/engine/emscripten_main.cpp`:
- IDBFS at `/save` + `Module.__tomsSyncfs` (called by the legacy `save_slots.cpp`) + `jsRefreshSlots`
- phone scaling: viewport < 900×560 css → `setPadScale(1.2)`, `setUiScale(1.5)` (the owner's
  "grow UI objects, keep the resolution" rule, via new `SessionOptions` fields)
- hold-to-repeat taps (phase 1 after 320 ms, every 150 ms) in `GameSession`, which the on-screen
  pad needs; desktop mouse gets the same behaviour
- page chrome in a new `game/web/shell.html`: fill-page canvas, loading %, readable errors, rotate
  hint, fullscreen and 背包 buttons
- URL options (`?stage=`, `?keys=`, `?stats`) as the web version of the command line

Also: `compat/renderer_webgl.h` (the legacy code does `new WebGLRenderer()` under Emscripten), ESSL
as the only shader profile on web, no sRGB on web (WebGL has no sRGB backbuffer; same look as the
old WebGL build), `-fexceptions` for our targets (the legacy JSON code catches), the prerequisite
notice for web turned from a WARNING into a STATUS line (it would have popped up on every
configure), the font / Qt / d3dcompiler checks skipped for web, and web checks added to
`check_env.ps1` (emsdk + version, host shaderc).

**Bugs found on the way, all fixed:**

| Symptom | Cause | Fix |
|---|---|---|
| `build_web.cmd` said "emsdk not found" although `D:\Work\emsdk` was found | the Write tool saved the `.cmd` files with **LF** line endings; cmd.exe mis-resumes after `call` in LF-only batch files | converted `tools\*.cmd` to CRLF; `toms_next/.gitattributes` keeps `*.cmd`/`*.bat`/`*.ps1` CRLF |
| same message when launched from Git Bash | Git Bash's environment; not a user path | documented: run from cmd / PowerShell / Explorer |
| page error "Invalid regular expression: /^?/" | `EM_ASM` makes the JS a C string, so `\?` lost its backslash | `location.search.substring(1)`, no regex |
| **debug** web build aborted at start: "This call only makes sense if used with multi-threaded renderer" | `bgfx::renderFrame()` before `init` is only valid for multi-threaded bgfx; Emscripten's is single-threaded; only debug builds assert | `#if BGFX_CONFIG_MULTITHREADED` around the call |
| debug build aborted when the page called C++ early | Emscripten debug builds abort on `ccall` before the runtime is initialised | `window.tomsReady` set in `onRuntimeInitialized`; the backpack button and tests wait for it |
| web "release" `.wasm` was 33 MB | the preset was RelWithDebInfo (DWARF) | `web-release-windows` is now Release: **2.8 MB** (the old WebGL build: 2.72 MB) |

Separate incident, fixed at once: a PowerShell bulk replace with a single-pair array flattened and
replaced every `i` with `f` in `TomsPrerequisites.cmake`. The file had no other uncommitted change,
so it was restored with `git checkout` and edited again with the Edit tool.

**Verified (Windows 11, Emscripten 6.0.9, headless Chrome through the DevTools protocol):**

- `tools\build_web.cmd release` and `debug` both build; the desktop `ci-windows` build and its
  smoke test still pass (D3D11, no leaks).
- In Chrome, **both** web builds: bgfx reports `OpenGL ES 3.0` (WebGL2); title screen with
  browser-font CJK text → Enter starts stage 1 (`slot1.json` written) → Esc opens the in-game menu
  → Enter saves → **page reload restores `settings.json` + `slot1.json` from IndexedDB**.
- Phone-size viewport (844×390): the small-screen path runs (pad ×1.2, UI ×1.5) and stage 1 renders.
- Output (release): `toms_game.html` 3 KB, `.js` 0.25 MB, `.wasm` 2.8 MB, `.data` 0.7 MB.
- The test is kept as `tools/web_smoke_test.mjs` (exit code 1 on failure).

**Docs:** new `docs/06_BUILD_WEB.md` (install emsdk, `build_web.cmd`, `serve_web.cmd`, output,
URL options, how it works, testing/debugging, Linux/WSL, troubleshooting). README, 02 (emsdk row),
04 (phase 2 step 1 ✅), 05 (pointer to 06 §7) updated.

**Not verified:** a real phone or tablet (touch, rotate hint, fullscreen); Firefox and Safari; the
Linux/WSL web presets since the `bimg_encode` fix; the `web-*-windows` presets from inside the Visual
Studio IDE (only through `build_web.cmd`).

### 2026-09-26 (later) — owner report: "web version seems not working" (black page) — fixed

The owner's Chrome showed a black page with the 背包/fullscreen buttons, while the console showed a
healthy start (`bgfx ... OpenGL ES 3.0`, assets, saves, `TOMS on bgfx`).

**Reproduced** only in a *visible* Chrome window at the owner's display scale (devicePixelRatio 2.4):
the canvas was **1×1** and stayed a few pixels after resizes. Headless Chrome (DPR 1, and the GPU
run at DPR 1.5) never showed it.

**Cause:** SDL3's `SDL_WINDOW_FILL_DOCUMENT` first decides whether the page sizes the canvas: it sets
the canvas to 1×1 and reads its CSS size. At a fractional DPR the browser reports ~0.83 px, SDL floors
it to 0 ≠ 1, concludes "externally sized", turns fill-document off and takes the (tiny) CSS size as
the window size. At DPR 1 the probe reads exactly 1, fill-document stays on and hides every other
page element, which is why the headless screenshots had no buttons and the owner's page did.

**Fix:** `shell.html` sizes the canvas itself (`position: fixed; 100vw × 100vh/100dvh`), so SDL
always takes the external-size path and sets the drawing buffer to CSS size × DPR;
`SDL_WINDOW_FILL_DOCUMENT` removed; `appFrame` compares the drawable size with bgfx's every frame
and resets on change (replaces the resize-event handler, also on desktop).

**Verified:** visible Chrome at DPR 2.4: canvas 3336×1939 for a 1390×808 page, follows three
resizes, full frame rate, title renders with both page buttons. Headless smoke test (new game → save
→ reload) and the desktop smoke test still pass.

**Lesson for the test:** `web_smoke_test.mjs` runs headless at DPR 1 and cannot catch DPR-dependent
layout bugs. A run in a visible window at the machine's real scaling is now part of checking web
changes (W12).
