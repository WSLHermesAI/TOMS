# 08 — 測試（CTest）

> 英文原文：[08_TESTS.md](../08_TESTS.md)

`tools\test.cmd` 預設只執行目前的變更可能影響到的測試；加上 `all` 才執行所有檢查：遊戲邏輯、畫面和 web 版。

```bat
tools\build.cmd windows-release          :: build first: the tests are programs in the build folder
tools\test.cmd                           :: only the tests for the uncommitted changes (the project rule, CLAUDE.md)
tools\test.cmd all                       :: everything, windows-release, web included (about 45 seconds)
tools\test.cmd --since main              :: the tests for what changed since a commit or branch
tools\test.cmd --list                    :: print which tests would run, and why; run nothing
tools\test.cmd windows-debug -L unit     :: a ctest filter runs as given (only the unit tests of another preset)
```

（先建置：測試是建置資料夾中的程式。`tools\test.cmd` 只跑變更相關的測試；`all` 執行全部，約 45 秒；
`--since <commit>` 以某個 commit 或分支為比較基準；`--list` 只列出會跑哪些測試與原因；`-L unit` 等 ctest 篩選照原樣執行。）

**選擇方式**（`tools/test_select.py`）：讀取變更的檔案（`git diff HEAD` 加上未追蹤的檔案）。**每個區域有自己的測試，
沒有變更的區域就不測。** 改 glTF 不會跑任何遊戲測試，改遊戲也不會跑 glTF 測試：

| 區域 | 它的檔案 | 它的測試 |
|---|---|---|
| 遊戲 | `src/core/game`、`src/game`、`assets/`（資料、UI、字型、sprite） | 遊戲的單元測試（`src/core/game/**_test`），以及 D3D11 上的遊戲截圖，依檔名縮小範圍：`player` / `saveload` → 選單場景、`battle` / `combat` → 戰鬥、`store`、`dialog`、`title`、`stair` / `floor` / `stage` → 地圖場景；其他檔名 → 全部遊戲場景 |
| glTF | glTF 程式、`gltf_viewer`、mesh shader、`tests/gltf/` | `unit.gltf_*` 與 glTF 截圖 |
| 粒子 | `particle_fx`、粒子編輯器、fx 檔與 shader | 粒子檢查、它的單元測試、`smoke.fx*` |
| 節點動畫 | `anim_clip`、動畫編輯器、`.anim` 檔 | 動畫檢查、它的單元測試、`smoke.anim`（`.anim` 檔另加遊戲場景） |
| Atlas | atlas 工具、atlas 檔 | atlas 檢查（atlas 檔另加遊戲場景） |
| 圖片字型 | `tools/font`、`tests/font`、`src/core/engine/bitmap_font*` | `font.tool`、`font.editor_selftest` |
| 關卡／事件編輯器程式 | `tools/stage_event_editor`（以及任何 `assets/data` 檔案：`unit.stage_core_test`） | `unit.stage_core_test`、`stage_editor.app_selftest` |
| Qt 編輯器共用程式 | `tools/studio_common`（含 `i18n/` 中文字典） | atlas / anim / particle / font 編輯器的檢查與 `studio.*` |
| 遊戲旁的 Qt 編輯器（ui_editor、battle_editor） | `src/editor` | `ui_editor.selftest`、`smoke.ui_editor`、`battle_editor.selftest`、`smoke.battle_editor` |
| 資源 | `src/core/engine/resource*`、`render_object*`（docs/engine/12） | 引擎的單元測試、動畫與粒子檢查，再加上遊戲區域 |
| 引擎核心 | `src/core/engine` 的其他部分（條件、節點、事件 …） | 引擎的單元測試，再加上遊戲區域 |
| 繪圖器 | `src/engine` 的共用檔（`bgfx_*`、內嵌 shader） | 所有截圖，所有後端 |
| | `src/engine` 的 sprite、UI、ImGui 檔案與 shader | 遊戲截圖，所有後端 |
| Web | `web/`、web 測試 | web 測試 |
| 測試 | 某個 `*_test.cpp`；某張 `tests/golden/x.png`；`tests/CMakeLists.txt` 中改動的那幾行 | 正好就是那些測試（改到 `_pm_give` 這類變數時，是用到它的測試） |
| 全域 | 根目錄的 `CMakeLists.txt`、preset、`cmake/`、`external/` | 所有桌面測試 |
| 不測 | 文件、`*.md`、HTML 編輯器、`android/`、測試執行器本身 | 不執行 |

**`tests/` 底下的資料夾**由 [tests/test_folders.json](../../../tests/test_folders.json) 決定：它列出哪些資料夾是測試資料
（`golden`、`smoke`、`tools`、`gltf`、`mpdi`、`stage`、`font`），以及各自要執行的測試（ctest 名稱、`label:<標籤>` 或
`@golden`）。`tests/` 底下其他資料夾不是測試資料，不執行測試（其他專案的檔案、工具輸出）；執行時只會列出一次「not test
data」。新測試要讀某個資料夾時，再把它加進去。

**關閉測試：**`tools/test_switches.cmd` 開啟一個視窗，以樹狀（群組 > 區域 > 測試）列出每個測試，各有一個勾選框；按 Save
把沒勾的寫進 [tests/test_switches.json](../../../tests/test_switches.json)，`test.cmd` 就不會執行它們（只測變更或 `all`
都一樣；一個群組（`-L scenario`、`-E ...`）與沒有 Python 時也一樣）。只有以名字指定的測試（`-R`）照指定執行，不論是否
關閉。新的測試在取消勾選之前都是開啟的。`test_switches.cmd --list` 以文字印出同樣的樹。執行時會列出被略過的已選測試。按兩下開啟時會立刻顯示「Loading the test list」
小視窗，無法開啟清單時（還沒建置、找不到 Visual Studio）會跳出訊息框。若按兩下 `.cmd` 完全沒有反應，表示主控台根本沒有開啟：
見 [04](04_TROUBLESHOOTING.md)〈按兩下 .cmd 沒有反應〉。

沒有任何規則認得的檔案不會執行測試，並列為 **NOT MAPPED**：請在 `RULES` 中為它補一條規則。各後端版本
（`stage_d3d12/vulkan/opengl`、`fx_cpu/compute/gpu`）只在繪圖器、它們的參考圖或全域設定改變時才執行。
沒有 Python 時，`test.cmd` 會執行全部測試。

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

要在 Visual Studio 中除錯某一個，在目標清單中選 **toms_tests.exe**，以測試名稱作為它的參數
（工作資料夾設為 `assets`）。因為每個測試都連結進同一個程式，測試自己定義的型別或
輔助函式要放在匿名命名空間中。曾經有兩個測試各自有不同的
`struct MockContext`，連結在一起時其中一個默默取代了另一個。

## 執行哪些測試

| 標籤 | 數量 | 檢查什麼 | 需要 |
|---|---|---|---|
| `unit` | 44 | 遊戲自己放在 `src/core` 程式碼旁邊的 `*_test.cpp` 程式（存檔、標題流程、條件、戰鬥條、鏡頭、裝備、技能、鍛造、據點、結局、輪迴、樓層、佔地、任務、節點動畫、粒子效果、glTF 模型、算圖器接受哪些三角網格、sprite 網格與蒙皮（[16](../../engine/zh_TW/16_MESH_ANIMATION.md)）、…），包括對發行的 `assets/data` 的檢查 | 不需要 |
| `atlas` | 3 | `atlas.core`：atlas 工具的打包器、子 sprite、每種匯出格式讀回、舊的 `.pi` 檔逐像素重建；`atlas_sprites_test`：程式碼、資料和 UI 用到的每個 sprite 名稱都在遊戲的 atlas 中；`atlas.assets_up_to_date`：`art_raw/atlas/game.atlasproj` 只靠它打包好的 atlas 就能開啟，且其他檔案都是最新的（[04](../../engine/zh_TW/04_ATLAS_TOOL.md)）；`atlas.fx_up_to_date`：粒子 sprite 的 `fx.atlasproj` 同上（[07](../../engine/zh_TW/07_PARTICLES.md)） | 不需要（全部也都在 `unit` 中；`atlas_sprites_test` 只在那裡） |
| `particle` | 1 | `particle.check_recipes`：對 `docs/examples/fx_recipes.particle` 執行 `particle_editor --headless check`（能解析、每個 sprite 都在它的 atlas 中、曲線 / 池 / 爆發都合理；[07](../../engine/zh_TW/07_PARTICLES.md)） | Qt 編輯器建置（也在 `unit` 中） |
| `anim` | 19 | `anim.check_*`（12 個）：對 `tests/smoke/anim_preview.anim`、蒙皮的 `tests/smoke/anim_skin.anim`、變形的 `tests/smoke/anim_deform.anim`、範例 `docs/examples/anim_recipes.anim` 以及遊戲的 `monster_idle` / `npc_idle` / `door_open` 與武器和角色片段（弓、弓箭手、劍、劍士、法杖、法師）執行 `anim_editor --headless check`（能解析、每個 sprite 都在遊戲 atlas 中、沒有重疊的 key；[05](../../engine/zh_TW/05_ANIMATION.md)、[06](../../engine/zh_TW/06_ANIMATION_RECIPES.md)）；`anim.check_skin_meshes`：對沒有網格檔的 `game.atlas` 副本執行 `anim_skin.anim` 的 `--headless check` 是錯誤，旁邊有 `game.mesh.*` 則沒有；`anim.meshes_up_to_date`：`python tools/refresh_meshes.py --check`（只在找得到 Python 3 時）：每個圖集的網格檔（包括美術風格的）正好只有片段需要的網格，而且是重新切割的結果（[16](../../engine/zh_TW/16_MESH_ANIMATION.md)）；`anim.recipes`：動作範本的 key 與形狀、拒絕的情況、寫出的檔案讀回相同；`anim.weights`：權重手繪的增加、減少與平滑使每個頂點總和維持 1 且最多 4 根骨頭（[16](../../engine/zh_TW/16_MESH_ANIMATION.md)）；`anim.recipe_headless`：`--headless recipe`（`--list`、在蒙皮 golem 的副本上寫 breathe 與 nod、被拒絕的骨頭範本）的結果通過 `--headless check`（[06](../../engine/zh_TW/06_ANIMATION_RECIPES.md) §7）；`anim.editor_selftest`：編輯器在 offscreen 下執行（`-platform offscreen`，不開視窗）——選取、Properties 與 key 清單編輯、viewport 拖曳、復原、儲存、網格格數、動作範本、權重手繪 | Qt 編輯器建置（也在 `unit` 中） |
| `font` | 2 | `font.tool`：圖片字型執行期（`.fnt` 解析、排版）與字型工具：`tests/font/connect_score_pictures`（單張圖片）必須逐位元組重建舊專案的 `connect_score.fnt`，用它繪製的測試文字必須與 `tests/font/preview.png` 相符；`font.editor_selftest`：`font_editor` 在 offscreen 下執行（[09](../../engine/zh_TW/09_FONT_TOOL.md)） | 不需要（兩者也在 `unit` 中；selftest 需要 Qt 建置） |
| `stage_editor` | 2 | `unit.stage_core_test`：原生關卡編輯器的規則，用真實樓層檢查（每個樓層一個位元組不差地寫回、畫格子／復原、檢查與劇情檢查、使用 HTML 編輯器亂數的自動放置、樓層索引與 `floor_index.json` 一個位元組不差）；`stage_editor.app_selftest`：`stage_event_editor` 在 offscreen 下執行，停駐的分頁 ↔ 浮動 ↔ 分割（[02](../../gameplay/zh_TW/02_STAGE_EDITOR.md) 第 6 節） | selftest 需要 Qt 建置（兩者也在 `unit` 中） |
| `studio` | 3 | `studio.canvas_coordinates`：編輯器畫布基底（座標顯示）；`studio.language`：編輯器的 English / 中文 切換，在 offscreen 的小視窗上檢查（[11](../../engine/zh_TW/11_EDITOR_LANGUAGE.md)）；`studio.i18n_complete`：Qt 編輯器每個 `tr()` 文字在 `tools/studio_common/i18n/zh_TW` 中都有中文 | Qt 建置；`i18n_complete` 需要 Python（也在 `unit` 中） |
| `smoke` | 24 | 真正的 `toms_game` 播放一段腳本化的場景；它的截圖必須和 `tests/golden/` 中的參考圖相符（包括 `smoke.anim` 和 `smoke.fx`：地圖上的 `.anim` 片段和粒子效果；`smoke.mesh` / `mesh_cpu`：一個 sprite 畫成擺動的三角網格、夾在四邊形之間，走預設與 CPU 的 sprite 路徑，以及 `smoke.anim_skin`：用三根骨頭彎曲 golem 網格的片段（[16](../../engine/zh_TW/16_MESH_ANIMATION.md)）；`smoke.fx_cpu` / `fx_compute`：同樣的東西走 sprite 批次的其他路徑，`smoke.fx_gpu`：所有發射器都在 GPU 上模擬——全部對同一張圖比對） | GPU；每個測試開一個視窗約 2 秒 |
| `smoke` + `gltf` | 24 | `gltf_viewer` 在固定時間繪製 `tests/gltf/` 中的測試模型；截圖必須和 `tests/golden/gltf_*.png` 相符：蒙皮（線性、cubic spline）、morph target（線性、step）、`EXT_mesh_gpu_instancing`、PBR 材質、法線檢視、陰影（平行光、點光、聚光、全部）；蒙皮、PBR 和陰影也在 Vulkan 和 OpenGL 上測（[08](../../engine/zh_TW/08_GLTF.md)）。物理（Jolt 上的 KHR_physics_rigid_bodies，[08](../../engine/zh_TW/08_GLTF.md) §7）2.5 秒時的畫面；檔案自己的燈光（KHR_lights_punctual）透過它的鏡頭：`gltf_lights`（透視）與 `gltf_camera_side`（正交）。`ctest -L gltf` 只執行這些 | GPU；每個約 1 秒 |
| `scenario` | 32 | `tests/scenarios/` 的每個資料夾一個：`toms_game --scenario=<資料夾> --scenario-test` 依它的設定檔（存檔、玩家、事件）直接開始在某個場景（戰鬥、對話、商店、選單分頁……），執行它的畫格與按鍵，檢查場景正在顯示以及 `expect` 的數值。沒有參考圖；最後一個畫格存在 `test-run/scenario/<name>/`（[08](../../gameplay/zh_TW/08_TEST_SCENARIOS.md)） | GPU；每個約 1 秒 |
| `web` | 5 | 在無頭 Chrome 中執行 `tools/web_smoke_test.mjs`：標題 → 新遊戲 → 存檔 → 重新載入 → 存檔還在，沒有頁面錯誤。對單執行緒版、多執行緒版（用隔離標頭提供），以及打包好的頁面兩次：在沒有標頭的伺服器上（它必須透過 service worker 變成多執行緒）和加上 `?nothreads`（[02](../../engine/zh_TW/02_THREADS.md)），再在**真正的 GPU** 上跑一次（`web.page_gpu`，`--gpu`：Direct3D 11 上的 ANGLE，對 shader 比其他測試用的 SwiftShader 軟體 GPU 更嚴格）。任何編譯或連結失敗的 WebGL shader 都會被記錄，並讓測試失敗 | web 版 / 套件（`tools\release_web.cmd`）、Node 22+、Chrome 或 Edge；缺少的話會**略過**，而不是失敗 |

Smoke 場景有標題、地圖（HUD + 方向盤）、玩家選單的五個分頁（`player_status`、`player_gear`、`player_items`、
`player_events`、`player_system`；[03](../../gameplay/zh_TW/03_PLAYER_MENU.md)）、對話、戰鬥（攻擊一次之後）、商店，以及地圖上的節點動畫
（`--anim`，[05](../../engine/zh_TW/05_ANIMATION.md)）。玩家選單的場景以 `--give=<道具 id>,gold:<n>` 開始：標題關閉時玩家就拿到這些道具和裝備，
讓格子、裝備比較和捲動有東西可顯示。`unit.player_menu_test` 檢查選單的規則，以及道具、商店、屬性和標題資料，
並確認每層列出的每個事件都有事件池紀錄與英文文字。兩個場景顯示上鎖的樓梯（[05](../../gameplay/zh_TW/event_editor/05_FLOOR_LINKS.md) 第 3.2 節）：
`stairs_locked` 走到被 `--stair-lock=F01:ev_village_well` 鎖住的 F01 樓梯（鎖頭與提示），`stairs_unlock` 以 `--events-done=`
完成該事件（「Stairs unlocked」對話框）。`bell_gate` 以 `--goto=F04` 帶著鐘紋鑰匙在 F04 開始新遊戲，打開專屬鑰匙門並觸發門後房間的事件（[event_editor/07](../../gameplay/zh_TW/event_editor/07_KEY_GATES.md)）；`unit.event_chain_test` 檢查資料中的事件動作、門與鐘的事件鏈。NPC 與綁定的事件（[event_editor/08](../../gameplay/zh_TW/event_editor/08_NPC_DIALOGUE.md)）：`talk_menu` 在 F02 走到村長上（他的話題選單），`talk_chain` 進行《夜裡的井》後再次交談（帳本被喚醒，井列為已完成），`talk_trusted` 顯示 `varChanged` 事件的通知，`talk_repeat` 買三次可重複的藥，`unit.talk_system_test` 檢查交談邏輯、存檔，以及資料中的每個 NPC 檔、對話與動作。`unit.stair_links_test` 檢查樓梯清單本身，`unit.floor_index_test` 檢查
`assets/data/events/floor_index.json` 仍與樓層相符（失敗時：用關卡或事件編輯器開啟專案資料夾，它會重建索引）。
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
- **要單獨測試的場景：**在 `tests/scenarios/` 加一個含 `scenario.json` 的資料夾（[08](../../gameplay/zh_TW/08_TEST_SCENARIOS.md)）；下次
  configure 之後就是 `scenario.<資料夾>`，`tools/test_select.py` 已經把資料夾對應到它。
- **畫面：** 加一行 `toms_smoke_test(name reference backend limit scene…)`，用
  `TOMS_UPDATE_GOLDEN=1` 執行一次，檢查新的 `tests/golden/<name>.png`，然後提交它。

Shipping preset 不建置任何測試（`TOMS_BUILD_TESTS=OFF`）。Web 和 Android 版也從不建置。
