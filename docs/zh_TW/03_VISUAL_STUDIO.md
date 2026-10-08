# 03 — 在 Visual Studio 中建置、執行和除錯

> 英文原文：[03_VISUAL_STUDIO.md](../03_VISUAL_STUDIO.md)

TOMS 是 CMake 專案。Visual Studio 用 **開啟資料夾** 就能直接打開它；不需要產生或提交
`.sln`。

## 1. 開啟專案

1. 先執行一次 `tools\check_env.cmd`（見 [02](02_INSTALL_WINDOWS.md)）。
2. Visual Studio → **檔案 → 開啟 → 資料夾…** → 選 **`TOMS`** 資料夾（repository 根目錄，
   也就是有 `CMakeLists.txt` 和 `CMakePresets.json` 的那個）。
3. Visual Studio 會讀取 `CMakePresets.json` 並開始 configure。請看 **檢視 → 輸出 →
   CMake**。
   - **第一次 configure 會下載 bgfx、SDL3、Dear ImGui、glm、FreeType 和 RmlUi**（要
     幾分鐘；第一次建置時會編譯它們）。之後的 configure 很快。原始碼只
     下載一次到 `Build\_deps-src`，所有 preset 共用；每個 preset 只在 `Build\<preset>\_deps`
     保留自己編譯好的副本。
   - 如果缺少前置需求，會跳出對話框說明缺什麼、去哪裡取得
     （[02](02_INSTALL_WINDOWS.md)）。CMake 輸出中也有同樣的文字。

## 2. 選擇組態

工具列的組態下拉選單列出各個 preset：

| Preset | 建置 | 用途 |
|---|---|---|
| **Windows x64 Debug（遊戲 + 編輯器）** | `toms_game`、`toms_editor` | 逐步執行程式碼 |
| **Windows x64 Release（遊戲 + 編輯器）** | 同上，最佳化並含除錯資訊 | 一般的試玩 |
| **Windows x64 Shipping（只有遊戲，不含 Qt）** | 只有 `toms_game`；不搜尋 Qt | 要發行的版本；沒有 Qt 的機器也能用 |
| CI（無對話框） | 和 Release 一樣，但永遠不跳對話框 | 建置伺服器 |

建置輸出放在 `Build\<preset>\bin\`。

## 3. 執行和除錯（F5）

在 **選取啟動項目** 下拉選單（綠色 ▶ 旁邊）中選擇啟動項目：

| 啟動項目 | 執行什麼 |
|---|---|
| **toms_game (auto renderer)** | 遊戲；bgfx 選最好的後端（Windows 上是 Direct3D 11/12） |
| **toms_game (Direct3D 11)** | 強制 `--renderer=d3d11` |
| **toms_game (Vulkan)** | 強制 `--renderer=vulkan` |
| **toms_editor (Qt + bgfx)** | 編輯器；它的 *Play (bgfx)* 分頁執行同一份遊戲程式碼 |
| **atlas_editor (game atlas)** | 開啟 `assets\media\atlas\game.atlasproj` 的 sprite atlas 編輯器（[14](14_ATLAS_TOOL.md)） |
| **atlaspack (rebuild game atlas)** | `atlaspack build assets\media\atlas\game.atlasproj --all-variants`，沒有視窗 |

這些來自 `launch.vs.json`。如果下拉選單只顯示 `toms_game.exe` / `toms_editor.exe`，
也可以用（使用預設參數）。

舊遊戲程式碼（`src\core\game\...`）中的中斷點在**兩個**執行檔中都會命中，因為兩者
都連結同一個 `toms_core` 函式庫。

### 遊戲操作

方向鍵/WASD 移動 · 點擊/輕觸地圖格就會走過去（怪物、門或物品：走到旁邊再踏進去）· Enter/Space 互動/攻擊 · F 防禦 · G 大招 · H 主動技 · I 物品欄 ·
B 商店 · Tab 關卡選擇 · F1 除錯覆蓋層 · F2 樣式測試 · Esc 選單/返回。
UI（[08](08_RMLUI.md)）：F5 重新載入 `assets\media\ui` · F8 RmlUi 除錯器。

### 編輯器

- **Play (bgfx)** 分頁：點一下畫面讓它取得鍵盤，然後像在遊戲中一樣玩。
- **Stages** 面板：雙擊一個關卡，把它載入正在執行的遊戲。
- **Session** 面板：渲染器名稱、fps、四邊形/draw call 數、*重新開始遊戲*、除錯覆蓋層
  和 bgfx 統計的開關。
- **Stage editor** 分頁：現有的 Qt 關卡編輯器（`src\editor\stage`），原封不動嵌入。

## 4. 命令列選項（偵錯 → *偵錯和啟動設定*，或終端機）

| 選項 | 意義 |
|---|---|
| `--renderer=auto\|d3d11\|d3d12\|vulkan\|opengl` | bgfx 後端（編輯器：環境變數 `TOMS_RENDERER`） |
| `--assets=<dir>` | media 資料夾，旁邊要有 `assets\data`（預設：環境變數 `ASSET_DIR`，否則用 exe 旁邊的 `assets\media`，否則用 checkout 中的 `assets\media`） |
| `--stage=<id>` | 第一個關卡，例如 `stage03` |
| `--no-vsync` | 不限制畫面更新率 |
| `--stats` | bgfx 畫面統計 |
| `--fps` | 一開始就顯示效能列（F3 切換）：FPS、畫面時間、後端、sprite 路徑、compute 支援、四邊形、draw call |
| `--fx-gpu-threshold=N` | 粒子數超過 N 的發射器在 GPU 上模擬（0 = 永不）；只限這次執行。存下來的設定是 `particleGpuThreshold`（桌機 5000，手機 3000） |
| `--sprite-path=auto\|instancing\|compute\|cpu` | sprite 批次如何產生頂點；auto = instancing（實測最快），否則 compute，否則 CPU。`--no-instancing` = `cpu` |
| `--frames=<n> --screenshot=<file.png>` | 執行 n 個畫面，存一張 PNG，然後結束（smoke 測試） |
| `--keys=enter@30,enter@60` | 在指定畫面按鍵（smoke 測試） |
| `--clicks=222:140@130` | 在某個畫面於設計空間（1024×768）的某一點按左鍵（smoke 測試） |

環境變數（除錯用，來自舊版）：`TOMS_HIDE`、`TOMS_SPLIT_NODE`、`TOMS_RENDER_DEBUG`。

啟動項目在 `Build\<preset>\bin` 中執行，所以 `toms.log` 和 `save\` 資料夾會
寫在那裡。

## 5. 不開 Visual Studio 建置

```bat
tools\build.cmd                 :: windows-release
tools\build.cmd windows-debug
tools\build.cmd windows-shipping
```

腳本會用 `vswhere` 找到 Visual Studio，載入 MSVC x64 環境，並使用 Visual
Studio 附帶的 CMake 和 Ninja。

測試：`tools\test.cmd` 會執行全部（單元測試、截圖 smoke 測試、web），Visual Studio 的
**測試 → 測試總管** 也列出同樣的測試。單元測試是同一個程式 `toms_tests`
（用 **「toms_tests (one unit test)」** 目標除錯其中一個）。見 [09_TESTS.md](09_TESTS.md)。

**粒子效果：** **「particle_editor (recipes)」** 會用 `docs/examples/fx_recipes.particle` 開啟粒子編輯器
（`bin\particle_editor.exe`）。**「toms_game (particle
preview: torch_fire)」** 在遊戲上播放一個效果；把它 `args` 中的 `#torch_fire` 換掉就能看
別的，例如 `#heal` 或 `#hit_sparks`（[17](17_PARTICLES.md)）。

快速手動截圖（繪製標題畫面，再繪製第 1 關，並寫出 PNG；編輯器另外會寫出
包含整個視窗的 `<name>_window.png`）：

```bat
cd Build\windows-release\bin
toms_game.exe --frames=60 --screenshot=title.png
toms_game.exe --frames=120 --keys=enter@30 --screenshot=stage.png
toms_editor.exe --frames=200 --screenshot=editor.png
```

## 6. 除錯技巧

- **GPU 擷取：** 啟動 RenderDoc，*Launch Application* → `toms_game.exe`。用 `--renderer=d3d11`
  擷取最可靠。
- **正在用哪個後端？** 主控台會印出 `bgfx ... ready: renderer=Direct3D 11`；
  編輯器在 *Session* 面板中顯示。
- **Shader：** 編輯 `src\engine\shaders\*.sc` 後建置。shaderc 會把它們重新編譯成嵌入
  exe 的標頭，所以沒有 shader 檔需要複製。
- **UI 畫面（RmlUi）：** 在遊戲執行時編輯 `assets\media\ui\*.rml` / `.rcss`，再按 F5；
  F8 顯示元素樹和計算後的樣式。不需要重新建置。
- **編輯器中的快速鍵沒反應？** 先點一下遊戲畫面；它需要鍵盤焦點。
- 問題排除：[05_TROUBLESHOOTING.md](05_TROUBLESHOOTING.md)。
