# 13 — 畫面風格（AI 重繪的美術，在「設定」裡切換）

> 玩家在 **設定 → 畫面風格** 選一個風格。在標題畫面選：圖片立即重新載入；在遊戲中選：
> **回到標題畫面時**才重新載入（遊戲進行中畫面不會突然變樣）。重新啟動遊戲也會套用。
> 原版美術永遠是預設，也一直可以選回來。
> 繁體中文敘述、指令與檔名保持英文。

---

## 1. 玩家看到的

| 位置 | 內容 |
|---|---|
| 標題 → 設定 | 兩個區塊：**語言**、**畫面風格**（原版 / <名稱>） |
| 遊戲中 Esc → 設定 | 三個區塊：**語言**、**畫面風格**、**其他**（鏡頭） |
| 在標題選了之後 | 確認視窗：「確定要把畫面風格換成「…」嗎？ 遊戲圖片會立即重新載入。」→ 按「是」立即套用 |
| 在遊戲中選了之後 | 確認視窗：「… 回到標題畫面時套用。」；該列右側顯示「回到標題畫面後套用」 |
| 回到標題畫面時 | 若選擇的風格和目前載入的不同，重新建立 sprite atlas（地圖、戰鬥、圖示一起換） |

只有原版時（沒有 `styles.json`）不會出現風格列。

## 2. 檔案放哪裡

```
assets/media/styles/styles.json            風格清單（id + 各語言名稱）
assets/media/styles/<id>/sprites/<sprite>.png   要替換的圖（可以只放一部分）
save/settings.json  "artStyle": "<id>"      玩家的選擇（"" = 原版）
```

- 風格裡沒有的圖沿用 `assets/media/sprites/` 的原版。
- 尺寸可以不同（例如風格 64px、原版 32px）：遊戲取最大的尺寸，較小的圖以 nearest-neighbour 放大（像素維持銳利）。
- 每個風格有預先打包好的 atlas：`assets/media/styles/<id>/atlas/game.atlas` + `game.png`，是
  `assets/media/atlas/game.atlasproj` 的一個 variant（見 [14](14_ATLAS_TOOL.md)）；風格的圖就存在這張打包好的
  texture 裡，`styles/<id>/sprites/` 只是匯入用的參考資料夾。有它時遊戲直接用它（每張圖維持原本尺寸）；
  沒有它時才在啟動時組上面那種格狀 atlas。
- 地圖、戰鬥畫面、背包/商店圖示、HUD 商店按鈕都跟著風格。
- 因為放在 `assets/media/` 底下，Windows 打包（`build_windows.bat`）、網頁版、Android 都會自動帶上。

## 3. 做一個新風格（ComfyUI）

前置：`tools\setup_ai_art.cmd`（安裝 ComfyUI + 模型，或指向遠端伺服器）。

```bat
:: 1. 產生候選圖（每張 4 個 seed）-> Build/art_out/<run>/
python tools/art/reskin_sprites.py --sprites all --seeds 4 --style "dark fantasy dungeon crawler, 16-bit SNES JRPG style" --out Build/art_out/dark16

:: 2. 看 Build/art_out/<run>/sheet_32.png / sheet_64.png，挑 seed（寫進 Build/art_out/<run>/picks.json，可只改要換的）
python tools/art/make_variant.py --run Build/art_out/dark16 --name dark16 --pick slime=1003 --pick wall=1002

:: 3a. 先試玩（不動 assets/）：Build/assets_variants/<name>/
Build\windows-release\bin\toms_game.exe --assets=D:\...\TOMS\assets_variants\dark16\media

:: 3b. 加進遊戲成為可選風格（寫入 assets/media/styles/）
python tools/art/make_variant.py --run Build/art_out/dark16 --name dark16 --size 64 --install --title "zh_TW=暗黑奇幻 16-bit,en=Dark fantasy 16-bit"

:: 4. 風格名稱若用到新字，重建 UI 字型（它也會讀 styles.json）
python tools/make_ui_font.py

:: 5. 在 assets/media/atlas/game.atlasproj 的 "variants" 加一筆（照 dark16 那筆：id、
::    "reference": "../styles/<id>/sprites"、output dir "../styles/<id>/atlas"），然後把風格的圖匯入並存檔
::    （編輯器：選這個 variant -> Import From References）。見 docs/14_ATLAS_TOOL.md
Build\windows-release\bin\atlaspack.exe refs assets\media\atlas\game.atlasproj --variant <id> --import all
```

`Build/art_out/` 與 `Build/assets_variants/` 不進版控；`assets/media/styles/` 要進版控（那是出貨內容）。

## 4. 已知限制

- **網頁版**：存檔資料夾（IndexedDB）是遊戲開始後才非同步載入，啟動時讀不到 `settings.json`，
  所以重新整理頁面後會回到原版（語言設定也有同樣的情況）；在同一次遊玩中從設定切換仍然有效。
  桌面與 Android 正常。
- 地板（`floor`）目前沒有 AI 版本（無縫拼接還沒處理）。
- 動畫幀尚未支援（每個角色一張圖）。

## 5. 程式在哪

| 檔案 | 內容 |
|---|---|
| `src/core/game/core/art_styles.h` | 讀 `styles.json`、風格索引 |
| `src/core/game/core/game_assets.cpp` | `loadPrebuiltSpriteAtlas(style)` 讀預先打包的 atlas；沒有時 `loadSpriteAtlas(style)` 組格狀 atlas（含尺寸統一）；`refreshArtStyle()` 在標題畫面套用新選擇 |
| `src/core/game/ui/title_screen.*` | 設定頁的風格列與確認視窗（`title_screen_test` 有測） |
| `src/core/game/core/game_ui.cpp` / `game_store.cpp` | 兩個設定頁的區塊與列、確認視窗文字、UI 圖片路徑；`returnToTitle()` 觸發套用 |
| `assets/data/text.json` | `settings.style_*` 六種語言 |
