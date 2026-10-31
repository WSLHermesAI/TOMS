# 12 — Resource manager and objects from JSON (design)

This is a design for one resource manager in the core engine, and for one way of making render objects and game
objects from JSON. It starts from what FM79979 already did, looks at what TOMS does today, and ends with a plan in
small steps.

Status (2026-10-17): steps 1–3 of the plan (section 6) are **built**: the resource cache, the game loading through
it, and render objects made from JSON. Steps 4–6 are still to do. The owner's decisions are in section 8; section 9
lists what was built and its tests.

## 1. Summary

| Today in TOMS | Proposed |
|---|---|
| Each user loads its own copy of an atlas and its textures: the idle clips, the door clips, each object clip file, both previews, each editor canvas | **One cache by file path.** An atlas is loaded once and shared through `std::shared_ptr` |
| A texture is freed only by the code that loaded it (`releaseTexture` lists in `Game`) | Holding is automatic (`shared_ptr`). Unused resources stay cached until a **memory budget** is passed; a **stamp** (scope) frees what a preview or document loaded |
| An object's look is a JSON object merged and read **again every frame** (`Game::lookOf`) | The JSON is turned into a **render object once** (at floor load); the frame only draws it |
| Each kind of drawing has its own code path (sprite, clip, idle clip, particle preview, glTF viewer) | One base, `RenderObject`, made by a **factory from a `"type"` field**. Each real class registers itself, as `GetMe` did |
| Game objects are plain structs filled by hand from `enemies.json`, `items.json` and the stage file | The same factory pattern makes game objects from their records |

The result:
- one GPU copy of each atlas page;
- no JSON work while drawing;
- floors release what only they used;
- editors and the game share the same loading code;
- a new kind of object is one class plus one registration line.

## 2. What FM79979 does (the references)

### 2.1 `NamedTypedObject`: the common base

`Core/Common/NamedTypedObject.h`

- **Type:** every class has a `TypeID` (`DEFINE_TYPE_INFO`, `TYPDE_DEFINE_MARCO`). `Type()` is virtual, so a base
  pointer can tell its real class. `IsSameType` compares the pointers, which is fast.
- **Name** (`wstring`), an **owner** (`m_pWoner`) and a **unique ID** (`GetGlobalUniqueID`).
- **`Clone()`** (`CLONE_MYSELF`, `LAZY_CLONE_FUNCTION`): an object loaded once is the template; scenes use clones.
- **`ToTiXmlElement()`:** the object can write itself back out.
- **Debug:** `DumpUnReleaseInfo()` lists objects never deleted, and a magic ID catches deleted pointers.

### 2.2 `cGameResourceManager`: one manager per kind of resource

`Core/GameplayUT/ResourceMaintaner/GameResourceManager.h`

- There is a vector of managers indexed by `eGameObjectType`:
  - 2D mesh, morphing animation, MPDI list, sound, image (PI), particle, behaviour particle, font;
  - and `eGBT_DEFAULT_RESOURCE_PARSER`, for objects that are not files.
- Look-ups are `GetObject<T>(name, type)` and `GetObjectByFileName<T>(file, type)`, each with a `dynamic_cast` to
  the real class.
- **`ParseLazyResource(file)`:** an XML file lists ready-made render objects, for example
  `<cNumeralImage PI="CarGoApp/JP_BBQ/UI.pi" Image0="ID-0" … Name="CarIDNumerial"/>`.
  - Each element goes through `GetRenderObjectBehavior(element)` (`ObjectXMLParser.cpp`), which compares the tag
    with each class's `TypeID` and calls that class's `GetMe(element)`.
  - The result is kept by name.

### 2.3 `GetMe` and `cCommonRegisterManager`: the factory

`Core/GameplayUT/CommonRegister/CommonRegisterManager.h`

- Every class has `static Base* GetMe(TiXmlElement*)`.
  - It returns `nullptr` when the element's tag is not its own type.
  - Otherwise it returns `new Type(element)`.
  - `GET_ME_WITH_BASE_TYPE` and `LAZY_GET_ME` write this function.
- The manager keeps a list of these functions (`REGISTER_CLONE_FUNCTION_TO_MANGER`).
  `GetObjectByParseXmlElement` tries each in turn and returns the first object made.
- The caller only knows the base type. The real class is chosen by the data.
- Sprite example: `cPuzzleImageUnit::GetMe` reads:
  - `PI` (the atlas file, found or loaded through `cGameApp::GetPuzzleImageByFileName`);
  - `PIUnit` (the sprite's name in that atlas);
  - `Name`.

  It returns the unit, or a clone of it. This is exactly "a JSON object that says it is a sprite, names the atlas and
  the image, and gives back a sprite".

### 2.4 `cResourceStamp`: releasing what a scene loaded

`Core/GameplayUT/ResourceMaintaner/ResourceStamp.h/.cpp`

1. `StampResource1()` copies the current list of every manager: animation, image, sound, particles, fonts, Collada,
   collision.
2. The scene loads what it needs.
3. `StampResource2()` copies the lists again.
4. `ReleaseNewResource()`:
   - for each manager, the entries in the second copy past the first copy's count are "new";
   - these are removed from the managers, so they are deleted;
   - "If you want to keep some new data, remove it from m_Stamp2Data."

Only what that scene added is released. What was there before stays.

### 2.5 `cSmartObject`: reference counting

`Core/Common/SmartPointer.h`

- `AddRef(user)` and `Release(user)` count users. The object deletes itself at zero.
- Debug builds keep the list of users.

### 2.6 What to keep and what to change

| Keep | Change |
|---|---|
| A type name per class, readable from a base pointer | `wchar_t` names → UTF-8 `std::string` everywhere; `dynamic_cast` on every look-up |
| Data files describe objects | XML (TinyXML) → JSON (nlohmann), the format every TOMS tool already writes |
| One loaded copy, many users; templates plus clones | Raw pointers with manual `AddRef`/`Release` → `std::shared_ptr` / `std::weak_ptr` |
| The factory: the data names the class, and the class makes itself | A chain of `GetMe` calls tried one by one → a map from type name to creator (one look-up) |
| A sprite = atlas file + sprite name | A manager per kind, indexed by an enum → one cache keyed by (type, path) |
| Release what a scope loaded (the stamp) | Three snapshots and count arithmetic → each resource remembers the stamp that loaded it |
| A list of what was never released | (TOMS already has it: `ObjectRegistry::DumpLeaks`) |

## 3. What TOMS has today

### 3.1 The base classes are already there

[src/core/engine/object.h](../../src/core/engine/object.h) ports `NamedTypedObject`:
- `Trackable` holds the type, name, unique ID and the live-object registry.
- `Object` adds `shared_ptr` and `As<T>()`.
- `TOMS_OBJECT(T)` gives `StaticType()` and `Type()`.

[node.h](../../src/core/engine/node.h) and [scene.h](../../src/core/engine/scene.h) give the scene tree: `Node`, then
`GameObject` with `Render(IRenderer*)`, then `SpriteNode` and `TextNode`.

**What's missing:** a resource type, a cache, a factory and stamps.

### 3.2 How assets are loaded now

| Asset | Where | How it is kept |
|---|---|---|
| Game sprite atlas | `Game::spriteAtlas_`, `BgfxRenderer::loadSpriteAtlas` | One, owned by `Game`; the texture is lent to the UI ([shared_textures.h](../../src/engine/src/shared_textures.h)) |
| Other atlases (clips, particles) | `Game::loadPreviewAtlases` ([game_scene_draw.cpp](../../src/core/game/core/game_scene_draw.cpp)) | **A new copy per caller**: `idleAtlases_`, `doorAtlases_`, `objAnims_[file]->atlases`, `previewAtlases_`, `previewFxAtlases_`, each with its own `ren->loadTexture` pages |
| `.anim` files | `idleAnim_`, `doorAnim_`, `objAnims_` (by file), `previewAnim_` | One member per use |
| `.prt` particle files | `previewFxFile_` | Preview only; the atlases go through `loadPreviewAtlases` again |
| glTF models | `gltf::loadModel` and `GltfRenderer::upload` ([gltf_renderer.h](../../src/engine/src/gltf_renderer.h)) | Whoever calls them owns the `Model` and `GpuModel` |
| Object looks | `Entity::look`, `Game::typeLook` / `lookOf` | JSON; **merged again on every draw** (`lookOf` builds a new `json` per entity per frame) |
| Editors | `GameTextures` ([GameCanvasView.h](../../tools/studio_common/GameCanvasView.h)), `StageCanvas::m_animTex`, `SpriteImageCache` | A copy per canvas and per `.anim` file |

### 3.3 The problems this causes

1. **The same atlas several times in memory and on the GPU.** If the idle clips, the door clips and an object's own
   clip all use `fx.atlas`, its pages are uploaded three times.
2. **Who frees what is spread out.** Each `std::vector<uint16_t> …Textures_` must be released by hand before it is
   cleared. One missed path leaks GPU memory.
3. **JSON in the frame.** Merging a look, finding its fields and parsing `"file#clip"` happen per entity per frame.
   In the stage editor the same pattern cost about 1 s per refresh in Debug builds (fixed in the editor on
   2026-10-17).
4. **No scope.** Nothing frees a floor's assets when the player leaves it. Today that only works because the game is
   small.
5. **Each new drawable is new code in `Game`.** Adding particles to the map, or a 3D model in a menu, means new
   members, new load code and new release code.

## 4. The design

Everything below lives in `src/core/engine/` (namespace `toms::res`). It has no Qt and no bgfx in its interfaces,
so the game, the web build, Android and the Qt editors all use it.

### 4.1 Resource keys

A resource is found by **(type, normalized path)**.

- The path is normalized as `sharedTextureKey` does it: `/` separators, `..` folded, lower case. Windows and the
  VFS then agree on the spelling.
- A part of a file (a sprite in an atlas, a clip in an `.anim` file, an effect in a `.prt` file) is **not** a
  resource. It is a *reference into* one: the resource plus a name.

### 4.2 `Resource`: the base of every loaded thing

```cpp
// resource.h
class Resource : public Object {                 // NamedTypedObject's role: type, name (= the path), unique ID
    TOMS_OBJECT(Resource)
public:
    const std::string& path() const;             // the normalized key
    int stamp() const;                           // the scope that loaded it (4.5)
    int version() const;                         // +1 when it is reloaded (4.6)
    int contentVersion() const;                  // version() + what it depends on (an atlas's pages)
    size_t bytes() const;                        // CPU + GPU size, for the memory report
    enum class State { Ready, Failed } state() const;
    const std::string& error() const;            // why it failed (logged once, kept, never retried every frame)
protected:
    virtual bool load(ResourceManager& rm, std::string& error) = 0;   // the class reads itself (as GetMe made itself)
};
```

These are the real classes. Each is a small wrapper around types the engine already has:

| Class | Holds | Depends on |
|---|---|---|
| `TextureRes` | A GPU texture handle (`IRenderer::loadTexture`), its size; frees it in its destructor | — |
| `AtlasRes` | `toms::AtlasFile` + `shared_ptr<TextureRes>` per page; `find(name)` | `TextureRes` (its pages) |
| `AnimRes` | `toms::anim::AnimFile` + an `AtlasSet` built from its atlases | `AtlasRes` |
| `ParticleRes` | `toms::fx::ParticleFile` + its `AtlasSet` | `AtlasRes` |
| `ModelRes` | `gltf::Model` + the `GpuModel` (and baked crowd textures) | — |
| `FontRes` | A `BitmapFont` and its page textures | `TextureRes` |
| `SoundRes` | Decoded audio, or a stream description | — |
| `JsonRes` | A parsed data file (`enemies.json`, a stage file) | — |

**Dependencies are `shared_ptr`s.** An `AtlasRes` keeps its page textures alive. An `AnimRes` keeps its atlases
alive. Releasing the clip file releases the atlases only if nothing else uses them. The game's own `game.atlas` is
one `AtlasRes` like any other. The "lend the texture to the UI" registry becomes "the UI asks the manager for the
same `TextureRes`".

### 4.3 `ResourceManager`: one cache by path

```cpp
class ResourceManager {
public:
    explicit ResourceManager(IRenderer* ren);    // nullptr: CPU only (tests, tools that draw with QPainter)
    template <class T> std::shared_ptr<T> get(const std::string& path);   // cached, or loaded now; never null
    template <class T> std::shared_ptr<T> find(const std::string& path) const;   // cached only
    template <class T> std::shared_ptr<T> provide(const std::string& path, std::function<void(T&)> update);   // made by its owner
    void setPathMapper(std::function<std::string(const std::string&)> mapper);   // e.g. every game.atlas -> @sprite-atlas

    void setBudget(size_t bytes);                // 4.4: unused resources kept until they pass this
    size_t releaseUnused();

    int  pushStamp(const std::string& label);    // 4.5
    void popStamp(int stamp);
    void keep(const std::shared_ptr<Resource>& r);   // survive the current stamp ("remove it from m_Stamp2Data")

    int  checkFiles();                           // 4.6: reload what changed on disk
    bool refresh(const std::shared_ptr<Resource>& r);
    void detachRenderer();                       // the renderer goes first: free every texture now
    std::vector<Info> report() const;            // path, type, bytes, users, stamp: the leak / memory list
};
```

- **The cache** is `std::unordered_map<Key, Entry>`. An entry holds the `shared_ptr<Resource>` (the cache's own
  reference), the stamp that loaded it, when it was last asked for, and the file's time and size. The key is the type
  plus the path made absolute and folded, so `media/atlas/fx.atlas` and `D:/…/assets/media/atlas/fx.atlas` are the
  same atlas.
- **`get<T>`** returns the same pointer every time for the same path. On the first call it reads the file through
  `vfs.h`, so desktop, web and Android all work, and builds `T`. A failed load is cached as `Failed` with its error,
  so it is reported once, not every frame.
- **Each class loads itself** (`Resource::load`), as each FM79979 class made itself in `GetMe`. A reload calls the
  same function on the same object. The atlas's `load` calls `get<TextureRes>` for each page, so pages are shared too.
- **Threads:** `get` runs on the main thread, because GPU uploads must. Later, `prefetch(paths)` can read and
  decode files on `job_system.h` workers and upload them on the main thread ([02_THREADS.md](02_THREADS.md)).
- One manager per process. The game owns one; each editor app owns one, shared by all its canvases. It is passed by
  reference, never as a global, so tests can make their own.

### 4.3a The manifest: one list of what the game needs, one way to ask (2026-10-17)

[assets/media/resources.json](../../assets/media/resources.json) lists the files the game needs, each with a key:

```json
{ "resources": {
    "atlas.fx":          { "type": "atlas", "path": "atlas/fx.atlas", "preload": true },
    "anim.monster_idle": { "type": "anim",  "path": "anim/monster_idle.anim", "preload": true },
    "anim.door_open":    { "type": "anim",  "path": "anim/door_open.anim", "preload": true } } }
```

**Startup:** the game sets the root folder (`setRoot`: `assets/media`), reads the manifest (`loadManifest`), and loads
every `preload` entry (`preload`) once the sprite atlas is up.

**One way to ask for anything:** `res->get<T>(name)`, where `name` is either:
- a **manifest key** (`"anim.door_open"`), or
- a **path under the root** (`"anim/door_open.anim"`, as data files write it). A key and its path give the same object.

Not loaded yet: loaded now. Code no longer has a load function per feature (`loadIdleAnims` and `loadDoorAnims` are
gone; the map's objects ask for `"anim.monster_idle"` and `"anim.door_open"`).

**A missing or broken file:**
- `get` returns a Failed resource, never null;
- the missing handler (`setOnMissing`) is told once per file;
- the game logs it, and once the map is on screen shows **"Missing file: <key or path>"** ("找不到檔案：…", text key
  `res.missing`);
- the game goes on without it (a monster without its idle clip stands still).

A key asked for as the wrong type (an atlas as an anim) is reported the same way.

**Checks:** `unit.resource_test` loads the shipped manifest and fails if any entry is missing, does not load, or has an
unknown type. A change to `resources.json` selects that test.

### 4.3b Every file by key: no paths in code (2026-10-24)

The owner (2026-10-24): too many file names were written in the code. Now **code names a key, never a path**, and
moving a file only changes its line in `resources.json`.

**1. Every file the game reads is in the manifest**, not only what the cache loads. Two more types name files that
something else reads:

| Type | What it is | Read by |
|---|---|---|
| `texture` `atlas` `anim` `particle` `font` `json` | loaded by the cache | `res->get<T>(key)` |
| `file` | a file with its own reader: the RmlUi screens (`ui.hud` …), data files with their own loaders | `pathOf(key)` |
| `folder` | a folder read file by file: `folder.events`, `folder.npcs`, `folder.floors`, `folder.stages`, `folder.sfx` | `pathOf(key)` |

The game data in `assets/data/` is named from `assets/media` as `../data/...` (`data.items`, `data.battle`,
`data.weapon_play`, …). `"optional": true` marks a file that may not be there yet (`data.event_kinds`: the event editor
writes `events/kinds.json` the first time its Kinds tab is used).

**2. The keys are a generated header, so a typo does not compile.** `python tools/make_res_keys.py` writes
[src/core/game/core/res_keys.h](../../src/core/game/core/res_keys.h) from the manifest:

```cpp
namespace toms::key     { inline constexpr const char kDataItems[] = "data.items"; ... }
namespace toms::pattern { inline constexpr const char kStage[] = "stage"; ... }

res_->get<toms::res::AnimRes>(toms::key::kAnimDoorOpen);     // a file the cache loads
toms::loadWeaponPlay(pathOf(toms::key::kDataWeaponPlay));    // a file with its own loader
pathFor(toms::pattern::kStage, "stage01");                   // one file of a family
```

`unit.res_keys_test` fails when the header and the manifest differ (run the script), when a key's file or folder is
not there, or when a pattern does not give a known file. `python tools/make_res_keys.py --check` says the same.

**3. Families of files are patterns**, one `{id}` each:

```json
"patterns": {
  "stage":       "../data/stages/{id}.json",
  "floor_stage": "../data/story/floors/{id}.stage.json",
  "dialogue":    "../data/dialogue/{id}.json",
  "chapter":     "../data/story/chapters/{id}.json",
  "style_atlas": "styles/{id}/atlas/game.atlas"
}
```

`ResourceManager::pathFor(pattern, id)` (the game: `Game::pathFor`) returns the path; an unknown pattern is told to the
missing handler and gives "".

**4. Data names the other files.** Only the entry files have keys. A file that a data file can name stays in the data,
not in code: a weapon's clips (`items.json` `battleAnim`), its particles (`weapon_play.json` `fx`), a floor's map
(the floor table), an object's look (`map_objects.h`).

**What stays a path:**
- `game_session.cpp` checks that `atlas/`, `data/stages/` and `ui/hud.rml` exist before anything else, so it can say
  "the game data was not found". This runs before the manifest is read.
- The save folder is per platform, not an asset: `toms::defaultSaveDir()`, the slots (`save_slots.cpp`), and
  `toms::settingsPath()` (one place for `settings.json`).
- Test scenarios and command-line options (`--stage=`, `--fx=`) take paths on purpose.

**Adding a file:** add its line to `resources.json` (in a `"resources"` key, or a pattern for a family), run
`python tools/make_res_keys.py`, and use `toms::key::k…` in the code.

### 4.4 Holding a resource: AddRef and Release happen on their own

`cSmartObject` needed `AddRef(user)` and `Release(user)` by hand; one missed `Release` leaked, one extra deleted an
object still in use. Here the count is `std::shared_ptr`'s, and the language does both calls:

| FM79979 | TOMS |
|---|---|
| `p->AddRef(this)` | copy the pointer: `auto mine = atlas;` (or keep it as a member) |
| `p->Release(this)` | let it go: the member is destroyed, the local leaves its scope, or `mine.reset()` |
| `GetReferenceCount()` | `use_count()`; `ResourceManager::report()` lists each resource's holders |
| debug list of users | `report()` + `ObjectRegistry::DumpLeaks` at exit ("live objects = 0, no leaks") |

- **Nothing to call, nothing to forget.** A class that keeps a `shared_ptr` member holds the resource for exactly as
  long as it lives, including when it is copied or destroyed by an exception.
- **The manager makes every resource** (`get<T>` uses `std::make_shared`), so there is never a raw `new` to pair with
  a `delete`. A resource can't be deleted while anything holds it.
- **Dependencies hold each other the same way:** an atlas holds its page textures, a clip file its atlases.
- **Textures and the renderer:** a `TextureRes` frees its GPU texture in its destructor through a shared `GpuLink`.
  When the renderer goes first, `detachRenderer()` frees every texture at once and clears the link, so a texture
  freed later never calls a renderer that is gone.
- Code that only looks something up and does not keep it uses `find<T>` (no load).

**Why `shared_ptr` and not an intrusive count?** `Object` already uses `shared_ptr` (with `enable_shared_from_this`
and `As<T>()`), the standard type works in every container and with `weak_ptr`, and the resource layer is not hot
enough for the count's cost to matter: pointers are copied when things load, not every frame.

**Memory: a budget, not a release per floor (owner's question 2).** The whole game decodes to about 2 MB of images
today, so freeing a floor's art when the player leaves it would save nothing and cost a reload when they come back.
Instead:
- a resource nobody holds **stays cached** (the floor you just left is still loaded);
- only when the unused resources together pass the **budget** (`setBudget`, 64 MB by default) are the least recently
  asked for freed, until they fit again. A web or phone build can set a smaller budget;
- `releaseUnused()` frees everything unused at once (for a low-memory warning, or after an art style change);
- stamps (4.5) remain for things that clearly come and go, such as a preview or an editor document.

With today's assets the budget is never reached, so nothing is ever reloaded. When the game grows (more art styles,
3D models), the budget starts working with no code change.

### 4.5 Stamps: releasing what a scope loaded

This is `cResourceStamp`, made automatic.

```cpp
int s = res.pushStamp("floor F03");   // every resource first loaded from now on remembers s
loadFloor("F03");                     // makes its objects: they get<> what they need
...
res.popStamp(s);                      // the player left F03: what F03 loaded is dropped from the cache,
                                      // unless something still holds it or keep() was called
```

- Each entry remembers the stamp that **first** loaded it. Something loaded before the stamp (the title screen's UI
  atlas, `game.atlas`) belongs to an older stamp and is never released by this one. That matches "stamp 1 = old,
  stamp 2 = old + new, release only new".
- `popStamp` drops the cache's reference to each resource of that stamp:
  - if nobody else holds it, it is freed now;
  - if something still does (an object carried to the next floor), it is moved to the enclosing stamp.
- `ResourceStamp` is a small RAII type that pushes in its constructor and pops in its destructor, for loading
  screens and editor previews.
- **Where to use it:** previews, editor documents, a loading screen: things that are clearly over at one moment.
  Floors and battles don't use stamps; the budget (4.4) covers them.
- In debug builds, `popStamp` logs what it freed and what it kept, and why.

### 4.6 The file version: reloading what changed

There is a second meaning of "stamp", used by tools: the file's own time and size.

- `checkFiles()` compares each cached file's modification time and size with what was read.
  - The game runs it once a second when hot reload is on: always in `ui_editor` and `battle_editor` (the game in a Qt window), by
    default in debug builds on desktop, and with `--hot-reload` in a release build. A shipped game never looks
    (owner's decision 3).
  - On a change, the resource is reloaded **in place**: the same object, new contents. Its `version()` goes up.
- Users that keep derived data (a clip player's pose cache, a sprite's region pointer) compare the version they saw
  with `version()` and look it up again.
  - This replaces `lentTexturesVersion()`, `GameTextures::cached`'s `cacheKey`, and the editors' own watchers.
- A reload that fails keeps the old contents and reports the error, so a half-saved file never blanks the screen.

### 4.7 Render objects from JSON: the `GetMe` factory

**The JSON** says what to draw. `"type"` picks the class; the other fields are that class's. (The paths are examples.)

```json
{ "type": "sprite",   "atlas": "media/atlas/game.atlas", "name": "slime", "tint": [1, 0.8, 0.8, 1] }
{ "type": "clip",     "anim": "media/anim/monster_idle.anim", "clip": "idle_slime", "loop": true }
{ "type": "particle", "file": "media/fx/fx.prt", "effect": "spark", "loop": true }
{ "type": "model",    "file": "media/models/VirtualCity.glb", "scale": 0.5 }
{ "type": "text",     "font": "media/fonts/score.fnt", "text": "1200" }
{ "type": "group",    "children": [ { "type": "sprite", "…": "…" }, { "type": "particle", "…": "…" } ] }
```

- `"atlas"` + `"name"` is FM79979's `PI` + `PIUnit`. The atlas is the owner; the name is the sprite in it.
- A short form stays valid: `"name": "fx:spark"` uses the atlas id, as `.anim` sprite keys already do
  (`parseSpriteRef`).
- The stage editor's existing `look` (`{"sprite", "anim": "file#clip", "tint"}`) is read by a small adapter into
  `clip` or `sprite`. Saved stages don't change.

**The base class** is a `GameObject`, so it sits in the scene tree, has a transform, can be hidden, and draws its
children:

```cpp
class RenderObject : public GameObject {
    TOMS_OBJECT(RenderObject)
public:
    virtual void update(float dt) {}                       // clips and particles advance
    virtual std::shared_ptr<RenderObject> clone() const = 0;   // the instance of a template (FM79979 Clone)
    virtual nlohmann::json toJson() const = 0;             // writes itself back (FM79979 ToTiXmlElement)
};
class SpriteObject   : public RenderObject { std::shared_ptr<res::AtlasRes> atlas; const AtlasRegion* region; float tint[4]; … };
class ClipObject     : public RenderObject { std::shared_ptr<res::AnimRes> anim; const anim::Clip* clip; float time, phase; … };
class ParticleObject : public RenderObject { std::shared_ptr<res::ParticleRes> file; fx::EffectInstance fx; … };
class ModelObject    : public RenderObject { std::shared_ptr<res::ModelRes> model; gltf::Pose pose; … };
class TextObject     : public RenderObject { std::shared_ptr<res::FontRes> font; std::string text; … };
class GroupObject    : public RenderObject { /* children only */ };
```

**The factory** is `cCommonRegisterManager` with a map instead of a chain. What changes, and why it is better than
`GetMe`:

| `GetMe` | `RenderObjectFactory` |
|---|---|
| Every class's `GetMe` is tried in turn until one says "mine": the cost grows with the number of classes | One map look-up by `"type"` |
| Each `GetMe` compares the tag itself, so a typo in one class silently never matches | The type name is written once (`kTypeName`); `add<T>()` fails to compile if a class has no `kTypeName` or `fromJson` |
| Returns `nullptr` with no reason | Returns the reason and where in the tree it is (`children[1]: no sprite 'x' in media/atlas/fx.atlas`); `makeOrMissing` draws a red box instead |
| A raw `new`, freed by whoever remembers | A `shared_ptr` |
| Clone by hand per class | `clone()` on every class, plus `instance()`: one template per description, a cheap clone per use |

Registration is an explicit list (`registerBuiltinRenderObjects`), not self-registering static objects. A static
registrar in a static library is dropped by the linker when nothing else references its file, and the class then
quietly disappears from the factory.

```cpp
using MakeFn = std::function<std::shared_ptr<RenderObject>(const nlohmann::json&, res::ResourceManager&, std::string* error)>;

class RenderObjectFactory {
public:
    void add(const std::string& type, MakeFn fn);          // REGISTER_CLONE_FUNCTION_TO_MANGER
    std::shared_ptr<RenderObject> make(const nlohmann::json& j, res::ResourceManager& res, std::string* error = nullptr) const;
};

// each class, as GET_ME_WITH_BASE_TYPE:
std::shared_ptr<RenderObject> SpriteObject::fromJson(const nlohmann::json& j, res::ResourceManager& res, std::string* error) {
    auto atlas = res.get<res::AtlasRes>(j.value("atlas", kGameAtlas));
    const AtlasRegion* r = atlas->find(j.value("name", ""));
    if (!r) { if (error) *error = "no sprite '" + j.value("name", "") + "' in " + atlas->path(); return nullptr; }
    auto o = std::make_shared<SpriteObject>(atlas, r);
    readTint(j, o->tint);
    return o;
}

// one place lists the built-in ones (CommonRegisterManager's AddCloneRegisterFunction):
void registerRenderObjects(RenderObjectFactory& f) {
    f.add("sprite", &SpriteObject::fromJson);
    f.add("clip", &ClipObject::fromJson);
    f.add("particle", &ParticleObject::fromJson);
    f.add("model", &ModelObject::fromJson);
    f.add("text", &TextObject::fromJson);
    f.add("group", &GroupObject::fromJson);
}
```

- **An unknown type, or a missing file or sprite,** gives `nullptr` and an error text. The caller logs it and draws
  a visible "missing" marker, so a mistake in data is seen, never a crash. FM79979 did the same with
  `"not registered element"`.
- **Templates and clones:** a JSON description used by many cells (the type's look of `monster:slime`) is made
  **once** into a template. Each cell gets `clone()`, which copies a few pointers and numbers, with no JSON and no
  look-ups.
- The game can add its own types (`f.add("hp_bar", …)`) without touching the engine.

### 4.8 Game objects from JSON

Game objects are made the same way, one level up. Their data already exists:

```
stage file  "legend": {"m": "monster:slime"}, grid cell 'm' at (4, 7),
            "objects": {"4,7": {"look": {...}, "events": [...]}}
type file   enemies.json  "slime": {"hp": 35, "atk": 18, "def": 1, "look": {...}}
```

1. **At floor load**, for each entity: kind and id from the legend (`monster` / `slime`), the type record from its
   file, and the object's own settings from `objects`.
2. **The game object factory**, keyed by kind (`monster`, `item`, `npc`, `door`, `key`, `stairs`, `event`), calls
   that class's `fromJson(record, own, ctx)`:
   - `MonsterObject` reads hp, atk, def and footprint;
   - `ItemObject` reads its effect;
   - `NpcObject` reads its dialogue file;
   - each one stores its rules data.
3. **Its look** is the type's look with the object's own look over it, merged once **here**, not every frame. It
   goes through the render factory (4.7), and the render object is added as the game object's child. Cells that use
   only the type's look clone that type's template.
4. **The frame** walks the tree: `update(dt)`, then `RenderTree(ren)`. `Game` no longer calls `lookOf`,
   `typeSprite` or `drawObjectAnim` per entity, and `objAnims_`, `idleAtlases_` and `doorAtlases_` go away.
   Idle clips and door clips are `clip` render objects.
5. **Saving** is unchanged. Game state (consumed, opened, HP left) stays in the save data (`EntityStatus`), not in
   the objects. Objects are rebuilt from data plus save state, so loading a save makes the same objects.

The stage editor uses the same factories through the same core library. What it shows is then the game's drawing,
not a copy of it (today `StageCanvas::paintGameScene` repeats `Game`'s look rules).

### 4.9 Particles, glTF and editors under the same concept

- **Particles:** a `.prt` file is a `ParticleRes`. Its atlases are `AtlasRes` from the cache, the same ones the
  clips use. An effect on the map is a `ParticleObject`. The preview (`playPreviewFx`) is a `ParticleObject` in a
  preview stamp.
- **glTF:** a `.glb` / `.gltf` file is a `ModelRes`: the parsed model and its GPU buffers and textures, uploaded
  once. Each `ModelObject` has its own `Pose` (or a crowd instance), so a hundred knights share one model. Baked
  animation textures (`uploadAnimTexture`) belong to the `ModelRes`.
- **Editors** (`stage_event_editor`, `anim_editor`, `particle_editor`, `gltf_viewer`, atlas tool):
  - each app has one `ResourceManager` on its bgfx renderer;
  - `GameTextures` becomes a thin view over it;
  - the QPainter path (`SpriteImageCache`) is keyed by the `AtlasRes` it cuts from;
  - `checkFiles()` replaces the per-tool reload code, so an atlas rebuilt in the atlas tool shows up in every open
    editor.

### 4.10 Game objects: a class per kind, drawn by status (owner's design, 2026-10-17)

Today every object on the map is the same struct (`Entity`: a position, a `kind` string, a look). What it does is
decided by string checks on `kind` inside `Game`:
- 6 places in `game_scene_draw.cpp` decide how it draws;
- 11 in `game_input.cpp` decide what touching it does;
- 4 in `game_assets.cpp` decide what happens at floor load.

Its state is spread over `Entity::consumed`, `Game::entityStatus_` and `Game::mapFx_`. Instead:

```
GameBaseObject : Node                 (in the floor's tree: the hierarchy panel shows it)
 ├─ cell, footprint, kind, id
 ├─ status            "idle", "hurt", "opening", "opened", "defeated", ...
 ├─ EventList         its linked events + what it listens to on the EventBus
 ├─ RenderSystem      status -> render object (4.7), made once from JSON; switches when the status changes
 ├─ update(dt)        the current render object advances (a clip plays); a clip that ends can move to the next status
 ├─ render(ren)       the current render object at its cell: a sprite only needs its place, a clip has its file + clip
 └─ virtual onTouch(player)
     MonsterObject (battle; hurt -> defeated), ItemObject (collected), DoorObject (key; opening -> opened),
     GateObject, StairsObject, NpcObject (talk), EventObject (the story plate), PlayerObject
```

- **Looks per status** are a `looks` table in the type's record. The old `sprite` / `look` fields still work and are
  read as the `idle` look:
  ```json
  "slime": { "hp": 35, "atk": 18, "def": 1,
    "looks": { "idle":     { "type": "clip", "anim": "anim/monster_idle.anim", "clip": "idle_slime", "loop": true },
               "hurt":     { "type": "clip", "anim": "anim/monster_fx.anim", "clip": "hurt", "next": "idle" },
               "defeated": null } }
  ```
  A placed object's own look (the stage editor's `objects`) overrides its type's, status by status. A status with no
  look falls back to `idle`.
- **Events:** the object owns its linked events (today read by `Game::fireLinkedEvents`) and subscribes to the
  `EventBus` (for example a door to "key used", an NPC to "story flag set"). An event changes the status; the render
  system switches the look.
- **A factory by `kind`** makes the objects, as `RenderObjectFactory` does by `"type"`.
- **Saving is unchanged:** the status is saved under the same `entityStatusKey` as today, so old saves load.
- **What `Game` keeps:** the rules shared by everyone (combat, inventory, story). An object asks for them through
  its `onTouch`, instead of `Game` asking each object what it is.

**Built (2026-10-17)** in [src/core/game/core/map_objects.h/.cpp](../../src/core/game/core/map_objects.h):
- `MapObject` and its kinds (`MonsterObject`, `ItemObject`, `EventObject`, `NpcObject`, `GateObject`,
  `DoorObject`, `StairsObject`, `PlayerObject`), and `MapFloor`, which makes them when a floor loads and keeps them
  as children of one root node;
- each object's looks per status, read once at floor load (the old fields as `idle`; door and gate `opening`; the
  data's `looks` table).

What moved into the objects:
- the object pass of `Game::draw` (the tiles are now `Game::drawTileLayer`), along with `Game::lookOf` per frame,
  `drawObjectAnim`, `drawIdleAnim` and `mapFx_`;
- the kind checks of `movePlayer`: gates (`bumpBeforeStep`), big monsters (`bumpBig`), and stepping onto a cell
  (`onTouch`);
- the NPC check of `interact` (`onInteract`);
- the door clip of `openDoor` (`DoorObject` plays `opening`).

Every game screenshot passes unchanged. The `looks` table (statuses beyond `idle` / `opening`) has no automated
test yet, since no shipped data uses it.

### 4.11 Floors drawn in batches (owner's requirement, 2026-10-17)

How a frame is drawn today: every quad goes into one list, and the renderer
([bgfx_renderer.cpp](../../src/engine/src/bgfx_renderer.cpp)) makes one draw call for each run of quads with the same
texture and blending. The F3 HUD shows the count. So the number of draw calls is the number of times the texture or
the blending changes, in drawing order. On a floor today:
- **Two textures take turns.** The game atlas (214×178) and `fx.atlas` (126×170) are separate textures. The monster
  idle clips and door clips mix both (`game:slime` with `fx:glow`, `fx:spark`, `fx:star`). Objects draw in row order,
  so every monster with a glow switches game → fx → game: about 2 extra draw calls per monster on screen.
- **Additive glows** in the clips switch the blending, which splits the batch again.
- **Everything is rebuilt each frame:** the visible tiles' quads, every object's look (a JSON merge), the sorted
  list. The CPU redoes work whose result has not changed.

The floor is built to be drawn in as few calls as possible:

1. **One texture for the floor.** The cache packs small atlases' pages into one shared page when it loads them. The
   game atlas and `fx.atlas` fit together on one 512×512 page with room to spare. An `AtlasRes` keeps its own
   regions and ids (`fx:glow` still means `fx.atlas`), but its UVs point into the shared page. The renderer's sprite
   texture becomes that page, and the UI borrows it as it does now. Then a whole floor (tiles, monsters, their
   glows, doors, the player) draws from one texture. Large atlases (3D textures, big art) keep their own pages.
2. **Additive quads grouped.** In the object pass, a clip's additive quads (glows, sparks) are drawn after all the
   normal quads of the same row instead of between them. That way a row costs at most one blend switch, not one per
   monster. Rows still sort front to back, so nothing that overlaps changes order.
3. **A static tile layer.** The floor's tile quads are built once at floor load into a buffer the renderer keeps
   (`IRenderer::createStaticBatch` / `drawStaticBatch`). They are rebuilt only when a tile changes (a door opens,
   the act's colour changes). The camera moves the layer with a transform, not by rebuilding it. A 34×31 floor is
   about 1,000 quads uploaded once, instead of the visible ~300 every frame.
4. **Objects keep their quads.** A `SpriteObject` already keeps its quad until its place, tint or atlas changes. An
   `AnimPlayer` keeps the quads of poses that did not move. The floor tree is re-sorted only when an object changes
   row (a roamer moves), not every frame. Objects outside the camera are skipped, as tiles already are.
5. **Measured, not guessed.** A screenshot test of a busy floor also checks the draw-call count (`lastDrawCalls`).
   The goal: **one call for the tiles and one for the objects**, plus one per additive run. A change that adds a
   texture switch then fails the test.

**Order of work** (step 5 of section 6):
1. The game object classes, the render system by status, and the floor tree: same pixels; the golden screenshots stay.
2. Batching: the shared page, additive grouping, the static tile layer and the draw-call test. Same pixels again;
   only the draw-call count changes.
3. The Hierarchy and Details panels, over the real tree (they were planned for `toms_editor`, removed on 2026-10-31; not built
   elsewhere yet).

## 5. How this makes the game faster and smaller

| Change | Effect |
|---|---|
| One `AtlasRes` per file; pages shared | Each page is uploaded once. Today an atlas used by idle clips, door clips and an object clip is uploaded three times |
| Looks resolved at floor load; templates plus `clone()` | No JSON merge, field search or string parse per entity per frame. This was the biggest cost in the editor's slow refresh |
| Render objects hold resolved pointers (region, clip) | A draw is a few multiplies and a `drawSprite`. Today each frame runs a name look-up for the atlas region (and a `find` for the clip) |
| Stamps for previews and editor documents, a budget for the rest (owner, §8) | Memory follows what is open; long runs don't grow |
| A failed load is cached with its error | A missing file costs one log line, not a file read every frame |
| `report()` and `DumpLeaks` | What is loaded, how big, who holds it: a memory budget per platform (web and Android matter most) |
| `prefetch` on worker threads (later) | Floor changes don't stall on file reads and PNG decoding |

## 6. Plan in steps

Each step builds on its own, keeps the game drawing the same, and comes with its tests. Status on 2026-10-24
(checked against the code): ✅ done, ⚠️ partly done, ❌ not started.

1. ✅ **Core resource layer** (§9): `resource.h/.cpp` (`Resource`, `ResourceManager`, stamps, `report()`) and
   `resource_types.h/.cpp` (`TextureRes`, `AtlasRes`, `AnimRes`, `ParticleRes`, `FontRes`, `JsonRes`). Unit tests:
   the same path gives the same pointer; a missing file is `Failed` once; an atlas's pages are shared; `popStamp`
   frees only what that stamp loaded; a reload bumps the version and keeps the object.
2. ⚠️ **The game's loading moves onto it** (§9): the clips, the effects, the previews and `game.atlas` (provided as
   `@sprite-atlas`) come from the cache. **Left:** the UI still borrows the sprite texture through
   `shared_textures.h` (`bgfx_renderer.cpp`, `rml_ui.cpp`), on purpose for now (§9).
3. ✅ **Render objects and the factory** (§4.7): `render_object.h/.cpp` with `sprite`, `clip`, `particle`, `text`,
   `group`, `missing`; the `look` adapter. Since 2026-10-24 they also have a **view** (a camera: everything draws
   through it, an effect's particles live under it), `ClipObject::setTime` (a clip on an outside clock) and solid
   sprites.
4. ✅ **The manifest** (§4.3a, 2026-10-17): `assets/media/resources.json`, every file by key, `preload`, the
   missing-file message.
5. ✅ **Every file by key** (§4.3b, 2026-10-24): the `file`, `folder` and `optional` entries, `patterns`, the
   generated `res_keys.h`, `unit.res_keys_test`.
6. ⚠️ **Game objects** (§4.10): a class per kind (`map_objects.h`: `MonsterObject`, `DoorObject`, ...), drawn by
   status. **Since 2026-10-24 every status's look is made of render objects** (`RenderLook`, §9.1), so the floor and
   the render objects are one system, and a look in the data can be any type (an effect, a group, text).
   `drawObjectAnim` and `drawIdleAnim` are gone. **Left:**
   - the old fields still build the idle look (`lookOf`, `legacyIdleLook`): the stage data should say it as a
     `"looks"` table instead;
   - the Hierarchy and Details panels (planned for `toms_editor`, removed on 2026-10-31; not built elsewhere yet) (the owner's order for this step: objects, batching, panels).
     `MapObject::details()` exists, nothing shows it yet;
   - the battle screen still draws through `CharacterRig` and its own `if`s: it moves onto the same looks with the
     battle's status map (docs/gameplay/11_BATTLE_DATA.md §4.2).
7. ❌ **Floors drawn in batches** (§4.11): the static tile layer (`createStaticBatch` / `drawStaticBatch`) and the
   rest of that section.
8. ❌ **`model`:** `ModelRes` and `ModelObject` over `GltfRenderer`; `gltf_viewer` loads through the manager.
9. ❌ **Editors:** one manager per app, `GameTextures` on top of it, `checkFiles()` instead of each tool's reload, the
   stage canvas drawing through the game's render objects. Today no tool uses `ResourceManager`.

Test selection: `src/core/engine/resource*` and `render_object*` map to the engine unit tests and the game's
screenshots in `tools/test_select.py`; `resources.json`, `res_keys.h` and `make_res_keys.py` select
`unit.resource_test` and `unit.res_keys_test`. Each step adds its rule with its tests.

## 7. Rules for code that uses it

- Never load a file directly for drawing. Ask the manager: `get<T>(path)`.
- Keep a `shared_ptr` to what you draw from. Never keep a raw region, clip or texture handle without its owner.
- Turn JSON into objects when data loads. Never read JSON in `update` or `Render`.
- Open a stamp around a preview or an editor document (something that ends at a clear moment). Floors and battles
  use no stamp: the budget covers them (the owner, §8).
- A new drawable is a `RenderObject` subclass with `fromJson`, `clone` and `toJson`, plus one `add` line. A new
  game object kind is the same at the game level.

## 8. The owner's decisions (2026-10-17)

1. **JSON field names:** `"type"` + `"atlas"` + `"name"`.
2. **Release timing:** the game is small, so not a release per floor. Unused resources stay cached until a memory
   budget is passed, then the least recently used go first (4.4). Stamps are kept for previews and editor documents.
3. **Reloading:** in the editors too. The game reloads changed files in `ui_editor` and `battle_editor`, in debug builds,
   and with `--hot-reload`. The Qt tools (stage editor, anim editor, …) move onto the manager in step 6.
4. **Order:** steps 1, 2 and 3 first.

Also from the owner: no `wchar_t` (UTF-8 strings, converted only where the OS needs it: `vfs.h` opens UTF-8 paths on
Windows through UTF-16); AddRef and Release must be automatic and safe (4.4); a better method than `GetMe` (4.7);
JSON instead of XML.

## 9. What was built (steps 1–6)

| File | What |
|---|---|
| [src/core/engine/resource.h/.cpp](../../src/core/engine/resource.h) | `Resource`, `ResourceManager` (cache by path, budget, stamps, `checkFiles`, `report`), `ResourceStamp` |
| [src/core/engine/resource_types.h/.cpp](../../src/core/engine/resource_types.h) | `TextureRes`, `AtlasRes`, `AnimRes`, `ParticleRes`, `FontRes`, `JsonRes`, and `AtlasList` (a file's atlases as an `AtlasSet`) |
| [src/core/engine/render_object.h/.cpp](../../src/core/engine/render_object.h) | `RenderObject` and `RenderObjectFactory`; `sprite`, `clip`, `particle`, `text`, `group`, `missing`; `lookToRenderJson` |
| [src/core/engine/vfs.h](../../src/core/engine/vfs.h) | `vfsOpen` / `vfsPath` / `vfsUtf8`: UTF-8 paths open on Windows (a Chinese folder name works) |
| `Game` (game.h, game_assets.cpp, game_scene_draw.cpp, game.cpp, game_input.cpp) | One `ResourceManager`. The idle clips, the door clips, object clips and both previews come from it; `loadPreviewAtlases` and its per-user atlas and texture lists are gone. The loaded sprite atlas is provided as `@sprite-atlas`, and every `.../atlas/game.atlas` maps to it, so clips follow the art style as before |
| `GameSession`, `main_sdl.cpp`, `toms_editor` (removed on 2026-10-31) | `SessionOptions::hotReload`, `--hot-reload`; on in `toms_editor` and in debug builds (today: on in `ui_editor`, `battle_editor` and debug builds) |

What changed for the player: nothing on screen (the golden screenshots pass unchanged). Under the hood:
- `fx.atlas` is loaded once, not once for the idle clips and again for the door clips;
- a missing or broken clip file is reported once;
- art, clips and effects saved while the game runs (with hot reload on) show up within a second.

Not done in step 2, on purpose:
- the UI still borrows the sprite texture through `shared_textures.h`. The game's own atlas is the renderer's
  built-in sprite texture, which the cache only describes (`provide`), so there was nothing to gain yet;
- the game's own `game.atlas` is not hot-reloaded (it goes through `loadSpriteAtlas`; an art style change reloads it).

Tests (in `toms_tests`, selected by `tools/test_select.py` for `src/core/engine/resource*` and `render_object*`):
- **`unit.resource_test`:**
  - one object per file;
  - the idle and door clips share one `fx.atlas`, and each page is uploaded once;
  - failures are cached;
  - `provide` and the path mapper;
  - the budget, including a budget of 0 freeing everything unused;
  - stamps and `keep`;
  - reload in place, including a broken reload that keeps the old contents and a changed page bumping the atlas's
    content version;
  - `detachRenderer`;
  - a UTF-8 path with Chinese letters.
- **`unit.render_object_test`:**
  - every built-in type from JSON;
  - errors (unknown type, missing sprite or file, a child's error saying where it is);
  - the red "missing" box;
  - clones and prototypes share resources;
  - `toJson` round trip;
  - a hidden group hides its children;
  - the stage editor's `look` adapter.

### 9.1 The floor on render objects (2026-10-24)

The owner chose one look system (2026-10-24): the floor's objects draw through render objects. In
`map_objects.cpp`, `RenderLook` replaced `SpriteLook`, `ClipLook` and `PlateLook`. A status's look is a list of
render objects placed in the object's rect each frame:
- a sprite: its whole frame stretched over the rect;
- anything else: its origin on the rect's bottom centre, scaled by `size` (default 32 x 32 art pixels).

A clip runs on the floor's clock: looping, the game's idle clock with each tile's own phase; once, from the start of
its status. The camera is the render objects' view, so an effect stays on its tile while the camera scrolls.
Descriptions used by many objects are made once and cloned (`RenderObjectFactory::instance`).

A `"looks"` entry in the data can now be any render object:

```json
"looks": { "idle": { "type": "group", "children": [
  { "type": "sprite", "name": "slime" },
  { "type": "particle", "file": "fx/battle.particle", "effect": "staff_release_3", "loop": true } ] } }
```

**Checked:** 9 scenes rendered with the old and the new build, compared with `image_diff` at 0 %, all
pixel-identical: F01, the objects test stage, the bell gate walk, the stair unlock, F07, F15, F30, F50, and F30
during the camera's slide. `unit.render_object_test` covers the view, `setTime` and solid sprites.
