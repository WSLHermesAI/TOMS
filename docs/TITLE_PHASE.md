# Title Phase — New Game / Continue / Settings

The game boots into a **title phase** instead of dropping straight into `stage01`. It is the
Boot screen named in `game_state.h` (`GameState::MainMenu`, with the Settings page reported as
`GameState::Settings`), and it blocks all gameplay input until the player picks something
(`Game::modalActive()` includes `titleOpen()`).

```
Boot ──► Title: Menu ──┬─► New Game ──► fresh run at stage01, slot written ──► Explore
                       ├─► Continue ──┬─► slot HAS a save ──► resume it      ──► Explore
                       │              └─► slot is EMPTY ───► prompt ──┬─ Yes ─► new run in THAT slot
                       │                                              └─ No  ─► back to the list
                       └─► Settings ──► language                             ──► back to Menu
```

The empty-slot prompt (`continue.new_game_confirm`, drawn by `Game::drawTitleConfirmDialog()`)
exists because activating an empty slot used to do nothing at all, which read as a broken
button. Yes starts a fresh run **in the slot the player picked** (`Game::newGame(slot)`, not
"first free slot"); No or Esc returns to the list with nothing changed.

## Why it is drawn by the game renderer, not ImGui

Dear ImGui is wired into the **desktop (Vulkan) build only** (`src/engine/imgui_layer.*` is
never compiled into `toms_web`), and the Stage Select hub is therefore desktop-only. The title
phase has to exist on both targets — the browser build is the one actually played — so it is
drawn with the same `Quad` + `Game::drawText` path the store/inventory/dialogue overlays use.
Solid-tint quads only: no new art assets, identical output on Vulkan, WebGL2 and WebGPU.

Consequence worth knowing: ImGui-drawn toasts (`Game::drawNotifications()`) use ImGui's default
font, which has **no CJK glyphs**, and would show stray `?` boxes over the title. They are
skipped while the title is up (see `main.cpp`), and the title phase itself pushes no
notifications for that reason.

## Files

- `src/game/title_screen.h/.cpp` — the page/selection state machine plus `computeTitleLayout()`
  (pure math, shared by the draw pass and click hit-testing so a tap always lands on the row
  that was drawn). No renderer dependency → headless-testable.
- `src/game/save_slots.h/.cpp` — numbered save files: `writeSlotSave` / `readSlotSave` /
  `summarizeSlot` (the Continue row's data) / `firstEmptySlot` / `newestSlot`, and the
  platform-specific save directory + flush.
- `src/game/game_settings.h/.cpp` — persisted preferences (`save/settings.json`): language,
  slot count, ImGui font scale.
- `src/game/localization.h/.cpp` — key → localized string from `data/text.json`, with a small
  built-in fallback table so a missing/broken JSON degrades to English instead of blanking the
  UI. Unknown keys return the key itself (an untranslated string is visible, not silent).
- `data/text.json` — the string table (`languages` + `strings`).
- `src/game/title_screen_test.cpp` — headless test: page state machine, slot I/O round-trip
  (including story flags and per-tile entity status), settings clamping/persistence,
  localization switching. Run `./title_screen_test`.

## Save files

```
save/settings.json        language / slotCount / uiFontScale        (survives New Game)
save/slot1.json           { meta, run, savedAt, playTimeSec, stageName }
save/slot2.json  ...
save/slot3.json  ...
```

`meta` + `run` reuse the schema in `src/game/save_system.h`, so a slot is exactly what a
Meta/Run save already described — no second save format. Writes go through
`writeJsonAtomic()` (temp file + rename), so an interrupted write cannot corrupt a slot.

### Autosave policy

| Event | What happens |
| --- | --- |
| New Game | slot chosen (first free, else slot 1), run written immediately |
| Floor change (stairs up/down confirmed) | written immediately |
| Combat won/lost, store purchase, dialogue choice | marked dirty; flushed at most every 3 s |
| Continue | slot read back, run applied, then saved again on the next event |

`playTimeSec` only advances outside the title phase, and the Continue row shows it.

### Browser persistence

`defaultSaveDir()` is `/save` under Emscripten. `emscripten_main.cpp` mounts IndexedDB-backed
IDBFS there at startup and flushes after every write, so saves survive a page reload; the
initial sync-in is async, and `jsRefreshSlots` re-reads the Continue list once the files have
actually arrived. If IndexedDB is unavailable (private browsing) the mount fails harmlessly and
saves last for the session only.

## Testing notes (learned the hard way)

- **Capture with `ffmpeg -f x11grab`, not `xwd`.** This session's hand-rolled `xwd` decoder
  ignored the 4-byte word order (LSBFirst) and produced a *wrapped, duplicated* image that
  looked like a rendering bug. It was a decoder artifact. `ffmpeg x11grab` is the trustworthy
  path (`xwd` output is only correct if you decode little-endian 32-bit words).
- **Synthetic xdotool mouse clicks are not seen** by the app (per-frame `glfwGetMouseButton`
  polling misses the quick press/release). Synthetic *key* events do work, so scripted input
  should be keyboard-only, or hold the button (`mousedown`, sleep, `mouseup`).
- The title's menu cursor keeps its position when returning from a sub-page (a deliberate small
  behaviour), so a key-only script must account for it — the tests therefore start from a known
  row and move explicitly.
- `xdotool` and a no-root `GL/gl.h` are required for the desktop build in this environment:
  configure with
  `-DCMAKE_CXX_FLAGS="-I$HOME/opt/x11root/usr/include -I$HOME/opt/glsym/include"` (imgui's GLFW
  backend includes `<GLFW/glfw3.h>` without `GLFW_INCLUDE_NONE`, so it needs `GL/gl.h`).
- `gen_font_atlas.py` is retired — both desktop and web now build the font atlas the same way at
  load time via `Font::buildFromFiles()` (stb_truetype, see `src/engine/font.cpp`), scanning
  `data/*.json` for codepoints, so adding text needs no separate regeneration step on either
  platform.

## Verified (native, 2026-09-10)

- Title renders (menu, Continue, Settings pages) — screenshots captured via x11grab.
- Settings → language switch persists to `save/settings.json` (`zh_TW` → `en`) and the UI
  follows immediately; it survives a restart.
- New Game: fresh stats, wiped meta/entity progress, `stage01` loaded, `save/slot1.json`
  written (with the stage's display name, e.g. `村莊外緣`).
- Continue with a save: row shows stage / LV / HP / gold / saved-at / play time; activating it
  resumes that run (dungeon renders, title gone).
- Continue with no saves: rows read `[空]`, the hint says to start a new game, and activating an
  empty slot opens the confirm prompt ("要用存檔格 2 開始新遊戲嗎？" with 是/否); No/Esc returns to
  the list, Yes starts a fresh run and the slot file proves which slot was used (`slot2.json`
  written, no `slot1.json`).
- `title_screen_test` — all checks pass.

## Verified (browser, WebGL2 build, 2026-09-10)

- `web-gl/toms_web.html` boots into the title phase (`jsModalActive()` returns 1 at boot).
- Menu → Continue shows the empty-slot list; activating an empty slot keeps the title up.
- Menu → New Game closes the title (modal 0), starts the run, and the dungeon renders.
- `/save/settings.json` and `/save/slot1.json` really exist inside the page's IDBFS
  (`Module.FS.readFile('/save/slot1.json')` returns the run), so browser saves persist.
- Settings → language switch writes `"language": "en"` into the browser's `settings.json`.

### Web design-space / canvas-size mismatch — FIXED (was: clipped layout + dead on-screen controls)

The browser build used to conflate two different sizes:

- `Game::loadAssets()` calls `ren->init(1280, 720)`, and `WebGLRenderer::width()/height()`
  returned those init values, so the web UI was laid out for a **1280x720** design;
- the canvas is **1024x768** (`emscripten_main.cpp`), and the page's tap mapping (`toBP`) maps
  into 0..1024 / 0..768;
- `WebGLRenderer::end()` set `glViewport(0,0,W,H)` = (0,0,1280,720) inside that 768-tall drawing
  buffer. A GL viewport is anchored at the buffer's **bottom-left**, so the 720-tall viewport
  covered image rows 48..768 and everything it drew landed 48 px (768-720) lower than the
  design coordinates the game keeps hit-testing taps against.

Net effect: the HUD/right edge was clipped **and** every on-screen control (virtual keypad,
store icon, dialogue choices, inventory cards) sat ~48 px above its own hit box — taps landed
on dead space. That is why the web keypad "did nothing".

Fix: `WebGLRenderer` now reports the same fixed **1024x768** design space the Vulkan backend
uses (`kDesignW/kDesignH`), takes the drawing-buffer size from
`emscripten_get_canvas_element_size("#canvas", ...)` (re-checked each frame in `begin()`), and
sets the viewport to that buffer size. Design space, drawing buffer and the page's tap mapping
are now all 1024x768, so drawing and hit-testing agree 1:1.

Verified on the local build and the live site by driving real pointer events at the d-pad's
design coordinates and diffing canvas pixels: taps now step the player (horizontal and vertical
moves both produce map changes), and the HUD/story text and the A button are fully visible.

**Still open:** `renderer_webgpu.cpp` has the same conflation (`W_/H_` from init serve as the
surface config, the uniform *and* `width()/height()`), so the WebGPU build would mis-map taps
the same way. It is not part of the deployed site (the Pages iframe loads the WebGL2 build) and
could not be verified in this environment (`navigator.gpu.requestAdapter()` returns null), so it
was deliberately left untouched rather than blind-fixed.

