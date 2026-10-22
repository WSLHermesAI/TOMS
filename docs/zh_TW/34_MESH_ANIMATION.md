> 英文原文：[34_MESH_ANIMATION.md](../34_MESH_ANIMATION.md)

# 34 — 網格動畫：取代 Spine（設計）

這是一份設計：在 TOMS 自己的 `.anim` 系統裡，用彎曲 sprite 的方式做動畫，就像 Spine 那樣。目標是：

- AI 換皮後的美術可以直接放進來，不必重做動畫；
- **形狀**改變的美術也能保留它的動作；
- 檔案小；
- 動作簡單到 AI 也能寫。

狀態（2026-10-22）：計畫（第 6 節）的第 1–3 步**已完成**，動畫編輯器也能顯示蒙皮（第 7 節）：算圖器能畫三角網格，sprite 有從 alpha 切出的網格，
片段以蒙皮彎曲它們。第 4–6 步還沒做。擁有者的決定在第 8 節。

## 1. 為什麼

AI 換皮流程（`AIGamestyle/Doc/Reskin_Game_Art.md`）只有在形狀不變時才能替 Spine 美術換皮。擁有者在
2026-10-19 的結論是：新的身體形狀需要在 Spine 工具裡手工做新的網格、權重與動畫。TOMS 沒有 Spine 執行環境，
Spine 的編輯器與執行環境授權也不在我們的工具裡。

Spine 是兩樣東西：

- **骨頭**：一棵變換的樹；
- **網格**：把 sprite 切成三角形，三角形的角跟著骨頭走（權重），或直接被移動（變形關鍵影格）。

TOMS 的 `.anim`（docs/15）原本就有第一樣：一棵節點樹，有位置／旋轉／縮放／顏色關鍵影格、緩動與 Bezier 曲線。
第 1–3 步加上了第二樣。

## 2. 原有的東西

| | 是什麼 | 用途 |
|---|---|---|
| FM79979 `Core/GameplayUT/Render/Morphing` + `2DMesh`（約 800 行）與 `TextureEditor/PI`（編輯器） | 把 sprite 切成三角形（手放的點做一般的 Delaunay）；每個頂點有自己的**絕對**位置關鍵影格，只有線性；CPU 每格內插每個頂點。檔案為 `.pi` + `.ti` + `.mx` + `.mb` | 取它的構想，不取程式碼。執行時有四個錯誤：結束時間從未設定；速度乘了兩次；「反向」沒有作用；沒有關鍵影格的頂點會縮到 (0, 0)。編輯器的關鍵影格最佳化會把「停住」變成緩慢漂移。關鍵影格綁在一個網格上，所以形狀改變的 sprite 會失去動畫 |
| `D:\Work\morphing` | 只有 2020 年那個格式的範例資料（`666`、`4545`、`222`、`999`；缺 `222.pi`） | 若要匯入舊格式，可當測試輸入 |
| TOMS `.anim` + `tools/anim`（Qt） | 節點樹、關鍵影格、緩動、PoseCache、MPDI 匯入；圖集 id 在繪製時才查，所以換一個圖集（美術風格）也能畫同一個片段 | 本設計的基礎 |
| TOMS 算圖器（bgfx） | 第 1 步之前：只有四角的四邊形 | 第 1 步加上了三角網格 |

## 3. 一張圖看懂設計

```
sprite（圖集區域）  --第 2 步-->  網格：從 sprite 的 alpha 切出的三角形（美術改變時重新切）
        |
        v
蒙皮（第 3 步）      -- 每個頂點跟著片段樹裡最多 4 個節點（它的骨頭）走，有權重；權重依頂點在靜止時離各骨頭的
        |               遠近自動產生（或手繪）
        v
晶格變形（第 4 步）  -- sprite 上的小網格（預設 4 x 4，每個節點可調）；關鍵影格移動它的點；每個網格頂點跟著它
        |               所在的格子走。壓扁、拉長、呼吸。
        v
螢幕 -> IRenderer::drawMesh（第 1 步）
```

有兩個選擇讓它比 Spine 更適合 AI 做的美術：

1. **網格與權重由工具產生，不是手做。** 網格來自 alpha，權重來自骨頭的靜止位置。AI 產生的新形狀（別的身體、
   更大的帽子）會自動重新切網格、重新算權重，片段的骨頭關鍵影格（動作）不變。
2. **變形關鍵影格移動的是晶格，不是網格頂點**（第 4 步）。晶格定義在 sprite 的矩形上，而不是它的三角形上。
   換皮後重新切網格的 sprite 照樣以同樣方式彎曲，檔案也很小。Spine 的變形關鍵影格和舊的 `.mx` 一樣屬於某一個
   網格，網格一改就壞。

## 4. 資料

### 4.1 網格：圖集旁的檔案，純文字或帶二進位緩衝（glTF 式）

網格屬於 sprite，不屬於片段：每個用到這個 sprite 的片段都彎曲同一組三角形。它放在圖集旁邊，檔名為
`<atlas>.mesh.json`（`assets/media/atlas/game.mesh.json`）。每種美術風格有自己的一份
（`styles/<id>/atlas/game.mesh.json`），從它自己的 alpha 切出。和 glTF 一樣，數字可以放在 JSON 裡，也可以放在
旁邊的二進位緩衝：

```jsonc
{ "version": 1,
  "buffers": [{"uri": "game.mesh.bin", "byteLength": 85904}],             // 只有用二進位緩衝時
  "meshes": {
    "golem": { "source": "alpha", "cells": [8, 8],                        // 從 alpha 切出，或 "hand"（保留）；格數
               "vertices": [0,0, 8,0, ...], "triangles": [0,1,2, ...] },  // 純文字，或
    "slime": { "source": "alpha", "vertexCount": 25, "vertices":  {"buffer": 0, "byteOffset": 0},     // float32 x, y
                                  "indexCount": 96,  "triangles": {"buffer": 0, "byteOffset": 200} }  // uint16
  } }
```

- 頂點以 sprite 原圖像素計（x 向右，y 從左上向下）。UV 不存，由區域算出，所以不論圖集怎麼排，同一個檔案都能用。
- 由 `atlaspack meshes <file.atlas> [--cell N] [--alpha-threshold N] [--bin] [--check]` 寫出（docs/14 §5）。sprite 依
  `--cell` 像素的格子切（預設 8）。每個有可見像素的格子變成兩個三角形，與相鄰格子共用角，所以沒有三角形蓋到空
  格子。子 sprite 與旋轉的區域沒有網格，以四邊形繪製。標為 `"hand"` 的網格會保留。帶 `"cells"` 的網格（由動畫編輯器的
  **網格格數** 依格切割）會以同樣的格數再切一次。
- 讀取兩種形式都接受；寫出時擇一。對 TOMS 32 × 32 的 sprite 來說**純文字比較小**：`game.atlas` 的 260 個 sprite
  是 97 KB，二進位則是 40 KB 的 JSON 加 86 KB 的 `.bin`。緩衝每個網格的位移比文字中的短整數還佔空間。遊戲
  出貨用純文字；`--bin` 給大型美術用（數百個頂點的角色）。

### 4.2 蒙皮（第 3 步）：`.anim` 第 3 版

節點多了選用的 `skin`（`anim_clip.h` 的 `Node::skinBones`／`skinWeights`）：

```jsonc
{ "name": "golem", "sprite": "golem", "pivot": [0.5, 1],
  "skin": { "bones": ["hip", "chest", "head"],   // 這個片段的節點，以名稱指定
            "weights": "auto" } }                  // 或每個頂點一組 [[bone, weight, bone, weight], ...]（最多 4 根骨頭）
```

- 只有節點有蒙皮時檔案才寫成版本 3；其他檔案仍是版本 2，舊版程式照樣能開。
- 骨頭的**靜止姿勢**（它們的靜止位置／旋轉／縮放，不看關鍵影格：Spine 的 setup pose）是網格未彎曲時的位置。
  每一格頂點會被它的骨頭從那裡帶走：`Σ 權重 × 骨頭(現在) × 骨頭(靜止)⁻¹ × 頂點(靜止)`。蒙皮節點自己的關鍵
  影格不移動網格，移動網格的是它的骨頭（和 Spine 一樣）。
- **自動權重：**
  - 每根骨頭是一條線段：從它的原點到它第一個也是骨頭的子節點。最後一根骨頭（頭、手）從原點再往外延伸，
    長度等於父骨頭到它的距離。
  - 頂點對一根骨頭的權重是 1 / (距離² + 1)。保留最大的 4 個，縮放到總和為 1。
  - 權重在繪製片段時才算，所以會跟著重新切的網格走。
- 手繪的權重屬於某一個網格。網格頂點數改變時，片段改用自動權重。
- 沒有網格的蒙皮 sprite 以四邊形繪製。

### 4.3 晶格（第 4 步，尚未做）：`lattice` + `tracks.deform`

```jsonc
"lattice": [4, 4],                         // 橫向與縱向的點數：預設 4 x 4，每個節點可設 2 x 2 .. 8 x 8
"tracks": { "deform": [ {"t": 0, "v": {}},
                        {"t": 0.5, "v": {"5": [0, -2], "6": [0, -2]}, "ease": "sinusoidalInOut"} ] }   // 只列出有動的點
```

### 4.4 大小

一個有 10 個節點、3 個網格部件的角色，1 秒的待機動作，5 個關鍵影格：

| | 大小 |
|---|---|
| 節點關鍵影格（骨頭） | 約 3 KB JSON |
| 蒙皮（`"auto"`） | 每個部件幾十個位元組 |
| 晶格關鍵影格（第 4 步）：3 個部件 × 5 個關鍵影格 × ≤16 個點 | 約 2 KB |
| 網格（所有片段共用）：3 × 約 60 個頂點 | 約 3 KB |
| 舊 `.mx`／Spine 式逐頂點變形做同樣的事 | 3 × 60 × 5 × 12 B ≈ 11 KB 二進位，JSON 更大 |

## 5. 執行時

- **載入。** `AtlasRes`（資源快取）與遊戲自己的 sprite 圖集會讀圖集旁的 `<atlas>.mesh.json`（以及它的 `.bin`）；
  每個 `AtlasSet` 項目帶著它的網格。沒有檔案就沒有網格；壞掉的檔案會記錄下來，它的 sprite 以四邊形繪製。動畫
  編輯器用同樣方式讀取。
- **繪製。**
  - `appendDraws(clip, poses, …)`（anim_player.h）依繪製順序產生四邊形與網格的 `DrawList`：有網格的蒙皮節點
    會被彎曲，其他都是 `appendQuads` 做出的四邊形。
  - `AnimPlayer::draw` 每格彎曲蒙皮節點，其餘沿用快取的四邊形。
  - 遊戲地圖上的片段（`drawClipInRect`）、`--anim` 預覽，以及動畫編輯器的遊戲算圖器檢視都使用 `appendDraws`。
  - `appendQuads` 不變：蒙皮節點在那裡給出未彎曲的四邊形。編輯器的 QPainter 備援、選取外框與關卡編輯器仍用它。
- **`IRenderer::drawMesh(const Mesh&)`**（第 1 步）：以像素計的頂點加 UV、16 位元索引、一個色調、加法或一般混合、
  任何貼圖。網格依送出順序畫在四邊形之間：sprite 批次在網格處切開，與在 GPU 粒子發射器處切開的方式相同。每格一
  個動態頂點與索引緩衝，用 CPU 路徑的 sprite 程式，不需要新的著色器；每個後端與網頁都能用。`validMesh`
  （render_iface.h）會擋掉壞掉的網格。
- **成本。** 蒙皮是每格每個頂點的 CPU 工作（golem：25 個頂點 × 3 根骨頭），每個蒙皮網格是一次繪製呼叫。畫面上
  有幾個角色沒問題；同時有許多蒙皮怪物時，網格需要合批（以後）。
- **美術風格。** 美術風格換掉圖集與它的網格檔；片段不變。

## 6. 計畫

| 步驟 | 內容 | 看得到的結果 | 狀態 |
|---|---|---|---|
| 1 | `IRenderer::drawMesh` + bgfx | `toms_game --mesh-test=golem`：golem 以擺動的 8 × 8 網格畫在原本的旁邊；`smoke.mesh`、`smoke.mesh_cpu` | **2026-10-21 完成** |
| 2 | 從 alpha 切網格：`sprite_mesh.h`、`atlaspack meshes`，純文字或 `.bin`；由遊戲、資源快取與動畫編輯器載入 | 原始美術與 `dark16` 的 `game.mesh.json` | **2026-10-21 完成** |
| 3 | 蒙皮：`.anim` 第 3 版的 `skin`、自動權重、`appendDraws`、編輯器的 **Skin** 欄位 | `tests/smoke/anim_skin.anim`：golem 被 hip／chest／head 彎曲，腳不動；`smoke.anim_skin` | **2026-10-21 完成** |
| 4 | 晶格變形：`lattice`（預設 4 × 4，每個節點可調）+ `tracks.deform`；動畫編輯器檢視區的晶格模式，關鍵影格清單的 Deform 欄 | 任何 sprite 都能呼吸／壓扁／擺動 | 下一步 |
| 5 | AI 連接：換皮流程以換皮後的圖集重做網格；給骨頭與晶格用的動作範本（docs/16），可由腳本或 AI 填入；編輯器中的權重檢視與手繪；檢查每個蒙皮 sprite 都有網格 | 換皮或改形的角色播放它原本的片段 | |
| 6 | 以後：Spine JSON → `.anim` 匯入器；舊 `.mx` 匯入器；發行用的二進位 `.anim`；蒙皮網格合批 | 現有的 Spine 美術不需 Spine 也能在 TOMS 跑 | 暫時略過（第 8 節） |

## 7. 完成了什麼

**第 1 步——算圖器中的網格**

- `src/core/engine/render_iface.h`：`MeshVertex`、`Mesh`、`validMesh()`、`IRenderer::drawMesh`（預設不做任何事）。
- `src/engine/src/bgfx_renderer.*`：`drawMesh` 把網格複製進這一格的緩衝，並在四邊形之間放一個標記；
  `uploadMeshes()` 在 `end()` 中一次送出；`drawGpuAt` 用 sprite 程式畫網格標記。
- `--mesh-test=<sprite>`（`Game::playPreviewMesh`／`drawPreviewMesh`）：那個 sprite 放大 4 倍、以擺動的 8 × 8 網格
  畫出，原本的 sprite 在旁邊。

**第 2 步——sprite 網格**

- `src/core/engine/sprite_mesh.h/.cpp`：`SpriteMesh`、`SpriteMeshFile`、`parseSpriteMeshes`（文字與緩衝）、
  `spriteMeshesToJson`（文字，或 JSON + 二進位）、`meshFromAlpha`、`spriteMeshPath`。
- `atlaspack meshes`（`tools/atlas/cli/atlas_cli.cpp`）。
- `resource_types.*`：`readSpriteMeshes`、`AtlasRes::meshes()`，`AtlasList` 把它們交給 `AtlasSet`。
  `game_assets.cpp`：遊戲圖集的網格（`Game::spriteMeshes_`），隨圖集一起提供。
- `assets/media/atlas/game.mesh.json`、`assets/media/styles/dark16/atlas/game.mesh.json`。

**第 3 步——蒙皮**

- `anim_clip.*`：`Node::skinBones`、`skinWeights`、`skin` JSON、`kAnimVersion = 3`。
- `anim_player.*`：`AtlasSet::findMesh`、`DrawList`、`appendDraws`、`makeSkinnedMesh`、`autoSkinWeights`、
  `restWorlds`；`AnimPlayer::draw` 彎曲蒙皮節點。
- 動畫編輯器：
  - `AnimDocument` 載入每個圖集的網格；
  - 遊戲算圖器檢視用 `appendDraws` 繪製；
  - **Properties → Skin（蒙皮）** 設定骨頭（可復原；改了骨頭就捨棄手繪的權重）。欄位下方的說明列出網格的頂點與三角形、
    骨頭以及權重是否自動，或 sprite 為何以四邊形繪製（沒有網格：執行 `atlaspack meshes`；旋轉的區域；沒有此節點的
    骨頭名稱）；
  - **檢視 > 顯示蒙皮**（Shift+K，會記住）在每個蒙皮節點上畫出（`AnimViewport::drawSkins`，來自 `anim::skinView`，
    與遊戲相同的彎曲）：
    - 網格的三角形，以細線表示；
    - 它的頂點，每個依所跟隨的骨頭上色，依權重混色（每根骨頭有自己的顏色）；
    - 它的骨頭：自動權重所量的線段，在骨頭目前帶到的位置，附上名稱（沒有此節點的名稱：紅色虛線）；
    - 選取一根骨頭時，頂點從該骨頭的顏色（完全跟隨）到暗色（不跟隨）著色，該骨頭加上外框。
    兩種檢視（遊戲算圖器與 QPainter）上都會畫。隱藏的蒙皮節點不顯示；
  - **物件模式（O）／骨頭模式（B）**（工具列、編輯選單，會記住）：在檢視區點選會選到什麼。骨頭模式顯示蒙皮，點選
    骨頭的關節或線段（`AnimViewport::boneAt`），不會選到精靈圖；控制點接著擺動骨頭（自動關鍵影格開啟）或把它放到精靈圖上
    （關閉：靜止姿勢）；
  - **骨架是精靈圖加上它底下的骨頭**，所以移動精靈圖時網格與骨頭一起移動。Properties 對節點外的蒙皮骨頭發出警告
    （`animed::bonesOutside`），並提供 **把骨頭移到此精靈圖底下**（`AnimDocument::parentBonesUnder`：骨頭子樹變成節點的
    子節點，靜止位置與位置關鍵影格跟著平移，畫面不動；節點相對於原父節點有旋轉或縮放時拒絕）；
  - **網格格數**（Properties）：`AnimDocument::setMeshGrid` 以 `meshFromAlphaGrid` 依 N × N 格（1–32）重新切割精靈圖的
    網格，並以原本的形式寫回圖集的網格檔；
  - 檢查會對不是片段節點的蒙皮骨頭發出警告（`anim_check.cpp`）；
  - 中文介面文字在 `tools/studio_common/i18n/zh_TW/anim.json`。
- `tests/smoke/anim_skin.anim`：示範片段。

**測試**

| 測試 | 檢查內容 |
|---|---|
| `unit.render_mesh_test` | `validMesh` 接受哪些網格 |
| `unit.sprite_mesh_test` | 從 alpha 切網格；文字與二進位讀回相同；錯誤；出貨的 `game.mesh.json` 符合 `game.atlas` |
| `unit.anim_skin_test` | 檔案中的蒙皮（只有用到時才是版本 3）；靜止時網格正好落在四邊形上；轉動的骨頭帶走它附近的頂點；自動權重；退回四邊形的情況；`AnimPlayer` 依序畫網格與四邊形；`skinView` 給出繪製的頂點、骨頭線段，以及無法顯示蒙皮的原因 |
| `anim.editor_selftest`（蒙皮部分） | 對 `anim_skin.anim`：Properties 的說明、檢視 > 顯示蒙皮畫出骨頭、選取 chest 時的畫面（`skin_view.png`、`skin_bone.png`） |
| `anim.check_skin` | 對示範片段執行 `anim_editor --headless check` |
| `smoke.mesh`、`smoke.mesh_cpu` | 截圖對 `tests/golden/mesh.png` 比對 |
| `smoke.anim_skin` | 截圖對 `tests/golden/anim_skin.png` 比對 |

**已知限制**

- 編輯器只在遊戲算圖器檢視中把蒙皮節點的 sprite 畫成彎曲的。它的選取外框與 QPainter 備援（沒有 GPU 時）顯示未彎曲的
  靜止四邊形；顯示蒙皮的線與頂點在兩者中都是彎曲的。
- 權重是自動的或寫在檔案裡；編輯器還不能手繪（第 5 步）。
- 網格的頂點還不能手動拖曳；網格格數只能讓它變粗或變細。
- 旋轉的圖集區域沒有網格。TOMS 的圖集目前不旋轉。

## 8. 擁有者的決定（2026-10-21）

1. **晶格大小：** 每個節點可調，預設 4 × 4。
2. **網格儲存：** 像 glTF，儲存時選擇純文字或二進位緩衝。讀取兩種都接受。
3. **Spine 匯入器：** 暫時略過。有用的匯入器需要先有蒙皮與變形（Spine 角色大多是帶變形關鍵影格的加權網格），
   在那之上還要再花好幾天；只有骨頭的匯入器約一天，但能匯入的東西很少。第 4 步之後再評估。
4. **順序：** 先做蒙皮，再做晶格。原本的動畫編輯器必須照常運作：它的檢查與自我測試通過，沒有蒙皮的片段畫法與
   存檔都和以前完全一樣（版本 2）。
