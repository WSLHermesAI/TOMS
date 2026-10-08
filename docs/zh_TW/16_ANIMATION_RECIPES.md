# 16 · 動畫食譜：手寫（或用 AI 寫）簡單的 `.anim` 片段

> 英文原文：[16_ANIMATION_RECIPES.md](../16_ANIMATION_RECIPES.md)

如何不用編輯器，直接以 `.anim` JSON 撰寫淡入 / 淡出、簡單的移動和短的戰鬥動作（踏進、揮擊、退回）。
規則的寫法讓人或 AI 都能一步一步照著做。完整的格式和編輯器在
[15_ANIMATION.md](15_ANIMATION.md)。

下面每個片段都在 [examples/anim_recipes.anim](../examples/anim_recipes.anim) 中，那是一個可用的檔案，
由 `ctest`（`anim.check_recipes`）檢查。請從那裡複製。

## 1. 工作流程

1. 撰寫或擴充一個 `.anim` 檔（JSON，UTF-8）。把它放在它的 atlas 附近：atlas 路徑是
   **相對於 `.anim` 檔**存放的。
2. 檢查它。必須印出 `0 error(s)`；結束碼 0 = 正常，2 = 有錯誤：
   ```
   Build\windows-release\bin\anim_editor.exe --headless check path\to\file.anim
   ```
3. 觀看它：
   - 在遊戲中：`toms_game --anim=path/to/file.anim#clipName` 在畫面中央播放片段，並
     記錄它的事件（`[anim] event hit`）。
   - 在編輯器中：`anim_editor path/to/file.anim`，選片段後按 Space。
4. 調整時間、數值和緩動，然後再檢查一次。

## 2. 格式速查表

```jsonc
{
  "version": 1,
  "atlases": ["../../assets/media/atlas/game.atlas"],   // relative to this file; first = lookup order
  "clips": [
    {
      "name": "attack",          // unique in the file; the game plays clips by name
      "length": 0.8,             // seconds; 0 or missing = up to the last key
      "playCount": 1,            // 1 = once (default), N = N times, -1 = loop forever
      "stayAtLastFrame": true,   // after the end: keep the last frame (default) or hide (false)
      "root": { /* node */ }
    }
  ]
}
```

（`atlases`：相對於本檔，順序 = 查找順序；`name`：檔案內唯一，遊戲依名稱播放片段；`length`：秒，0 或沒有 = 到最後一個 key；
`playCount`：1 = 一次（預設），N = N 次，-1 = 永遠循環；`stayAtLastFrame`：結束後保留最後一格（預設）或隱藏（false）。）

**節點。** 除了 `name` 之外每個欄位都是選填。表中列出預設值。

| 欄位 | 預設 | 意義 |
|---|---|---|
| `name` | — | 在兄弟節點之間唯一；用好讀的名稱（`body`、`weapon`、`slash_fx`） |
| `sprite` | `""` | atlas 中的 sprite 名稱（`"slime"`，或帶 atlas id 的 `"game:slime"`）；`""` = 看不見的群組 |
| `pos` | `[0, 0]` | 相對於父節點的位置，單位像素；**y 往下** |
| `rot` | `0` | 角度；螢幕上**正值 = 順時針**；不會折返（0 → 720 = 兩圈） |
| `scale` | `[1, 1]` | x 為負值時鏡像（`[-1, 1]` = 面向另一邊） |
| `color` | `[1, 1, 1, 1]` | r、g、b、a 從 0 到 1，和 sprite 相乘；子節點也會被乘 |
| `visible` | `true` | `false` 會隱藏節點**和它的子節點** |
| `pivot` | atlas 的軸心（中心） | sprite 的 `[0..1, 0..1]`，從左上角量起；是節點旋轉和縮放所繞的點 |
| `order` | `0` | 兄弟節點之間的繪製順序（越大越前面）；`< 0` = 在父節點自己的 sprite 後面 |
| `blend` | `"normal"` | `"add"` = 加法（光暈、閃光、斬擊） |
| `inheritColor` | `true` | `false` = 忽略父節點的顏色/淡化 |
| `loop` | `false` | `true` = 在它最後一個 key 之後，再次播放它（和它子節點）的 key，永遠持續，片段結束後也一樣 |
| `stayAtLastFrame` | `true` | 在節點上：`false` = 在它最後一個 key 之後隱藏它和它的子節點 |
| `tracks` | 無 | key，每個通道一個陣列（見下方） |
| `children` | 無 | 子節點：會和這個節點一起移動、轉動、縮放和淡化 |

**軌。** 每個通道有自己的 key 陣列。**只放會改變的通道**：只淡化的節點
就只有 `color`。

| 軌 | `v` 值 | key 之間 |
|---|---|---|
| `pos` | `[x, y]` | 依 key 的 `ease` 內插 |
| `rot` | `角度` | 內插 |
| `scale` | `[x, y]` | 內插 |
| `color` | `[r, g, b, a]` | 內插 |
| `sprite` | `"name"` | 在 key 處跳到新的 sprite（翻頁動畫的影格） |
| `visible` | `true` / `false` | 在 key 處跳變 |
| `event` | `"name"` | 時間經過 key 時，送給遊戲一次 |

**Key：** `{ "t": 秒, "v": 值, "ease": "名稱" }`。`ease` 是選填（預設
`"linear"`），決定**從這個 key 到下一個 key** 的曲線，所以最後一個 key 的 ease 會被
忽略。

**緩動：** `linear`、`stepped`（維持值，然後在下一個 key 跳變），以及
`quadratic`、`cubic`、`quartic`、`quintic`、`sinusoidal`、`exponential`、`circular`、`bounce`、
`elastic`、`back`，每個後面加上 `In`、`Out` 或 `InOut`（`quadraticOut`、
`backInOut`、…）。也可以用三次貝茲曲線 `[x1, y1, x2, y2]`（像 CSS 的 `cubic-bezier`）。

| 你想要 | 使用 |
|---|---|
| 一開始快、然後變慢（衝進來、滑進來、彈出） | `quadraticOut`、`cubicOut` |
| 一開始慢、然後加速（蓄力、落下、淡出） | `quadraticIn`、`cubicIn` |
| 平滑地來回移動（上下浮動、回到原位） | `sinusoidalInOut`、`quadraticInOut` |
| 超出一點再穩定下來（彈出、壓扁後恢復、武器的揮過頭） | `backOut` |
| 落地時反彈 | `bounceOut` |
| 瞬間改變 | `stepped`，或兩個非常靠近的 key |

## 3. 規則（大家常犯的錯）

1. **在第一個 key 之前，軌維持的是第一個 key 的值，不是靜止值。** 要讓
   某個東西到 0.15 秒之前都隱藏，它的 `visible` 軌要以 `{ "t": 0, "v": false }` 開始。
   `color` 也一樣：淡入要從 `t: 0` 的 alpha-0 key 開始。
2. **在最後一個 key 之後，軌維持最後一個 key 的值。** 要結束在起點，就以
   起始值結束（例如退回到 `[0, 0]`）。
3. **要在中間停住，用兩個相同值的 key**（0.15 和 0.45 都是 `[28, 0]`），
   或給第一個 key `"ease": "stepped"`。
4. **同一軌中的 key 時間各不相同，且依遞增順序排列。** 同一軌中一個時間不能出現
   兩次；要做突然的跳變，就把兩個 key 相隔 0.01 秒。
5. **讓每個 key 都在 `length` 之內。** 檢查會對超過片段長度的 key 發出警告。
6. **子節點跟著父節點移動。** 把武器或特效放在 body 底下，它的 `pos`
   就是相對於 body。給武器一個在握柄處的 `pivot`（例如 `[0.5, 0.9]`），
   讓它繞著手揮動。
7. **淡化只改變 alpha。** 讓 r、g、b 保持 1，除非你想要色調，例如紅色的受擊
   閃光（`[1, 0.3, 0.3, 1]`）。
8. **Sprite 名稱必須存在於 atlas 中。** 遊戲 atlas
   （`assets/media/atlas/game.atlas`，32×32 的 sprite）有：`bat boss_demonlord coin demon
   door_blue door_red door_yellow exp_up floor gem_atk gem_def golem key_blue key_red key_yellow
   npc_handmaiden npc_king npc_princess npc_sorcerer npc_villager player potion_blue potion_red
   scroll skeleton slime stairs_down stairs_up wall wraith`。它還沒有武器或斬擊的 sprite，
   所以範例用 `gem_atk` 代替。等 atlas 有了真正的 sprite 再換掉。
9. **時間：** 60 fps 時一個畫面約 0.016 秒。戰鬥動作總長 0.3–1.0 秒看起來最清楚。
   打擊本身是 0.08–0.15 秒，恢復大約是打擊的兩倍長。
10. **事件是名稱，不是程式碼。** 使用和遊戲程式碼約定好的名稱（`hit`、`sfx_slash`、
    `shake`）。遊戲在時間經過 key 的那個畫面，從 `AnimPlayer::takeEvents()` 收到它們。

## 4. 食譜

每個食譜是片段 `root` 底下的一個節點。只列出 `tracks`；完整的片段在
[examples/anim_recipes.anim](../examples/anim_recipes.anim)。

**淡入**（`fade_in`，0.5 秒）：
```json
"color": [ { "t": 0.0, "v": [1, 1, 1, 0], "ease": "quadraticOut" },
           { "t": 0.5, "v": [1, 1, 1, 1] } ]
```

**淡出**（`fade_out`，0.5 秒，`"stayAtLastFrame": false`）：把同樣兩個 key 反過來，
用 `quadraticIn`。

**從左邊滑入**（`slide_in`，0.4 秒）。節點同時移動和淡入：
```json
"pos":   [ { "t": 0.0, "v": [-64, 0], "ease": "cubicOut" }, { "t": 0.4, "v": [0, 0] } ],
"color": [ { "t": 0.0, "v": [1, 1, 1, 0] },                  { "t": 0.2, "v": [1, 1, 1, 1] } ]
```

**待機浮動**（`idle_bob`，1 秒，`"playCount": -1`）。節點結束在起點，所以循環
沒有接縫：
```json
"pos": [ { "t": 0.0, "v": [0, 0],  "ease": "sinusoidalInOut" },
         { "t": 0.5, "v": [0, -3], "ease": "sinusoidalInOut" },
         { "t": 1.0, "v": [0, 0] } ]
```

**跳一下，落地時壓扁**（`hop`，0.45 秒）：
```json
"pos":   [ { "t": 0.0,  "v": [0, 0],   "ease": "quadraticOut" },
           { "t": 0.18, "v": [0, -16], "ease": "quadraticIn" },
           { "t": 0.36, "v": [0, 0] } ],
"scale": [ { "t": 0.36, "v": [1, 1],     "ease": "quadraticOut" },
           { "t": 0.4,  "v": [1.2, 0.8], "ease": "backOut" },
           { "t": 0.45, "v": [1, 1] } ]
```

**脈動 / 彈出**（`pulse`，0.4 秒）：scale `[1,1]` → `[1.3,1.3]`（`quadraticOut`）→ `[1,1]`
（`backOut`）。

**閃爍**（`blink`，0.6 秒）：`visible` key 每 0.1 秒在 `false` / `true` 之間交替，以
`true` 結束。

**旋轉**（`spin`，1 秒，循環）：`rot` 從 `0` 到 `360`，線性。

**翻頁動畫**（`flipbook`，循環）：每 0.15 秒一個 `sprite` key
（`key_yellow` → `key_blue` → `key_red`）。在編輯器中做影格序列時，在 Sprites 分頁選取影格，
再放到拖曳條上：編輯器會詢問時間間隔
（[15](15_ANIMATION.md)）。

**受傷**（`hurt`，0.3 秒）：紅色閃光淡回白色，同時節點左右搖晃，
每次越來越小：
```json
"pos":   [ { "t": 0.0, "v": [0, 0] }, { "t": 0.05, "v": [-4, 0] }, { "t": 0.1, "v": [4, 0] },
           { "t": 0.15, "v": [-3, 0] }, { "t": 0.2, "v": [2, 0] }, { "t": 0.25, "v": [0, 0] } ],
"color": [ { "t": 0.0, "v": [1, 0.3, 0.3, 1], "ease": "quadraticIn" }, { "t": 0.3, "v": [1, 1, 1, 1] } ]
```

**死亡**（`die`，0.6 秒，`"stayAtLastFrame": false`）：紅色閃光，然後節點一邊淡出
一邊壓扁（scale 到 `[1.4, 0.2]`）。

**有停留、消失和循環部件的彈出視窗**（[examples/anim_child_timing.anim](../examples/anim_child_timing.anim) 中的
`popup`）：
- **面板** 以 `backOut` 放大進來並停留（預設）。
- 它底下的**閃光** 淡出，並設 `"stayAtLastFrame": false`，所以之後就消失了。
- **徽章** 較晚彈出（0.3–0.6 秒）並停留。
- 設了 `"loop": true` 的 **`orbit` 群組** 在 0.5–2.5 秒之間轉 0 → 360，帶著一枚金幣繞圈。
- 設了 `"loop": true` 的**史萊姆** 上下浮動（key 在 0.6、1.1、1.6 秒，第一個和最後一個 key
  的值相同，所以循環沒有接縫）。

2.5 秒之後片段結束：面板和徽章維持，閃光隱藏，金幣和
史萊姆繼續移動。
```json
{ "name": "orbit", "loop": true,
  "tracks": { "rot": [ { "t": 0.5, "v": 0 }, { "t": 2.5, "v": 360 } ] },
  "children": [ { "name": "coin", "sprite": "coin", "pos": [70, 0] } ] }
```
沒有接縫的循環需要**第一個和最後一個 key 的值相同**（0 度和 360 度是同一個
角度）。

## 5. 戰鬥範例：踏進、揮擊、退回（`attack`）

攻擊者面向右方（目標在 +x）。總長 0.8 秒。

| 時間（秒） | `body` | `weapon`（body 的子節點） | `slash_fx`（body 的子節點，加法） |
|---|---|---|---|
| 0.00 → 0.15 | 往前踏 `[0,0]` → `[28,0]`，`quadraticOut` | 隱藏 | 看不見（alpha 0） |
| 0.15 → 0.20 | 停在 `[28,0]` | 出現，向後舉在 `-100°`（蓄力） | |
| 0.20 → 0.32 | 停住 | 揮擊 `-100°` → `80°`，`cubicOut` | 0.22–0.26 閃現並變大 |
| 0.28 | **事件 `hit`**（目標反應：傷害數字、`hurt` 片段） | | |
| 0.32 → 0.45 | 停住（揮過頭的延續） | 停在下方 `80°` | 0.26–0.40 淡出，放大到 1.6 倍 |
| 0.45 → 0.75 | 走回 `[28,0]` → `[0,0]`，`quadraticInOut` | 從 0.5 起隱藏 | |
| 0.75 → 0.80 | 靜止 | | |

```json
{
  "name": "attack",
  "length": 0.8,
  "root": {
    "name": "attacker",
    "children": [
      {
        "name": "body",
        "sprite": "player",
        "tracks": {
          "pos": [
            { "t": 0.0,  "v": [0, 0],  "ease": "quadraticOut" },
            { "t": 0.15, "v": [28, 0] },
            { "t": 0.45, "v": [28, 0], "ease": "quadraticInOut" },
            { "t": 0.75, "v": [0, 0] }
          ],
          "event": [ { "t": 0.28, "v": "hit" } ]
        },
        "children": [
          {
            "name": "weapon",
            "sprite": "gem_atk",
            "pos": [12, 4],
            "pivot": [0.5, 0.9],
            "order": 1,
            "tracks": {
              "visible": [ { "t": 0.0, "v": false }, { "t": 0.15, "v": true }, { "t": 0.5, "v": false } ],
              "rot": [
                { "t": 0.15, "v": -100 },
                { "t": 0.2,  "v": -100, "ease": "cubicOut" },
                { "t": 0.32, "v": 80 }
              ]
            }
          },
          {
            "name": "slash_fx",
            "sprite": "gem_atk",
            "pos": [30, 0],
            "order": 2,
            "blend": "add",
            "inheritColor": false,
            "tracks": {
              "color": [
                { "t": 0.22, "v": [1, 1, 1, 0] },
                { "t": 0.26, "v": [1, 1, 1, 1], "ease": "quadraticOut" },
                { "t": 0.4,  "v": [1, 1, 1, 0] }
              ],
              "scale": [
                { "t": 0.22, "v": [0.6, 0.6], "ease": "quadraticOut" },
                { "t": 0.4,  "v": [1.6, 1.6] }
              ]
            }
          }
        ]
      }
    ]
  }
}
```

為什麼這樣建：

- **武器和特效是 `body` 的子節點，** 所以往前踏和退回時會跟著走，
  不需要額外的 key。
- **武器的 `pivot` 靠近它的底部**（握柄），所以 `rot` 讓它繞著手揮動。
  負角度讓它往後傾（逆時針）；正角度讓它往前方揮下。
- **`visible` 在 `t: 0` 以 `false` 開始**（規則 1），所以揮擊之前不會顯示武器。
- **停住用的 key（0.15 和 0.45 都是 `[28,0]`）** 讓 body 在揮擊時保持不動（規則 3）。
- **`hit` 在 0.28，** 剛好在揮擊最快的部分之後。`cubicOut` 在前段就轉過大部分
  角度，所以撞擊感覺正好落在 hit 上。
- **`slash_fx` 用 `inheritColor: false` 和 `blend: add`，** 所以它會自己發光。

**變化**

| 變化 | 修改 |
|---|---|
| 向左攻擊 | 給 root `"scale": [-1, 1]`。這會鏡像整個片段，包括位置和揮擊。 |
| 更重的一擊 | 更長的蓄力（維持 `-100` 到 0.3）、更快的揮擊（0.08 秒），以及在 `hit` 之後 `body` 短暫後座 `[24,0]`。 |
| 突刺 | 沒有 `rot` 軌。把武器的 `pos` 從 `[8,4]` 移到 `[24,4]` 再移回來。 |
| 目標的反應 | 給目標第二個片段，在遊戲收到 `hit` 時播放：`hurt`（搖晃 + 紅色閃光），或 `die`。 |
| 施法 | `body` 做一個小的 `hop`。頭上方的 `spell_fx` 子節點 `pulse` 後淡出（`blend: add`）。在最高點送出 `"cast"`。 |

## 6. 檢查清單（連同需求一起交給 AI）

- [ ] 只有會改變的軌，每個 key 陣列都依 `t` 排序，沒有重複的時間。
- [ ] 每個節點在第一個 key 之前的值都是對的（需要時在 `t: 0` 設 `visible: false` / alpha 0）。
- [ ] 每個片段都結束在下一個片段預期的位置（一次性的片段回到靜止姿勢；循環的片段和
      開始時相同）。
- [ ] `length` 涵蓋最後一個 key；循環和會消失的一次性片段都設好了 `playCount` / `stayAtLastFrame`。
- [ ] 每個 sprite 名稱都存在於 atlas 中，每個事件名稱都是遊戲會處理的。
- [ ] `anim_editor --headless check file.anim` → `0 error(s)`；用
      `toms_game --anim=file.anim#clip` 看過一次。

**提示範本：**

> 閱讀 docs/16_ANIMATION_RECIPES.md 和 docs/examples/anim_recipes.anim。在 `<file>.anim` 中加入
> 片段 `<name>`：`<依序發生什麼，附大約的時間>`。Sprite：`<名稱>`。在 `<時機>` 送出事件
> `<name>`。遵守規則和檢查清單，執行 `--headless check`，並修正它回報的任何問題。
