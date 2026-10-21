> 英文原文：[33_LEAK_CHECK.md](../33_LEAK_CHECK.md)

# 33 — 記憶體洩漏檢查（Windows）

`toms_game` 結束時會回報還有哪些記憶體沒有釋放。共有兩份報告：

| 報告 | 建置 | 列出什麼 |
|---|---|---|
| 遊戲物件 | 所有建置、所有平台 | `Object::DumpLeaks()`：還存活的 `Trackable` 衍生物件（遊戲物件、資源），依類型與名稱，寫在記錄中 |
| 堆積區塊 | **Windows Debug 建置**（`windows-debug`） | `main()` 開始之後配置、結束時仍未釋放的每個堆積區塊，來自 MSVC 偵錯堆積：配置編號、大小與開頭的位元組，寫在 **`toms_leaks.log`**（工作資料夾中）與偵錯工具的「輸出」視窗 |

Release 建置與其他平台不產生堆積報告。程式在 `src/game/src/leak_check.h/.cpp`，由 `main_sdl.cpp` 的 `main()` 開頭
與結尾呼叫。

## 使用方式

```
tools\build.cmd windows-debug
cd <任何資料夾>
Build\windows-debug\bin\toms_game.exe --scenario=tests/scenarios/arena_restart --scenario-test --fixed-dt=16
```

結束時主控台會顯示，例如：

```
[leaks] 99 block(s), 292348 bytes still allocated since start -- see toms_leaks.log
```

`toms_leaks.log`：

```
{28529} normal block at 0x000001CA9E5DD080, 1320 bytes long.
 Data: <        def     > ...
```

`{28529}` 是**配置編號**。找出區塊在哪裡配置的兩種方法：

- **`--leak-trace=28529,4558,...`**，不需要偵錯工具。用這個選項再執行一次同樣的內容，報告最後會附上每個列出的配置的
  **呼叫堆疊**：函式名稱與 `檔案:行號`，從 exe 旁邊的 `.pdb` 讀取。最多 32 個編號。
- **`--leak-break=28529`**，在 Visual Studio 偵錯工具中。程式會停在那次配置裡面；「呼叫堆疊」視窗顯示是誰配置的。

只有執行過程相同時編號才會相同：使用有 `--scenario-test` 與 `--fixed-dt=16` 的情境，每次都執行相同的畫格與按鍵
（[31](31_TEST_SCENARIOS.md)）。

## 報告的內容（2026-10-21 起）

1. **new / delete 對照表**（見下方）：程式在結束時仍存活的每個配置，依配置位置分組，附呼叫堆疊。這才是報告本身。
2. **簡短的偵錯堆積摘要**：整個行程中仍配置的區塊數與位元組數（包括 Qt、靜態物件與本檢查工具自己的對照表）、峰值，以及
   整次執行配置的總量。這不是洩漏清單。

偵錯堆積逐一列出區塊的清單（「Dumping objects ->」，數千筆沒有名稱的項目）**已關閉**：偵錯堆積分不出洩漏與靜態物件，而且
大部分是 Qt 與檢查工具本身。環境變數 `TOMS_LEAK_HEAP_DUMP=1` 可以讓它回來；`--leak-trace` / `--leak-break` 需要它印出的
`{N}` 編號。

## new / delete 對照表（只限 Windows Debug）

在 Windows Debug 建置中，`leak_check.cpp` 也會為它所在的程式（`toms_game`、`toms_editor`）**取代全域的 `operator new` 與
`operator delete`**（所有形式：陣列、`nothrow`、對齊）：

- 每次 `new` 都把指標連同大小、編號與**配置它的呼叫堆疊**放進對照表。呼叫堆疊提供函式與 `檔案:行號`；不使用 `#define new`，
  因為那會弄壞 placement new 與函式庫的標頭檔；
- 記憶體被釋放時，對照表中的項目就會移除。偵錯堆積的釋放掛勾能看到每個模組的每次釋放，所以我們的程式用 `new` 建立、再由
  Qt 刪除的元件也會被移除。**正確刪除的東西永遠不會出現在報告中**；
- 結束時，對照表中剩下、而且是在 `main()` 開始之後配置的，就是報告：**只有沒有釋放的配置，依配置位置分組，列出每個位置，
  最大的在前**，附上呼叫堆疊（先列我們程式碼的堆疊框；C 執行階段與 STL 的配置細節略去）。

這也涵蓋智慧指標：`shared_ptr`、`make_shared` 的控制區塊、`unique_ptr` 與所有 STL 容器都經過 `operator new`。Qt 的 DLL
有自己的 `new`，所以 Qt 的內部不在對照表中，只有程式本身的程式碼（遊戲、引擎、RmlUi、bgfx、編輯器自己的程式碼）。

**只限 Windows Debug 建置。**所有東西（運算子、對照表、掛勾、報告）都在 `#if defined(_WIN32) && defined(_DEBUG)` 之內。
Release 建置與其他平台只有兩個空函式；已在建置好的程式上確認：報告的文字只存在於 Debug 版的 `toms_game` 與 `toms_editor`，
兩個 Release 版都沒有。

環境變數 `TOMS_LEAK_TRACK=0` 可關閉對照表。對照表會為每次 `new` 擷取呼叫堆疊；開啟時一次情境執行花了 6 秒。對照表本身用的
記憶體會出現在報告的偵錯堆積部分（第二部分）。

## 確保沒有洩漏（2026-10-21）

**報告是空的就表示沒有洩漏；列出的任何東西都是洩漏。**靠兩件事做到：

1. **報告之前先釋放單例。**擁有記憶體或執行緒的單例會登記釋放方式（`toms::onReleaseSingletons(名稱, 函式)`，
   `src/core/engine/singletons.h`）；`main()` 結尾在遊戲（或編輯器）結束後呼叫 `toms::releaseSingletons()`，由新到舊釋放。
   目前：工作系統（它的工作執行緒），由 `JobSystem::start` 登記。RmlUi 的記憶體內檔案由 `RmlUi::shutdown` 釋放；編輯器的
   翻譯器與對照表由 `Language::shutdown()` 釋放（它們是用 `new` 建立、以前從未釋放）。
2. **報告在靜態物件解構之後才寫出。**`leakCheckBegin` 在 `main()` 一開始、任何函式內靜態物件建立之前，就用 `atexit` 登記
   報告；C 執行階段由新到舊執行 `atexit` 處理函式，所以之後建立的每個靜態物件（表格、登錄表、空字串預設值、事件匯流排）
   都會在報告之前解構。`leakCheckEnd` 只負責說明報告寫到哪裡。

新的單例若把記憶體放在指標中，就必須登記釋放，否則會被列為洩漏——這正是目的。

主控台那一行說得很清楚：`[leaks] no leaks: 0 allocation(s) never deleted` 或 `[leaks] LEAKS: ...`。這次修改後，
`toms_game` 與 `toms_editor` 都回報沒有洩漏。堆積摘要的變化可能是負數（`main()` 之前建立的靜態物件也被釋放了），在編輯器中
則可能有幾百個 Qt 自己的區塊（它的 DLL 會保留快取直到卸載；對照表不涵蓋 Qt 的內部）。

**順便修正了一個死結。**以前對照表本身的記憶體來自 C 執行階段的堆積。釋放掛勾在堆積的鎖之內執行，然後去取對照表的鎖；
而正在加入對照表的執行緒持有對照表的鎖，又需要堆積的鎖：兩個執行緒可能永遠互相等待（遊戲在結束時卡住）。現在對照表使用
私有的 Windows 堆積（`HeapCreate`），持有它的鎖時永遠不需要 C 執行階段的鎖。

## toms_editor

`toms_editor` 也有同樣的檢查（同一個 `leak_check.cpp`，由 `src/editor/src/main.cpp` 呼叫）。它的報告是
**`toms_editor_leaks.log`**，選項 `--leak-trace` / `--leak-break` 也相同：

```
Build\windows-debug\bin\toms_editor.exe --scenario=tests/scenarios/cards_first_win --frames=90 --screenshot=shot.png
```

（`--frames` 加上 `--screenshot` 會自動關閉編輯器；沒有它們時，手動關閉後報告就會寫出。）

清單會比遊戲的長很多（2026-10-21：6,674 個區塊，1 MB）。大部分是 Qt：它的字型、樣式、外掛與檔案快取要到 DLL 卸載時才
釋放，也就是 `main()` 之後。沒有安裝 Qt 的 `.pdb` 檔時，追蹤中 Qt 自己的堆疊會顯示為 `?`。那次執行的追蹤結果：
- 遊戲的區塊，和 `toms_game` 相同（見下方）；
- 編輯器的翻譯表（`Language.cpp`，靜態物件）；
- 建立「關卡」與「情境」停駐視窗時 Qt 產生的快取。

在呼叫堆疊中找**我們的程式碼**（`src/editor`、`src/core`、`src/game`、`tools/studio_common`），並像下面那樣比較短的與長的
工作階段。

## 解讀報告

不是每個區塊都是洩漏：

- **靜態物件是正常的。**函式內的靜態物件（全域事件匯流排、物件登錄表、共用材質登錄表、bgfx 會讀取的
  `static std::vector`）要到 `main()` 返回後才釋放，所以會被列出。遊戲跑得越久，它們也不會變多。
- **真正的洩漏會成長。**把同一個情境跑短的與長的版本（測試場多打幾場、多換幾次樓層）；數量或大小隨著執行而增加的區塊
  就是洩漏。追蹤那些。

## 對照表找到了什麼（2026-10-21）

- **`toms_game`**（`arena_restart`）：99 個從未刪除的配置，和下方偵錯堆積的區塊相同，現在每個都有位置。都是靜態物件：
  精靈索引緩衝區（`bgfx_renderer.cpp:142`）、緩動名稱表（`anim_clip.cpp:67`）、物件登錄表（`object.h:35`）、存檔資料夾名稱
  （`save_slots.cpp:19`）、工作系統（`job_system.cpp:114`）、RmlUi 的虛擬檔案（`rml_ui.cpp:443`）與資料模型名稱。
- **`toms_editor`**（`cards_first_win`）：**一個真正的洩漏，已修正**。`StageScene`（關卡編輯器分頁）為每個牆與地板格子用
  `new` 建立一個 `GameObject`（`stagescene.cpp:65`，共 143 個）。重建時會釋放前一組，但最後一組沒有被釋放：它沒有解構子。
  現在 `~StageScene` 會釋放它們；編輯器的數量從 436 降到 293 個配置，其餘是靜態物件：語言表（`Language.cpp:70`）、關卡
  編輯器的物件目錄（`catalog.h:31`）與遊戲的。

## 第一份報告（2026-10-21，`arena_restart`）

99 個區塊，292 KB。追蹤結果：

| 區塊 | 大小 | 配置位置 | 判斷 |
|---|---|---|---|
| {4558} | 276 KB | `BgfxRenderer::init` 的 `static std::vector<uint16_t> indices`（`bgfx_renderer.cpp:139`） | 刻意的靜態物件（bgfx 保留對它的參照）：不是洩漏 |
| {19435} 與其他 16 位元組的區塊 | 16 B | `sharedTextureKey` / `lendTexture`（`shared_textures.h`），共用材質登錄表 | 函式內的靜態物件：不是洩漏 |
| {342466} | 280 B | RmlUi 資料模型的類型註冊（`registerTypes`，`game_ui.cpp:320`） | RmlUi 保留到它的靜態類型被釋放；檢查 `GameUi` 關閉時是否應該移除資料模型 |
| {1193507} … | 每個 16 B | RmlUi：`GameSession::stop` 時 `Debugger::Shutdown` → `UnloadDocument` → `GenerateMouseEventParameters` | 在關閉時配置且沒有釋放；待檢查 |
| {348307} | 112 B | `Game::missingUiSprites`（`game_ui.cpp:62`）經由 `GameUi::publishSprites` 產生的字串 | 遊戲結束後仍被某處保留；待檢查 |
| {28529} | 1320 B | 原本判斷為動畫的節點清單；new / delete 對照表顯示它是緩動名稱表（`easeNames`，`anim_clip.cpp:67`） | 靜態物件：不是洩漏 |

RmlUi 與 `missingUiSprites` 兩列仍待檢查；在這次執行中，它們都不會隨著遊玩而成長。
