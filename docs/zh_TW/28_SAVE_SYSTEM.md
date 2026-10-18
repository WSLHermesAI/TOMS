> 英文原文：[28_SAVE_SYSTEM.md](../28_SAVE_SYSTEM.md)

# 28 — 存檔系統

遊戲如何保存玩家的進度：三個存檔格、存檔檔案的內容、何時寫入、如何防止遺失、遊戲改版後舊存檔如何繼續讀取，以及樓層
物件在存檔中如何識別。程式碼：`src/core/game/save/`（`save_system.*`：格式與檔案讀寫；`save_slots.*`：存檔格），遊戲端在
`src/core/game/core/game_title_glue.cpp`。

## 1. 存檔格與檔案

| 平台 | 位置 |
|---|---|
| 桌面 | 遊戲旁的 `save/slot1.json` … `slot3.json`（可用 `TOMS_SAVE_DIR` 改） |
| 網頁 | 瀏覽器 IndexedDB 中的 `/save/`（IDBFS） |
| Android | App 的私有資料夾（`SDL_GetPrefPath`） |

每個存檔格旁邊有 `slotN.json.bak`（上一次之前的存檔，第 3 節），寫入時會短暫出現 `slotN.json.tmp`。設定另外存放
（`save/settings.json`），開新遊戲也會保留。

**繼續遊戲**列出各存檔格；選一個就讀取它（`Game::continueFromSlot`），之後自動存檔都寫入這一格。空的存檔格會詢問是否在
那裡開新遊戲。

## 2. 存檔格的內容

```json
{
  "saveVersion": 1, "savedAt": "2026-10-18 10:00:00", "playTimeSec": 3725, "stageName": "...",
  "meta": { "schemaVersion": 3, "storyFlags": [], "cycleIndex": 1, "endingsSeen": [], "forgeRecipesKnown": [] },
  "run": {
    "schemaVersion": 3, "currentStageId": "F03", "floor": "F03",
    "player": { "hp": 210, "atk": 18, "key_yellow": 2, "inv": ["treasure_map"], "weapon": "iron_sword" },
    "entityStatus": { "F03#o12": "Defeated", "F03#o7": "Collected", "F02#o3": "Opened" },
    "vars": { "elder_trust": 2 }, "flags": { "met_elder": true },
    "eventsStarted": ["e_lost_key"], "eventsFinished": ["e_intro"], "eventCounts": { "e_daily_gift": 3 },
    "npcTalk": { "elder": [1, 4, 0] }, "choices": {}, "counters": { "humanity": 1 }, "sideStories": {}
  }
}
```

- `meta`：轉生後仍保留的進度（劇情旗標、看過的結局、永久加成、鍛造配方）。
- `run`：這一輪。**擊敗的怪物、撿起的道具、打開的門與閘門、完成的地圖事件**在 `entityStatus`（第 5 節）。
  **事件變數**在 `vars`（只存有變動的；其餘是 `story/vars.json` 的預設值），另有 `flags`、事件記錄、重複次數與每位 NPC
  的對話進度。

## 3. 何時寫入，以及防止遺失的措施

- **自動存檔：** 有變動就標記為「待存」，最多每 3 秒寫一次。換樓層、離開商店、開新遊戲與一輪結束會立即寫入。
- **關閉與切到背景時**（`Game::flushSave`）：關閉視窗（`SDL_EVENT_QUIT`）、Android 把 App 切到背景、網頁被隱藏或關閉
  （`pagehide`、`visibilitychange`）時立即寫入，不會遺失自動存檔最後幾秒的進度。
- **原子寫入並寫到磁碟**（`writeJsonAtomic`）：先寫到 `.tmp`，強制寫到磁碟（`_commit`／`fsync`，所以改名後斷電也不會
  留下空檔），再改名覆蓋存檔格。
- **備份：** 改名前，目前的存檔檔案改名為 `.bak`——只在它還能讀取時才這麼做，所以損毀的檔案絕不會取代好的備份。
- **讀取時改用備份：** 存檔檔案不見或讀不了時，讀取它的 `.bak`；繼續遊戲頁面標示「使用備份」，讀取後也會顯示訊息。
- **損毀不等於空：** 兩個檔案都讀不了時，存檔格顯示「存檔損毀」。不會讀取，也不會提供在那裡開新遊戲（那會覆蓋也許還能
  手動救回的檔案）；遊戲內的存檔頁仍可刻意存到那一格。
- **較新版本遊戲的存檔**（`saveVersion` 或 `schemaVersion` 較高）仍可讀取，但絕不覆蓋：遊玩時不會寫入那一格，繼續遊戲
  頁面標示「較新版本的存檔」，並顯示訊息；存檔頁不提供那一格。它不認得的欄位會保留。
- **刪除存檔格**時連同備份一起刪除。

## 4. 修改存檔格式（版本與遷移）

每個存檔格都帶有版本：`saveVersion`（存檔格表頭，`kSlotSaveVersion`），以及 `meta` 與 `run` 的 `schemaVersion`
（`kMetaSaveSchemaVersion`、`kRunSaveSchemaVersion`）。

- **新增欄位**只需要預設值：較舊的檔案沒有這個欄位時，讀取端就用預設值（目前所有欄位都是這樣加的）。在 `toJson` 寫入，
  用 `j.value("name", 預設值)` 讀取。
- **改名、刪除、拆分欄位，或改變欄位的意義**需要遷移：提高格式版本，並在 `save_system.cpp` 的 `kMetaMigrations`／
  `kRunMigrations` 加一個步驟。步驟依序（v1 → v2 → v3 …）在讀取前作用在 json 上，所以任何舊版本的存檔都會升級到最新。
  提高版本時有 `static_assert` 提醒加上步驟。
- **絕不拒絕舊存檔。** `save_test` 會讀取一個版本 1 的檔案來確保這一點。

## 5. 存檔中的樓層物件：穩定 id

會存狀態的樓層物件（怪物、道具、鑰匙、事件、閘門、門）在關卡檔中有穩定 id：`"objects": { "5,3": { "uid": "o12" } }`。
存檔以它作為狀態的鍵：`"F03#o12": "Defeated"`。

- **原因：** 以位置作鍵（`"F03|5,3"`，舊方式）的話，玩家已有存檔後在關卡編輯器移動敵人，存檔會標錯格子——那一格的新敵人
  一開始就被擊敗，而被移動的敵人又會出現。
- **關卡編輯器**在每次確認編輯時替每個這類物件給 uid（`o1`、`o2`……在樓層內唯一）；uid 跟著物件移動，新物件得到新的
  uid，複製出來的會重新編號。物件設定視窗會保留它。
- **`tools/add_stage_uids.py`** 替已出貨的關卡檔加上 uid（`--check` 列出缺少的檔案）。
- **舊存檔：** 樓層載入時，以物件舊位置鍵存的狀態會移到它的 uid 鍵（`Game::migrateStatusKeys`）。沒有 uid 的物件（舊的
  或手寫的關卡檔）仍以位置作鍵。

已發佈的樓層不要手動重新編號 uid：玩家的存檔參照的就是它們。

## 6. 為什麼用 JSON（而不是 SQLite 或 MongoDB）

- **MongoDB** 是資料庫伺服器：用於遊戲伺服器後面的雲端存檔，不適合本機存檔檔案。
- **SQLite** 可行（內建交易），但存檔是一整棵狀態樹，多半只會存一個 JSON 字串；格式變更一樣需要版本與遷移
  （`PRAGMA user_version`），還會多一個相依套件，網頁版也要另外建置 wasm。
- **有版本、原子寫入、備份與遷移的 JSON** 是單機遊戲的常見做法，也就是目前的做法。若在意檔案大小，nlohmann/json 可以把
  同樣的結構寫成 MessagePack 或 CBOR。

## 7. 測試

`save_test`（單元測試）：往返轉換、第二次存檔後的備份、存檔被截斷時改讀備份、損毀檔不會取代備份、損毀與空的區別、較新
版本存檔可讀但不被覆蓋（且保留未知欄位）、版本 1 檔案以預設值讀取、uid 鍵、刪除存檔格連同備份。
`stage_core_test`：uid 存在、移動時保留、新物件有新的、且唯一。
