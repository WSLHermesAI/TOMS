# 09 — 圖片字型（字型工具）

> 英文原文：[09_FONT_TOOL.md](../09_FONT_TOOL.md)

舊老虎機專案的分數與計數文字（副本在 `tests/font`：`connect_score.fnt`、`total_score.fnt`…）不是用 TTF 繪製的：每個字元都是手繪圖片，
合併在一張 PNG 圖頁裡，並由 BMFont 文字格式的 `.fnt` 描述。字型工具把**你加入、並為每張設定字元的圖片**合併成同樣的一對檔案，
並用它繪製測試文字。操作步驟見使用說明 [10_FONT_TOOL_GUIDE.md](10_FONT_TOOL_GUIDE.md)。

| 部分 | 位置 | 內容 |
|---|---|---|
| 執行期 | `src/core/engine/bitmap_font.h/.cpp` | `BitmapFont`：解析 `.fnt`（BMFont "Arial" 排版與 convert2fnt-web）、排版 UTF-8 文字、產生給 `IRenderer` 的 `Quad`；`FontText`：load / setText / setPosition / setAlign / render |
| 核心 | `tools/font/core/font_build.h/.cpp` | 圖片 → 圖頁 + `.fnt`；字型 → 圖片；預覽繪製 |
| `fontpack` | `tools/font/cli` | 命令列：build、split、preview、info |
| `font_editor` | `tools/font/qt` | 視窗：加入圖片、輸入每張的字元、有即時預覽的測試文字、儲存 |
| 測試 | `tools/font/tests`、`tests/font` | `font.tool`、`font.editor_selftest` |

## 1. 輸入：圖片與其字元

每個字元一張 PNG（`fonttool::GlyphPicture`：圖片 + 碼位）。在 `font_editor` 中可一次加入一張或多張圖片，並在每列輸入它的字元；
`checkGlyphs` 列出沒有字元或與前面圖片字元重複的圖片，有問題時不會建置。檔名本身是字元時會自動猜出（`charFromName`）：
`0.png`、`K.png`、`中.png`；`dot.png`、`comma.png`、`space.png`、`slash.png`、`times.png`（×）等檔名放不下的字元名稱；`U+00D7.png`。
`fontpack build` 以檔案方式接收圖片，字元來自檔名，或依序來自 `--chars "0123456789.,KM"`；資料夾參數代表其中的 PNG 與 `chars.txt`
（`檔名=字元` 的行，也決定順序）。

每張圖片**原樣**使用：寬度就是前進距離，高度就是字格。高度不同的圖片以行的底部對齊。完全透明的圖片只提供前進距離（空白）。
沒有空白圖片時會加入空白。空白寬度（`spaceAdvance`，預設 `defaultSpaceAdvance`：空白圖片的寬度，否則 `1` 圖片的寬度，否則 5）
與行高（`lineHeight`，`\n` 往下移的距離；預設 `defaultLineHeight`：最高圖片）都可設定；字元仍對齊最高圖片的基線（`base`）。
圖頁上的順序就是清單順序。

`glyphsFromFont` 把現成的字型還原成圖片（空白：與其前進距離同寬的透明圖片），編輯器就是這樣開啟 `.fnt` 來修改。底部對齊以外的位移不會保留；
舊字型都沒有這類位移。

## 2. 輸出

`NAME.fnt` + `NAME.png`。`.fnt` 的文字一律是 `tests/font/connect_score.fnt` 的 convert2fnt-web 格式（其中 `info face="…"`
是字型名稱，只是標籤，沒有程式會讀）。圖片在圖頁上的位置由 `BuildOptions::layout`（`PageLayout`）決定：

| 版面 | `--layout` | 圖頁 |
|---|---|---|
| `Row`（預設） | `row` | 圖片從 (1, 1) 起並排成一列，間隔 1 px，超過最大尺寸就換列：舊字型的排版。`font.tool` 檢查 `tests/font/connect_score_pictures` 的圖片重建出的 `connect_score.fnt` 逐位元組相同，PNG 逐像素相同 |
| `Tight` | `tight` | atlas 工具的打包器 `atlas::packPages`（MaxRects，嘗試每種放置規則，取最小圖頁再壓縮）：邊框與間隔 1 px，邊長不超過最大尺寸 |
| `TightPow2` | `pot` | 同上並設定 `PackSettings::powerOfTwo`：寬高都是 2 的次方（`connect_score`：1024 × 128） |

版面只會移動圖片在圖頁上的位置：`font.tool` 檢查每種版面繪製的測試文字逐像素相同、字元互不相碰、每個字元保留原像素。最大尺寸內放不下時會是錯誤。

## 3. fontpack

```bat
fontpack build   a.png b.png c.png --chars "0.," --out assets\media\fonts\score.fnt [--layout row|tight|pot] [--face F] [--space N] [--line-height N] [--advance-pad N] [--max-width N]
fontpack build   art\0.png art\1.png art\dot.png --out assets\media\fonts\score.fnt   :: 字元來自檔名
fontpack preview tests\font\connect_score.fnt --text "12,345.6K" --out preview.png [--scale 0.5] [--bg 0F0F1A]
fontpack split   tests\font\total_score.fnt --out art\total_score   :: 字型 -> 圖片 + chars.txt
fontpack info    tests\font\connect_score.fnt
```

結束碼：0 成功，1 警告（有檔案被略過、文字用到字型沒有的字元），2 錯誤（圖片沒有字元或字元重複、檔案無法讀取），3 命令列參數錯誤。
`font_editor --headless <指令>` 執行的是同樣的指令。

## 4. font_editor

`font_editor [a.png b.png … | font.fnt]`。圖片表格：圖片、**Character**（可編輯：單一字元、`space` / `dot` 等名稱或 `U+XXXX`）、
碼位、尺寸、來源。**Add pictures…**（或把 PNG 拖進視窗）、**Remove**、**Up** / **Down**（圖頁順序）、**File → New**。紅色的 Character 格
是 `checkGlyphs` 找到的問題，列在表格下方並會阻止儲存。每次修改都會用沒有問題的列在記憶體中重建字型，所以預覽永遠顯示目前的內容。
**Test text**：每按一鍵預覽就重繪；Enter 換行；字型沒有的字元會被略過並計數。顯示：倍率、背景（棋盤格、遊戲背景色、黑、白）。
**Combined picture**（`PageView`）：建置後的圖頁縮放到面板大小，顯示尺寸（是 2 的次方時標示 "power of 2"），每個字元都有外框；
選取列的字元會被標示（其餘變暗），點擊字元會選取它的列。字型：字型名稱（`face`，只是標籤）、圖頁版面（一列 / tight / tight 2 的次方）、
圖頁最大尺寸、空白寬度、行高（兩者 *auto* 時顯示其數值）、字距。**Save font as…** 寫出 `.fnt` + `.png`。**Open font (.fnt)** 以字型的字元
取代所有列（`glyphsFromFont`）；不修改直接儲存會得到相同的 `.fnt`。

預覽透過 `BitmapFont::layout` 繪製，與遊戲排版文字的程式碼相同，並像遊戲的精靈一樣使用最近取樣。

## 5. 在遊戲中繪製

```cpp
FontText score;
score.load(renderer, "assets/media/fonts/connect_score.fnt");   // 一次
score.setText("12,345");                                          // 在 update / 事件中
score.setPosition(1200, 40);
score.setAlign(BitmapFont::Align::Right);
score.render();                                                   // 在 render 中，begin() 與 end() 之間
```

`FontText`（`bitmap_font.h`）透過 `vfsReadAll`（桌面、web、Android）讀取 `.fnt` 與其圖頁，用 `IRenderer::loadTexture` 上傳圖頁，
並保留四邊形直到文字、位置、對齊、倍率或顏色改變，所以每幀 `render()` 成本很低。另有 `setScale`、`setColor`、`setVisible`、
`width`/`height`、`missingChars`、`release`。已載入的 `FontText` 的複本共用字型與貼圖。完整表格見
[10_FONT_TOOL_GUIDE.md](10_FONT_TOOL_GUIDE.md) 第 5 節。底層的 `BitmapFont` 提供 `layout()`、`measure()`、`appendQuads()` 供自訂繪製。

某個字母只有一種大小寫時會改用另一種：舊字型只有 `K M` 或 `k m`。`bitmap_font.cpp` 是遊戲 `toms_core` 的一部分
（`src/game/CMakeLists.txt`），所以遊戲程式可直接引入 `bitmap_font.h` 並使用 `FontText`。工具則另外把它建置為 `toms_bitmap_font`
（`tools/font/CMakeLists.txt`，含解碼圖頁用的 `stb_image_impl.cpp`）；不要把 `toms_bitmap_font` 與 `toms_core` 連結進同一個程式（兩者都定義了 stb_image）。

## 6. 測試

- `font.tool`（`font_tool_test tests/font <暫存資料夾>`）：執行期的解析與排版（位移、字距調整、倍率、對齊、換行、UTF-8、舊 BMFont
  排版、大小寫替代）；檔名；`tests/font/connect_score_pictures` 的圖片 → 逐位元組相同的 `connect_score`；split → build；換列、字距與
  `chars.txt`；手動設定字元的圖片、缺少與重複的字元、字型還原成圖片；`tests/font` 中每個 `.fnt` 都能載入、繪製 0–9 並還原成圖片；
  tight 與 2 的次方版面（字元互不相碰、保留像素、寬高為 2 的次方、文字繪製與一列版面相同）；
  在假渲染器上的 `FontText`（load、set*、render、複本、release）；以及用 0.5 倍繪製的測試文字 `12,345.6K / 9,999M 0`，與
  `tests/font/preview.png` 比對。若是刻意變更繪製結果，請用 `TOMS_UPDATE_GOLDEN=1` 執行一次，查看新的 `preview.png` 後再提交。
- `font.editor_selftest`：`font_editor -platform offscreen --selftest tests/font/connect_score_pictures <輸出資料夾>`。加入圖片、加入一張沒有
  字元的圖片並確認無法儲存、輸入 `%`、重複的 `7` 與 `U+0025`、刪除與調整列的順序、選取一列（合併圖片上標示其字元）與點擊字元（選取其列）、
  切換到 2 的次方版面並儲存、輸入測試文字、調整倍率、儲存、開啟儲存的字型再存一次
  （相同的 `.fnt`），並執行 `--headless info`。也會在輸出資料夾留下 `window.png` 與 `preview.png` 供查看。

`tools/test_select.py` 會在 `tools/font`、`tests/font` 或 `bitmap_font.*` 有任何變更時執行這兩個測試。
