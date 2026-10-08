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
（`tools/web_editors/data_snapshot/`，用 `tools/web_editors/refresh_data.cmd` 更新）。

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

## 下一步

1. **第 1 階段：** 把四個檔案加進 `src/editor/CMakeLists.txt`，並把 `EventFlowView` 開成一個分頁。
   不需要新程式碼，就能得到唯讀的圖和問題數量。
2. **第 2 階段：** 屬性面板、儲存和復原。
3. **第 3 階段：** 問題面板，以及建立或刪除事件和事件池。

細節在 [02_EDITOR_DESIGN.md](02_EDITOR_DESIGN.md) 第 5 節。
