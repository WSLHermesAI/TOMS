# 34 — Mesh animation: replacing Spine (design)

This is a design for animating sprites by bending them, the way Spine does, inside TOMS's own `.anim` system.
The goals are:

- art an AI restyles can be dropped in without redoing the animation;
- art whose **shape** changes keeps its motion too;
- small files;
- motion simple enough for AI to write.

Status (2026-10-22): steps 1–4 of the plan (section 6) are **built**, with the anim editor's tools for them (section 7):
the renderer draws triangle meshes, sprites have meshes cut from their alpha, clips bend them by skinning and by a
deform lattice. Step 5 is under way: the anim tools add the meshes a clip needs to the mesh file beside its atlas, and
a skinned sprite without a mesh is reported; part 1, `tools/refresh_meshes.py`, refreshes every atlas's meshes (art
styles included) after the atlases change, and part 2, motion recipes, writes breathing, swaying or nodding keys for a
node (both built 2026-10-22). Part 3, weight painting in the editor, **waits for the owner's decision** (2026-10-22): no character is skinned
yet, and automatic weights, Show Skin (which already shades a selected bone's pull), the mesh grid and moving the bones
cover today's needs. It is done when a real skinned character bends badly. Step 6 is skipped for now.
The owner's decisions are in section 8.

## 1. Why

The AI reskin pipeline (`AIGamestyle/Doc/Reskin_Game_Art.md`) can restyle Spine art only as long as the shapes stay
the same. On 2026-10-19 the owner concluded that a new body shape needs new meshes, weights and animations made in
a Spine tool by hand. TOMS has no Spine runtime, and Spine's editor and its runtime licence are outside our tools.

Spine is two things:

- **bones**: a tree of transforms;
- **meshes**: a sprite cut into triangles whose corners follow the bones (weights) or are moved directly
  (deform keys).

TOMS's `.anim` (docs/15) already had the first: a node tree with pos / rot / scale / colour keys, easing and Bezier
curves. Steps 1–3 add the second.

## 2. What existed

| | What it is | Use |
|---|---|---|
| FM79979 `Core/GameplayUT/Render/Morphing` + `2DMesh` (~800 lines) and `TextureEditor/PI` (the editor) | A sprite cut into triangles (plain Delaunay of hand-placed points); each vertex has its own keys of **absolute** positions, linear only; the CPU interpolates every vertex every frame. Files `.pi` + `.ti` + `.mx` + `.mb` | The idea, not the code. It has four runtime bugs: the end time is never set; speed is applied twice; "invert" does nothing; a vertex with no keys collapses to (0, 0). The editor's key optimiser turns a hold into a slow drift. Its keys are tied to one mesh, so a reshaped sprite loses its animation |
| `D:\Work\morphing` | Only 2020 sample data in that format (`666`, `4545`, `222`, `999`; `222.pi` is missing) | Test input if the old format is ever imported |
| TOMS `.anim` + `tools/anim` (Qt) | Node tree, keys, easing, PoseCache, MPDI import; atlas ids are looked up at draw time, so another atlas (an art style) draws the same clip | The base this design builds on |
| TOMS renderer (bgfx) | Before step 1: four-corner quads only | Step 1 added triangle meshes |

## 3. The design in one picture

```
sprite (atlas region)  --step 2-->  mesh: triangles cut from the sprite's alpha (cut again when the art changes)
        |
        v
skinning (step 3)        -- each vertex follows up to 4 nodes of the clip's tree (its bones), weighted; the weights
        |                   are made automatically from how near the vertex is to each bone at rest (or painted)
        v
lattice deform (step 4)  -- a small grid over the sprite (default 4 x 4, adjustable per node); keys move its
        |                   points; every mesh vertex follows the grid cell it sits in. Squash, stretch, breathing.
        v
screen -> IRenderer::drawMesh (step 1)
```

Two choices make this better than Spine for AI-made art:

1. **Meshes and weights are made by the tools, not by hand.** The mesh comes from the alpha, and the weights come
   from the bones' rest positions. A new shape from the AI (another body, a bigger hat) is meshed and weighted
   again automatically, and the clip's bone keys (the motion) stay as they are.
2. **Deform keys move a lattice, not mesh vertices** (step 4). The lattice is defined over the sprite's rectangle,
   not over its triangles. A sprite re-meshed after a reskin still bends the same way, and the files stay tiny.
   Spine's deform keys, like the old `.mx`, belong to one mesh and break when it changes.

## 4. Data

### 4.1 Meshes: a file beside the atlas, as text or with a binary buffer (glTF style)

A mesh belongs to an atlas's sprite: every clip that uses the atlas shares it, so no data is repeated across `.anim`
files. It goes next to the atlas: `game.atlas` → `game.mesh.json` (+ `game.mesh.bin` when binary;
`toms::spriteMeshPath(atlasPath)`). The shipped one is `assets/media/atlas/game.mesh.json` + `game.mesh.bin`, with
`golem` (for `tests/smoke/anim_skin.anim`) and `slime` (for `tests/smoke/anim_deform.anim`). Only the anim tools write
it; the atlas tool (atlaspack, the atlas editor) never reads or writes meshes. (The old `atlaspack meshes` command is
gone: use `anim_editor --headless meshes`, below.) Like glTF, the numbers are either in the JSON or in a binary buffer
beside it:

```jsonc
{ "version": 1,
  "buffers": [{"uri": "game.mesh.bin", "byteLength": 588}],               // only with a binary buffer
  "meshes": {
    "golem@6x8": { "source": "alpha", "cells": [6, 8],                    // cut from alpha, or "hand" (kept); the grid
                   "vertices": [0,0, 8,0, ...], "triangles": [0,1,2, ...] },  // plain text, or
    "slime": { "source": "alpha", "vertexCount": 16, "vertices":  {"buffer": 0, "byteOffset": 352},   // float32 x, y
                                  "indexCount": 54,  "triangles": {"buffer": 0, "byteOffset": 480} }  // uint16
  } }
```

- **Which meshes are needed is decided by the clips.** The `.anim` names the atlases it uses (`atlases`); its skinned
  or deformed nodes name the sprite (their rest sprite and their sprite keys) and the grid. Each such sprite needs a
  mesh in the mesh file of the atlas it comes from (`meshesNeeded(file|clip, atlases)`, giving a
  `MeshNeed{key, entry, region, cols, rows}` each).
- **The grid.** A node can set `"meshGrid": [cols, rows]` (1 .. 32 each, `kMaxMeshGrid`): the grid its sprite is cut
  on. Without it the sprite is cut on 8-pixel square cells (`kMeshCell`). A file with a `meshGrid` is version 3, like
  one with a skin or a lattice. `cutMesh(need, rgba, stride)` cuts one mesh (`meshFromAlphaGrid` on the node's grid,
  `meshFromAlpha` on 8-pixel cells): every cell with a visible pixel becomes two triangles, sharing its corners with its
  neighbours, so no triangle covers an empty cell. Child sprites and rotated regions get no mesh and draw as quads.
- **Keys.** `meshKey(sprite, cols, rows)`: the sprite name, plus `"@CxR"` when the node has a `meshGrid`: `golem`
  (8-pixel cells), `golem@6x8`. So one sprite can have several meshes, one per grid, each shared by every clip that
  asks for it. At draw time `AtlasSet::findMesh(ref, node)` finds a node's mesh in the mesh file of the atlas its
  sprite comes from (`AtlasSet::meshKeyFor(ref, node)` gives its key).
- Vertices are in the sprite's original (untrimmed) pixels (x right, y down from its top-left). The UVs are not
  stored: they come from the region at draw time, so repacking or trimming the atlas does not invalidate a mesh. The
  UVs are clamped inside the region, so a mesh cut from another packing or art style never samples a neighbouring
  sprite: it just follows the silhouette it was cut from.
- **`"version"` is always the first key**, a header: the file's format version (`kSpriteMeshVersion`, now 1). A file
  without one, or with a newer one than the build reads, is an error, so an old game never misreads a new file.
- **The anim editor adds them** (section 7, step 5): when a clip file is saved it reads the atlas's mesh file again and
  merges in the meshes this file needs. Other clips' meshes stay, and so do `"hand"` meshes.
- **Without the editor:** `anim_editor --headless meshes <file.anim>... [--atlas [<id>=]<file.atlas>]... [--bin|--text]
  [--check] [--prune]` adds the clips' meshes to the mesh file of each atlas they use, cut from its pixels.
  - `--atlas` replaces each file's atlas list, e.g. an art style's copy:
    `--atlas game=assets/media/styles/dark16/atlas/game.atlas` writes `styles/dark16/atlas/game.mesh.json`, cut from
    dark16's pixels.
  - `--prune`: each mesh file gets exactly what the given clip files need. One clip cannot know what other clips use,
    so pruning is a separate step that takes the full list of clips.
  - `--default-atlas [<id>=]<file.atlas>`: the atlas list for a clip that names none (the game draws those with its
    sprite atlas).
  - `--map <from.atlas>=<to.atlas>`: swaps one atlas for another wherever a clip uses it.
  - `--check` writes nothing: exit 1 when a needed mesh is missing or stale, or (with `--prune`) when the file has
    meshes no listed clip uses.
- **After the atlases change** (`atlaspack build art_raw/atlas/game.atlasproj --all-variants`, new AI sprites, a
  restyled art style): run `python tools/refresh_meshes.py` (section 7, step 5). It refreshes every mesh file, the art
  styles' included.
- The reader takes both forms. The writer keeps the form the file has; a new file is binary; `--bin` or `--text`
  chooses. **Binary is the shipped form** (2026-10-22, the owner's choice): measured on 260 32 × 32 sprites, the text
  form took 3.7 ms to parse, JSON + `.bin` 0.8 ms (about 5× faster, release build). Text stays useful for reading a
  mesh or writing one by hand.

### 4.2 Skins (step 3): `.anim` version 3

A node gets an optional `skin` (`anim_clip.h` `Node::skinBones` / `skinWeights`):

```jsonc
{ "name": "golem", "sprite": "golem", "pivot": [0.5, 1],
  "skin": { "bones": ["hip", "chest", "head"],   // nodes of this clip, by name
            "weights": "auto" } }                  // or per vertex [[bone, weight, bone, weight], ...] (up to 4 bones)
```

- A file is written as version 3 only when a node has a skin; other files stay version 2, so older builds still
  open them.
- The bones' **rest pose** (their rest pos / rot / scale, keys ignored: Spine's setup pose) is where the mesh sits
  unbent. Each frame a vertex goes where its bones have carried it since then:
  `Σ weight × bone(now) × bone(rest)⁻¹ × vertex(rest)`. The skinned node's own keys do not move the mesh; its bones
  do (as in Spine).
- **Automatic weights:**
  - Each bone is a segment: from its origin to its first child that is also a bone. A last bone (a head, a hand)
    reaches on past its origin as far as its parent bone reaches to it.
  - A vertex's weight for a bone is 1 / (distance² + 1). The 4 largest are kept and scaled to sum to 1.
  - They are worked out when the clip is drawn, so they follow a re-meshed sprite.
- Painted weights belong to one mesh. When the mesh's vertex count changes, the clip uses automatic weights instead.
- A skinned sprite without a mesh draws as its quad.

### 4.3 Lattice (step 4): `lattice` + `tracks.deform`

```jsonc
"lattice": [4, 4],                         // points across and down: default 4 x 4, any 2 .. 16 each per node
"tracks": { "deform": [ {"t": 0, "v": {}},
                        {"t": 0.5, "v": {"5": [0, -2], "6": [0, -2]}, "ease": "sinusoidalInOut"} ] }   // only the points that move
```

- The lattice spans the sprite's **original image** (0 .. width, 0 .. height), its points evenly spaced, numbered row by
  row (`row * cols + col`). A key's value lists the points that are off their place, in the sprite's pixels; a point
  it leaves out is at 0 there. Keys are eased like the other interpolated channels (`deformAt`).
- Each mesh vertex takes the offset of the cell it lies in, bilinear (`latticeOffset`). The deform comes first: a
  skinned node's bones then carry the deformed shape.
- A deformed sprite without a mesh in its atlas's mesh file is bent on a plain grid of its own (twice the lattice).
  A skinned one still needs its mesh.
- Written as version 3 (only when a node has a lattice or deform keys).

### 4.4 Size

For one character with 10 nodes and 3 meshed parts, a 1 s idle with 5 keys:

| | Size |
|---|---|
| Node keys (bones) | ~3 KB JSON |
| The skins (`"auto"`) | a few dozen bytes per part |
| Lattice keys (step 4): 3 parts × 5 keys × ≤16 points | ~2 KB |
| Meshes (shared by every clip of the atlas): 3 × ~60 vertices | ~3 KB |
| The old `.mx` / Spine-style per-vertex deform for the same thing | 3 × 60 × 5 × 12 B ≈ 11 KB binary, more as JSON |

## 5. Runtime

- **Loading.** `AtlasRes::load` (the resource cache) reads `<atlas>.mesh.json` (and its `.bin`) beside each atlas
  (`res::readMeshFile(spriteMeshPath(path))`) into `AtlasRes::meshes()`; `AtlasList` hands them to
  `AtlasSet::Entry::meshes`. `AnimRes` has no meshes of its own: its clips find theirs through their atlases. The
  game's own sprite atlas (provided, it follows the art style) loads the mesh file beside the active style's atlas, or
  else the original art's `media/atlas/game.mesh.json`. A missing file means no meshes; a broken one is logged and its
  sprites draw as quads. A skinned sprite without a mesh still draws, as a flat quad, with one `[res]` warning line per
  sprite at load; a deformed sprite without a mesh still bends, on a plain grid. The anim editor loads each atlas's mesh
  file and adds what its clips need (section 7, step 5).
- **Drawing.**
  - `appendDraws(clip, poses, …)` (anim_player.h) makes a `DrawList` of quads and meshes in draw order: a skinned
    node with a mesh is bent, everything else is the quad `appendQuads` makes.
  - `AnimPlayer::draw` bends skinned nodes every frame and keeps its cached quads for the rest.
  - The game's clips on the map (`drawClipInRect`), the `--anim` preview and the anim editor's game-renderer view use
    `appendDraws`.
  - `appendQuads` is unchanged: skinned nodes give their unbent quad there. The editor's QPainter fallback, its
    selection outlines and the stage editor still use it.
- **`IRenderer::drawMesh(const Mesh&)`** (step 1): vertices in pixels with UVs, 16-bit indices, one tint, additive or
  normal, any texture. Meshes are drawn in submission order between the quads: the sprite batch is split at a mesh
  the way it is split at a GPU particle emitter. One dynamic vertex and index buffer per frame, the CPU path's sprite
  program, no new shader; it works on every backend and on the web. `validMesh` (render_iface.h) rejects broken
  meshes.
- **Cost.** Skinning is CPU work per vertex per frame (the golem: 25 vertices × 3 bones), and each skinned mesh is one
  draw call. That is fine for a few characters on screen; many skinned monsters at once would want the meshes
  batched (later).
- **Art styles.** An art style swaps the atlas, and with it the mesh file beside it
  (`assets/media/styles/<id>/atlas/game.mesh.json`, written by `tools/refresh_meshes.py` from the style's own pixels);
  the clip is unchanged. A style without one uses the original art's meshes:
  their UVs come from the style's region, clamped inside it, so they never sample a neighbouring sprite, but they keep
  the original silhouette (Known limits).

## 6. Plan

| Step | What | Visible result | Status |
|---|---|---|---|
| 1 | `IRenderer::drawMesh` + bgfx | `toms_game --mesh-test=golem`: the golem as a waving 8 × 8 grid beside the plain one; `smoke.mesh`, `smoke.mesh_cpu` | **built 2026-10-21** |
| 2 | Meshes cut from alpha: `sprite_mesh.h`, text or `.bin`, one mesh file beside each atlas, written by the anim tools; loaded with the atlas by the game, the resource cache and the anim editor | `assets/media/atlas/game.mesh.json` with the golem and the slime | **built 2026-10-21 – 22** |
| 3 | Skinning: `skin` in `.anim` version 3, automatic weights, `appendDraws`, the editor's **Skin** field | `tests/smoke/anim_skin.anim`: the golem bent by hip / chest / head, its feet planted; `smoke.anim_skin` | **built 2026-10-21** |
| 4 | Lattice deform: `lattice` (default 4 × 4, adjustable per node) + `tracks.deform`; a lattice mode in the anim editor's viewport and a Deform column in the key list | breathing / squash / sway on any sprite: `tests/smoke/anim_deform.anim` (`smoke.anim_deform`) | **built 2026-10-22** |
| 5 | AI hooks: part 1, the meshes remade from the rebuilt or restyled atlases (`tools/refresh_meshes.py` in the pipeline); part 2, motion recipes (docs/16 §7) for lattices and bones that the editor, a script or AI can apply; part 3, a weight view and painting in the editor; a check that every skinned sprite has a mesh | a restyled or reshaped character plays its old clips | the editor's mesh cutting, `--headless meshes`, the mesh check, part 1 and part 2 **built 2026-10-22**; part 3 **waits for the owner's decision** (when a real skinned character needs it) |
| 6 | Later: a Spine JSON → `.anim` importer; an old `.mx` importer; a binary `.anim` for release; batching skinned meshes | existing Spine art runs in TOMS without Spine | skipped for now (section 8) |

## 7. What was built

**Step 1 — meshes in the renderer**

- `src/core/engine/render_iface.h`: `MeshVertex`, `Mesh`, `validMesh()`, `IRenderer::drawMesh` (a no-op by default).
- `src/engine/src/bgfx_renderer.*`: `drawMesh` copies the mesh into this frame's buffers and places a marker among
  the quads; `uploadMeshes()` sends them once in `end()`; `drawGpuAt` draws a mesh marker with the sprite program.
- `--mesh-test=<sprite>` (`Game::playPreviewMesh` / `drawPreviewMesh`): that sprite at 4× as a waving 8 × 8 grid,
  the plain sprite beside it.

**Step 2 — sprite meshes**

- `src/core/engine/sprite_mesh.h/.cpp`: `SpriteMesh`, `SpriteMeshFile`, `parseSpriteMeshes` (text and buffers),
  `spriteMeshesToJson` (text, or JSON + binary), `meshFromAlpha`, `meshFromAlphaGrid`.
- `anim_player.*`: `meshKey`, `MeshNeed`, `meshesNeeded`, `cutMesh`, `AtlasSet::Entry::meshes`, `AtlasSet::findMesh` /
  `meshKeyFor`; `anim_clip.*`: `Node::meshGrid`, `kMaxMeshGrid`, `kMeshCell`.
- `resource_types.*`: `res::readMeshFile`; `AtlasRes::load` reads the mesh file beside the atlas (`AtlasRes::meshes()`),
  `AtlasList` hands it to the `AtlasSet` entry. `game_assets.cpp`: the game atlas's meshes (`Game::spriteMeshes_`,
  beside the style's atlas or else the original art's), provided with it.
- `tools/anim/core/anim_meshes.h/.cpp`: `addAnimMeshes` (adds every mesh a file's clips need to its atlases' mesh
  files, cut from the atlas pages' pixels; other meshes and `"hand"` meshes stay), `readMeshFile`, `writeMeshFile`,
  `headlessMeshes` (`anim_editor --headless meshes`).
- `assets/media/atlas/game.mesh.json` + `game.mesh.bin`: `golem` (for `tests/smoke/anim_skin.anim`) and `slime` (for
  `tests/smoke/anim_deform.anim`). The game's own clips (`assets/media/anim`) use no skin or lattice yet.

**Step 3 — skinning**

- `anim_clip.*`: `Node::skinBones`, `skinWeights`, the `skin` JSON, `kAnimVersion = 3`.
- `anim_player.*`: `AtlasSet::findMesh`, `DrawList`, `appendDraws`, `makeSkinnedMesh`, `autoSkinWeights`,
  `restWorlds`; `AnimPlayer::draw` bends skinned nodes.
- The anim editor:
  - `AnimDocument` loads each atlas's mesh file and adds what the clips need (step 5);
  - the game-renderer view draws with `appendDraws`;
  - **Properties → Skin** sets the bones (with undo; changing them drops painted weights). A note under it gives the
    mesh's points and triangles, the bones and whether the weights are automatic, or why the sprite draws as a quad
    (no mesh: a rotated or child region, or a sprite with no pixels; a bone name no node has);
  - **View > Show Skin** (Shift+K, remembered) draws over every skinned node (`AnimViewport::drawSkins`, from
    `anim::skinView`, the same bending as the game):
    - the mesh's triangles as thin lines;
    - its points, each coloured by the bones it follows, mixed by weight (every bone has its own colour);
    - its bones, as the segments the automatic weights measure against, where the bones have carried them, with
      their names (a name no node has: red and dashed);
    - with a bone selected, the points shade from that bone's colour (follows it fully) to dark (not at all), and the
      bone is outlined.
    It is painted over both views (the game renderer's and QPainter's). Hidden skinned nodes show nothing;
  - **Object mode (O) / Bones mode (B)** (toolbar, Edit menu, remembered): what a viewport click picks. Bones mode
    shows the skin and picks a bone's joint or segment (`AnimViewport::boneAt`), never a sprite; the gizmo then poses
    the bone (Auto-Key on) or places it in the sprite (off: its rest pose);
  - **a rig is the sprite with its bones under it**, so moving the sprite moves mesh and bones together. Properties
    warns about skin bones outside the node (`animed::bonesOutside`) and offers **Move Bones Under This Sprite**
    (`AnimDocument::parentBonesUnder`: the bone subtrees become the node's children, their rest positions and position
    keys shifted so nothing moves; refused when the node is rotated or scaled against their old parent);
  - **Mesh grid** (Properties, x and y, 1–32; shown for skinned or deformed nodes): `AnimDocument::setMeshGrid` sets the
    node's `meshGrid` in one undo step, and the mesh is cut again on that grid at once (step 5);
  - the check warns about a skin bone that is no node of the clip (`anim_check.cpp`);
  - the Chinese UI text is in `tools/studio_common/i18n/zh_TW/anim.json`.
- `tests/smoke/anim_skin.anim`: the demo clip.

**Step 4 — the deform lattice**

- `anim_clip.*`: `Deform`, `Node::lattice` / `deformKeys` / `usesMesh()`, `deformAt`, the `lattice` and `tracks.deform`
  JSON, `NodePose::time` (the node's own time, for its keys).
- `anim_player.*`: the mesh path takes deformed nodes too (a plain grid when the sprite has no mesh); `latticeOffset`;
  `latticeView` (the lattice the editor shows and drags).
- The anim editor:
  - **Lattice mode (L)** (toolbar, Edit menu, remembered with the other modes): the selected sprite's lattice is shown;
    dragging a point keys the deform at the playhead (always a key: a deform has no rest; Pixel Snap rounds it). The
    whole drag is one undo step; playback pauses for it;
  - **Properties → Lattice**, x and y (2 – 16): the lattice's size. Its keys are kept, each moved point going to the
    nearest point of the new grid;
  - the key list's **Deform** column: the moved points as text, `5: 0, -2; 6: 0, -2`, editable like the other cells.
- `tests/smoke/anim_deform.anim`: a slime squashed by its 4 × 4 lattice, the plain slime beside it.

**Tests**

| Test | What it checks |
|---|---|
| `unit.render_mesh_test` | which meshes `validMesh` accepts |
| `unit.sprite_mesh_test` | cutting from alpha; text and binary read back the same; `"version"` is the first key in both forms, and a file without one is an error; the other errors |
| `unit.anim_skin_test` | the skin in the file (version 3 only when used); at rest the mesh lies exactly on the quad; a turning bone carries the vertices near it; the automatic weights; the fallbacks to quads; `AnimPlayer` draws mesh and quads in order; `skinView` gives the drawn points, the bones' segments and why a skin cannot be shown |
| `anim.editor_selftest` (its skin part) | on `anim_skin.anim`: the Properties note, View > Show Skin drawing the bones, the view with the chest selected (`skin_view.png`, `skin_bone.png`) |
| `anim.check_skin` | `anim_editor --headless check` on the demo clip |
| `smoke.mesh`, `smoke.mesh_cpu` | screenshot against `tests/golden/mesh.png` |
| `smoke.anim_skin` | screenshot against `tests/golden/anim_skin.png` |
| `unit.anim_lattice_test` | the lattice in the file (version 3, the errors); the eased offsets; the bilinear offset in a cell; a deformed sprite drawn as a bent mesh (a plain grid without a mesh); the lattice the editor shows |
| `anim.editor_selftest` (its lattice part) | on `anim_deform.anim`: Lattice mode, dragging a point keys the deform at the playhead in one undo step, resizing the lattice remaps the key (`lattice_drag.png`) |
| `anim.check_deform`, `smoke.anim_deform` | the demo clip; screenshot against `tests/golden/anim_deform.png` |

**Step 5 (part) — meshes added by the anim tools, and checked**

- The anim editor (`AnimDocument`) loads each atlas's mesh file. As the clips change it adds the meshes this `.anim`
  needs, cut from that atlas's pixels and cached by key; every edit, undo, redo and atlas reload refreshes them. It
  draws with them. When the clip file is saved it reads the atlas's mesh file again and merges this file's meshes into
  it (`addAnimMeshes`, `writeMeshFile`): other clips' meshes stay, and so do `"hand"` meshes; the file keeps its form,
  a new one is binary.
- **Properties → Mesh grid** x / y (skinned or deformed nodes) sets the node's `meshGrid`: one undo step, and the
  mesh is cut again at once. Setting the golem to 8 × 8 adds `golem@8x8` beside `golem` (another clip may use that one).
- `anim_editor --headless meshes <file.anim>... [--atlas [<id>=]<file.atlas>]... [--bin|--text] [--check] [--prune]`
  (`headlessMeshes`): the same without the editor, for several clip files at once (section 4.1).
- `anim_player.*`: `skinnedWithoutMesh(clip, atlases)` — every skinned node's sprites that have no mesh in their
  atlas's mesh file.
- The check (`anim_check.*`, so `anim_editor --headless check` and the editor's **Problems**) reads the mesh file beside
  each atlas (`CheckAtlas::meshes`): an **error** per such sprite — "skinned, but the sprite 'golem' has no mesh 'golem'
  in <atlas>.mesh.json (there is no such file): … Save the clip in the anim editor, or run anim_editor --headless
  meshes".
- The game (`resource_types.cpp`, `AnimRes::load`): one `[res]` line per such sprite when a clip loads; the sprite is
  still drawn, as a quad.

*Part 1 — the pipeline: `tools/refresh_meshes.py`*

- Run it after the atlases change: `atlaspack build art_raw/atlas/game.atlasproj --all-variants`, new AI sprites
  (docs/27), or a restyled art style (docs/13).
- It reads every `.anim` under `assets/`, `tests/smoke/` and `docs/examples/`, and runs
  `anim_editor --headless meshes <all clips> --default-atlas game=assets/media/atlas/game.atlas --prune`: the base
  atlases' mesh files get exactly what the clips need.
- Then, for each art style `assets/media/styles/<id>/atlas/game.atlas`, it runs the same with
  `--map assets/media/atlas/game.atlas=<style atlas>`: the style's own `game.mesh.json`, cut from its own pixels.
  `assets/media/styles/dark16/atlas/game.mesh.json` + `.bin` is made this way.
- `python tools/refresh_meshes.py --check` writes nothing: exit 1 when any mesh file is out of date.
  `--anim-editor <exe>` picks the anim editor (default: the first build found).

*Part 2 — motion recipes*

- `tools/anim/core/anim_recipes.h/.cpp` (no Qt): `motionRecipes()`, `findMotionRecipe(id)`,
  `applyMotionRecipe(clip, nodePath, id, RecipeParams{start, period, strength, spriteW, spriteH}, err)`,
  `headlessRecipe(args)`. Lattice recipes (`breathe`, `sway`, `squash`, `jelly`) key a node's deform lattice; bone
  recipes (`bone_sway`, `nod`, `swing`) key the z rotation of a skinned node's bones. The list and the rules are in
  docs/16 §7.
- The editor: **Key > Motion Recipe…** for the selected node; applying is one undo step.
- Headless: `anim_editor --headless recipe <file.anim> --node <name> --recipe <id> [...]` and
  `anim_editor --headless recipe --list`.

| Test | What it checks |
|---|---|
| `anim.meshes_up_to_date` | `python tools/refresh_meshes.py --check --anim-editor <anim_editor>` (only when Python 3 is found): every atlas's mesh file, the art styles' included, has exactly what the clips need, freshly cut |
| `anim.recipes` | `tools/anim/tests/anim_recipes_test.cpp`: each recipe's keys and shape, keys outside the range kept, the clip length growing, the bone recipes from each bone's rest angle, the refusals, the written file read back (docs/16 §7) |
| `anim.recipe_headless` | `--headless recipe --list`; breathe and nod written into a copy of the skinned golem; a bone recipe on a bone without a skin refused; the result passes `--headless check` |
| `anim.editor_selftest` (its recipe part) | breathe and nod on the golem, one undo step each; swing refused on the hip |
| `anim.check_skin_meshes` | `--headless check` of `anim_skin.anim` against a copy of `game.atlas` + `.png` without its mesh file: an error; with `game.mesh.*` copied beside it: none |
| `anim.editor_selftest` (its mesh grid part) | the golem's key in the atlas's mesh file is `golem`; setting 8 × 8 is one undo step, stored in the node, and adds `golem@8x8` beside it |
| `unit.anim_skin_test` | `meshKey("golem")` / `meshKey("golem", 6, 8)`, `meshesNeeded`, `cutMesh`, `findMesh` through the atlas entry's meshes; `skinnedWithoutMesh`: the node and the sprite; none when the mesh is there; a sprite in no atlas left to the other check |

**Known limits**

- The editor shows a skinned node's sprite bent only in its game-renderer view. Its selection outline and the
  QPainter fallback (no GPU) show the unbent rest quad; Show Skin's lines and points are bent in both.
- Weights are automatic or written in the file; the editor cannot paint them (step 5, part 3, waiting for the owner's
  decision). To improve a bend: a finer mesh grid, or move the bones at rest (the automatic weights follow them).
- The mesh's points cannot be dragged by hand yet; the Mesh grid only makes it coarser or finer. Shape changes go
  through the lattice.
- The lattice is shown and dragged in the node's own frame. On a skinned node its bones are not applied there, so drag
  the lattice with the bones at rest.
- Rotated atlas regions have no mesh. TOMS's atlases do not rotate today.
- An art style gets its own mesh file from `tools/refresh_meshes.py`. A style that has none (the script not run
  since it was added) uses the original art's `game.mesh.json`: its UVs are clamped inside the sprite's region, so it
  never samples a neighbour, but it follows the original silhouette, not the style's own.

## 8. The owner's decisions (2026-10-21)

1. **Lattice size:** adjustable per node, default 4 × 4.
2. **Mesh storage:** like glTF, either plain text or a binary buffer, chosen when saving. The reader takes both.
3. **Spine importer:** skipped for now. A useful one needs skinning and deform first (Spine characters are mostly
   weighted meshes with deform keys), and would take several more days on top of them; a bones-only importer is about
   a day but would import little. Revisit after step 4.
4. **Order:** skinning before the lattice. The original anim editor must keep working: its checks and self tests
   pass, and clips without skins draw and save exactly as before (version 2).
