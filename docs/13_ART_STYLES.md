# 13 — Art styles (AI-redrawn art, switched in Settings)

> 中文版：[zh_TW/13_ART_STYLES.md](zh_TW/13_ART_STYLES.md)

> The player picks a style in **Settings → Art style**. Picked on the title screen, the images
> reload at once; picked during a game, they reload **when the player returns to the title screen**
> (the picture never changes suddenly mid-game). Restarting the game applies it too.
> The original art is always the default, and can always be picked again.
> Commands and file names are in English.

---

## 1. What the player sees

| Where | What |
|---|---|
| Title → Settings | Two sections: **Language**, **Art style** (Original / <name>) |
| In game, Esc → Settings | Three sections: **Language**, **Art style**, **Other** (camera) |
| After picking on the title screen | A confirmation: "Change the art style to '…'? The game's images reload now." → **Yes** applies it at once |
| After picking during a game | A confirmation: "… applied when you return to the title screen."; the row shows "Applies after returning to the title screen" on its right |
| On returning to the title screen | If the chosen style differs from the loaded one, the sprite atlas is rebuilt (map, battle and icons change together) |

With only the original art (no `styles.json`), the style row does not appear.

## 2. Where the files go

```
assets/media/styles/styles.json            the style list (id + a name per language)
assets/media/styles/<id>/sprites/<sprite>.png   the images to replace (can be only some of them)
save/settings.json  "artStyle": "<id>"      the player's choice ("" = original)
```

- An image the style does not have uses the original from `assets/media/sprites/`.
- Sizes may differ (for example 64 px in the style, 32 px in the original): the game uses the
  largest size and scales smaller images up with nearest-neighbour (pixels stay sharp).
- Each style has a pre-packed atlas: `assets/media/styles/<id>/atlas/game.atlas` + `game.png`, a
  variant of `assets/media/atlas/game.atlasproj` (see [14](14_ATLAS_TOOL.md)). The style's images
  live in that packed texture; `styles/<id>/sprites/` is only the reference folder they are imported
  from. When the atlas exists the game uses it directly (each image keeps its own size); only without
  it does the game build the grid atlas described above at start-up.
- The map, the battle screen and the item / store icons (the player menu's grids, the store cards) all follow the style.
- Because it is under `assets/media/`, the Windows package (`build_windows.bat`), the web build and
  Android all include it automatically.

## 3. Making a new style (ComfyUI)

Prerequisite: `tools\setup_ai_art.cmd` (installs ComfyUI + models, or points at a remote server).

```bat
:: 1. Generate candidates (4 seeds per image) -> Build/art_out/<run>/
python tools/art/reskin_sprites.py --sprites all --seeds 4 --style "dark fantasy dungeon crawler, 16-bit SNES JRPG style" --out Build/art_out/dark16

:: 2. Look at Build/art_out/<run>/sheet_32.png / sheet_64.png and pick seeds (written to Build/art_out/<run>/picks.json; you can change only the ones to replace)
python tools/art/make_variant.py --run Build/art_out/dark16 --name dark16 --pick slime=1003 --pick wall=1002

:: 3a. Try it first (assets/ untouched): Build/assets_variants/<name>/
Build\windows-release\bin\toms_game.exe --assets=D:\...\TOMS\assets_variants\dark16\media

:: 3b. Add it to the game as a selectable style (writes assets/media/styles/)
python tools/art/make_variant.py --run Build/art_out/dark16 --name dark16 --size 64 --install --title "zh_TW=暗黑奇幻 16-bit,en=Dark fantasy 16-bit"

:: 4. If the style's name uses new characters, rebuild the UI font (it also reads styles.json)
python tools/make_ui_font.py

:: 5. Add an entry to "variants" in assets/media/atlas/game.atlasproj (like the dark16 one: id,
::    "reference": "../styles/<id>/sprites", output dir "../styles/<id>/atlas"), then import the style's images and save
::    (editor: select this variant -> Import From References). See docs/14_ATLAS_TOOL.md
Build\windows-release\bin\atlaspack.exe refs assets\media\atlas\game.atlasproj --variant <id> --import all
```

`Build/art_out/` and `Build/assets_variants/` are not version-controlled; `assets/media/styles/` is
(it is shipped content).

## 4. Known limits

- **Web build:** the save folder (IndexedDB) loads asynchronously after the game starts, so
  `settings.json` cannot be read at start-up, and a page reload goes back to the original art (the
  language setting has the same issue). Switching in Settings during one session still works.
  Desktop and Android are fine.
- The floor (`floor`) has no AI version yet (seamless tiling is not handled).
- Animation frames are not supported yet (one image per character).

## 5. Where the code is

| File | What |
|---|---|
| `src/core/game/core/art_styles.h` | reads `styles.json`; the style index |
| `src/core/game/core/game_assets.cpp` | `loadPrebuiltSpriteAtlas(style)` reads the pre-packed atlas; without it `loadSpriteAtlas(style)` builds the grid atlas (with size unification); `refreshArtStyle()` applies a new choice on the title screen |
| `src/core/game/ui/title_screen.*` | the style row and the confirmation in the settings page (tested by `title_screen_test`) |
| `src/core/game/core/game_ui.cpp` / `game_store.cpp` | the sections and rows of both settings pages, the confirmation texts, UI image paths; `returnToTitle()` triggers applying the style |
| `assets/data/text.json` | `settings.style_*` in six languages |
