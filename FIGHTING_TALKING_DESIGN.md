# 巫師之塔 (Tower of the Sorcerer) — 戰鬥與對話系統設計文件 (Fighting & Talking System Design)

> 配套：玩法說明書、技術建構文件、以及 `data/` 下的 JSON 內容。
> 本文件定義「自動回合拳戰（flight/punch scene）」與「對話（talking）系統」的資料結構與運作規則，供 Vulkan C++ 實作對齊。

---

## 1. 戰鬥系統（自動回合拳戰 / Flight Scene）

### 1.1 觸發
玩家移動到怪物格 → 進入 `CombatState`。無手動指令，雙方**自動輪流出拳**（flight scene / punch scene）。

### 1.2 傷害公式（確定式，無隨機）
```
dmgToTarget = max(1, attacker.atk - target.def)        // 每擊傷害
hitsToKill  = ceil(target.hp / dmgToTarget)            // 擊殺所需擊數
damageTaken = (hitsToKill - 1) * max(1, target.atk - attacker.def)  // 我方總承傷
```
- 玩家持續攻擊直到怪物死亡；期間怪物反擊 `(hitsToKill - 1)` 次。
- 若 `attacker.atk <= target.def`：每擊仍造成 1（避免卡死）。
- 若 `target.atk <= attacker.def`：攻擊方完全無傷。

### 1.3 一場戰鬥的演出（flight scene 時間軸）
| 時間 | 事件 | 畫面 |
|---|---|---|
| 0ms | 戰鬥開始，雙方拉近至拳戰框 | 玩家在左、敵人在右 |
| 每 700ms（round_time_ms） | 一回合：玩家出拳→敵人受擊閃紅；敵人出拳→玩家受擊閃紅 | 顯示雙方 HP 條 |
| 回合結束判定 | 任一方 HP<=0 → 結束 | 勝：敵人消散 + 取得 exp/gold；負：玩家回到本層起點 |

### 1.4 成長
- 擊敗敵人取得 `exp`；`exp` 累積達門檻 → 自動升級，`atk += 2`、`def += 1`（見 combat.json `player_base`）。
- 關卡上的 `gem_atk` / `gem_def` 立即 +atk / +def；`potion_*` 回復 HP。
- 這些皆為「確定式」，與玩法文件 §4 一致。

### 1.5 資料（combat.json 摘要）
```json
{
  "model": "auto_round",
  "round_time_ms": 700,
  "player_base": { "hp": 120, "atk": 12, "def": 4 },
  "formula": { "dmg_to_target": "max(1, attacker.atk - target.def)", ... }
}
```

---

## 2. 對話系統（Talking System）

### 2.1 資料結構（dialogue/*.json）
每個 NPC / 敵人一份對話樹：
```json
{
  "npcId": "sorcerer_teacher",
  "start": "root",
  "nodes": {
    "root": {
      "text": "戰鬥是自動的——你走向敵人便會輪流出拳。",
      "choices": [
        { "label": "那我該注意什麼？", "next": "tip" },
        { "label": "懂了。", "next": null }
      ]
    },
    "tip": { "text": "先吃攻擊寶石再打高防敵人。", "choices": [ { "label": "記住了。", "next": null } ] }
  }
}
```
- `start`：進入對話的起始節點。
- `nodes`：節點表；每個節點有 `text` 與 `choices[]`。
- `choice.next`：下一個節點 id；`null` = 結束對話。
- 可選 `requires`（數值門檻，決定選項是否出現）與 `action`（給物品／開門／設旗標）。

### 2.2 敵人對話
敵人也有 `enemy_*.json` 對話（開戰前的吆喝），選「（戰鬥）」即進入 CombatState。

### 2.3 運作
`TalkingSystem.start(npcId)` 載入對應 JSON → 顯示 `text` 與 `choices` → 玩家點選 → 若有 `action` 先執行 → 跳到 `next` 或結束。

---

## 3. 關卡系統（Stage System）與戰鬥的介接
- `StageSystem.load("stage_01")` 讀 `data/stages/stage01.json`，依 `tiles` 字元地圖 + `legend` 建立網格實體。
- 玩家走到 `monster:*` 格 → 依 `enemies.json` 取出該敵數值 → 進入 CombatState。
- 戰鬥結束勝利 → 套用 `exp/gold`、移除該格怪物、回傳主迴圈。
- 走到 `stairs_up/down` → 切換 `connect.up/down` 所指的關卡。
- 走到 `npc:*` → `TalkingSystem.start(...)`。

---

## 4. 數值平衡檢查清單（設計／測試）
- [ ] 任意路線都存在「可由公式驗證的可通關解」（先吃寶石再打怪）。
- [ ] 第 10 層 Boss（HP 400 / atk 34 / def 14）在玩家典型成長下可擊敗。
- [ ] 戰鬥結果完全由 atk/def/hp 決定，無隱藏隨機。
- [ ] 對話 `next` 鏈無懸空（指向不存在的節點）。

---

*戰鬥與對話皆由 `data/` JSON 驅動；C++ 端只讀取與演出，不改數值邏輯。*
