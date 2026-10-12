# 01 — 資料模型

> 英文原文：[01_DATA_MODEL.md](../../event_editor/01_DATA_MODEL.md)

事件編輯器沒有自己的檔案格式。它讀寫的是遊戲已經在用的三種檔案。資料模型寫在
`src/editor/src/event_graph.h`，裡面沒有 Qt，所以測試可以在每個平台上檢查這些規則。

## 1. 檔案

### 事件池：`assets/data/events/pool_*.json`

一個事件池一個檔案。事件池把相關的事件放在一起（`pool_act01` 放村莊的事件），但不決定
它們出現在哪裡。決定的是每個樓層自己的 `events` 清單，可以來自任何事件池，因為
一層樓可以容納好幾個角色的故事。遊戲在執行時不讀事件池檔。

```json
{
  "_comment": ["S3 — the common pool ..."],
  "events": [
    { "eventId": "ev_common_relic_road", "kind": "relic", "text": "ev_common_relic_road.text" }
  ]
}
```

| 欄位 | 意義 |
|---|---|
| `eventId` | 在所有事件池中唯一。慣例是 `ev_<地點>_<名稱>`，例如 `ev_village_well`，但任何名稱都可以，包括繁體中文（編輯器只拒絕 `" ' < > & \| \ /`）。事件變數、類型和樓層名稱也一樣；樓層名稱也是它的檔名（`第一層.json`），所以那裡也不能有 `: * ?` |
| `kind` | `assets/data/events/kinds.json` 中的一個類型（目前有 `relic`、`whisper`、`cache`、`trap`、`rescue`、`merchant_echo`、`memory_shard`）；在事件編輯器的 **類型** 分頁中編輯（[06](06_EVENT_LOGIC.md)） |
| `text` | `text.json` 中的一個鍵。慣例是 `<eventId>.text` |
| `title`、`desc` | 事件在玩家事件紀錄中的名稱和說明的文字鍵（`<eventId>.title`、`<eventId>.desc`）。每個事件都必填；見 [06](06_EVENT_LOGIC.md) §7。遊戲自 2026-10-10 起讀取 |
| `inLog` | 選用。`false` 讓這個事件不進事件紀錄；預設值來自 `kinds.json` 中它的種類 |

目前有的事件池：

| 事件池 | 事件數 | 類型 |
|---|---|---|
| `pool_common` | 10 | relic 2、whisper 2、cache 2、trap、rescue、merchant_echo、memory_shard |
| `pool_act01`（村莊） | 8 | whisper 2、rescue 2、cache 2、relic、trap |
| `pool_act02`（森林） | 8 | relic 2、trap 2、cache 2、whisper、rescue |
| `pool_act03`（城門） | 8 | cache 3、relic 2、whisper、trap、rescue |

### 樓層：`assets/data/story/floors/F01.json` … `F70.json`

每個樓層設定在 `"events"` 中列出它能抽到的事件，另外還有它的 `act`（`ch_01` …
`ch_10`）、迷宮、敵人和劇情。旁邊的 `F01.stage.json` 是產生出來的地圖格；裡面沒有
事件，編輯器會略過它。

```json
{ "id": "F01", "act": "ch_01", "events": ["ev_village_well", "ev_common_cache_wall", "ev_village_ledger"], ... }
```

### 文字：`assets/data/text.json`

```json
{ "strings": { "ev_village_well.text": { "zh_TW": "井水映出一個不是你的人影。", "en": "...", ... } } }
```

每個鍵對每種語言各有一個字串（`zh_TW`、`en`、`zh_CN`、`ja`、`ko`、`es`）。帶有
`"_todo": true` 的字串是佔位文字。

## 2. 關係圖

```
pool ──membership──▶ event ──text──▶ text key
floor ──can roll──▶ event
```

| 節點 | ID | 顯示為 |
|---|---|---|
| 事件池（Pool） | `pool_common` | 檔名 |
| 事件（Event） | `ev_village_well` | ID 和它的類型 |
| 樓層（Floor） | `F01` | 樓層 ID；也依編號排序和篩選 |
| 文字鍵（TextKey） | `ev_village_well.text` | 鍵 |

## 3. 問題規則

載入器會回報三種問題。每一種都是遊戲在執行時會碰到、或永遠不會顯示的東西：

| 規則 | 意義 | 在遊戲中會怎樣 |
|---|---|---|
| **孤立事件** | 事件在某個事件池中，但沒有任何樓層列出它 | 它永遠不會出現 |
| **缺少的事件 ID** | 樓層列出了一個沒有任何事件池定義的 ID | 抽到那一格時什麼也找不到 |
| **缺少文字鍵** | 事件的 `text` 鍵在 `text.json` 中沒有對應項目 | 玩家會看到原始的鍵名 |

值得在編輯器中加入的規則（`event_graph.cpp` 還沒有）：

- **未翻譯的文字：** 每種語言都是同一個字串，或字串帶有 `"_todo": true`。
- **重複的 ID：** 同一個 `eventId` 出現在兩個事件池。目前第二個會被默默略過。
- **事件池組合：** 某層樓的事件違反產生器的規則：至少一個 relic 或 whisper，
  且至少一個 cache（`pool_common.json` 註解中記載的限制）。

## 4. 目前資料的狀況

2026-10-05 對 repo 執行的結果：

- **4 個孤立事件：** `ev_village_grave_new`、`ev_village_dog_bowl`、`ev_village_bell_rope`、
  `ev_gate_broken_ram`。
- **376 個缺少的事件引用，指向 92 個不同的 ID，分布在 49 層樓（F22–F70，第 ch_04 到
  ch_10 章）。** 第 3 幕之後沒有任何事件池檔。這些 ID 已經命名好了，例如
  `ev_cavern_echo_wall` 和 `ev_common_relic_road_v3`，而且每個在 `text.json` 中都有一個佔位文字，
  放在不帶 `.text` 後綴的 ID 底下，標記為 `"_todo": true`，內容就是 ID 本身。
- **在事件池中的事件，缺少文字鍵 0 個。**
- **還沒有翻譯：** 現有的事件字串在每種語言中都是同一段中文。

所以編輯器的第一個真正工作，是用樓層已經使用的 ID 建立 `pool_act04` … `pool_act10`，
並撰寫它們的文字。

**2026-10-12 完成（負責人對 Q10 的答覆「a」）：** 這 92 個 ID 現在都是獨立的事件，各有事件池紀錄，以及自己的英文標題、
說明與文字：`pool_act04` … `pool_act10`（第 4–10 章的種子 ID 與它們的變體），另外 `pool_common` 多了 28 筆
`ev_common_*_v2` … `_v5`。`_v2` … `_v5` 不再是同一個事件的複本，每個講的內容都不同。事件池現在共有 126 個事件，
沒有任何樓層列出未定義的 ID（`unit.player_menu_test` 會檢查）。目前只有英文；其他語言在要求翻譯之前會改用英文。

**每層的事件索引**（`assets/data/events/floor_index.json`，Q10）：一個產生出來的檔案，列出每層列出或放置的事件
（id、類型、英文標題、事件池、格子）、該章在此層的劇情節點，以及樓梯鎖，並把每個事件對應回它所在的樓層。
由 HTML 編輯器寫出（`tools/web_editors/floor_index.js`）：關卡或事件編輯器開啟專案資料夾時，以及每次存進資料夾後。
索引與樓層不符時，`unit.floor_index_test` 會失敗（手動改過 JSON 後：用編輯器開啟資料夾即可）。
