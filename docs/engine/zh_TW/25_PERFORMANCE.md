> 英文原文：[25_PERFORMANCE.md](../25_PERFORMANCE.md)

# 25 — 效能檢查：每一格畫面花在哪裡，顯示在畫面上，也寫進檔案

擁有者，2026-11-26：遊戲要能說明每一格畫面的時間花在哪裡，依主要的更新與繪製分開，並列出 draw call 與三角形數量，
即時顯示在畫面上；持續一整秒低於 30 fps 時，把花時間的地方寫成紀錄檔（2026-11-26：30 fps，之後 30 秒不再寫，同一原因不寫第二次）。

## 1. 使用方式

| | |
|---|---|
| **F4** | 任何畫面皆可，包括標題：**Performance** 視窗（ImGui）：最近一秒的 fps 與每格時間（低於 30 fps 顯示紅色）、GPU 時間、draw call、三角形、compute 呼叫、最近 300 格的時間圖、每個區段的平均與最大值（Update、Render、Frame）、計數器、各 view 的 GPU 時間、**Dump now** 按鈕，以及自動紀錄的開關 |
| **自動紀錄** | 一整秒內結束的畫面少於 30 格（或單一格超過一秒）時，在開始 3 秒後：寫出 `perf_dumps/perf_<日期>_<時間>_<n>.txt`（與 `toms.log` 同一處），並在主控台印一行說明原因（最花時間的兩個區段，不算 `frame.present`：「render.game + render.walk3d」）。之後 30 秒內不再寫，且要等遊戲恢復速度；本次執行已寫過同一原因的變慢不再寫（主控台會說明） |
| **設定 > 效能檢查：開 / 關** | 沒有鍵盤時開同一個視窗（手機、手機上的網頁）：遊戲選單、系統、設定的最後一行；視窗的 ✕ 也能關閉 |
| **Ctrl +** / **Ctrl −** | F4 視窗的文字放大 / 縮小一級（10%），50% 到 300%（`=`/`+` 與 `-` 鍵，或數字鍵盤的）；視窗隨之變大；也可按視窗裡的 **A−** / **A+** |
| `--perf-dump=on` / `off` | 自動紀錄；預設 **開**，腳本執行（`--frames`、`--fixed-dt`）時 **關**：測試慢不算 |
| `--perf-dump=exit` | 結束時寫出最後 2 秒：用來量測一個場景，例如情境測試 |

紀錄內容：原因與位置（繪圖後端、遊戲狀態、樓層）、fps、每個區段的平均與最大值、計數器、各 view 的 GPU 時間，以及最後 2 秒
每一格的時間、GPU 時間、draw call、三角形與最花時間的三個區段。

## 2. 量測內容

**區段**（CPU 時間，毫秒，每格加總；會巢狀：`render.particles` 也包含在 `render.battle` 與 `render.game` 裡）：

| 區段 | 位置 |
|---|---|
| `update.game` | `Game::f_Update` 全部 |
| `update.map_objects`、`update.battle_rig`、`update.monster_rig`、`update.battle_fx`、`update.backdrop`、`update.battle_logic` | 它的各部分 |
| `update.particles` | **所有**粒子特效的更新（引擎的 `EffectInstance::f_Update`：戰鬥、背景、舞台火焰、預覽） |
| `update.anim` | 所有動畫播放器（`AnimPlayer::f_Update`） |
| `update.stage3d` | 3D 舞台的請求與載入（`BattleStage::f_Request` / `f_Poll`） |
| `update.ui` | UI 全部：`update.ui_build` 遊戲產生 UI 狀態（`Game::f_BuildUiState`）、`update.ui_sync` 放入資料模型、`update.ui_rml` RmlUi 的更新（有變的綁定、樣式、版面） |
| `render.game` | `Game::f_Draw` 全部；`render.walk3d`、`render.battle` 是它的部分；`render.sprite_submit` 是把 sprite 批次交給 bgfx |
| `render.particles` | **所有**粒子特效的繪製（`EffectInstance::f_Draw`） |
| `render.stage3d_battle`、`render.stage3d_walk`、`render.stage3d_sprites`、`render.title_scene` | 3D 場景（glTF）與站在其中的 sprite |
| `render.ui`、`render.imgui` | RmlUi 文件、除錯視窗 |
| `frame.present` | `bgfx::frame()`：等待 GPU 與螢幕（開啟 vsync 時，60 Hz 一格剩下的時間會出現在這裡） |

**計數器**（每格）：`particles.live`（繪製的特效中存活的粒子）、`particles.effects`、`particles.gpu_emitters`、
`anim.players`、`sprites.quads`。

**GPU**（bgfx 統計，最近完成的一格）：draw call、三角形（list 與 strip）、compute 呼叫、整格的 GPU 時間，以及各 view 的
GPU 與 CPU 時間（bgfx profiler，在 F4 視窗開啟或自動紀錄啟用時開啟）。view 有名稱：`clear`、`game`、`ui`、`top`、`imgui`、
`3d shadow` / `3d opaque` / `3d blended`（標題、戰鬥或樓層的 3D 場景）、`capture`、`layer`。整格的 GPU 時間包含等待 vsync；
各 view 的時間才是實際工作。

## 3. 加入量測

```cpp
#include "perf.h"
{ toms::perf::Scope l_oPerf(toms::perf::section::g_pszRenderParticles); ... }   // 區塊的時間計入區段
toms::perf::f_Count(toms::perf::counter::g_pszParticlesLive, l_iN);             // 本格計數器加上
```

新的區段或計數器：在 `src/core/engine/perf.h`（`section` / `counter`）加一行（名稱不寫死字串，docs/project/12 §7），
再在執行處加 `Scope` 或 `f_Count`。名稱開頭（`update.`、`render.`、`frame.`）決定它在 F4 視窗的哪個表格。`perf.h`
只有標頭：不含遊戲而建置 `particle_fx.cpp`、`anim_player.cpp` 的粒子與動畫工具不需改動。只用於主執行緒。一個 scope
花費兩次讀取時鐘。

主程式（`main_sdl.cpp`）在 `bgfx::frame()` 之後以 `GameSession::f_EndPerfFrame()` 結束每一格。

## 4. 第一次量測（2026-11-26）

F30 頭目戰，Direct3D 11，vsync 開：2 秒內 57 fps，102 個 draw call，約 80 000 個三角形；遊戲本身的 CPU 工作每格不到 1 ms
（UI 0.4 ms 最多），GPU 約 0.9 ms（3D 舞台的 opaque view 0.5 ms）。有一格 43 ms：`render.ui` 39 ms，是 RmlUi 第一次繪製
頭目名稱橫幅（它的字形）。`frame.present` 的時間（15 ms）是 vsync 的等待。

**UI（擁有者的量測，F30 地圖：`update.ui` 13 ms，32 fps）。** 那是 **Debug** 建置（Visual Studio 的 F5）：每個區段都是 Release
的 15 到 40 倍（本機 `render.walk3d` 9.9 ms 對 0.25，`update.ui` 6.1 ms 對 0.42）。UI 大部分時間花在 RmlUi 每格重新檢查所有文件的
每個綁定，連隱藏的畫面也算（`GameUi::f_Sync` 呼叫 `DirtyAllVariables`）。現在只重新檢查顯示中的畫面，畫面關閉那一格再檢查一次：
F30 地圖 `update.ui` 在 Release 0.42 → 0.21 ms，Debug 6.1 → 1.6 ms（48 → 60 fps）。Debug 下剩下最大的是 `render.walk3d`（10 ms）。
請以 Release 判斷速度（`tools\build.cmd`，或 Visual Studio 的 `windows-release` 設定）；Debug 是用來逐步除錯的。

## 5. 檔案

| 內容 | 位置 |
|---|---|
| 檢查器（區段、計數器、歷史、紀錄檔） | `src/core/engine/perf.h` |
| 視窗（F4）、view 名稱、每格的 GPU 數字 | `src/game/src/game_session.cpp`（`f_DrawPerfWindow`、`f_NameViews`、`f_EndPerfFrame`） |
| bgfx 統計 | `src/engine/src/bgfx_host.cpp`（`f_BgfxHostFrameStats`、`f_BgfxHostSetProfiler`、`f_BgfxHostSetViewName`） |
| 選項、`frame.present`、結束時的紀錄 | `src/game/src/main_sdl.cpp` |
