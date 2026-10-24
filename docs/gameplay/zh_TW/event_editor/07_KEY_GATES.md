# 07 — 專屬鑰匙門與事件鏈

> 英文原文：[07_KEY_GATES.md](../../event_editor/07_KEY_GATES.md)

**專屬鑰匙門**只會被一個特定道具打開：不是彩色鑰匙，而是由事件給予的劇情道具。門後的房間可以放事件、NPC 或可撿取的道具。
事件的 **actions** 負責給出這個道具。搭配 `requires` 與 `next`（[06_EVENT_LOGIC.md](06_EVENT_LOGIC.md)），一個長事件可以跨越多個樓層喚醒後續事件。

## 1. 門

在樓層地圖（`F??.stage.json`）的圖例中加入 `gate:<道具 id>`：

```json
"legend": { "Q": "gate:key_bellward" },
```

| | 規則 |
|---|---|
| 開啟 | 玩家持有該道具時撞門即開；道具會被用掉，門消失 |
| 上鎖 | 否則玩家原地不動，並看到「這道門刻著紋樣，只有「{item}」能打開。」 |
| 保持開啟 | 是，離開樓層再回來也一樣（實體狀態 *Opened*，與一般門相同） |
| 阻擋 | 開啟前會擋住行走與點擊移動 |
| 外觀 | 門上覆蓋鑰匙的顏色：`items.json` 中道具的 `tint`（預設紫色），所以不會被誤認為黃 / 藍 / 紅鑰匙門 |
| 任意字元 | 不需要固定字元（不像 `y` / `b` / `r` 門） |

鑰匙是 `data/items.json` 中類型為 `story` 的道具。它會放進背包（不是計數的彩色鑰匙），在背包中不能「使用」（使用只會弄丟它），
`"tint"` 決定門的顏色：

```json
"key_bellward": { "type": "story", "sprite": "key_red.png", "tint": [0.62, 0.3, 0.92],
                  "name": "item.key_bellward.name", "desc": "item.key_bellward.desc", "important": true }
```

**關卡編輯器：** 調色盤有 **專屬鑰匙門**（每個劇情道具一扇門）與 **專屬鑰匙**（劇情道具本身，可直接放在地圖上，而不是由事件給予）。
檢視器會顯示哪把鑰匙能開這扇門。

## 2. 事件動作（遊戲現在會執行）

事件的 `actions` 在事件觸發時執行一次：

| 動作 | 效果 |
|---|---|
| `{ "type": "give", "itemId": "key_bellward" }` | 給予道具；玩家看到「獲得：{item}」 |
| `{ "type": "addCounter", "counter": "insight", "delta": 1 }` | 增加劇情計數（有上下限） |
| `{ "type": "setFlag", "flag": "flag_x" }` | 設定本輪旗標 |

`addVar` / `setVar`（事件變數）尚未執行（[06](06_EVENT_LOGIC.md) 第 6 節）。

已完成事件的格子在玩家回到該樓層時不會再出現，所以事件不會重複給予道具。

## 3. 範例：鐘的事件鏈（第 1 章）

`pool_act01.json` 中的六個事件，從低樓層 → 高樓層 → 再回到低樓層：

| # | 樓層 | 事件 | 等待（`requires`） | 接著（`next`） | 動作 |
|---|---|---|---|---|---|
| 1 | F03 | `ev_bell_cracked`「裂開的鐘」 | — | 2 | |
| 2 | F05 | `ev_bell_ringer_key`「敲鐘人的鑰匙」 | 1 完成 | 3 | 給予 `key_bellward` |
| 3 | F04，鐘紋門後 | `ev_bell_ringer_room`「敲鐘人的小屋」 | 2 完成 | 4 與 5 | |
| 4 | F06 | `ev_bell_rope`「磨斷的鐘繩」 | 3 完成 | 6 | |
| 5 | F02 | `ev_bell_clapper`「失落的鐘舌」 | 3 完成 | 6 | |
| 6 | F03 | `ev_bell_rings`「鐘聲再響」 | 4 **與** 5 都完成 | — | 洞察 +1，給予 `gem_atk` |

```
F03 裂開的鐘 ─▶ F05 敲鐘人 ─▶（鑰匙）─▶ F04 鐘紋門 ▶ 小屋 ─┬▶ F06 鐘繩 ─┐
                                                         └▶ F02 鐘舌 ─┴▶ F03 鐘聲再響
```

- 「等待」寫成 `{ "type": "runFlagSet", "flag": "event_<id>" }`（事件觸發時設定）；最後一個是兩者的 `all`。
  踩到仍在等待的事件只會讓它「開始」：它留在格子上，玩家的事件記錄會列出它。
- F04 的敲鐘人小屋是 x 16–17、y 13–14 的 2 × 2 空間；位於 (16, 12) 的門 `Q` 是唯一入口。
- 文字：`text.json` 中的標題、描述與台詞（zh_TW 與 en）：`ev_bell_*`、`item.key_bellward.*`、`gate.locked`、`gate.opened`、`event.gotItem`。

**在事件編輯器中**每個事件會顯示「只有在 … 完成時才啟用」與「接著觸發 …」；邏輯圖與關係圖會畫出這條鏈。**在關卡編輯器中**各樓層會顯示事件，F04 會顯示門。

## 4. 檢查

- `unit.event_chain_test`：每個動作都指向真實的道具 / 計數，每個「等待」都指向真實的事件；每扇門都指向某個事件給予或某個樓層放置的劇情道具；
  鐘事件鏈的連結與樓層；F04 的小屋在門關著時到不了，門開後可以到達。
- `smoke.bell_gate`：遊戲本身，在 F04 帶著鑰匙：走到門前、開門、走進小屋、觸發事件（`tests/golden/bell_gate.png`）。

## 5. 自己做一個

1. 在 `items.json` 加入 `story` 道具，附上 `name` / `desc` 文字與 `tint`。
2. 在關卡編輯器中，把門（**專屬鑰匙門**）畫在房間唯一的出入口；把房間的事件、NPC 或道具放在裡面。
3. 在事件編輯器中，讓某個事件給予該道具（**接著做 → 給予**），並用「只有在…時才啟用」與「接著觸發」把事件連起來。
4. 儲存。不使用編輯器時，用 `node tools/web_editors/sync_floor_index.mjs` 重建事件索引，並用 `tools\web_editors\refresh_data.cmd` 更新編輯器的資料副本。

測試選項：`toms_game --goto=F04 --give=key_bellward --events-done=ev_bell_cracked,ev_bell_ringer_key` 會直接在 F04 開始新遊戲，帶著鑰匙，且事件鏈已完成到小屋之前。
