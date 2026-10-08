# 事件編輯器

> 英文原文：[README.md](../../event_editor/README.md)

**現況：** 設計階段。資料模型和唯讀的流程圖已經寫在程式碼中
（`src/editor/src/event_graph.*`、`event_flow_view.*`），但還沒編譯進 `toms_editor`，
也還不能編輯任何東西。在編輯器推出之前，事件要手動編輯 JSON
（[03_USER_GUIDE.md](03_USER_GUIDE.md) 第 1 節）。

事件編輯器是 `toms_editor` 中的一個分頁，處理隨機樓層事件：每層樓可能抽到的遺物、低語、
補給、陷阱和救援。它會顯示哪些樓層能抽到哪些事件，讓你在同一個地方編輯事件和它的文字，
並在遊戲碰到之前就列出資料問題。

## 文件

| 檔案 | 內容 |
|---|---|
| [01_DATA_MODEL.md](01_DATA_MODEL.md) | 三種 JSON 檔、它們如何互相連結、問題規則，以及目前資料的樣子 |
| [02_EDITOR_DESIGN.md](02_EDITOR_DESIGN.md) | 視窗配置、每個面板、儲存、復原，以及開發階段 |
| [03_USER_GUIDE.md](03_USER_GUIDE.md) | 常見工作怎麼做：今天手動做，以及編輯器推出後怎麼做 |
| [05_FLOOR_LINKS.md](05_FLOOR_LINKS.md) | 一層樓有多個樓梯、樓梯要等某個事件完成才開放：格式、遊戲規則、檢查 |
| [06_EVENT_LOGIC.md](06_EVENT_LOGIC.md) | 事件觸發方式（踩到、交談、變數改變、連結）、條件、動作、直接連結、事件變數，以及節點圖編輯器 |
| [04_TUTORIAL.md](04_TUTORIAL.md) | 一步一步：建立事件、看懂關係圖、找出問題、修好、儲存 |
| [tools/web_editors/index.html](../../../tools/web_editors/index.html) | 可實際操作的事件編輯器（含教學）和關卡編輯器 |

## HTML 編輯器

它們是靜態 HTML 加一點 JavaScript，使用真實事件資料的副本
（`tools/web_editors/data.js`）。`serve.cmd` 每次啟動都會從 `assets/data` 更新它；
不用伺服器時，請執行 `refresh_data.cmd`。

**編輯器放在 [tools/web_editors/](../../../tools/web_editors/index.html)**，和關卡編輯器在一起：開啟 `index.html`（或執行
`serve.cmd`），用 **⇄** 按鈕在兩者之間切換。事件編輯器內含**教學**，在一個可操作的迷你編輯器中共 29 步：

1. **建立事件：** 建立、命名、撰寫文字、連接到樓層、觀看關係圖，然後儲存。
2. **找出問題：** 「問題」分頁；一個孤立事件；一個缺少的事件 ID。
3. **解決問題：** 連接孤立事件、在新的事件池中建立缺少的事件、一次補齊整個 ch_04，然後儲存。
4. **連接樓層：** 在樓層圖上給 F50 第二座通往新樓層 F96 的上樓梯，鎖到
   `ev_common_relic_road` 完成為止，然後儲存（[05_FLOOR_LINKS.md](05_FLOOR_LINKS.md)）。
5. **事件邏輯：** 兩個事件都會加的變數、變數達到 2 之後跟村民交談就會觸發的事件、
   連到另一個事件的直接連結，以及**邏輯圖**節點編輯器（仿 imgui-node-editor），
   拖曳接腳就會編輯同一份資料（[06_EVENT_LOGIC.md](06_EVENT_LOGIC.md)）。

每一步都有 *示範給我看*，**▶ 播放示範** 會自己跑完全部，儲存時會顯示每個 JSON 檔確切的變更。
教學頁面有英文和繁體中文（上方列的 🌐 選單）。
[04_TUTORIAL.md](04_TUTORIAL.md) 是同一份教學的文件版。

HTML 編輯器呈現的是預期的外觀和行為，不保證每個像素都一樣：真正的編輯器是 Qt
Widgets，和動畫、粒子編輯器用同一套深色主題。

## 存進 assets/data

一開始編輯器使用 `data.js` 中的副本，儲存時不會寫入任何東西（教學就是用這個）。要編輯真正的資料：

1. 啟動 `tools\web_editors\serve.cmd`（Chrome 或 Edge）。
2. 點 **📂 開啟專案資料夾…**，選 **TOMS** 資料夾本身（裡面有 `assets` 的那個），並允許瀏覽器編輯
   檔案。橫幅會變成綠色：*專案資料夾 · 正在編輯 assets/data*。關卡編輯器會自動開啟同一個資料夾
   （反過來也一樣）。
3. 編輯，然後 **儲存**（Ctrl+S）→ **寫入專案**。

儲存只會寫入真正有變更的檔案：

| 檔案 | 寫法 |
|---|---|
| `assets/data/events/pool_*.json`（以及新的事件池） | 事件池的 `events` 清單；其他欄位（`_comment`、`act`、`theme`）和紀錄中未知的欄位都會保留 |
| `assets/data/events/kinds.json`、`story/vars.json` | 依類型分頁 / 事件變數重寫 |
| `assets/data/story/floors/F??.json` | 只改 `events`、`act` 和 `stairs`；樓層設定的其他內容不變 |
| `assets/data/text.json` | 只改有變更的字串 |
| `assets/data/story/flags.json`、`counters.json` | 只改有變更的那幾行，所以手動對齊的排版會保留 |

- 每次寫入都從當下磁碟上的檔案開始，只套用這個編輯器的變更。如果關卡編輯器在這之間
  存了同一層樓，兩邊的變更都會留在檔案中。
- 覆寫之前，每個檔案都會複製到 `Build/editor_backups/<日期-時間>/`（關卡編輯器也一樣）。
  `Build/` 不在 git 中；`git diff` 也能精確看出改了什麼。
- 資料夾權限在分頁開著時有效；重新載入後要再開一次。
- 在專案資料夾模式下，教學和播放示範會關閉，所以它們不會寫進真正的資料。

## 下一步

1. **第 1 階段：** 把四個檔案加進 `src/editor/CMakeLists.txt`，並把 `EventFlowView` 開成一個分頁。
   不需要新程式碼，就能得到唯讀的圖和問題數量。
2. **第 2 階段：** 屬性面板、儲存和復原。
3. **第 3 階段：** 問題面板，以及建立或刪除事件和事件池。

細節在 [02_EDITOR_DESIGN.md](02_EDITOR_DESIGN.md) 第 5 節。
