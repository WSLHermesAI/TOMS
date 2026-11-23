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

之後（2026-11-21）：「make 3D looking as default」（瀏覽器也要），以及「remove fallback to 2D and delete related useless files,
and make a loading circle while change to next scene too long」。

**已做好：每一層都是 3D，沒有 2D 地圖（§6）。第 3–5 步（§5）還沒做。**未決的問題在 §7 與
[13_OPEN_DECISIONS.md](../../project/zh_TW/13_OPEN_DECISIONS.md)。

## 1. 原本的樣子

- 走路的畫面把樓層畫成平的 16 像素圖塊、從上往下看，每種格子、每種樓層主題一張圖（`wall_wood`、`floor_wood`……），依章節染色，
  用 2D 鏡頭（設定：跟隨或分區）在上面捲動。
- 戰鬥已經有 3D 舞台（[21_BATTLE_3D.md](21_BATTLE_3D.md)）：glTF PBR 繪製器、照明用的 HDR 天空、有陰影貼圖的太陽或月亮、點光源、
  背景載入，以及站在場景中、受場景光照的 2D 圖（world group）。**3D 樓層全部沿用**；繪製器不需要新東西。

## 2. 怎麼運作

```
樓層的格子地圖（FNN.stage.json / stageNN.json、編輯器做的任何關卡）─┐
data/stage3d.json（外觀的光、鏡頭、圖的高度）─────────────────────┼─► Game::f_Walk3DStage
                                                                   │     （牆的列、外觀）
主程式的 BattleStage ◄─ floor_mesh.cpp：牆＋地面，記憶體中的 .glb ◄─┘
        ▲                  ＋ stage3d/looks/<look>/*_c.jpg / *_n.jpg（手繪）
地圖的物件（MapFloor）──► 各自一個 world group，站在自己的格子上 ──► 在同一個場景中受光
```

- **一格是一公尺**：x 是行，z 是列（南方是 +z），y 朝上。
- **進入樓層時才做出樓層的模型**（`src/game/src/floor_mesh.cpp`），從牆的格子，在載入器的背景執行緒上：每層不需要檔案，所以每一層、
  手繪地圖與編輯器做的關卡都是 3D。只看牆：門打開或怪物的格子清空時不會重做。
- **主程式**（`GameSession`）為樓層多一個 `BattleStage`。進入樓層時在背景載入，在畫面是走路場景時畫出來。戰鬥的舞台與樓層不會在
  同一格畫面出現；兩者共用 3D 的 view。
- **遊戲**（`game_walk3d.cpp`）提供舞台設定、鏡頭與影子的位置。每個地圖物件放進自己的 world group：它的 2D 外觀照舊畫進每格
  100 像素地圖上的方框，group 讓方框站在它的格子上，面向鏡頭並稍微往後仰。地面的標記（樓梯、門的格子、上鎖樓梯的鎖頭、開啟樓梯的
  箭頭、走路的目標）平躺在地板上；在畫面外的開啟樓梯，畫面邊緣會有一個指向它的金色標記。頭目與遊蕩者的名牌在它們的圖的頭上。
- **樓梯**在模型裡（擁有者，2026-11-22：「2D 樓梯太平，很容易被忽略」）：四級台階，從開放的一側走上去（優先南側，面向鏡頭）。
  **上樓**爬到牆頂的高度；**下樓**往下進入一樣深的坑。踏面用外觀的地面材質再調暗（`stairShade` 0.35：擁有者，2026-11-22，「和地板顏色太像」），豎面與坑壁用牆的材質。每座樓梯都有
  自己的光（`stairLights`：上樓的上方是暖光，下樓的坑裡是冷光；剛開啟時更亮，上鎖時較暗；離主角最近的優先）。每座樓梯上方立著它的**標示**（擁有者，2026-11-23：只有 3D 台階仍不容易看懂）：三個箭頭在上樓梯上方不斷**往上升**（金色「^」），
  在下樓梯上方不斷**往下沉**（淡藍「v」），樓梯剛開啟時更亮、更快並閃動；上鎖時改為鎖頭（`stairSign`：`size` 1 公尺、`speed`）。
- **事件**在它的格子上是一個小小的光之漩渦，「像記憶與思緒」（擁有者，2026-11-22；原本是金色的板子，在 3D 中站起來成了一塊黑磚）：
  兩條旋臂上像彗星的光點旋向一團呼吸般的光，淡紫與淡藍，每個事件有自己的節奏（`SwirlLook`，map_objects.cpp；特效圖集的 `glow`
  與 `dot`）。有自己外觀的事件維持原樣。
- **主角**是他武器的手繪戰鬥角色（播 idle），面向他最後走的方向。怪物、人物與物品仍是地圖小圖，放大（第 3 步）。
- **點擊**經過鏡頭：螢幕上的一點變成一條射線，射線在地面上找到格子。
- **設定 > 畫面範圍**（擁有者，2026-11-22）：×1、×1.5、×2、×3（`viewScales`）乘上鏡頭的距離：橫向約 12、18、24、36 格
  （19 × 16 的樓層在 ×3 時整層都看得到）；**預設 ×1.5**。存在設定檔（`viewScale`）。情境除非寫了 `"viewScale"` 否則用 ×1（情境 `walk_3d_far`：×2）。
- **沒有 2D 地圖。**樓層載入時，HUD 後面什麼都不畫；載入超過 `loadingDelayMs`（250 毫秒）時，畫面中央顯示**載入圓圈**（十二個點，
  一秒轉一圈，淡入）。戰鬥的 3D 舞台還在載入時也會顯示。腳本執行（測試）會等載入完成，所以每次的畫面都一樣。
- 所有地方：Windows 桌面、瀏覽器（那裡沒有太陽的影子，與 3D 戰鬥相同）、編輯器的遊戲畫面，以及 Android（沒試過：Android 只在
  擁有者要求時建置）。

## 3. 外觀（`tools/stage3d/paint_looks.py`）

- **三個部分**：地面（每 2 公尺重複）、牆的側面、牆頂。牆高 1 公尺（鏡頭看得到牆後：牆後的主角仍看得見）。
- **10 種外觀**都畫好了：wood、forest、castle、cave、crypt、crystal、ice、lava、sand、shadow。每個部分用 ComfyUI 畫（SDXL，
  Juggernaut XL，1024 像素；文字在工具的 `LOOKS`），做成無縫（與移動一半的自己混合）、512 像素，並從亮度做出法線貼圖。樓層用它關卡檔的
  `"theme"`；`stage3d.json` 沒有的主題用 `"defaultLook"`（castle）。
- `--seeds` 可試多個；用第一個，其他留在 `Build/art_out/stage3d/` 比較。木的牆頂重畫過一次（第一次有很搶眼的綠苔）。

```
python tools/stage3d/paint_looks.py castle                    畫一種外觀（ComfyUI 在 127.0.0.1:8188）
python tools/stage3d/paint_looks.py --all                     所有外觀
python tools/stage3d/paint_looks.py wood --parts top --seeds 5,8   重畫一個部分，試兩次
```

## 4. `data/stage3d.json`

| 鍵 | 內容 |
|---|---|
| `defaultLook` | 主題沒有外觀的樓層用的外觀（castle） |
| `wallHeight`、`groundRepeat` | 牆高（1 公尺）、地面材質的大小（2 公尺） |
| `loadingDelayMs` | 場景載入超過這麼久就顯示載入圓圈（250） |
| `viewScales` | 設定 > 畫面範圍的各段，× 鏡頭距離（1、1.5、2、3） |
| `shadow` | 主角、怪物與人物腳下的柔和圓影（戰鬥用的那個） |
| `stairShade` | 樓梯踏面的深淺：地面材質 × 它（0.35） |
| `stairLights` | 每座樓梯的光：`up`／`down` 顏色、`intensity`（剛開啟 ×1.6、上鎖 ×0.5）、`range`、`off` |
| `camera` | `pitch`（56°）、`distance`（11 公尺）、`fov`（38°）、`follow`（追上主角的速度）、`lookAhead` |
| `sprites` | `glow`、`lean`（24°）、`shadow`（圓影半徑）、依種類的 `heights`（一格大的圖：player、monster、npc、item、door、event、other）、`paintedHero`、`paintedHeroHeight` |
| `looks` | 每種外觀的 `sky`（只照明：`"background": false`，塔站在黑暗中）、`exposure`、`light`（太陽：dir、color、intensity、ambient） |

## 5. 計畫

| 步驟 | 內容 | 狀態 |
|---|---|---|
| 1. F01 原型 | 外觀繪製工具、木外觀、`stage3d.json`、主程式的第二個舞台、跟隨鏡頭、站在場景中的地圖物件、會轉身的手繪主角、點擊 3D 地面、情境 `walk_3d` | **2026-11-21 已做好** |
| 2. 每一層，沒有 2D | 10 種外觀都畫好；進入樓層時從格子地圖做出模型；移除 2D 地圖、它的鏡頭與設定列；載入圓圈；瀏覽器也開 | **2026-11-21 已做好**（11-22 樓梯做成台階並有自己的光；第一版計畫中還沒做的：門做成門框；擋在主角前的牆變淡） |
| 3. 地圖上的手繪圖 | 怪物用戰鬥圖（放大，播 idle）、人物用全身手繪圖、物品與鑰匙用手繪圖示；主角的走路動畫與格子之間的平順移動（現在像 2D 一樣一次跳一格） | |
| 4. 氣氛 | 火把（點光源與火焰）、光束、灰塵與火星、一點霧、每章的調色；樓層橫幅與進場時的慢速鏡頭 | |
| 5. 打磨 | 地圖邊緣的鏡頭限制、事件時短暫的晃動、慢電腦的速度；Android 在擁有者要求時 | |

## 6. 已做好的內容（2026-11-21）

- `src/game/src/floor_mesh.cpp`（＋ `.h`）：從牆的列做出樓層模型，記憶體中的 `.glb`。`BattleStage` 用它讀取 prop
  `{"floor": {"walls", "look", "wallHeight", "groundRepeat"}}`；天空可以只照明不畫出來（`"background": false`）。
- `src/core/game/core/game_walk3d.cpp`：舞台設定、鏡頭、影子、站立的物件、手繪主角、地面的標記、畫面外樓梯的標記、點擊地面、載入圓圈。
  `Game::f_DrawGroundMarks`（game_scene_draw.cpp）畫標記；`MapFloor::f_Render` 把每個物件放進自己的 group。
- `src/game/src/game_session.cpp`：`m_upWalkStage`，在走路場景時畫；`Game::f_SetSceneLoading`。
- `tools/stage3d/paint_looks.py`、`assets/media/stage3d/looks/<look>/`（10 種外觀 × 3 部分：顏色與法線貼圖）、
  `assets/data/stage3d.json`（鍵 `data.stage3d`）。
- 情境 `walk_3d`：F01 是 3D，點擊主角會找到他的格子；`walk_3d_far`：F05 在畫面範圍 ×2 時一樣。
- 設定 > 畫面範圍（2026-11-22）：`Game::f_CycleViewScale`、`GameSettings::m_fViewScale`、文字 `settings.view_scale`。
- **已移除：**2D 圖塊的繪製、它的章節染色（`f_FloorThemeTint`）、2D 鏡頭（`toms::Camera`、`systems/camera.*`、`camera_test`）、
  設定 > 鏡頭（跟隨／分區）與它的文字、設定檔的 `cameraMode` 與 `viewCols`、F1 面板的「可見格數」滑桿、`--walk-3d`、情境的
  `"walk3d"`，以及第 1 步預先建好的 `stage3d/floors/F01.gltf`。**保留：**2D 的牆與地板圖（圖集中的 `wall_<theme>`、`floor_<theme>`）：
  關卡編輯器用它們畫地圖。

## 7. 未決的問題

| 問題 | 預設 | 我的建議 | 回答 |
|---|---|---|---|
| **Q117** 預設每個人都用 3D 樓層，設定裡可選 2D？ | — | — | **每個人都 3D，沒有 2D**（擁有者，2026-11-21：「remove fallback to 2D」） |
| **Q118** 鏡頭：固定（北方在上），還是玩家可以轉 90°？ | 固定 | 固定：地圖是從上往下設計的，「上」維持在上 |  |
| **Q119** 牆高：1 公尺（鏡頭看得到牆後），或更高（1.6 公尺）並讓擋在主角前的牆變淡？ | 1 公尺 | 現在 1 公尺；試 1.6 公尺加變淡 |  |
| **Q120** 地圖上的怪物：用戰鬥圖（一張圖，與戰鬥中相同），還是新畫的小地圖圖？ | 戰鬥圖 | 戰鬥圖 |  |
| **Q121** 村莊與據點畫面也要 3D？ | 不要，保留手繪畫面 | 不要，等樓層做完 |  |
