# TOMS progress report

> 中文版：[zh_TW/PROGRESS_REPORT.md](zh_TW/PROGRESS_REPORT.md). The live status board is
> [progress_report/PROGRESS_REPORT.md](progress_report/PROGRESS_REPORT.md).

## 2026-09-30 — Event editor (Qt) and the Linux editor build

**Done and verified**
- `src/editor/src/event_graph.{h,cpp}`: the event graph model (plain C++, no Qt, compiles on every
  platform). Run against the real data: 142 nodes / 593 links / 380 problems (4 pools, 34 events,
  70 floors, 34 text keys).
- A real data problem found: floors reference event ids that no pool defines (the `_v2` … `_v5`
  variants), so **most floors' event references cannot be resolved**.
- `src/editor/src/event_flow_view.{h,cpp}`: a `QGraphicsView` flow map (columns by type, colours by
  relation, problem nodes in red, filters for chapter / kind / floor / problems only). Passes
  `-fsyntax-only` against the Qt6 headers.
- Portability fixes (not in the editor itself; any Linux/CI build would hit them):
  - `src/engine/CMakeLists.txt`: the shader profiles branch on `WIN32` (Linux desktop has no `s_5_0`).
  - `src/engine/src/embedded_shaders.cpp`: the DXBC headers and the D3D11/12 cases are wrapped in
    `TOMS_SHADER_HAS_DXBC`.
- **`toms_editor` built on Linux for the first time** (`build-linux-editor/bin/toms_editor`, exit=0).

**Not done yet**
- The flow map is not wired into `editor_window` (no tab, no CMake entry), so it has **not actually
  been drawn yet** (only the syntax check passed).
- Property editing and write-back that keeps `_comment` have not started.
- The editor crashes with SIGSEGV on start-up on Linux (after the window is created; probably the
  existing bgfx Play viewport crashing without a GPU surface).

**Next step:** wire the flow map tab in (`editor_window` + CMake) and prove it draws with an Xvfb
screenshot; then fix the start-up crash; then property editing (keeping `_comment` on write-back).
