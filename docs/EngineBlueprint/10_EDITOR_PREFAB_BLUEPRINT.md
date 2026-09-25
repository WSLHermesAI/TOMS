# 10 — Qt Editor, Prefabs and Code Binding (L6.5 + L9.1–9.2)

Part of the [Engine Blueprint](README.md). The goal is an editor that **makes game objects with UI
and binds them to code**, the way Unreal Blueprints or Cocos Creator prefabs work.

> **Revised 2026-09-25.** The editor is a **Qt6 desktop application** (`toms_editor`) with an
> embedded **bgfx** viewport that runs the real game code. It is not shipped and has no web build;
> players on the web only get the game. The earlier plan (an ImGui editor inside the game, also on
> web) is kept in §1 as the rejected alternative. Integration details: [11](11_BGFX_QT_ARCHITECTURE.md).

---

## 1. Can an existing engine or editor give us this?

Every option is scored against the same constraints:
- C++ game code
- a **web build with no install** (for the game; the editor is desktop-only since 2026-09-25)
- our own RHI (bgfx since 2026-09-25; the table was scored against Vulkan / WebGPU)
- debugging in **Visual Studio**
- **text assets** that AI and git can edit
- licence

Facts are dated 2026-09 ([07](07_THIRD_PARTY_LIBRARIES.md)).

| Option | Prefabs / visual logic | Web export | C++ game code | VS debugging | Verdict |
|---|---|---|---|---|---|
| **Unreal** (Blueprints) | best in class | ✗. Built-in HTML5 ended with 4.23 (moved to a community plugin in 4.24) | ✓ | ✓ | fails "web, no install" |
| **Cocos Creator** (prefabs) | ✓ | ✓ | ✗: TypeScript gameplay on a C++ native engine | ✗ for gameplay | fails C++-first. **Its prefab model is the design to copy** (§3) |
| **Godot 4** (scenes = prefabs) | ✓ | ✓. C++ GDExtensions work on web when compiled for web + cross-origin isolation headers. **C# cannot export to web** | ◐ via GDExtension | ◐ (engine is external) | the strongest "adopt a whole engine" alternative, but you give up the own RHI / VS-native workflow and rewrite the game against Godot's scene API |
| **Defold** | ✓ (collections, game objects) | ✓ excellent, small builds | ◐ (Lua gameplay, C++ native extensions) | ✗ for gameplay | good for 2D; Lua-first conflicts with C++ |
| **Flax** 1.12 | ✓ (prefabs, Visual Scripting) | ◐ **experimental** web export (WebGPU), **without C#** | ✓ (C++ scripting) | ✓ | worth watching; web is too new to bet on; source-available licence with royalties (check terms) |
| **Axmol** (cocos2d-x successor) | ✗ no editor | ◐ WASM is "preview" | ✓ | ✓ | a runtime, not an editor |
| **O3DE / Wicked Engine** | ✓ | ✗ (none known) | ✓ | ✓ | no web |
| **External authoring tools** feeding our runtime: **LDtk** / **Tiled** (2D levels + entity fields), **Blender** (3D layout; glTF `extras` custom properties → components), **Inky** (Ink dialogue) | partial | n/a (offline tools) | ✓ | ✓ | ⭐ **adopt as a complement** for level/scene layout and dialogue text |
| Own editor on Dear ImGui, inside the game | build | ✓ (same WASM) | ✓ | ✓ | ○ was the pick until 2026-09-25. Editing on web is lost with the Qt choice, but ImGui stays for in-game debug overlays |
| **Own Qt6 editor app with a bgfx viewport** | build | ✗ (editor only; the game ships on web) | ✓ (links `mt_game`) | ✓ | ⭐ **chosen 2026-09-25**: real desktop widgets (docking, trees, tables, property sheets, undo, file dialogs), the existing `editor/` as a start, and the same bgfx renderer as the game so the viewport is exact |

**Conclusion:** build the editor *shell* in Qt6, but adopt the heavy parts: docking (Qt Advanced
Docking System), node graphs (QtNodes), ECS + reflection (EnTT or flecs), level layout
(LDtk/Tiled/Blender) and dialogue (Ink). Gizmos are drawn by the engine inside the bgfx viewport.
Copy Cocos Creator's prefab and property-binding model and Unreal's event-graph model, scoped down.

## 2. The Qt editor (9.1)

- **Separate executable.** `toms_editor` (Qt6 Widgets) links `eng_*` and `mt_game`. It is built
  only by desktop presets with `ENGINE_EDITOR=ON`; shipping and web presets never see Qt.
- **Viewport = the game renderer.** Each Scene / Prefab / UI-preview panel is a `BgfxViewport`
  widget: a native child window whose handle is passed to bgfx, one bgfx view (or view range) per
  panel, driven by a Qt timer. The engine draws exactly what the shipped game draws
  ([11 §2](11_BGFX_QT_ARCHITECTURE.md#2-embedding-bgfx-in-the-qt-editor)).
- **Play-in-editor:** *Play* snapshots the world (serialized through O.3), runs `mt_game` inside the
  viewport, and *Stop* restores the snapshot. The same mechanism powers "play from here" and the
  Event System's live preview ([EventSystem 04 §5](../EventSystem/04_FLOW_PREVIEW.md#5-live-in-game-preview)).
  *Play standalone* launches the real `mt_app` exe (or the web build) on the current data.
- **Panels (Qt docks):** Hierarchy (`QTreeView` over the entity model), Inspector (generated from
  O.2 reflection), Scene view (2D/3D, gizmos drawn by the engine in the viewport), Asset browser
  (from the manifest, R.1), Prefab mode, UI preview (phone / tablet / desktop sizes and UI scales),
  Console (log), Resource overlay (R.4), Event/graph editor (9.4, QtNodes), Profiler (bgfx stats).
- **Saving files:** the editor writes into the repo directly. A running web dev build hot-reloads
  through the dev server (`tools/dev_server.py`); there is no in-browser editing any more.
- **Undo/redo:** `QUndoStack`. Every inspector edit is a property-path `QUndoCommand` generated
  from reflection.
- **Licence:** Qt6 is used under the LGPLv3 (dynamic linking) or a commercial licence. Because the
  editor is an internal tool that is not distributed to players, LGPL obligations only apply if the
  editor itself is given to people outside the team (e.g. modders).

## 3. Object model (L6.5)

### O.1 Entity-component model

Adopt **EnTT** or **flecs** (both MIT, header/C, work with MSVC and Emscripten). This is a decision
card:

| | EnTT | flecs |
|---|---|---|
| Style | C++ template library, very fast, minimal | C core + C++ API, "batteries included" |
| Reflection | `entt::meta` (manual registration) | built-in meta addon + **JSON serializer** |
| Hierarchy | build it (a parent component) | built in (`ChildOf` relations) |
| Editor support | we build the inspector over `entt::meta` | a lot is available through its meta/JSON |
| Recommendation | choose it if you want full control | ⭐ **choose it to write less code** (reflection + JSON + hierarchy out of the box) |

The existing `node.h` tree remains the transform/UI tree inside components where useful.

### O.2 Reflection

One registration per component and type, used by the inspector, serialization, undo, prefab
diffs, **bindable functions** and visual-graph nodes:

```cpp
ENGINE_COMPONENT(Health)
    .field("max",     &Health::max,     {.min = 1, .tooltip = "Maximum HP"})
    .field("current", &Health::current, {.readonly_in_editor = true})
    .method("heal",   &Health::heal,    {.callable = true, .category = "Combat"});   // appears in graphs & binding dropdowns
```

The backing library is flecs meta, `entt::meta` or **Boost.Describe** (BSL-1.0, MSVC OK). The macro
hides which one is chosen.

### O.3 Prefabs (Cocos-style)

```json
{ "schema": "engine.prefab/1", "id": "prefab.ui.shop_item_row",
  "root": { "name": "Row", "components": {
      "UIRect":   { "anchor": "stretch-x", "height": 96 },
      "NineSlice":{ "image": "tex.ui.row_bg", "border": [12, 12, 12, 12] },
      "Button":   { "onClick": { "target": "..", "component": "ShopPage", "method": "buy", "args": ["$item.id"] } } },
    "children": [
      { "prefab": "prefab.ui.item_icon", "overrides": { "Image.tint": [1, 1, 1, 1] } },
      { "name": "Price", "components": { "Label": { "text": "$item.price", "style": "price" } } } ] } }
```

- **Nested prefabs** (`"prefab": id`), **instance overrides** stored as property-path diffs against
  the source, **variants** (`"base": prefabId`), and *apply / revert override* in the inspector.
- Prefabs are resources (R.2). They are preloaded with their scene's group and instantiated from the
  entity pool (R.3).
- Plain JSON with a schema: AI can write them, the validator checks them, and git diffs are readable.

### UI inside prefabs

UI widgets are **components on the same entities** (UIRect, Image, NineSlice, Label, Button,
ScrollView, Grid, Toggle…). Alternatively, a `UIDocument` component hosts an **RmlUi** document if
RmlUi is chosen for L5 ([02 §L5](02_RUNTIME_SYSTEMS.md#l5-ui)). Either way, the same editor places
game objects and UI, and the UI preview shows phone, desktop and web sizes at each UI scale.

## 4. Binding code: three levels, all picked from dropdowns

| Level | What it is | Stored as | Checked by |
|---|---|---|---|
| **a. C++ method binding** (Cocos-style `@property` + handler) | a UI event or component event calls a **reflected C++ method** on a component in the prefab | `{target, component, method, args}` | load-time resolve; a missing method or wrong arg types is a validation error |
| **b. Event System** (no code) | a UI event or trigger component **emits a signal / arms an event flow** | `{ "emit": "shop_opened" }` or a flow reference | the EventSystem validator ([EventSystem 01 §8](../EventSystem/01_CONCEPTS_AND_DATA_SCHEMA.md#8-validation-rules-enforced-by-toolsvalidate_eventspy-and-the-editor)) |
| **c. Visual graph** (Blueprint-like, O.4) | a graph asset attached to a prefab: event nodes (OnClick, OnTriggerEnter, OnSignal) → flow nodes (branch, sequence, delay, for-each) → **nodes generated from reflected `callable` methods** | `*.graph.json` | typed pins are checked at edit and load time |

- **Visual graphs are interpreted** (no JIT), so they run identically on desktop and web. The editor
  gives live debugging: execution highlight, breakpoints, pin value watch.
- **Scope is kept small on purpose**: event graphs for glue and designer-level logic, not a
  replacement for C++ systems.
- **One node-graph UI** (QtNodes) serves both the visual graphs and the Event Editor.
  Event flows are the specialised graph type ([EventSystem 03](../EventSystem/03_EVENT_EDITOR.md)).

## 5. Hot reload

| Change | Desktop | Web |
|---|---|---|
| Prefab / UI / graph / flow / data | live reload behind the same handle ([08 §7](08_RESOURCE_AND_LIFETIME.md#7-hot-reload)) | the dev server tells the running web build to refetch the changed file |
| C++ | **MSVC Hot Reload** (`/ZI`, Debug) for small edits; **Live++** (commercial) for heavy use | rebuild and reload the page. A play-in-editor snapshot restores state |
| Scripting (optional 7.8) | Lua reload | Lua reload |

## 6. Editor libraries to adopt

| Need | Library (licence) |
|---|---|
| Editor UI | **Qt6 Widgets** (LGPLv3 / commercial) |
| Docking | **Qt Advanced Docking System** (LGPL-2.1) · fallback `QDockWidget` |
| Node graphs | **QtNodes** (paceholder/nodeeditor, BSD-3-Clause) · alternative: own `QGraphicsView` scene |
| Property inspector | own, generated from O.2 reflection on `QTreeView` + delegates (QtPropertyBrowser is unmaintained) |
| Undo | `QUndoStack` (Qt) |
| 3D / 2D gizmos | drawn by the engine in the bgfx viewport; **ImGuizmo** (MIT) via an ImGui overlay inside the viewport if a full gizmo set is wanted |
| Plots (profiler, stats) | Qt Charts (GPLv3 / commercial; fine for an internal tool) or ImPlot inside the viewport overlay |
| File dialogs | `QFileDialog` |
| In-game debug overlays (not the editor) | **Dear ImGui** with bgfx's imgui backend (`ENGINE_DEBUGUI`, dev builds only) |
| Level layout | **LDtk** (MIT; JSON with a published schema) + LDtkLoader · **Tiled** (editor GPL, which does not affect its output) + tmxlite (zlib) · **Blender** glTF `extras` |
| Dialogue | **Ink** + **inkcpp** (MIT; full ink 1.1 support) with the Inky editor · Yarn Spinner has **no standalone official C++ runtime** (its C++ runtime lives in the Unreal plugin and is pre-release) and uses a custom licence |

## 7. Order of building

1. **E-M0:** the `toms_editor` shell with one docked `BgfxViewport` (see [06](06_START_PLAN.md)).
2. **E-M4:** O.1 ECS + O.2 reflection + O.3 prefabs; Qt editor shell (hierarchy, inspector, asset
   browser, undo); prefab mode; UI placement and preview; **C++ method binding (level a)** and
   **Event System binding (level b)**; play-in-editor.
3. **E-M6:** visual graph runtime + editor (level c), sharing the QtNodes UI with the event editor;
   Spine / glTF / FX previews (9.7).

See [06](06_START_PLAN.md) for exit criteria.
