> 英文原文：[08_TEST_SCENARIOS.md](../08_TEST_SCENARIOS.md)

# 08 — 測試情境：用設定檔直接開始任何場景

**情境（scenario）**讓遊戲依設定檔直接開始在某一個場景，所以遊戲的每個部分都能單獨測試：一場戰鬥、一段對話、商店、存檔
當下的某一層樓。不必從標題畫面一路走過迷宮。每個情境同時也是一個自動測試。

```
toms_game --scenario=tests/scenarios/battle_slime          # 玩這個情境（資料夾代表其中的 scenario.json）
toms_game --scenario=tests/scenarios/battle_slime --scenario-test   # 執行它的測試：它的畫格與按鍵，然後檢查
toms_game --arena=slime,bat:wand                           # 武器測試場：依序和這些敵人戰鬥，使用魔杖
tools\test.cmd -L scenario                                  # 所有情境測試
tools\test.cmd -R scenario.store_buy                        # 單一個
```

**在 `toms_editor` 中：**「情境」停駐視窗（和「關卡」並列的分頁）列出 `tests/scenarios/` 中的每個情境與它的場景。**按兩下就
在「遊玩」分頁中開始**：一場新遊戲，直接進入那個場景。`_comment` 是它的提示文字。在情境上按右鍵可以遊玩、開啟它的資料夾或
編輯它的 `scenario.json`；在任何地方按右鍵可以重新載入清單。
- **檔案 > 開啟情境資料夾...** 開始單一情境資料夾，或把整個資料夾的情境列成另一組。
- 下載的情境（`tests/scenarios/_cloud/`，[09](09_DOWNLOADS_AND_DLC.md)）自成一組。
- `toms_editor --scenario=<資料夾>` 讓編輯器啟動時就進入情境。把情境資料夾（或它的 `scenario.json`）拖到 `toms_editor.exe`
  上也一樣。

情境**從不寫入存檔**：它的遊戲沒有存檔欄位，作為起點的存檔是從情境資料夾讀取的。

## 1. 資料夾

每個情境是 `tests/scenarios/` 底下的一個資料夾，放 `scenario.json` 以及它用到的其他檔案：存檔、對話、關卡。新資料夾在下次
CMake configure 之後就成為新的測試 `scenario.<資料夾名稱>`。多個情境共用的檔案放在 `tests/scenarios/_shared/`，情境用
`"include"` 引入。

```
tests/scenarios/
  _shared/mid_game.json         共用："save": "save_mid_game.json"
  _shared/save_mid_game.json    一個存檔（遊戲本身的存檔格式）
  stage_from_save/scenario.json
  stage_from_save/save.json     這個情境自己的存檔
  dialogue_from_file/scenario.json
  dialogue_from_file/dialogue.json
  ...
```

## 2. 設定檔

除了 `scene` 之外都可以省略。遊戲依以下順序套用各欄位：

```json
{
  "_comment": "這個情境顯示什麼",
  "include": ["../_shared/mid_game.json"],
  "scene": "battle",
  "save": "save.json",
  "stage": "F04",
  "stairLocks": [ { "floor": "F01", "event": "ev_village_well" } ],
  "flags": ["hub.village"],
  "eventsDone": ["ev_elder_north_road"],
  "player": { "x": 3, "y": 5, "hp": 200, "maxhp": 300, "atk": 30, "def": 20, "gold": 500, "exp": 0, "lv": 8,
              "keys": { "yellow": 2, "blue": 1, "red": 0 }, "attrs": { "str": 3, "agi": 2 }, "attrPoints": 3,
              "inventory": ["potion_red"], "give": ["wand"], "equip": ["wand"] },
  "battle": { "enemy": "slime", "hp": 300 },
  "test": { "frames": 60, "keys": "enter@20" },
  "expect": { "scene": "store", "inventoryHas": ["potion_red"] }
}
```

| 欄位 | 作用 |
|---|---|
| `include` | 先讀取的其他設定檔。這個檔案的欄位蓋在上面：物件逐一合併鍵值，其他的直接取代。被引入的檔案也可以再引入別的。 |
| `scene` | 要開啟的場景（第 3 節）。 |
| `save` | 從這個存檔開始，使用遊戲的存檔格式（複製 `save/slotN.json` 即可）。沒有就是新遊戲。 |
| `stage` | 前往這個樓層（`F04`）、關卡 id（`stage02`）或關卡檔（`my_floor.stage.json`，放在設定檔旁邊）。 |
| `stairLocks` | 把某層第一個上樓梯鎖在事件之後：`{ "floor": "F01", "event": "ev_a" }`，或多個事件用 `"events": [...]`（全部完成才打開，W38）。 |
| `flags` | 要設定的流程旗標（例如 `hub.village` 解鎖村莊）。 |
| `eventsDone` | 像玩家一樣完成的事件。它們的獎勵、解鎖與通知都會發生。 |
| `player` | 新遊戲開始時裝備著弓（初始武器）；`"unarmed": true` 會卸下它。接著 `give` 與 `equip`（依 `items.json` 的 id 給道具與裝備；`give` 中也可以用 `gold:<n>`、`exp:<n>`、`points:<n>`），再套用給定的數值。`inventory` 會取代背包內容。`weaponUses`（`{ "<武器 id>": <勝場> }`）設定每把武器的勝場，也就是它的等級（[07](07_GAMEPLAY_PLAN.md) 第 13 節）。 |
| `test` | 自動測試用：場景開啟後執行幾個畫格，以及要按的鍵（`鍵@畫格`，從場景的第一個畫格算起；鍵名和 `--keys` 相同），以及 `clicks`：在設計座標上按滑鼠左鍵（`x:y@畫格`，1024 x 768，同 `--clicks`；一次點擊先懸停 2 畫格、按下 2 畫格，再放開）。 |
| `expect` | 測試結束時檢查什麼（第 4 節）。沒有的話只檢查場景是否正在顯示。 |

設定檔中的檔名是相對於**該檔案**本身。共用檔案指定的存檔，會在共用檔案旁邊尋找。

## 3. 場景

| `scene` | 開啟的畫面 | 專屬欄位 | 範例資料夾 |
|---|---|---|---|
| `title` | 遊戲啟動時的標題畫面 | — | `title` |
| `stage` | 行走場景：玩家在樓層中，處於存檔或給定的時刻 | — | `stage_new_game`、`stage_from_save`、`stage_from_events` |
| `battle` | 與 `enemies.json` 中某個敵人的戰鬥（敵人不在地圖上） | `battle`：`enemy`，可選擇覆寫 `hp`、`atk`、`def`、`atk_interval_ms`、`moves`、`look`、`break`、`events`；`party`：最多再兩隻（id，或 `{ "enemy": id, ...覆寫 }`）；`pace`：`cooldown` / `stamina`；`play`：攻擊的玩法，`track`（情境中的預設）、`bow`、`slice`、`circles` 或 `weapon`（身上武器自己的玩法，`weapon_play.json` 的 `"play"`；[15](15_BATTLE_REVAMP.md) §3.3）；`rules`：這場戰鬥用的 `data/battle.json` 數值（`{ "stamina": {...}, "break": {...} }`）（[13](13_BATTLE_EDITOR.md)） | `battle_slime`、`battle_strong_player`、`battle_bow`（弓的戰鬥片段；另有 `battle_bow_charge`、`battle_bow_idle`）；`battle_party`、`battle_party_target`（三隻怪物）；`battle_parry`、`battle_jabs`、`battle_spell_break`、`battle_break`、`battle_transform`、`battle_stamina`、`battle_stamina_short`、`battle_attack_in_combo`、`battle_combo_holds`（戰鬥規則）；`battle_guard_early`、`battle_guard_bar`、`battle_result`（改版：防禦的時機、結算的評等）；`battle_play_bow`、`battle_play_slice`、`battle_play_circles`（每把武器自己的玩法，由一次點擊開始） |
| `battleWon` | 那場戰鬥之後的勝利畫面 | 同 `battle` | `battle_won` |
| `arena` | **武器測試場**（[07](07_GAMEPLAY_PLAN.md) 第 0 階段）：依序和列出的敵人一場接一場地戰鬥，每場都是滿血，沒有獎勵，也不影響故事。勝利後按 Enter 開始下一場；落敗後自動開始。武器是玩家的：初始的弓，用 `player.equip` 換別的，用 `player.unarmed` 則空手。`toms_game --arena=<敵人,...>[:<武器>]` 不用檔案也能開啟（沒寫武器：空手） | `battle`：`enemies`（或 `enemy`），以及和 `battle` 相同的覆寫值，套用到每一場 | `arena_unarmed`、`arena_wand`、`arena_restart` |
| `weaponTrial` | **開場故事**（[07](07_GAMEPLAY_PLAN.md) 第 15 節）：老武師的對話（`dialogue/weapon_master.json`），就像標題畫面的新遊戲開啟它一樣；它的選項在試用戰鬥（武器測試場）中試用武器並留下一把 | — | `trial_start`、`trial_try_sword`、`trial_choose_staff` |
| `miniGame` | **獎勵事件**（[07](07_GAMEPLAY_PLAN.md) 第 16 節）：`"miniGame": "<data/minigames.json 中的 id>"` 開啟它；Enter 開始並點擊各點 | — | `minigame_chest`、`minigame_crystal` |
| `dialogue` | 一段對話：遊戲內的（`id`），或設定檔旁邊的檔案（`file`）；可用 `node` 指定起始節點 | `dialogue`：`id` 或 `file`、`node` | `dialogue_by_id`、`dialogue_from_file` |
| `talk` | 和 NPC 交談：依已完成的事件出現閒聊、話題或話題選單 | `talk`：`npc`（`npcs/` 的 id） | `talk_elder` |
| `store` | 商店（情境中直接解鎖） | — | `store_buy`、`store_from_save` |
| `menu` | 玩家選單的某個分頁 | `menu`：`tab` = `status`、`gear`、`items`、`events`、`cards` 或 `system` | `menu_status` … `menu_system`、`menu_cards`、`events_stairs_where` |
| `saveLoad` | 存檔／讀檔畫面 | `saveLoad`：`mode` = `save` 或 `load` | `save_load` |
| `stageSelect` | 關卡選擇（已到過的樓層） | — | `stage_select` |
| `stairsConfirm` | 樓梯上的「要上樓／下樓嗎？」詢問 | `stairs`：`to`（樓層）、`up`（true／false） | `stairs_confirm` |
| `notice` | 由設定產生的通知，例如鎖住的樓梯的事件完成後的「樓梯已解鎖」 | — | `notice_stair_unlock` |
| `ending` | 結局畫面 | `ending`：`id`（`story/endings.json` 中的 `e_01` …） | `ending` |
| `hub` | 村莊大廳的某個地點（它的對話）；解鎖旗標要放在 `flags` | `hub`：`location` | `hub_village` |

節點動畫與粒子預覽已經有自己的選項（`--anim=<檔案>#<片段>`、`--fx=<檔案>#<特效>`；[05](../../engine/zh_TW/05_ANIMATION.md)、
[07](../../engine/zh_TW/07_PARTICLES.md)）。

**玩法規劃（[07](07_GAMEPLAY_PLAN.md)）中的新場景**做好時會在這裡加上場景名稱：
- 武器測試場：已完成（上面的 `arena`）。每種新武器的玩法都先在那裡試；
- 卡片收集頁：`menu` 的 `cards` 分頁；
- 其他模式，各自是一個場景。

計畫的每個階段完成時都附上它的情境。

## 4. 測試檢查什麼

在 `test` 的畫格結束時：
- **場景**：`expect.scene`（預設是 `scene`）必須正在顯示。例如 `battle` 表示戰鬥仍在進行，`stage` 表示樓層上沒有任何
  視窗。
- **可選的數值：**

  | 欄位 | 檢查 |
  |---|---|
  | `menuTab` | 玩家選單所在的分頁 |
  | `gold`、`hp`、`lv` | 玩家的數值 |
  | `arenaFights`、`arenaWins` | 測試場中開始的場數／勝利的場數 |
  | `enemyAnim` | 戰鬥畫面上怪物的狀態：`idle`、`attack`、`hurt`、`die`（[05](../../engine/zh_TW/05_ANIMATION.md)「戰鬥中的怪物」） |
  | `foesDown`、`target` | 戰鬥中倒下的怪物數；攻擊打向的怪物（0 = 第一隻） |
  | `battleEvents` | `{ "parry": 1, "hit": 0 }`：紀錄中每種戰鬥事件的數量（`toms_game --battle-log` 會印出） |
  | `pace` | 戰鬥的節奏：`cooldown` 或 `stamina` |
  | `grade` | 勝利後結算畫面的評等：`S`、`A`、`B` 或 `C`（[15](15_BATTLE_REVAMP.md) §3.6） |
  | `enemyClip` | 它為那個狀態播放的片段（`attack`，或某隻怪物自己的 `attack_<id>`） |
  | `battleAnim` | 有戰鬥片段的武器，戰鬥畫面顯示的片段：`charge`、`attack`、`miss` 或 `idle`（[05](../../engine/zh_TW/05_ANIMATION.md)〈角色〉） |
  | `crystals` | 玩家擁有的水晶（[07](07_GAMEPLAY_PLAN.md) 第 16 節；`give` 也接受 `crystals:<n>`） |
  | `equipped` | 裝備中武器的 id（沒有則為 ""） |
  | `gearOwned` | `["<id>", ...]`：擁有的裝備正好是這些，順序不限 |
  | `dialogueNode` | 對話開著，且在這個節點 |
  | `battleClip` | 該狀態播放的片段：武器為它取的名稱（`"battleAnim"` 的 `"clips"`，例如劍的 `raise`） |
  | `weaponLevel` | `{ "<武器 id>": <等級> }`：每把武器由勝場得到的等級（[07](07_GAMEPLAY_PLAN.md) 第 13 節） |
  | `cards` | `{ "slime": 1 }`：每種怪物持有的卡片張數 |
  | `stairUpLocked`、`stairUpMarked` | 目前樓層第一個上樓梯：鎖住或打開；有金色箭頭標記（剛打開） |
  | `stage` | 目前的樓層 |
  | `inventoryHas` | 背包中的道具 |
  | `flags` | 必須已設定的流程旗標 |
  | `condition` | 遊戲本身格式的任何條件，和事件的 `requires` 相同 |

檢查失敗時會印出例如 `[scenario] <name>: FAILED  gold is 350, expected 999`，遊戲以代碼 3 結束，測試失敗。設定檔無法讀取，
或指定了不存在的敵人、對話、道具、大廳地點，也會在開始時同樣失敗。

測試會把最後一個畫格存成 `Build/<preset>/test-run/scenario/<name>/<name>.png` 供查看。它不和參考圖比較，所以畫面的改變不會讓
情境失敗；那是截圖測試（`smoke.*`）的工作。

## 5. 變更會執行哪些情境

`tools\test.cmd`（只測變更，[08](../../project/zh_TW/08_TESTS.md)）挑選情境的方式和截圖測試相同：
- `tests/scenarios/<name>/` 中的變更執行 `scenario.<name>`；
- `_shared/` 或執行腳本的變更執行所有情境；
- 遊戲程式依檔名縮小範圍：`game_combat.cpp` 執行 `scenario.battle_*`，`game_store.cpp` 執行 `scenario.store_*`，對話檔執行
  `scenario.dialogue_*` 與 `scenario.talk_*`，依此類推；
- 檔名都不符合的遊戲程式執行所有情境。

程式在 `src/core/game/core/game_scenario.cpp`（`Game::startScenario`、`Game::scenarioCheck`）、`toms::readSaveFile`
（`save_slots.h`）、`src/game/src/main_sdl.cpp` 的 `--scenario`，以及 `tests/scenarios/run_scenario.cmake`。
