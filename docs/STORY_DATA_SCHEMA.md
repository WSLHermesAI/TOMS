# 故事資料結構與關卡生成（STORY DATA SCHEMA）v3

> 搭配 `STORY_BIBLE.md`（70 層／10 幕／15 結局／輪迴）與 `SIDE_STORIES.md`（10 條旗艦支線＋事件池）。
> 本檔定義：**故事如何以資料表達**、**如何由故事生成 70 層關卡資料**、**結局判定與輪迴（半力重來）的資料規則**，以及故事要驅動的四個系統（技能樹／鍛造／裝備主動技／村莊樞紐）之 schema。

---

## 1. 檔案佈局

### 1.1 沿用（既有）

| 檔案 | 處置 |
|---|---|
| `data/story.json` | **升級 v3**（`arc[]` 保留為相容欄位） |
| `data/missions.json` | 保留並擴充（`branches`、`stageId` 改為樓層 id、`choiceId` 條件） |
| `data/stages/*.json` | 保留為**各幕首領層的手工關卡**（見 §3.2 對應表）；追加 `story` 區塊 |
| `data/dialogue/*.json` | 沿用（新增節點即可） |
| `data/equipment.json` | 擴充 `actives`、`upgradePath`、`forgedFrom`、`lore` |
| `data/items.json` / `store.json` / `combat.json` / `text.json` | 沿用；`text.json` 追加故事 key |

### 1.2 新增

| 檔案／目錄 | 用途 |
|---|---|
| `data/story/chapters/ch_01…ch_10.json` | 幕（10 幕）層級：主題、樓層範圍、首領、分歧、解鎖 |
| `data/story/floors/F01…F70.json` | **每層一檔**：迷宮參數、敵人組合、事件、支線掛點、敘事 key |
| `data/story/flags.json` | 旗標宣告表 |
| `data/story/counters.json` | `insight` / `resolve` / `humanity` 的宣告與顯示文字 |
| `data/story/endings.json` | **15 種結局**與優先序條件樹、輪數需求 |
| `data/story/cycles.json` | 輪迴規則（半力公式、清空清單、每輪調整） |
| `data/events/pool_common.json` + `pool_act01…10.json` | 每層小事件池 |
| `data/skills.json` / `forge.json` / `hub.json` | 四大系統（技能樹／鍛造／樞紐；主動技在 `equipment.actives`） |
| `tools/gen_story_data.py` | 由故事生成關卡與系統資料（§6） |
| `tools/gen_floors.py` | 由樓層表生成 70 層檔案（§6.3） |
| `tools/validate_story.py` | 驗證器（§10） |

---

## 2. 條件 DSL

### 2.1 既有

```json
{ "type": "storyBeatAtLeast", "value": "ch_01" }
{ "type": "storyFlagSet",     "flag": "met_sorcerer" }
{ "type": "stageCleared",     "stageId": "F05" }
{ "type": "itemHeld",         "itemId": "key_yellow", "count": 1 }
```

### 2.2 新增（本版需要）

```json
{ "type": "choiceMade",       "choiceId": "c_final_stance", "optionId": "opt_guard" }
{ "type": "sideStoryState",   "sideStoryId": "ss_08", "state": "completed" }
{ "type": "cultivationTier",  "min": "golden_core" }
{ "type": "memoryShards",     "min": 5 }
{ "type": "counterAtLeast",   "counter": "insight", "min": 6 }
{ "type": "cycleIndexAtLeast","min": 2 }
{ "type": "endingSeen",       "endingId": "e_04" }
```

複合運算沿用 `{"all":[…]} / {"any":[…]} / {"not":{…}}`。

### 2.3 旗標與計數器宣告

`data/story/flags.json`

```json
{
  "flag_motive_know_self":  { "setBy": "ch_01.c_motive.opt_know_self", "scope": "run" },
  "flag_gate_unsealed":     { "setBy": "ch_03.c_gate_seal.opt_unseal", "scope": "run" },
  "flag_hate_carried":      { "setBy": "ch_05.c_vengeance_understanding.opt_vengeance", "scope": "run" },
  "flag_truth_sought":      { "setBy": "ch_05.c_vengeance_understanding.opt_understanding", "scope": "run" },
  "flag_path_vendetta":     { "setBy": "ch_09.c_final_stance.opt_vendetta", "scope": "run" },
  "flag_path_guard":        { "setBy": "ch_09.c_final_stance.opt_guard", "scope": "run" },
  "flag_blow_kill":         { "setBy": "ch_10.c_final_blow.opt_kill", "scope": "run" },
  "flag_blow_stay":         { "setBy": "ch_10.c_final_blow.opt_stay", "scope": "run" },
  "flag_memory_shard_01":   { "setBy": "F04.beat_shard", "scope": "run" }
}
```

`data/story/counters.json`

```json
{
  "insight":  { "label": { "zh_TW": "道心澄明" }, "displayAt": 6, "clamp": [-20, 20] },
  "resolve":  { "label": { "zh_TW": "執念纏身" }, "displayAt": 6, "clamp": [-20, 20] },
  "humanity": { "label": { "zh_TW": "仁心未泯" }, "displayAt": 6, "clamp": [-20, 20] }
}
```

> `displayAt`：達到門檻才在 HUD 以文字顯示狀態（**不顯示數字**）。

---

## 3. 樓層與關卡

### 3.1 樓層 id 規則

- 樓層 id：`F01` … `F70`（`F` + 兩位數）。
- 幕：`ch_01` … `ch_10`（第 N 幕涵蓋 `F(7N-6)`–`F(7N)`）。
- 每幕第 7 層為首領層，對應一支**手工關卡**（既有 `data/stages/*.json`）。

### 3.2 樓層 ↔ 既有關卡對應表

| 首領層 | 既有關卡檔 | 場景 | 幕 |
|---|---|---|---|
| F07 | `stage01.json` 村莊外緣 | 邊村與井 | 一 |
| F14 | `stage02.json` 森林小徑 | 林道 | 二 |
| F21 | `stage03.json` 門樓 | 黃門 | 三 |
| F28 | `stage04.json` 藏書閣 | 書閣 | 四 |
| F35 | `stage05.json` 墓穴 | 墓穴 | 五 |
| F42 | `stage06.json` 洞窟 | 洞窟 | 六 |
| F49 | `stage07.json` 軍營 | 軍營 | 七 |
| F56 | `stage08.json` 神殿 | 神殿 | 八 |
| F63 | `stage09.json` 王座前室 | 前室 | 九 |
| F70 | `stage10.json` 巫師王座 | 王座 | 十 |
| （F70 後） | `stage_11.json` 安穩之星 | 結局舞台（非樓層） | 終 |

**非首領層（60 層）**由 `tools/gen_floors.py` 依樓層表生成（Wilson 迷宮＋事件＋敵人組合），不需手工擺放。

---

## 4. 樓層檔 schema（`data/story/floors/F33.json` 範例）

```json
{
  "id": "F33",
  "act": "ch_05",
  "indexInAct": 5,
  "name": "story.f33.name",
  "seal": "soul",
  "role": "side-story-stage",
  "maze": { "cols": 35, "rows": 24, "rooms": 8, "loops": 6, "seed": "auto" },
  "enemies": { "mix": ["skeleton", "wraith"], "count": [12, 16], "elites": 2, "boss": null },
  "items": { "count": 8, "table": ["potion_red", "potion_blue", "mat_echo_shard", "coin"] },
  "events": ["ev_crypt_ten_year_candle", "ev_crypt_nameless_brick", "ev_crypt_choir_echo"],
  "sideStoryHooks": [
    { "sideStoryId": "ss_05", "cell": [7, 18], "trigger": "proximity",
      "requires": { "type": "storyBeatAtLeast", "value": "ch_05" } }
  ],
  "story": {
    "chapter": "ch_05",
    "intro": "story.f33.intro",
    "ambient": ["story.f33.ambient.1", "story.f33.ambient.2"],
    "beats": ["b_crypt_evidence", "b_crypt_memory"],
    "exitUnlocks": { "counters": { "insight": 1 } }
  },
  "nextFloor": "F34",
  "meta": { "targetMinutes": [4, 7], "difficultyTier": 5 }
}
```

### 4.1 幕檔 schema（`data/story/chapters/ch_05.json`）

```json
{
  "id": "ch_05",
  "floors": ["F29", "F30", "F31", "F32", "F33", "F34", "F35"],
  "bossFloor": "F35",
  "bossStage": "stage05",
  "title": "story.ch05.title",
  "seal": "soul",
  "beats": [
    { "id": "b_evidence",  "kind": "dialogue", "nodeId": "ghost_villager.evidence", "floor": "F30" },
    { "id": "b_memory",    "kind": "memory",   "shardId": "shard_05_the_night", "floor": "F32",
      "setFlags": ["flag_memory_shard_05"] },
    { "id": "b_finale",    "kind": "boss",     "floor": "F35", "enemyId": "wraith_choir" }
  ],
  "grants": { "skills": ["s_shenshi"], "skillPoints": 2, "hub": ["hub.crypt_shrine"] },
  "choices": [
    { "id": "c_vengeance_understanding",
      "prompt": "story.ch05.c5.q",
      "options": [
        { "id": "opt_vengeance",     "text": "story.ch05.c5.o1", "setFlags": ["flag_hate_carried"],
          "counters": { "resolve": 2 }, "endingWeight": {} },
        { "id": "opt_understanding", "text": "story.ch05.c5.o2", "setFlags": ["flag_truth_sought"],
          "counters": { "insight": 2 }, "endingWeight": {} }
      ], "reversible": false }
  ],
  "sideStoryHooks": ["ss_05"],
  "exitConditions": { "type": "stageCleared", "stageId": "F35" }
}
```

---

## 5. 迷宮與規模（70 層放大曲線）

### 5.1 樓層表（`tools/gen_floors.py` 的輸入，與 `STORY_BIBLE.md` §5 一致）

| 樓層 | 迷宮格 (cols×rows) | 房間 | 環路 | 事件 | 敵人 | 精英 | 道具 | 難度階 |
|---|---|---|---|---|---|---|---|---|
| F01–F07 | 19×16 | 3 | 2 | 3 | 4–6 | 0 | 4 | 1 |
| F08–F14 | 23×18 | 4 | 3 | 4 | 6–8 | 0 | 5 | 2 |
| F15–F21 | 27×20 | 5 | 4 | 5 | 8–11 | 1 | 6 | 3 |
| F22–F28 | 31×22 | 6 | 5 | 6 | 10–14 | 1 | 7 | 4 |
| F29–F35 | 35×24 | 7 | 6 | 7 | 12–16 | 2 | 8 | 5 |
| F36–F42 | 39×26 | 8 | 7 | 8 | 14–19 | 2 | 9 | 6 |
| F43–F49 | 45×30 | 9 | 8 | 9 | 16–22 | 3 | 10 | 7 |
| F50–F56 | 51×34 | 10 | 9 | 10 | 18–25 | 3 | 11 | 8 |
| F57–F63 | 57×38 | 11 | 10 | 11 | 20–28 | 4 | 12 | 9 |
| F64–F70 | 63×42 | 12 | 11 | 12 | 22–32 | 4 | 13 | 10 |

**生成公式**（`gen_floors.py`）

```python
def tile_counts(floor_no):                      # floor_no: 1..70
    tier = (floor_no - 1) // 7                  # 0..9
    base_cols, base_rows = 19 + 4 * tier, 16 + 2 * tier + 2 * (tier >= 4)
    # 59 以上另加寬度，讓終幕有「塔頂空曠」感
    if tier >= 8: base_cols += 2 * (tier - 7)
    return base_cols, base_rows                 # F70 → 63 x 42
```

- **必須維持連通性**：沿用 `tools/gen_mazes.py` 的 Wilson 演算法與 BFS 可達性檢查（每層至少 1 條往下一層的路徑）。
- **環路（loops）**：70 層的長度下，純完美迷宮會過於折磨；每層額外打通 `loops` 條牆，形成迴圈。
- **房間**：房間即「事件容器」，房間數與事件數一致（§5.1 欄位）。
- **相機**：地圖大於畫面時由相機跟隨（既有 `add camera` 功能）。

### 5.2 事件抽樣

```
events(F) = 幕限定池 × 40% + 通用池 × 40% + 主線碎片（若該層有）× 20%
數量      = §5.1 的「事件」欄
約束      = 同層不得重複 eventId；每層至少 1 個 relic/whisper 與 1 個 cache
```

---

## 6. 由故事生成資料（`tools/gen_story_data.py` ＋ `gen_floors.py`）

### 6.1 流程

```
data/story.json + story/chapters/*.json
        │
        ├─(1) 生成 70 層樓層檔 ──────────► data/story/floors/F01…F70.json（gen_floors.py）
        ├─(2) 注入首領層 story 區塊 ────► data/stages/stage01…stage10.json（僅 story 欄位）
        ├─(3) 展開 grants ──────────────► skills.json / forge.json / equipment.json / hub.json
        ├─(4) 產生任務條目 ─────────────► missions.json（m_chNN 主線、ss_NN 支線）
        ├─(5) 產生事件 ─────────────────► floors[].events（由事件池抽樣）
        ├─(6) 產生 i18n key 佔位 ───────► text.json（缺翻譯以 zh_TW 回填並標 _todo）
        └─(7) 驗證 ─────────────────────► validate_story.py（§10）
```

### 6.2 生成器偽代碼

```python
def emit_floor(floor_no, act, spec):
    doc = {
        "id": f"F{floor_no:02d}", "act": act["id"],
        "indexInAct": ((floor_no - 1) % 7) + 1,
        "role": "boss-stage" if (floor_no % 7 == 0) else
                ("side-story-stage" if ((floor_no - 1) % 7) == 4 else "normal"),
        "maze":   {"cols": spec.cols, "rows": spec.rows, "rooms": spec.rooms, "loops": spec.loops},
        "enemies": {"mix": act["enemyMix"], "count": spec.enemyRange,
                    "elites": spec.elites, "boss": act["bossId"] if floor_no % 7 == 0 else None},
        "items":  {"count": spec.items, "table": act["itemTable"]},
        "events": sample_events(floor_no, act, spec.eventCount),
        "sideStoryHooks": hooks_on_floor(floor_no, act),
        "story":  {"chapter": act["id"], "intro": act_key(act, floor_no, "intro"),
                   "ambient": act_keys(act, floor_no, "ambient", 2),
                   "beats": beats_on_floor(act, floor_no)},
        "nextFloor": f"F{floor_no + 1:02d}" if floor_no < 70 else None,
    }
    write_json(f"data/story/floors/{doc['id']}.json", doc)

def sample_events(floor_no, act, n):
    pool = load(f"data/events/pool_act{act['number']:02d}.json") + load("data/events/pool_common.json")
    picked = weighted_sample(pool, n, weights={"act": 0.4, "common": 0.4, "main": 0.2})
    assert any(e["kind"] in ("relic", "whisper") for e in picked)
    assert any(e["kind"] == "cache" for e in picked)
    return [e["eventId"] for e in picked]
```

**規則**：生成器只覆寫自己擁有的欄位；手工關卡的 `tiles`／`legend` 不動。

---

## 7. 結局系統（`data/story/endings.json`）

### 7.1 Schema

```json
{
  "resolutionOrder": "firstMatchWins",
  "fallback": "e_05",
  "endings": [
    {
      "id": "e_01",
      "name": "story.ending.e01.name",
      "tone": "secret-good",
      "requires": { "all": [
        { "type": "counterAtLeast",  "counter": "insight", "min": 12 },
        { "type": "cycleIndexAtLeast", "min": 9 },
        { "type": "itemHeld", "itemId": "qingxiao_blade" },
        { "type": "sideStoryState", "sideStoryId": "ss_04", "state": "completed" },
        { "type": "sideStoryState", "sideStoryId": "ss_08", "state": "completed" },
        { "type": "sideStoryState", "sideStoryId": "ss_10", "state": "completed" },
        { "type": "sideStoryState", "sideStoryId": "ss_09", "state": "completed" }
      ] },
      "text": "story.ending.e01.text",
      "unlocksHint": "hint_e02_location",
      "allowsRebirth": true
    },
    {
      "id": "e_08", "name": "story.ending.e08.name", "tone": "bad",
      "requires": { "all": [
        { "not": { "type": "itemHeld", "itemId": "key_red" } },
        { "type": "sideStoryState", "sideStoryId": "ss_10", "state": "completed",
          "negate": true }
      ] },
      "text": "story.ending.e08.text", "allowsRebirth": true
    },
    {
      "id": "e_05", "name": "story.ending.e05.name", "tone": "neutral",
      "requires": { "any": [
        { "type": "storyFlagSet", "flag": "flag_blow_stay" },
        { "type": "always" }
      ] },
      "text": "story.ending.e05.text", "allowsRebirth": true
    }
  ]
}
```

### 7.2 判定演算法

```
judge(context):
  for e in endings (依 resolutionOrder 的優先序，陣列順序即優先序):
      if conditionsSatisfied(e.requires, context):  return e
  return fallback

context = { flags, choices, counters, items, sideStories, cycleIndex, shards, deaths, endingsSeen }
```

**優先序原則**：隱藏好結局 → 好 → 中性 → 壞（特定條件） → fallback。**保證 fallback 條件為 `always`**（§10 V13）。

### 7.3 觸發時機

| 觸發點 | 結局 |
|---|---|
| 任何樓層全滅 | `e_10`（`cycleIndex=9` 且全滅）、`e_13`（非首領戰死亡累積 ≥12）、否則輪迴提示（非結局） |
| F70 第三階段失敗 | `e_12`（星核裂痕 100%） |
| F69 特定選擇 | `e_09`（被說服） |
| F70 通關 | 依 §7.2 判定（`e_01`–`e_08`、`e_11`、`e_14`、`e_15`） |

---

## 8. 輪迴（`data/story/cycles.json`）

```json
{
  "maxCycles": 9,
  "carry": {
    "cultivationTier":        { "op": "halveFloor" },
    "skills":                 { "op": "keepAll", "effectScale": 0.5, "keepUnlockOnlyFull": true },
    "skillPoints":            { "op": "halveFloor" },
    "atkDefBonus":            { "op": "scale", "value": 0.5 },
    "superMax":               { "op": "halveFloor", "min": 1 },
    "memoryShards":           { "op": "keepAll" },
    "forgeRecipesKnown":      { "op": "keepAll" },
    "activeSkillsKnown":      { "op": "keepAll" },
    "cycleIndex":             { "op": "increment", "by": 1 }
  },
  "reset": {
    "equipment":       "starting_set",
    "consumables":     "clear",
    "keys":            "clear",
    "materials":       "clear",
    "gold":            0,
    "sideStories":     "reset",
    "flags":           "clear",
    "counters":        "clear"
  },
  "perCycleModifiers": {
    "enemyStatScale":  { "op": "addPerCycle", "value": 0.15, "cap": 1.20 },
    "variantFloorsPct": 20,
    "extraEliteChance": { "op": "addPerCycle", "value": 0.05, "cap": 0.40 }
  },
  "endingGates": { "e_01": { "minCycle": 9 }, "e_09": { "minCycle": 2 }, "e_11": { "minCycle": 2 } }
}
```

**數值範例（半力示意）**

| 項目 | 第 1 輪結束值 | 入輪迴後（第 2 輪起始） |
|---|---|---|
| 修為階 | 大帝（tier 6） | 金丹（tier 3 = ⌊6/2⌋） |
| 青霄劍意 | 會心 +8%、判定帶 +5% | 會心 +4%、判定帶 +2.5% |
| 技能點 | 18 點（已用完 18） | 9 點可重配 |
| 必殺上限 | 4 | 2 |
| 武器 | `qingxiao_blade`（T4） | `wand`（起始） |
| 消耗品／鑰匙／素材 | 全部 | 清空 |
| 記憶碎片 | 7 片 | **7 片保留** |
| 敵人 | 基準 | **+15%** |

---

## 9. 存檔整合（`schemaVersion: 3`）

| 資料 | 落點 | 說明 |
|---|---|---|
| 旗標 `flags{}` / 分歧 `choices{}` | run | 每輪重置 |
| 計數器 `counters{insight,resolve,humanity}` | run | 每輪重置 |
| 支線狀態 `sideStories{}` | run | `available/accepted/declined/completed/failed` |
| 樓層進度 `floor` / `clearedFloors[]` | run | 供 `stageCleared` 條件 |
| 技能／技能點／裝備／道具／金幣 | run | 輪迴時依 §8 處理 |
| 記憶碎片 `shards[]` | run（輪迴保留） | 影響 `e_07` |
| 死亡統計 `deaths{total, nonBoss}` | run | 影響 `e_13` |
| `cycleIndex` / `endingsSeen[]` / `hintsUnlocked[]` | **meta** | 跨輪保留，供輪迴錄 UI |

**相容規則**：讀到 `schemaVersion < 3` 的舊存檔時，以預設值補齊（`counters` 全 0、`cycleIndex = 1`、`endingsSeen` 空），**不得拒絕載入**。

---

## 10. 驗證器（`tools/validate_story.py`）

| # | 不變式 | 失敗訊息示例 |
|---|---|---|
| V1 | 每個 `setFlags` 的旗標在 `flags.json` 宣告 | `ch_09 sets undeclared flag` |
| V2 | 每個 `choiceMade` 引用的選項存在 | `ss_09 requires c_final_stance.opt_nonexist` |
| V3 | 每條支線可達（前置可滿足） | `ss_10 unreachable` |
| V4 | 每個 `grants` 引用的 id 存在 | `ch_01 grants unknown skill s_typo` |
| V5 | **每個結局可達**（以條件求解器驗證旗標／計數器組合存在） | `e_01 unreachable: needs cycle 9 + all side stories` |
| V6 | 6 語系皆有值或已標 `_todo` | `story.f33.intro.ja missing` |
| V7 | 樓層 ↔ 幕 對應一致（`act`、`indexInAct`、`nextFloor`） | `F34.act=ch_04 mismatch` |
| V8 | 迷宮連通且至少 1 條通往下一層 | `F50 unreachable stairs` |
| V9 | **樓層表與 `STORY_BIBLE §5` 一致**（格數／事件／敵人數） | `F57 cols 55 != 57` |
| V10 | 單行文字在 UI 欄寬內（中／英） | `overflow: 31 chars > 15` |
| V11 | 支線獎勵含技能／裝備／圖譜／NPC／關鍵道具之一 | `ss_06 rewards only gold` |
| V12 | 每層事件含 ≥1 relic/whisper 與 ≥1 cache | `F18 missing whisper` |
| V13 | 結局優先序**存在 fallback**（`always`）且無覆蓋死區 | `e_14 shadowed by e_05` |
| V14 | 輪迴公式不得產生負值或超過原始值 | `e_01 carry: superMax < 1` |
| V15 | 每幕恰有 1 條旗艦支線，且落在該幕第 5 層 | `ch_06 has 0 side stories` |
| V16 | 事件池總量 ≥ 該幕樓層所需事件的 3 倍（避免重複感） | `act07 pool too small` |

---

## 11. i18n 規則

1. **key 命名**：`story.f33.intro`、`story.ch05.c5.q`、`story.ss05.n2`、`story.ending.e01.text`、`skill.s_yinqi.name`、`ev_crypt_nameless_brick.text`。
2. **六語系**：`zh_TW`（權威）／`en`／`zh_CN`／`ja`／`ko`／`es`；缺翻譯以 zh_TW 回填並標 `"_todo": true`。
3. **單行上限**（依 UI 欄寬）：戰鬥欄位中文 ≤15 字／英文 ≤28 字元；一般敘事行中文 ≤28 字。
4. **前世／今生名**：以 `nameNow` / `namePrev` 兩 key 表達，`revealAt` 決定切換。

---

## 12. 相容性與遷移

1. `story.json` 舊 `arc[]` 保留（由 `chapters[].title` 投影）。
2. `missions.json` 既有三筆不動；主線 `m_chNN`、支線 `ss_NN` 前綴避免碰撞。
3. `stage.story` 與 `floors/*.json` 為 additive；缺 `story` 的關卡視為「無劇情」。
4. 既有 11 個關卡檔 → 對應 §3.2 的首領層；其餘 60 層由生成器產生，**手工關卡不受生成器影響**。
5. 條件 DSL 新增型別需同步擴充 `condition.cpp` 並補 `condition_eval_test` 案例。

---

## 13. 實作順序（70 層版）

| 階段 | 內容 | 產出 |
|---|---|---|
| M1 | `gen_floors.py` + 樓層表 + V7/V8/V9 | 70 層可生成、可走通 |
| M2 | `story.json` v3 + `ch_01`–`ch_03` + 事件池（前三幕） | 前 21 層有敘事與事件 |
| M3 | 條件 DSL 擴充（`choiceMade`／計數器／`cycleIndex`）＋存檔 v3 | 分歧與計數器可保存 |
| M4 | 技能樹（`skills.json` ＋UI） | 成長線成立 |
| M5 | 鍛造（`forge.json` ＋火爐 UI）＋樞紐 NPC（`hub.json`） | 經濟與強化閉環 |
| M6 | 裝備主動技（`actives`） | 主動技灌注 |
| M7 | `endings.json` ＋判定器 ＋ 結局 UI（15 種） | 結局可達成 |
| M8 | `cycles.json` ＋輪迴流程（半力、清空、變體層、輪迴錄） | 重玩機制 |
| M9 | `ch_04`–`ch_10` 全展開 ＋ 全驗證器（V1–V16） | 完整 70 層 |

---

## 14. 與既有文件的關係

| 文件 | 關係 |
|---|---|
| `GAMEPLAY_ROGUELIKE_*` | 迷宮（Wilson）與 roguelike 方向；本檔 §5 是其輸入 |
| `MAIN_BATTLE_SCENE_DESIGN.md` / `BATTLE_SYSTEM_V2_PROPOSALS.md` | 戰鬥機制；`bar`、`actives` 建立其上 |
| `NODE_SYSTEM.md` | `beats[].kind` 沿用其語意 |
| `story_controller.h` / `mission_system.h` / `condition.h` / `save_system.h` | 需擴充的四個引擎模組（§2.2、§9、§12.5） |
| `STORY_BIBLE.md` / `SIDE_STORIES.md` | 敘事與支線來源（本檔為其資料契約） |
