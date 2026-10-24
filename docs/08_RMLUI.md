# 08 — The game UI (RmlUi)

Every screen a player sees is an [RmlUi](https://github.com/mikke89/RmlUi) 6.3 document: RML (HTML-like) for the
structure and RCSS (CSS-like) for the look. That covers the title, HUD, on-screen pad, battle, dialogue, inventory,
store, in-game menu, stairs and store-unlocked popups, stage select, ending, toasts and notifications. The game draws
only the world (map, entities, player), plus the two ImGui developer tools (F1 debug overlay, F2 styling spike). This
is the same on desktop and web.

## Where things are

| Path | What it is |
|---|---|
| `assets/media/ui/*.rml` | one document per screen (`title`, `hud`, `pad`, `battle`, `dialogue`, `player` (the player menu), `saveload` (its Save / Load screen), `store`, `dialogs`, `stage_select`, `ending`, `overlay`) |
| `assets/media/ui/*.rcss` | their styles; `common.rcss` holds the shared ones (buttons, rows, panels, palette, scrollbars) |
| `assets/media/fonts/NotoSansCJKtc-TOMS.otf` | desktop's default UI font: Noto Sans CJK TC cut down to the characters the game uses (about 790 KB). Not in the web build |
| `tools/make_ui_font.py` | rebuilds that font; `--check` reports characters it lacks |
| `assets/data/text.json` → `languages[].font` / `web_font` | per-language fonts, see [Fonts](#fonts) |
| `src/core/game/ui/ui_state.h` | **the contract**: every value a screen can show (`UiState`), as plain data |
| `src/core/game/core/game_ui.cpp` | `Game::buildUiState()` fills it each frame; `Game::uiEvent()` handles every button |
| `src/game/src/game_ui.*` | binds `UiState` to RmlUi as the data model `game`, loads the documents, shows and hides them |
| `src/engine/src/rml_ui.*` | RmlUi on bgfx: render interface, system interface, font loading, debugger |
| `src/engine/src/rml_canvas_font.*` | web only: RmlUi's font engine on an HTML canvas (the browser draws the text) |
| `src/engine/shaders/vs_rml.sc`, `fs_rml.sc` | RmlUi's shaders (`fs_rml.sc` premultiplies the game's straight-alpha sprite atlas, which the UI borrows) |

The rules stay in `Game`, the text comes from `data/text.json` through `Locale`, and the look lives in the `.rcss`
files. There is no UI logic in the host.

## How a screen works

```
Game::buildUiState(UiState&)     every frame: what to show (all text already translated)
        |  data model "game"     {{battle.log}}, data-for="r, i : menu.rows", data-if="store.visible", ...
        v
assets/media/ui/*.rml            layout + style
        |  act('name', arg)      data-event-click="act('pm_btn', i)", "act('menu_row', i)"
        v
Game::uiEvent(name, arg)         calls the same Game functions the keyboard uses
```

- **Showing and hiding.** `GameUi::sync()` shows each document when its section is visible (`hud.visible`,
  `battle.visible`, ...; the table is `kDocs` in `game_ui.cpp`). Stacking order is each document's `z-index`: HUD 10,
  pad 20, battle/dialogue/inventory 30, store 40, menu and stage select 45, popups 50, title 60, ending 70, overlay 90.
- **Keyboard.** Keys work as before (`GameSession::frame` calls `Game`'s functions). The UI only shows the current
  selection, so keyboard and mouse always agree.
- **Mouse and touch.** Everything goes to RmlUi first. The HUD, pad and overlay are `pointer-events: none` except on
  their buttons, so a click on the map falls through to `Game::handleTouch` (click-to-move).
- **Lists.** Put `data-for` on a plain wrapper and the bindings (`data-class-*`, `data-style-*`) on its child. With
  bindings on the `data-for` element itself, RmlUi reads a row that is being removed and logs "Data array index out
  of bounds" whenever a list gets shorter.

## Editing the UI

**The UI editor** (`Editor\UiEditor.cmd`, `ui_editor.exe`) is the easiest way: the file and the running game side by
side, every change shown as you type. Without it, any text editor works: run the game, edit, save (see *Live reload*).

### The UI editor

A step-by-step tutorial with a practice screen: [37_UI_EDITOR.md](37_UI_EDITOR.md) (in the editor: Help > UI Editor
Tutorial, F1; Help > Open Practice Screen).

- **Files** (left): every `.rml` and `.rcss` of `assets/media/ui`. ● = changed here, not saved yet.
- **The text** (middle): colours for tags, attributes, bindings (`{{...}}`), selectors, properties and values; a colour
  (`#ffcaa8`) is shown on its own colour; line numbers.
- **The game** (right): the real game. A pause in typing (0.3 s) shows the change: the changed files are handed to
  RmlUi from memory and the screens reload. **Nothing on disk changes until Save (Ctrl+S)**; Revert File drops the
  changes. A file changed on disk by someone else is reloaded when it has no changes here.
- **Preview**: what the game shows: the title screen, or any test scenario (`tests/scenarios`, listed as
  *scene — name*: a battle, a dialogue, the store, a menu page, a mini-game ...). **Restart Preview** (F5) starts it again.
- **Language**: the game's text in another language, to check long text, here only (the game's setting is not changed).
- **Inspect** (Ctrl+I): a click in the game picks the element under the mouse instead of pressing it. Its line in the
  `.rml` opens, and the **Inspector** lists the style rules (in the sheets the document links) that name its id, its
  classes or its tag; double-click one to open it.
- **Problems**: RmlUi's warnings and errors since the last change; double-click one that names a file and line.
- **RmlUi Debugger** (F8): RmlUi's own inspector over the game.
- `ui_editor -platform offscreen --selftest` and `ui_editor --smoke=<png>` are its tests (`ui_editor.selftest`,
  `smoke.ui_editor`).

### Live reload

With hot reload on (`TomsEditor`, the UI editor, a debug build, or `toms_game --hot-reload`), the game watches the UI
folder: a saved `.rml` or `.rcss` shows within half a second, with "UI reloaded: <file>" at the top
(`GameUi::reloadIfChanged`). **F5** still reloads by hand.

### In any editor

Run the game, edit a `.rml`/`.rcss` file, and press **F5**: every document reloads from disk. **F8** opens RmlUi's
debugger (element tree, computed styles, layout boxes). A yellow "!" beacon at the top right means RmlUi logged a
warning; the debugger shows which.

- **Units:** `px` are design pixels. Every document is laid out at 1024×768 and scaled to the window, letterboxed like
  the map.
- **Colours:** the value you write is the colour on screen. The shader converts for the desktop's sRGB backbuffer; the
  web has no sRGB backbuffer, so there it passes through.
- **Game sprites (icons):** from the packed atlas, by name: `<img sprite="coin"/>`, or bound with
  `data-attr-sprite="..."` (the model holds sprite names, `Game::uiSprite`). A document that shows sprites links
  `<link type="text/rcss" href="_atlas.rcss"/>`. That stylesheet is not a file: the game makes it in memory from the
  atlas it loaded for the map (`Game::uiSpritesheet`), so the UI always matches the map and the art style. When the
  style changes, the documents reload with the new sheet. `atlas_sprites_test` fails if a sprite name used by the
  code, `assets/data` or an `.rml` is missing from an atlas ([14](14_ATLAS_TOOL.md)).
- **Other images:** `src="..."`, relative to the document. Loaded as sRGB, point-sampled.
- **RmlUi has no default stylesheet.** `common.rcss` makes `div`/`p` block; anything else needs a `display`.
- **New text (desktop):** after adding characters that are not yet used anywhere (a new dialogue line, a new symbol
  in an `.rml`), run `python tools/make_ui_font.py`, which needs Python 3 with fontTools. `--check` lists what is
  missing, and the test `data.ui_font_covers_text` fails until it is run. A character the bundled font still lacks is
  drawn from the system fallback font (see Fonts), in that font's style; without one, it is an empty box. The web build
  is not affected (see Fonts).

## Fonts

The two builds get their text differently.

**Web: the browser draws the text.** No font file and no FreeType are in the web build. `rml_canvas_font.cpp` is
RmlUi's font engine on an HTML canvas: widths come from `measureText()`, and every distinct string is drawn *whole*
with `fillText()` into a small cached texture. Because whole strings are drawn, the browser handles everything that
per-glyph rendering can't:
- Arabic and Persian letter joining, and right-to-left runs;
- Indic and Thai shaping;
- CJK variants per language;
- emoji;
- font fallback for any character.

Strings are drawn at the screen's real pixel density, so text stays sharp on phones. The fonts are whatever the device
has, picked by the language's `web_font` CSS list in `text.json`:

```json
{ "code": "zh_TW", "name": "繁體中文",
  "web_font": "'PingFang TC', 'Microsoft JhengHei', 'Noto Sans TC', 'Noto Sans CJK TC', sans-serif" }
```

The first installed font wins, and `sans-serif` at the end always matches. Put macOS/iOS, Windows and Android names
in the list; without `web_font` the browser's default is used. The language tag (`zh-TW`) is passed to the canvas too,
so Chinese and Japanese get their own glyph shapes. Because fonts differ between devices, leave some room in fixed-size
boxes.

**Desktop: font files (FreeType).** The default is `assets/media/fonts/NotoSansCJKtc-TOMS.otf`. It also serves as the
**fallback** for any character another font lacks.

**System fallback (Windows, Android; 2026-10-16).** After the bundled font, `rml_ui.cpp` loads one CJK font from the
system as the last fallback face, so a character the bundled subset lacks is drawn instead of shown as a box. RmlUi tries
the fallback faces in load order, so the bundled font always wins and the screenshots do not change.

| Platform | First that exists |
|---|---|
| Windows | `%WINDIR%\Fonts\msjh.ttc` (Microsoft JhengHei), `msyh.ttc`, `mingliu.ttc`, `simsun.ttc` |
| Android | `/system/fonts/NotoSansCJK-Regular.ttc` face 3 (Traditional Chinese), `NotoSansCJKtc-Regular.otf`, `DroidSansFallbackFull.ttf`, `DroidSansFallback.ttf` |
| Linux | `NotoSansCJK-Regular.ttc` face 3 under `/usr/share/fonts` |

- The file is read into memory (10-25 MB) and never shipped: it comes from the player's own system. The log says
  which one (`[rmlui] system fallback font: …`) or that there is none.
- `TOMS_SYSTEM_FONT=<path>[#face]` picks another file; `TOMS_SYSTEM_FONT=none` turns it off.
- It is a safety net, not a replacement: keep the bundled subset complete (`tools/make_ui_font.py`), because a character
  from the system font has that font's look.

A language can name its own font:

```json
{ "code": "ja", "name": "日本語", "font": "fonts/NotoSansJP-Regular.otf" }
```

The path is relative to `assets/media` or absolute (`.ttf`, `.otf` and `.ttc`, first face). When the game switches to
that language and the file exists, every document uses it (family `toms-<code>`). If it's missing, the game logs it and
keeps the default. FreeType does no text shaping, so scripts that need it (Arabic, Hebrew, Indic) don't render
correctly on desktop yet; RmlUi's HarfBuzz font engine would add that later. On the web they already work.

## Adding a screen

1. Add its values to `ui_state.h` (a `UiXxx` struct with `visible`, plus a member in `UiState`).
2. Register the struct's members in `registerTypes()` and add a `c.Bind(...)` in `GameUi::init` (`game_ui.cpp`), and
   add the document to `kDocs` with its visibility test.
3. Fill it in `Game::buildUiState()` and handle its buttons in `Game::uiEvent()` (`game_ui.cpp`).
4. Write `assets/media/ui/xxx.rml` + `.rcss`: link `common.rcss`, set `data-model="game"` and a `z-index`.

The player menu (`player.rml`: Status / Equipment / Items / Events / System; it replaced `inventory.rml` and `menu.rml`)
was built this way: see [20_PLAYER_MENU.md](20_PLAYER_MENU.md) §8–9. Two rules it follows: put `data-for` on a wrapper
element and the bindings on the element inside it (a binding on the `data-for` element itself can read past the end
of a list that just shrank and log "Data array index out of bounds"); and fill every list a hidden part of a document
binds, every frame. For Chinese and Japanese text, `word-break: break-word` lets a line without spaces wrap.

## Not supported

The render interface draws boxes, borders, rounded corners, images, text, transforms, transitions, animations and
clipping (`overflow`). RmlUi 6's layers, filters, masks, `box-shadow` and shader gradients (`linear-gradient` and the
like) are not implemented. The geometry gradients `horizontal-gradient`/`vertical-gradient` work.

## History

- 2026-09-27: web text moved to the browser (canvas font engine); per-language fonts (`font` / `web_font` in
  `text.json`). Web download about 2.6 MB → 1.7 MB gzip.
- 2026-09-27: the store was rewritten in RmlUi as a test, then every screen moved to RmlUi. The hand-drawn UI code, the
  game's own font atlas (`font.cpp`, `stb_truetype`, the browser-canvas font on web) and the old font downloads
  (WenQuanYi Zen Hei, the Windows `msjh.ttc` fallback) were removed.
- 2026-10-16: a system CJK font is the last fallback face again (Windows, Android), behind the bundled font, for
  characters the subset lacks; `data.ui_font_covers_text` checks that the subset covers the game's text.
