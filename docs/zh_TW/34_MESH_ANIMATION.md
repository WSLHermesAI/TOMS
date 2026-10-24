> 英文原文：[34_MESH_ANIMATION.md](../34_MESH_ANIMATION.md)

# 34 — 網格動畫：取代 Spine（設計）

這是一份設計：在 TOMS 自己的 `.anim` 系統裡，用彎曲 sprite 的方式做動畫，就像 Spine 那樣。目標是：

- AI 換皮後的美術可以直接放進來，不必重做動畫；
- **形狀**改變的美術也能保留它的動作；
- 檔案小；
- 動作簡單到 AI 也能寫。

狀態（2026-10-22）：計畫（第 6 節）的第 1–4 步**已完成**，動畫編輯器也有對應的工具（第 7 節）：算圖器能畫三角網格，
sprite 有從 alpha 切出的網格，片段以蒙皮與變形晶格彎曲它們。第 5 步進行中：動畫工具把片段需要的網格加進它的圖集旁的
網格檔，沒有網格的蒙皮 sprite 會被回報；第 1 部分 `tools/refresh_meshes.py` 在圖集改變後更新每個圖集（包括美術風格）的
網格，第 2 部分動作範本替節點寫出呼吸、擺動或點頭的關鍵影格（兩者都在 2026-10-22 完成），第 3 部分編輯器的權重模式
以熱度圖顯示骨頭的權重並可手繪（2026-10-23 完成）。第 6 步暫時略過。擁有者的決定在第 8 節。編輯器的逐步教學（蒙皮、晶格、權重手繪）在 [35_SKINNING_TUTORIAL.md](35_SKINNING_TUTORIAL.md)。

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

網格屬於圖集的 sprite：每個用到這個圖集的片段都共用它，所以不同的 `.anim` 檔之間沒有重複的資料。它放在圖集旁邊：
`game.atlas` → `game.mesh.json`（二進位時另有 `game.mesh.bin`；`toms::spriteMeshPath(atlasPath)`）。出貨的是
`assets/media/atlas/game.mesh.json` + `game.mesh.bin`，內有 `golem`（給 `tests/smoke/anim_skin.anim`）與 `slime`
（給 `tests/smoke/anim_deform.anim`）。只有動畫工具會寫它；圖集工具（atlaspack、圖集編輯器）從不讀寫網格。（舊的
`atlaspack meshes` 指令已移除：請改用下面的 `anim_editor --headless meshes`。）和 glTF 一樣，數字可以放在 JSON 裡，
也可以放在旁邊的二進位緩衝：

```jsonc
{ "version": 1,
  "buffers": [{"uri": "game.mesh.bin", "byteLength": 588}],               // 只有用二進位緩衝時
  "meshes": {
    "golem@6x8": { "source": "alpha", "cells": [6, 8],                    // 從 alpha 切出，或 "hand"（保留）；格數
                   "vertices": [0,0, 8,0, ...], "triangles": [0,1,2, ...] },  // 純文字，或
    "slime": { "source": "alpha", "vertexCount": 16, "vertices":  {"buffer": 0, "byteOffset": 352},   // float32 x, y
                                  "indexCount": 54,  "triangles": {"buffer": 0, "byteOffset": 480} }  // uint16
  } }
```

- **需要哪些網格由片段決定。** `.anim` 寫明它用的圖集（`atlases`）；它的蒙皮或變形節點寫明 sprite（靜止 sprite 與
  sprite 關鍵影格）與格子。每個這樣的 sprite 都需要一個網格，放在它來源圖集的網格檔裡（`meshesNeeded(file|clip, atlases)`，
  每個給出一個 `MeshNeed{key, entry, region, cols, rows}`）。
- **格子。** 節點可設 `"meshGrid": [cols, rows]`（各 1 .. 32，`kMaxMeshGrid`）：切割它的 sprite 用的格子。沒有時，
  sprite 依 8 像素的正方形格子切（`kMeshCell`）。有 `meshGrid` 的檔案是版本 3，和有蒙皮或晶格的檔案一樣。
  `cutMesh(need, rgba, stride)` 切出一個網格（依節點的格子用 `meshFromAlphaGrid`，依 8 像素格子用 `meshFromAlpha`）：
  每個有可見像素的格子變成兩個三角形，與相鄰格子共用角，所以沒有三角形蓋到空格子。子 sprite 與旋轉的區域沒有網格，
  以四邊形繪製。
- **鍵。** `meshKey(sprite, cols, rows)`：sprite 名稱，節點有 `meshGrid` 時再加 `"@CxR"`：`golem`（8 像素格子）、
  `golem@6x8`。所以一個 sprite 可以有多個網格，每種格子一個，由所有要用它的片段共用。繪製時以
  `AtlasSet::findMesh(ref, node)` 在 sprite 來源圖集的網格檔中找到節點的網格（`AtlasSet::meshKeyFor(ref, node)` 給出它的鍵）。
- 頂點以 sprite 原圖（未裁切）像素計（x 向右，y 從左上向下）。UV 不存，繪製時由區域算出，所以重新打包或裁切圖集
  不會讓網格失效。UV 會被限制在區域內，所以從別的打包或美術風格切出的網格不會取樣到相鄰的 sprite：它只是沿用
  切割時的輪廓。
- **`"version"` 一定是第一個鍵**，也就是檔頭：檔案的格式版本（`kSpriteMeshVersion`，目前為 1）。沒有版本，或版本比這個
  建置能讀的還新，都是錯誤，所以舊版遊戲不會誤讀新檔案。
- **由動畫編輯器加入**（第 7 節第 5 步）：儲存片段檔時，它重新讀取圖集的網格檔，把這個檔案需要的網格合併進去。
  其他片段的網格會保留，`"hand"` 網格也會保留。
- **不用編輯器：** `anim_editor --headless meshes <file.anim>... [--atlas [<id>=]<file.atlas>]... [--bin|--text]
  [--check] [--prune]` 把片段的網格加進它們用到的每個圖集的網格檔，從該圖集的像素切出。
  - `--atlas` 取代每個檔案的圖集清單，例如美術風格的副本：
    `--atlas game=assets/media/styles/dark16/atlas/game.atlas` 寫出 `styles/dark16/atlas/game.mesh.json`，從 dark16 的
    像素切出。
  - `--prune`：每個網格檔正好只留下所給片段檔需要的網格。一個片段無法知道其他片段用了什麼，所以修剪是另外明確執行的
    步驟，要給完整的片段清單。
  - `--check` 不寫檔：缺少需要的網格或網格已過時，或（加上 `--prune` 時）檔案中有清單上的片段都沒用到的網格時，以 1 結束。
  - `--default-atlas [<id>=]<file.atlas>`：沒有寫明圖集的片段所用的圖集清單（遊戲用它的 sprite 圖集畫這些片段）。
  - `--map <from.atlas>=<to.atlas>`：片段用到某個圖集的地方，都換成另一個圖集。
- **圖集改變後**（`atlaspack build art_raw/atlas/game.atlasproj --all-variants`、新的 AI sprite、換皮的美術風格）：
  執行 `python tools/refresh_meshes.py`（第 7 節第 5 步）。它會更新每個網格檔，包括美術風格的。
- 讀取兩種形式都接受。寫出時沿用檔案原本的形式；新檔案用二進位；`--bin` 或 `--text` 可指定。**出貨用二進位**
  （2026-10-22，擁有者的選擇）：以 260 個 32 × 32 的 sprite 量測，純文字解析需 3.7 ms，JSON + `.bin` 需 0.8 ms
  （約快 5 倍，release 建置）。純文字仍適合閱讀網格或手寫網格。

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

### 4.3 晶格（第 4 步）：`lattice` + `tracks.deform`

```jsonc
"lattice": [4, 4],                         // 橫向與縱向的點數：預設 4 x 4，每個節點各方向可設 2 .. 16
"tracks": { "deform": [ {"t": 0, "v": {}},
                        {"t": 0.5, "v": {"5": [0, -2], "6": [0, -2]}, "ease": "sinusoidalInOut"} ] }   // 只列出有動的點
```

- 晶格覆蓋 sprite 的**原始圖片**（0 .. 寬、0 .. 高），點平均分布，逐列編號（`row * cols + col`）。關鍵影格的值列出偏離原位的點，
  以 sprite 像素計；沒列出的點在那裡為 0。關鍵影格和其他內插通道一樣有緩動（`deformAt`）。
- 每個網格頂點取它所在格子的位移，雙線性內插（`latticeOffset`）。變形先套用：蒙皮節點的骨頭再帶著變形後的形狀。
- 圖集的網格檔中沒有網格的變形 sprite，會用自己的一般格子彎曲（晶格的兩倍）。蒙皮的 sprite 仍需要網格。
- 寫成版本 3（只有節點有晶格或變形關鍵影格時）。

### 4.4 大小

一個有 10 個節點、3 個網格部件的角色，1 秒的待機動作，5 個關鍵影格：

| | 大小 |
|---|---|
| 節點關鍵影格（骨頭） | 約 3 KB JSON |
| 蒙皮（`"auto"`） | 每個部件幾十個位元組 |
| 晶格關鍵影格（第 4 步）：3 個部件 × 5 個關鍵影格 × ≤16 個點 | 約 2 KB |
| 網格（用到該圖集的片段共用）：3 × 約 60 個頂點 | 約 3 KB |
| 舊 `.mx`／Spine 式逐頂點變形做同樣的事 | 3 × 60 × 5 × 12 B ≈ 11 KB 二進位，JSON 更大 |

## 5. 執行時

- **載入。** `AtlasRes::load`（資源快取）讀取每個圖集旁的 `<atlas>.mesh.json`（以及它的 `.bin`；
  `res::readMeshFile(spriteMeshPath(path))`）到 `AtlasRes::meshes()`；`AtlasList` 把它們交給 `AtlasSet::Entry::meshes`。
  `AnimRes` 沒有自己的網格：它的片段透過圖集找到網格。遊戲自己的 sprite 圖集（由遊戲提供，跟著美術風格）載入目前風格
  圖集旁的網格檔，沒有時改用原始美術的 `media/atlas/game.mesh.json`。沒有檔案就沒有網格；壞掉的檔案會記錄下來，它的
  sprite 以四邊形繪製。沒有網格的蒙皮 sprite 仍會畫出，以平的四邊形，載入時每個 sprite 印一行 `[res]` 警告；沒有網格的
  變形 sprite 仍會彎曲，用一般格子。動畫編輯器載入每個圖集的網格檔，並加入它的片段需要的網格（第 7 節第 5 步）。
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
- **美術風格。** 美術風格換掉圖集，也一併換掉旁邊的網格檔（`assets/media/styles/<id>/atlas/game.mesh.json`，由
  `tools/refresh_meshes.py` 從該風格自己的像素寫出）；片段不變。沒有網格檔的風格使用原始美術的網格：UV 來自該風格的區域並限制在區域內，所以不會取樣到相鄰的 sprite，但保持原始的
  輪廓（見已知限制）。

## 6. 計畫

| 步驟 | 內容 | 看得到的結果 | 狀態 |
|---|---|---|---|
| 1 | `IRenderer::drawMesh` + bgfx | `toms_game --mesh-test=golem`：golem 以擺動的 8 × 8 網格畫在原本的旁邊；`smoke.mesh`、`smoke.mesh_cpu` | **2026-10-21 完成** |
| 2 | 從 alpha 切網格：`sprite_mesh.h`，純文字或 `.bin`，每個圖集旁一份網格檔，由動畫工具寫出；遊戲、資源快取與動畫編輯器隨圖集載入 | `assets/media/atlas/game.mesh.json`，內有 golem 與史萊姆 | **2026-10-21 – 22 完成** |
| 3 | 蒙皮：`.anim` 第 3 版的 `skin`、自動權重、`appendDraws`、編輯器的 **Skin** 欄位 | `tests/smoke/anim_skin.anim`：golem 被 hip／chest／head 彎曲，腳不動；`smoke.anim_skin` | **2026-10-21 完成** |
| 4 | 晶格變形：`lattice`（預設 4 × 4，每個節點可調）+ `tracks.deform`；動畫編輯器檢視區的晶格模式，關鍵影格清單的 Deform 欄 | 任何 sprite 都能呼吸／壓扁／擺動：`tests/smoke/anim_deform.anim`（`smoke.anim_deform`） | **2026-10-22 完成** |
| 5 | AI 連接：第 1 部分，以重建或換皮後的圖集重做網格（流程中的 `tools/refresh_meshes.py`）；第 2 部分，給晶格與骨頭用的動作範本（docs/16 §7），可由編輯器、腳本或 AI 套用；第 3 部分，編輯器中的權重檢視與手繪；檢查每個蒙皮 sprite 都有網格 | 換皮或改形的角色播放它原本的片段 | 編輯器的網格切割、`--headless meshes`、網格檢查、第 1 與第 2 部分 **2026-10-22 完成**；第 3 部分 **2026-10-23 完成** |
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
  `spriteMeshesToJson`（文字，或 JSON + 二進位）、`meshFromAlpha`、`meshFromAlphaGrid`。
- `anim_player.*`：`meshKey`、`MeshNeed`、`meshesNeeded`、`cutMesh`、`AtlasSet::Entry::meshes`、`AtlasSet::findMesh`／
  `meshKeyFor`；`anim_clip.*`：`Node::meshGrid`、`kMaxMeshGrid`、`kMeshCell`。
- `resource_types.*`：`res::readMeshFile`；`AtlasRes::load` 讀取圖集旁的網格檔（`AtlasRes::meshes()`），`AtlasList` 把它交給
  `AtlasSet` 的項目。`game_assets.cpp`：遊戲圖集的網格（`Game::spriteMeshes_`，在風格的圖集旁，否則用原始美術的），隨圖集一起提供。
- `tools/anim/core/anim_meshes.h/.cpp`：`addAnimMeshes`（把檔案中片段需要的每個網格加進它的圖集的網格檔，從圖集頁面的
  像素切出；其他網格與 `"hand"` 網格保留）、`readMeshFile`、`writeMeshFile`、`headlessMeshes`（`anim_editor --headless meshes`）。
- `assets/media/atlas/game.mesh.json` + `game.mesh.bin`：`golem`（給 `tests/smoke/anim_skin.anim`）與 `slime`（給
  `tests/smoke/anim_deform.anim`）。遊戲自己的片段（`assets/media/anim`）還沒用到蒙皮或晶格。

**第 3 步——蒙皮**

- `anim_clip.*`：`Node::skinBones`、`skinWeights`、`skin` JSON、`kAnimVersion = 3`。
- `anim_player.*`：`AtlasSet::findMesh`、`DrawList`、`appendDraws`、`makeSkinnedMesh`、`autoSkinWeights`、
  `restWorlds`；`AnimPlayer::draw` 彎曲蒙皮節點。
- 動畫編輯器：
  - `AnimDocument` 載入每個圖集的網格檔，並加入片段需要的網格（第 5 步）；
  - 遊戲算圖器檢視用 `appendDraws` 繪製；
  - **Properties → Skin（蒙皮）** 設定骨頭（可復原；改了骨頭就捨棄手繪的權重）。欄位下方的說明列出網格的頂點與三角形、
    骨頭以及權重是否自動，或 sprite 為何以四邊形繪製（沒有網格：旋轉的區域或子區域，或沒有像素的 sprite；沒有此節點的
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
  - **網格格數**（Properties，x 與 y，1–32；蒙皮或變形節點才顯示）：`AnimDocument::setMeshGrid` 以一個復原步驟設定
    節點的 `meshGrid`，網格立即依新的格子重新切割（第 5 步）；
  - 檢查會對不是片段節點的蒙皮骨頭發出警告（`anim_check.cpp`）；
  - 中文介面文字在 `tools/studio_common/i18n/zh_TW/anim.json`。
- `tests/smoke/anim_skin.anim`：示範片段。

**第 4 步——變形晶格**

- `anim_clip.*`：`Deform`、`Node::lattice`／`deformKeys`／`usesMesh()`、`deformAt`、`lattice` 與 `tracks.deform` 的 JSON、
  `NodePose::time`（節點自己的時間，給它的關鍵影格用）。
- `anim_player.*`：網格路徑也處理變形節點（sprite 沒有網格時用一般格子）；`latticeOffset`；`latticeView`（編輯器顯示與拖曳的晶格）。
- 動畫編輯器：
  - **晶格模式（L）**（工具列、編輯選單，和其他模式一起記住）：顯示選取 sprite 的晶格；拖曳一個點即在播放頭設定變形關鍵影格
    （一律是關鍵影格：變形沒有靜止值；像素吸附會取整數）。整個拖曳是一個復原步驟；拖曳時會暫停播放；
  - **Properties → 晶格**，x 與 y（2 – 16）：晶格大小。關鍵影格會保留，每個移動過的點移到新格子中最近的點；
  - 關鍵影格清單的 **Deform（變形）** 欄：移動過的點以文字顯示，`5: 0, -2; 6: 0, -2`，可像其他格子一樣編輯。
- `tests/smoke/anim_deform.anim`：以 4 × 4 晶格壓扁的史萊姆，旁邊是原本的史萊姆。

**測試**

| 測試 | 檢查內容 |
|---|---|
| `unit.render_mesh_test` | `validMesh` 接受哪些網格 |
| `unit.sprite_mesh_test` | 從 alpha 切網格；文字與二進位讀回相同；兩種形式中 `"version"` 都是第一個鍵，沒有版本的檔案是錯誤；其他錯誤 |
| `unit.anim_skin_test` | 檔案中的蒙皮（只有用到時才是版本 3）；靜止時網格正好落在四邊形上；轉動的骨頭帶走它附近的頂點；自動權重；退回四邊形的情況；`AnimPlayer` 依序畫網格與四邊形；`skinView` 給出繪製的頂點、骨頭線段，以及無法顯示蒙皮的原因 |
| `anim.editor_selftest`（蒙皮部分） | 對 `anim_skin.anim`：Properties 的說明、檢視 > 顯示蒙皮畫出骨頭、選取 chest 時的畫面（`skin_view.png`、`skin_bone.png`） |
| `anim.check_skin` | 對示範片段執行 `anim_editor --headless check` |
| `smoke.mesh`、`smoke.mesh_cpu` | 截圖對 `tests/golden/mesh.png` 比對 |
| `smoke.anim_skin` | 截圖對 `tests/golden/anim_skin.png` 比對 |
| `unit.anim_lattice_test` | 檔案中的晶格（版本 3、錯誤）；緩動後的位移；格子內的雙線性位移；變形 sprite 畫成彎曲的網格（沒有網格時用一般格子）；編輯器顯示的晶格 |
| `anim.editor_selftest`（晶格部分） | 對 `anim_deform.anim`：晶格模式，拖曳一個點在播放頭設定變形關鍵影格且只佔一個復原步驟，調整晶格大小會重新對應關鍵影格（`lattice_drag.png`） |
| `anim.check_deform`、`smoke.anim_deform` | 示範片段；截圖對 `tests/golden/anim_deform.png` 比對 |

**第 5 步（部分）——由動畫工具加入網格，並受檢查**

- 動畫編輯器（`AnimDocument`）載入每個圖集的網格檔。片段變動時，它加入這個 `.anim` 需要的網格，從該圖集的像素切出並依鍵
  快取；每次編輯、復原、重做與重新載入圖集都會更新。編輯器用它們繪製。儲存片段檔時，它重新讀取圖集的網格檔，把這個檔案的
  網格合併進去（`addAnimMeshes`、`writeMeshFile`）：其他片段的網格保留，`"hand"` 網格也保留；檔案沿用原本的形式，新檔案用二進位。
- **Properties → 網格格數** x／y（蒙皮或變形節點）設定節點的 `meshGrid`：一個復原步驟，網格立即重新切割。把 golem 設成
  8 × 8 會在 `golem` 旁加入 `golem@8x8`（其他片段可能用到原本那個）。
- `anim_editor --headless meshes <file.anim>... [--atlas [<id>=]<file.atlas>]... [--bin|--text] [--check] [--prune]`
  （`headlessMeshes`）：不開編輯器做同樣的事，可一次處理多個片段檔（第 4.1 節）。
- `anim_player.*`：`skinnedWithoutMesh(clip, atlases)`——每個蒙皮節點中，在它圖集的網格檔裡沒有網格的 sprite。
- 檢查（`anim_check.*`，也就是 `anim_editor --headless check` 與編輯器的 **Problems**）會讀取每個圖集旁的網格檔
  （`CheckAtlas::meshes`）：每個這樣的 sprite 一個**錯誤**——「skinned, but the sprite 'golem' has no mesh 'golem' in
  <atlas>.mesh.json (there is no such file): … Save the clip in the anim editor, or run anim_editor --headless meshes」。
- 遊戲（`resource_types.cpp`，`AnimRes::load`）：載入片段時，每個這樣的 sprite 印一行 `[res]`；sprite 仍會畫出，
  以四邊形。

*第 1 部分——流程：`tools/refresh_meshes.py`*

- 圖集改變後執行它：`atlaspack build art_raw/atlas/game.atlasproj --all-variants`、新的 AI sprite（docs/27），
  或換皮的美術風格（docs/13）。
- 它讀取 `assets/`、`tests/smoke/` 與 `docs/examples/` 底下的每個 `.anim`，並執行
  `anim_editor --headless meshes <所有片段> --default-atlas game=assets/media/atlas/game.atlas --prune`：基本圖集的
  網格檔正好只有片段需要的網格。
- 接著對每個美術風格 `assets/media/styles/<id>/atlas/game.atlas`，加上 `--map assets/media/atlas/game.atlas=<風格的圖集>`
  再執行一次：寫出該風格自己的 `game.mesh.json`，從它自己的像素切出。`assets/media/styles/dark16/atlas/game.mesh.json`
  + `.bin` 就是這樣產生的。
- `python tools/refresh_meshes.py --check` 不寫檔：任何網格檔過時就以 1 結束。`--anim-editor <exe>` 指定動畫編輯器
  （預設：找到的第一個建置）。

*第 2 部分——動作範本*

- `tools/anim/core/anim_recipes.h/.cpp`（不用 Qt）：`motionRecipes()`、`findMotionRecipe(id)`、
  `applyMotionRecipe(clip, nodePath, id, RecipeParams{start, period, strength, spriteW, spriteH}, err)`、
  `headlessRecipe(args)`。晶格範本（`breathe`、`sway`、`squash`、`jelly`）替節點的變形晶格設定關鍵影格；骨頭範本
  （`bone_sway`、`nod`、`swing`）替蒙皮節點的骨頭設定 z 旋轉關鍵影格。清單與規則在 docs/16 §7。
- 編輯器：對選取的節點使用 **Key > Motion Recipe…**；套用是一個復原步驟。
- Headless：`anim_editor --headless recipe <file.anim> --node <name> --recipe <id> [...]` 與
  `anim_editor --headless recipe --list`。

| 測試 | 檢查內容 |
|---|---|
| `anim.meshes_up_to_date` | `python tools/refresh_meshes.py --check --anim-editor <anim_editor>`（只在找得到 Python 3 時）：每個圖集的網格檔（包括美術風格的）正好只有片段需要的網格，而且是重新切割的結果 |
| `anim.recipes` | `tools/anim/tests/anim_recipes_test.cpp`：每個範本的關鍵影格與形狀、範圍外的關鍵影格保留、片段長度變長、骨頭範本從每根骨頭的靜止角度起算、拒絕的情況、寫出的檔案讀回相同（docs/16 §7） |
| `anim.recipe_headless` | `--headless recipe --list`；把 breathe 與 nod 寫進蒙皮 golem 的副本；對沒有蒙皮的骨頭套用骨頭範本會被拒絕；結果通過 `--headless check` |
| `anim.editor_selftest`（範本部分） | 在 golem 上套用 breathe 與 nod，各是一個復原步驟；在 hip 上套用 swing 會被拒絕 |
| `anim.check_skin_meshes` | 對 `game.atlas` + `.png` 的副本（沒有網格檔）執行 `anim_skin.anim` 的 `--headless check`：是錯誤；把 `game.mesh.*` 複製到旁邊後：沒有錯誤 |
| `anim.editor_selftest`（網格格數部分） | golem 在圖集網格檔中的鍵是 `golem`；設成 8 × 8 是一個復原步驟、存在節點中，並在旁邊加入 `golem@8x8` |
| `unit.anim_skin_test` | `meshKey("golem")`／`meshKey("golem", 6, 8)`、`meshesNeeded`、`cutMesh`、透過圖集項目的網格做 `findMesh`；`skinnedWithoutMesh`：節點與 sprite；網格存在時沒有；不在任何圖集中的 sprite 交給另一項檢查 |

*第 3 部分——權重檢視與權重手繪（2026-10-23 完成）*

- `tools/anim/core/anim_weights.h/.cpp`（不用 Qt）：一次塗抹用 `paintWeights(weights, boneCount, bone, mode, amount,
  triangles, fallback)`，另有 `brushFalloff(dist, radius)`（(1 - (d/r)²)²）與 `boneWeight(weights, vertex, bone)`。模式：
  - **增加**：該骨頭的比例朝 1 增加，其他骨頭按比例減少；
  - **減少**：朝 0 減少，其他骨頭按比例增加。只被這根骨頭帶動的頂點，由最近的另一根骨頭（依目前畫出的線段）接手；
  - **平滑**：每個比例朝頂點在網格中鄰居的平均移動。

  每個畫過的頂點總和仍是 1，並保留最強的 4 根骨頭。
- **權重模式（P）**（工具列、編輯選單，和其他模式一起記住）：
  - **檢視**：只顯示選取的骨頭所在的蒙皮。每個三角形以它頂點對該骨頭的平均權重填色，從藍（0）經青、綠、黃到紅（1）。
    游標最近的頂點會列出它的骨頭與權重；還沒有手繪時會加上「（自動權重）」。
  - **繪製**：拖曳增加，Ctrl+拖曳減少，Shift+拖曳平滑。沿途每移動筆刷的五分之一塗抹一次，整筆是一個復原步驟。
    第一筆從自動權重開始，並把全部權重寫進節點（`.anim` 的 `skin.weights`，遊戲本來就會讀取）。
  - **選取骨頭**：點選骨頭的關節即選取那根骨頭；線段不會選取，所以可以跨過它們繪製。節點面板也能選取骨頭。
  - **筆刷**：大小以螢幕像素計（工具列的「筆刷」，或 `[` 與 `]`），強度 0.01 – 1，兩者都會記住。圓圈表示大小，
    點線內圈表示強度；增加為橘色、減少為藍色、平滑為灰色。
- **編輯 > 重設為自動權重**：捨棄選取的蒙皮 sprite（或選取的骨頭所在的每個蒙皮）手繪的權重（一個復原步驟）。
- 在任何影格都能繪製：筆刷碰到的是頂點目前畫出的位置。權重屬於網格：重新切割後頂點數不同的網格（新的網格格數、
  改形的 sprite）會回到自動權重。

| 測試 | 檢查內容 |
|---|---|
| `anim.weights` | `tools/anim/tests/anim_weights_test.cpp`：衰減；增加、減少（有無接手骨頭）與平滑照預期改變比例；每個頂點總和為 1；最多 4 根骨頭，保留剛畫的那根 |
| `anim.editor_selftest`（權重部分） | 對 `anim_skin.anim`：P 切換模式，點選 chest 的關節即選取它；在 hip 旁畫一筆使那裡 chest 的比例增加、寫入 golem 的權重、總和維持 1 且是一個復原步驟（`skin_weights.png`）；Ctrl+畫一筆使它減少；重設為自動權重會捨棄它們 |

**已知限制**

- 編輯器只在遊戲算圖器檢視中把蒙皮節點的 sprite 畫成彎曲的。它的選取外框與 QPainter 備援（沒有 GPU 時）顯示未彎曲的
  靜止四邊形；顯示蒙皮的線與頂點在兩者中都是彎曲的。
- 手繪的權重只在網格頂點數不變時保留；重新切割後會回到自動權重（請重新繪製，或先重設為自動權重）。筆刷無法只畫被另一個
  蒙皮 sprite 蓋住的那個：它會畫筆刷下該骨頭所在的每個蒙皮。
- 網格的頂點還不能手動拖曳；網格格數只能讓它變粗或變細。形狀改變請用晶格。
- 晶格在節點自己的座標中顯示與拖曳。蒙皮節點的骨頭在那裡不會套用，所以請在骨頭靜止時拖曳晶格。
- 旋轉的圖集區域沒有網格。TOMS 的圖集目前不旋轉。
- 美術風格的網格檔由 `tools/refresh_meshes.py` 產生。沒有網格檔的風格（加入後還沒執行過這個腳本）使用原始美術的
  `game.mesh.json`：它的 UV 限制在 sprite 的區域內，所以不會取樣到相鄰的 sprite，但它沿用原始的輪廓，而不是該風格
  自己的輪廓。

## 8. 擁有者的決定（2026-10-21）

1. **晶格大小：** 每個節點可調，預設 4 × 4。
2. **網格儲存：** 像 glTF，儲存時選擇純文字或二進位緩衝。讀取兩種都接受。
3. **Spine 匯入器：** 暫時略過。有用的匯入器需要先有蒙皮與變形（Spine 角色大多是帶變形關鍵影格的加權網格），
   在那之上還要再花好幾天；只有骨頭的匯入器約一天，但能匯入的東西很少。第 4 步之後再評估。
4. **順序：** 先做蒙皮，再做晶格。原本的動畫編輯器必須照常運作：它的檢查與自我測試通過，沒有蒙皮的片段畫法與
   存檔都和以前完全一樣（版本 2）。
