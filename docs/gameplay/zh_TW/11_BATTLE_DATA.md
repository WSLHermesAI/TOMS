> 英文原文：[11_BATTLE_DATA.md](../11_BATTLE_DATA.md)

# 11 — 戰鬥資料：屬性、戰鬥者、狀態、事件與公式（設計）

**狀態：** 設計，2026-10-24。擁有者的四個回答在 §2。目前已完成：依等級給屬性點數（§3.2）。§4 全部是計畫，還不是程式。

擁有者的目標：每場戰鬥都讀同一套結構，讓玩家和怪物共用屬性、戰鬥者的狀態決定它的動畫和音效、由戰鬥事件系統改變戰鬥的進行，
而且故事可以安排任何對戰（玩家對怪物、怪物對怪物、NPC 對怪物）。手感目標：**勇者鬥惡龍（Dragon Quest）**。

## 1. 現在有什麼

| 部分 | 位置 | 現況 |
|---|---|---|
| 屬性 | [stats.json](../../../assets/data/stats.json) | STR、DEX、AGI、VIT、INT、LUK，各有效果：STR → ATK、DEX → 命中區、AGI → 等待變短、VIT → 最大 HP、INT → 技能威力、LUK → 金幣。**只有玩家。** |
| 等級 | stats.json `levelUp` | 每級 `lv × 30` EXP；每級 +2 ATK、+1 DEF、+10 HP、每個屬性 +1（LUK +0），**再加屬性點數**（§3.2）；最高 99 級 |
| 能力 | skills.json、actives.json、weapon_play.json | 7 個功法（固定加 ATK / DEF）、2 個主動技（必定會心、致命傷留 1 HP）、武器等級（連擊點 3→5、屬性加成、粒子） |
| 怪物 | [enemies.json](../../../assets/data/enemies.json) | 67 筆：`hp, atk, def, exp, gold, atk_interval_ms, boss, sprite`。**沒有屬性、武器、特殊招式或音效。** |
| 狀態 → 外觀 | game_combat.cpp | 玩家：idle / charge / attack / miss（動畫來自武器的 `battleAnim`）；怪物：idle / attack / hurt / die（`monster_battle.anim`）。在 `updateBattleRig` / `updateMonsterRig` 裡用 `if` 選。音效寫在程式裡（`player_attack`、`enemy_attack`）。玩家沒有受傷或倒下。 |
| 事件 | event_bus.h | 只有 `EnemyDefeated`；動畫的 `hit` / `release` 事件讀到後就丟掉 |
| 誰對誰 | `CombatState` | 永遠是玩家對一個 `EnemyInst` |
| 公式 | game_combat.cpp、power_bar.h | 寫在 C++：`max(1, ATK − DEF) × 連擊威力 × 集氣`；防禦減少傷害；沒有會心、閃避或命中率 |

[combat.json](../../../assets/data/combat.json) 是舊的自動戰鬥公式，沒有程式讀它，應該刪除。

## 2. 擁有者的決定（2026-10-24）

1. **怪物和玩家有同樣的六個屬性。** 所有人用同一份屬性清單，公式就能雙向使用。
2. **怪物對怪物（或 NPC 對怪物）是故事事件。** 以觀看為主。只有在事件設定的時機，玩家才能接手操作：「第 3 次攻擊之後」、
   某段對話之後，或遊戲中的事件顯示對話時。
3. **公式：尚未決定。** 自由運算式或固定公式加可調數值，是一項待決任務（§5）。目標手感是勇者鬥惡龍：攻擊對防禦算出傷害並帶一點
   隨機幅度、會心一擊、閃避，以及由敏捷決定的出手順序。
4. **不需要每級一個能力。** 用一張簡單的表，每級給 N 個屬性點數，玩家在選單裡分配，按**確認**後存進存檔。**已完成**，見 §3.2。

## 3. 已完成

### 3.1 資料驅動的檔案（2026-10-24）

每個戰鬥檔案都有鍵（[docs/engine/12_RESOURCE_MANAGER.md](../../engine/zh_TW/12_RESOURCE_MANAGER.md) §4.3b）。武器粒子依等級從
`weapon_play.json` 的 `fx` 選出（`media/fx/battle.particle`）。

### 3.2 依等級給屬性點數

```jsonc
// stats.json
"levelUp": { "expPerLevel": 30, "atk": 2, "def": 1, "maxhp": 10, "maxLevel": 99,
             "points": [ { "from": 2, "points": 3 }, { "from": 20, "points": 4 }, { "from": 50, "points": 5 } ] }
```

- 每達到一級，給 `from` 不大於該等級、且最大的那一列的點數。
- 在狀態分頁，**＋ / −**（或 Enter）只在草稿中移動點數：該列顯示 `15 → 17`，還不算數。按**確認**才套用並立刻存檔；
  按**重設**、關閉選單或讀取存檔都會丟棄草稿。
- 細節：[03_PLAYER_MENU.md](03_PLAYER_MENU.md)（屬性）。
- **待決（平衡）：** 每級仍然會讓每個屬性 +1（stats.json 的 `perLevel`）。再加上點數，成長大約是原本的兩倍。若只想靠點數成長，
  把 `perLevel` 設為 0。

## 4. 計畫

### 4.1 所有人共用一個 `Combatant`

```jsonc
// enemies.json
"slime": {
  "stats":  { "hp": 24, "str": 4, "def": 2, "dex": 3, "agi": 2, "vit": 3, "int": 1, "luk": 1 },
  "weapon": "claw",            // 它的攻擊方式與節奏（weapon_play.json）
  "moves":  ["slime_split"],   // 特殊招式（moves.json）
  "look":   "slime",           // looks.json：狀態對應表（4.2）
  "ai":     "basic"
}
```

玩家也由存檔、等級和裝備組成同樣的 `Combatant`。戰鬥只處理戰鬥者，除了「誰讀取輸入」之外，從不問「這是不是玩家？」。

### 4.2 狀態對應表（"look"）：每個狀態的動畫、音效和粒子

```jsonc
// looks.json
"archer": {
  "anim": "anim/archer_bow.anim", "held": "anim/weapon_bow.anim",
  "states": {
    "idle":   { "clip": "idle" },
    "charge": { "clip": "charge", "sound": "bow_draw", "fx": "charge_aura" },
    "attack": { "clip": "attack", "sound": "bow_shot" },
    "hurt":   { "clip": "hurt",   "sound": "player_hurt", "fx": "hit_red" },
    "guard":  { "clip": "guard",  "sound": "shield" },
    "die":    { "clip": "die",    "sound": "player_down" }
  }
}
```

狀態來自戰鬥本身（4.3），不再由繪圖程式裡的 `if` 決定。武器依等級的 `fx`（3.1）會併入這張表。

### 4.3 戰鬥事件

```
BattleController（只管規則）
  發出：BattleStart  TurnStart{who}  AttackStart{who, target, move}  HitLanded{target, dmg, crit}
        Missed  Guarded  ComboPoint{n}  StateChanged{who, state}  Died{who}  BattleEnd{winner}
      │
      ▼
BattleEventBus ──► 呈現：looks.json[state] → 動畫 + 音效 + 粒子
      ▲        ──► 故事：「第 3 次攻擊後，顯示對話／交給玩家操作」（4.4）
      └── 動畫事件：「hit」影格告訴控制器傷害何時生效（取代 hit_number_delay_ms）
```

控制器從不播放音效或動畫。故事腳本聽同樣的事件，並送出自己的指令（暫停、顯示對話、交出操作、改變戰鬥者的 AI、結束戰鬥）。

### 4.4 誰對誰：戰鬥設定

```jsonc
// 在事件（事件編輯器）或關卡物件裡
"battle": {
  "left":  [ { "who": "npc:elder",     "ai": "basic" } ],
  "right": [ { "who": "monster:wolf" } ],
  "control": [
    { "when": "attacks:3",        "give": "left" },     // 第 3 次攻擊後，玩家操作長老
    { "when": "dialogue:ch2_cry", "give": "left" }       // 或在這段對話之後
  ]
}
```

沒有 `control` 時，沒有 `player` 參與的戰鬥從頭到尾都是觀看。不由玩家操作的每一方都跑自己的 `ai`。

### 4.5 參數與公式

戰鬥用到的所有數值都從 C++ 搬到 `battle.json`（或新的 `battle_rules.json`）：傷害、防禦、攻擊之間的等待（AGI）、
會心與閃避（LUK、DEX、AGI）、隨機幅度。形式尚未決定（§5）。

## 5. 待決任務

| # | 任務 | 說明 |
|---|---|---|
| O1 | **公式的形式** | 自由運算式（`"max(1, a.atk - t.def / 2) * rand(0.9, 1.1)"`，一個只認固定變數的小型求值器），或勇者鬥惡龍式的固定公式加可調數值。在 4.5 之前決定。 |
| O2 | 勇者鬥惡龍的手感 | 參考：傷害 ≈（ATK / 2 − DEF / 4），幅度 ±1/16；會心約 1/32 且無視防禦；閃避與順序看敏捷。要和量條及連擊一起檢查。 |
| O3 | `perLevel` 與點數 | 兩者都保留，或只靠點數（3.2） |
| O4 | 刪除 `combat.json` | 沒有程式讀它 |

## 6. 工作順序（提案）

1. **戰鬥事件（4.3）** 與 **狀態對應表（4.2）**：它們改變戰鬥的運作方式和畫面。先把呈現搬到事件上，規則維持不變。
2. **`Combatant`（4.1）**：怪物得到六個屬性（由現在的 hp/atk/def 換算，戰鬥結果不變）。
3. **戰鬥設定（4.4）**：怪物對怪物、NPC 對怪物、由故事交出操作。
4. **公式（4.5）**：等 O1 決定之後。
