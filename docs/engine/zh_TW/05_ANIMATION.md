# 05 — 節點動畫（`.anim`）

> 英文原文：[05_ANIMATION.md](../05_ANIMATION.md)

用打包好的 sprite atlas（[04](04_ATLAS_TOOL.md)）建立的動畫：一棵節點樹，每個節點有
位置 / 旋轉 / 縮放 / 顏色的 key、sprite 切換和事件。用於角色動作、戰鬥
場景、簡單的特效。它用節點階層取代 FM79979 的 MPDI（扁平的路徑清單）：
移動一個群組就會移動裡面的所有東西，而樹決定誰畫在上面。

**編輯器的畫面由遊戲的渲染器繪製。** Atlas、動畫和粒子編輯器用 toms_game 自己的 `BgfxRenderer`
繪製它們的畫布（`tools/studio_common` 中的 `GameCanvasView`，函式庫
`studio_bgfx`），所以編輯器顯示的和遊戲顯示的逐像素相同：
- **和遊戲相同的渲染：** 點取樣的 sRGB 貼圖、straight-alpha 和加法
  混色、相同的 sprite 批次，粒子則用 GPU 模擬。
- **遊戲內容**（atlas 頁面、正在編輯的 sprite、片段的 sprite、粒子）由
  渲染器繪製在畫布的原生視窗中。
- **編輯用的視覺元素**（外框、選取、控制點、軸心、9-slice 參考線、gizmo、標籤、
  狀態文字）保留它們的 QPainter 程式碼，以透明覆蓋層畫在上面。
- **每個編輯器行程一個 bgfx。** 它跟著正在顯示的畫布：atlas 編輯器的頁面和
  sprite 編輯畫布共用它。
- **座標提示**（檢視 > *顯示座標*，預設開啟），和 Cocos Creator 的場景視圖一樣：
  淡淡的格線，x 值沿著下邊緣，y 值沿著左邊緣。間距
  跟著縮放（1、2、5 × 10ⁿ 內容像素，至少相隔 90 螢幕像素）：縮小時每 500，
  放大時每 10 或每 1。數值是遊戲的座標（y 往下增加）。
  滑鼠在畫布上時也會顯示它的位置：游標旁的「x, y」、穿過游標的淡色輔助線，以及底邊與左邊數值間的醒目標籤
  （放大 8 倍以上時顯示到 0.1 像素）。測試：`studio.canvas_coordinates`（offscreen）。
- **復原 / 重做和備份**（三個編輯器都有）：
  - **復原 / 重做：** 編輯 > 復原 / 重做（Ctrl+Z / Ctrl+Y，或工具列箭頭）逐步走過
    每次編輯。
  - **歷程面板**（檢視 > 歷程；它是屬性旁邊的分頁）：列出每一步，最舊的
    在前。點一步就會復原或重做到那一步。
  - **自動備份：** 每 5 分鐘（只在有變更時）以及每 20 次編輯後寫一份副本。
    - **位置：** `%LOCALAPPDATA%\TOMS\<Editor>ackups\<file>\<file>_<date-time>.<ext>`，絕不
      放在檔案旁邊。
    - **數量：** 每個檔案保留最新的 20 份。
    - **開啟備份：** 備份中的 atlas 和圖片路徑是絕對路徑，所以可以直接從那裡開啟。
      另存新檔就能放回去。Atlas 備份是一個資料夾：專案和它打包好的 atlas。
    - **設定：** 檔案 > 備份 > 設定（每 N 分鐘、N 次編輯後、保留幾份、開
      / 關）。同一個選單有 *立即備份* 和 *開啟備份資料夾*。
- **QPainter 後備：** 檢視 > *用遊戲渲染器預覽*（預設開啟；重新啟動後生效）、
  離螢幕的 `--selftest`、bgfx 啟動失敗，以及獨立建置的 atlas
  工具（沒有遊戲引擎），都和以前一樣用 QPainter 繪製。
- **`--selftest-gpu`**（在螢幕上）用遊戲渲染器檢查每個編輯器，並存下 bgfx
  截圖：
  - `atlas_editor --selftest-gpu project.atlasproj outdir`：頁面、sprite 編輯模式，再回來；
  - `anim_editor --selftest-gpu clip.anim outdir`；
  - `particle_editor --selftest-gpu effects.particle outdir`。


**現況：** 執行期和檔案格式（第 1 階段）以及編輯器 `anim_editor`（第 3 階段，見
[編輯器](#編輯器)）已完成。接下來：一個以外掛為基礎、同時容納 atlas 和動畫
編輯器的 studio 應用程式（第 2 階段），以及多軌時間軸（第 4 階段）。

**手寫或用 AI 撰寫片段**（淡入/淡出、移動、踏進-揮擊-退回的攻擊）：
見 [06_ANIMATION_RECIPES.md](06_ANIMATION_RECIPES.md)。

| 部分 | 位置 |
|---|---|
| 資料、`.anim` 載入/儲存、求值 | `src/core/engine/anim_clip.h/.cpp`（`toms::anim`；`PoseCache`、`evaluate`） |
| 播放 + 繪製 | `src/core/engine/anim_player.h/.cpp`（`AnimPlayer`、`appendQuads`） |
| 緩動 | `src/third_party/tweeny/easing.h`（Tweeny，MIT）：Robert Penner 的緩動函式 |
| 編輯器 | `tools/anim`（`anim_editor`，Qt 6），共用的 Qt 程式碼在 `tools/studio_common` |
| 測試 | `anim_clip_test`（CTest `unit`）、`smoke.anim`（截圖）、`anim.check_preview` |

## 檔案

```json
{
  "version": 2,
  "atlases": ["../media/atlas/game.atlas", "fx/slash.atlas",
              {"id": "dark16", "path": "../styles/dark16/atlas/game.atlas"}],
  "clips": [{
    "name": "slime_attack", "length": 0.6, "playCount": 1, "stayAtLastFrame": true,
    "root": {
      "name": "slime", "sprite": "game:slime",
      "tracks": {
        "pos":    [{"t": 0, "v": [0, 0, 0], "ease": "quadraticOut"}, {"t": 0.3, "v": [24, -8, 0]}, {"t": 0.6, "v": [0, 0, 0]}],
        "rot":    [{"t": 0, "v": [0, 0, 0]}, {"t": 0.3, "v": [0, 0, -10]}, {"t": 0.6, "v": [0, 0, 0]}],
        "scale":  [{"t": 0.3, "v": [1.2, 0.8, 1], "ease": "backOut"}, {"t": 0.6, "v": [1, 1, 1]}],
        "sprite": [{"t": 0, "v": "game:slime"}, {"t": 0.3, "v": "dark16:slime_bite"}],
        "event":  [{"t": 0.3, "v": "hit"}]
      },
      "children": [
        {"name": "shadow", "sprite": "game:shadow", "order": -1, "inheritColor": false},
        {"name": "fx", "sprite": "slash:slash_0", "blend": "add",
         "tracks": {"visible": [{"t": 0, "v": false}, {"t": 0.3, "v": true}, {"t": 0.45, "v": false}]}}
      ]
    }
  }]
}
```

- **`atlases`：** sprite 來源的 `.atlas` 檔，**依查找順序排列**，每個都有一個
  **id**。單純路徑的 id 是不含 `.atlas` 的檔名（`fx/slash.atlas` → `slash`）；物件
  `{"id": "...", "path": "..."}` 則另外命名——當兩個 atlas 檔名相同時需要這樣做，
  例如美術風格的 `game.atlas` 和原版的放在一起（只有 id 不是預設值時才會寫出）。
  兩個 atlas 用同一個 id 是解析錯誤。路徑永遠**相對於
  `.anim` 檔**，所以 `.anim` 和它的 atlas 放在同一個磁碟上相關的資料夾中；
  編輯器永遠不會寫入絕對路徑。舊檔案中單一的 `"atlas": "..."` 仍然能讀（視為只有一項的清單），
  寫回時會變成 `"atlases"`。
- **Sprite 引用**（節點的 `sprite`、`sprite` 軌的值）是 `"atlasId:name"`——
  那個 atlas 的 sprite，就像 MPDI 的 PIName + ImageName——所以一個節點可以逐 key 在
  不同 atlas 的 sprite 之間切換，而同一個名稱也可以來自任一個 atlas。指名的 atlas 如果
  沒有那個 sprite，就什麼也不畫（沒有後備）。**不帶前綴**的 `"name"`（舊檔案）會依序在
  每個 atlas 中查找，第一個有的勝出。編輯器寫出的每個引用都帶有前綴。
  如果遊戲用來繪製的 `AtlasSet` 中沒有那個 id 的 atlas，會退回不帶前綴的查找方式。
- **片段：** `length` 以秒為單位（0 或沒有 = 到最後一個 key 為止）；`playCount`（-1 = 永遠循環）；`stayAtLastFrame`。
- **每個節點都在片段唯一的時間軸上執行。** 子節點的 key 可以在和父節點不同的時間
  開始和結束：父節點最後一個 key 之後，父節點維持它最後的姿勢，子節點則繼續
  動畫（在父節點維持的變換之內）。`length` 為 0 時，片段持續到**任何**節點的最後一個
  key。固定的 `length` 會在那個時間截斷每個節點：之後的 key 永遠不會播放（
  問題面板會警告：「key at …, after the clip's length」）。
- **節點：** `name`、`sprite`（沒有 = 只移動它的子節點的群組）、靜止值 `pos`
  `rot` `scale` `color` `visible`、`pivot`（sprite 的 0..1，左上 = 0,0；沒有 =
  atlas 的 `x_pivot`，再沒有就是中心）、`order`、`blend`（`normal` / `add`）、`inheritColor`、`loop`、`stayAtLastFrame`、`children`；
  版本 3 另有 `skin`、`lattice` 與 `meshGrid`（見下文與 [16_MESH_ANIMATION.md](16_MESH_ANIMATION.md)）；
  版本 4 有 `fx`（它的子樹透過著色器畫出：動態模糊、發光、閃白、外框）與 `trail`（殘影），見 [18_NODE_EFFECTS.md](18_NODE_EFFECTS.md)。
- **節點如何播放**（每個節點各自獨立；它的範圍是它子樹的第一個..最後一個 key）：

  | 設定 | 節點最後一個 key 之後 | 用途 |
  |---|---|---|
  | 預設 | 維持最後的姿勢：仍然繪製，不再更新 | 跳出來後停留的部件 |
  | `"stayAtLastFrame": false` | 和它的子節點一起隱藏 | 會消失的閃光或爆發 |
  | `"loop": true` | 一直重複播放第一個..最後一個 key，**片段結束之後也會** | 閃爍、繞圈飛的金幣、上下浮動的箭頭 |

  - **循環在片段結束後仍會繼續。** 只播放一次的片段，和以前一樣在它最後一個
    key 結束：遊戲的 `finished()` 為 true，其他節點維持或隱藏。循環
    節點在片段顯示期間會一直播放（`AnimPlayer::poseTime()` 持續
    前進）。
  - **循環的群組會循環它整個子樹**，子節點自己的設定在群組的時間內套用。
  - **事件 key** 只在片段的時間軸上觸發；不會隨著循環節點重複。
  - **範例：** [examples/anim_child_timing.anim](../../examples/anim_child_timing.anim) 中的 `popup` 片段。
- **軌：** `pos` `rot` `scale`（皆為 **x, y, z**）、`color`（r, g, b, a 0..1）會內插；`sprite` `visible` `event` 是階梯式。沒有 key 的通道
  維持靜止值。第一個 key 之前維持那個 key，最後一個之後維持最後一個。
- key 上的 **`ease`** 決定從那個 key 開始的區段形狀：`linear`（預設）、`stepped`（維持），或
  `quadratic` `cubic` `quartic` `quintic` `sinusoidal` `exponential` `circular` `bounce`
  `elastic` `back` 任一種 + `In` / `Out` / `InOut`（https://easings.net），或像 CSS `cubic-bezier()` 一樣的
  三次貝茲曲線 `[x1, y1, x2, y2]`。`back`、`elastic` 和 `bounce` 會超出範圍；顏色會被限制在範圍內。

**x, y, z：** 位置、旋轉、縮放都有三個值，雖然遊戲目前以 2D 繪製，資料已可用於 3D 攝影機。
- **位置：** 像素，y 往下，**z 往螢幕內**。
- **旋轉：** 度，不會折返（0 → 720 是兩圈）。**z** 在螢幕平面上轉動節點（順時針）；**x** 和 **y**
  讓它傾出平面。順序是先 z、再 y、再 x（R = Rx · Ry · Rz），與舊引擎的 `cMatrix44::RotationMatrix`
  相同，所以 MPDI 的角度可直接沿用。
- **縮放：** x, y, z；x 或 y 為負會鏡像。
- **目前的繪製：** 世界變換是 3D 的（`NodePose::world`，4×4 矩陣）。遊戲和編輯器用沿 z 看的正交攝影機
  繪製（`flatten()`）：z 不會移動 sprite，x / y 傾斜會讓它縮短（60° 時顯示為一半）。
- **舊檔案：** 版本 1 的檔案存 `[x, y]` 和單一的 `rot` 數字。仍可讀取（z = 0、縮放 z = 1、該數字為 z
  旋轉），儲存時寫成版本 2。

**蒙皮（版本 3）：** sprite 節點可以用片段中的其他節點（它的骨頭）彎曲它的 sprite 的三角網格：
`"skin": {"bones": ["hip", "chest", "head"], "weights": "auto"}`。網格來自 sprite 所屬圖集旁的網格檔
（`game.atlas` → `game.mesh.json`，二進位時另有 `game.mesh.bin`），由所有用到該圖集的片段共用。動畫工具把片段需要的網格
加進去：編輯器在儲存片段時，圖集重建或換皮後則用 `anim_editor --headless meshes`。`"meshGrid": [cols, rows]`（各 1–32）設定節點的 sprite 切割用的格子；
沒有時用 8 像素的格子。`"auto"` 權重由靜止姿勢產生；手繪的權重是每個頂點一組 `[[bone, weight, ...], ...]`。
只有節點有蒙皮、晶格或網格格數時檔案才存成版本 3，所以其他檔案在舊版程式中照樣能開。沒有網格的蒙皮 sprite 以四邊形繪製
（檢查會回報為錯誤）。在編輯器中
是 Properties 下的 **Skin（蒙皮）** 欄位（骨頭名稱，以逗號分隔）；欄位下方的說明列出 sprite 的網格、骨頭與權重，或它
為何以四邊形繪製。**檢視 > 顯示蒙皮**（Shift+K）在每個蒙皮節點上畫出它的網格、依所跟隨骨頭上色的頂點（依權重混色），
以及帶名稱的骨頭；選取一根骨頭時，頂點從亮（完全跟隨它）到暗（不跟隨）著色。詳見 [16_MESH_ANIMATION.md](16_MESH_ANIMATION.md)。

**編輯器中的蒙皮流程。**
- **骨架 = 一個精靈圖節點加上它底下的骨頭**（`golem` > `hip` > `chest` > `head`）。這樣移動精靈圖時，網格與骨頭會
  一起移動。骨頭在節點外的蒙皮，Properties 會顯示警告與 **把骨頭移到此精靈圖底下** 按鈕，按下後骨頭改變父節點，
  畫面上不會有任何移動。
- 工具列與編輯選單中的 **物件模式（O）** 與 **骨頭模式（B）** 決定在檢視區點選會選到什麼：
  - 物件模式選取精靈圖，和以前一樣。
  - 骨頭模式顯示蒙皮，只會選到骨頭（關節或骨頭線），不會選到底下的精靈圖。移動／旋轉／縮放控制點作用在骨頭上：
    自動關鍵影格開啟時擺動骨頭（設定關鍵影格），關閉時把骨頭放到精靈圖上（它的靜止姿勢）。
- **網格格數** x／y（Properties，蒙皮或變形節點）設定節點的 `meshGrid`：依精靈圖的 alpha 以這麼多格重新切割網格
  （各 1–32；沒有設定時用 8 像素的格子，所以 32 像素的精靈圖從 4 × 4 開始）。格數越多彎曲越平滑。這是一個復原步驟；
  網格立即重新切割，儲存片段時加進圖集的網格檔，和其他格數的網格並存（`golem`、`golem@8x8`）。
- **晶格模式（L）** 用變形晶格彎曲 sprite（預設 4 × 4；**Properties → 晶格** 設定大小）：拖曳一個點，就在播放頭設定變形關鍵影格。
  關鍵影格清單的 **Deform（變形）** 欄列出移動過的點（`5: 0, -2; 6: 0, -2`）。見 [16_MESH_ANIMATION.md](16_MESH_ANIMATION.md) §4.3。
- **編輯會暫停播放。** 片段播放中在檢視區拖曳或做任何編輯（輸入數值、關鍵影格）時會先暫停，所以開啟自動關鍵影格時，
  編輯只會在畫面上的這一格設定一次關鍵影格，而不是播放頭經過的每一格。
- **滑鼠滾輪只會在點選（或按 Tab 進入）數值欄位後才改變它的值。** 游標停在未取得焦點的數值框、下拉選單或滑桿上時，
  滾輪會捲動它所在的面板（Properties 面板在欄位上也能捲動）；捲軸與清單一律接受滾輪。所有 Qt 編輯器都是如此（`Theme.cpp`，`WheelNeedsFocus`）。
- 隱藏的蒙皮節點（靜止的 `visible` 關閉、父節點隱藏、visible 關鍵影格）不顯示蒙皮疊圖，也沒有可選的骨頭。

**空間和順序：** 節點的世界變換 = 父節點的 × 平移 · 旋轉 · 縮放。繪製順序是深度優先：先畫 `order < 0` 的子節點（依 order，
再依清單位置），然後是節點自己的 sprite，再來是 `order >= 0` 的子節點。顏色沿著樹
往下相乘，除非 `inheritColor` 為 false；隱藏的節點會隱藏它的子樹。

## 在遊戲中

```cpp
toms::anim::AnimFile file;  toms::anim::parseAnim(text, file, &err);
toms::anim::AnimPlayer p;   p.play(file.find("slime_attack"));
// every frame:
p.update(dtMs);
for (const std::string& e : p.takeEvents()) { /* "hit": damage, sound ... */ }
p.draw(ren, game.spriteAtlas(), toms::anim::placement(x, y, scale));   // one atlas
// several: toms::anim::AtlasSet set; set.add(atlasA, "game", pageTexturesA); set.add(atlasB, "fx", pageTexturesB);
//          p.draw(ren, set, ...)  -- ids and lookup order = the file's "atlases"
```

（每個畫面：`update`、取出事件（例如 `"hit"`：傷害、音效…）、`draw`。多個 atlas 時用 `AtlasSet`，id 和查找順序 = 檔案的 `"atlases"`。）

同一個 atlas 頁面上的 sprite 共用一張貼圖（`Quad::texture`；遊戲自己的 sprite atlas 是
`kSpriteAtlasTexture`），所以任何深度、旋轉和縮放過的片段，仍然只用少數幾個批次繪製
（`Quad` 為此帶有四個角；只有在貼圖或加法混色改變的地方才多一個 draw call）。
每個事件只觸發一次，循環折返時也一樣；時間 0 的事件在第一次 `update` 時觸發。

**只更新有 key 的東西。** 每個節點保存它目前的位置、旋轉、縮放、
顏色、sprite 和可見性（`PoseCache`，從靜止值開始），每個通道是
自己的 key 陣列（`posKeys`、`rotKeys`、`scaleKeys`、`colorKeys`、`spriteKeys`、`visibleKeys`）。
一個畫面只推進有 key 的陣列，每個都有游標（往前播放時不需要搜尋）：只有 sprite key
的節點永遠不會重新計算它的變換，沒有 key 的節點在第一個畫面之後永遠不會被碰到，
維持一個值的軌（第一個 key 之前、最後一個之後、階梯區段）只花一次比較。世界變換 / 顏色只在
節點自己的值或父節點的值改變時才重建，`AnimPlayer` 也只在節點姿勢改變時
（或位置 / 色調 / atlas 集合改變時）才重建它的四邊形。`evaluate()` 是無狀態的
版本（編輯器的預覽）；測試會檢查兩者在任何跳轉順序下都給出相同的姿勢。

**試試看：** `toms_game --anim=<file>#<clip>` 在畫面上的任何東西上方置中播放一個片段，並
記錄它的事件（`[anim] event hit`）。`tests/smoke/anim_preview.anim` 是截圖測試用的片段。

### 怪物的待機動畫

地圖上的每隻怪物都會循環播放待機片段，而不是站著不動。片段放在
[`assets/media/anim/monster_idle.anim`](../../../assets/media/anim/monster_idle.anim)，每種敵人一個，名稱是
`idle_<敵人 id>`（`assets/data/enemies.json` 中的 id）：用 `anim_editor` 開啟這個檔案就能修改，新的敵人則加一個
`idle_<id>`。敵人等級越高，待機動作越好笑：

| 片段 | 待機 |
|---|---|
| `idle_slime` | 慢慢擠壓伸展地呼吸 |
| `idle_bat` | 快速盤旋，翅膀拍動時左右收縮 |
| `idle_skeleton` | 搖擺、發抖、牙齒打顫 |
| `idle_golem` | 沉重地呼吸，然後跳起再重重踩地，揚起塵土（fx `smoke`） |
| `idle_wraith` | 在脈動的紫色光暈中漂浮、閃爍、傾斜（fx `glow`） |
| `idle_demon` | 小小的舞步：跳兩下、轉頭看另一邊，每次落地都冒出火花（fx `spark`） |
| `idle_demonlord_vorkath` | 一連串快速彈跳的捧腹大笑並漲紅、得意地往後仰、頭上繞著暈眩的星星（fx `star`）、紅色氣場 |

遊戲如何使用它們（`Game::drawIdleAnim`，`game_scene_draw.cpp`）：

- **製作框：** 片段圍繞怪物 32x32 的 sprite 製作，sprite 的軸心在底部中央 (0, 0)；sprite 節點設定
  `"pivot": [0.5, 1]`。特效可以稍微超出框外。
- **擺放：** 遊戲把 (0, 0) 放在怪物矩形的底部中央，並縮放片段，讓 sprite 剛好填滿那個矩形，和原本的靜態
  sprite 一樣。2x1 的石巨人或 2x2 的沃卡司會以同樣方式拉伸整個片段。
- **相位：** 每隻怪物依它所在的格子有自己的相位，所以一整房的史萊姆不會同步呼吸。
- **Atlas：** `game:` sprite 來自載入的遊戲 atlas，所以美術風格仍然適用；`fx:` sprite 來自
  `assets/media/atlas/fx.atlas`。
- **後備：** 沒有檔案、沒有 `idle_<id>` 片段，或使用執行時的 sprite 格（沒有預先打包的 atlas）時，怪物照舊繪製。
- **檢查：** CTest `anim.check_monster_idle`（`anim_editor --headless check`）。

### 開門

[`assets/media/anim/door_open.anim`](../../../assets/media/anim/door_open.anim) 每種門的顏色一個片段：
`door_open_yellow` / `_blue` / `_red`（0.55 秒，播放一次）：鑰匙在鎖中轉動，門晃一下、壓扁，然後在該顏色的閃光和
幾顆火花中沉入地面。玩家持有鑰匙走向門時（`Game::openDoor`），會用掉一把鑰匙，片段在門的格子上播放，格子變成地板，
並記錄為已開啟（`EntityStatus::Opened`）：再次來到這層樓時門仍是開的，也不會再花鑰匙。那一回合玩家停在原地。
擺放方式和待機片段相同（格子底部中央，依門 sprite 的大小縮放）。CTest `anim.check_door_open`。

### 弓（武器片段）

[`assets/media/anim/weapon_bow.anim`](../../../assets/media/anim/weapon_bow.anim) 有弓的四個片段。目前還沒有弓的美術，所以弓是用
fx 圖集的基本形狀上色組成：拉長的 `square` 做弓臂、握把、弓弦、箭桿和箭羽，`dot` 做弓梢，再加上 `glow` / `ring` / `spark` /
`smoke` / `star` 特效。弓朝右，握把在片段原點，高約 95 px。

| 片段 | 長度 | 內容 | 事件 |
|---|---|---|---|
| `idle` | 1.6 秒，循環 | 搭著箭的靜止弓，緩慢起伏，上弓梢閃一下光 | |
| `charge` | 1.2 秒，一次，停留 | 拉弓（0.6 秒），接近拉滿時開始顫抖；之後持續到放箭為止，整把弓因用力而晃動，金色光暈沿著弓脈動，弓臂冒出火花，箭頭發光 | 拉滿時 `charged` |
| `attack` | 0.7 秒，一次 | 從拉滿開始：弓弦彈回並震動，箭帶著殘影飛出 400 px，閃光，弓後座 | 0.06 秒 `release`、0.42 秒 `hit` |
| `miss` | 1.0 秒，一次 | 從拉滿開始：箭滑脫，翻滾著飛不遠就掉落，一團塵土，弓垂下 | 0.08 秒 `miss` |

檔案由 `python tools/make_bow_anim.py` 產生，它會算出兩段弓弦的角度，讓弓弦永遠接在箭尾。要修改時請改腳本後重新執行，
不要手動編輯檔案。遊戲不會單獨播放這些片段：戰鬥會把它們畫在角色手上（見下文）。CTest `anim.check_bow`。

循環節點會循環它底下的所有關鍵影格，所以 `charge` 把弓做了兩份：`drawing`（0–0.6 秒，之後隱藏）和 `held`（0.6 秒起顯示，
同樣的姿勢放在 `shake` 裡；`shake` 是循環群組，關鍵影格都在 0.6–1.0 秒）。片段結束後會繼續晃動，而不會重新拉弓。
腳本中的 `STRAIN` 決定晃動的強度。

### 角色與手持物：兩個檔案，由程式綁在一起

角色的動作與他手上拿的東西是**兩個 .anim 檔**，由遊戲綁在一起：

- [`assets/media/anim/archer_bow.anim`](../../../assets/media/anim/archer_bow.anim)：**只有角色**拉弓射箭的動作
  （`python tools/make_archer_anim.py`）。以 `player` sprite 製作，任何角色都能播放。
- [`assets/media/anim/weapon_bow.anim`](../../../assets/media/anim/weapon_bow.anim)：**只有弓**（見上一節）。

兩者都有 `idle`、`charge`、`attack`、`miss` 片段，時間相同。角色檔的約定是兩個節點名稱：

```
archer
  lean            身體從腳底起的動作：呼吸、後仰、後座、退縮
    body          角色：樞紐在底部中央，腳在 (0, 0)               <- 依角色換掉
    hand          沒有 sprite：手持物畫在這裡——位置、旋轉、縮放     <- 依角色移動
```

- **綁定：** `CharacterRig`（anim_player.h）同時播放角色片段與同名的手持片段。每一格先畫角色，再把手持片段畫在角色的
  `hand` 節點（`attachPlacement`：手的世界變換），所以弓會跟著身體後仰、後座與晃動。手的縮放（0.32）讓 95 像素的弓
  以 32 像素角色的大小畫出。事件來自兩個片段。
- **任何角色：** `fitCharacter`（anim_clip.h）讓 `body` 換成另一個 sprite，並移動 `hand`，讓它留在那個身體的相同位置：
  高度維持在可見身體（頭到腳）中的比例，x 維持與身體右邊緣的距離。兩者都由 sprite 的可見像素量出（`visibleBox`，
  sprite_mesh.h）。圖集載入時會量好遊戲圖集的每個 sprite。若某個角色看起來仍有偏差，可在
  [`assets/data/character_fit.json`](../../../assets/data/character_fit.json) 加微調：`"player_viking": {"hand": [0, -1]}`
  （像素，x 向右、y 向下）。沒列出的角色什麼都不需要。
- **戰鬥中：** `items.json` 中的武器指定它的兩個檔案：`"battleAnim": {"character": "anim/archer_bow.anim",
  "held": "anim/weapon_bow.anim"}`（`bow` 就是如此）。`"clips": {"idle": "ready", "charge": "raise", ...}` 指定每個狀態
  播放的片段，給片段有自己名稱的武器用（`sword` 與 `staff` 就是如此；沒列出的狀態播放同名片段）。裝備它時，戰鬥畫面會在玩家圖片的位置畫出玩家角色與武器（圖片隱藏），
  播放戰鬥狀態選出的片段：

  | 狀態 | 時機 |
  |---|---|
  | `charge` | 攻擊條等待點擊（弓拉滿並晃動，等多久就持續多久） |
  | `attack` | 命中的點擊，播放到結束 |
  | `miss` | 沒造成傷害的點擊，播放到結束 |
  | `idle` | 攻擊條為下一回合恢復中，以及戰鬥結束後 |

  （`Game::updateBattleRig` / `drawBattleRig`，game_combat.cpp。）情境：`battle_bow`（可直接玩；它的測試點一次：`attack`）、
  `battle_bow_charge`（`charge`）與 `battle_bow_idle`（`idle`），見 [08](../../gameplay/zh_TW/08_TEST_SCENARIOS.md)。
- **觀看：** `toms_game --anim=assets/media/anim/archer_bow.anim#charge --character=player_mage
  --held=assets/media/anim/weapon_bow.anim`。動畫編輯器的 **檢視 > 以角色預覽** 以另一個角色顯示角色檔（只改變畫面；
  編輯仍套用在原本的片段）。
- **其他武器或動作：** 一個帶有同樣 `body` / `hand` 節點的角色檔，加上一個同名片段的手持檔；武器在 `battleAnim` 中指定兩者。
- **測試：** `unit.anim_character_test`（套用到每個 `player*` sprite；手持片段畫在手上並跟著它的關鍵影格）；CTest
  `anim.check_archer`；編輯器自我測試預覽 knight girl；三個情境。

### 戰鬥中的怪物

戰鬥畫面上的怪物會隨戰鬥而動：等待時呼吸，攻擊時撲向玩家，被打中時退縮，被擊敗時淡出。所有怪物共用一個檔案：
`assets/media/anim/monster_battle.anim`，由 `tools/make_monster_battle_anims.py` 產生（修改腳本後再執行它）。

- **片段**（用史萊姆做的；遊戲會把每隻怪物自己的 sprite 放進 `body` 節點，`fitCharacter`）：

  | 片段 | 何時播放 | 長度 |
  |---|---|---|
  | `idle` | 沒有事發生時（循環） | 1.4 秒 |
  | `attack` | 敵人的時鐘觸發：蓄力、在 0.2 秒撲向玩家（傷害數字出現時）、回位 | 0.55 秒 |
  | `hurt` | 玩家的攻擊命中時（它頭上的數字出現時）：被擊退、紅色閃光、晃動 | 0.4 秒 |
  | `die` | 贏得戰鬥後，在最後一次 `hurt` 之後：閃光、癱倒並淡出，之後不再出現 | 0.7 秒 |

  攻擊或被擊中都會從頭播放它的片段（攻擊中被打中會中斷攻擊）；片段結束後由戰鬥決定接下來播什麼（贏了就 `die`，否則 `idle`）。
  （`Game::updateMonsterRig`、`drawBattleRig`，game_combat.cpp。）
- **大小與位置：** 在戰鬥畫面原本怪物圖片的位置（`battle.rcss #enemy`，134 × 134），腳在底部中央，sprite 較長的一邊填滿方框
  （兩格寬的石像和方框一樣寬）。遊戲畫怪物時圖片會隱藏（`battle.enemy_anim`）；沒有這個檔案時仍顯示圖片。
- **某隻怪物自己的動作：** 在檔案中加入名為 `<狀態>_<敵人 id>` 的片段（例如 `attack_golem`，在動畫編輯器中把石像放進 `body` 來做）；
  對那隻怪物它會取代共用的片段。請保留 `body` 節點。
- **背景遮罩**由遊戲畫在兩邊角色底下（`drawBattleRig`）；`battle.rml` 沒有遮罩（RmlUi 的遮罩會讓遊戲畫在它下面的一切變暗）。
- **測試：** CTest `anim.check_monster_battle`；情境 `battle_enemy_attack`（撲擊中 `enemyAnim` 為 `attack`）、`battle_enemy_hurt`
  （弓射中後為 `hurt`）、`battle_enemy_die`（勝利後為 `die`），[08](../../gameplay/zh_TW/08_TEST_SCENARIOS.md)。

## 編輯器

`anim_editor`（tools/anim/qt，和 Qt 編輯器一起建置）開啟和儲存 `.anim` 檔。它的預覽
用遊戲自己的 `evaluate()` + `appendQuads()` 對片段求值，並用 QPainter 把每個四邊形畫成
貼了貼圖的平行四邊形（加法 = `CompositionMode_Plus`，色調 = sprite
乘以顏色），所以它顯示的就是遊戲畫的。

```
anim_editor                                    the editor
anim_editor tests/smoke/anim_preview.anim      with a file open (Visual Studio: "anim_editor (preview clip)")
anim_editor --headless check x.anim [--atlas [id=]a.atlas]...
                                               parse + check sprites and keys; exit 0 ok (warnings allowed), 2 errors, 3 usage
anim_editor --headless meshes x.anim... [--atlas [id=]a.atlas]... [--default-atlas [id=]a.atlas]
            [--map from.atlas=to.atlas]... [--bin|--text] [--check] [--prune]
                                               add the clips' meshes to the mesh file beside each atlas they use
                                               (a.mesh.json); --prune: only what these clips need; --check: exit 1
                                               when a needed mesh is missing or stale (or, with --prune, extra);
                                               python tools/refresh_meshes.py runs it for every clip and art style
anim_editor --headless recipe x.anim --node <name> --recipe <id> [--clip <name>] [--start s] [--period s]
            [--strength k] [--out y.anim]      write a motion recipe's keys (docs/engine/06 §7); exit 0 ok, 2 failed, 3 usage
anim_editor --headless recipe --list           the motion recipes
anim_editor --selftest x.anim outdir           automated check (with -platform offscreen); outdir must be on
                                               the atlases' drive, e.g. Build/anim_selftest
```

（`--headless check`：解析並檢查 sprite 和 key，結束碼 0 正常（允許警告）、2 錯誤、3 用法錯誤。
`--headless meshes`：把片段的網格加進它們用到的每個 atlas 旁的網格檔（a.mesh.json）；`--prune`：只留這些片段需要的；
`--check`：缺少需要的網格或網格已過時（加上 `--prune` 時也包括多餘的）時以 1 結束；`--default-atlas`：沒有寫明圖集的
片段所用的圖集；`--map`：把片段用到的某個圖集換成另一個。`python tools/refresh_meshes.py` 會對每個片段與美術風格執行它。
`--headless recipe`：把動作範本的關鍵影格寫進檔案（docs/engine/06 §7），`--list` 列出所有範本；結束碼 0 正常、2 失敗、3 用法錯誤。
`--selftest`：自動檢查（搭配 `-platform offscreen`），outdir 必須和 atlas 在同一個磁碟上。）

`--headless check` 載入檔案的 `atlases`（相對於 `.anim`）；每個 `--atlas`（可重複，
相對於工作目錄，id = 檔名，除非以 `id=path` 指定）會依序取代那份
清單。它對每個無法載入的 atlas 回報一個錯誤；對每個 sprite 引用
`"id:name"`，若沒有 atlas 有那個 id、或那個 atlas 沒有那個名稱，回報一個錯誤；對每個不帶前綴的 `"name"`，
若沒有 atlas 有它就回報錯誤，若有好幾個 atlas 有它就回報警告（只針對不帶前綴的引用：第一個勝出——
在 Sprites 面板中選它，或用 Qualify Sprite References 存下 atlas；指向這些名稱的帶前綴引用
沒有問題）；每個絕對的 atlas 路徑回報一個警告；並讀取每個 atlas 旁的網格檔，對每個在其中沒有網格的蒙皮節點 sprite 回報一個錯誤。

**編輯器中的 sprite 引用。** 在任何地方挑選 sprite——從 Sprites 面板拖到
viewport 或節點上、雙擊它、Sprite Seq、屬性中的 sprite 欄位、key 清單的 Sprite 格——
都會為剛好挑選的那一份存下 `"id:name"`，包括某個名稱在後面 atlas 中的副本。欄位以
`name (id)` 顯示引用；舊的不帶前綴名稱顯示為 `name (auto: id)`，
帶有目前查找會選到的 atlas，在被修改之前保持原樣。輸入不帶前綴的名稱時，
會連同查找選到的 atlas 一起存下。**Key > Qualify Sprite References**（Atlases 清單底下的
**Qualify** 也是）會依目前的查找順序，把檔案中每個不帶前綴的名稱變成 `id:name`，
算一個復原步驟（沒有任何 atlas 有的名稱保持不帶前綴）。

**Atlases**（屬性面板，在 Clip 底下）：檔案的 atlas，依查找順序，每一列是它的 **id**、
存下的路徑（相對於 `.anim`）和它的 sprite 數，沒載入時顯示為紅色（提示會說明原因）。
**+** 加入一個或多個（檔案對話框可以多選；也可以用
**File > Add Atlas…**；清單中已有的檔案會略過），**−** 移除，**↑ / ↓** 改變
查找順序（給不帶前綴的名稱用），**Qualify** 為每個不帶前綴的名稱存下 atlas，**⟳** 從磁碟重新載入
每個 atlas（Ctrl+Shift+R）；每次變更都是一個復原步驟。

- **Id：** 雙擊 id 可以重新命名：每個片段中每個 `oldId:...` 引用（節點 sprite
  和 sprite key）都會在同一個復原步驟中改寫。Id 必須唯一，不能是空的，也不能
  包含 `:`、`/`、`\`、`=` 或空白。
- **加入** 預設 id 已被使用的 atlas 時（第二個 `game.atlas`，例如來自
  `styles/dark16/atlas`），會要求輸入 id，並建議它資料夾的上一層資料夾名稱（`dark16`）；
  不會默默產生重複。同一個檔案加兩次會被拒絕。
- **移除** 仍有引用在使用的 atlas 時，會先詢問並顯示引用數量：它們會變成錯誤
  （問題面板），直到 atlas 回來（復原）或它們被修改。

Atlas 路徑永遠以相對路徑存放：

- 對**未命名**的檔案加入 atlas 時，會先要求儲存（「Save the animation first: atlas
  paths are stored relative to the .anim file」），並在 atlas 的資料夾中開啟另存新檔；取消
  = 不加入 atlas。未命名的檔案只在記憶體中保存絕對路徑（File > New 會保留
  目前的 atlas），永遠不會出現在存下的檔案中。
- **儲存 / 另存新檔** 會把每個 atlas 路徑重新對應到新位置。當某個 atlas 無法以相對路徑
  到達（在另一個磁碟上）時，儲存會被**拒絕**，並在訊息中指出那個 atlas：
  把 `.anim` 存在 atlas 所在的磁碟上，或移除 / 替換那個 atlas。對已存檔的檔案加入
  另一個磁碟上的 atlas，也會同樣被拒絕。
- 以**絕對路徑**指名 atlas 的檔案仍然能開啟並載入它；問題面板會警告
  「absolute atlas path … stored relative on save」，下次儲存時會轉換它（或如上所述拒絕）。

| 視圖 | 做什麼 |
|---|---|
| **Clips** | **雙擊**（或 Enter）開啟片段，以粗體顯示；如果開啟的片段在開啟或儲存後有變更，會詢問 *Discard all changes to “A” and open “B”?*（是 = 捨棄，算一個復原步驟；否 = 留下）；F2 或工具列可重新命名；新增 / 複製 / 重新命名 / 刪除作用在反白的片段上；長度（0 = 到最後一個 key）、播放次數（-1 = 循環）、停在最後一格 |
| **Nodes** | 節點樹，附每個節點的靜止 sprite（`name (id)`）：**Playback** 欄（↻：點一列的圖示可切換 播放一次並停留 ▶ / 播放一次後隱藏 ■ / 循環 ↻）；**Solo** 欄（◎：點一列的目標，在片段照常播放時只畫那個節點和它的子節點：父節點仍會移動它們，但父節點自己的 sprite 隱藏；再點一次，或開啟另一個片段，就會結束；只影響預覽，不會儲存；狀態列顯示 `SOLO: name`）；新增子節點 / 兄弟節點 / sprite 節點、再製、刪除、重新命名（F2）；**剪下 / 複製 / 貼上**（編輯選單或按右鍵：Ctrl+X / Ctrl+C / Ctrl+V 貼到選取的節點底下，Ctrl+Shift+V 貼在它後面）——節點連同子節點、關鍵影格與蒙皮一起，以文字放在系統剪貼簿（另一個編輯器視窗或文字檔都能接收）；片段中已有的名稱會加上 `_2`、`_3`…，貼上（或再製）的部分中的蒙皮會跟隨一起貼上的骨頭，所以複製的骨架就是第二副骨架；拖放以改變父節點或順序（清單順序 = 相同 `order` 之間的繪製順序）、眼睛 = 靜止的 `visible`、`order` 欄 |
| **Sprites** | 每個 atlas 一個分頁（標題是它的 id 和 sprite 數，依查找順序），每個列出那個 atlas 的 sprite 和縮圖；篩選套用到每個分頁（標題會顯示符合的數量）；每一列是某個 atlas 的那一份，挑選它就會存下那個 atlas（`id:name`）；出現在多個 atlas 中的名稱有一個小標記（「also in X; each copy is usable」）；篩選比對 `id:name`；把 sprite 拖到 viewport（在選取節點底下、放開的位置建立節點）或節點上；雙擊 = 選取節點的 sprite（開啟自動 key 時在播放頭建立 sprite key） |
| **Viewport** | 播放頭位置的片段；點擊選取最上面的節點；**移動（W）** / **旋轉（E）**（rot z）/ **縮放（R）** gizmo。開啟 **自動 key（N，預設開啟）** 時，拖曳會在播放頭寫入 key，關閉時寫入靜止值（剛好在播放頭的 key 兩種情況都會更新）。Ctrl = 移動時對齊像素，Shift = 15° 一格 / 保持比例；滑鼠中鍵平移，滾輪縮放 |
| **Properties** | 片段設定；**Atlases** 清單（如上）；節點的靜止設定（名稱、sprite、軸心、order、混色、繼承顏色、可見、**Playback**：播放一次並停留 / 播放一次後隱藏 / 循環）；播放頭位置的值（Pos、Rot、Scale 皆為 x, y, z；x / y 旋轉在這裡輸入），每個通道有一個 key 按鈕（◆ 這裡有 key：點擊移除；◇ 有動畫：點擊把顯示的值設為 key；虛線：沒有軌）；播放頭所在或之前那個 key 的緩動（任何 Tweeny 緩動，或附曲線編輯器的 Bezier…） |
| **Keys** | MPDI 風格的 key 清單（見下方） |
| **Events** | 選取節點的事件 key：在播放頭新增、移除、編輯時間 / 名稱 |
| **Timeline** | 第 4 階段多軌時間軸的預留位置；可接收拖放的 sprite（見下方） |
| **Problems / Log** | 指向未知 atlas id 或其 atlas 沒有的名稱的 sprite 引用、不在任何 atlas 中或（警告）在多個 atlas 中的不帶前綴名稱、重疊的 key 時間、超出片段長度的 key、片段名稱衝突、無法載入的 atlas、絕對的 atlas 路徑、沒有網格的蒙皮 sprite（和 `--headless check` 相同的檢查）；log 顯示開啟 / 儲存的檔案，以及播放時觸發的事件 |

**播放控制：** 播放 / 暫停（Space）、停止、循環預覽、速度、時間、帶有選取節點 key 時間的
拖曳條（`,` / `.` 在 key 之間跳）。播放使用 `AnimPlayer`（播放次數、
停在最後一格、每個事件觸發一次）。

**拖曳建立 sprite 序列（MPDI 風格）。** 在 Sprites 分頁中選取多個 sprite（Shift / Ctrl，
例如序列 atlas 的各個影格），放到某個時間區域上：**拖曳條**（開始 = 游標下的
時間，對齊到 key）、**Keys 清單**的某一列（開始 = 那一列的時間；放在列的下方 = 播放頭）或
**Timeline** 分頁（開始 = 播放頭）。對話框會詢問
「Insert N sprite keys on node '…'?」，附**開始時間**和**每個 key 的間隔**（預設
0.1 秒；會記住上次用的間隔），顯示結果的時間範圍，以及那些時間上有多少現有的
sprite key 會被取代；當最後一個 key 超過固定的片段長度時，
會提議延長它。**是** 依清單順序，在選取節點上為每個 sprite 插入一個 sprite key
（一個復原步驟）；**否** 什麼也不改。沒有選取節點時只會這樣告訴你。

**Key 清單。** 選取節點的每個 key 時間一列（它所有通道的聯集）；欄位
Pos、Rot、Scale、Color、Sprite（`name (id)`，無法解析時為紅色）、Visible、Event 顯示那個時間的 key 或 `·`，Ease 顯示
該列內插 key 的緩動（不同時顯示 `mixed`）。選取一列會把播放頭移到那裡；
可以選取多列。雙擊儲存格可以編輯（空的儲存格會建立 key，
清空儲存格會移除它；Time 會移動整列，Ease 接受名稱或 `bezier(x1, y1, x2, y2)`）。
每個通道是自己的 key 陣列（`pos`、`rot`、`scale`、`color`、`sprite`、`visible`、`event`
軌；沒有 key 的通道維持節點的靜止值，不會存下），所以一列只
包含它需要的 key：只切換 sprite 的節點就只有 sprite key。任何 key 操作都不會
建立你沒要求的軌：**Insert** 為目前欄的通道建立 key，或（沒有
欄時）為節點已經在動畫的內插通道建立 key（節點沒有任何動畫時則是位置）。**Key animated channels together**（預設關閉）：
編輯內插儲存格時，也會在那個時間為節點*其他有動畫*的通道建立 key，讓 MPDI 風格的列保持完整；
它永遠不會加入新的軌。

| 工具 | 對選取的列 |
|---|---|
| Insert | 在播放頭建立 key（目前欄的通道，否則是節點有動畫的通道） |
| Delete | 刪除這些列在每個通道上的 key，或只刪除目前欄的通道（Del） |
| Set Time… | 移動這些列，保持它們的間隔；永遠不會讓同一通道的兩個 key 在同一時間（會提議把後面的 key 往後移） |
| Even | 把這些列平均分布在第一列和最後一列之間 |
| Rescale… | 把節點、或整個片段（及其長度）的每個 key 時間縮放到新的持續時間 |
| Ramp… | MPDI 的 *AverageAssign*：某個通道（或 alpha）在第一列和最後一列各設一個值，中間依時間線性內插 |
| Fade In / Out | 在這些列之間 alpha 0 → 1 / 1 → 0 |
| Sprite Seq | 在 Sprites 面板中選取的 sprite（2 個以上，依清單順序）之間循環的 sprite key |
| Ease… | 這些列中每個內插 key 的緩動 |

**Key > Motion Recipe…** 替選取的節點寫出一段有名稱的短動作：晶格範本（breathe、sway、squash、jelly）作用在任何 sprite
的變形晶格上，骨頭範本（標有「(bones)」：bone sway、nod、swing）作用在蒙皮節點的骨頭上。選好範本、開始時間（預設：
播放頭）、週期與強度；範本會取代它在這段範圍內寫的 key，套用是一個復原步驟。要重複的待機動作，就讓片段或節點循環。
見 [06_ANIMATION_RECIPES.md](06_ANIMATION_RECIPES.md) §7。

每次編輯都是一個復原步驟（拖曳和數值框編輯會合併成一步）；標題中的 `*` 表示
有未儲存的變更。編輯器的 `AnimEditor` 物件為宿主視窗建立文件、viewport、面板、選單和
工具列，這正是第 2 階段的 studio 應用程式會以外掛方式載入的東西；主題、
圖示和畫布基底來自 `tools/studio_common`，和 atlas 編輯器共用。

### 將片段匯出為 GIF 或動態 PNG

**檔案 > 匯出片段為動畫**（Ctrl+Shift+E）：
- **片段：**列出檔案中的所有片段，每個有勾選框（預設勾選目前開啟的片段；可按「全部／無」）。每個勾選的片段寫成一個檔案：
  `<檔名>_<片段>.gif`、`<檔名>_<片段>.png`（APNG），或 `<檔名>_<片段>_000.png`、`_001.png`……，放在選擇的資料夾。
- **格式：**GIF（256 色，每個像素只有透明或不透明：柔和邊緣和影子會變硬）、動態 PNG（全彩與柔和透明；瀏覽器和大多數看圖軟體
  都能播放），或 PNG 影格（每格一張圖）。
- **影格速率**（GIF 最多 50）、**縮放**（片段像素的倍數）、**透明背景**或指定顏色、**無限循環**或只播一次，以及**包含手持片段**
  （檢視 > 手持片段：畫在 `hand` 的武器）。

影格由遊戲繪製器在離屏畫出（`BgfxRenderer::setCapture`、`GameCanvasView::renderOffscreen`）：蒙皮、晶格與效果圖層都與遊戲畫的
一樣，不含編輯器的任何東西。每張圖都以整個片段取景一次，所以角色不會跳動；大於 1500 像素的節點（整個畫面大小的背景）不列入取景。
循環的片段輸出一個循環；只播一次的片段也包含最後一格。腳本中可用：
`anim_editor --export file.anim outdir [gif|apng|png] [clip,clip,...] [held.anim]`（測試：`anim.export_gif`）。

## 轉換 MPDI（舊引擎的動畫）

編輯器的 **Convert → MPDI Folder to .anim…** 會轉換資料夾（含子資料夾）中的每個 `.mpdi`。它們使用的 `.pi` sprite sheet
也會轉成 atlas（`.atlasproj` + `.atlas` + `.png` + `.plist`），每個只轉一次。結果可以放在舊檔案旁邊，或放到另一個資料夾
（保留相同的子資料夾）。完成後的報告會列出寫出的檔案、哪些地方是近似處理，以及無法轉換的檔案。**Open the First .anim**
會開啟其中一個結果。命令列：

```bat
anim_editor --headless convert-mpdi <資料夾> [--out <輸出資料夾>]
```

程式在 `tools/anim/core/mpdi_convert.*`，選單與命令列共用；`.pi` 讀取器沿用 atlas 工具的。規則依照舊引擎的讀取與繪製程式
（`MPDIDataParse.cpp`、`SubMPDI.cpp`、`Curve.cpp`）：

| MPDI | .anim |
|---|---|
| 檔案、`ImageName`（它的 `.pi`） | 一個 `.anim`；每個 `.pi` 一個 atlas，id = `.pi` 的名稱 |
| `MultiPathDynamicImage` | 一個 clip：`TotalPlayTime` → length、`PlayCount` → playCount（-1 = 無限）、`StayAtLastFrame` → stayAtLastFrame |
| 一條路徑（`CueToStartCurvesWithTime`） | 一個節點，key 的時間 = `StartTime` + 各點時間 |
| 點的位置 | 圖片中心：1.1 以後存的是未裁切圖片的左上角（再加原始尺寸的一半）；1.0 以前存的是中心。線性 |
| `LOD` 2 / 3 | 引擎的平滑（Chaikin 切角，LOD − 1 次）烘焙成更多位置 key |
| `Size` / `Scale`、`Mirror` | 縮放 = 尺寸 ÷ sprite 裁切後的尺寸；鏡像 = x 為負 |
| `Angle`（z）/ `Angles`（x, y, z） | 旋轉 x, y, z，度（與引擎相同的旋轉順序） |
| `Color` | 顏色（最舊格式的 0..255 → 0..1） |
| `PIName` + `ImageName` | sprite key `"pi:image"`，階梯式 |
| `BehaviorUpdate` | 尺寸、旋轉、顏色在點之間線性；沒有時為階梯式 |
| `Loop` | 節點循環；有 `StartTime` 時由一個 gate 群組在那之前隱藏它 |
| `StartTime` > 0 | 在那之前隱藏 |
| 加法混合 | 目的端 `GL_ONE` 或 `GL_DST_ALPHA` → `blend: add` |
| `RotationPosOffset`、非置中的 `AnchorType` | 一個旋轉的群組，圖片以偏移放在裡面，位置與引擎繪製的相同 |
| `HintPoint` | event key（聲音提示為 `sound:<名稱>`） |

**讀取的格式：**
- 最舊的檔案：沒有版本，有 `CurvePoint` / `CurveTime` 清單，點的資料為 `w,h,angle,r,g,b,a,image,mirror`；
- 在 `<MPDIList>` 之前有 `<Version>` 元素的 1.0 檔案；
- 1.1、1.2、1.3 版。

**不轉換，並以警告回報：**
- 非單位矩陣的 `CameraView`；
- `ViewPort` 裁切；
- 一般與加法以外的混合模式。

**用舊引擎自己的資料檢查過**（`MagicTower/MagicSrc/FM79979`）：找到 48 個 `.mpdi`，轉換了 47 個（312 個 clip、3,220 條路徑），
76 個 `.pi` 轉成 atlas，約 10 秒。
- 唯一失敗的是舊編輯器建置資料夾中、沒有指定 sprite sheet 的測試檔。
- 47 個結果中有 44 個通過 `--headless check`；其餘 3 個引用的圖片在舊資料的 `.pi` 裡本來就不存在，轉換時已回報。

**測試：** 沒有。轉換器的測試（`anim.mpdi_convert`）與它手寫的檔案（`tests/mpdi/`）已於 2026-10-31 移除（擁有者）。
轉換的結果可以用 `anim_editor --headless check` 檢查。
