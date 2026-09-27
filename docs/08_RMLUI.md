# 08 — RmlUi (HTML/CSS game UI) and the RmlUi store

[RmlUi](https://github.com/mikke89/RmlUi) 6.3 (MIT) draws game screens from **RML** (HTML-like)
and **RCSS** (CSS-like) files with data binding. The item store is its first screen, built as a
test: the old hand-drawn store (`Game::drawStoreUI`) is still there, and **F4** switches between
the two at run time.

Status (2026-09-27): **desktop only** (D3D11, D3D12, Vulkan and OpenGL are verified to render the
same image). The web build leaves RmlUi out and uses the old store. See [Web](#web).

## Try it

1. Build as usual (`tools\build.cmd`, Visual Studio or `build_windows.bat`). The first configure
   downloads RmlUi and FreeType.
2. Run `toms_game.exe --stage=stage02`. The store unlocks on stage 2. Confirm the "store
   unlocked" dialog, then press **B**, or click the coin icon.

| Key | In the store |
|---|---|
| ← / → or 1–9 | select a card (the mouse selects by hovering) |
| Enter | buy the selected card |
| Esc | close the store (so does a click outside the panel or on the Close button) |
| **F4** | switch between the old and RmlUi stores (`TOMS_OLD_STORE=1` starts with the old one) |
| **F5** | reload `store.rml` / `store.rcss` from disk: edit, save, press F5 |
| **F8** | RmlUi debugger (element tree, computed styles, layout boxes) |

Tabs are mouse only in both stores.

## Files

| File | What it is |
|---|---|
| `assets/media/ui/store.rml` | the store document: structure plus data bindings (`{{gold}}`, `data-for`, `data-event-click`) |
| `assets/media/ui/store.rcss` | its styles. Sizes are in the game's 1024×768 design pixels, and colours are what appears on screen |
| `src/game/src/store_screen.*` | the `store` data model. Each frame it copies the store state from `Game`; events call `Game`'s store functions |
| `src/engine/src/rml_ui.*` | RmlUi on bgfx: the render interface (geometry, textures, scissor, transforms), system interface, fonts and debugger |
| `src/engine/shaders/vs_rml.sc` | RmlUi vertex shader (the fragment shader is ImGui's `fs_imgui.sc`) |
| `cmake/TomsDependencies.cmake` | fetches FreeType VER-2-14-3 and RmlUi 6.3; option `TOMS_WITH_RMLUI` (default ON on desktop, OFF on web) |

`Game` keeps every rule: prices, buying, equipping and the toast. `Game::setStoreUiExternal(true)`
only stops `draw()` from painting the store panel. The unlock dialog and the HUD icon stay in the
old code. Both UIs therefore share one selection and one purchase path.

## How it fits in a frame

```
GameSession::frame
  input:  keys -> Game (storeKey) as before
          mouse -> Rml::Context (design coords) while the RmlUi store or the debugger is up,
                   otherwise Game::handleTouch as before
  g.draw()                      views 0 (clear) + 1 (game)
  StoreScreen::sync()           show/hide the document; refresh the data model (dirtied only on change)
  RmlUi::update / render        view 2 (kViewUi), letterboxed exactly like the game view
  ImGui                         view 3 (kViewOverlay)
```

The RmlUi context is sized to the design resolution (1024×768), so RCSS pixel values are the same
units as the old store's code. `setViewport` maps them into the letterboxed rectangle on screen.

**Colour.** The desktop backbuffer is sRGB. The game's own tints are linear values, which is why
the old code's `0.08, 0.1, 0.16` looks lighter on screen than its numbers suggest. RCSS colours
are sRGB, so `vs_rml.sc` linearizes them and `#505a6f` in RCSS is `#505a6f` on screen. The store's
RCSS colours were converted this way from the old store's values, so the two look the same.
Images load as sRGB textures, like the game's atlases.

**Fonts.** Family `toms` is the first font found among `TOMS_FONT`, `assets/media/wqy-zenhei.ttc`
and the Noto JP/KR fonts. The others are registered as fallbacks for glyphs it lacks. When
`wqy-zenhei.ttc` is missing, the game sets `TOMS_FONT` to Windows' `msjh.ttc` (see
[02](02_INSTALL_WINDOWS.md)).

## Writing another screen

1. Add `assets/media/ui/<name>.rml` / `.rcss`. Start the RCSS with
   `body, div, p { display: block; }`: RmlUi has no default stylesheet.
2. Add a `<name>_screen.*` pair next to `store_screen.*`:
   - Create a data model with `ctx->CreateDataModel("<name>")` and `Bind` the values.
   - `BindEventCallback` the actions to `Game` calls.
   - Load the document and `Show()` it when the game's state says so.
3. Tell `Game` not to draw the old version of that screen (a flag like `storeUiExternal_`).
4. Sync the screen and route the mouse to it in `GameSession::frame`, then shut it down in
   `GameSession::stop` before `RmlUi::shutdown`.

## Testing without a mouse

`toms_game.exe` accepts `--clicks=x:y@frame,...` in design coordinates. The cursor hovers for 2
frames, presses for 2, then releases. It works alongside `--keys`, `--frames` and `--screenshot`:

```
toms_game.exe --stage=stage02 --keys=enter@30,enter@60,enter@90,b@110 --clicks=222:140@130 --frames=170 --screenshot=store.png
```

This opens the store and clicks the Weapons tab.

## Not done yet

- **Web:** RmlUi needs a CJK font file in the page's data (5–20 MB), because the browser build
  normally draws text with the browser's own fonts. It also needs FreeType built for Emscripten
  and a non-sRGB colour path. The last is in place: `u_rmlParams.x = 0` under Emscripten, but it is
  untested.
- **Render interface:** the RmlUi 6 layers, filters, clip masks and shaders (`filter`,
  `box-shadow`, `mask-image`, gradients) are not implemented. Plain boxes, borders, rounded
  corners, images, text, transitions and transforms work.
- **Editor viewport:** it gets the same store, since it runs `GameSession`. Only a build check has
  been done; it has not been tested by hand.
- **Input:** keyboard focus and navigation inside RmlUi are not used. Keys still go through
  `Game::storeKey`.
