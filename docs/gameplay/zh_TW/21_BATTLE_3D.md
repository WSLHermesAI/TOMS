> 英文原文：[21_BATTLE_3D.md](../21_BATTLE_3D.md)

# 21 — 3D 戰鬥：3D 場景、2D 角色

> 狀態（2026-11-12）：**B1、B2 已製作**（舞台、站在上面的 2D 角色、攝影機動作），角色的精靈已**直接畫在 3D 場景中**（§4.3：
> 受場景的光照亮、有深度測試）；接著是 B3–B5。擁有者以提案回答了 Q88–Q93（「do it」，2026-11-11）：§7。

## 1. 擁有者的要求

> 「我想讓戰鬥場景使用 3D 攝影機，場景是 3D 模型加上天空盒，怪物和玩家角色保持 2D，但粒子和一些特殊招式要用 3D 的方式做。」

現在的戰鬥（2026-11-11 的截圖）：一張畫好的月下遺跡填滿畫面，弓箭手站在左前方，苔蛙在中間，HUD 在最上層（指示列、HP 條、
準備好 / 防禦按鈕、必殺充能點）。

完成後的戰鬥：

| 部分 | 現在 | 3D |
|---|---|---|
| 場地 | 一張畫（`.anim` 背景，battle.json `"backdrops"`） | 3D 模型（地面、遺跡、樹）和四周的天空 |
| 攝影機 | 沒有：畫面就是那張圖 | 透視攝影機：會輕微呼吸、受擊時搖晃、特殊招式時移動 |
| 主角和怪物 | 2D 動畫畫在螢幕框內 | **仍是 2D**、同樣的動畫，站在 3D 地面上：越近越大，並有影子 |
| 粒子 | 2D，螢幕像素 | **3D**：火花、塵土、魔法在場景中，可在角色前後，會被遺跡遮住 |
| 特殊招式 | 2D 效果 | 3D：箭飛入景深、地上的魔法陣、光柱、劍弧、衝擊波環 |
| HUD、提示、數字 | RmlUi 在最上層 | 不變 |

## 2. 引擎已經有的

| 部分 | 位置 | 用於 |
|---|---|---|
| glTF 模型：PBR、蒙皮、動畫、攝影機 | `GltfRenderer`（src/engine/src/gltf_renderer.*）、[08_GLTF](../../engine/zh_TW/08_GLTF.md) | 場地模型 |
| 模型後面的 HDR 天空，以及它給模型的光照 | `gltf_environment.*`、`Frame::background` | 天空盒和光照 |
| 在精靈和 UI 後面的 3D 場景 | `TitleScene` + `BgfxRenderer::setSceneViews`（標題的城市和海） | 戰鬥用同一個位置 |
| 程序天空（Preetham）和海洋 | `OceanRenderer`、[21_OCEAN_VIEWER](../../engine/zh_TW/21_OCEAN_VIEWER.md) | 之後的海上戰鬥、沒有 HDR 檔的天空 |
| 自己畫進框內的角色 | `FighterView`（fighter_view.h） | 不變：框由 3D 位置算出 |
| 粒子：曲線、顏色、爆發、GPU 或 CPU | `particle_fx.*`（2D：像素的 `vec2` 位置） | 加上 3D 發射器（§4.4） |

## 3. 一張圖說明

```
 天空（HDR，四周）
   遺跡、樹（glTF）                <- 3D，有深度測試
        怪物標記  o  <- 苔蛙的腳；投影到螢幕 -> 牠的 2D 框
   地面（glTF）
            主角標記  o            <- 弓箭手的腳 -> 他的 2D 框（較近：較大）
 攝影機：在主角肩後上方，看向怪物
 HUD（RmlUi）在最上層，和現在一樣
```

## 4. 設計

### 4.1 戰鬥舞台（資料）

battle.json 加上 `"stages3d"`，依樓層選擇，和 `"backdrops"` 一樣：

```json
"stages3d": [
  { "floors": [1, 10], "model": "models/arena_moon_ruins.glb", "sky": "skies/moonlit_night.hdr",
    "camera": { "eye": [-1.6, 1.5, 4.2], "look": [0.4, 0.9, 0.0], "fov": 45 },
    "marks": { "hero": [-1.2, 0, 2.0], "monster": [0.6, 0, -1.0], "monsters3": [[-0.6,0,-1.2],[0.6,0,-1.0],[1.8,0,-1.3]] },
    "light": { "sky": 1.0, "sun": [0.3, -1.0, -0.4], "sunColor": [0.6, 0.7, 1.0] } }
]
```

檔案和其他檔案一樣放進 resources.json（同一類用 `pattern`）。沒有 3D 舞台的樓層、沒有網格著色器的後端，或「2D 戰鬥」設定，
都保持現在畫好的背景。

### 4.2 攝影機

`BattleCamera`：每個舞台一個基本鏡頭（如上），上面再疊加動作，每個都是資料：

| 動作 | 何時 | 做什麼 |
|---|---|---|
| 呼吸 | 一直 | 幾公分的緩慢晃動，讓畫面有生命 |
| 搖晃 | 受擊、踩地 | 短暫的 3D 搖晃（強度依傷害），取代現在的螢幕搖晃 |
| 推近 | 拉弓蓄力、魔杖的最後一個法術 | 緩慢推向目標 |
| 環繞 | 特殊招式、魔王登場 | 繞目標轉一段弧，再回來 |
| 慢動作 | 最後一擊 | 攝影機和效果的時間變慢 |

special_moves.json 和怪物的攻擊用名稱指定動作（`"camera": "orbit"`），和現在指定效果一樣，是可重用的組件
（[16](16_SKILLS_AND_PLAYS.md)）。

### 4.3 站在 3D 裡的 2D 角色

每個角色在地上有一個標記（腳的位置）。角色是 **3D 場景中的物件**（擁有者，2026-11-12：「應該把 2D 物件當成 3D 物件……
動態光也要影響 2D 物件」）：

它的 2D 精靈**直接畫在 3D 場景中**，中間沒有任何圖（擁有者，2026-11-12：「應該像以前一樣當 2D 東西畫，只是調整比例讓它看起來是 3D」）：

1. 遊戲照 2D 畫面的方式畫角色（`FighterView::f_Draw` 畫進版面的框：動畫、武器、[18](18_FIGHTER_VIEWS.md) 的 2D/3D 角色切換，
   全都不用改），但包在一個**世界群組**裡（`IRenderer::f_BeginWorld` / `f_EndWorld`）。群組的矩陣（`Game::f_Battle3DSpriteMatrix`）
   把每個設計像素變成以公尺計的點：框的底部中間在角色的腳上，框的高度等於角色的身高，平面直立且與螢幕平行。
2. 2D 渲染器（`BgfxRenderer`）保留群組以公尺計的三角形（`f_WorldGroups`）：每個四邊形和網格，依繪製順序，按同一貼圖和混合方式
   分段。不畫在螢幕上，也不會被切掉：舉起的手、頭上的弓、衝出框外的動作都用同樣方式擺放。
3. 遊戲畫完後，主程式把群組交給舞台（`BattleStage::f_DrawSprites`），舞台用模型自己的著色器畫每一段（`GltfRenderer::f_DrawSprites`）：
   **受**月光、火盆和受擊閃光**照亮**（PBR，平面的法線朝向攝影機），**有深度測試**（前面的遺跡會遮住角色），alpha 混合，
   並和場景其他混合繪製一起依距離排序（邊緣平滑；近的角色蓋在遠的上面）。一點自身的光（`"glow"`，÷ 曝光）讓它在暗處仍看得清楚；
   加法混合的部分（發光）不受光，直接加上它的光。
4. 舞台的火也是世界群組，不受光：粒子在火盆處（每公尺 100 px），站在前面的角色會遮住它。

世界群組裡，圖層的效果（發光、動態模糊、外框：[18 節點效果](../../engine/zh_TW/18_NODE_EFFECTS.md)）直接畫出、不套效果，
GPU 模擬的粒子不畫（都是螢幕空間的功能；B3）。每個角色腳下的地面畫一個柔和的圓影子。角色的螢幕框仍由標記投影算出，
所以提示、傷害數字和現在的 2D 受擊效果都在原位。渲染器無法擺放精靈（`f_SupportsWorld` 為 false）或資料列寫
`"sprites": {"off": true}` 時，角色照 B1 的方式畫在舞台上方的螢幕上。

### 4.4 3D 粒子

`.particle` 格式加上 `"space": "3d"` 發射器：同樣的曲線、顏色、範圍、爆發和混合，位置以公尺計（`vec3`），重力以 m/s² 計，
還有 3D 形狀：

| 形狀 | 用於 |
|---|---|
| 點 / 球 / 錐 | 火花、衝擊、塵土、碎片 |
| 地面環 | 衝擊波、落地塵土 |
| 軌跡（帶狀） | 箭的尾跡、劍弧、火球的尾巴 |
| 地面貼花 | 魔法陣、焦痕、冰霜 |
| 閃光燈 | 短暫照亮場地的點光源（受擊、法術） |

它們在 3D 繪製中面向攝影機，和場地做深度測試，在場地之後、HUD 之前。效果從角色的標記、動畫的骨骼（反投影回世界）或舞台上的點開始。
戰鬥編輯器（[13](13_BATTLE_EDITOR.md)）之後再加預覽（B5）。

### 4.5 3D 的特殊招式

| 招式 | 3D 版本 |
|---|---|
| 弓：蓄力射擊 | 箭帶著尾跡飛入景深，放開時攝影機推近，怪物處爆開並有一圈塵土 |
| 弓：聚集與爆炸（[20](20_BATTLE_EFFECTS.md) §4） | 光點在怪物周圍聚成球，再在地上擴散衝擊波環 |
| 魔杖：法術 | 目標腳下的魔法陣、光柱、在不同深度環繞的光球 |
| 劍：斬擊 | 劃過怪物的 3D 弧形帶，火花飛進場景 |
| 怪物攻擊（[20](20_BATTLE_EFFECTS.md) §5） | 大型怪物的投射物**朝攝影機**飛來，依威力搖晃，踩地時腳下揚塵 |

### 4.6 保持 2D 的

HUD 和所有 RmlUi 畫面、角色上方的提示（魔杖的圓圈、攻擊前的「!」、目標線）、傷害數字和教學。它們是螢幕資訊，保持清楚好讀，
跟著角色的投影位置移動。

### 4.7 平台和速度

先做 Windows（[CLAUDE.md](../../../CLAUDE.md)）。場地是小場景：一個模型、一個天空、幾盞燈。粒子預算和現在一樣。Web 和 Android
保持 2D 背景，直到擁有者要求（Q92）；之後以海洋的畫質分級（[21_OCEAN_VIEWER](../../engine/zh_TW/21_OCEAN_VIEWER.md)）為範本。

## 5. 階段

| 階段 | 內容 | 驗證方式 |
|---|---|---|
| **B1** ✅ 已製作 | 舞台：場地模型 + 天空 + 基本鏡頭；2D 角色站在標記上並有影子；HUD 照舊；可退回畫好的背景 | 戰鬥情境的 3D 參考圖，以及關閉 3D 時的舊圖 |
| **B2** ✅ 已製作 | 攝影機動作：呼吸、3D 搖晃、推近、環繞、慢動作；由招式和攻擊指定 | 固定時間點的情境截圖 |
| **B3** | 3D 粒子（§4.4），並把現在的戰鬥效果改成 3D：受擊、火花、投射物 | 粒子單元測試、情境截圖 |
| **B4** | §4.5 的特殊招式 | 每種武器的情境截圖 |
| **B5** | 戰鬥編輯器：預覽中顯示 3D 舞台、攝影機動作和 3D 效果 | 編輯器自我測試 |

## 6. 美術

參考圖是畫風、月夜、略寫實的遺跡。同樣氛圍的 3D 舞台：

| 來源 | 合不合 | 成本 |
|---|---|---|
| Poly Haven（CC0）：夜晚 HDRI 天空、石地貼圖、掃描的遺跡和岩石模型 | 接近圖中氛圍 | 免費；檔案大，要為遊戲縮減 |
| 低多邊形素材包（Quaternius、Kenney；CC0） | 玩具感，不是畫風 | 免費、小 |
| 在這裡做簡單舞台：地面加幾面遺跡牆，用 AI 畫的貼圖，遠方的樹用卡片 | 畫風、量身打造 | 花時間；近看較平 |
| 3D 美術師 | 完全符合遊戲風格 | 花錢 |

提議（Q88）：先用 Poly Haven 做 1–10 樓的一個舞台，判斷效果後再做更多。

## 7. 決定（擁有者，2026-11-11：「do it」，即提案）

| 問題 | 回答 |
|---|---|
| Q88 舞台美術 | Poly Haven（CC0）：先做 1–10 樓的月下遺跡，判斷效果 |
| Q89 幾個舞台 | 每個樓層區段一個（`"from"`、`"to"`），和畫好的背景一樣；魔王樓層可以有自己的 |
| Q90 3D 裡的 2D 角色 | 投影框（B1）；**2026-11-12 起精靈直接畫在 3D 繪製中**（擁有者：動態光必須照亮 2D 角色；接著：不用卡片圖，精靈本身擺進 3D，§4.3） |
| Q91 攝影機動多少 | 一直輕微；推近和環繞只用在特殊招式、魔王登場和最後一擊 |
| Q92 Web 和 Android | 畫好的背景，直到擁有者要求。**Web：2026-11-11 起開啟**（擁有者：「turn it on」）；Android 仍關閉 |
| Q93 3D 粒子 | 同樣的 `.particle` 檔，`"space": "3d"` 發射器 |

## 8. 已製作的（B1、B2）

| 部分 | 位置 |
|---|---|
| 舞台建置：把 Poly Haven 的檔案下載到 `Build/art_cache/polyhaven`，寫出 `assets/media/battle3d/moonlit_ruins/`（**1.2 MB**，擁有者的上限 3 MB，2026-11-11；原本 16.9 MB）：夜空（Qwantani Moonrise）存成 `sky.jpg`（1536 px，gamma 2.2，大於 1 的值截斷：51 KB，原本是 4.7 MB 的 `.hdr`）、簡化過的掃描道具（`simplify_gltf.py`，numpy 頂點聚合：樹幹 101,802 → 2,881 個三角形，每個約 3,000）、城堡零件 5,123 個三角形、依種類縮小的貼圖（顏色 / 法線 / 其他：城堡與地面 512 / 256 / 256 px，道具 256 / 256 / 128），JPG 品質 80、城堡的 6 個零件放在一個精簡的 `fort_pieces.gltf`（每個道具用 `"node"` 指定）、40 公尺的長草鵝卵石地面、柔和的圓形影子 | 每個模型打包成一個 `.glb`（`pack_glb.py`：JSON、buffer 與貼圖放在同一個檔案，不重新編碼）：舞台共 10 個檔案，9 個 `.glb` 加 `sky.hdr`；`--loose` 保留 `.gltf` 資料夾 | `tools/battle3d/pack_glb.py` |
| `tools/battle3d/make_moonlit_ruins.py`、`simplify_gltf.py`。舞台：約 56,000 個三角形，GPU 上 71 MB 貼圖 + 17 MB 天空（原本 290,000 和 150 MB） |
| 舞台資料（1–10 樓）：天空、曝光 0.25、月光、地面、影子、13 個道具、攝影機、標記、身高 | `assets/data/battle.json` `"stages3d"` |
| 主程式在精靈後面畫舞台（GltfRenderer，用標題的 view）。**一進入樓層就在背景載入**（讀檔、解碼、天空的預先濾波和半精度打包都在工作執行緒；只有上傳 GPU 在主執行緒），並在戰鬥之間保留，所以戰鬥立即開始。2026-11-11 實測：Release 背景 1.0 秒 + 上傳 0.09 秒；Debug 8 秒 + 1 秒（原本 2 秒 / 17 秒，全在戰鬥開始時）。預先濾波後的天空快取在 `<存檔資料夾>/cache/sky_<hash>.env`（17 MB，每個天空檔做一次）：之後在 Debug 啟動，舞台在 4 秒後就緒，而不是 10 秒。舞台還沒好就開始的戰鬥只顯示黑暗，不會先出現畫好的背景；每場戰鬥的舞台都從黑暗淡入（0.45 秒）。腳本執行會等它載完（每次畫面相同；`--battle-3d-wait=off` 用一般方式） | `src/game/src/battle_stage.*`、`game_session.cpp` |
| 遊戲依樓層選資料列，由標記算出每個角色的框（投影腳和頭；版面的框只提供比例），舞台下不畫原本的背景圖 | `game_combat.cpp` `f_Battle3D*`、`f_PlaceBattle3DBoxes` |
| 攝影機：舞台的鏡頭（窄螢幕較廣）、呼吸、特殊招式的 `"camera"` 區塊變成推近（`"move": "orbit"` + `"deg"`：同時繞一段再回來；star_seal 和 tiger_wind 有用）、第一次危險的特寫、最後一擊慢動作時緩緩推近、搖晃改由攝影機做 | `Game::f_Battle3DCamera`、`special_moves.json` |
| 特殊招式的變暗：舞台的光變暗，不是黑色遮罩 | `Battle3DCamera::m_fDim` |
| 會動的光（2026-11-11，擁有者：「看起來像一張靜態的 3D 圖」）：兩個火盆（Poly Haven 石造火坑），各有一盞閃爍的暖色點光源和自己的火焰（`stage_fire`：火焰、火星、煙；`tools/battle3d/make_stage_fx.py`），畫在所在位置並依距離縮放；雲飄過月亮（月光在 60–100 % 之間變化）；場景中的每個戰鬥效果都會在最近的角色處短暫照亮舞台（打怪物時暖色、主角受擊時紅色）；樓層的光塵和螢火蟲飄在舞台上。battle.json `"fires"`、`"clouds"`、`"flash"`。渲染器可用月光 + 3 盞點光源（這些不投影） | `Game::f_Battle3DLights`、`f_AddBattle3DFlash`、`f_DrawBattle3DStageFx`、`battle_stage.cpp` |
| 怪物即將使出特殊攻擊（危險格子亮起時）：攝影機壓低、靠近並仰望牠（牠佔滿畫面），稍微往旁邊轉一點讓景深顯現，並伴隨震動；在攻擊落下前回到原位，讓格子看得清楚；戰鬥的第一次會配合格子的介紹停留。battle.json 舞台 camera `"danger"`（zoom、height、lookAt、fovAdd、deg、ms、rumble、off）；開啟「減少效果」時不會發生。在攝影機後面的角色不畫 | `Game::f_Battle3DDangerShot`、`f_Battle3DCamera`（"low"） |
| **Web**（2026-11-11）：瀏覽器中開啟舞台。單執行緒版本在進入樓層時於主執行緒製作（那裡會短暫停頓，實測 0.33 秒）；多執行緒版本在工作執行緒。瀏覽器的天空較小（256 px 的面、16 個取樣）且不快取（不在瀏覽器存放 17 MB）；沒有陰影。在這台電腦的 GPU 以 headless Chrome 實測：兩個版本都能載入並畫出舞台（45 次繪製），所有著色器都能編譯。任何裝置：若 3 秒內大部分戰鬥畫面超過 45 ms，本次遊玩改回畫好的背景（`[battle3d] too slow here`）。用 `node tools/web_smoke_test.mjs <build> <out> --battle --gpu` 檢查 | `battle_stage.cpp`、`game_session.cpp`、`main_sdl.cpp`、`tools/web_smoke_test.mjs` |
| 縮放為零的節點不畫（城堡隱藏的零件）：舞台一幀 45 次繪製 + 41 次陰影繪製（原本約 200）；Debug GPU 16.6 → 約 3 ms | `gltf_renderer.cpp` |
| 開關：`--battle-3d=on|off`；桌面版一般遊玩開啟，腳本執行（參考圖）、Web 和 Android 關閉；情境用 `"battle3d": true` 開啟 | `main_sdl.cpp`、`game_scenario.cpp` |
| 角色的精靈在場景中（2026-11-12，§4.3）：畫在世界群組裡（每個像素以公尺擺在角色腳上，面向攝影機），再由舞台用模型的著色器畫出：受月光、火和閃光照亮，有深度測試，alpha 混合，依距離排序；火盆的火同樣方式畫，不受光（左邊的火焰在弓箭手的弓後面）。不會被切掉（第一版把角色畫進卡片圖：圖的邊緣切掉了舉起的手和弓）。實測：怪物攻擊命中時弓箭手的亮度 76 → 117 並轉紅。battle.json 舞台 `"sprites"`（`glow`、`off`）；舞台共 10 個檔案，1.2 MB | `IRenderer::f_BeginWorld`、`BgfxRenderer::f_WorldGroups`、`Game::f_DrawBattle3DFighter` / `f_Battle3DSpriteMatrix`、`BattleStage::f_DrawSprites`、`GltfRenderer::f_DrawSprites` |
| 攻擊提示的位置（擁有者，2026-11-12/13）：不會在怪物上，**在主角頭上**——弓的靶、劍的箭頭與計數、法杖的圓圈、連打都在同一區（設計像素 420 × 120，直向 160 高），置中於主角上方，底部比他的框高出他身高的四分之一（他畫出來的頭比框高約五分之一），並保持在畫面內；弓的第一次提示在這區的上緣。每隻怪物的「!」、下一招、名字加 HP 和短 HP 條放在牠自己頭上（`.fhead`，依牠站的位置） | `Game::f_TouchArea`、`game_ui.cpp`、`battle.rml`、`battle.rcss` |
| 特殊攻擊的蓄勢（2026-11-13，擁有者）：危險格子亮起時，怪物先**蓄力**——牠的計時暫停 3.5 秒，攝影機一直貼近牠，牠播放「power」動畫（蹲低、越抖越劇烈並膨脹、泛紅閃動，3.05 秒時怒吼）、`monster_power` 粒子和 `sfx.monster_power` 低鳴——接著**集氣**直到命中（循環的「charge」動畫，每 0.45 秒 `monster_charge` 能量向內聚集；也就是格子 1.4 秒的警告時間），命中時**爆發**：`monster_unleash`、牠的吼叫 `sfx.monster_roar` 和強烈震動。若打中主角，他會**被擊倒**：正在做的事中斷，播放 `hurt_big` 動畫（被震退、單膝跪地、再站起來：1.6 秒；`tools/art/make_hero_rig2d.py`，每種武器都有），攻擊面板顯示「受到重創」，`downMs` 1600 毫秒內不能開始任何攻擊（攻擊條、武器玩法、必殺、主動技能）；移動和防禦仍可使用（事件 `heroDown`；測試 `scenario.battle_special_down`）。戰鬥中的第一次（格子介紹）把介紹的時間算在 3.5 秒內。被暈眩會失去蓄力。怪物開始的每個效果和聲音都綁在牠身上（`CombatState::Foe::m_vFx`、`m_vSounds`；擁有者：「怪物應該把發射器放在自己的類別裡」），牠倒下時由 battle.json 的 `"onFoeDeath"` 逐一判斷（`fxRules`、`soundRules`：stop、fade 或 keep；蓄力和集氣會停止，爆發和吼叫會播完）：`Game::f_FoeEffectsOnDeath`，是專案規則（CLAUDE.md「Battle effects belong to their monster」）。沒有這些動畫的外觀改播「special」和待機。battle.json `feelFx` 的 `"specialAttack"`（powerMs、powerFx、powerSound、chargeFx、chargeFxEveryMs、unleashFx、shout、shakePx、shakeMs、downMs、off）；戰鬥事件 `power`、`unleash`、`heroDown`。動畫由 `tools/make_monster_battle_anims.py` 與 `tools/art/make_monster_rig.py` 產生，粒子由 `tools/make_battle_fx.py`，聲音由 `tools/audio/make_sfx.py` | `Game::f_StartFoePower`、`f_UnleashFoePower`、`f_FoeCharging`、`f_StepFoes` |
| 體型與樓層頭目（2026-11-13，擁有者）：每隻怪物都有 `enemies.json` 的 `"body"` 1-5（1 小：史萊姆、蝙蝠；2 大多數；3 大：石巨人、苔蛙；4-5 頭目），battle.json 的 `"bodyScale"` 決定牠在 3D 舞台上的身高（1：0.75、2：1、3：2、4：2.4、5：2.8 倍的舞台 1.6 公尺）。頭目是體型 3 以上、有特殊招式 `boss_near`、`boss_line`、`boss_fan`（moves.json；格子、蓄力、集氣、爆發、擊倒，和苔蛙一樣）。十個頭目站在 F01-F10（最前面的樓層）通往上樓梯的最後一格（擁有者：「改成 F01-F10，我只是想測試」；圖例 "X"）：F01 蜂巢熊 3、F02 蕈菇巨像 3、F03 晶岩龜 4、F04 魔典牛 4、F05 守墓骷髏騎士 3（特別的、保持小）、F06 爐火狐 4、F07 小斯芬克斯 4、F08 冰晶熊 5、F09 星辰梟法師 5、F10 時計騎士 5；前期的數值（HP 160，每層加 20 到 340，ATK 20-30，DEF 2-5）；F06 起也會用重擊。F70 的最終頭目沃卡司也是體型 5 並有頭目招式，並且和牠們一樣是彩繪的（`tools/art/make_boss_concept.py`：戴紅色兜帽的魔王；外觀 `vorkath`）。十個頭目各自**守住上樓的樓梯**（`enemies.json` 的 `"guardsStairs"`）：頭目還沒被打倒時樓梯是鎖著的；撞到樓梯會說「{name}擋住了上樓的樓梯」，打倒牠之後會說樓梯開了（`Game::f_StairGuard`；測試 `scenario.stairs_boss_guard`）。美術：各是 `make_monster_concepts.py` 中一隊的第三隻，彩繪骨架由 `tools/art/make_monster_rig.py` 產生（各自一個 atlas 和 .anim，`looks.json`），32 px 地圖圖在 `art_raw/sprites`（重建遊戲 atlas）。測試 `scenario.battle_floor_boss` | `Game::f_Battle3DHeight`、`enemies.json`、`moves.json`、`story/floors/F01-F10` maps |
| 怪物在 3D 舞台上的身體比例（2026-11-13，擁有者）：`enemies.json` 的 `"scale3d"`（1 = 舞台的身高：`"heights"` 的 `monster` 1.6 公尺、`big` 3.0 公尺）乘上牠在場景中的身高；牠的圖、影子和頭上的資訊塊都跟著變。教學的苔蛙設為 2（3.2 公尺） | `Game::f_Battle3DHeight`、`assets/data/enemies.json` |
| 測試：`scenario.battle_3d`（舞台有畫出；主角較近：較高、腳較低）、`smoke.battle_3d`（參考圖 `tests/golden/battle_3d.png`，2026-11-12 為場景中的精靈更新） | `tests/` |

還沒做：2D 粒子和受擊效果仍在螢幕空間、角色的投影位置播放，蓋在場景上（B3 會改成 3D）；戰鬥編輯器的預覽仍是畫好的背景（B5）。
