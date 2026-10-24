> 英文原文：[14_AI_SPRITES.md](../14_AI_SPRITES.md)

# 14 — 用 AI 製作 TOMS 新圖（怪物、NPC、門、道具、樓層主題）

本文說明遊戲如何用 `AIGamestyle/` 的本機 AI 流程（與遊戲換皮相同的 ComfyUI 設定；電腦安裝見 [AIGamestyle/Doc/Reskin_Setup_New_PC.zh-TW.md](../../../AIGamestyle/Doc/Reskin_Setup_New_PC.zh-TW.md)）
得到約 230 張新的 32 × 32 圖，這些圖如何變成關卡編輯器可放置的草稿類型，以及讓它們顯示所需的幾個遊戲小功能：
圖集中任何圖都能以名稱使用、門以自己的圖繪製、樓層主題、玩家外觀。

## 1. 做了什麼（擁有者的選擇，2026-10-17）

| 類別 | 新增 | 成為 |
|---|---|---|
| 怪物 | 60 隻，分 6 階各 10 隻（森林、洞窟、不死、火焰、冰雪、城堡） | `enemies.json` 條目，依階給草稿數值 |
| NPC | 50 位（村民、商人、守衛、貴族、神職、旅人） | 每位一個 NPC 檔與一頁對話 |
| 門與鑰匙 | 10 + 10（翡翠、紫晶、銀、青銅、黑曜石、寒霜、火焰、骨、符文、太陽） | 一把只開自己門的鑰匙道具；門以自己的圖繪製 |
| 事件道具 | 50 件（護身符、地圖、封印信、提燈、法術書……） | `items.json` 劇情道具（保留，不消耗） |
| 樓層主題 | 10 套 ×（牆、地板、上樓梯、下樓梯） | 在關卡編輯器為樓層選主題 |
| 玩家外觀 | 10 種（法師、盜賊、聖騎士、遊俠……） | 設定 `playerLook` |

選擇：**圖加草稿類型**（類型標記 `"draft": true`：數值與文字是待平衡與改寫的初稿）；**60 隻怪物與 50 位 NPC**；
**每扇新門有自己的圖**（一個小的遊戲修改）；**樓層主題**（圖與功能）。

## 2. 一張圖怎麼做

TOMS 的圖是 32 × 32 像素畫：平塗色、粗的深色外框、塊狀造型、透明背景。依類別分兩種做法
（`AIGamestyle/_pipeline/tools/make_sprites.py`）：

**依描述畫新東西**（怪物、NPC、玩家、道具）：
1. SDXL base 加 **pixel-art-xl LoRA**，在素色背景上以 1024 × 1024 畫出描述。提示詞要求 Q 版、塊狀、少細節的角色
   （生物與人物），或單一物件圖示（道具）。
2. **IP-Adapter** 參考一張遊戲同類舊圖的拼圖（生物參考生物，道具參考寶石／藥水／鑰匙，權重 0.3），讓新圖像舊圖。
   參考太強會把道具畫成角色並讓顏色變灰；0.3 加上依類別的拼圖效果好。
3. **Inspyrenet**（ComfyUI 節點）去除背景。
4. 縮小到 32 × 32 格內的 30 × 30（生物腳貼底，物件置中），把縮小後混掉的顏色調回鮮明，alpha 改為硬邊，
   調色盤減到 8 色，外圍畫 1 像素近黑外框。

**遊戲已有之物的變體**（門、鑰匙、樓梯、牆、地板）：從遊戲原圖**衍生**，所以新門的形狀與黃門完全相同，
在地圖上的位置也一樣：
1. 原圖（例如 `door_yellow`）轉灰（否則原色會壓過描述：「翡翠」門仍是黃色），放大到 1024。
2. 以 pixel-art LoRA 做 img2img（強度 0.8）朝描述改畫；ControlNet canny 維持形狀。
3. 縮回 32 × 32，套用原圖的**精確輪廓**；原圖的近黑外框像素保持近黑。

每張圖旁邊保留 1024 的大圖（`<id>_big.png`）以便檢查或重做。

## 3. 計畫檔

`AIGamestyle/_pipeline/style_profiles/make_toms_sprites_plan.py` 列出每張圖：id、一行描述、英文與繁體中文名稱。
執行它會寫出 `toms_sprites.json`，兩個工具都讀它：

```
cd AIGamestyle\_pipeline\style_profiles
python make_toms_sprites_plan.py                       # 改了描述或名稱之後
cd ..\tools
python make_sprites.py ..\..\output\toms_sprites --plan ..\style_profiles\toms_sprites.json        # 還沒做的
python make_sprites.py ..\..\output\toms_sprites --plan ..\style_profiles\toms_sprites.json --only m_yeti --force   # 重做一張
python sprite_sheet.py sheet.png ..\..\output\toms_sprites --big                                  # 檢視
python add_toms_types.py ..\..\output\toms_sprites    # 放進遊戲：圖、清單、草稿類型、名稱
Build\windows-release\bin\atlaspack.exe build art_raw\atlas\game.atlasproj --all-variants      # 重建圖集
python tools\refresh_meshes.py                       # 依新的像素重新切 sprite 網格（docs/engine/16）
```

計畫中的類別有 `style`（提示詞開頭）、`reference`（外觀參考哪些遊戲圖）、`mode`，衍生類別還有 `base` 與 `denoise`。

## 4. 遊戲與編輯器新增的功能

- **圖集中任何圖都能以名稱繪製。** 遊戲以前只認得內建的 30 張圖名（類型指定其他圖會畫成地板）。現在載入圖集時，
  第一頁上的每張圖都有自己的圖層（`game_assets.cpp`），所以類型的 `"sprite"` 可以是任何新圖。
- **門以自己的圖繪製。** 鑰匙道具若有 `"door": "door_emerald"`，它的門就顯示那張圖（不染色，開門時直接打開，
  不播放黃門的動畫）。沒有的門照舊是染色的黃門。關卡編輯器也用同樣方式畫門（以前顯示的是鑰匙圖）。
- **調色盤**分開劇情道具：會 `"opens"` 某扇門的是門鑰匙（門與鑰匙分類）；其他是事件道具（道具分類）。
  在門／鑰匙按 **+** 會寫入 `"opens"`。
- **樓層主題。** 關卡檔的 `"theme"`（例如 `"ice"`）讓遊戲在圖集有圖時改畫 `wall_ice`、`floor_ice`、
  `stairs_up_ice`、`stairs_down_ice`。關卡編輯器的屬性面板有 **樓層主題**（列出圖清單中找到的主題），並以它繪製地圖。
- **玩家外觀**：`settings.json` 的 `"playerLook"`（例如 `"player_mage"`）會畫在地圖與選單上。
  設定頁目前還沒有這一列（加上會改變設定畫面與其參考截圖）。

## 5. 結果

**已放進遊戲（2026-10-19）。**230 張圖都在 `art_raw/sprites`，也在遊戲圖集中（260 張，一頁 502 × 682；
`atlaspack refs --import new` 把新檔匯入內嵌專案，再 `build --all-variants`）。資料：

| 產生 | 位置 |
|---|---|
| 60 隻怪物 | `enemies.json`，依階的草稿數值 |
| 50 位 NPC | `npcs/<id>.json` + `dialogue/<id>.json`，各一句閒聊 |
| 50 件事件道具 | `items.json`，類型 `story` |
| 10 把閘門鑰匙 | `items.json`，`"opens": "gate:key_<主題>"`、`"door": "door_<主題>"` |
| 名稱、說明 | `text.json`（zh_TW、en） |

UI 字型子集已為新名稱重建（2292 → 2431 字）。

**檢視。**每張圖都在拼圖上看過。約 50 張重做：
- 畫成人的生物，以及好幾個小人擠在一起的圖：改用更明確的描述（「沒有手腳，不是人」、「只有一個角色」）並換種子；
- 最難的幾張用五選一的種子搜尋；
- 火焰門與它的鑰匙每次都是灰色，所以依它們自己的明暗改成火焰漸層（外框保留）。

第一次的結果保留在 `AIGamestyle/output/toms_sprites/_first/`。

**簡單動畫。**`tools/make_idle_clips.py` 替每隻沒有待機動畫的怪物與草稿 NPC 加上循環待機動畫，依名稱選擇：

| 動作 | 對象 | 數量 |
|---|---|---|
| 懸浮：上下飄、翅膀收縮 | 蝙蝠、小精靈、幽靈、龍、貓頭鷹…… | 14 |
| 壓扁拉長 | 史萊姆、蟲 | 4 |
| 緩慢呼吸 | 魔像、巨魔、猛獁、雪怪…… | 15 |
| 左右搖擺 | 骷髏、騎士、木乃伊、巫妖…… | 15 |
| 小跳 | 其他 | 12 |
| 輕微呼吸 | 50 位新 NPC（`npc_idle.anim`） | 50 |

已有的動畫（原本七隻怪物，在動畫編輯器手做）不會被改動。每段動畫的長度依 id 略有不同，遊戲也會錯開每一格的循環，所以
同一房間的怪物不會同步。NPC 現在可以有待機動畫（`assets/media/anim/npc_idle.anim` 的 `idle_<npc id>`，資源
`anim.npc_idle`）；原本的五位 NPC 沒有，所以它們的截圖不變。任何動畫都可在動畫編輯器修改；`make_idle_clips.py` 只會補上缺少的。

在測試樓層檢查（冰主題、八隻新怪物、四位 NPC、翡翠與火焰閘門、道具）：全部都畫得出來，每隻怪物與 NPC 在不同影格間有動，
閘門顯示自己的門。

## 6. 已知限制

- **32 像素很小**：描述太細會失去細節；描述用幾個字說明是什麼生物即可。有些圖需要換種子重做
  （`--only <id> --force --seed N`）。
- **草稿類型**：數值依階而定，不是設計過的；NPC 只有一句閒聊；事件道具的說明是通用句。都標記 `"draft": true` 待改寫。
- **牆與地板圖塊並非無縫**：它們像原圖一樣以 32 像素重複。
- **待機動畫很簡單**（每隻一種動作，依名稱選）；更好的可在動畫編輯器手做。
- **關卡檔中的格子字母 `y`／`b`／`r` 是彩色門**：不要拿來代表別的東西。
