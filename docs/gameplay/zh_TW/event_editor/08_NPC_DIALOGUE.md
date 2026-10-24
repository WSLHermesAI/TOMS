# 08 — NPC 與綁定的事件（對話事件系統）

> 英文原文：[08_NPC_DIALOGUE.md](../../event_editor/08_NPC_DIALOGUE.md)

**狀態：** 2026-10-16 已實作：遊戲本身、資料格式、F02 村長的事件、測試，以及事件編輯器（交談事件的欄位、含交談預覽的 NPC 分頁、
對話分頁）。可重複的事件（`once: false`）與 `varChanged` 事件也在同一天加入（第 6.5、11.1 節）。專案負責人的決定在第 12 節。

本文件說明玩家與 NPC 交談時 NPC 如何反應，以及同樣的元件如何串起更大的故事（第 11 節）：
- 沒有特別要說的事時說什麼：依**交談索引**輪流播放的**閒聊台詞**；
- 事件如何**綁定**到 NPC，並以**話題**的形式提供給玩家，同時有多個啟用的話題時顯示選單；
- 事件的對話如何提供**選項**，以及每個選項如何改變遊戲：狀態、變數、道具或另一個事件；
- 事件完成之後，它的話題會怎樣。

## 1. 位置

| 部分 | 位置 |
|---|---|
| NPC 檔 | `assets/data/npcs/<npcId>.json`（第 3 節）：地圖上的六個 NPC 都有 |
| 交談事件 | 事件池檔 `assets/data/events/pool_*.json`：`"trigger": {"on": "talk", "npc": "<id>"}`，加上 `dialogue`、`afterDialogue`、`topicOrder`（第 6 節） |
| 對話 | `assets/data/dialogue/<id>.json`：有文字與選項的節點；選項帶有結果（第 6.2 節） |
| 事件變數 | `assets/data/story/vars.json`（事件編輯器的變數分頁；第 8 節） |
| 交談邏輯 | `src/core/game/systems/talk_system.h/.cpp`：NPC 檔、話題、閒聊輪替、是否顯示選單（純邏輯） |
| 結果 | `src/core/game/systems/story_actions.h/.cpp`：所有劇情位置共用的一份動作清單（第 7 節） |
| 遊戲端 | `Game::talkTo`、`openTopic`、`openTopicMenu`、`chooseDialogue`、`wakeEvent`、`runStoryActions`（`game_story.cpp`） |
| 條件 | `src/core/engine/condition.cpp`，新增 `varAtLeast`、`varEquals`、`talkCountAtLeast`、`onStage` |
| 測試 | `unit.talk_system_test`（邏輯、存檔，以及所有附帶的 NPC 檔、對話與動作）；`smoke.talk_menu`、`smoke.talk_chain`（遊戲） |

**交談方式：** 玩家走到 NPC 的格子上（與以前相同），或站在該格上按 Enter / 空白鍵。

## 2. 本文用語

| 用語 | 意思 |
|---|---|
| **NPC** | 地圖上的角色（樓層 legend 中的 `npc:<id>`）。它的行為由它的 **NPC 檔**決定 |
| **閒聊台詞** | NPC 平常的對話，在沒有事件可提供時使用。多段會輪流播放 |
| **交談索引** | 下一次播放哪一段閒聊台詞；每個 NPC 各一個，每次遊玩各自記錄 |
| **綁定的事件** | 觸發時機為「與這個 NPC 交談」的事件 |
| **話題** | 玩家看到的綁定事件：NPC 話題選單中的一列 |
| **事件對話** | 選擇話題時事件播放的對話；結果在它的選項中發生 |
| **結果** | 選項做的事：動作（改變狀態、設定變數、給予道具…），以及事件是否完成 |

## 3. NPC 檔

每個 NPC 一個檔案：`assets/data/npcs/<npcId>.json`。村長的檔案：

```json
{
  "npcId": "villager",
  "name": "npc.villager.name",
  "greeting": "npc.villager.greeting",
  "chat": {
    "lines": [
      { "dialogue": "villager_elder" },
      { "dialogue": "elder_chat_weather" },
      { "dialogue": "elder_chat_tower", "requires": { "type": "stageCleared", "stageId": "F05" } },
      { "dialogue": "elder_secret_well", "requires": { "type": "talkCountAtLeast", "npc": "villager", "value": 8 },
        "actions": [ { "type": "setVar", "var": "well_secret", "value": true } ] }
    ],
    "order": "loop"
  },
  "topics": { "menu": "auto" }
}
```

| 欄位 | 意思 | 預設 |
|---|---|---|
| `npcId` | 地圖 legend 中的 id（`npc:villager`）；檔名與它相同 | 必填 |
| `name` | 對話框上顯示的名字（文字鍵） | 無：不顯示名字 |
| `greeting` | NPC 在話題選單最上方說的一行台詞（文字鍵，第 5 節） | 無：選單只顯示名字 |
| `sprite` | 它在地圖上的精靈圖（遊戲圖集中的名稱），新的 NPC 因此不需要改程式 | 圖集中有 `npc_<id>` 就用它，否則用舊的對照表 |
| `chat.lines` | 依順序的閒聊台詞：對話檔 id、可選的 `requires`、可選的 `actions`（每次播放都執行） | 必填，至少一項 |
| `chat.order` | 下一段播放哪一句閒聊：`loop`、`stay`、`random`（第 4 節） | `loop` |
| `topics.menu` | 何時顯示話題選單：`auto`（第 5 節）或 `always` | `auto` |

- 已完成事件的話題如何處理，由每個事件自己設定（第 6.4 節）。
- **沒有 NPC 檔時：** 遊戲把 `assets/data/dialogue/<npcId>.json` 當作該 NPC 唯一的閒聊台詞。
- **公主**（在 `stage_11` 用不同的對話）改為兩段閒聊台詞，分別帶 `{"type": "onStage", "stageId": "stage_11"}` 與它的 `not`，
  不再是程式碼中的特例。
- 其他 NPC 保留原本的那一段對話：`sorcerer` → `sorcerer_teacher`、`king` → `king_lieutenant`…

## 4. 閒聊台詞與交談索引

每個 NPC 在每次遊玩中都有一個**交談索引**，從 0 開始。玩家與 NPC 交談且沒有話題可提供時（第 5 節），NPC 播放一段閒聊台詞：

1. 取出目前 `requires` 成立的台詞（沒有 `requires` 的台詞永遠成立），依檔案中的順序，稱為**可用的台詞**。
2. 播放第 `交談索引 mod 可用台詞數` 段，並把交談索引加 1。設為 `"order": "stay"` 時，索引停在最後一段，不會回到 0。

設為 `"order": "random"` 時，第 2 步改為隨機選一段可用的台詞；有兩段以上時，不會連續兩次選到同一段。

| 台詞 | 第 1 次 | 第 2 次 | 第 3 次 | 第 4 次 | 第 5 次 |
|---|---|---|---|---|---|
| 1 段（A） | A | A | A | A | A |
| 3 段，`loop` | A | B | C | A | B |
| 3 段，`stay` | A | B | C | C | C |
| 3 段，`random` | B | A | C | A | B（舉例） |

- **只有一段時：** 不論順序設定，永遠是第一段。
- **需要交談很多次才出現的隱藏訊息：** 帶有 `"requires": {"type": "talkCountAtLeast", "npc": "villager", "value": 8}`
  的閒聊台詞，在第 8 次交談之後加入輪替：條件看到的是*這次之前*的交談次數，所以最早在第 9 次播放。可以串成多段（8、15、25 次），
  讓線索慢慢出現。台詞的 `actions` 可以開啟其他內容，例如村長的秘密會設定 `well_secret`，讓《夜裡的井》出現第三個選項。
- **只有閒聊台詞會移動索引。** 談話題不會，所以下一次單純交談會接著上次的閒聊繼續。每次交談（包含談話題）都算進 `talkCountAtLeast`。
- **索引在台詞播放時移動**，所以提早離開對話也算。
- **之後才可用的台詞** 從那時起加入輪替，索引不會重設。
- 交談索引、交談次數與上一次隨機選到的台詞會隨這次遊玩儲存（第 8 節）。

## 5. 與 NPC 交談：完整流程

```
玩家走到 NPC 上（或站在它的格子上按 Enter）
        │
        ▼
收集這個 NPC 的話題：
  開放   = 尚未完成、requires 成立，且（若有其他事件在 next 中指名它）已被喚醒的綁定事件
  已完成 = 已完成且有 afterDialogue 的綁定事件（第 6.4 節）
        │
        ├─ 沒有開放也沒有已完成的話題 ──────────────► 播放下一段閒聊台詞（第 4 節）
        │
        ├─ 恰好 1 個開放、沒有已完成、選單為 auto ──► 直接播放該事件的對話
        │   （其他綁定事件都未啟用，或已完成且隱藏）
        │
        └─ 其他情況 ─────────────────────────────► 話題選單：
                                                     ┌────────────────────────────────┐
                                                     │ 村長                           │  ← 名字
                                                     │ 啊，又是你。……                │  ← 招呼語
                                                     │ ▶ 北邊的路                     │  ← 開放的話題
                                                     │   失蹤的帳本                   │
                                                     │   ✓ 夜裡的井                   │  ← 已完成，有完成後對話
                                                     │   隨便聊聊                     │  ← 播放閒聊台詞
                                                     │   離開                         │
                                                     └────────────────────────────────┘
```

（這個選單就是 `smoke.talk_chain` 的截圖：完成《夜裡的井》之後，它喚醒了《失蹤的帳本》。）

- **選單順序固定：** 先列開放的話題，再列已完成的話題，各自依 NPC 綁定事件的順序（事件池檔依名稱排序，再依檔案內的順序）。
  事件可以設定 `"topicOrder": <數字>` 來調整位置（數字小的在前；沒有設定的排在後面，依事件池順序）。每一列是事件的 `title` 文字。
- **未啟用的事件**（`requires` 尚未成立，或在等待被喚醒）**不會列出**。
- **喚醒：** 被其他事件在 `next`（或某個選項在 `fire`）中指名的事件，即使自己的 `requires` 成立，也要等到被喚醒。
  沒有被任何事件連結的事件，`requires` 一成立就開放。
- **招呼語** 是選單最上方的一行台詞。選單之前不播放閒聊台詞。
- **何時出現選單**（`auto`）：只要有兩個以上的話題（開放或已完成）就顯示。只有一個開放話題、沒有其他可列的話題時直接播放；
  一個開放話題加上一個已完成話題時顯示選單。
- **隨便聊聊** 播放下一段閒聊台詞並移動交談索引。**離開** 關閉選單。
- 事件對話關閉後，玩家回到地圖上，下次交談時重新建立清單。

**例子：村長綁定五個事件，列出三個、兩個不列出**（由 `talk_system_test` 檢查）。

| 事件 | `requires` | 目前 | 選單中 |
|---|---|---|---|
| `ev_elder_north_road` | 無；已完成，有 `afterDialogue` | 已完成 | ✓ 北邊的路 |
| `ev_elder_well_night` | 無 | 開放 | 夜裡的井 |
| `ev_elder_ledger` | `ev_elder_well_night` 已完成，且被它的 `next` 喚醒 | 未啟用 | 不列出 |
| `ev_elder_bell_rumour` | `insight ≥ 1`（鐘的事件鏈給的） | 開放 | 不會響的鐘 |
| `ev_elder_last_wish` | `elder_trust ≥ 3` 且 F05 已通過 | 未啟用 | 不列出 |

## 6. 事件對話：選項與結果

### 6.1 把事件綁定到 NPC

```json
{
  "eventId": "ev_elder_well_night",
  "kind": "whisper",
  "text": "ev_elder_well_night.text",
  "title": "ev_elder_well_night.title",
  "desc": "ev_elder_well_night.desc",
  "trigger": { "on": "talk", "npc": "villager" },
  "dialogue": "elder_well_night",
  "afterDialogue": "elder_well_night_after",
  "next": ["ev_elder_ledger"]
}
```

- `dialogue` 是 `assets/data/dialogue/` 中的檔案。沒有的話，事件以單頁顯示它的 `text`，只有 **確定**，按下即完成事件。
- 一個事件綁定一個 NPC；一個 NPC 可以有任意數量、來自任何事件池的事件。
- 交談事件不需要地圖上的格子，也不必列在任何樓層的 `events` 清單中。它的 NPC 必須站在某一層樓上。

### 6.2 對話檔：起點、選項與結果

```json
{
  "npcId": "villager",
  "start": "root",
  "nodes": {
    "root": {
      "text": { "zh_TW": "每到夜裡，老井裡就有東西在哭。", "en": "Something cries in the old well at night." },
      "choices": [
        { "label": { "zh_TW": "它怕光。我帶火把下去。", "en": "It fears light. I'll take a torch." },
          "id": "secret", "requires": { "type": "varEquals", "var": "well_secret", "value": true },
          "actions": [ { "type": "addStat", "stat": "str", "delta": 2 }, { "type": "setVar", "var": "well_helped", "value": true },
                       { "type": "addVar", "var": "elder_trust", "delta": 2 } ],
          "finish": true, "next": "secret_done" },
        { "label": { "zh_TW": "我下去看看。" }, "id": "help", "requires": { "type": "statAtLeast", "stat": "hp", "value": 50 },
          "actions": [ { "type": "addStat", "stat": "atk", "delta": 2 }, { "type": "setVar", "var": "well_helped", "value": true } ],
          "finish": true, "next": "help_done" },
        { "label": { "zh_TW": "先付錢。" }, "id": "pay",
          "actions": [ { "type": "addStat", "stat": "gold", "delta": 30 }, { "type": "addCounter", "counter": "humanity", "delta": -1 } ],
          "finish": true, "next": "pay_done" },
        { "label": { "zh_TW": "現在不行。" }, "next": null }
      ]
    }
  }
}
```

| 欄位 | 意思 |
|---|---|
| `start` | 第一個節點，或一個清單 `[{"node": "pay", "requires": {...}}, {"node": "thanks"}]`：取第一個 `requires` 成立的（最後一項應不帶條件） |
| `text` | 每種語言一個字串（至少 zh_TW 與 en；其他語言退回英文） |
| `label` | 選項的文字 |
| `requires` | 條件不成立時隱藏選項 |
| `actions` | 結果，選擇時依序執行（第 7 節）。舊的單一 `action` 仍可使用 |
| `finish` | `true`：選擇它就完成這段對話的事件（第 6.3 節） |
| `fire` | 立即喚醒的事件 id（只屬於這個選項的 `next`） |
| `id` | 記錄為 `choiceMade {eventId, id}`，供之後的條件使用 |
| `next` | 下一個節點；`null` 或 `""` 關閉對話 |
| `finishOnClose` | （最上層）不論對話如何關閉，都完成事件 |

- **不同選項，不同結果：** 每個選項有自己的 `actions` 與 `fire`。上例中，帶火把提升力量與信任，爬下去提升攻擊，要錢會得到金幣、
  降低人性，*現在不行* 關閉對話但不完成事件，所以下次話題仍在。
- **記得的選擇：** 之後的對話與事件可以查詢 `{"type": "choiceMade", "choiceId": "ev_elder_well_night", "optionId": "pay"}`；
  完成後對話就用它選擇第一個節點。

### 6.3 執行事件

選擇話題時：

1. 事件**開始**（玩家的事件紀錄將它列為進行中）。
2. 播放它的對話；每個被選的選項先執行它的 `actions`，再喚醒它的 `fire` 事件。
3. 選擇 `"finish": true` 的選項時事件**完成**：執行事件紀錄本身的 `actions`，設定 `event_<id>`，並喚醒它的 `next` 事件（第 11.1 節）。
4. 對話在沒有選擇完成選項的情況下關閉時，不會有其他變化；話題仍然開放。

### 6.4 事件完成之後

| 事件紀錄 | 完成之後的話題 | 選擇它時 |
|---|---|---|
| 沒有 `afterDialogue` | 消失 | — |
| `"afterDialogue": "elder_well_night_after"` | 留在選單中，標示 ✓ | 播放該對話 |

- 完成後對話是獨立的對話檔：簡短，並可用條件式的 `start` 回應玩家當初的選擇（村長感謝幫忙的人，揶揄要錢的人）。
- 其中的 `finish` 與 `fire` 會被忽略。它不應給予任何東西：玩家可以重複播放，所以完成後對話或閒聊台詞中有 `addStat`、
  `setStat` 或 `give` 時，測試會失敗。

### 6.5 可重複的事件

事件紀錄上的 `"once": false` 讓它可以重複；`"maxTimes": N` 限制次數（0 或不設 = 不限）：

```json
{ "eventId": "ev_elder_potion", "trigger": { "on": "talk", "npc": "villager" },
  "requires": { "type": "runFlagSet", "flag": "event_ev_elder_last_wish" },
  "actions": [ { "type": "give", "itemId": "potion_red" } ], "once": false, "maxTimes": 3, "dialogue": "elder_potion" }
```

- **每一次** 都會再產生事件的結果：交談話題完成後仍然開放（每次都執行對話的結果與紀錄的 `actions`），格子事件留在格子上（再撞一次），
  `chain` / `varChanged` 事件再次觸發時再執行一次。
- **只有第一次** 會完成它：設定 `event_<id>`、列入事件紀錄、喚醒它的 `next` 事件。
- **次數用完**（執行了 `maxTimes` 次）：與已完成的一次性事件相同：話題隱藏或顯示完成後對話，格子消失。
- 次數隨這次遊玩儲存（`eventCounts`）。村長的買藥話題以每瓶 20 金幣賣出三瓶藥（`smoke.talk_repeat`）。

## 7. 動作（結果）

事件紀錄、對話選項與閒聊台詞共用同一份清單（`story_actions.h`）。結果是開放的：任意組合、任意順序。

| 動作 | 作用 |
|---|---|
| `{"type": "addStat", "stat": "str", "delta": 2}` | 依名稱增加玩家狀態：`hp`、`maxhp`、`atk`、`def`、`gold`、`exp`（含升級）、`lv`，以及 `stats.json` 的屬性（`str`、`dex`、`agi`、`vit`、`int`、`luk`）。負數為扣除（不低於 0；HP 不低於 1）。顯示「STR +2」 |
| `{"type": "setStat", "stat": "hp", "value": 100}` | 設定狀態 |
| `{"type": "give", "itemId": "gem_def", "count": 1}` | 給予道具（「獲得：…」） |
| `{"type": "take", "itemId": "potion_red", "count": 1}` | 拿走道具（交易）；數量不足時失敗且不改變任何東西，所以選項應以 `itemHeld` 作為 `requires` |
| `{"type": "setVar", "var": "well_helped", "value": true}` | 設定事件變數（`true` / `false` 為 1 / 0） |
| `{"type": "addVar", "var": "elder_trust", "delta": 1}` | 增加事件變數（限制在它的範圍內） |
| `{"type": "setFlag", "flag": "flag_x"}` | 設定本次遊玩的旗標 |
| `{"type": "setStoryFlag", "flag": "..."}` | 設定劇情旗標（跨遊玩保留） |
| `{"type": "addCounter", "counter": "insight", "delta": 1}` | 劇情計數 |
| `{"type": "fireEvent", "eventId": "ev_x"}` | 喚醒另一個事件（與 `fire` 相同） |
| （任何改變了數值的 `setVar` / `addVar`） | 同時執行該變數上的 `varChanged` 事件（第 11.1 節） |
| `{"type": "message", "text": "text.key"}` | 一行通知 |
| `startMission`、`claimMission`、`makeChoice`、`unlockSkill`、`setSideStoryState`、`enterBattle` | 與以前相同 |

失敗的動作（不存在的狀態、道具不足）會記錄為 `[story] action failed`，且不改變任何東西；`talk_system_test` 會在附帶資料中
事先擋下這類動作。

## 8. 變數、條件與儲存的狀態

- **事件變數** 存在 `assets/data/story/vars.json`，也就是事件編輯器變數分頁寫入的檔案：

  ```json
  {
    "elder_trust": { "type": "int", "default": 0, "scope": "run", "min": 0, "max": 5, "category": "elder" },
    "well_helped": { "type": "bool", "default": false, "scope": "run", "category": "elder" },
    "_categories": ["elder"]
  }
  ```

  `bool` 為 0 / 1；`min` / `max` 限制每一次變更。未宣告的變數讀到 0。
- **條件：** `{"type": "varEquals", "var": "well_helped", "value": true}`、
  `{"type": "varAtLeast", "var": "elder_trust", "value": 3}`、`{"type": "talkCountAtLeast", "npc": "villager", "value": 8}`、
  `{"type": "onStage", "stageId": "stage_11"}`，以及所有原有的條件（`runFlagSet "event_<id>"` = 該事件已完成、`counterAtLeast`、
  `choiceMade`、`stageCleared`、`itemHeld`、`statAtLeast`——現在也能讀 `maxhp` 與屬性——…），可用 `all` / `any` / `not` 組合。
- **隨本次遊玩儲存**（與 `eventsStarted` / `eventsFinished` 並列；舊存檔沒有這些欄位也能讀取）：

  ```json
  "vars": { "well_helped": 1, "elder_trust": 2 },
  "npcTalk": { "villager": [2, 7, -1] }
  ```

  `npcTalk` 為 [交談索引, 交談次數, 上一次隨機選到的台詞]。新的遊玩中每個變數從 `default` 開始，交談全部歸 0。
  劇情旗標（`setStoryFlag`）與 meta 存檔不受影響。

## 9. 編輯器

全部在網頁事件編輯器中（`tools/web_editors/event_editor.html`、`WebEditors.cmd`）。只有在 **Open project folder…** 之後按 **儲存**
才會寫入；每個變更的檔案會先備份到 `Build/editor_backups/`。

**事件表單**（邏輯）：
- 交談事件：**對話**（檔案不存在時 ⛔）、**完成之後**（隱藏話題，或選一個對話檔）、**在哪一層交談**（NPC 所在的任何一層，或其中一層：
  它會新增或移除 `onStage` 條件，並保留其他條件）、**選單順序**（`topicOrder`）；
- 任何事件：**重複**——*可重複*、*最多 N 次*（第 6.5 節）；`varChanged` 使用 *觸發時機 → 這個變數改變時*；
- 動作：新增 *拿走道具* 與 *狀態加上*（狀態與屬性）。

**NPC 分頁：**
- 每個 NPC id（來自 NPC 檔、地圖與交談事件）；⚠ = 還沒有 NPC 檔，可按 **建立 NPC 檔**，**+ NPC** 新增一個；
- 它的檔案：名字與招呼語（zh_TW、en）、地圖精靈圖（圖集中的名稱）、放置的樓層、話題選單規則；
- 閒聊台詞：對話、▲ / ▼、✕、*只在…時*（條件列）、*播放時執行*（動作）、台詞的開頭，以及順序（loop / stay / random）；
- 依選單順序排列的話題，含在哪一層交談、條件、完成後對話與重複設定；**▲ / ▼** 重新編號 `topicOrder`；
- **交談預覽：** 選擇在哪一層交談（一個 NPC 可能站在好幾層），勾選已完成的事件，設定話題會讀取的變數、計數與已通過的樓層，按 **💬 交談**：每次交談顯示遊戲會顯示的內容
  （閒聊台詞與它的開頭、直接播放一個話題，或選單）。話題上的 *完成* 假設玩家完成了它：喚醒它的 `next` 事件並改變它的變數。
  使用遊戲的規則（`talk_system.cpp`），並顯示交談次數與閒聊索引。

**對話分頁：** 以卡片顯示一個對話檔，每個節點一張：
- *使用者*（話題、完成後對話、閒聊台詞）、*起點* 為某個節點或 **依條件**（每列一個節點加它的條件列）、*不論如何關閉都完成事件*；
- 每個節點：重新命名、zh_TW 與 en 文字（其他語言保留）、刪除；**+ 節點**；
- 每個選項：文字（zh_TW、en）、*接著*（某個節點，或關閉）、*完成事件*、`id`、*只在…時*（條件列）、*然後執行*（動作列；不常見的動作以標籤保留）、
  *喚醒*（事件）、▲ / ▼ / ✕；**+ 選項**；
- **+ 對話** 建立新檔案。問題：*對話：缺少節點*（`start` 或 `next` 指向檔案中沒有的節點）。

**條件**（事件、閒聊台詞、選項、起點列）在每個地方都以 **條件列** 編輯，每列一個條件：

| 對象 | 運算 | 條件 |
|---|---|---|
| 變數 | ≥ n、= n | `varAtLeast`、`varEquals` |
| 計數 | ≥ n | `counterAtLeast` |
| 旗標 | 已設定、未設定 | `runFlagSet` |
| 事件 | 已完成、未完成 | `runFlagSet "event_<id>"` |
| 與某個 NPC 的交談次數 | ≥ n | `talkCountAtLeast` |
| 狀態或屬性 | ≥ n | `statAtLeast` |
| 樓層 | 已通過、未通過 | `stageCleared` |
| 道具 | 持有 ≥ n | `itemHeld` |
| 目前所在的關卡 | 是、不是 | `onStage` |
| 在某事件對話中做過的選擇 | 選了 `id`（會列出該對話的選項 id） | `choiceMade` |

兩列以上以 **以下全部成立** 或 **以下任一成立** 結合；*未…* 會寫成 `{"not": …}`。條件列無法表示的條件（巢狀的 `all` / `any` / `not`）
保留為 JSON，**{ }** 可把任何條件切換成 JSON（JSON 無效時變紅並保留原值）。執行測試時，`talk_system_test` 會再檢查一次每個 NPC 檔、對話與動作。

## 10. 程式碼與測試

| 檔案 | 內容 |
|---|---|
| `talk_system.h/.cpp` | `parseNpc`、`collectTopics`（依 NPC 分組的交談事件、選單順序、被 `next` 喚醒）、`menuTopics`、`nextChatLine`、`planTalk`、`startNode`。不依賴 Game：`TalkQuery` 回答條件、事件與亂數 |
| `story_actions.h/.cpp` | `runActions`（透過遊戲實作的 `StoryEffects`）、`checkActions`（依名稱目錄檢查資料）、`grantsReward` |
| `run_state.h/.cpp`、`save_system.*` | 事件變數（`var`、`setVar`、`addVar`、宣告的範圍）與 `npcTalk`，可存檔與讀檔 |
| `game_story.cpp` | `talkTo`、`playChatLine`、`openTopic`、`openTopicMenu`、`chooseDialogue`、`endDialogue`、`finishTalkEvent`、`wakeEvent`、`runStoryActions`、`loadNpcs` |
| `game_input.cpp` | 走到 NPC 上與在其格子上按 Enter 都呼叫 `talkTo`；格子事件透過 `runStoryActions` 執行動作；`finishEvent` 喚醒 `next` |
| `dialogue.rml/.rcss`、`UiDialogue::speaker` | 對話框上的名字列 |

| 測試 | 檢查 |
|---|---|
| `unit.talk_system_test`（154 項） | 輪替（1 段、loop、stay、random、之後加入的台詞、交談次數）；話題（開放、未啟用、被喚醒、已完成且有無完成後對話、`topicOrder`、何時出現選單）；條件式起點；四個新條件；每個動作與其失敗；`checkActions`；存檔與讀檔；所有附帶的 NPC 檔、交談事件、對話與動作；村長的故事從開始到最後的心願 |
| `smoke.talk_menu` | F02：走到村長上，顯示他的名字、招呼語與兩個話題 |
| `smoke.talk_chain` | 《夜裡的井》→「我下去看看」（攻擊 +2）→ 在他的格子上按 Enter：帳本被喚醒，井列為已完成 |
| `smoke.talk_trusted` | 北邊的路、井與帳本讓信任達到 3：`varChanged` 事件的通知《村長信任你了》 |
| `smoke.talk_repeat` | 買藥三次（每次金幣 −20、得到一瓶藥），之後選單中不再出現 |

## 11. 串起更大的故事：可重複使用的模型

以上的一切都由少數幾種元件組成，更大的故事就是把同樣的元件連起來。沒有專為某個故事寫的程式碼：村長的五個事件只是資料。

### 11.1 元件

| 元件 | 是什麼 | 如何連到其他元件 |
|---|---|---|
| **事件** | 一個劇情步驟：id、文字、`requires`、`actions`、`next` | 在格子上觸發（`step`）、由 NPC 觸發（`talk`），或一被喚醒且 `requires` 成立就執行（`chain`） |
| **條件** | 事件、選項、閒聊台詞與 `start` 清單上的 `requires` | 讀取已完成的事件、變數、計數、旗標、做過的選擇、交談次數、樓層、道具、狀態 |
| **動作** | 一個結果：狀態、道具、變數、計數、旗標、喚醒事件 | 在事件紀錄、選項或閒聊台詞上執行 |
| **連結** | `next`（完成時）、`fire`（某個選項）、`fireEvent`（動作） | **喚醒**事件：交談事件成為話題，格子事件在格子上等待，`chain` 事件若 `requires` 成立就立即執行 |
| **NPC** | 閒聊台詞 + 綁定的事件 | 它的話題隨事件的條件出現與消失 |
| **對話** | 節點與選項 | 每個選項是一條分支，有自己的結果；`start` 依條件選擇節點 |

**chain** 事件（`"trigger": {"on": "chain"}`）沒有格子也沒有 NPC：它是一個邏輯步驟，例如「兩條線索都找到時，門就打開」——
它在被喚醒且 `requires` 成立的那一刻執行動作，再喚醒自己的 `next`。喚醒最多連續 32 層，所以資料中的迴圈不會讓遊戲卡住。

**varChanged** 事件（`"trigger": {"on": "varChanged", "var": "elder_trust"}`）在某個動作把該變數改成新的值、且 `requires` 成立時執行：
「信任達到 3 時，村長信任你了」。它自己的動作改變的變數可以再觸發其他事件，最多 16 層。

這兩種事件都沒有格子也沒有 NPC，所以執行時 **它的標題會以通知顯示**（與它的動作的通知並列，例如「ATK +2」）；
`smoke.talk_trusted` 顯示《村長信任你了》。

### 11.2 模式

| 模式 | 做法 |
|---|---|
| **順序** A → B → C | A 的 `next: ["B"]`；B 的 `requires: event_A 已完成`（編輯器的 *只在…時啟用*） |
| **依選擇分支** | 不同選項 `fire` 不同事件，或設定之後的事件會讀取的變數 |
| **匯合**（兩者都要） | 事件 `requires` `all` 兩個已完成的事件；兩者都在 `next` 中指名它（鐘事件鏈的 `ev_bell_rings`） |
| **門檻** | 跨 NPC 的許多選項 `addVar`，一個事件 `requires varAtLeast`（村長的信任 → 《最後的心願》） |
| **記得的選擇** | 選項的 `id`；之後在條件、`start` 清單與完成後對話中使用 `choiceMade` |
| **慢慢出現的線索** | 以 `talkCountAtLeast` 控制的閒聊台詞；它的動作設定話題選項所需的變數 |
| **跨 NPC** | 完成 NPC A 的話題，喚醒綁定在 NPC B 的交談事件 |
| **跨樓層** | F03 的格子事件喚醒 F02 某個 NPC 的交談事件，或反過來；專屬鑰匙門打開通往下一步的房間 |
| **後果** | 選項上的狀態、道具或計數變化（`addStat`、`give`、`addCounter humanity -1`） |

### 11.3 村長的故事是一張圖

```
 F03 ev_bell_cracked ─► F05 ev_bell_ringer_key（給鑰匙）─► F04 專屬鑰匙門 ─► ev_bell_ringer_room
        ─► F06 ev_bell_rope ┐
        ─► F02 ev_bell_clapper ┴─► F03 ev_bell_rings（insight +1）────────────────┐
                                                                                   ▼ insight ≥ 1
 F02 村長：ev_elder_north_road（信任 +1）          ev_elder_bell_rumour（防禦 +1、信任 +1 | 執念 +1）
           ev_elder_well_night ──next──► ev_elder_ledger（若 well_helped：信任 +1 + 寶石）
             ├ 火把（需要 well_secret ◄── 交談 8 次後的閒聊台詞）：力量 +2、信任 +2
             ├ 爬下去：攻擊 +2、信任 +1
             └ 要錢：金幣 +30、人性 −1 ──► 帳本找不回來；完成後對話揶揄你
             信任達到 3 ──► ev_elder_trusted（varChanged：通知、人性 +1）
                        信任 ≥ 3 且 F05 已通過 ──► ev_elder_last_wish（寶石、最大 HP +20）
                                                        └─► ev_elder_potion（可重複 ×3：20 金幣 → 一瓶藥）
```

兩條事件鏈、好幾層樓的格子事件與一個 NPC 的話題，透過一個計數（`insight`）與一個變數（`elder_trust`）相連，沒有任何程式碼知道它們。

### 11.4 撰寫新的故事

1. **先畫出圖**（如上）：有哪些步驟、各自在哪裡或由誰觸發、彼此如何連結。
2. **變數：** 在事件編輯器的變數分頁新增（`story/vars.json`），並設定範圍。
   遊玩之前，可以用 NPC 分頁的 **交談預覽** 試試話題的順序與條件。
3. **事件：** 在事件編輯器中設定：類型、文字（標題、描述、內文；zh_TW 與 en）、*觸發時機*、*只在…時啟用*、*然後執行*、*接著觸發*；
   交談事件還要設定 **對話**、**完成之後** 與 **選單順序**。
4. **對話：** 事件編輯器的對話分頁（**+ 對話**）：每頁一個節點、每條分支一個選項、結束事件的選項勾選 *完成事件*、
   之後會被查詢的選項加上 `id`。
5. **NPC：** NPC 分頁（**+ NPC**：名字、招呼語、精靈圖、閒聊台詞），並放在某層樓上（關卡編輯器）。
6. **檢查：** `tools\test.cmd` 會為這些檔案執行 `talk_system_test`（以及截圖測試）。它會指出每個錯誤的引用；事件編輯器的問題分頁
   對事件顯示相同的內容。
7. **實際遊玩：** `toms_game --goto=<樓層> --events-done=<ids> --give=<道具>` 可以從故事的任何一點開始。

## 12. 決定（專案負責人，2026-10-16）

| # | 問題 | 決定 |
|---|---|---|
| 1 | 未啟用的事件是否列在選單中 | **隱藏** |
| 2 | 只有一個開放話題 | 其他綁定事件都未啟用（或已完成且隱藏）時 **直接播放**。若仍有已完成的事件列在選單中，**顯示選單** 讓玩家選擇 |
| 3 | 已完成的話題 | **依事件設定：** 隱藏，或保留並播放 **完成後對話**（道謝，或再說一次玩家有多勇敢），在編輯器中指定（第 6.4 節） |
| 4 | 選單順序 | **固定順序**（事件池順序，可用 `topicOrder` 調整；第 5 節） |
| 5 | 交談索引 | 預設 `loop`，另有 `stay` 與 **`random`**；需要交談很多次才出現的隱藏訊息，在閒聊台詞上使用 `talkCountAtLeast`（第 4 節）。談話題不移動索引，但算一次交談 |
| 6 | 「增加 STR」 | **結果是開放的：** 依名稱改變任何玩家狀態（`addStat` / `setStat`，包含 STR）、贈送道具（`give`）、變數（`setVar` / `addVar`），可任意組合（第 7 節） |
| 7 | 選單之前是否播放閒聊 | **不播放：** 選單有可選的一行 `greeting` 招呼語 |
| 8 | 第一個 NPC | **村民**（F02 的村長），以五個事件作為真正的測試資料 |
| 9 | 編輯對話 | **卡片：** 每個節點一張，含它的選項（第 9 節） |
| 10 | 可重複的事件 | **每次都執行，可選上限**（`maxTimes`）；`next` 只在第一次喚醒（第 6.5 節） |
| 11 | 沒有格子也沒有 NPC 的事件執行時 | **以通知顯示它的標題**（第 11.1 節） |
