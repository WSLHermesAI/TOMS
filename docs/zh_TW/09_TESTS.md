# 09 — 測試（CTest）

> 英文原文：[09_TESTS.md](../09_TESTS.md)

一個指令就能執行所有檢查：遊戲邏輯、畫面和 web 版。

```bat
tools\build.cmd windows-release          :: build first: the tests are programs in the build folder
tools\test.cmd                           :: everything, windows-release (about 45 seconds)
tools\test.cmd windows-debug -L unit     :: only the unit tests of another preset (under a second)
```

（先建置：測試是建置資料夾中的程式。`tools\test.cmd` 執行全部，約 45 秒；`-L unit` 只跑單元測試，不到一秒。）

Visual Studio 在 **測試 → 測試總管** 中列出同樣的測試（來自 CMake preset）：點一下就能執行或除錯其中一個。
底層是 `ctest`，CMake 的測試執行器（Visual Studio 有附；`tools\test.cmd` 會找到它），所以
在 `PATH` 上有它的命令提示字元中，`ctest --preset windows-release` 也可以用。

**所有單元測試都是同一個程式** `bin\tests\toms_tests.exe`，所以 Visual Studio 的目標
清單中它們只佔一項，而不是三十項。每個測試檔保留自己的 `main()`；建置時
把它改名為 `toms_test_<name>`，由一個產生出來的驅動程式執行命令列上指名的那一個。
CTest 仍然逐一執行並回報每個測試。請在 `assets\` 中執行：

```
toms_tests save_test      one test (the names are the file names: save_test, particle_fx_test, ...)
toms_tests --list         every name
toms_tests --all          all of them in one go
```

（`toms_tests save_test` 執行一個測試，名稱就是檔名；`--list` 列出所有名稱；`--all` 一次全部執行。）

**用遊戲渲染器的編輯器**（不在 CTest 中：它們會開視窗）。`atlas_editor`、`anim_editor`
和 `particle_editor` 都有 `--selftest-gpu <file> <outdir>`。它會檢查 viewport 是否跑在
toms_game 的渲染器上，在那裡繪製和編輯，並存下 bgfx 截圖以供比對。離螢幕的
`--selftest` 檢查 QPainter 的後備路徑。

要在 Visual Studio 中除錯某一個，在目標清單中選 **「toms_tests (one unit test)」**，並修改
`launch.vs.json` 中它的 `args`。因為每個測試都連結進同一個程式，測試自己定義的型別或
輔助函式要放在匿名命名空間中。曾經有兩個測試各自有不同的
`struct MockContext`，連結在一起時其中一個默默取代了另一個。

## 執行哪些測試

| 標籤 | 數量 | 檢查什麼 | 需要 |
|---|---|---|---|
| `unit` | 35 | 遊戲自己放在 `src/core` 程式碼旁邊的 `*_test.cpp` 程式（存檔、標題流程、條件、戰鬥條、鏡頭、裝備、技能、鍛造、據點、結局、輪迴、樓層、佔地、任務、節點動畫、粒子效果、glTF 模型、…），包括對發行的 `assets/data` 的檢查 | 不需要 |
| `atlas` | 3 | `atlas.core`：atlas 工具的打包器、子 sprite、每種匯出格式讀回、舊的 `.pi` 檔逐像素重建；`atlas_sprites_test`：程式碼、資料和 UI 用到的每個 sprite 名稱都在遊戲的 atlas 中；`atlas.assets_up_to_date`：`assets/media/atlas/game.atlasproj` 只靠它打包好的 atlas 就能開啟，且其他檔案都是最新的（[14](14_ATLAS_TOOL.md)）；`atlas.fx_up_to_date`：粒子 sprite 的 `fx.atlasproj` 同上（[17](17_PARTICLES.md)） | 不需要（三個也都在 `unit` 中；`atlas_sprites_test` 只在那裡） |
| `particle` | 1 | `particle.check_recipes`：對 `docs/examples/fx_recipes.particle` 執行 `particle_editor --headless check`（能解析、每個 sprite 都在它的 atlas 中、曲線 / 池 / 爆發都合理；[17](17_PARTICLES.md)） | Qt 編輯器建置（也在 `unit` 中） |
| `anim` | 2 | `anim.check_preview` / `anim.check_recipes`：對 `tests/smoke/anim_preview.anim` 和範例 `docs/examples/anim_recipes.anim` 執行 `anim_editor --headless check`（能解析、每個 sprite 都在遊戲 atlas 中、沒有重疊的 key；[15](15_ANIMATION.md)、[16](16_ANIMATION_RECIPES.md)） | Qt 編輯器建置（也在 `unit` 中） |
| `smoke` | 15 | 真正的 `toms_game` 播放一段腳本化的場景；它的截圖必須和 `tests/golden/` 中的參考圖相符（包括 `smoke.anim` 和 `smoke.fx`：地圖上的 `.anim` 片段和粒子效果；`smoke.fx_cpu` / `fx_compute`：同樣的東西走 sprite 批次的其他路徑，`smoke.fx_gpu`：所有發射器都在 GPU 上模擬——全部對同一張圖比對） | GPU；每個測試開一個視窗約 2 秒 |
| `smoke` + `gltf` | 17 | `gltf_viewer` 在固定時間繪製 `tests/gltf/` 中的測試模型；截圖必須和 `tests/golden/gltf_*.png` 相符：蒙皮（線性、cubic spline）、morph target（線性、step）、`EXT_mesh_gpu_instancing`、PBR 材質、法線檢視、陰影（平行光、點光、聚光、全部）；蒙皮、PBR 和陰影也在 Vulkan 和 OpenGL 上測（[18](18_GLTF.md)）。`ctest -L gltf` 只執行這些 | GPU；每個約 1 秒 |
| `web` | 5 | 在無頭 Chrome 中執行 `tools/web_smoke_test.mjs`：標題 → 新遊戲 → 存檔 → 重新載入 → 存檔還在，沒有頁面錯誤。對單執行緒版、多執行緒版（用隔離標頭提供），以及打包好的頁面兩次：在沒有標頭的伺服器上（它必須透過 service worker 變成多執行緒）和加上 `?nothreads`（[10](10_THREADS.md)），再在**真正的 GPU** 上跑一次（`web.page_gpu`，`--gpu`：Direct3D 11 上的 ANGLE，對 shader 比其他測試用的 SwiftShader 軟體 GPU 更嚴格）。任何編譯或連結失敗的 WebGL shader 都會被記錄，並讓測試失敗 | web 版 / 套件（`build_web.bat`）、Node 22+、Chrome 或 Edge；缺少的話會**略過**，而不是失敗 |

Smoke 場景有標題、地圖（HUD + 方向盤）、遊戲內選單、物品欄、對話、戰鬥（攻擊一次之後）、商店，以及地圖上的節點動畫（`--anim`，[15](15_ANIMATION.md)）。
它們在 Direct3D 11 上執行，地圖場景也在 Direct3D 12、Vulkan 和 OpenGL 上執行。場景清單在
`tests/CMakeLists.txt`，每個測試一行。

## 截圖測試如何運作

- `toms_game --fixed-dt=16 --frames=N --keys=… --clicks=… --screenshot=…`：每個畫面剛好前進 16 ms，
  真正的滑鼠/鍵盤輸入會被忽略，所以同一個場景每次執行都是相同的像素。
- 每個測試在 `Build\<preset>\test-run\<name>\` 底下的全新資料夾中執行，所以先前的存檔不會改變
  標題畫面。
- `image_diff`（`tests/tools/image_diff.cpp`）計算每個顏色通道差異超過一個小量的像素數。
  上限：Direct3D 11 上是畫面的 0.1%，實際上完全相同；其他後端是 0.5%
  （OpenGL 在邊緣約有 0.02% 不同）。改變一條 HP 條是畫面的 0.3%，會失敗。
- 在 HiDPI 顯示器上視窗比較大；截圖會先縮放到參考圖的大小再比對。

**Smoke 測試失敗時**，測試輸出會指出它 `test-run` 資料夾中的兩個檔案：截圖和
`<name>.diff.png`（不同的像素在變暗的參考圖上標成紅色）。如果改變是錯誤，就修正它。如果是
刻意的（新的配置、新的顏色），就更新參考圖，並在提交前**看過它們**：

```bat
set TOMS_UPDATE_GOLDEN=1
tools\test.cmd windows-release -L smoke
set TOMS_UPDATE_GOLDEN=
```

只有以參考圖命名的測試會寫入它（`smoke.stage` 寫 `stage.png`；`smoke.stage_vulkan`
會和它比對，但永遠不會取代它）。

## 新增測試

- **單元測試：** 撰寫 `src/core/…/xxx_test.cpp`，其中 `main()` 成功時回傳 0，並把它的路徑加進
  `tests/CMakeLists.txt` 的 `TOMS_UNIT_TESTS`。它連結核心函式庫，並在 `assets/` 中執行，所以以
  `data/…` 讀取資料。
- **畫面：** 加一行 `toms_smoke_test(name reference backend limit scene…)`，用
  `TOMS_UPDATE_GOLDEN=1` 執行一次，檢查新的 `tests/golden/<name>.png`，然後提交它。

Shipping preset 不建置任何測試（`TOMS_BUILD_TESTS=OFF`）。Web 和 Android 版也從不建置。
