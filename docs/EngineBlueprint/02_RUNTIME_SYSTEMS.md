# 02 — Runtime System Cards (L0–L7)

Part of the [Engine Blueprint](README.md). There is one card per runtime system from
[01 §2](01_SYSTEM_HIERARCHY.md#2-full-system-list). Library facts (licence, WASM support, status) are
collected with their sources in [07](07_THIRD_PARTY_LIBRARIES.md). The cards only name the pick.

**Card fields:**

| Field | Meaning |
|---|---|
| **Needs** | requirements, including desktop/mobile/web differences |
| **TOMS** | what exists today, its maturity (solid / partial / stub / hacky / none), and the reuse verdict |
| **A · Build** | own implementation, with effort: S ≤ 2 days · M ≤ 1 week · L ≤ 3 weeks · XL > 3 weeks (AI-assisted) |
| **B · Adopt** | library, plus whether it builds with both **MSVC and Emscripten** (✓ yes · ◐ with effort · ✗ no) |
| **VS** | how the system is run and debugged in Visual Studio |
| **Scope** | which resource scope it acquires into ([08 §4](08_RESOURCE_AND_LIFETIME.md#4-preload-and-release-by-scope-the-easy-part)); only for systems that hold resources |

⭐ marks the recommended option.

---

## L0 Foundation

### 0.1 Core types, containers, math · P0
- **Needs:** fixed-width types, span/string_view, small vectors, 2D/3D math (vec, mat3/mat4, quat), rect helpers.
- **TOMS:** `node.h` has its own mat3 (solid, 2D only); otherwise it uses std.
- **A · Build:** keep std plus the mat3 from `node.h` (S).
- **B · Adopt ⭐:** **glm** for 3D math (✓, header-only). The std library is enough for containers.
- **VS:** natvis for vec/mat. Unit tests.

### 0.2 Logging, assert, diagnostics · P0
- **Needs:** levels, categories, file/line, thread-safe; sinks for file, debugger output and the browser console; asserts that break into VS.
- **TOMS:** `src/engine/log.h` (solid, `{}` formatting, file sink `toms.log`). **Reuse.**
- **A · Build ⭐:** extend `log.h` with an `OutputDebugString` sink, a web `console.*` sink and an assert macro (S).
- **B · Adopt:** spdlog / fmt (✓). Not needed.
- **VS:** logs appear in the Output window; asserts trigger `__debugbreak()`.

### 0.3 Object tracking & memory stats · P0
- **Needs:** live-object registry per type, leak dump at exit, per-type byte counters feeding R.4.
- **TOMS:** `src/engine/object.h` `Trackable` / `Object` / `DumpLeaks` (solid). **Reuse.**
- **A · Build ⭐:** add byte counters and a `Stats()` snapshot API for the soak test (S).
- **B · Adopt:** Tracy memory zones for profiling (desktop only; its client cannot run in the browser build).
- **VS:** natvis for the registry. `DumpLeaks` output goes to the Output window.

### 0.4 Time, timers, fixed-step clock · P0
- **Needs:** a monotonic clock; **fixed-step simulation** with interpolated rendering, the same on desktop and web; pause/scale.
- **TOMS:** none. Desktop passes a variable `dtMs`; the web uses a fixed 33 ms step at 30 fps (hacky, and the two differ).
- **A · Build ⭐:** accumulator loop in L1.1 (S).
- **B · Adopt:** SDL3 timers (✓) for the clock source.
- **VS:** a unit test with a fake clock.

### 0.5 Strings / UTF-8 · P0
- **Needs:** UTF-8 decode/iterate, case-insensitive compare, id interning (hashed asset ids).
- **TOMS:** UTF-8 decode in `game_text_draw.cpp` and `ui_layout.h::wrap` (partial).
- **A · Build ⭐:** a small utf8 + string-id module (S).
- **B · Adopt:** utfcpp (✓, header-only), optional.
- **VS:** natvis shows interned ids as text.

### 0.6 Serialization (JSON + schema) · P0
- **Needs:** JSON read/write with stable formatting (small git diffs); **JSON Schema validation** shared by runtime, editors and `tools/*.py`; later a binary format for shipping.
- **TOMS:** nlohmann/json (solid), with no schema validation in C++.
- **A · Build:** hand-written validators (M), which is what `validate_story.py` does today.
- **B · Adopt ⭐:** **nlohmann/json** + **json-schema-validator** (✓). Python uses `jsonschema` against the same schema files.
- **VS:** nlohmann's upstream natvis.

## L1 Platform

### 1.1 App lifecycle & main loop · P0
- **Needs:** one entry point for Win/macOS/Linux/Android/iOS/Web; init → loop → shutdown; suspend/resume (mobile, browser tab hidden); the fixed-step loop from 0.4.
- **TOMS:** `src/game/core/main.cpp` (GLFW) + `src/engine/emscripten_main.cpp` (593 lines, JS inside strings). Two diverging mains (hacky). **Replace.**
- **A · Build:** own per-OS layer (L–XL; mobile is costly).
- **B · Adopt ⭐:** **SDL3** app callbacks (`SDL_AppInit` / `Iterate` / `Event` / `Quit`), which drive the browser loop for you (✓).
- **Editor note:** the Qt editor has its own Qt event loop and does not use SDL3. The engine loop is therefore an Engine::tick(dt) call that SDL3's SDL_AppIterate (game) or a Qt timer (editor viewport) drives; nothing in L2–L8 may call SDL window or event APIs directly ([11 §2](11_BGFX_QT_ARCHITECTURE.md#2-embedding-bgfx-in-the-qt-editor)).
- **VS:** F5 on the desktop exe. Web via [05 §4](05_DEV_WORKFLOW_VS.md#4-f5-targets).

### 1.2 Window, display, DPI, safe area · P0
- **Needs:** resizable window, **devicePixelRatio / DPI-correct back buffer**, orientation, notch safe areas, fullscreen.
- **TOMS:** the web back buffer is fixed at 1024×768 and DPI is never used; desktop DPI is only mouse remapping (hacky).
- **A · Build:** per OS (L).
- **B · Adopt ⭐:** **SDL3** (pixel density, safe area, fullscreen; ✓).
- **VS:** debug overlay showing window size, pixel density and safe area.

### 1.3 Input & action map · P0
- **Needs:**
  - devices: keyboard, mouse, **touch/multi-touch**, gamepad
  - an **action map** (`move_up`, `confirm`, `menu`) with rebinding
  - the virtual pad as an input source
  - identical behavior on desktop and web
- **TOMS:** key routing duplicated between the two mains; all pointers go through `Game::handleTouch` in 1024×768 design space (hacky).
- **A · Build ⭐ (the action map):** on top of SDL3 events (M).
- **B · Adopt:** **SDL3** for devices and gamepad database (✓). The action map layer is small, so build it.
- **VS:** an input debug overlay; recorded input replays in tests.

### 1.4 Device / OS info & capabilities · P1
- **Needs:** OS, device class (phone/tablet/desktop), locale, GPU caps (`bgfx::getCaps()`: renderer type, max texture size, instancing, compute, texture formats), memory hints. This feeds the quality tiers (3.12).
- **TOMS:** mobile detection is `innerWidth<900 || innerHeight<560` (hacky).
- **A · Build ⭐:** small module over SDL3 + RHI caps (S).
- **B · Adopt:** SDL3 (✓).
- **VS:** printed to the log at startup.

### 1.5 Web page shell · P0
- **Needs:**
  - page: HTML/CSS canvas fit, loading screen and progress, fullscreen, rotate-device overlay
  - audio unlock on the first gesture
  - hosting: cross-origin isolation headers when threads are used, a cache-busting versioned bundle name
- **TOMS:** ~60 lines of JS injected as a string (`emscripten_run_script`), plus `web/*.html` (hacky).
- **A · Build ⭐:** a real `shell.html` + `engine.js` checked in as files (M). The JS test API (Q.3) lives here.
- **B · Adopt:** Emscripten `--shell-file` (✓).
- **VS:** F5 web target; DevTools for the JS side.

### 1.6 Threads & job system · P1
- **Needs:** a worker pool for decode/IO (R.2); must degrade to single-thread on web builds without threads.
- **TOMS:** none.
- **A · Build:** a small job queue (M).
- **B · Adopt ⭐:** **enkiTS** or Taskflow (◐: web needs `-pthread` + cross-origin isolation, or a single-thread fallback path).
- **VS:** Threads window; Tracy on desktop.

## L2 Files & persistence

### 2.1 VFS & mounts · P0
- **Needs:**
  - read-only mounts: `assets://`, `data://` (folders in dev, packages in shipping)
  - a writable per-OS `user://`: `%APPDATA%`, `~/Library/Application Support`, Android internal storage, IDBFS on web
- **TOMS:** paths built as `assetDir + "/../data/…"`; saves go to a `save/` folder relative to the working directory, so VS F5 writes into `Debug/save` (hacky).
- **A · Build:** own mount table over std::filesystem (M).
- **B · Adopt ⭐:** **SDL3** `SDL_GetPrefPath` + Storage API, or **PhysicsFS** for mounts/zip (✓ / ◐, its web support is not confirmed). IDBFS for web persistence (Emscripten).
- **VS:** unit tests with a temp dir; the overlay shows the resolved paths.

### 2.2 Async IO & packages · P1
- **Needs:** background reads; **per-scene web packages** fetched on demand (08 §2); a progress callback.
- **TOMS:** a single `--preload-file` bundle with all of `assets/` + `data/` (partial).
- **A · Build ⭐:** package writer (pipeline) + reader (M).
- **B · Adopt:** `emscripten_fetch` (✓), **miniz** for zip packages (✓).
- **VS:** a desktop test reads the same package format.

### 2.3 Atomic write, backup, recovery · P0
- **Needs:** write to a temp file, flush, **replace atomically**; keep the last good file as `.bak`; recover from a corrupt file with a user message; call `syncfs` on web.
- **TOMS:** `writeJsonAtomic` in `src/game/save/save_system.cpp:142-155` uses C `std::rename`, which **fails on Windows when the target exists**. Every save after the first stays in `.tmp`. Web works.
- **A · Build ⭐:** use `std::filesystem::rename` (which replaces the target on MSVC via `MoveFileExW(... MOVEFILE_REPLACE_EXISTING)`) + a `.bak` rotation + an IDBFS sync hook (S).
- **B · Adopt:** —.
- **VS:** a test that **overwrites an existing save 3×** (today's `save_test.cpp` deletes the target first, which is why it never caught the bug).

### 2.4 Save data model & migrations · P0
- **Needs:** versioned documents per slot (meta / run / settings); **migration functions** vN→vN+1; unknown fields preserved; slot metadata (play time, location).
- **TOMS:** `save_system.*`, `save_slots.*`: JSON, schema v3, versions **detected but never migrated** (partial). **Reuse the model, add migrations.**
- **A · Build ⭐:** a migration chain (M).
- **B · Adopt:** —; reflection (O.2) can generate field IO later.
- **VS:** golden save files from older versions as test fixtures.

### 2.5 Settings store · P1
- **Needs:** typed settings with defaults, clamps and a change signal; per-device overrides (quality tier).
- **TOMS:** `game_settings.*` (solid, clamped). **Reuse.**
- **A · Build ⭐:** generalize into a key/value store with schema (S).
- **B · Adopt:** —.
- **VS:** unit tests.

## L2.5 Resources
Full design in [08](08_RESOURCE_AND_LIFETIME.md).

| Card | Pri | TOMS | A · Build | B · Adopt | VS |
|---|---|---|---|---|---|
| R.1 Asset registry & manifest | P0 | none (paths everywhere) | ⭐ pipeline step + loader (M) | — | validator runs as a CTest test |
| R.2 Resource manager: handles, refcount, groups, async, hot reload | P0 | `texture.*` unused, `Audio::play` reloads every call | ⭐ (M–L) | `entt::resource_cache` if EnTT is chosen (✓) | natvis for `Handle`/`Ref`; resource overlay |
| R.3 Object & particle pools | P1 | none | ⭐ (S–M) | Effekseer's internal pools for FX (✓ via EffekseerForWebGL) | high-water marks in the overlay |
| R.4 Leak & memory tooling, budgets | P0 | `object.h` `DumpLeaks` (solid) | ⭐ scope-exit + shutdown checks + 100× soak (M) | ASan (MSVC `/fsanitize=address`, Emscripten), CRT debug heap, Tracy (desktop) | `desktop-asan` preset; soak test in Test Explorer |

## L3 Render
Full design in [09](09_RENDERING_2D_3D.md). Summary:

| Card | Pri | TOMS | ⭐ Recommendation (see 09 for alternatives) |
|---|---|---|---|
| 3.1 RHI | P0 | three hand-written renderers; `render_iface.h` too narrow; WebGPU broken (hacky) | **decided: bgfx** (09 §2, [11](11_BGFX_QT_ARCHITECTURE.md)); web = WebGL2 |
| 3.2 Frame graph | P0 | none (sprites pass, then text pass) | build (M) |
| 3.3 2D renderer | P0 | `batch_renderer.h` (Vulkan only) | build on the RHI (M) |
| 3.4 Spine 2D | P1 | none | adopt spine-cpp (licence per developer) or Rive (MIT) |
| 3.5 glTF parser | P1 | none | adopt fastgltf or cgltf |
| 3.6 3D renderer | P1 | none | build a forward renderer on bgfx, starting from the bgfx examples (PBR, shadows, instancing) ([11 §5](11_BGFX_QT_ARCHITECTURE.md#5-3d-models-what-bgfx-supports)) |
| 3.7 Skeletal anim 3D | P2 | none | adopt ozz-animation |
| 3.8 Shadows | P1 | none | build CSM + PCF; volumes optional |
| 3.9 2D/3D composition | P1 | none | build on the frame graph |
| 3.10 Particles / FX | P1 | none | adopt Effekseer |
| 3.11 Post-processing | P2 | none | build (S–M) |
| 3.12 Quality tiers | P1 | UI-scale hack only | build (S) |

## L4 Text

### 4.1 Glyph cache & paged atlases · P0
- **Needs:**
  - glyphs rasterized on demand into **multiple atlas pages** with LRU eviction; **no silent failure when an atlas fills**
  - CJK-scale glyph counts; per-size or SDF caches
- **TOMS:** `src/engine/font.*` (partial): one atlas with 2048 spare cells; if it exceeds `GL_MAX_TEXTURE_SIZE`, `makeTexture` returns 0 and **no text draws**; every added glyph re-uploads the whole atlas.
- **A · Build ⭐:** paged atlas + dirty-rect upload (M). Rasterization comes from 4.2.
- **B · Adopt:** the msdf-atlas-gen approach for scalable text (see 4.4).
- **VS:** an atlas viewer panel in the editor.

### 4.2 Font sources per platform · P0
- **Needs:**
  - **web: render with the browser's system font, with no font download.**
  - desktop/mobile: use OS fonts, with a small bundled fallback for missing scripts
- **TOMS:**
  - web: **already solved**. `Font::buildFromCanvas` draws glyphs with Canvas 2D `fillText` using the browser's sans-serif fonts, so no font file ships. **Keep this path.**
  - desktop: a 16.8 MB gitignored `wqy-zenhei.ttc` + Noto JP/KR subsets; a runtime fallback that scans `/usr/share/fonts`, which finds nothing on Windows.
- **A · Build ⭐:** one `IGlyphSource` interface:
  - `CanvasGlyphSource` (web; exists)
  - `DWriteGlyphSource` (Windows), `CoreTextGlyphSource` (Apple)
  - `FreeTypeGlyphSource` (Linux/Android + bundled fallback)
  - Effort: M per platform.
- **B · Adopt:** **FreeType** (✓) as the rasterizer for file-based fonts. The system font lookup is per-OS code (DirectWrite / CoreText / fontconfig).
- **VS:** a test renders a CJK/Hangul/Latin sample string to an offscreen PNG (Q.2).

### 4.3 Shaping & line breaking · P1
- **Needs:** kerning/ligatures (Latin), correct CJK line breaking (no break before `。」` and similar), mixed scripts.
- **TOMS:** `ui_layout.h::wrap` (partial, CJK-aware but hand-written).
- **A · Build:** keep `wrap` + rules (S) — enough for CJK plus Latin without shaping.
- **B · Adopt ⭐ (when Latin quality matters):** **HarfBuzz** (✓) + **libunibreak** (✓). On web, Canvas text shaping can also be used by rasterizing whole runs.
- **VS:** golden text-layout tests.

### 4.4 Rich text & SDF text · P2
- **Needs:** inline color/size/icons (`[gold]+50[/]`), outline/shadow, crisp scaling for UI scale 1.0–2.0.
- **TOMS:** none.
- **A · Build ⭐:** a markup parser → glyph runs (M).
- **B · Adopt:** **msdf-atlas-gen** (✓, offline) for bundled fonts; RmlUi covers rich text if chosen for L5.
- **VS:** golden tests.

## L5 UI

### 5.1 UI tree, layout, anchors, UI scale · P0
- **Needs:** retained tree; anchors, stretch, stack/grid layouts; **safe area**; one **UI scale** for phone/desktop; layout, draw and hit-test from **one geometry** (the rule from `dialogue_layout.h`).
- **TOMS:**
  - `node.h` (solid, barely used)
  - `ui_root.h` (partial, dialogue only)
  - `ui_layout.h` helpers
  - `dialogue_layout.h` (solid pattern)
- **A · Build:** on `node.h` + **Yoga** (flexbox) or **Clay** for layout (M–L).
- **B · Adopt ⭐:** **RmlUi**: HTML/CSS-like documents, flexbox, data binding and a custom render interface; runs on Emscripten (✓). This is a decision card together with 5.2–5.5.
- **VS:** UI debugger overlay (outline rects, hovered widget, layout values).

### 5.2 Widget library · P0
- **Needs:**
  - basics: Panel/Image, **9-slice**, Label/RichText, **Button** (normal/hover/pressed/disabled), Toggle, Slider, ProgressBar
  - lists: **ScrollView** (clip + inertia + scrollbar), **virtualized List/Grid** (inventory 9-grid, store, save slots)
  - containers: Tabs, Modal/Dialog stack, Toast, Virtual pad
- **TOMS:** none. Everything is immediate-mode quads + text in `game_scene_draw.cpp` / `game_store.cpp` / `game_inventory.cpp` / `game_title_glue.cpp`.
- **A · Build:** ~15 widgets on 5.1 (L).
- **B · Adopt ⭐:** **RmlUi** provides most of them (the `ninepatch` decorator, `overflow: scroll`, form controls). Build the rest (virtual pad, virtualized grid) as custom elements (M).
- **VS:** a widget gallery scene, used as a golden-image test.

### 5.3 Input routing, focus, navigation · P0
- **Needs:** hit-testing, pointer capture, drag, **gamepad/keyboard focus navigation**, and modal blocking handled by the tree, not by hand.
- **TOMS:** ~30 `int xRect_[4]` members in `game.h`, written by draw code and checked in a long if-chain in `Game::handleTouch` (hacky). **Delete.**
- **A · Build:** part of 5.1 (M).
- **B · Adopt ⭐:** RmlUi event model + focus (✓).
- **VS:** input overlay showing the hit widget.

### 5.4 Themes & styles · P1
- **Needs:** skin atlas, fonts and colors in one theme; per-state styles.
- **TOMS:** colors hard-coded in the draw code.
- **A · Build:** style sheets on 5.1 (M).
- **B · Adopt ⭐:** RmlUi RCSS (✓).
- **VS:** hot reload of theme files.
- **Scope:** Global.

### 5.5 UI documents & data binding · P1
- **Needs:** UI authored as **text documents** (AI-editable, editor-editable); binding to reflected game data (O.2); localization keys.
- **TOMS:** none (UI is code).
- **A · Build:** a JSON layout format + binder (L).
- **B · Adopt ⭐:** RmlUi documents (RML/RCSS) + data models (✓).
- **VS:** hot reload.
- **Scope:** Scene / Global.

## L6 Scene

### 6.1 Scene / state stack · P0
- **Needs:** push/pop/replace scenes (Title, World, Battle, Menu, StageSelect, Ending); the **top scene owns input**; scenes own their resource group.
- **TOMS:** `src/engine/game_state.h` has a validated transition table that **does not drive control flow**; the real state is ~15 bools in `Game` (stub). Reuse the table idea.
- **A · Build ⭐:** (S–M).
- **B · Adopt:** —.
- **VS:** overlay showing the stack; tests of the transition table.

### 6.2 Tilemap & world grid · P1
- **Needs:** multi-layer tile grids, footprints (multi-cell entities), markers, chunked drawing for the 45×30 floors, import from stage JSON / LDtk / Tiled.
- **TOMS:** `stage.h`, `game_assets.cpp::parseStage` (partial), `footprint.h` (solid).
- **A · Build ⭐:** on 3.3 (M).
- **B · Adopt:** **LDtkLoader** / **tmxlite** for import (✓).
- **VS:** map debug overlay.
- **Scope:** Scene.

### 6.3 Camera · P1
- **Needs:** follow/rooms modes, bounds, shake; 2D orthographic and 3D perspective cameras for the camera stack (3.9).
- **TOMS:** `src/game/systems/camera.*` (solid, 2D, tested). **Reuse.**
- **A · Build ⭐:** extend it for 3D (S).
- **B · Adopt:** glm for the math.
- **VS:** unit tests.

### 6.4 Sprite animation & tween · P1
- **Needs:** frame animations from atlases; tweens (UI transitions, damage numbers); easing.
- **TOMS:** none.
- **A · Build ⭐:** (S–M).
- **B · Adopt:** tweeny (✓, header-only), optional.
- **VS:** an animation preview panel.

### 6.5 Timers, sequencing, coroutines · P1
- **Needs:** delays and sequences for cut-scenes / battle flow, cancelled with their scene.
- **TOMS:** ad hoc timers in `Game`.
- **A · Build ⭐:** C++20 coroutines tied to the scene lifetime (M).
- **B · Adopt:** —.
- **VS:** coroutine state shown in the scene overlay.

## L6.5 Object model
Full design in [10](10_EDITOR_PREFAB_BLUEPRINT.md).

| Card | Pri | TOMS | ⭐ Recommendation |
|---|---|---|---|
| O.1 Entity-component model | P1 | `Stage::entities` strings (`monster:slime`) | adopt **EnTT** (✓) or **flecs** (✓) — decision card in 10 |
| O.2 Reflection | P1 | none | `entt::meta` / flecs meta, or Boost.Describe (✓) |
| O.3 Prefabs & scene serialization | P1 | none | build on O.1 + O.2 (M–L) |
| O.4 Visual graph runtime | P2 | none | build a small interpreter (L); the node UI is shared with the event editor |

## L7 Services

### 7.1 Event system (flows) · P1
- **Needs:** data-driven Trigger → Steps → Fire flows; per-system step handlers; save/resume.
- **TOMS:** designed in [docs/EventSystem](../EventSystem/README.md) (not built). The hard-coded event tiles in `game_input.cpp` are what it replaces.
- **A · Build ⭐:** as designed (L). No library matches it.
- **B · Adopt:** —.
- **VS:** simulator + debug overlay ([EventSystem 04](../EventSystem/04_FLOW_PREVIEW.md)).
- **Scope:** flows in Scene groups.

### 7.2 Event bus / messaging · P0
- **Needs:** typed events, **unsubscribe tokens (RAII)**, an **immediate and a queued (end-of-frame) mode**, and safe subscribe/unsubscribe during dispatch.
- **TOMS:** `src/engine/event_bus.h` (58 lines; typed but no unsubscribe or queue).
- **A · Build ⭐:** extend it (~40 lines, S). This was the conclusion of the MessageSender comparison.
- **B · Adopt:** `entt::dispatcher` (✓) if EnTT is chosen.
- **VS:** a subscriber list in the overlay.

### 7.3 Condition evaluator · P1
- **Needs:** JSON condition DSL (leaf + all/any/not) over a context interface.
- **TOMS:** `src/engine/condition.*` (solid, tested). **Reuse.**
- **A · Build ⭐:** as is.
- **B · Adopt:** —.
- **VS:** unit tests.

### 7.4 Data registry & catalog · P0
- **Needs:**
  - typed definition tables (enemies, items, equipment, skills, floors…) loaded by id through R.2
  - validated against JSON schemas, cross-referenced (`itemId` exists)
  - exposed to editors and the Event System's `_schema` files
- **TOMS:** each system parses its own JSON through `readJsonFile` (partial).
- **A · Build ⭐:** (M).
- **B · Adopt:** json-schema-validator (0.6).
- **VS:** the validator runs as a test.
- **Scope:** Global / Session.

### 7.5 Localization · P1
- **Needs:** key → text per language, fallback chain, plural/format args, and a missing-key report.
- **TOMS:** `src/game/ui/localization.*` (solid). **Reuse.**
- **A · Build ⭐:** add formatting args + a missing-key report (S).
- **B · Adopt:** —.
- **VS:** unit tests.
- **Scope:** Global.

### 7.6 Audio · P1
- **Needs:** SFX with caching, **music streaming**, buses (master/music/SFX/UI) with volumes, **web audio unlock on the first gesture**, and 3D positional audio later.
- **TOMS:** `src/game/audio/Audio.*` (stub-level; miniaudio; reloads the WAV on every play; master volume only).
- **A · Build ⭐:** a mixer layer on miniaudio (M).
- **B · Adopt:** **miniaudio** (✓, in use). SoLoud (✓) is an alternative.
- **VS:** audio overlay (voices, buses).
- **Scope:** Global (SFX), Scene (music).

### 7.7 Progress services (run / meta) · P1
- **Needs:** the game-agnostic part of run/meta progress (flags, counters, choices, per-tile status, cycle), serialized through 2.4.
- **TOMS:** `run_state.*`, `story_controller.h`, `entity_status.*` (solid). **Reuse** and generalize the flag/counter storage.
- **A · Build ⭐:** (S).
- **B · Adopt:** —.
- **VS:** a save inspector panel (9.6).

### 7.8 Scripting (optional) · P2
- **Needs:** optional fast-iteration gameplay glue next to C++ and visual graphs.
- **TOMS:** none. The design so far deliberately avoids scripting (data + C++).
- **A · Build:** — (use O.4 visual graphs).
- **B · Adopt:** **Lua 5.4 + sol2** (✓). AngelScript is possible, but on WASM it needs `AS_MAX_PORTABILITY` (slower).
- **VS:** Lua debugging needs an extra tool; this is a cost to weigh. A decision card in 10.
