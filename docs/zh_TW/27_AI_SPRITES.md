> 英文原文：[27_AI_SPRITES.md](../27_AI_SPRITES.md)

# 27 — 用 AI 製作 TOMS 新圖（怪物、NPC、門、道具、樓層主題）

本文說明遊戲如何用 `AIGamestyle/` 的本機 AI 流程（與遊戲換皮相同的 ComfyUI 設定；電腦安裝見 [AIGamestyle/Doc/Reskin_Setup_New_PC.zh-TW.md](../../AIGamestyle/Doc/Reskin_Setup_New_PC.zh-TW.md)）
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
Build\windows-release\bin\atlaspack.exe build assets\media\atlas\game.atlasproj --all-variants      # 重建圖集
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

（執行完成後填入）

## 6. 已知限制

- **32 像素很小**：描述太細會失去細節；描述用幾個字說明是什麼生物即可。有些圖需要換種子重做
  （`--only <id> --force --seed N`）。
- **草稿類型**：數值依階而定，不是設計過的；NPC 只有一句閒聊；事件道具的說明是通用句。都標記 `"draft": true` 待改寫。
- **牆與地板圖塊並非無縫**：它們像原圖一樣以 32 像素重複。
- **怪物沒有待機動畫**（動畫編輯器的 `monster_idle.anim` 只有原本七隻的動畫）。
