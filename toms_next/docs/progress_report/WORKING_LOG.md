# Working Log — toms_next (Qt6 + bgfx)

Track what was done, by date. Details: the numbered `*_PROGRESS_REPORT.md` parts.

- **2026-09-25** — **Decision:** game runtime on bgfx, editor on Qt6, shipped builds contain bgfx
  only (no Qt). Recorded as decided in `docs/EngineBlueprint/` (README, 01, 02, 04–10); Diligent
  demoted, the in-game web editor replaced by the Qt editor, the old Qt stage editor kept.
  `11_BGFX_QT_ARCHITECTURE.md` still to write.
- **2026-09-25** — **Research:** bgfx has no glTF loader (`geometryc` keeps geometry only);
  skinning / morph / instancing patterns on bgfx; no reliable drop-in library, so the plan is
  fastgltf + ozz-animation CPU `SkinningJob` first.
- **2026-09-25** — **toms_next created** (new folder; nothing in `TOMS/src` edited): `BgfxRenderer`
  behind the existing `IRenderer` via a compat `renderer.h`, SDL3 `toms_game.exe`, Qt6
  `toms_editor.exe` with a `BgfxViewport` and the old stage editor as a tab, embedded shaders,
  prerequisite checks with popups + download links, VS presets, `build.cmd`, docs 01–05.
- **2026-09-25** — **Build fixes:** bx needs C++20 (isolated to `toms_bgfx`), the new bgfx
  `SwapChain` init/reset API, `SDL_MAIN_HANDLED`, `QT_NO_EMIT` vs `Logger::emit`, glm fetched
  instead of `GLM_DIR`.
- **2026-09-25** — **Verified:** game on D3D11/D3D12/Vulkan/OpenGL (title + stage 1 screenshots),
  editor on D3D11 at HiDPI, msjh font fallback, no leaks, no Qt DLLs in `toms_game.exe`, shipping
  preset builds without Qt. Not yet: F5 in the IDE, clicking the popups, a fresh machine.
- **2026-09-25** — **Next:** web build in toms_next (bgfx WebGL2 + SDL3 on Emscripten).
- **2026-09-26** — **Web build done (Windows):** `tools\build_web.cmd [release|debug]` +
  `tools\serve_web.cmd`; guide `docs/06_BUILD_WEB.md`. The web build uses the desktop build's
  `shaderc.exe` (1083 → 348 build steps) and excludes `bimg_encode` (the etcpak blocker). One entry
  point (`main_sdl.cpp`) for desktop and web; IDBFS saves, phone scaling, hold-to-repeat taps,
  `game/web/shell.html`. Release `.wasm` 2.8 MB.
- **2026-09-26** — **Fixed:** LF-only `.cmd` files (cmd.exe `call` bug; now CRLF + `.gitattributes`),
  an `EM_ASM` regex that lost its backslash, `bgfx::renderFrame()` asserting on single-threaded
  (web debug) bgfx, `ccall` before runtime init aborting debug builds (`window.tomsReady`), the 33 MB
  RelWithDebInfo web "release".
- **2026-09-26** — **Verified** in headless Chrome, release and debug: title → new game → save →
  reload restores the save; phone-size viewport runs. `tools/web_smoke_test.mjs` kept as the test.
  Desktop build + smoke test still pass. Not yet: a real phone, Firefox/Safari, WSL web presets.
- **2026-09-26** — **Next:** Phase 2 step 2, the old unit tests + golden-image smoke tests in CTest.
- **2026-09-26** — **Fixed the black web page** the owner saw: at fractional display scaling (240%)
  SDL's fill-document probe failed and the canvas stayed 1×1. `shell.html` now sizes the canvas
  with CSS, fill-document is gone, and the game resets bgfx whenever the drawable size changes.
  Verified in a visible Chrome window at DPR 2.4 plus the headless and desktop smoke tests.
