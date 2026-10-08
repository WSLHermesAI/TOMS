# 08 — 遊戲 UI（RmlUi）

> 英文原文：[08_RMLUI.md](../08_RMLUI.md)

玩家看到的每個畫面都是一份 [RmlUi](https://github.com/mikke89/RmlUi) 6.3 文件：RML（類似 HTML）負責
結構，RCSS（類似 CSS）負責外觀。涵蓋標題、HUD、畫面上的方向盤、戰鬥、對話、物品欄、
商店、遊戲內選單、樓梯和商店解鎖的彈出視窗、關卡選擇、結局、提示和通知。遊戲本身只繪製
世界（地圖、實體、玩家），再加上兩個 ImGui 開發者工具（F1 除錯覆蓋層、F2 樣式測試）。桌機和 web
都一樣。

## 東西放在哪裡

| 路徑 | 內容 |
|---|---|
| `assets/media/ui/*.rml` | 每個畫面一份文件（`title`、`hud`、`pad`、`battle`、`dialogue`、`inventory`、`store`、`menu`、`dialogs`、`stage_select`、`ending`、`overlay`） |
| `assets/media/ui/*.rcss` | 它們的樣式；`common.rcss` 放共用的樣式（按鈕、列、面板、色盤、捲軸） |
| `assets/media/fonts/NotoSansCJKtc-TOMS.otf` | 桌機預設的 UI 字型：把 Noto Sans CJK TC 精簡到遊戲用到的字元（約 790 KB）。web 版沒有 |
| `tools/make_ui_font.py` | 重建那個字型；`--check` 回報缺少的字元 |
| `assets/data/text.json` → `languages[].font` / `web_font` | 各語言的字型，見 [字型](#字型) |
| `src/core/game/ui/ui_state.h` | **契約**：畫面能顯示的每個值（`UiState`），都是單純的資料 |
| `src/core/game/core/game_ui.cpp` | `Game::buildUiState()` 每個畫面填入它；`Game::uiEvent()` 處理每個按鈕 |
| `src/game/src/game_ui.*` | 把 `UiState` 綁定到 RmlUi，作為資料模型 `game`，載入文件，顯示和隱藏它們 |
| `src/engine/src/rml_ui.*` | bgfx 上的 RmlUi：render interface、system interface、字型載入、除錯器 |
| `src/engine/src/rml_canvas_font.*` | 只有 web：在 HTML canvas 上實作的 RmlUi 字型引擎（由瀏覽器繪製文字） |
| `src/engine/shaders/vs_rml.sc`、`fs_rml.sc` | RmlUi 的 shader（`fs_rml.sc` 把 UI 借用的遊戲 straight-alpha sprite atlas 轉成預乘 alpha） |

規則留在 `Game` 中，文字透過 `Locale` 來自 `data/text.json`，外觀則在 `.rcss`
檔中。宿主中沒有任何 UI 邏輯。

## 一個畫面如何運作

```
Game::buildUiState(UiState&)     every frame: what to show (all text already translated)
        |  data model "game"     {{battle.log}}, data-for="r, i : menu.rows", data-if="store.visible", ...
        v
assets/media/ui/*.rml            layout + style
        |  act('name', arg)      data-event-click="act('inv_use')", "act('menu_row', i)"
        v
Game::uiEvent(name, arg)         calls the same Game functions the keyboard uses
```

（每個畫面：`Game::buildUiState` 決定要顯示什麼（文字都已翻譯）→ 透過資料模型 `game` 綁定到 `.rml` 的配置和樣式 →
按鈕以 `act('name', arg)` 呼叫 `Game::uiEvent`，它呼叫和鍵盤相同的 `Game` 函式。）

- **顯示和隱藏。** `GameUi::sync()` 在某個區段可見時顯示它的文件（`hud.visible`、
  `battle.visible`、…；表格是 `game_ui.cpp` 中的 `kDocs`）。疊放順序是每份文件的 `z-index`：HUD 10、
  方向盤 20、戰鬥/對話/物品欄 30、商店 40、選單和關卡選擇 45、彈出視窗 50、標題 60、結局 70、覆蓋層 90。
- **鍵盤。** 按鍵和以前一樣運作（`GameSession::frame` 呼叫 `Game` 的函式）。UI 只顯示目前的
  選取，所以鍵盤和滑鼠永遠一致。
- **滑鼠和觸控。** 所有事件都先送到 RmlUi。HUD、方向盤和覆蓋層除了按鈕之外都是
  `pointer-events: none`，所以點在地圖上的事件會穿過去，交給 `Game::handleTouch`（點擊移動）。
- **清單。** 把 `data-for` 放在單純的外層元素上，綁定（`data-class-*`、`data-style-*`）放在它的子元素上。如果
  綁定直接放在 `data-for` 元素上，RmlUi 會讀到正在被移除的列，每當清單變短時就會記錄「Data array index out
  of bounds」。

## 編輯 UI

執行遊戲，編輯一個 `.rml`/`.rcss` 檔，再按 **F5**：每份文件都會從磁碟重新載入。**F8** 開啟 RmlUi 的
除錯器（元素樹、計算後的樣式、配置框）。右上角黃色的「!」表示 RmlUi 記錄了一則
警告；除錯器會顯示是哪一則。

- **單位：** `px` 是設計像素。每份文件都以 1024×768 配置，再縮放到視窗大小，和地圖一樣加黑邊。
- **顏色：** 你寫的值就是螢幕上的顏色。Shader 會為桌機的 sRGB backbuffer 轉換；
  web 沒有 sRGB backbuffer，所以在那裡直接通過。
- **遊戲 sprite（圖示）：** 從打包好的 atlas 依名稱取用：`<img sprite="coin"/>`，或用
  `data-attr-sprite="..."` 綁定（模型中存的是 sprite 名稱，`Game::uiSprite`）。顯示 sprite 的文件要連結
  `<link type="text/rcss" href="_atlas.rcss"/>`。這個樣式表不是檔案：遊戲在記憶體中用它為地圖載入的
  atlas 產生它（`Game::uiSpritesheet`），所以 UI 永遠和地圖及美術風格一致。
  風格改變時，文件會用新的樣式表重新載入。如果程式碼、`assets/data` 或某個 `.rml` 用到的 sprite 名稱
  不在任何 atlas 中，`atlas_sprites_test` 會失敗（[14](14_ATLAS_TOOL.md)）。
- **其他圖片：** `src="..."`，相對於文件。以 sRGB 載入，點取樣。
- **RmlUi 沒有預設樣式表。** `common.rcss` 讓 `div`/`p` 成為 block；其他元素都需要設定 `display`。
- **新文字（桌機）：** 加入任何地方都還沒用過的字元之後（新的對話台詞、`.rml` 中的新符號），
  執行 `python tools/make_ui_font.py`，需要 Python 3 和 fontTools。`--check` 列出
  缺少的字元。在桌機上，RmlUi 對缺少的字元什麼也不畫。Web 版不受影響（見字型）。

## 字型

兩種版本取得文字的方式不同。

**Web：由瀏覽器繪製文字。** Web 版中沒有字型檔，也沒有 FreeType。`rml_canvas_font.cpp` 是
在 HTML canvas 上實作的 RmlUi 字型引擎：寬度來自 `measureText()`，每個不同的字串用
`fillText()` *整串*畫進一個小的快取貼圖。因為畫的是整串字串，瀏覽器能處理所有
逐字形渲染做不到的事：
- 阿拉伯文和波斯文的字母連寫，以及由右至左的段落；
- 印度系文字和泰文的字形組合；
- 依語言不同的 CJK 字形變體；
- emoji；
- 任何字元的字型後備。

字串以螢幕真正的像素密度繪製，所以在手機上文字依然清晰。字型就是裝置上
有的字型，依 `text.json` 中該語言的 `web_font` CSS 清單挑選：

```json
{ "code": "zh_TW", "name": "繁體中文",
  "web_font": "'PingFang TC', 'Microsoft JhengHei', 'Noto Sans TC', 'Noto Sans CJK TC', sans-serif" }
```

第一個已安裝的字型勝出，最後的 `sans-serif` 一定會匹配。請把 macOS/iOS、Windows 和 Android 的字型名稱
都放進清單；沒有 `web_font` 時使用瀏覽器的預設字型。語言標籤（`zh-TW`）也會傳給 canvas，
所以中文和日文會有各自的字形。因為不同裝置的字型不同，固定大小的框請留一點空間。

**桌機：字型檔（FreeType）。** 預設是 `assets/media/fonts/NotoSansCJKtc-TOMS.otf`。它也是其他字型缺少某個字元時的
**後備字型**。某個語言可以指定自己的字型：

```json
{ "code": "ja", "name": "日本語", "font": "fonts/NotoSansJP-Regular.otf" }
```

路徑相對於 `assets/media`，或是絕對路徑（`.ttf`、`.otf` 和 `.ttc`，取第一個 face）。當遊戲切換到
那個語言且檔案存在時，每份文件都會使用它（字型家族 `toms-<code>`）。如果檔案不存在，遊戲會記錄下來並
維持預設字型。FreeType 不做文字的字形組合，所以需要它的文字（阿拉伯文、希伯來文、印度系文字）在桌機上
還無法正確顯示；之後 RmlUi 的 HarfBuzz 字型引擎可以補上。在 web 上它們已經可以正常顯示。

## 新增畫面

1. 把它的值加進 `ui_state.h`（一個帶 `visible` 的 `UiXxx` struct，加上 `UiState` 中的一個成員）。
2. 在 `registerTypes()` 中註冊這個 struct 的成員，在 `GameUi::init`（`game_ui.cpp`）中加一個 `c.Bind(...)`，並
   把文件和它的可見性判斷加進 `kDocs`。
3. 在 `Game::buildUiState()` 中填入它，在 `Game::uiEvent()`（`game_ui.cpp`）中處理它的按鈕。
4. 撰寫 `assets/media/ui/xxx.rml` + `.rcss`：連結 `common.rcss`，設定 `data-model="game"` 和 `z-index`。

## 不支援的功能

Render interface 能繪製方框、邊框、圓角、圖片、文字、變形、過渡、動畫和
裁切（`overflow`）。RmlUi 6 的 layer、filter、mask、`box-shadow` 和 shader 漸層（`linear-gradient` 之類）
沒有實作。幾何漸層 `horizontal-gradient`/`vertical-gradient` 可以用。

## 歷史

- 2026-09-27：web 文字改由瀏覽器繪製（canvas 字型引擎）；各語言字型（`text.json` 中的 `font` / `web_font`）。
  Web 下載量從約 2.6 MB → 1.7 MB gzip。
- 2026-09-27：先以商店作為測試用 RmlUi 重寫，接著所有畫面都移到 RmlUi。手繪的 UI 程式碼、
  遊戲自己的字型 atlas（`font.cpp`、`stb_truetype`、web 上的瀏覽器 canvas 字型）以及舊的字型下載
  （文泉驛正黑、Windows 的 `msjh.ttc` 後備）都已移除。
