# TOMS 編輯器

> 英文原文：[README.md](../README.md) · 給 AI 製作素材用：[AI_GUIDE.md](../AI_GUIDE.md)

可直接執行的 TOMS 編輯器。不需要建置或安裝：在 `Editor` 資料夾中按兩下啟動檔即可。

| 啟動檔 | 程式 | 用途 |
|---|---|---|
| `AtlasEditor.cmd` | `bin\atlas_editor.exe` | 圖集：把多張 PNG 打包成一張貼圖（`.atlasproj` → `.atlas` + `.png` + `.plist`） |
| `AnimEditor.cmd` | `bin\anim_editor.exe` | 節點動畫（`.anim`）：在時間軸上移動、旋轉、縮放精靈圖並改變顏色 |
| `ParticleEditor.cmd` | `bin\particle_editor.exe` | 粒子特效（`.particle`）：火焰、火花、煙、魔法 |
| `FontEditor.cmd` | `bin\font_editor.exe` | 圖片字型（`.fnt` + `.png`），每個字元一張圖片 |
| `GltfViewer.cmd` | `bin\gltf_viewer.exe` | 檢視 3D 模型（`.gltf` / `.glb`）：動畫、燈光、鏡頭、物理 |
| `TomsEditor.cmd` | `bin\toms_editor.exe` | 在編輯器視窗中執行遊戲，使用本 repo 的 `assets` |
| `WebEditors.cmd` | `..\tools\web_editors\serve.cmd` | 關卡（樓層地圖）與事件編輯器，在瀏覽器中使用 |
| `Tools.cmd` | | 開啟命令提示字元，`atlaspack`、`fontpack` 與各編輯器都在 PATH 上 |

**語言：** 每個 Qt 編輯器的選單列右側都有 **中文 / English** 按鈕。選擇會套用到所有編輯器。

## 1. 開始之前

- **Windows 10 或 11，64 位元。** `bin` 已附上 Microsoft C++ 執行階段，不需另外安裝。
- **repo 必須用 Git LFS 取得。** `bin` 中的程式存放在 Git LFS。請在這台電腦上 clone **之前**先執行一次
  `git lfs install`（或在 clone 之後執行 `git lfs pull`）。沒有這一步，`.exe` 與 `.dll` 只會是很小的文字檔，
  Windows 會顯示「這個應用程式無法在您的電腦上執行」。
- **請讓這個資料夾留在 repo 內。** `TomsEditor`、`WebEditors` 與粒子編輯器預設的 fx 圖集會用到 `..\assets` 與
  `..\tools`。其他編輯器只需要 `bin`，這個資料夾可以單獨複製到任何地方使用。
- **WebEditors 需要 Python 3**（在 PATH 上；它會啟動一個本機網頁伺服器）以及 Chrome 或 Edge。
- **GltfViewer 與 TomsEditor 需要支援 Direct3D 11 的顯示卡**（近十年的電腦都可以）。

## 2. 各編輯器

每一節是簡短版本；完整細節請看連結的文件。

### 圖集編輯器：把精靈圖合成一張貼圖（[docs/14](../../docs/zh_TW/14_ATLAS_TOOL.md)）

1. **檔案 → 新增**，然後 **加入圖片…**（PNG 或整個資料夾，也可以直接拖到視窗上）。
2. 在右側設定打包方式：最大頁面大小、**padding**（精靈圖之間的空白像素）、**extrude**（在每張精靈圖四周重複邊緣像素，
   縮放時才不會取到鄰近圖片的顏色；除非精靈圖會縮得很小，否則 1 就夠了）、**trim**（裁掉透明邊緣；繪製時仍是原本大小）、
   2 的次方。
3. 每張精靈圖可選擇設定：中心點（pivot）、九宮格切割、標籤、**子精靈圖**（另一張精靈圖中的一塊矩形，不複製像素）。
4. **儲存** 會寫出 `NAME.atlasproj` + `NAME.atlas` + `NAME.png` + `NAME.plist`。加入的 PNG 存在打包後的貼圖中，
   之後不再需要原始檔案。

- **更換圖片：** 在專案的參考資料夾中修改 PNG，然後執行 **從參考資料夾匯入**（該 PNG 會列為「已變更」），再儲存。
- **匯入舊圖集：** **匯入圖集** 可讀取 `.pi`、`.atlas`、TexturePacker `.json` 與 Cocos `.plist`。
- 遊戲自己的圖集是 `assets\media\atlas\game.atlasproj`。

### 動畫編輯器：節點動畫（[docs/15](../../docs/zh_TW/15_ANIMATION.md)，範例：[docs/16](../../docs/zh_TW/16_ANIMATION_RECIPES.md)）

1. **檔案 → 新增**，然後對每個精靈圖來源圖集按 **加入圖集…**。
2. 按 **加入精靈圖節點**（或從精靈圖面板把精靈圖拖進畫面）建立節點。節點組成樹狀結構；子節點會跟著父節點移動。
3. 在時間軸上選一個時間，修改位置、旋轉、縮放、顏色或精靈圖，然後 **在播放頭插入關鍵影格**。每個關鍵影格都可以設定
   **ease**（移動如何加速與減速）。
4. 按播放，或按兩下片段播放它。**儲存** 會寫出 `.anim`。

- 每個節點可設定：**loop**（持續播放，片段結束後也一樣）、關閉 **停在最後一格**（節點在最後一個關鍵影格後消失）、
  **blend add**（發光效果）。
- **事件關鍵影格**（例如 `hit`）告訴遊戲何時造成傷害或播放音效。
- **轉換 → MPDI 資料夾轉為 .anim…** 可匯入舊引擎的 `.mpdi` 動畫。
- **問題 / 記錄** 面板會列出缺少的精靈圖，以及超過片段長度的關鍵影格。

### 粒子編輯器：特效（[docs/17](../../docs/zh_TW/17_PARTICLES.md)）

1. **檔案 → 新增**，或開啟 `docs\examples\fx_recipes.particle`（11 個現成特效：火把、命中火花、治療、下雪…）再修改。
2. **加入效果**，然後 **加入發射器**（或 **加入預設效果**）。一個特效包含多個發射器。每個發射器可設定：發射（每秒數量與爆發）、
   形狀（點、線、矩形、圓、環）、壽命、方向與擴散角、速度、大小、顏色、精靈圖、隨壽命變化的曲線（顏色、大小、速度、旋轉）、
   外力（重力、阻力、放射、環繞）、序列圖，以及混合模式（火焰與光用 `add`，煙與灰塵用 `normal`）。
3. 預覽會一直播放；在畫面中拖曳特效可看它移動時的樣子。
4. **儲存** 會寫出 `.particle`。**問題 / 記錄** 面板會列出遊戲會拒絕的內容。

預設的精靈圖是 fx 圖集（`assets\media\atlas\fx.atlas`：dot、glow、spark、star、ring、smoke、flame）。

### 字型編輯器：圖片字型（[docs/23](../../docs/zh_TW/23_FONT_TOOL_GUIDE.md)）

1. **加入圖片...**（每個字元一張 PNG，也可以拖到視窗上）。檔名為 `0.png`、`K.png`、`dot.png`、`comma.png`、
   `space.png` 的圖片會自動得到對應的字元。
2. 在 **字元** 欄輸入每張圖片的字元。紅色的格子（沒有字元，或字元重複）必須修正後才能儲存。
3. 輸入 **測試文字**：預覽會以遊戲相同的方式繪製。在 **合併圖片** 中點一個字元，可找到它所在的列。
4. 設定 **頁面版面**（一列、緊密、緊密 2 的次方）、**空白寬度** 與 **行高**（兩者預設為 *自動*）。
5. **字型另存新檔...** 會寫出 `NAME.fnt` + `NAME.png`。**開啟字型** 可載入現有字型來修改。

### glTF 檢視器：3D 模型（[docs/18](../../docs/zh_TW/18_GLTF.md)）

把 `.gltf` / `.glb` 拖到視窗上（或拖到 `GltfViewer.cmd` 上）。

| 操作 | 作用 |
|---|---|
| 左鍵拖曳 / 右鍵或中鍵拖曳 / 滾輪 | 環繞 / 平移 / 縮放 |
| `Space` `F` `G` `K` `B` `W` `R` `H` | 播放暫停、置中、格線、骨架、外框、線框、自動旋轉、面板 |
| `1`..`9` | 只播放該動畫 |
| `C` 或鏡頭按鈕 | 切換檔案中的鏡頭（最後回到自由鏡頭） |
| 點選網格，再按 `T` / `E` / `S` | 選取其節點，然後移動 / 旋轉 / 縮放（不會存回檔案） |

面板會顯示模型用到的功能（綠色：有繪製，橘色：忽略）、動畫、變形目標、檔案的燈光或燈光組、陰影與物理。

### Toms 編輯器：在視窗中執行遊戲

在編輯器視窗中執行遊戲，旁邊有事件流程與舊的關卡清單。它讀取本 repo 的 `assets`（啟動檔會設定好路徑）。

### 網頁編輯器：樓層與事件（[docs/19](../../docs/zh_TW/19_STAGE_EDITOR.md)、[事件編輯器](../../docs/zh_TW/event_editor/README.md)）

`WebEditors.cmd` 會從 `assets\data` 更新編輯器的資料，以 `http://localhost:8000` 開啟瀏覽器，並在它的視窗中持續執行伺服器
（關閉視窗或按 Ctrl+C 即停止）。

- 按 **Open project folder…** 並選擇 TOMS 資料夾：之後 **Save** 會直接寫入 `assets\data`（每個檔案會先備份到
  `Build\editor_backups\`）。
- **關卡編輯器：** 在每層樓繪製牆、門、鑰匙、怪物、道具、事件與專屬鑰匙門。下方的檢查會找出無法到達的物件、不足的鑰匙與
  有問題的樓梯。
- **事件編輯器：** 事件、各語言的文字，以及事件之間的連結（*Active only when*、*Then fire*、*Then do → give*）。

## 3. 發生問題時

| 問題 | 解決方式 |
|---|---|
| 「這個應用程式無法在您的電腦上執行」，或 `.exe` 很小 | LFS 檔案沒有下載：執行 `git lfs install`，然後在 repo 中執行 `git lfs pull` |
| 「找不到 VCRUNTIME140.dll」 | `bin` 中缺少檔案：執行 `git lfs pull`，或 `git checkout -- Editor/bin` |
| 「no Qt platform plugin could be initialized」 | 缺少 `bin\platforms\qwindows.dll`：同上 |
| TomsEditor 的地圖是空的或全黑 | 直接從 `bin` 啟動了；請用 `TomsEditor.cmd`，它會設定 `ASSET_DIR` |
| WebEditors 一開就關閉 | PATH 上沒有 Python 3；請從 python.org 安裝，並勾選 *Add to PATH* |
| 粒子編輯器沒有精靈圖 | 在精靈圖面板開啟一個圖集，或讓 `Editor` 留在 repo 內，它才找得到 `assets\media\atlas\fx.atlas` |

## 4. 更新這個資料夾（給建置編輯器的人）

```bat
tools\package_editors.cmd             :: 建置 windows-release，然後更新 Editor\bin 與 Editor\BUILD_INFO.md
tools\package_editors.cmd --no-build  :: 使用上次的建置結果更新
tools\package_editors.cmd --check     :: 只列出缺少、多餘或過期的檔案（有的話 exit 1）
```

- 它會複製 8 個程式、它們載入的 Qt 6 DLL 與外掛、著色器編譯器 DLL（`dxcompiler`、`dxil`），以及已安裝 Visual Studio 的
  Microsoft C++ 執行階段。除錯檔與測試程式不會複製。
- [BUILD_INFO.md](../BUILD_INFO.md) 記錄 commit、日期，以及每個檔案和它的檢查碼。
- `Editor/bin/**` 存放在 Git LFS（`.gitattributes`）。提交新版本前請執行 `tools\test.cmd all`。
- 啟動檔與這些文件是手寫的；腳本不會修改它們。
