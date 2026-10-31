> 英文原文：[11_TEAM_PLAN.md](../11_TEAM_PLAN.md)

# 團隊計畫：程式、遊戲企劃、美術

三個人如何一起繼續開發 TOMS：誰負責什麼、每種內容（UI、事件、關卡、美術、動畫、特效、文字）如何從想法進到遊戲裡、
如何在 Git 上分工而不互相覆蓋，以及頭幾週要做什麼。

---

## 1. 三個角色

| 角色 | 負責 | 主要使用 | 要建置程式碼嗎？ |
|---|---|---|---|
| **程式** | `src/`、`tools/`（程式碼與腳本）、`tests/`（C++ 與 CMake）、建置、`Editor/bin`、網頁版發佈 | Visual Studio、`tools\build.cmd`、`tools\test.cmd` | 要 |
| **遊戲企劃** | `assets/data/` 中的規則與數值（道具、敵人、武器、商店、技能、任務）、樓層（`stages/`）、事件、對話與劇情、文字（`text.json`）、測試情境（`tests/scenarios/`） | 關卡與事件編輯器、網頁編輯器、編輯 JSON 的文字編輯器、UI 編輯器旁的遊戲（`Editor\UiEditor.cmd`） | 不用 |
| **美術** | 原始美術（`art_raw/`）、圖集（`assets/media/atlas`、`styles/`）、動畫（`assets/media/anim`）、特效、字型、UI 的外觀（`assets/media/ui/*.rcss`）、音效（`assets/media/sfx`） | 圖集、動畫、粒子與字型編輯器、AI 流程（AIGamestyle）、繪圖軟體 | 不用 |

「負責」的意思是：由那個人決定，這些檔案的修改要經過他。任何人都可以*建議*修改別人負責的部分，但由負責人來改或核准。

有些工作一定需要兩個人。規則：**需要修改的人提出，負責人動手。**

| 共同的工作 | 誰 |
|---|---|
| 新種類的畫面，或畫面要顯示新的資訊 | 企劃：要顯示什麼；美術：外觀（`.rcss`）；程式：背後的資料（`ui_state.h`、`game_ui.cpp`） |
| 新種類的事件、道具效果或戰鬥規則 | 企劃：規則與資料；程式：執行它的程式碼 |
| 帶戰鬥片段的新角色或新武器 | 美術：sprite 與片段；企劃：數值；程式：只有需要新狀態或新片段名稱時 |
| 新的美術風格 | 美術（[03](../../engine/zh_TW/03_ART_STYLES.md)）；程式：只有風格需要程式碼時 |

---

## 2. 每個人的環境設定

**所有人：**

1. 安裝 Git 與 **Git LFS**，先執行一次 `git lfs install`，再 clone。沒有 LFS 時，`Editor\bin` 中的編輯器只是很小的文字檔，
   Windows 會顯示「這個應用程式無法在您的電腦上執行」。
2. 安裝 **Python 3**（網頁編輯器、`make_ui_font.py`、`refresh_meshes.py` 與 AI 流程都需要）。
3. 閱讀 [Editor/README.md](../../../Editor/README.md)：每個編輯器、怎麼啟動、做出什麼。

**程式：** 完整建置：[01_INSTALL_WINDOWS.md](01_INSTALL_WINDOWS.md)、[09_NEW_MACHINE_SETUP.md](09_NEW_MACHINE_SETUP.md)、
[02_VISUAL_STUDIO.md](02_VISUAL_STUDIO.md)。網頁版建置：[05_BUILD_WEB.md](05_BUILD_WEB.md)。

**企劃與美術：** 不需要建置任何東西。使用 `Editor\` 中現成的編輯器，並在 `Editor\UiEditor.cmd` 中看遊戲：它用這個儲存庫的
`assets`，所以你存了什麼就看到什麼。程式碼改變時由程式更新 `Editor\bin`（第 6 節）。

**美術，做 AI 美術時：** ComfyUI 以及 AIGamestyle 指南（`AIGamestyle/Doc/Reskin_Game_Art.md`）中的模型、
[../../engine/zh_TW/13_AI_RESTYLE.md](../../engine/zh_TW/13_AI_RESTYLE.md)、[../../engine/zh_TW/14_AI_SPRITES.md](../../engine/zh_TW/14_AI_SPRITES.md)。

---

## 3. 每種內容如何進到遊戲

### 3.1 看到你的修改

| 你改了 | 在執行中的遊戲裡顯示 |
|---|---|
| 美術（圖集）、動畫、特效 | 自動，一秒內（熱重載：編輯器與除錯版） |
| UI 版面或樣式（`.rml`、`.rcss`） | 在 UI 編輯器中輸入時；在遊戲中，存檔後立刻；**F8** 開啟 UI 除錯器 |
| 遊戲資料（`assets/data/*.json`、關卡、事件、對話、文字） | 重新啟動遊戲 |

### 3.2 樓層（關卡）——企劃

1. `Editor\StageEventEditor.cmd`，選擇樓層（[../../gameplay/zh_TW/02_STAGE_EDITOR.md](../../gameplay/zh_TW/02_STAGE_EDITOR.md)）。
2. 畫牆與地板，放置怪物、道具、門、鑰匙、NPC 與事件。編輯器會拒絕放不下的東西，並在下方列出問題（到不了的物件、沒有樓梯、
   未知的事件）。存檔前先修正所有 ⛔。
3. 存檔，重新啟動遊戲，走一遍這層樓。
4. 大量修改多個樓層：先和其他人商量；計畫在 [../../gameplay/zh_TW/06_FLOOR_REDESIGN.md](../../gameplay/zh_TW/06_FLOOR_REDESIGN.md)。

### 3.3 事件、NPC、對話、劇情——企劃

- 事件編輯器（在關卡與事件編輯器中，或 `Editor\WebEditors.cmd`）：每幕的事件池（`assets/data/events/`）、事件種類、NPC 對話
  （`dialogue/`、`npcs/`）、劇情章節（`story/`）。指南：[event_editor/README.md](../../gameplay/event_editor/README.md)、教學
  [event_editor/04_TUTORIAL.md](../../gameplay/event_editor/04_TUTORIAL.md)、邏輯 [06](../../gameplay/event_editor/06_EVENT_LOGIC.md)、NPC 對話
  [08](../../gameplay/event_editor/08_NPC_DIALOGUE.md)。
- 獎勵小遊戲（金幣或水晶）：加進 `assets/data/minigames.json`，由對話選項開啟（`{"type": "miniGame", "id": "..."}`）；不需要程式碼
  （[07](../../gameplay/zh_TW/07_GAMEPLAY_PLAN.md) 第 16 節）。
- `events/kinds.json` 要等有人在「種類」分頁編輯事件種類時才會產生；在那之前遊戲會提示一次，並使用內建的種類。建立它之後提示就會消失。
- **需要遊戲還做不到的事的事件**（新的動作類型、新的條件）：寫下它該做什麼，交給程式。

### 3.4 道具、武器、怪物、數值——企劃（美術負責圖）

- 道具與武器：`items.json`；武器等級與攻擊玩法：`weapon_play.json`；怪物：`enemies.json`；商店、鍛造、技能、任務：各自的 JSON 檔。
  它們的意義在 [../../gameplay/zh_TW/07_GAMEPLAY_PLAN.md](../../gameplay/zh_TW/07_GAMEPLAY_PLAN.md)。
- 玩家看得到的每個名稱或文字，都是 `text.json` 中的一個鍵，含六種語言（第 5 節）。
- 新怪物或新道具需要 sprite：先向美術要 sprite 名稱，並使用那個名稱。
- 用**測試情境**鎖住重要規則：在 `tests/scenarios/<名稱>/` 放一個小 JSON，設定一場戰鬥或一段對話，並寫出一定要發生什麼
  （[../../gameplay/zh_TW/08_TEST_SCENARIOS.md](../../gameplay/zh_TW/08_TEST_SCENARIOS.md)）。由程式執行它們。

### 3.5 Sprite 與圖集——美術

1. 把 PNG 放進 `art_raw/sprites/`（32 × 32，透明背景），名稱與企劃說好。AI 做的 sprite：[../../engine/zh_TW/14_AI_SPRITES.md](../../engine/zh_TW/14_AI_SPRITES.md)。
2. 在圖集編輯器開啟 **`art_raw/atlas/game.atlasproj`**（絕對不要開打包好的 `assets/media/atlas/game.atlas`），*從參考匯入*，存檔。
   不開視窗時：`atlaspack refs art_raw\atlas\game.atlasproj --import changed` 與
   `atlaspack build art_raw\atlas\game.atlasproj --all-variants`（[../../engine/zh_TW/04_ATLAS_TOOL.md](../../engine/zh_TW/04_ATLAS_TOOL.md)）。
3. **任何**一次圖集重建後：`python tools\refresh_meshes.py`（會彎曲的 sprite 的網格）。
4. 重新命名或刪除資料或 UI 還在用的 sprite 會讓遊戲壞掉：要告訴企劃與程式。

### 3.6 動畫——美術

- 動畫編輯器（[../../engine/zh_TW/05_ANIMATION.md](../../engine/zh_TW/05_ANIMATION.md)、範本 [06](../../engine/zh_TW/06_ANIMATION_RECIPES.md)、彎曲 sprite
  [../../engine/zh_TW/17_SKINNING_TUTORIAL.md](../../engine/zh_TW/17_SKINNING_TUTORIAL.md)；編輯器中的 說明 > 蒙皮教學）。
- **片段名稱是和程式碼的約定。** 角色的片段有 `body` 與 `hand` 節點；武器的片段依戰鬥狀態命名（`idle`、`charge`、`attack`、`miss`…），
  寫在 `items.json` 中它的 `battleAnim`。新的狀態或節點名稱需要程式。
- 有些檔案是由腳本寫出的，再執行一次腳本會覆蓋手動修改：

  | 檔案 | 腳本 |
  |---|---|
  | `weapon_bow.anim` | `tools/make_bow_anim.py` |
  | `archer_bow.anim` | `tools/make_archer_anim.py` |
  | `weapon_sword.anim`、`fighter_sword.anim`、`weapon_staff.anim`、`caster_staff.anim` | `tools/make_weapon_anims.py` |

  手動修改這些檔案之前，請程式停用它的腳本，免得有人再執行它。
  （`tools/make_idle_clips.py` 不同：它只把缺少的 `idle_<id>` 片段加進 `monster_idle.anim` 與 `npc_idle.anim`，不會動已經存在的片段，
  所以手做的待機片段是安全的。）
- 提交前檢查檔案：`anim_editor --headless check <檔案.anim>`。

### 3.7 特效、字型、3D——美術

- 粒子特效：粒子編輯器（[../../engine/zh_TW/07_PARTICLES.md](../../engine/zh_TW/07_PARTICLES.md)）。
- 圖片字型：字型編輯器（[09](../../engine/zh_TW/09_FONT_TOOL.md)、[10](../../engine/zh_TW/10_FONT_TOOL_GUIDE.md)）。
- UI 的文字字型是依文字產生的：`text.json` 或 `.rml` 出現新字元後，執行 `python tools\make_ui_font.py`
  （否則那些字元在網頁版與手機上會顯示成方框）。
- 3D 模型（標題畫面）：glTF 檢視器（[../../engine/zh_TW/08_GLTF.md](../../engine/zh_TW/08_GLTF.md)）。

### 3.8 UI——三個人

- 畫面是 `assets/media/ui/<畫面>.rml`（結構）+ `.rcss`（外觀），以 1024 × 768 排版（[../../gameplay/zh_TW/01_RMLUI.md](../../gameplay/zh_TW/01_RMLUI.md)）。
- **美術：** 顏色、大小、間距、位置：`.rcss` 檔，以及 `.rml` 中固定的部分。使用 **UI 編輯器**（`Editor\UiEditor.cmd`）：遊戲在文字旁邊，
  輸入時就顯示每個修改，用「預覽」看任何畫面，用「語言」看長文字，用「檢查」找出是哪條規則套用到元素。也可以用任何文字編輯器：
  執行中的遊戲會自己重新載入存檔後的檔案。
- **企劃：** 畫面上要有哪些資訊與按鈕：描述出來，或在 `.rml` 中畫個草稿。
- **程式：** 所有綁定遊戲資料的部分（`{{...}}`、`data-if`、`data-for`、`data-event-click`）：`ui_state.h` 中的值，在
  `Game::buildUiState()` 中填入，按鈕動作在 `Game::uiEvent()`（[01](../../gameplay/zh_TW/01_RMLUI.md)「Adding a screen」）。
- UI 中的 sprite 依名稱取自遊戲圖集（`<img sprite="coin"/>`）：重新打包圖集時 UI 不用改。

### 3.9 音效——美術（何時播放由企劃決定）

`assets/media/sfx/*.wav`。替換檔案時保留名稱即可，不需要其他動作。新時機的新音效需要程式來播放。

---

## 4. Git：分享而不互相覆蓋

### 4.1 每天的循環

1. **開始前：** `git pull`。
2. 小步前進。**常常提交**，一個提交一件事，訊息寫出改了什麼（「第 3 關：史萊姆後面的第二把鑰匙」，而不是「更新」）。
3. **推送前：** 再 `git pull` 一次，開一次遊戲，然後 `git push`。
4. 較大的工作（新系統、重新設計好幾層樓、新的美術風格）：開**分支**，在 GitHub 上發 pull request，讓其他人先看過再合併。
   小的資料與美術修改可以直接進 `main`。
5. 絕對不要提交建置產物（`Build/`）、個人設定，或自己玩遊戲產生的存檔。

### 4.2 會衝突的檔案，以及各自的規則

| 檔案 | 為什麼會衝突 | 規則 |
|---|---|---|
| `assets/data/text.json`（1400 多個鍵、六種語言） | 每個人都會加文字 | 編輯前立刻 pull，改完立刻 push。新的鍵加在同一組旁邊（`npc.*`、`item.*`…），絕不重新排序或重新排版整個檔案。衝突時**兩邊**的鍵都保留 |
| `assets/media/atlas/*` 與 `styles/*/atlas/*`（`.atlas`、`.png`、`.mesh.json`、`.mesh.bin`） | 產生出來的二進位檔：無法合併 | **只有美術**重建與提交。合併衝突後：任選一邊，再從 `art_raw` 重建（3.5 節）並提交結果 |
| `art_raw/atlas/*.atlasproj` | 每次存檔都會整個重寫 | 只有美術 |
| `assets/data/stages/*.json` | 一層樓一個檔 | 同一時間一層樓只給一個人：說出你正在改哪幾層 |
| `items.json`、`enemies.json`、`weapon_play.json` | 企劃負責 | 其他人找企劃 |
| `Editor/bin/**`（Git LFS，約 116 MB） | 二進位檔 | **只有程式**，用 `tools\package_editors.cmd`（第 6 節） |
| `src/core/game/ui/ui_state.h`、`game_ui.cpp` | 每個新的 UI 綁定都會動到 | 只有程式 |

**絕對不要**手動編輯產生出來的檔案來解決衝突。重新產生它。

### 4.3 AIGamestyle 子模組

AI 美術流程在它自己的儲存庫（`AIGamestyle/`）。先在那裡提交並推送，再在 TOMS 提交新的指標。要推送到那裡的人都需要那個儲存庫的寫入權限。

---

## 5. 文字與語言

- 玩家看得到的每個字串都是 `assets/data/text.json` 中的一個鍵，含 **zh_TW、en、zh_CN、ja、ko、es**。缺少的語言會退回另一種，所以遊戲
  仍可執行，但發佈前要把六種都寫好。
- 企劃寫 zh_TW 與 en；其他語言可以先用 AI 翻譯草擬，再請母語者檢查。
- 新增文字後：`python tools\make_ui_font.py`（由美術或新增文字的人執行）。忘了執行時，測試 `data.ui_font_covers_text` 會抓到。
- 編輯器本身的中文（選單、提示）在 `tools/studio_common/i18n/zh_TW/*.json`：屬於程式。

---

## 6. 建置、測試與發佈（程式）

- **測試：** 修改後執行 `tools\test.cmd`（只測這次修改可能影響的測試）；發行或發佈前執行 `tools\test.cmd all`
  （[08_TESTS.md](08_TESTS.md)）。企劃的情境與美術的資源檢查也在這裡執行：pull、建置、執行，並告訴弄壞測試的那個人。
- **不需要建置、任何人都能執行的資料檢查：** 關卡編輯器的檢查面板、`anim_editor --headless check`（在 `Editor\bin`）、
  `python tools\refresh_meshes.py --check`、`python tools\make_ui_font.py --check`。
- **編輯器：** 程式碼修改到編輯器時，用 `tools\package_editors.cmd` 更新 `Editor\bin`；把它單獨提交（「Editor：為 … 重新建置」），並通知
  其他人 pull。它會刪除 `Editor\bin` 中不屬於發行內容的檔案（例如執行遊戲產生的 `save\`）；先把它們移開。
- **網頁版：** `tools\release_web.cmd`，然後 `tools\publish_web.cmd`（GitHub Pages，[07_PUBLISH_GITHUB_PAGES.md](07_PUBLISH_GITHUB_PAGES.md)）。
  從所有人都已 pull 的提交發佈，並先執行 `tools\test.cmd all`。
- **發佈節奏：** 每週一次，由程式從 `main` 建置 `Editor\bin` 與網頁版並貼出連結，讓企劃與美術測試的永遠是玩家看到的同一個版本。

---

## 7. 工作節奏

| 時間 | 內容 |
|---|---|
| 週一，30 分鐘 | 每個人這週做什麼；誰認領了哪些樓層、檔案與 sprite 名稱；每個人需要其他人提供什麼 |
| 每天 | 早上 pull，下班前 push；任何卡住別人的事，在團隊聊天中講一句 |
| 週五 | 程式建置並發佈；每個人玩這週的版本 15 分鐘，記下問題 |
| 每件工作 | 進度報告（[progress_report/](../progress_report/PROGRESS_REPORT.md)）加一行：改了什麼、誰、還剩什麼 |

**角色之間的請求**放在 GitHub Issues（或一份共用清單），一個請求一個 issue，寫明誰提出、誰來做，以及怎樣算「完成」
（一張截圖、一個通過的情境、一個 sprite 名稱）。

---

## 8. 頭四週

### 第 1 週——每個人都能獨立作業

| 角色 | 工作 |
|---|---|
| 程式 | 確認企劃與美術能在自己的電腦上執行第 2 節的所有東西。把 `tests/test_switches.json` 中被關閉、但能通過的測試打開，並恢復提交 `795385a` 刪除的參考圖與測試資料（或移除那些測試），讓 `tools\test.cmd` 重新有意義。更新 `Editor\bin` 並發佈網頁版 |
| 企劃 | 把開場玩一遍：新遊戲、老武師、第 1 關、行商與祭壇。記下每個感覺不對的數值（每個武器等級的點數、每級的勝場、傷害、獎勵）。閱讀 [../../gameplay/zh_TW/07_GAMEPLAY_PLAN.md](../../gameplay/zh_TW/07_GAMEPLAY_PLAN.md) 與事件編輯器教學 |
| 美術 | 照著蒙皮教學（[17](../../engine/zh_TW/17_SKINNING_TUTORIAL.md)）做一次。列出遊戲還在借用的美術：弓是用特效形狀拼成的、劍與法杖是產生出來的暫用品、戰鬥中的玩家角色是 32 像素的地圖 sprite |

### 第 2 週——第一批真正的內容

| 角色 | 工作 |
|---|---|
| 程式 | 讓瀏覽器中的舊存檔得到初始弓（沒有武器的存檔）；修正攻擊軌道的 UI 警告（一個 `data-for` 綁定，[01](../../gameplay/zh_TW/01_RMLUI.md)「Adding a screen」）；不重啟遊戲就重新載入資料，讓企劃能即時調整 |
| 企劃 | 調整第 1–3 關：`weapon_play.json`、`enemies.json` 的數值、`minigames.json` 的獎勵；每改一條規則就寫一個測試情境 |
| 美術 | 真正的弓、劍與法杖美術及它們的片段（保留片段名稱）；戰鬥畫面的外觀（`battle.rcss`） |

### 第 3–4 週——讓遊戲長大

| 角色 | 工作 |
|---|---|
| 程式 | 劍與法杖自己的攻擊玩法（[07](../../gameplay/zh_TW/07_GAMEPLAY_PLAN.md) 5.2 多邊形、5.3 圓圈）；把 `text.json` 依區域拆開，減少多人同時改一個檔；在每次推送時建置並執行測試的 GitHub Actions |
| 企劃 | 用關卡編輯器與重新設計計畫（[06](../../gameplay/zh_TW/06_FLOOR_REDESIGN.md)）做第 4–10 層；第一幕的事件與對話；招式名稱與熟練度（5.4、第 6 節），寫成給程式的規則 |
| 美術 | 第一幕的怪物與 NPC（AI 流程 + 手工修整）；有幾個片段的蒙皮戰鬥角色；命中與升級的新特效 |

---

## 9. 讓三人協作更順的事（程式的待辦）

| 現在 | 更好 | 為什麼 |
|---|---|---|
| 遊戲資料要重啟才會顯示 | 按一個鍵重新載入 `assets/data`（像 UI 的 F5） | 企劃一分鐘內會調很多次數值 |
| 所有字串都在同一個 `text.json` | 每個區域一個檔（UI、道具、NPC、劇情） | 衝突更少 |
| 只有程式的電腦會跑測試 | 每次推送都跑 GitHub Actions | 企劃與美術能立刻知道自己弄壞了什麼 |
| 許多測試被關閉，有些參考圖不見了 | 全部打開，全部都在 | 測試通過才真的代表沒問題 |
| 關卡編輯器無法直接在正在編輯的樓層開始遊戲 | *從這裡開始玩* | 企劃最短的循環 |
| 需要程式碼的事件很晚才被發現 | 事件編輯器標出遊戲不認得的動作 | 請求能早點送到程式手上 |
