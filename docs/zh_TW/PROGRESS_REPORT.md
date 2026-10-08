# TOMS 進度報告

> 英文版：[PROGRESS_REPORT.md](../PROGRESS_REPORT.md)

## 2026-09-30 — 事件編輯器（Qt）與 Linux 編輯器建置

**已完成並驗證**
- `src/editor/src/event_graph.{h,cpp}`：事件圖模型（純 C++，無 Qt，可跨平台編譯）。對真實資料執行結果：
  142 節點 / 593 連線 / 380 問題（4 個 pool、34 個事件、70 層樓、34 個文字鍵）。
- 發現真實資料問題：樓層引用了 pool 未定義的事件 id（`_v2`…`_v5` 變體），**多數樓層的事件引用無法解析**。
- `src/editor/src/event_flow_view.{h,cpp}`：`QGraphicsView` 流程圖（依類型分欄、依關係上色、問題節點標紅、
  章節/類型/樓層/只看問題 篩選）。已對 Qt6 標頭通過 `-fsyntax-only`。
- 平台可攜性修正（非編輯器本身，任何 Linux/CI 建置都會踩到）：
  - `src/engine/CMakeLists.txt`：shader profile 依 `WIN32` 分支（Linux desktop 不含 `s_5_0`）。
  - `src/engine/src/embedded_shaders.cpp`：DXBC 標頭與 D3D11/12 case 以 `TOMS_SHADER_HAS_DXBC` 包住。
- **`toms_editor` 首次在 Linux 建置成功**（`build-linux-editor/bin/toms_editor`，exit=0）。

**尚未完成**
- 流程圖尚未接進 `editor_window`（沒有分頁、沒有 CMake 項目），因此**尚未實際繪出**（只有語法檢查通過）。
- 屬性編輯與「保留 `_comment`」的寫回尚未開始。
- 編輯器在 Linux 啟動時 SIGSEGV（視窗已建立後崩潰，疑似既有 bgfx Play viewport 在無 GPU surface 時崩潰）。

**Next Step**：接上流程圖分頁（`editor_window` + CMake），用 Xvfb 截圖證明繪出；再修啟動崩潰；再做屬性編輯（寫回時保留 `_comment`）。
