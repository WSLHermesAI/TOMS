# 關卡編輯器 (Stage Editor) — Qt6 C++

A **Qt6 Widgets** sub-project that edits the game's stage data with a
**layer concept**, a **node/grid canvas** for placing game objects, and
**import / export** of the game-compatible JSON.

## Features
- **Layer system** — `地形 Terrain` + `物件 Objects` layers (toggle visibility,
  pick active layer). Add more layers in `StageModel::layers`.
- **Node system / canvas** — click a palette item to arm a "brush", then click
  grid cells to place game objects (wall, floor, stairs, doors, keys, player
  start, NPCs, monsters, items). Drag placed nodes to move them.
- **Add / remove / edit** — click a node to select it; the Inspector shows its
  properties (type, x, y, layer, and monster stats / item effects) which are
  editable inline.
- **Connections (node graph)** — link this stage to the next via `up`
  (extendable to `down`).
- **Import / Export** — reads/writes the game's `stageNN.json`
  (`tiles` + `legend` + `connect`). Editor-only fields (`editor_layers`,
  `editor_objects`) are embedded so the layer/object state survives round-trips
  without affecting the engine.

## Objects catalog (`catalog.h`)
27 object types mirrored from the game: terrain, stairs, doors (Y/B/R),
keys (Y/B/R), player start `@`, NPCs (法師/村民/國王/公主/侍女),
monsters (史萊姆/蝙蝠/石魔像/骷髏/幽靈/惡魔/巫王 Vorkath),
items (攻擊/防禦寶石, 紅/藍藥水, 金幣).

## Build — Windows (Visual Studio + Qt6)
1. Install **Visual Studio 2022** (Desktop C++).
2. Install **Qt 6** (e.g. via the Qt Online Installer) and its **MSVC 64-bit**
   component. Make sure `qt-cmake` is on PATH (or use the VS "Qt VS Tools").
3. Configure & build:
   ```
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
         -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
   cmake --build build --config Release
   build\Release\tower_editor.exe
   ```
   (The CMakeLists uses `CMAKE_AUTOMOC`, so moc runs automatically.)

## Build — Linux / WSL (no root, verified)
Qt is installed here with **aqtinstall** (no `sudo`, no `apt`):
```
python3 -m venv ~/qt-venv && source ~/qt-venv/bin/activate
python3 -m pip install aqtinstall
python3 -m aqt install-qt linux desktop 6.7.3 linux_gcc_64 -O ~/opt/Qt
# -> ~/opt/Qt/6.7.3/gcc_64
```
Qt6Gui needs system OpenGL dev files (present only as runtime .so.1). Provide
no-root shims once:
```
mkdir -p ~/opt/glsym/lib ~/opt/glsym/include/GL ~/opt/glsym/include/EGL
ln -sf /usr/lib/x86_64-linux-gnu/libGL.so.1     ~/opt/glsym/lib/libGL.so
ln -sf /usr/lib/x86_64-linux-gnu/libEGL.so.1    ~/opt/glsym/lib/libEGL.so
ln -sf /usr/lib/x86_64-linux-gnu/libOpenGL.so.0 ~/opt/glsym/lib/libOpenGL.so
# add minimal GL/gl.h + EGL/egl.h (Khronos headers) under ~/opt/glsym/include
```
Then configure & build (the ~/opt/ccache wrapper is broken, so pin the real g++):
```
source ~/.vulkan-env.sh
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="$HOME/opt/Qt/6.7.3/gcc_64" \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_CXX_COMPILER_LAUNCHER= \
  -DCMAKE_LIBRARY_PATH="$HOME/opt/glsym/lib" -DCMAKE_INCLUDE_PATH="$HOME/opt/glsym/include"
cmake --build build
# run headless (smoke test) or with an X server:
QT_QPA_PLATFORM=offscreen ./build/tower_editor
# interactive UI on WSL: xvfb-run ./build/tower_editor  (or VcXsrv/X410 on Windows)
```
Verified: editor builds, launches under offscreen, and a compiled import/export
round-trip of stage01.json reproduces identical `tiles` + `connect.up`.

## Workflow
1. **Import** an existing stage (`data/stages/stage01.json`) to edit it, or
   **New** to start one.
2. Pick a layer, then a palette item, then click the grid to place.
3. Click a placed node → edit its properties in the Inspector.
4. Set stage `up` connection.
5. **Export** — produces JSON the game engine loads directly.

## Files
```
editor/
  CMakeLists.txt
  src/catalog.h        object-type catalog (shared with game)
  src/stagemodel.h/.cpp  data model + import/export (game-JSON compatible)
  src/stagescene.h/.cpp  node/grid canvas (layers + placement)
  src/mainwindow.h/.cpp  palette, layer panel, inspector, connections, toolbar
  src/main.cpp
```
It reuses the vendored `../external/json/json.hpp` header (same as the engine).
