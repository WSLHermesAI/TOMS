# 14 — Atlas 工具（sprite → 一張貼圖）

> 英文原文：[14_ATLAS_TOOL.md](../14_ATLAS_TOOL.md)

把許多小圖片變成一張貼圖加上一個描述檔。遊戲從一張貼圖繪製每個 sprite：
貼圖切換更少，web 上只要下載一次，而不是三十次。

**編輯器的畫面由遊戲的渲染器繪製。** Atlas、動畫和粒子編輯器用 toms_game 自己的 `BgfxRenderer`
繪製它們的畫布（`tools/studio_common` 中的 `GameCanvasView`，函式庫
`studio_bgfx`），所以編輯器顯示的和遊戲顯示的逐像素相同：
- **和遊戲相同的渲染：** 點取樣的 sRGB 貼圖、straight-alpha 和加法
  混色、相同的 sprite 批次，粒子則用 GPU 模擬。
- **遊戲內容**（atlas 頁面、正在編輯的 sprite、片段的 sprite、粒子）由
  渲染器繪製在畫布的原生視窗中。
- **編輯用的視覺元素**（外框、選取、控制點、軸心、9-slice 參考線、gizmo、標籤、
  狀態文字）保留它們的 QPainter 程式碼，以透明覆蓋層畫在上面。
- **每個編輯器行程一個 bgfx。** 它跟著正在顯示的畫布：atlas 編輯器的頁面和
  sprite 編輯畫布共用它。
- **座標提示**（檢視 > *顯示座標*，預設開啟），和 Cocos Creator 的場景視圖一樣：
  淡淡的格線，x 值沿著下邊緣，y 值沿著左邊緣。間距
  跟著縮放（1、2、5 × 10ⁿ 內容像素，至少相隔 90 螢幕像素）：縮小時每 500，
  放大時每 10 或每 1。數值是遊戲的座標（y 往下增加）。
- **復原 / 重做和備份**（三個編輯器都有）：
  - **復原 / 重做：** 編輯 > 復原 / 重做（Ctrl+Z / Ctrl+Y，或工具列箭頭）逐步走過
    每次編輯。
  - **歷程面板**（檢視 > 歷程；它是屬性旁邊的分頁）：列出每一步，最舊的
    在前。點一步就會復原或重做到那一步。
  - **自動備份：** 每 5 分鐘（只在有變更時）以及每 20 次編輯後寫一份副本。
    - **位置：** `%LOCALAPPDATA%\TOMS\<Editor>ackups\<file>\<file>_<date-time>.<ext>`，絕不
      放在檔案旁邊。
    - **數量：** 每個檔案保留最新的 20 份。
    - **開啟備份：** 備份中的 atlas 和圖片路徑是絕對路徑，所以可以直接從那裡開啟。
      另存新檔就能放回去。Atlas 備份是一個資料夾：專案和它打包好的 atlas。
    - **設定：** 檔案 > 備份 > 設定（每 N 分鐘、N 次編輯後、保留幾份、開
      / 關）。同一個選單有 *立即備份* 和 *開啟備份資料夾*。
- **QPainter 後備：** 檢視 > *用遊戲渲染器預覽*（預設開啟；重新啟動後生效）、
  離螢幕的 `--selftest`、bgfx 啟動失敗，以及獨立建置的 atlas
  工具（沒有遊戲引擎），都和以前一樣用 QPainter 繪製。
- **`--selftest-gpu`**（在螢幕上）用遊戲渲染器檢查每個編輯器，並存下 bgfx
  截圖：
  - `atlas_editor --selftest-gpu project.atlasproj outdir`：頁面、sprite 編輯模式，再回來；
  - `anim_editor --selftest-gpu clip.anim outdir`；
  - `particle_editor --selftest-gpu effects.particle outdir`。


```bat
:: the editor (Qt): open the game's atlas, edit, Ctrl+S
Build\windows-release\bin\atlas_editor.exe assets\media\atlas\game.atlasproj

:: the same without a window
Build\windows-release\bin\atlaspack.exe refs  assets\media\atlas\game.atlasproj --import all
Build\windows-release\bin\atlaspack.exe build assets\media\atlas\game.atlasproj --all-variants
```

（編輯器（Qt）：開啟遊戲的 atlas、編輯、Ctrl+S；下面兩行是不開視窗的做法。）

它取代舊的 FM79979 `PI.exe`（TextureEditor，C++/CLI WinForms），運作方式相同：
專案和它打包好的貼圖放在一起，圖片就存在貼圖裡。它仍然能讀寫
`.pi`。和 PI.exe 不同的是，配置是自動的，而且有命令列。

| 部分 | 位置 | 內容 |
|---|---|---|
| `atlas_core` | `tools/atlas/core` | C++17 函式庫，沒有 Qt、沒有引擎：專案檔、打包、匯出/匯入 |
| `atlaspack` | `tools/atlas/cli` | 命令列（無介面） |
| `atlas_editor` | `tools/atlas/qt` | Qt 6 編輯器；`atlas_editor --headless <atlaspack 參數>` 不開視窗執行 |
| web 編輯器 | `tools/atlas/web` | 同一份核心編譯成 WebAssembly，在瀏覽器中執行；`node atlaspack.mjs` 是無介面的 web/Node 版 |
| `atlas_file.h` | `src/core/engine` | 遊戲讀取 `.atlas` 檔的程式（只有標頭） |

## 1. 專案：圖片放在打包好的 atlas 裡

一個專案是一個資料夾，裡面有：

```
game.atlasproj     settings, child sprites, pivots, 9-slices, tags, animations, references (JSON)
game.atlas         the packed atlas: where each sprite is          ┐ the images live here:
game.png           the packed texture (game_1.png ... if more)     ┘ no other PNGs are needed
game.plist         the other output formats
```

（`game.atlasproj`：設定、子 sprite、軸心、9-slice、標籤、動畫、參考資料夾（JSON）；`game.atlas` 和 `game.png`：
打包好的 atlas 和貼圖，圖片就存在這裡，不需要其他 PNG；`game.plist`：其他輸出格式。）

- **儲存** 會寫出全部。編輯器的儲存也一定會寫出 `.plist`（給 Cocos Creator）。
- **開啟** 讀取 JSON，並把每個 sprite 以原始大小從打包好的貼圖中切回
  記憶體。PI.exe 就是這樣做的。原始的 PNG 檔不需要，可以刪除。
- **參考資料夾** 是美術來源（或將來的來源）的資料夾。建置從不讀取它們。
  *從參考資料夾匯入* 會把它們和專案比對，並把每個 PNG 列為 **新的**、
  **已改變** 或 **相同**。你選擇要帶入哪些，儲存時會存進打包好的貼圖。
- **加入圖片** 會把 PNG 的像素複製進來；PNG 檔本身不會被保留或引用。
- **匯出圖片** 會把每個 sprite 寫回成單獨的 PNG。
- **變體**（美術風格）用同樣的方式保存替換的美術，放在它們自己打包好的 atlas 中，並有
  自己的參考資料夾。

```json
{
  "name": "game",
  "embedded": true,
  "settings":   { "maxWidth": 2048, "maxHeight": 2048, "padding": 2, "extrude": 1, "trim": false,
                  "powerOfTwo": false, "dedupe": true, "filter": "Nearest", "heuristic": "auto" },
  "output":     { "dir": ".", "formats": ["atlas", "plist"] },
  "references": [ { "path": "../sprites", "recursive": false, "prefix": "" } ],
  "sprites":    [
    { "name": "ui/frame", "split": [8, 8, 6, 6] },
    { "name": "ui/frame_corner", "parent": "ui/frame", "rect": [0, 0, 16, 12] },
    { "name": "floor", "pivot": [0.5, 1.0], "tags": ["map"] }
  ],
  "animations": [ { "name": "coin_spin", "loop": true, "frames": [ { "sprite": "coin", "time": 0.1 } ] } ],
  "variants":   [ { "id": "dark16", "reference": "../styles/dark16/sprites", "images": ["bat", "wall"],
                    "output": { "dir": "../styles/dark16/atlas", "formats": ["atlas", "plist"] } } ]
}
```

- **output.dir** `"."` = 放在專案旁邊。那個資料夾放著打包好的 atlas，所以 *另存新檔* 到
  別處時會一起帶走。
- **sprites**：依名稱設定某個 sprite（軸心、9-slice `split` = 左、右、上、下、
  `tags`、`trim` 開/關、`pin` = `[page, x, y]` 固定它的位置），或是一個**子 sprite**。
- **variants**：`images` 列出這個風格替換的 sprite。它們的像素在變體自己
  打包好的 atlas 中；其他每個 sprite 保留原本的美術。
- **不會默默遺失任何東西。** 如果某張圖片無法打包（例如比
  最大頁面還大），儲存會以錯誤停止，什麼也不寫。

**資料夾專案**（沒有 `"embedded"`）是另一種：每次建置時都會掃描 `"sources"` 資料夾中的
PNG，`"file"` 加入單一 PNG，變體的 `"overrides"` 資料夾替換美術。
`atlaspack pack` 產生這種專案，用於腳本中「打包這個資料夾」的工作。編輯器開啟它時會轉成
內嵌專案，`atlaspack embed` 從命令列做同樣的事。

### 子 sprite（沒有像素的 sprite）

子 sprite 指定另一個 sprite 中的一個矩形：`"parent"` + `"rect": [x, y, w, h]`，以父 sprite
原始（未裁切）的像素計算。它沒有自己的圖片，所以背景可以分塊重複使用
（對話框 → 9-slice 的角和邊、tileset → 單一圖塊），不必複製像素。

- 子 sprite 還可以有子 sprite；矩形相對於它自己的父 sprite。
- 裁切永遠不會切進子 sprite：父 sprite 至少保留它的子 sprite 用到的區域。
- 在美術大小不同的變體中，子 sprite 的矩形會隨它的根圖片縮放。
- `"bake": true` 把子 sprite 的像素複製成一個獨立的 sprite，有自己的 padding 和
  extrude，用於必須平鋪、或濾波時不能滲入鄰近像素的區塊。
- 錯誤（建置停止，什麼也不寫）：矩形超出父 sprite、找不到父 sprite、迴圈。

FM79979 的 `.pi` 格式已經有這個功能（「PuzzleUnitChild」）；本工具能讀寫它。

## 2. 輸出格式

每種格式都來自同一次建置，所以永遠一致。在 `output.formats` 中選任意組合。
內嵌專案一定會寫 `atlas` 格式（圖片就存在那裡），
編輯器一定會加上 `plist`（給 Cocos Creator）。

| 格式 | 檔案 | 由誰讀取 |
|---|---|---|
| `atlas` | `name.atlas` + 頁面 | **TOMS**、libGDX、Spine 4 執行環境、Godot 匯入器。主要格式 |
| `atlas-spine3` | `name.atlas`（或和 `atlas` 並存時為 `name.spine3.atlas`） | 讀固定行數的舊執行環境（Spine 3.x）；沒有擴充欄位 |
| `plist` | 每頁一個 `name.plist` | **Cocos Creator**（Sprite Atlas 資源）、cocos2d-x — TexturePacker 格式 3 |
| `tp-json` | 每頁一個 `name.json` | Phaser、PixiJS（TexturePacker JSON hash；Pixi 用 `animations`） |
| `pi` | 每頁一個 `name.pi` | FM79979 引擎（`cPuzzleImage`）、舊的 PI 編輯器 |
| `rcss` | `name.rcss` | RmlUi：每頁一個 `@spritesheet`，給其他 RmlUi 專案用（TOMS 自己從 `.atlas` 在記憶體中產生） |

頁面是 PNG：`name.png`、`name_1.png`、… 只有在 sprite 放不進
最大尺寸時才會產生第二頁。

### 擴充欄位

標準的解析器必須仍然能用，所以每個 sprite——包括子 sprite——都以一般區域寫出，
帶有它真正的位置。額外資料以各格式允許的方式加入，名稱以 `x_` 開頭：

```
game.png
size: 214, 178
format: RGBA8888
filter: Nearest, Nearest
repeat: none
ui/frame
  bounds: 0, 0, 64, 48
  split: 8, 8, 6, 6
ui/frame_corner
  bounds: 0, 0, 16, 12
  x_parent: ui/frame
  x_local: 0, 0, 16, 12
  x_pivot: 0.5, 1
  x_tags: ui
```

| 欄位 | 意義 |
|---|---|
| `x_parent` / `x_local` | 子 sprite：父 sprite 名稱、在父 sprite 原始像素中的矩形 |
| `x_pivot` | 軸心，從左上角起算，原始大小的 0..1（不是 0.5, 0.5 時才寫出） |
| `x_tags` | 標籤 |

libGDX 和 Spine 4 會保留未知的區域欄位（`names`/`values`），並略過未知的頁面欄位。在
plist 中，同樣的鍵是 frame 字典中額外的項目（`x_parent`、以 `{{x,y},{w,h}}` 表示的 `x_local`、
`x_split`、`x_tags`），軸心則用標準的 `anchor`。在 TexturePacker
JSON 中，它們放在每個 frame 的 `"x"` 底下。在 `.pi` 中，子 sprite 是一般的 `PuzzleUnit` 加上一個
`PuzzleUnitChild` 元素，和舊編輯器寫的完全一樣。

Cocos Creator 的 sprite 不用 `.atlas`（只有 Spine 骨架用）：給它 `plist`。

尚未驗證：Cocos Creator 3.x 是否會忽略額外的 frame 鍵，以及 Java 版 libGDX 和
Spine 執行環境能否載入擴充過的 `.atlas`。這些鍵遵循兩種解析器文件中的規則（未知的鍵
會被保留或略過），但還沒有人在那些引擎中載入過這些檔案。要在那裡依賴它們之前，請先試過。

### 輸出遵守的規則

- Sprite 永遠不會旋轉（`.pi` 載入器沒有旋轉，像素美術也不需要）。
- 相同的輸入 → 在每台機器上（以及 web 版中）產生逐位元組相同的檔案。沒有改變的檔案
  不會被重寫，所以它們的時間戳記不會觸發重新建置。
- `.pi`：`Count` = `PuzzleUnit` 的數量；unit 依名稱排序；`UV` 是半開區間矩形加上
  精確的 `ShowPosInPI`；`OriginalSize ≥ Size`。

## 3. 打包設定

| 設定 | 意義 |
|---|---|
| `maxWidth` / `maxHeight` | 最大頁面；sprite 更多 → 頁面更多 |
| `powerOfTwo`、`square`、`fixedSize` | 頁面大小規則。沒有這些時，頁面會縮小到剛好放得下（二分搜尋） |
| `padding` | sprite 之間的空白像素（真的是在它們之間：舊編輯器的間隔少了一個） |
| `border` | 沿著頁面邊緣的空白像素 |
| `extrude` | 在每個 sprite 周圍重複邊緣像素，讓濾波/縮放永遠不會取樣到鄰居 |
| `trim`、`alphaThreshold` | 裁掉透明邊緣；原始大小和偏移保留在輸出中 |
| `dedupe` | 相同的圖片（裁切後）共用同一個位置 |
| `heuristic` | MaxRects 規則：`auto`（依序試 bssf、blsf、baf、bl、cp，取第一個放得下的），或 `shelf`（一列一列排，像舊編輯器） |
| `premultiplyAlpha`、`filter` | 寫進檔案給引擎用 |

固定的 sprite（`"pin"`，或在編輯器中拖曳）保持它們的位置；其他的 sprite 圍繞它們打包。

## 4. 在 TOMS 中

`assets/media/atlas/` 是專案資料夾：`game.atlasproj` + `game.atlas` + `game.png`（+ `.plist`）。
`dark16` 美術風格打包好的 atlas 在 `assets/media/styles/dark16/atlas/`。參考資料夾是 `assets/media/sprites` 和 `assets/media/styles/dark16/sprites`。

- **遊戲**（`Game::loadPrebuiltSpriteAtlas`，`src/core/game/core/game_assets.cpp`）為原版美術載入
  `atlas/game.atlas`，為某個風格載入 `styles/<id>/atlas/game.atlas`。它上傳
  頁面（`IRenderer::loadSpriteAtlas`），並從中取得每個 sprite 的 UV。裁切過的 sprite 仍然
  填滿它的格子，因為 `spriteQuad` 會用裁切偏移。整個 `toms::AtlasFile`（子 sprite、
  軸心、9-slice、標籤）都可以透過 `Game::spriteAtlas()` 取得，所以程式碼可以依名稱查詢
  sprite。如果檔案不存在或缺少某個 sprite，遊戲會記錄原因，並和以前一樣在啟動時建立舊的格狀 atlas。
  透過 atlas 繪製的地圖場景和格狀版逐像素相同（以 `image_diff` 檢查）。
- **檔案是提交進 repo 的；建置時不產生任何東西。** 儲存專案就會寫出它們。
  CTest `atlas.assets_up_to_date`（`atlaspack build --check`）檢查專案只靠
  打包好的 atlas 就能開啟，而且其他檔案（plist、風格的 atlas）都一致。
- **修改美術：** 在參考資料夾中編輯 PNG，然後在編輯器中執行 *從參考資料夾匯入*
  （它會把那個 PNG 列為 *已改變*），再儲存。不開視窗：
  `atlaspack refs assets\media\atlas\game.atlasproj --import changed`。
- **UI（RmlUi）：** UI 從同一個 atlas 繪製它的圖示。啟動時，遊戲把載入的
  `game.atlas` 在記憶體中轉成 RmlUi 的 `@spritesheet`（`_atlas.rcss`，[08](08_RMLUI.md)），`.rml`
  檔則以名稱指定 sprite（`data-attr-sprite`）。重新打包 atlas 永遠不需要修改 UI。重新命名或
  刪除程式碼、`assets/data` 或某個 `.rml` 仍在使用的 sprite，會讓 `atlas_sprites_test` 失敗。
  `media/sprites` 中的 PNG 現在只是參考資料（以及沒有 atlas 時後備格狀 atlas 的來源）。
- **新的美術風格：** 在專案中加一個變體（[13](13_ART_STYLES.md)，第 5 步）。

## 5. 命令列

```
atlaspack new     <OUT.atlasproj> [--source DIR]... [--format LIST] [options]
atlaspack add     <project> <png|folder>... [--prefix P] [--variant ID] [--replace]
atlaspack refs    <project> [--variant ID] [--import new|changed|all]
atlaspack extract <project> --out DIR [--variant ID] [--children]
atlaspack embed   <folder-project> [--save-as NEW.atlasproj] [--keep-output]
atlaspack build   <project> [--variant ID | --all-variants] [--out DIR] [--format LIST] [--strict] [--check]
atlaspack import  <file.pi|.atlas|.json> --project OUT.atlasproj [--images DIR] [--format LIST]
atlaspack pack    <folder>... --out DIR [--name NAME] [--format LIST] [options] [--save-project FILE]
atlaspack info    <file.pi|.atlas|.json>
atlaspack formats
```

| 指令 | 做什麼 |
|---|---|
| `new` | 一個內嵌專案；`--source` 資料夾會被匯入並保留為參考資料夾 |
| `add` | 把 PNG（檔案或整個資料夾）複製進專案並儲存 |
| `refs` | 把每個參考 PNG 列為新的 / 已改變 / 相同；`--import` 把它們帶進來並儲存 |
| `extract` | 把每個 sprite（加 `--children` 時包括每個子 sprite）再寫成單獨的 PNG |
| `embed` | 資料夾專案 → 內嵌專案；它的資料夾變成參考資料夾 |
| `build` | 重新建置並寫出輸出檔；`--check` 只比對 |
| `import` | 從現有的 `.pi` / `.atlas` / TexturePacker `.json` 建立內嵌專案 |
| `pack` | 資料夾直接變成 atlas 檔（資料夾專案，給腳本用） |

選項：`--max WxH --pot --square --padding N --border N --extrude N --no-trim
--alpha-threshold N --no-dedupe --heuristic auto|bssf|blsf|baf|bl|cp|shelf --filter Nearest|Linear`。
結束碼：0 正常，1 搭配 `--strict` 時有警告（或搭配 `--check` 時有過時的檔案），2 錯誤，3 命令列
錯誤。訊息格式是 `file: error: sprite: text`，每則一行。路徑和名稱可以是任何 Unicode
（內部用 UTF-8；在 Windows 上命令列以 UTF-16 讀取）。

**把舊的 PI 專案搬過來：**

```bat
atlaspack import old\UI.pi --project ui\ui.atlasproj     :: ui.atlasproj + ui.atlas + ui.png + ui.pi
```

每個 unit 以原始大小切出來，裁切掉的像素放回它的偏移位置。
`PuzzleUnitChild` unit 變成子 sprite，序列動畫也會一起帶過來。三角網格
（`.ti`）和變形（`.mx`）不會帶過來（見第 7 節）。`--images DIR` 改成把 unit
寫成 PNG，並建立資料夾專案。

## 6. 測試

`atlas_core_test`（CTest `atlas.core`）：

- **打包器**：沒有重疊、遵守 padding、在頁面內、結果確定、固定的矩形位置精確。
- **建置**：裁切、子 sprite（巢狀、在裁切過的父 sprite 底下、bake）、去重、2 倍大小的變體。
- **格式**：每種格式都會匯出並讀回。`.atlas` 經過遊戲自己的
  `atlas_file.h`。每個 sprite 繪製的必須剛好是它來源的像素。
- **內嵌專案**：
  - 把資料夾專案轉成內嵌並存到別處，刪除它原始的 PNG，再
    重新開啟。每個 sprite 和每個變體 sprite 都必須繪製相同的像素。
  - 再次儲存時，除了專案檔之外什麼都不重寫。
  - 參考狀態（新的 / 已改變 / 相同）正確。
  - 太大而無法打包的圖片會讓儲存拒絕並什麼也不寫。
  - `extract` 把每個 sprite 寫成 PNG。
  - 把 `.pi` 匯入成內嵌專案，再存回 `.pi`。
- **基準檔**：來自舊編輯器的 `tests/data/9Slicing_TalkingDialob.pi` 被逐像素重建。

```bat
set ATLAS_PI_CORPUS=D:\Work\MagicTowerOfSoercer\MagicTower
Build\windows-release\bin\atlas_core_test.exe
```

這會對那個資料夾底下每個 `.pi` 執行同樣的來回測試（匯入 → 重建 → `.pi` → 比對每個 unit 繪製的內容）。
MagicTower 目錄中所有 95 個可讀的檔案都通過。那裡有一個檔案是空的，
另一個缺少 PNG。

## 7. 刻意省略的功能

- **三角網格和變形**（`.ti`、`.mx`，舊的 Triangulator 分頁）：FM79979 的舊功能，
  在 TOMS 中沒有用途。舊的 PI.exe 仍可編輯它們。
- **打包時旋轉**：見 §2。
- **`.pib`/`.pngb`**（FM79979 的 Huffman 壓縮副本）和 **DDS**：不寫出。單純的 `.pi` + `.png` 到哪裡都能載入。
