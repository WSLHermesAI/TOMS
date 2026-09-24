# 10 — In-Game Editor, Prefabs and Code Binding (L6.5 + L9.1–9.2)

Part of the [Engine Blueprint](README.md). The goal is an editor that **makes game objects with UI
and binds them to code**, the way Unreal Blueprints or Cocos Creator prefabs work. It must run
**inside the game, on desktop (F5 in Visual Studio) and in the browser with no install**.

---

## 1. Can an existing engine or editor give us this?

Every option is scored against the same constraints:
- C++ game code
- a **web build with no install**
- Vulkan (desktop) / WebGPU (web)
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
| **Own editor on Dear ImGui, inside the game** | build | ✓ (same WASM) | ✓ | ✓ | ⭐ **recommended core**: no engine meets C++ + web + VS + own RHI together. Adopt a library for every sub-part (§6) |

**Conclusion:** build the editor *shell* ourselves on Dear ImGui, but adopt the heavy parts:
ECS + reflection (EnTT or flecs), the node-graph UI (imgui-node-editor), gizmos (ImGuizmo), level
layout (LDtk/Tiled/Blender) and dialogue (Ink). Copy Cocos Creator's prefab and property-binding
model and Unreal's event-graph model, scoped down.

## 2. Editor mode inside the game (9.1)

- **Same binary.** A dev build (`ENGINE_EDITOR=ON`) contains the editor. `F12`, `--editor` on
  desktop or `?editor=1` on web switches **Play ⇄ Edit**. Shipping builds compile it out.
- **Play-in-editor:** *Play* snapshots the world (serialized through O.3), runs the game, and *Stop*
  restores the snapshot. The same mechanism powers "play from here" and the Event System's live
  preview ([EventSystem 04 §5](../EventSystem/04_FLOW_PREVIEW.md#5-live-in-game-preview)).
- **Panels:** Hierarchy, Inspector, Scene view (2D/3D, with gizmos), Asset browser (from the
  manifest, R.1), Prefab mode, UI preview (phone / tablet / desktop sizes and UI scales), Console
  (log), Resource overlay (R.4), Event/graph editor (9.4), Profiler.
- **Saving files:**
  - desktop writes into the repo directly
  - on web, the editor posts files to a tiny local **dev server** (`tools/dev_server.py`, which
    writes into the repo), or uses the browser File System Access API where available
  - the local dev server is also what makes web hot reload possible
- **Undo/redo:** a command stack. Every inspector edit is a property-path command generated from reflection.

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
- **One node-graph UI** (imgui-node-editor) serves both the visual graphs and the Event Editor.
  Event flows are the specialised graph type ([EventSystem 03](../EventSystem/03_EVENT_EDITOR.md)).

## 5. Hot reload

| Change | Desktop | Web |
|---|---|---|
| Prefab / UI / graph / flow / data | live reload behind the same handle ([08 §7](08_RESOURCE_AND_LIFETIME.md#7-hot-reload)) | the same, pushed by the editor or dev server |
| C++ | **MSVC Hot Reload** (`/ZI`, Debug) for small edits; **Live++** (commercial) for heavy use | rebuild and reload the page. A play-in-editor snapshot restores state |
| Scripting (optional 7.8) | Lua reload | Lua reload |

## 6. Editor libraries to adopt

| Need | Library (licence) |
|---|---|
| Editor UI + docking | **Dear ImGui** docking branch (MIT) |
| Node graphs | **imgui-node-editor** (MIT; mature but slow-moving) · alternative **imnodes** (MIT) |
| 3D gizmos | **ImGuizmo** (MIT) |
| Plots (profiler, stats) | **ImPlot** (MIT) |
| Icons | IconFontCppHeaders + an icon font (check the font's licence) |
| File dialogs | **nativefiledialog-extended** (zlib, desktop) · `emscripten_browser_file` (MIT) / File System Access API (web) |
| Level layout | **LDtk** (MIT; JSON with a published schema) + LDtkLoader · **Tiled** (editor GPL, which does not affect its output) + tmxlite (zlib) · **Blender** glTF `extras` |
| Dialogue | **Ink** + **inkcpp** (MIT; full ink 1.1 support) with the Inky editor · Yarn Spinner has **no standalone official C++ runtime** (its C++ runtime lives in the Unreal plugin and is pre-release) and uses a custom licence |

## 7. Order of building

1. **E-M4:** O.1 ECS + O.2 reflection + O.3 prefabs; editor shell (hierarchy, inspector, asset
   browser, undo); prefab mode; UI placement and preview; **C++ method binding (level a)** and
   **Event System binding (level b)**; play-in-editor.
2. **E-M6:** visual graph runtime + editor (level c), sharing the node UI with the event editor;
   Spine / glTF / FX previews (9.7).

See [06](06_START_PLAN.md) for exit criteria.
