# 08 — Resources and Lifetime (L2.5)

Part of the [Engine Blueprint](README.md). **Priority P0.** Every texture, atlas, font, sound,
data file, UI document, prefab, mesh, Spine skeleton, particle effect and shader goes through this
layer. Objects and particles come from its pools. **No other system may open a file or create a
GPU/audio object on its own** ([01 §1.1](01_SYSTEM_HIERARCHY.md#11-dependency-rule)).

Goals:
- **easy preload** (declare a group, await it)
- **easy release** (leave the scope)
- **provably no leaks**: checked by automated tests, not by review

---

## 1. Starting point in TOMS

| What | Where | State |
|---|---|---|
| Leak registry (`Trackable` / `Object`, `DumpLeaks`) | `src/engine/object.h` | solid. **Kept** as the base of the leak checks |
| `TextureManager` | `src/engine/texture.*` | unused (only `texture_test` links it) |
| Sprite + font atlas | the renderers (`renderer*.cpp`) | loaded once, never released per scene; one atlas of each |
| Sound effects | `src/game/audio/Audio.cpp` `play(name)` | loads the WAV on **every** call; no cache and no release |
| Dialogue JSON | `Game::startDialogue()` | re-read from disk for every conversation |
| Data JSON | `readJsonFile()` in `src/game/core/game_helpers.h` | scattered, path-based (`assetDir + "/../data/…"`) |
| Web packaging | `CMakeLists.txt` `--preload-file` | one bundle with **all** of `assets/` and `data/`, including tool-only files |

## 2. Asset registry and manifest (R.1)

A pipeline step (L9.8) scans the content folders and writes `assets.manifest.json`:

```json
{
  "tex.ui.panel":      { "type": "texture", "path": "ui/panel.ktx2", "bytes": 48213, "hash": "…" },
  "atlas.ui":          { "type": "atlas",   "path": "ui/ui.atlas.json", "deps": ["tex.ui.page0"] },
  "spine.hero":        { "type": "spine",   "path": "spine/hero.skel", "deps": ["atlas.hero"] },
  "gltf.tower_room":   { "type": "gltf",    "path": "models/room.glb", "deps": ["tex.room.albedo", "tex.room.orm"] },
  "fx.slash":          { "type": "effect",  "path": "fx/slash.efkefc", "deps": ["tex.fx.slash"] },
  "prefab.enemy.slime":{ "type": "prefab",  "path": "prefabs/enemy_slime.prefab.json", "deps": ["spine.slime", "fx.hit"] },
  "data.enemies":      { "type": "data",    "path": "data/enemies.json", "schema": "enemy_table" },
  "data.story_pool":   { "type": "data",    "path": "data/events/pool_act01.json", "tags": ["tool-only"] }
}
```

- **Everything is referenced by id**, never by path: prefabs, UI documents, event flows, code.
- `deps` are computed by the importers: a glTF → its textures and buffers, a UI doc → its fonts
  and images, a prefab → everything its components reference.
- **The validator fails the build** on a dangling id, a dependency cycle, or a `tool-only` asset
  referenced by runtime content.
- **Packages** are derived from the manifest and groups (§4): one web package per scene group plus a
  global one, so the web build downloads what a scene needs, not everything.

## 3. Handles and the resource manager (R.2)

```cpp
// engine/resource/resource.h  (sketch)
template <class T> class Handle {            // 32-bit index + 32-bit generation; trivially copyable
    uint32_t index = 0, gen = 0;             // gen mismatch = stale (released) -> Debug assert, never deref
};

template <class T> class Ref {               // RAII: holds one reference; copy = +1, destroy = -1
public:
    const T* get() const;                    // nullptr until Ready
    LoadState state() const;                 // Unloaded / Queued / Loading / Ready / Failed
    Handle<T> handle() const;
};

class ResourceManager {
public:
    template <class T> Ref<T> acquire(AssetId id, ResourceGroup& into);   // +1, starts the load if needed
    ResourceGroup& group(GroupId id);                                      // see §4
    void update(FrameBudget budget);         // finishes async loads: main-thread GPU uploads within budget
    void collect();                          // frees refcount-0 assets (CPU now, GPU deferred, §5)
    ResourceStats stats() const;             // per type: live count, bytes CPU / GPU estimate
};
```

- **Loaders per type** (texture, atlas, font, sound, data, UI document, prefab, glTF, Spine,
  effect, shader) are registered by the system that owns the type. This is the same "owner
  registers its own verbs" rule as the [EventSystem](../EventSystem/02_GAME_INTEGRATION.md).
- **Async:**
  - Desktop: a job thread (L1.6) reads and decodes, and the main thread does the GPU upload inside
    a per-frame budget.
  - Web: `emscripten_fetch` per package, decoding in a worker where the build has threads, upload
    on the main thread.
  - Progress is reported for loading screens.
- **Dependencies load first.** An asset is Ready only when all its `deps` are Ready. Acquiring
  an asset acquires its dependencies into the same group.
- **Failure is a state, not a crash.** A Failed asset returns a visible placeholder (a magenta
  texture, a "?" glyph, silence) and logs once. The validator should have caught it.

## 4. Preload and release by scope (the easy part)

Scopes nest, and each is a **ResourceGroup**:

| Scope | Lives for | Example contents |
|---|---|---|
| **Global** | the whole process | UI skin atlas, fonts, common SFX, the text table |
| **Session / run** | one play session / run | player prefab, equipment icons |
| **Scene / floor** | while a scene is on the stack | the floor's tileset, its enemies' prefabs, the floor's event flows |
| **Transient** | one battle, one cut-scene | battle effects, a boss's Spine skeleton |

```cpp
// A scene declares what it needs (in data or computed from its files) and awaits it:
auto& g = res.group("scene.floor.F33");
g.preload({"prefab.enemy.wraith", "tileset.crypt", "flow.ss05_crypt_choir"});   // + all deps
co_await g.ready();                  // or poll g.progress() for a loading bar
// … scene runs; systems acquire into g or a longer-lived group …
g.release();                         // on scene exit: drop every reference the group holds
```

Rules:
- **A scene can only acquire into its own group or a longer-lived one.** Acquiring outside any
  scope is a Debug assert, so there is no orphan reference that nobody will release.
- `release()` drops the group's references. An asset **shared** with a longer-lived group stays;
  anything that reaches refcount 0 is freed at the next `collect()`.
- **Preload lists can be generated.** The pipeline walks a scene's prefabs, flows and UI documents
  and writes the group, so authors rarely write them by hand. An explicit list is only for
  "likely soon" assets, such as the next floor.
- Scene transitions overlap: preload the next group, switch, then release the old group, so shared
  assets are never unloaded and reloaded.

## 5. GPU lifetime

bgfx already defers GPU destruction: `bgfx::destroy(handle)` is queued and executed by the render
thread after the frames that use the handle are done, so the engine never needs its own fence
logic. What bgfx does **not** do is refcount or track ownership: its handles are plain 16-bit
indices, a double `destroy` or a use-after-destroy asserts only in debug builds, and handle pools
are fixed-size (`BGFX_CONFIG_MAX_TEXTURES` and friends). So this layer wraps every bgfx handle in a
resource-owned `Ref`, decides **when** the last reference drops, and counts live handles per type
against the bgfx limits. Memory passed to `bgfx::makeRef` must stay alive for two frames (or use
`bgfx::copy`); the resource layer owns that lifetime. The leak checks compare live counts with
`bgfx::getStats()` (`numTextures`, `numVertexBuffers`, …) at shutdown.

Render targets (shadow maps, transient frame-graph targets) are owned by the frame graph (L3.2).
They are recreated on resize or quality-tier change through the same queue.

## 6. Pools for objects and particles (R.3)

| Pool | Holds | Notes |
|---|---|---|
| Entity pool | spawned prefab instances (enemies, projectiles, pickups) | instantiating a prefab takes a pooled entity and applies the prefab's components |
| UI row pool | virtualized list / grid rows (inventory, store, save slots) | ScrollView reuses rows; there is no allocation per scroll |
| Toast / notification pool | on-screen messages | fixed capacity |
| **Particle pool** | a preallocated particle buffer per effect type / quality tier | emitters never allocate per particle. When the adopted FX runtime (Effekseer) has its own pools, they are sized from here |

- Every pool has a **fixed capacity** set per quality tier (L3.12), and reports **high-water
  marks** in the resource overlay.
- Exceeding capacity is a **logged, counted event** that falls back to reusing the oldest item or
  dropping the spawn. It never silently allocates.
- Pooled items are "returned", never deleted. The leak check asserts that every pool is full again at scene exit.

## 7. Hot reload

- Desktop: a file watcher notices a changed file and reloads it **behind the same handle**, so
  every holder sees the new version with no re-acquire.
- Editor: the Qt editor ([10](10_EDITOR_PREFAB_BLUEPRINT.md)) writes into the repo; its own viewport
  reloads through the same watcher. A running **web** dev build is told by the dev server
  (`tools/dev_server.py`) to refetch the changed file.
- A failed reload keeps the old version and logs the error.

## 8. Leak and memory tooling (R.4), all automated

| Check | When | Failure means |
|---|---|---|
| **Resource overlay** (ImGui) | always available in dev builds | — (shows groups → assets → refcount → owners, per-type live counts and bytes, pool high-water marks, pending GPU deletions) |
| **Scope-exit check** | every `group.release()` in Debug | an asset owned only by that group still has references. **Report the acquire call sites** (Debug builds record them) |
| **Shutdown check** | process exit, every test binary | `DumpLeaks` (`object.h`) is non-empty, live handles ≠ 0, or pending GPU deletions ≠ 0. **The test run fails** |
| **Soak test** | CI + VS Test Explorer | for each scene: load → run scripted frames → unload, **100 times**, headless. Live counts and bytes must return to the baseline, with a small tolerance for allocator slack |
| **ASan** | the `desktop-asan` preset (MSVC `/fsanitize=address`, integrated in VS) and a web ASan debug build | use-after-free, overflow, leaks at exit |
| **CRT debug heap** | Windows Debug | `_CrtDumpMemoryLeaks` as a backstop for raw allocations |
| **Tracy memory zones** | on demand (desktop) | profiling of allocation hot spots |

## 9. Budgets

Each quality tier sets memory budgets (texture, audio, meshes). Web and mobile are the tight ones.
When a budget is exceeded, the manager logs it and **may evict cached assets whose refcount is 0**
(LRU) before loading more. It never evicts anything still referenced. A budget overrun that
eviction cannot fix is a warning in dev builds and an entry in the performance log.

## 10. Resource types covered

| Type | Loader owner | Typical scope | Release path |
|---|---|---|---|
| Texture / atlas page | L3.1 / L3.3 | Global (UI), Scene (tilesets) | refcount → GPU deferred destroy |
| Font face + glyph pages | L4 | Global | pages evicted by LRU when unreferenced |
| Sound (SFX / music stream) | L7.6 | Global (SFX), Scene (music) | refcount; streams close on release |
| Data table | L7.4 | Global / Session | refcount |
| UI document / style | L5.5 | Scene / Global | refcount |
| Prefab | L6.5 | Scene | refcount; instances return to the entity pool |
| Event flow / visual graph | L7.1 / L6.5 | Scene | refcount; the runner drops flows of released scenes |
| glTF scene (mesh, material, buffers) | L3.5 / L3.6 | Scene | refcount; deps released with it |
| Spine skeleton + atlas | L3.4 | Scene / Transient | refcount |
| Particle effect | L3.10 | Transient / Scene | refcount; particles from the pool |
| Shader / pipeline | L3.1 | Global | refcount; permutations cached per tier |
| Render target | L3.2 frame graph | frame graph | recreated on resize or tier change, via the deferred queue |

## 11. Build vs. adopt

| Part | Recommendation |
|---|---|
| Manager, handles, groups, pools, leak checks | **Build.** Small (M effort) and the heart of the engine. No library fits this contract across every resource type |
| GPU object lifetime | **Adopt** bgfx's deferred destroy; **build** the refcounted `Ref` wrapper and handle-count checks on top |
| File IO / packages | **Adopt** PhysicsFS or the SDL3 Storage API + `emscripten_fetch` (L2) |
| Entity handles / resource cache | **Adopt** `entt::resource_cache` / `entt::handle` if EnTT is chosen for O.1 |
| Effect pools | **Adopt** the Effekseer manager, sized from R.3 |
| Detection | **Adopt** ASan, the CRT debug heap and Tracy |

Library facts are in [07](07_THIRD_PARTY_LIBRARIES.md).
