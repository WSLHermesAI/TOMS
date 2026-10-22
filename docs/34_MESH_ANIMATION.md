# 34 — Mesh animation: replacing Spine (design)

This is a design for animating sprites by bending them, the way Spine does, inside TOMS's own `.anim` system.
The goals are:

- art an AI restyles can be dropped in without redoing the animation;
- art whose **shape** changes keeps its motion too;
- small files;
- motion simple enough for AI to write.

Status (2026-10-22): steps 1–3 of the plan (section 6) are **built**, and the anim editor shows a skin (section 7): the renderer draws triangle meshes, sprites
have meshes cut from their alpha, and clips bend them by skinning. Steps 4–6 are still to do. The owner's decisions
are in section 8.

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

A mesh belongs to a sprite, not to a clip: every clip that uses the sprite bends the same triangles. It goes next
to the atlas, as `<atlas>.mesh.json` (`assets/media/atlas/game.mesh.json`). An art style has its own
(`styles/<id>/atlas/game.mesh.json`), cut from its own alpha. Like glTF, the numbers are either in the JSON or in a
binary buffer beside it:

```jsonc
{ "version": 1,
  "buffers": [{"uri": "game.mesh.bin", "byteLength": 85904}],             // only with a binary buffer
  "meshes": {
    "golem": { "source": "alpha", "cells": [8, 8],                        // cut from alpha, or "hand" (kept); the grid
               "vertices": [0,0, 8,0, ...], "triangles": [0,1,2, ...] },  // plain text, or
    "slime": { "source": "alpha", "vertexCount": 25, "vertices":  {"buffer": 0, "byteOffset": 0},     // float32 x, y
                                  "indexCount": 96,  "triangles": {"buffer": 0, "byteOffset": 200} }  // uint16
  } }
```

- Vertices are in the sprite's original pixels (x right, y down from its top-left). The UVs are not stored: they come
  from the region, so the same file works for any packing of the atlas.
- `atlaspack meshes <file.atlas> [--cell N] [--alpha-threshold N] [--bin] [--check]` writes it (docs/14 §5). The
  sprite is cut on a grid of `--cell`-pixel cells (default 8). Every cell with a visible pixel becomes two
  triangles, sharing its corners with its neighbours, so no triangle covers an empty cell. Child sprites and rotated
  regions get no mesh and draw as quads. A mesh marked `"hand"` is kept. A mesh with `"cells"` (cut on a grid by the
  anim editor's **Mesh grid**) is cut on that grid again.
- The reader takes both forms; the writer picks one. For TOMS's 32 × 32 sprites **plain text is the smaller**: 97 KB
  for the 260 sprites of `game.atlas`, against 40 KB of JSON plus an 86 KB `.bin`. The buffer's per-mesh offsets cost
  more than short integers do in text. The game ships text; `--bin` is for big art (characters with hundreds of
  vertices).

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

### 4.3 Lattice (step 4, to do): `lattice` + `tracks.deform`

```jsonc
"lattice": [4, 4],                         // points across and down: default 4 x 4, any 2 x 2 .. 8 x 8 per node
"tracks": { "deform": [ {"t": 0, "v": {}},
                        {"t": 0.5, "v": {"5": [0, -2], "6": [0, -2]}, "ease": "sinusoidalInOut"} ] }   // only the points that move
```

### 4.4 Size

For one character with 10 nodes and 3 meshed parts, a 1 s idle with 5 keys:

| | Size |
|---|---|
| Node keys (bones) | ~3 KB JSON |
| The skins (`"auto"`) | a few dozen bytes per part |
| Lattice keys (step 4): 3 parts × 5 keys × ≤16 points | ~2 KB |
| Meshes (shared by every clip): 3 × ~60 vertices | ~3 KB |
| The old `.mx` / Spine-style per-vertex deform for the same thing | 3 × 60 × 5 × 12 B ≈ 11 KB binary, more as JSON |

## 5. Runtime

- **Loading.** `AtlasRes` (the resource cache) and the game's own sprite atlas read `<atlas>.mesh.json` (and its
  `.bin`) beside the atlas; each `AtlasSet` entry carries its meshes. A missing file means no meshes; a broken one is
  logged and its sprites draw as quads. The anim editor reads them the same way.
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
- **Art styles.** An art style swaps the atlas and its mesh file; the clip is unchanged.

## 6. Plan

| Step | What | Visible result | Status |
|---|---|---|---|
| 1 | `IRenderer::drawMesh` + bgfx | `toms_game --mesh-test=golem`: the golem as a waving 8 × 8 grid beside the plain one; `smoke.mesh`, `smoke.mesh_cpu` | **built 2026-10-21** |
| 2 | Meshes cut from alpha: `sprite_mesh.h`, `atlaspack meshes`, text or `.bin`; loaded by the game, the resource cache and the anim editor | `game.mesh.json` for the original art and `dark16` | **built 2026-10-21** |
| 3 | Skinning: `skin` in `.anim` version 3, automatic weights, `appendDraws`, the editor's **Skin** field | `tests/smoke/anim_skin.anim`: the golem bent by hip / chest / head, its feet planted; `smoke.anim_skin` | **built 2026-10-21** |
| 4 | Lattice deform: `lattice` (default 4 × 4, adjustable per node) + `tracks.deform`; a lattice mode in the anim editor's viewport and a Deform column in the key list | breathing / squash / sway on any sprite | next |
| 5 | AI hooks: the reskin pipeline remakes the meshes with the restyled atlas; motion recipes (docs/16) for bones and lattices that a script or AI can fill in; a weight view and painting in the editor; a check that every skinned sprite has a mesh | a restyled or reshaped character plays its old clips | |
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
  `spriteMeshesToJson` (text, or JSON + binary), `meshFromAlpha`, `spriteMeshPath`.
- `atlaspack meshes` (`tools/atlas/cli/atlas_cli.cpp`).
- `resource_types.*`: `readSpriteMeshes`, `AtlasRes::meshes()`, `AtlasList` hands them to the `AtlasSet`.
  `game_assets.cpp`: the game atlas's meshes (`Game::spriteMeshes_`), provided with it.
- `assets/media/atlas/game.mesh.json`, `assets/media/styles/dark16/atlas/game.mesh.json`.

**Step 3 — skinning**

- `anim_clip.*`: `Node::skinBones`, `skinWeights`, the `skin` JSON, `kAnimVersion = 3`.
- `anim_player.*`: `AtlasSet::findMesh`, `DrawList`, `appendDraws`, `makeSkinnedMesh`, `autoSkinWeights`,
  `restWorlds`; `AnimPlayer::draw` bends skinned nodes.
- The anim editor:
  - `AnimDocument` loads each atlas's meshes;
  - the game-renderer view draws with `appendDraws`;
  - **Properties → Skin** sets the bones (with undo; changing them drops painted weights). A note under it gives the
    mesh's points and triangles, the bones and whether the weights are automatic, or why the sprite draws as a quad
    (no mesh: run `atlaspack meshes`; a rotated region; a bone name no node has);
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
  - **Mesh grid** (Properties): `AnimDocument::setMeshGrid` cuts the sprite's mesh again with `meshFromAlphaGrid` on
    N × N cells (1–32) and writes the atlas's mesh file in the form it had;
  - the check warns about a skin bone that is no node of the clip (`anim_check.cpp`);
  - the Chinese UI text is in `tools/studio_common/i18n/zh_TW/anim.json`.
- `tests/smoke/anim_skin.anim`: the demo clip.

**Tests**

| Test | What it checks |
|---|---|
| `unit.render_mesh_test` | which meshes `validMesh` accepts |
| `unit.sprite_mesh_test` | cutting from alpha; text and binary read back the same; the errors; the shipped `game.mesh.json` fits `game.atlas` |
| `unit.anim_skin_test` | the skin in the file (version 3 only when used); at rest the mesh lies exactly on the quad; a turning bone carries the vertices near it; the automatic weights; the fallbacks to quads; `AnimPlayer` draws mesh and quads in order; `skinView` gives the drawn points, the bones' segments and why a skin cannot be shown |
| `anim.editor_selftest` (its skin part) | on `anim_skin.anim`: the Properties note, View > Show Skin drawing the bones, the view with the chest selected (`skin_view.png`, `skin_bone.png`) |
| `anim.check_skin` | `anim_editor --headless check` on the demo clip |
| `smoke.mesh`, `smoke.mesh_cpu` | screenshot against `tests/golden/mesh.png` |
| `smoke.anim_skin` | screenshot against `tests/golden/anim_skin.png` |

**Known limits**

- The editor shows a skinned node's sprite bent only in its game-renderer view. Its selection outline and the
  QPainter fallback (no GPU) show the unbent rest quad; Show Skin's lines and points are bent in both.
- Weights are automatic or written in the file; the editor cannot paint them yet (step 5).
- The mesh's points cannot be dragged by hand yet; the Mesh grid only makes it coarser or finer.
- Rotated atlas regions have no mesh. TOMS's atlases do not rotate today.

## 8. The owner's decisions (2026-10-21)

1. **Lattice size:** adjustable per node, default 4 × 4.
2. **Mesh storage:** like glTF, either plain text or a binary buffer, chosen when saving. The reader takes both.
3. **Spine importer:** skipped for now. A useful one needs skinning and deform first (Spine characters are mostly
   weighted meshes with deform keys), and would take several more days on top of them; a bones-only importer is about
   a day but would import little. Revisit after step 4.
4. **Order:** skinning before the lattice. The original anim editor must keep working: its checks and self tests
   pass, and clips without skins draw and save exactly as before (version 2).
