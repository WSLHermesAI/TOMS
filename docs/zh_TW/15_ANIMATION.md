# 15 — 節點動畫（`.anim`）

> 英文原文：[15_ANIMATION.md](../15_ANIMATION.md)

用打包好的 sprite atlas（[14](14_ATLAS_TOOL.md)）建立的動畫：一棵節點樹，每個節點有
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
見 [16_ANIMATION_RECIPES.md](16_ANIMATION_RECIPES.md)。

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
  "version": 1,
  "atlases": ["../media/atlas/game.atlas", "fx/slash.atlas",
              {"id": "dark16", "path": "../styles/dark16/atlas/game.atlas"}],
  "clips": [{
    "name": "slime_attack", "length": 0.6, "playCount": 1, "stayAtLastFrame": true,
    "root": {
      "name": "slime", "sprite": "game:slime",
      "tracks": {
        "pos":    [{"t": 0, "v": [0, 0], "ease": "quadraticOut"}, {"t": 0.3, "v": [24, -8]}, {"t": 0.6, "v": [0, 0]}],
        "scale":  [{"t": 0.3, "v": [1.2, 0.8], "ease": "backOut"}, {"t": 0.6, "v": [1, 1]}],
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
  atlas 的 `x_pivot`，再沒有就是中心）、`order`、`blend`（`normal` / `add`）、`inheritColor`、`loop`、`stayAtLastFrame`、`children`。
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
  - **範例：** [examples/anim_child_timing.anim](../examples/anim_child_timing.anim) 中的 `popup` 片段。
- **軌：** `pos` `scale`（x, y）、`rot`（角度，順時針，不會折返：0 → 720 是兩圈）、
  `color`（r, g, b, a 0..1）會內插；`sprite` `visible` `event` 是階梯式。沒有 key 的通道
  維持靜止值。第一個 key 之前維持那個 key，最後一個之後維持最後一個。
- key 上的 **`ease`** 決定從那個 key 開始的區段形狀：`linear`（預設）、`stepped`（維持），或
  `quadratic` `cubic` `quartic` `quintic` `sinusoidal` `exponential` `circular` `bounce`
  `elastic` `back` 任一種 + `In` / `Out` / `InOut`（https://easings.net），或像 CSS `cubic-bezier()` 一樣的
  三次貝茲曲線 `[x1, y1, x2, y2]`。`back`、`elastic` 和 `bounce` 會超出範圍；顏色會被限制在範圍內。

**空間和順序：** 像素，y 往下。節點的世界變換 = 父節點的 × 平移 · 旋轉 ·
縮放；負的縮放會鏡像。繪製順序是深度優先：先畫 `order < 0` 的子節點（依 order，
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
anim_editor --selftest x.anim outdir           automated check (with -platform offscreen); outdir must be on
                                               the atlases' drive, e.g. Build/anim_selftest
```

（`--headless check`：解析並檢查 sprite 和 key，結束碼 0 正常（允許警告）、2 錯誤、3 用法錯誤。
`--selftest`：自動檢查（搭配 `-platform offscreen`），outdir 必須和 atlas 在同一個磁碟上。）

`--headless check` 載入檔案的 `atlases`（相對於 `.anim`）；每個 `--atlas`（可重複，
相對於工作目錄，id = 檔名，除非以 `id=path` 指定）會依序取代那份
清單。它對每個無法載入的 atlas 回報一個錯誤；對每個 sprite 引用
`"id:name"`，若沒有 atlas 有那個 id、或那個 atlas 沒有那個名稱，回報一個錯誤；對每個不帶前綴的 `"name"`，
若沒有 atlas 有它就回報錯誤，若有好幾個 atlas 有它就回報警告（只針對不帶前綴的引用：第一個勝出——
在 Sprites 面板中選它，或用 Qualify Sprite References 存下 atlas；指向這些名稱的帶前綴引用
沒有問題）；每個絕對的 atlas 路徑回報一個警告。

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
| **Nodes** | 節點樹，附每個節點的靜止 sprite（`name (id)`）：**Playback** 欄（↻：點一列的圖示可切換 播放一次並停留 ▶ / 播放一次後隱藏 ■ / 循環 ↻）；**Solo** 欄（◎：點一列的目標，在片段照常播放時只畫那個節點和它的子節點：父節點仍會移動它們，但父節點自己的 sprite 隱藏；再點一次，或開啟另一個片段，就會結束；只影響預覽，不會儲存；狀態列顯示 `SOLO: name`）；新增子節點 / 兄弟節點 / sprite 節點、複製、刪除、重新命名（F2）、拖放以改變父節點或順序（清單順序 = 相同 `order` 之間的繪製順序）、眼睛 = 靜止的 `visible`、`order` 欄 |
| **Sprites** | 每個 atlas 一個分頁（標題是它的 id 和 sprite 數，依查找順序），每個列出那個 atlas 的 sprite 和縮圖；篩選套用到每個分頁（標題會顯示符合的數量）；每一列是某個 atlas 的那一份，挑選它就會存下那個 atlas（`id:name`）；出現在多個 atlas 中的名稱有一個小標記（「also in X; each copy is usable」）；篩選比對 `id:name`；把 sprite 拖到 viewport（在選取節點底下、放開的位置建立節點）或節點上；雙擊 = 選取節點的 sprite（開啟自動 key 時在播放頭建立 sprite key） |
| **Viewport** | 播放頭位置的片段；點擊選取最上面的節點；**移動（W）** / **旋轉（E）** / **縮放（R）** gizmo。開啟 **自動 key（N，預設開啟）** 時，拖曳會在播放頭寫入 key，關閉時寫入靜止值（剛好在播放頭的 key 兩種情況都會更新）。Ctrl = 移動時對齊像素，Shift = 15° 一格 / 保持比例；滑鼠中鍵平移，滾輪縮放 |
| **Properties** | 片段設定；**Atlases** 清單（如上）；節點的靜止設定（名稱、sprite、軸心、order、混色、繼承顏色、可見、**Playback**：播放一次並停留 / 播放一次後隱藏 / 循環）；播放頭位置的值，每個通道有一個 key 按鈕（◆ 這裡有 key：點擊移除；◇ 有動畫：點擊把顯示的值設為 key；虛線：沒有軌）；播放頭所在或之前那個 key 的緩動（任何 Tweeny 緩動，或附曲線編輯器的 Bezier…） |
| **Keys** | MPDI 風格的 key 清單（見下方） |
| **Events** | 選取節點的事件 key：在播放頭新增、移除、編輯時間 / 名稱 |
| **Timeline** | 第 4 階段多軌時間軸的預留位置；可接收拖放的 sprite（見下方） |
| **Problems / Log** | 指向未知 atlas id 或其 atlas 沒有的名稱的 sprite 引用、不在任何 atlas 中或（警告）在多個 atlas 中的不帶前綴名稱、重疊的 key 時間、超出片段長度的 key、片段名稱衝突、無法載入的 atlas、絕對的 atlas 路徑（和 `--headless check` 相同的檢查）；log 顯示開啟 / 儲存的檔案，以及播放時觸發的事件 |

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

每次編輯都是一個復原步驟（拖曳和數值框編輯會合併成一步）；標題中的 `*` 表示
有未儲存的變更。編輯器的 `AnimEditor` 物件為宿主視窗建立文件、viewport、面板、選單和
工具列，這正是第 2 階段的 studio 應用程式會以外掛方式載入的東西；主題、
圖示和畫布基底來自 `tools/studio_common`，和 atlas 編輯器共用。
