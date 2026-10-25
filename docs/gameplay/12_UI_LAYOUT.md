# 12 — UI layout: landscape and portrait

> 中文版：[zh_TW/12_UI_LAYOUT.md](zh_TW/12_UI_LAYOUT.md)

**Status:** steps 1–3 built for the map, the HUD, the notices and the battle (2026-10-25), waiting for the owner's
review; the other 10 screens are next (§5).

## 1. The owner's decisions (2026-10-25)

| Question | Answer |
|---|---|
| Long phone screens (16:9, 20:9) | **Fill the screen, no black bars.** |
| When the orientation changes | **Live, any time**: turning the phone or resizing the window switches at once, in a battle too. |
| The portrait battle screen | **The monster on top, the player below it, the controls at the bottom** (for thumbs). |
| How to review | **Battle and HUD first**, screenshots in both orientations, then the other screens the same way. |

## 2. The canvas

Everything is laid out on a canvas of design pixels and scaled to the screen ([canvas.h](../../src/core/engine/canvas.h)):

- the **short side is always 768**, so text and buttons have the same size in either orientation;
- the **long side follows the screen's shape**, from 1024 (the 4:3 canvas the layouts were made for: a 4:3 window looks
  as it always did) up to 2048;
- a screen squarer than 4:3 or 3:4 gets the 4:3 or 3:4 canvas, with thin bars.

| Screen | Canvas |
|---|---|
| 4:3 window, iPad landscape | 1024 × 768 |
| 1920 × 1080 (16:9) | 1365 × 768 |
| phone 2400 × 1080, landscape | 1707 × 768 |
| phone 1080 × 2400, portrait | 768 × 1707 |
| iPad portrait (3:4) | 768 × 1024 |

`GameSession::fitCanvas` works it out every frame. When it changes, the renderer (`BgfxRenderer::kDesignW/H`), the UI
(`RmlUi::setDesignSize`: the documents lay out again and match their `@media` rules again) and the game
(`Game::canvasChanged`: the camera fits the new canvas at once) all follow it.

`--design=WxH` pins a canvas (letterboxed); the screenshot tests pin 1024 × 768, so their reference images stay valid.
`--window=WxH` sets the desktop window's size (e.g. `--window=540x1200` to see a portrait phone); `--resize=FRAME:WxH`
resizes it at that frame. Checked 2026-10-25: a battle started at 1280 x 720 and turned to 540 x 1200 at frame 48
switches to the portrait layout in the same fight (and back), the fighters and the track's hint moving with it.

Android: `screenOrientation="fullSensor"`; the activity already handles a rotation itself (`configChanges`), so a turn
arrives as a resize.

Web: the page's "rotate your device" hint for portrait phones is gone (`src/game/web/shell.html`, 2026-10-25); the game
lays itself out for portrait instead.

`uiScale` (bigger UI objects on small screens, the web build on phones) stays as it was, on top of the canvas.

## 3. The map

The zoom (`viewCols`, Settings) means "this many tiles across a 1024-wide canvas": a tile is `1024 / viewCols` design
pixels on **every** canvas, and the camera shows what fits ([camera.h](../../src/core/game/systems/camera.h)
`kZoomWidth`). A wide screen shows more columns, a portrait one fewer columns and more rows, with the same tile size.
The view's width counts part-tiles (a 768-wide canvas holds 9.75 tiles at the default zoom), so it scrolls to the
grid's real edge.

## 4. The screens

Each `.rcss` keeps its landscape rules and adds `@media (orientation: portrait)` blocks (RmlUi 6 matches
`orientation`, `min-height`, `max-width`, `aspect-ratio`, ...). Rules for laying out:

- **Landscape** is always 768 high, so rules anchored to the top keep working; only the width grows. A block that was
  placed for the 1024 canvas is placed from the centre (`left: 50%; margin-left: -250px` = x 262 on 1024), so a wide
  screen keeps it in the middle and the 1024 canvas is pixel-identical.
- **Portrait** is 768 wide: rows 40 px from each edge (688 px wide). What is tapped is anchored to the bottom
  (`bottom: ...`), what is read to the top.
- **The game never copies a UI position.** Where the game draws on a UI element (the battle's fighters, their particles
  and damage numbers, the attack track's hints), `GameUi::sync` reads the element's layout and tells the game
  (`Game::setBattleBoxes`, `setComboPointsOnScreen`).

| Screen | Landscape | Portrait | Status |
|---|---|---|---|
| Battle (`battle.rcss`) | the 700 px block, centred | fighters at the top; the clock, the attack, the defence and super anchored to the bottom. **A tall phone** (`min-height: 1300px`): the monster above the player, 180 px each. **A short portrait** (3:4): side by side, 134 px | built |
| HUD (`hud.rcss`) | unchanged; the act card centred | the title line wraps clear of the menu button; the story line on the bottom edge; the act card across the width | built |
| Notices (`overlay.rcss`) | unchanged | across the width, in the middle (the top holds the monster in a battle, the bottom its controls); the toast lower | built |
| Player menu (`player.rcss`) | the 992 × 744 panel, centred; the confirm dialogs centred on any canvas | the panel fills the screen less 12 px; each page's two columns stacked (the list above, the detail below); Status: the stats at their own height, then the attributes (a short 3:4 screen: the stats scroll in 360 px). The lists (grid, events, system rows) are flex columns filling the height under their filters, which also stopped the Cards grid running out of the panel | built (2026-10-25) |
| Dialogue, store, stage select, save/load, dialogs, title, ending, mini-game | — | — | next |

## 5. Next

1. The owner reviews the battle, the HUD and the notices in both orientations and on a phone.
2. The other 10 screens, the same way.
3. Portrait screenshot tests, with reference images, once the layouts are approved.

Open for the review: on a tall phone the battle has empty space between the message and the controls (the fighters
could be bigger on 20:9, but not on 16:9 without meeting the controls).
