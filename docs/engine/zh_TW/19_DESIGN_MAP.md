> 英文原文：[19_DESIGN_MAP.md](../19_DESIGN_MAP.md)

# 19 — 設計地圖：遊戲怎麼組成，以及有多容易擴充

**用瀏覽器開啟 [../design_map/design_map.html](../design_map/design_map.html)**（本機頁面，不需要伺服器，不上網）：
分頁介紹各層、一個畫格、畫面與切換、繪製順序、事件、對話、戰鬥，以及可擴充性的評估。這一頁保存它背後的程式碼位置。
內容依 2026-10-31 的程式碼整理。

## 1. 各層

| 層 | 位置 | 內容 |
|---|---|---|
| 主程式 | src/game/src | main_sdl.cpp（畫格迴圈，桌面 + 網頁）、GameSession（game_session.cpp:225 `frame`）、GameUi（RmlUi 文件） |
| Game | src/core/game/core | 一個類別，game.h 1,544 行（約 300 個函式、約 350 個欄位，以正規式估算），分散在 game.cpp 與 12 個 `game_*.cpp`，約 9,100 行；map_objects.cpp：每種地圖物件一個 MapObject 子類別 |
| 規則 | src/core/game/systems | 28 個純模組，21 個有自己的 `*_test.cpp`（cue_track、gesture、touch_play 也在 play_blocks_test） |
| 引擎 | src/core/engine、src/engine | IRenderer → BgfxRenderer、資源管理（resources.json 的 key）、動畫（CharacterRig、節點效果）、particle_fx、condition、event_bus、glTF、音效、RmlUi |
| 內容 | assets/data | 以 key 讀取（res_keys.h）；新增一筆不需要程式 |
| 畫面 | assets/media/ui | 12 個 RmlUi 文件，由 `UiState` 填入（27 個 `Ui*` 結構，ui_state.h）；按鈕呼叫 `Game::uiEvent(name)` |

## 2. 一個畫格

- `appFrame`（main_sdl.cpp:446）→ `GameSession::frame`（game_session.cpp:225）→ `bgfx::frame()`（bgfx_host.cpp:188）。
- `frame` 內依序：`Game::update`（:230）、按鍵（:234-332）、滑鼠（:336-359）、ImGui（:362）、標題 3D（:374）、`Game::draw`（:378）、
  UI（`sync`、RmlUi 更新與繪製，:379-387）、ImGui 繪製（:388）、HUD 文字（:389）。
- **update 在讀取這個畫格的按鍵之前執行。** 按鍵在下一次 update 才生效。
- `Game::update`（game.cpp:177）沒有場景切換：每個部分檢查自己的旗標（戰鬥時間、rig、計時器、小遊戲、戰鬥、競技場、行走、攝影機）。

## 3. 畫面與切換

- **沒有場景列舉。** 顯示哪個畫面由旗標決定：`title_.isOpen()`、`activeEndingId_`、`storeOpen`、`cs.active` / `cs.won`、
  `inDialogue`、`inGameMenuOpen_`、`saveLoadOpen_`、`stageSelectOpen_`、`stairsConfirmOpen_`、`miniGame_.open`、`notices_`
  （判斷式在 game.h:423-541）。`modalActive()`（game.h:449）會停止行走與對話。
- 優先順序寫了三次：按鍵鏈（game_session.cpp:249-332）、`buildUiState`（game_ui.cpp:224-228：商店 > 戰鬥 > 對話 > 行走）、
  `draw`（game_scene_draw.cpp:35-46）。
- RmlUi 的堆疊是每個文件 body 的 `z-index`：hud 10、battle 30、dialogue 30、store 40、minigame 40、player 45、stage_select 45、
  saveload 50、dialogs 50、title 60、ending 70、overlay 90。

## 4. 繪製順序（bgfx view）

| View | id | 內容 |
|---|---|---|
| kViewClear | 0 | 清除 |
| 標題場景 | 16+ | glTF（只有標題） |
| 圖層 | 200+ | 節點效果的圖，內層先 |
| kViewGame | 1 | 世界：地圖格 → 地圖物件；或戰鬥背景 → 戰鬥者 → 粒子（`drawBattleRig`，game_combat.cpp:526） |
| kViewUi | 2 | RmlUi |
| kViewTop | 3 | `setLayer(1)` 在 UI 上方的 sprite（戰鬥量條的提示） |
| kViewOverlay | 4 | ImGui |

順序由 `applyViewOrder`（bgfx_renderer.cpp:1023）設定。每個畫格一批 sprite；網格、GPU 粒子與圖層的圖是其中的標記。

## 5. 事件

- 記錄在 `events/pool_*.json`（載入 game_player_menu.cpp:319）；地圖物件在 `.stage.json` 的 `objects."x,y".events` 連結事件。
- 觸發：事件格（map_objects.cpp:294）、連結的物件包括被打倒的怪物（`fireLinkedEvents`，game_story.cpp:278）、`talk` 話題、
  `chain`（`wakeEvent`，:268）、`varChanged`（:290）。
- 完成：`completeEvent`（game_story.cpp:250）→ `finishEvent`（game_input.cpp:220）：旗標、`next`、樓梯鎖。
- 條件：19 種葉節點 + all / any / not（condition.cpp）。動作：`story_actions`（11 個動詞）+ `runDialogueAction`（約 12 個 Game
  動詞）。怪物事件（enemies.json）有自己的動作鍵（game_combat.cpp:275）。
- 缺口：事件格的效果以 id 比對寫在程式裡（map_objects.cpp:313）；沒有 `eventDone` 條件；章節的 beat 沒有被執行；event bus
  只給任務用。

## 6. 對話

- `npcs/*.json`、`dialogue/*.json`、話題 = `trigger.on: "talk"` 的事件。
- `talkTo`（game_story.cpp:166）→ `planTalk`（talk_system.cpp:133：閒聊 / 一個話題 / 選單）→ `openDialogue`（:46）→
  `chooseDialogue`（:89）→ `endDialogue`（:123）。
- 不支援：戰鬥中對話（game_combat.cpp:349）。

## 7. 戰鬥

- 狀態：`CombatState cs`（game.h:118），由 `startCombat` 重設。
- 攻擊：武器自己的玩法（`armTouchPlay`，game_skills.cpp）或量條；答對立刻命中（`playHit`）；結束時由 `resolveCombo` 結算。
- 怪物：`stepFoes` → `resolveEnemyClockFire`（節奏、防禦模式、特殊防禦玩法）→ 怪物事件。
- 招式：`startSkill` / `updateSkill`（game_skills.cpp）執行 special_moves.json 的一筆：demo → 玩法 → finish。
- 結束：`afterPlayerHit` → `startWinMoment` → `finishCombatWin` → 結算 → `dismissVictory`。

## 8. 可擴充性

| 改動的種類 | 成本 | 原因 |
|---|---|---|
| 新內容（怪物、道具、招式、事件、NPC、對話、樓層、文字） | 容易 | 只要資料 |
| 規則調整 | 容易 | 數值在資料，規則在有測試的純模組 |
| 新的種類（事件動作、玩法、地圖物件、條件） | 中等 | 2-3 個 if 鏈要改 C++；沒有表格 |
| 新畫面 | 昂貴 | 一個旗標、三處優先順序、一個 Ui 結構、一個 .rml、uiEvent 名稱 |

建議的順序（都不妨礙目前的工作）：
1. **畫面狀態機**：一個 `Screen` 列舉與一個小堆疊，按鍵、UI 與繪製共用一個「最上層畫面」函式。
2. **用表格取代 if 鏈**：`uiEvent`（32 + 20 個名稱）、故事動作（3 個鏈；驗證器不認得 `tryWeapon`、`chooseWeapon`、`miniGame`）、
   怪物事件動作、玩法種類都改成「名稱 → 處理函式」。
3. **依區域拆分 Game**：由 Game 持有的戰鬥、對話、選單控制器。
4. **地圖事件與怪物事件共用一套動作**；讓事件可以聽 event bus。
5. **把內容移出程式**：事件格效果改成 `actions`；加入 `eventDone` 條件；決定章節 beat 的去留；刪除沒用到的 combat.json 與 story.json。
