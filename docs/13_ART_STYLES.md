# 13 — 畫面風格（AI 重繪的美術，在「設定」裡切換）

> 玩家在 **設定 → 畫面風格** 選一個風格；確認視窗會說明「需要重新啟動遊戲才會套用」，
> 下次啟動時遊戲就用那套圖。原版美術永遠是預設，也一直可以選回來。
> 繁體中文敘述、指令與檔名保持英文。

---

## 1. 玩家看到的

| 位置 | 內容 |
|---|---|
| 標題 → 設定 | 語言列之後多出「畫面風格：原版 / 畫面風格：<名稱>」 |
| 遊戲中 Esc → 設定 | 語言、鏡頭之後同樣有畫面風格列 |
| 選了之後 | 確認視窗：「確定要把畫面風格換成「…」嗎？ 需要重新啟動遊戲才會套用。」 |
| 確認後、重啟前 | 該列右側顯示「重新啟動遊戲後套用」 |

只有原版時（沒有 `styles.json`）不會出現風格列。

## 2. 檔案放哪裡

```
assets/media/styles/styles.json            風格清單（id + 各語言名稱）
assets/media/styles/<id>/sprites/<sprite>.png   要替換的圖（可以只放一部分）
save/settings.json  "artStyle": "<id>"      玩家的選擇（"" = 原版）
```

- 風格裡沒有的圖沿用 `assets/media/sprites/` 的原版。
- 尺寸可以不同（例如風格 64px、原版 32px）：遊戲取最大的尺寸，較小的圖以 nearest-neighbour 放大（像素維持銳利）。
- 地圖、戰鬥畫面、背包/商店圖示、HUD 商店按鈕都跟著風格。
- 因為放在 `assets/media/` 底下，Windows 打包（`build_windows.bat`）、網頁版、Android 都會自動帶上。

## 3. 做一個新風格（ComfyUI）

前置：`tools\setup_ai_art.cmd`（安裝 ComfyUI + 模型，或指向遠端伺服器）。

```bat
:: 1. 產生候選圖（每張 4 個 seed）-> art_out/<run>/
python tools/art/reskin_sprites.py --sprites all --seeds 4 --style "dark fantasy dungeon crawler, 16-bit SNES JRPG style" --out art_out/dark16

:: 2. 看 art_out/<run>/sheet_32.png / sheet_64.png，挑 seed（寫進 art_out/<run>/picks.json，可只改要換的）
python tools/art/make_variant.py --run art_out/dark16 --name dark16 --pick slime=1003 --pick wall=1002

:: 3a. 先試玩（不動 assets/）：assets_variants/<name>/
out\build\windows-release\bin\toms_game.exe --assets=D:\...\TOMS\assets_variants\dark16\media

:: 3b. 加進遊戲成為可選風格（寫入 assets/media/styles/）
python tools/art/make_variant.py --run art_out/dark16 --name dark16 --size 64 --install --title "zh_TW=暗黑奇幻 16-bit,en=Dark fantasy 16-bit"

:: 4. 風格名稱若用到新字，重建 UI 字型（它也會讀 styles.json）
python tools/make_ui_font.py
```

`art_out/` 與 `assets_variants/` 不進版控；`assets/media/styles/` 要進版控（那是出貨內容）。

## 4. 已知限制

- **網頁版**：存檔資料夾（IndexedDB）是遊戲開始後才非同步載入，啟動時讀不到 `settings.json`，
  所以網頁版目前一律用原版美術（語言設定也有同樣的情況）。桌面與 Android 正常。
- 地板（`floor`）目前沒有 AI 版本（無縫拼接還沒處理）。
- 動畫幀尚未支援（每個角色一張圖）。

## 5. 程式在哪

| 檔案 | 內容 |
|---|---|
| `src/core/game/core/art_styles.h` | 讀 `styles.json`、風格索引 |
| `src/core/game/core/game_assets.cpp` | 啟動時依 `settings.artStyle` 組 sprite atlas（含尺寸統一） |
| `src/core/game/ui/title_screen.*` | 設定頁的風格列與確認視窗（`title_screen_test` 有測） |
| `src/core/game/core/game_ui.cpp` / `game_store.cpp` | 兩個設定頁的列、確認視窗文字、UI 圖片路徑 |
| `assets/data/text.json` | `settings.style_*` 六種語言 |
