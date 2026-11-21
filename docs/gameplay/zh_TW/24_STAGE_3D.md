> 英文原文：[24_STAGE_3D.md](../24_STAGE_3D.md)

# 24. 用 3D 走樓層（2026-11-21）

擁有者（2026-11-20，附上一張 3D 風格關卡的圖）：走在樓層上「看起來像 80 年代，太舊了，像 40 年前做的」；關卡的樣子要改造。
擁有者對四個問題的回答：

| 問題 | 回答 |
|---|---|
| 做到多深？ | **真正的 3D 樓層**：每層的牆與地面是 3D 模型，有光照與陰影 |
| 鏡頭？ | **傾斜、跟著主角**（往下約 55°），北方仍在上 |
| 角色？ | **手繪圖，放大**：地圖上用戰鬥角色的手繪圖，不用 16 像素小圖 |
| 材質？ | **AI 繪製，每種外觀一套**（木、城堡、洞窟、墓穴……），無縫 |

**這是計畫。第 1 步（F01 原型，§6）已做好；其他步驟還沒做。**未決的問題在 §7 與
[13_OPEN_DECISIONS.md](../../project/zh_TW/13_OPEN_DECISIONS.md)。

## 1. 原本的樣子

- 走路的畫面把樓層畫成平的 16 像素圖塊、從上往下看（`Game::f_DrawTileLayer`），每種格子、每種樓層主題一張圖
  （`wall_wood`、`floor_wood`……），地圖上的物件畫在上面（`MapFloor::f_Render`）。
- 戰鬥已經有 3D 舞台（[21_BATTLE_3D.md](21_BATTLE_3D.md)）：glTF PBR 繪製器、照明用的 HDR 天空、有陰影貼圖的太陽或月亮、點光源、
  背景載入，以及站在場景中、受場景光照的 2D 圖（world group）。**3D 樓層全部沿用**；繪製器不需要新東西。

## 2. 怎麼運作

```
樓層檔（FNN.stage.json：tiles、theme）──tools/stage3d/make_floor3d.py──► stage3d/floors/FNN.gltf
                                                                         （＋ looks/<theme>/*.jpg、*_n.png）
data/stage3d.json（鏡頭、每種外觀的光、圖的高度）──► Game::f_Walk3DStage ──► 主程式的 BattleStage
地圖的物件（MapFloor）──► 各自一個 world group，站在自己的格子上 ──► 在同一個場景中受光
```

- **一格是一公尺**：x 是行，z 是列（南方是 +z），y 朝上。模型用這個單位，所以地圖的一格就是模型的一平方公尺，不需要其他換算。
- **主程式**（`GameSession`）為樓層多一個 `BattleStage`。進入樓層時在背景載入樓層的模型，在畫面是走路場景時（上面沒有戰鬥、對話、商店、
  標題或結局）畫出來。戰鬥的舞台與樓層不會在同一格畫面出現；兩者共用 3D 的 view。
- **遊戲**（`game_walk3d.cpp`）提供舞台設定（模型當成一個 prop，所以牆會投下太陽的影子）、鏡頭與影子的位置。畫的時候，每個地圖物件
  放進自己的 world group：它的 2D 外觀照舊畫進每格 100 像素地圖上的方框，group 讓方框站在它的格子上，面向鏡頭並稍微往後仰。
  地面的標記（樓梯、上鎖樓梯的鎖頭、走路的目標）平躺在地板上。
- **主角**是他武器的手繪戰鬥角色（與戰鬥相同的動畫，播 idle），面向他最後走的方向。怪物、人物與物品仍是地圖小圖，放大（§5）。
- **點擊**經過鏡頭：螢幕上的一點變成一條射線，射線在地面上找到格子（`Game::f_Walk3DScreenToTile`）。按鍵照舊。
- **2D 地圖保留**：沒有模型的樓層、`stage3d.json` `"on": false`、`--walk-3d=off`，或沒有 mesh shader 的繪製器，都畫舊的地圖。
  腳本執行（測試）除非要求（`--walk-3d=on`、情境的 `"walk3d": true`）都畫 2D，所以參考圖不會變。

## 3. 樓層的模型（`tools/stage3d/make_floor3d.py`）

- **牆**：每個 `#` 格是 1 公尺高的方塊（鏡頭看得到牆後：牆後的主角仍看得見）。只做它的頂面，以及旁邊是空格的側面（包括地圖邊緣：
  最下面一列的正面看得到）。F01 是 734 個三角形。
- **地面**：整張地圖下面一個四邊形，材質每 2 公尺重複一次。
- **三種材質**：地面、牆、牆頂。材質用樓層的外觀（它的 `"theme"`；沒有就是 castle），同一外觀的所有樓層共用（`.gltf` 中的
  `../looks/<look>/`）。
- **外觀**（wood、forest、castle、cave、crypt、crystal、ice、lava、sand、shadow）：`--paint <look>` 用 ComfyUI 畫每個部分
  （SDXL，Juggernaut XL，1024 像素；文字在工具的 `LOOKS`），做成無縫（與移動一半的自己混合）、512 像素，並從亮度做出法線貼圖。
  `--seeds` 可試多個；用第一個，其他留在 `Build/art_out/stage3d/` 比較。還沒畫的外觀由工具畫（外觀顏色的簡單花紋），所以每層都能建。
- **木**（第 1 章）已畫好：橡木地板（地面）、圓木牆、牆頂是深色的舊木板（重畫一次：第一次的牆頂有很搶眼的綠苔）。

```
python tools/stage3d/make_floor3d.py F01 F02           建這幾層
python tools/stage3d/make_floor3d.py --all             所有樓層
python tools/stage3d/make_floor3d.py --paint castle    畫一種外觀（ComfyUI 在 127.0.0.1:8188），然後建
python tools/stage3d/make_floor3d.py --paint wood --parts top --seeds 5,8   重畫一個部分
```

## 4. `data/stage3d.json`

| 鍵 | 內容 |
|---|---|
| `on` | false：所有樓層 2D |
| `model` | 樓層的模型，`{id}` = 樓層 id（`stage3d/floors/{id}.gltf`） |
| `shadow` | 主角、怪物與人物腳下的柔和圓影（戰鬥用的那個） |
| `camera` | `pitch`（55°）、`distance`（11 公尺）、`fov`（38°）、`follow`（追上主角的速度）、`lookAhead` |
| `sprites` | `glow`、`lean`（24°）、`shadow`（圓影半徑）、依種類的 `heights`（一格大的圖：player、monster、npc、item、door、event、other）、`paintedHero`、`paintedHeroHeight` |
| `looks` | 依主題，其餘用 `default`：`sky`（只照明：`"background": false`，塔站在黑暗中）、`exposure`、`light`（太陽：dir、color、intensity、ambient） |

## 5. 計畫

| 步驟 | 內容 | 狀態 |
|---|---|---|
| **1. F01 原型** | 工具、畫好的木外觀、`stage3d.json`、主程式的第二個舞台、跟隨鏡頭、站在場景中的地圖物件、會轉身的手繪主角、點擊 3D 地面、`--walk-3d`、情境 `walk_3d` | **2026-11-21 已做好** |
| 2. 每一層 | 畫其他 9 種外觀；建 F01–F35；設定 > 3D 樓層（開／輕量／關，像 3D 戰鬥）；模型中的樓梯做成階梯、門做成門框；擋在鏡頭與主角之間的牆變淡 | |
| 3. 地圖上的手繪圖 | 怪物用戰鬥圖（放大，播 idle）、人物用全身手繪圖、物品與鑰匙用手繪圖示；主角的走路動畫與格子之間的平順移動（現在像 2D 一樣一次跳一格） | |
| 4. 氣氛 | 火把（點光源與火焰）、光束、灰塵與火星、一點霧、每章的調色；樓層橫幅與進場時的慢速鏡頭 | |
| 5. 打磨 | 地圖邊緣的鏡頭限制、事件時短暫的晃動、慢電腦的速度；網頁與 Android 只在擁有者要求時 | |

## 6. 第 1 步的成果（2026-11-21）

- `tools/stage3d/make_floor3d.py`：模型建造與外觀繪製（§3）。`assets/media/stage3d/floors/F01.gltf`（＋ `.bin`）、
  `assets/media/stage3d/looks/wood/`（地面、牆、頂：顏色與法線貼圖）。
- `assets/data/stage3d.json`（鍵 `data.stage3d`）。
- `src/core/game/core/game_walk3d.cpp`：舞台設定、鏡頭、影子、站立的物件、手繪主角、地面的標記、點擊地面。3D 開著時
  `MapFloor::f_Render` 把每個物件放進自己的 group。
- `src/game/src/game_session.cpp`：`m_upWalkStage`，在走路場景時畫。`BattleStage`：天空可以只照明不畫出來（`"background": false`）。
- `--walk-3d=on|off`（`main_sdl.cpp`）：Windows 桌面一般遊玩時開；腳本執行時關；網頁與 Android 只在要求時。
- 情境 `walk_3d`：F01 是 3D，點擊主角會找到他的格子。

**第 1 步仍像 2D 的部分：**怪物、人物與物品的圖（16 像素圖，放大）；樓梯（地板上的一張平面圖）；門（一張站立的圖）。F01 以外的樓層
還沒有模型，所以畫 2D。

## 7. 未決的問題

| 問題 | 預設 | 我的建議 | 回答 |
|---|---|---|---|
| **Q117** 預設每個人都用 3D 樓層，設定裡可選 2D？ | 3D 開（桌面），沒有模型時 2D | 是；第 2 步加設定 > 3D 樓層 |  |
| **Q118** 鏡頭：固定（北方在上），還是玩家可以轉 90°？ | 固定 | 固定：地圖是從上往下設計的，「上」維持在上 |  |
| **Q119** 牆高：1 公尺（鏡頭看得到牆後），或更高（1.6 公尺）並讓擋在主角前的牆變淡？ | 1 公尺 | 現在 1 公尺；第 2 步試 1.6 公尺加變淡 |  |
| **Q120** 地圖上的怪物：用戰鬥圖（一張圖，與戰鬥中相同），還是新畫的小地圖圖？ | 戰鬥圖 | 戰鬥圖 |  |
| **Q121** 村莊與據點畫面也要 3D？ | 不要，保留手繪畫面 | 不要，等樓層做完 |  |
