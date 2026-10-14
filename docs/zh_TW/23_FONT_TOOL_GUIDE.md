# 23 — 字型工具使用說明

> 英文原文：[23_FONT_TOOL_GUIDE.md](../23_FONT_TOOL_GUIDE.md)

如何用字型工具製作圖片字型。給程式人員的技術文件在 [22_FONT_TOOL.md](22_FONT_TOOL.md)。

## 工具用途

加入字元的圖片（每個字元一張 PNG），為每張圖片設定它的字元，工具就會把它們合併成圖片字型：一個 `.fnt` 檔加一張 `.png`
圖頁。格式與 `tests/font` 裡的舊分數字型相同（`connect_score.fnt`、`total_score.fnt`…：BMFont 文字格式、convert2fnt-web
排版），所以舊字型與這個工具做的字型可以互換，舊字型也能開啟、修改後再儲存。

工具有兩種形式：

| 工具 | 用途 | 位置 |
| --- | --- | --- |
| `font_editor` | 手動製作字型：加入圖片、設定每張圖片的字元、輸入測試文字即時預覽、儲存 | `Build\windows-release\bin\font_editor.exe` |
| `fontpack` | 腳本與批次工作：由圖片建置、輸出預覽 PNG、把字型拆成圖片、列出資訊 | `Build\windows-release\bin\fontpack.exe` |

兩者都隨 TOMS 一起建置（`tools\build.cmd`）。

## 1. 圖片

每個字元一張 PNG，可以放在磁碟上任何地方，不必在同一個資料夾。

- 圖片原樣使用，不裁切：**寬度就是前進距離**（下一個字元從哪裡開始），高度就是字格。想要的間距請留在圖片裡，或之後用 Letter spacing 加上。
- 高度不同的圖片以最高那張圖片的**底部**對齊。
- 完全透明的圖片不繪製任何東西，只提供前進距離，可用來設定空白的寬度。
- 字型一定有**空白**。預設寬度：有加入空白圖片時用它的寬度，否則用 `1` 圖片的寬度，都沒有時為 5 px。可在 **Space width** 修改。
- **行高**（換行 `\n` 時往下移的距離）預設為最高圖片的高度。可在 **Line height** 修改；每行內的字元位置不變。
- 字元在圖頁上的順序就是清單的順序。

**圖片的字元**在編輯器中輸入。為了少打字，檔名本身是字元時會自動猜出：

| 檔名 | 猜出的字元 |
| --- | --- |
| `0.png` … `9.png`、`K.png`、`x.png`、`中.png` | 字元本身 |
| `dot.png`、`comma.png`、`space.png` | `.` `,` 與空白 |
| `colon.png`、`slash.png`、`backslash.png`、`star.png`、`question.png`、`quote.png`、`less.png`、`greater.png`、`pipe.png` | `:` `/` `\` `*` `?` `"` `<` `>` `\|`（Windows 檔名不允許的字元） |
| `plus.png`、`minus.png`、`percent.png`、`dollar.png`、`hash.png`、`times.png`（×）、`yen.png`、`euro.png` | 其他常用符號 |
| `U+00D7.png` | 任何 Unicode 碼位 |
| 其他名稱（`digit_zero.png`、`score_07.png`） | 不猜：請在編輯器中輸入 |

編輯器的 Character 欄也接受同樣的寫法：輸入 `7`、`space`、`dot` 或 `U+0025`。

## 2. 用 font_editor 製作字型

執行 `font_editor.exe`。也可以在命令列帶入圖片或 `.fnt`（`font_editor a.png b.png`、
`font_editor tests\font\connect_score.fnt`），或在檔案總管中把它們拖到 font_editor.exe 上。

1. **Add pictures…**（按鈕、File 選單或 Ctrl+O）：選擇一或多個 PNG 檔。也可以從檔案總管把 PNG 拖進視窗。每張圖片成為一列：圖片、**Character**、碼位、尺寸與來源檔。
2. **為每張圖片設定字元：** 在 **Character** 格按兩下輸入（`7`、`K`、`%`、`space`、`dot`、`U+00D7`）。由檔名猜出的字元也可以這樣修改。**紅色**格表示沒有字元，或字元與前面某列重複。選項上方會列出問題，修正前無法儲存。
3. 整理清單：**Up** / **Down** 調整在圖頁上的順序，**Remove** 刪除選取的列，**File → New** 全部清除。
4. 在右側 **Test text** 欄輸入文字。每按一鍵、每次修改清單，預覽都會重繪；Enter 換行。字型沒有的字元會被略過，數量顯示在預覽下方。
5. 依需要調整顯示：**Scale**（0.1–8）與 **Background**（棋盤格、遊戲背景色、黑、白）。
6. 查看預覽下方的 **Combined picture**（合併後的圖片）：就是即將儲存的圖頁，並顯示尺寸（是 2 的次方時會標示 "power of 2"）。
   **選取一列**，該字元在圖上會以黃框標示，其餘變暗；**點擊圖上的字元**，清單中對應的列就會被選取。
7. 依需要調整字型（預覽與合併圖片會同步更新）：
    - **Font name (label only)**：寫入 `.fnt` 第一行（`info face="…"`）的名稱。BMFont 稱為 *face*，原本是製作字型所用的字體名稱。
      對圖片字型來說只是標籤：遊戲與工具都不會讀取。留空則使用檔名。
    - **Page layout**：圖片合併成圖頁的方式。
        - *One row (as convert2fnt-web)*：並排成一列、間隔 1 px，與舊字型相同。字元很多時會在最大尺寸處換列。
        - *Tight (atlas packer)*：使用 atlas 編輯器的打包器（MaxRects）找出能放下所有圖片的最小圖頁，間隔 1 px。
        - *Tight, power of 2 (atlas packer)*：同上，但圖頁的寬高是 2 的次方（64、128、256、512、1024…），給需要的平台或壓縮格式使用。
          `connect_score` 會變成 1024 × 128。
      版面只改變圖片在圖頁上的位置：文字繪製結果完全相同。
    - **Page max size**：一列超過這個寬度就換列；Tight 版面的寬高都不超過它（預設 4096）。放不下時會顯示錯誤。
    - **Space width**：空白讓下一個字元往右移的距離。*auto*（顯示其數值）：空白圖片的寬度，否則 `1` 圖片的寬度，否則 5。
      在這裡設定的數值優先於空白圖片；設回最小值（*auto*）即恢復預設。
    - **Line height**：換行時往下移的距離。*auto*（顯示其數值）：最高圖片的高度。加大可讓行距變寬，減小則更緊密。開啟的字型會保留其行高。
    - **Letter spacing**：每個字元前進距離加上的像素（負值會縮緊）。
8. **Save font as…**（Ctrl+S），選擇 `NAME.fnt`。工具會寫出並排的 `NAME.fnt` 與 `NAME.png`。

**修改現有字型：** **File → Open font (.fnt)** 會把字型的每個字元還原成一列（空白還原成與其前進距離同寬的透明圖片）。
新增、刪除、調整順序或重新輸入字元後儲存即可。開啟字型後不做修改直接儲存，會得到相同的 `.fnt`。

預覽使用與遊戲相同的排版程式碼及相同的最近像素取樣，所見即遊戲中的樣子。

## 3. fontpack 命令列

`fontpack` 有四個指令。`font_editor --headless <指令>` 執行的是同樣的指令。

| 指令 | 用途 |
| --- | --- |
| `fontpack build <a.png> <b.png> … --out NAME.fnt` | 依給定順序把圖片合併成 `NAME.fnt` + `NAME.png` |
| `fontpack preview <font.fnt> --text 文字 --out FILE.png` | 用字型把測試文字繪成 PNG |
| `fontpack split <font.fnt> --out <資料夾>` | build 的反向：每個字元存成一張圖片，加上 `chars.txt` |
| `fontpack info <font.fnt>` | 列出字型名稱、行高、圖頁與每個字元 |

選項：

| 選項 | 指令 | 意義 | 預設 |
| --- | --- | --- | --- |
| `--chars 文字` | build | 依序指定圖片的字元（`"0123456789.,KM"`）；沒有時使用檔名 | 檔名 |
| `--layout row\|tight\|pot` | build | 圖頁版面：一列；tight（atlas 打包器）；寬高為 2 的次方的 tight | `row` |
| `--face NAME` | build | 寫入 `.fnt` 的字型名稱（只是標籤） | 輸出檔名 |
| `--space N` | build | 空白寬度（像素） | 空白圖片的寬度，否則 `1` 圖片的寬度，否則 5 |
| `--line-height N` | build | 換行往下移的距離（像素） | 最高圖片 |
| `--advance-pad N` | build | 每個前進距離加上的像素（字距） | 0 |
| `--max-width N` | build | 圖頁最大尺寸：一列超過就換列；tight 版面寬高都不超過它 | 4096 |
| `--scale S` | preview | 繪製倍率（0.5 = 一半） | 1 |
| `--pad N` | preview | 文字周圍的空白像素 | 8 |
| `--bg RRGGBB` 或 `RRGGBBAA` | preview | 背景色；`0F0F1A` 是遊戲背景色 | 透明 |

範例：

```
fontpack build art\digit_a.png art\digit_b.png art\digit_c.png --chars "012" --out assets\media\fonts\score.fnt
fontpack build tests\font\connect_score_pictures --layout pot --out assets\media\fonts\connect_score.fnt
fontpack build art\0.png art\1.png art\dot.png --out assets\media\fonts\score.fnt
fontpack preview tests\font\connect_score.fnt --text "12,345.6K" --out preview.png --scale 0.5 --bg 0F0F1A
fontpack split tests\font\total_score.fnt --out art\total_score
fontpack info tests\font\connect_score.fnt
```

`build` 給資料夾名稱時代表其中所有 PNG（並用它的 `chars.txt`，`檔名=字元` 的行，決定字元與順序），這是把 `split` 的輸出重新建置的方式。

結束碼（供腳本使用）：**0** 成功 · **1** 警告（有檔案被略過，或文字中有字型沒有的字元） · **2** 錯誤（圖片沒有字元、字元重複、檔案無法讀取） · **3** 命令列參數錯誤。

## 4. 常見工作

**由美術素材製作新的分數字型**

1. 從美術檔把每個字元各自匯出成 PNG，高度一致，間距直接留在圖裡。
2. 在 `font_editor` 按 **Add pictures…** 全部選取。
3. 為沒有猜出字元的圖片輸入字元（`0`–`9`、`.`、`,`、`K`、`M`…）。修正所有紅色列。
4. 在測試欄輸入實際數值，例如 `1,234,567`、`12.5K`、`9.99M`。字元太開或太擠時，修改圖片或使用 Letter spacing。
5. **Save font as…**

**修改或擴充舊字型**（例如幫 `connect_score` 加上 `%`）

1. 在 `font_editor` 用 **File → Open font (.fnt)** 開啟 `tests\font\connect_score.fnt`。
2. **Add pictures…** 加入 `%` 的圖片，在它的 Character 格輸入 `%`。
3. **Save font as…** 存到遊戲載入的位置。

或用命令列：`fontpack split tests\font\connect_score.fnt --out art\connect_score`，加入 `art\connect_score\percent.png`，
再執行 `fontpack build art\connect_score --out <出貨位置>\connect_score.fnt`。

**出貨前檢查文字**

`fontpack preview my.fnt --text "<最長的數值>" --out check.png`。結束碼 1 表示文字中有字型沒有的字元。

## 5. 在遊戲中繪製字型

使用 `FontText`（在 `src/core/engine/bitmap_font.h`）：畫面上每一段文字一個物件。

```cpp
#include "bitmap_font.h"

FontText score;                                   // 場景 / HUD 類別的成員

// 一次，場景開始時
score.load(renderer, "assets/media/fonts/connect_score.fnt");
score.setPosition(1200, 40);
score.setAlign(BitmapFont::Align::Right);

// 在 update 中，或數值改變時
score.setText("12,345");

// 在 render 中，renderer.begin() 與 renderer.end() 之間
score.render();
```

| 函式 | 用途 |
| --- | --- |
| `load(renderer, "x.fnt")` | 讀取 `.fnt` 與其 PNG 並上傳圖頁。檔案不存在時傳回 false。 |
| `setText("...")` | 文字，UTF-8；`"\n"` 換行。重複設定相同文字不耗費任何成本。 |
| `setPosition(x, y)` | 文字位置（螢幕像素）：`y` 是第一行頂端，`x` 依對齊方式是左緣、中心或右緣。 |
| `setAlign(BitmapFont::Align::Left / Center / Right)` | 分數通常用 Right，數字會往左增加。 |
| `setScale(0.5f)` | 繪製倍率（1 = 圖片原尺寸）。 |
| `setColor(r, g, b, a)` | 與圖片相乘；`a` 讓文字淡出。白色 `(1, 1, 1, 1)` 為原樣繪製。 |
| `setVisible(false)` | 隱藏文字但不清除。 |
| `render()` | 繪製。每幀呼叫成本很低：只有變更後才重新排版。 |
| `width()`、`height()` | 目前倍率下文字的尺寸，用來擺放旁邊的物件。 |
| `missingChars()` | 文字中有幾個字元字型沒有圖片。 |
| `release()` | 場景結束時釋放圖頁貼圖。 |

**多個標籤使用同一字型：** 載入一個 `FontText` 後複製它。複本共用字型與貼圖（不會再次上傳），但有自己的文字、位置與顏色。
在所有複本都不再繪製時，呼叫一次 `release()`。

```cpp
FontText total = score;           // 同字型，自己的文字
total.setPosition(640, 300);
total.setAlign(BitmapFont::Align::Center);
total.setText("1,000,000");
```

某個字母只有其中一種大小寫時會使用另一種：舊字型只有 `K M` 或 `k m`，不會兩者都有。

`FontText` 是遊戲核心函式庫（`toms_core`）的一部分：任何遊戲程式都可以引入 `bitmap_font.h` 並使用它。
需要更底層的控制時可直接使用 `BitmapFont`：`layout()`、`measure()`、`appendQuads()`。

## 6. 疑難排解

| 問題 | 原因與解法 |
| --- | --- |
| Character 格是紅色 / "no character given" | 在該圖片的 Character 格輸入字元（用 `fontpack` 時，以字元命名檔案或使用 `--chars`）。 |
| "… is already given to …" | 兩張圖片設定了相同字元。修改或刪除其中一張。 |
| "'…' is not a character" | Character 格接受單一字元、名稱（`space`、`dot`、`comma`…）或 `U+XXXX`。 |
| 預覽少了某個字元 | 字型沒有這個字元的圖片。數量顯示在預覽下方，`fontpack preview` 的結束碼為 1。 |
| 字元重疊或間距太大 | 前進距離就是圖片寬度。裁切或加寬圖片，或設定 Letter spacing / `--advance-pad`。 |
| 空白太寬或太窄 | 設定 Space width / `--space`。 |
| 行距太大或太小 | 設定 Line height / `--line-height`。 |
| 小字元太低或太高 | 圖片以底部對齊。讓每張圖都是完整行高，字元畫在應該的位置。 |
| "only one page fonts are supported" | 這個 `.fnt` 用了多張 PNG 圖頁，本工具不支援。 |
| "the pictures do not fit on one … page" | Tight 版面需要更大的圖頁：加大 Page max size / `--max-width`。 |
| 圖頁必須是 2 的次方 | 選擇版面 *Tight, power of 2* / `--layout pot`。 |
