# 04 — 遷移計畫：從 TOMS（Vulkan）到 Qt + bgfx 上的 TOMS

> 英文原文：[04_MIGRATION_PLAN.md](../04_MIGRATION_PLAN.md)

每個階段的原則：**遊戲保持可玩**，而且每個階段結束時都有一項可以執行的檢查。
`src/core` 中的核心遊戲程式碼在某個階段需要改它之前，都不加修改直接編譯；在那之前
就從它現在的位置編譯。

## 第 1 階段 — 現有遊戲跑在 bgfx 上，外面包一個 Qt 編輯器（已完成）

| 項目 | 位置 |
|---|---|
| `BgfxRenderer` 實作現有的 `IRenderer`；compat 的 `renderer.h` 讓 `new Renderer()` 建立它 | `src/engine/src/bgfx_renderer.*`、`src/game/compat/` |
| SDL3 遊戲執行檔取代 GLFW + Vulkan 的 `main.cpp`（操作相同） | `src/game/src/main_sdl.cpp`、`src/game/src/game_session.*` |
| 由 bgfx 繪製的 ImGui 開發者視窗（F1、F2）（所有玩家 UI 已在 2026-09-27 移到 RmlUi，[08](08_RMLUI.md)） | `src/engine/src/imgui_bgfx.*` |
| Qt 編輯器：執行真正遊戲的 bgfx *Play* 視圖、關卡清單、session 面板、舊關卡編輯器作為分頁 | `src/editor/` |
| 每個程式一份 shader 原始碼，由 shaderc 編譯成 D3D11/12、Vulkan、GL、GLES | `src/engine/shaders/` |
| 附對話框和下載連結的前置需求檢查；VS preset；`build.cmd` | `cmake/`、`tools/`、`CMakePresets.json` |
| 改為自動取得 glm，不再用手動安裝的 `GLM_DIR` 副本 | `cmake/TomsDependencies.cmake` |

**檢查：** `toms_game.exe --frames=120 --keys=enter@30 --screenshot=stage.png` 顯示的第 1 關
和 Vulkan 版相同；`toms_editor.exe` 在 *Play* 分頁中能玩它。

**2026-09-25 已驗證**（Windows 11、Visual Studio 2026 / MSVC 14.51、Qt 6.9.0）：

- `toms_game` 在 **Direct3D 11、Direct3D 12、Vulkan 和
  OpenGL 4.3** 上繪製標題畫面和第 1 關。D3D12 和 OpenGL 與 D3D11 逐像素相同，只有會動的標題游標例外。
  Vulkan 上大約 1% 的像素不同，全部在字形和面板邊緣：是一個像素的光柵化
  偏移，不是少了元素。
- `toms_editor` 在 Direct3D 11、HiDPI（1972×1634 裝置像素）下於 *Play* 分頁執行遊戲，
  並有關卡清單、session 面板和舊關卡編輯器分頁。
- 兩個執行檔結束時，舊的 `Object::DumpLeaks` 報告都顯示「no leaks」。
- `toms_game.exe` 只匯入 Windows 和 MSVC 執行階段 DLL（沒有 Qt）；`toms_editor.exe` 匯入
  Qt6Core/Gui/Widgets。

**第 1 階段沒有做的（已知缺口）：**

- **Web 版。** 在第 2 階段完成（2026-09-26）：見 [06_BUILD_WEB.md](06_BUILD_WEB.md)。
- **測試。** 30 個 `*_test.cpp` 檔（在 `src/core` 中）還沒建置：原本建置它們的
  舊專案已經不在了。第 2 階段會把它們註冊到 CTest。
- **行動裝置配置切換：** web 版套用的是擁有者的規則（透過
  `Game::setUiScale` / `setPadScale` 放大 UI 物件，遊戲解析度不變），而不是已退役的 `setDesignSize`。
  只有讀取 `UiRoot` 的畫面會放大；其餘的遷移是遊戲端的工作。

## 第 2 階段 — 掌握平台層和測試

1. 把舊的單元測試（`src/**/*_test.cpp`）在 `tests/` 中註冊為 CTest 目標，讓
   Visual Studio 的 *測試總管* 列出它們。加一個 smoke 測試，執行 `toms_game --frames` 並
   把截圖和存好的基準 PNG 比對（容許 GPU 差異）。
2. ✅ **Web 版**（2026-09-26）：`web-*-windows` / `web-*` preset、`tools\build_web.cmd` +
   `serve_web.cmd`；瀏覽器膠合程式（IDBFS 存檔、存檔欄重新整理、行動裝置縮放、頁面按鈕）放在
   `main_sdl.cpp` + `src/game/web/shell.html`，而不是另外的 `main_web.cpp`。已在
   無頭 Chrome 中驗證：標題 → 新遊戲 → 存檔 → 重新載入後存檔還在。見 [06](06_BUILD_WEB.md)。
3. ✅ 刪除了 GLFW/Vulkan 和 Emscripten 的進入點（2026-09-27）；從第 1 階段起它們的行為就在
   `game_session.cpp` / `main_sdl.cpp` 中。

**檢查：** CTest 在 VS 和 CI 中全綠；web 版在 Chrome 和 Edge 中能玩第 1 關。

## 第 3 階段 — 拆掉 `Game` 這個上帝物件，移除 compat shim

1. 開始直接編輯 `src/core`（它仍是遊戲程式碼的家；「不修改」的規則到此結束）。
2. 把 `Game::loadAssets` 中的 `new Renderer()` 換成由宿主傳入的渲染器；然後
   刪除 `src/game/compat/`。
3. 依 `docs/EngineBlueprint/03_GAME_LAYER.md` 的設計，把 `Game` 拆成場景和服務。
4. 把即時模式 UI 和大約 30 個點擊矩形欄位換成真正的 UI 層（RmlUi 或自己的
   widget；見 EngineBlueprint 02 §L5）。

**檢查：** 所有移植的測試都通過；`src/game/compat/` 不見了；`src/core` 直接和引擎溝通。

## 第 4 階段 — 擴充編輯器

1. 關卡編輯器改讀遊戲的資料登錄，而不是寫死的 `catalog.h`
   （EngineBlueprint 04 §1 描述的落差），並透過 bgfx viewport 繪製關卡，讓
   你編輯的和遊戲繪製的完全一樣。
2. 使用 Qt Advanced Docking System 的停駐面板；由反射驅動的屬性面板；`QUndoStack`。
3. 來自 `docs/EventSystem/` 的事件 / 流程編輯器（QtNodes），在 viewport 中即時預覽。
4. 更多 viewport（prefab 預覽、sprite atlas 檢視器），透過 `bgfx::createFrameBuffer` 在它們
   自己的原生視窗上（見 [01 §5](../01_ARCHITECTURE.md#5-qt--bgfx-in-the-editor)）。

**檢查：** 在編輯器中編輯的關卡，不需任何手動步驟就能在 `toms_game.exe` 中載入。

## 第 5 階段 — bgfx 上的 3D 和特效

計畫在 EngineBlueprint `09_RENDERING_2D_3D.md` 和 `11_BGFX_QT_ARCHITECTURE.md`：透過
fastgltf 載入 glTF、ozz-animation（先用它的 CPU `SkinningJob`，之後再做 GPU 蒙皮）、bgfx instancing、
來自 bgfx `16-shadowmaps` 範例的 cascaded shadow map、透過 efkbgfx 使用 Effekseer。

## 舊專案的程式碼會怎樣

從 2026-09-27 起，舊的 Vulkan 建置已經不在，本專案就是 repository 根目錄。舊的
遊戲程式碼放在 `src/core`，由 `toms_core` 編譯。那裡所有不再被
編譯的檔案都在 2026-09-27 刪除了（git 歷史中還有）。

| 舊的 | 狀態 | 移除於 |
|---|---|---|
| `src/core/engine/renderer.*`、`renderer_webgl.*`、`renderer_webgpu.*`、`vk_util.h`、`batch_renderer.h`、`texture.*`（+ `texture_test.cpp`） | 已刪除；由 `BgfxRenderer` 取代（透過 `src/game/compat/renderer.h`） | 已完成 |
| `src/core/engine/imgui_layer.*`、`imgui_web.*` | 已刪除；由 `src/engine/src/imgui_bgfx.*` 取代 | 已完成 |
| `src/core/game/core/main.cpp`、`src/core/engine/emscripten_main.cpp` | 已刪除；由 `src/game/src/main_sdl.cpp` + `game_session.cpp` + `src/game/web/shell.html` 取代 | 已完成 |
| `src/game/compat/renderer.h`（`Renderer` = `BgfxRenderer`） | 核心程式碼的 `new Renderer()` 仍在使用 | 第 3 階段 |
| Vulkan/GLSL shader（`.spv`、`.vert`、`.frag`） | 隨舊建置一起刪除；由 `src/engine/shaders/*.sc` 取代 | 已完成 |
| `src/editor/stage/`（Qt 關卡編輯器） | 編譯進 `toms_editor` 作為分頁 | 程式碼在第 4 階段搬移 |
| 舊的根目錄 `CMakeLists.txt`（Vulkan/WebGL） | 2026-09-27 刪除；根目錄的 `CMakeLists.txt` 現在是本專案的 | 已完成 |
