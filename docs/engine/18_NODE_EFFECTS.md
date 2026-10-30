# 18 — Node effects, and the brawler showcase: is the anim editor enough without Spine?

The owner asked (2026-10-30) whether the anim editor can stand in for Spine. The test was to take a fighting-game figure
as reference, have ComfyUI draw a character's parts, and animate it in the anim editor: walk, run, attacks (kick, punch,
sword, bow, staff) and a KOF-style super combo. The owner also asked for a feature Spine does not have: **a node with a
custom shader that applies to that node and everything under it** (motion blur, HDR rendering).

This document records:

- the feature (§1–4);
- the showcase that uses it (§5);
- the answer: what the anim editor does that Spine does not, and what it still lacks (§6).

The reference picture was of a licensed character, so the showcase character is an **original design**. It keeps only
the reference's proportions and stance: a big head, huge forearms and fists, a wide stance (the owner's choice,
2026-10-30).

## 1. Effects: a subtree drawn as one picture, then through a shader

A node's `fx` draws **the node and its whole subtree** into an offscreen picture of their own, then draws that picture
into what is under it through a shader. Nodes outside the subtree are not touched. Effects nest: a layer's picture is
drawn into the layer around it.

| Shader | params (lengths in clip pixels) | What it does |
|---|---|---|
| `group` | – | The subtree as one picture: one alpha over what is under it (no see-through between overlapping parts) |
| `motionBlur` | dx, dy, auto, – | The picture smeared along (dx, dy). `auto` > 0 adds the node's own movement over the last 1/30 s, times `auto` |
| `glow` | intensity, radius, threshold, bloom | Colours × intensity (> 1 is HDR: the picture is RGBA16F). Light above the threshold blooms `radius` past the outline, in `color` |
| `flash` | amount | Towards `color` (0..1), with the alpha kept: a hit flash of the whole body as one silhouette |
| `outline` | width, intensity | A band of `color` around the subtree's alpha (intensity > 1 is HDR); a super-move aura |

```json
{"name": "front_shoulder", "fx": {"shader": "motionBlur"},
 "tracks": {"fx": [{"t": 0.08, "v": [0, 0, 0, 0]}, {"t": 0.12, "v": [0, 0, 1.4, 0]}, {"t": 0.26, "v": [0, 0, 0, 0]}]},
 "children": [ ... the upper arm, the elbow, the forearm, the fist ... ]}
```

- The `fx` keys animate the four params, eased like the other interpolated channels. Without keys, the rest `params` apply.
- An effect that shows nothing opens **no layer**, so it costs nothing. That covers a blur vector under half a pixel,
  a flash of amount 0, an outline of width 0, and a hidden or fully transparent node.
- Lengths scale with the placement. A clip drawn at half size blurs and glows half as far, and the blur vector turns
  and mirrors with the placement.

## 2. Trails: afterimages

A node's `trail` draws `count` copies of its subtree as it was `gap`, 2 × `gap`, … seconds earlier. They are tinted by
`color`: the nearest copy has `color.a`, and they fade to the oldest. They are drawn behind the node and outside its own
effect layer. A `trail` key track (0..1) scales them; with no keys the strength is 1. `"blend": "add"` makes them light.

```json
{"name": "body", "trail": {"count": 5, "gap": 0.035, "color": [1, 0.55, 0.15, 0.6], "blend": "add"},
 "tracks": {"trail": [{"t": 0.4, "v": 0}, {"t": 0.48, "v": 1}, {"t": 0.78, "v": 1}, {"t": 0.9, "v": 0}]}}
```

The copies are the clip evaluated at those earlier times (`evaluate`, stateless). They are right after a seek, and for
looping nodes too.

## 3. The file: version 4

`fx`, `trail` and their key tracks are written only when used. A file then says `"version": 4`; files without them stay
at version 2 or 3, so older builds still open them. Errors are reported when the file is read: an unknown shader, a
trail count over 16, a gap of 0 or less, or a blend other than normal / add.

## 4. How it is built

| Part | Where |
|---|---|
| Data, JSON, `fxParamsAt` / `trailAt`, each pose's subtree range `NodePose::first / end` | `src/core/engine/anim_clip.h/.cpp` |
| Opening and closing layers around subtrees, `nodeLayerEffect`, `appendTrail` (both `appendDraws` and `AnimPlayer::draw`) | `src/core/engine/anim_player.h/.cpp` |
| `IRenderer::beginLayer / endLayer`, `LayerEffect` (does nothing by default: a backend without layers draws straight through) | `src/core/engine/render_iface.h` |
| The layers: an RGBA16F picture per layer, a bgfx view per layer, the composite | `src/engine/src/bgfx_renderer.*`, `src/engine/shaders/fs_layer.sc` |
| Editor: the Properties dock's Effect box, the key list's Fx and Trail columns | `tools/anim/qt/AnimPropertiesDock.*`, `AnimKeyListDock.*`, `tools/anim/core/anim_keys.*` |
| Tests | `unit.anim_fx_test`, `smoke.anim_fx` (the super move, dashing in), `anim.check_brawler*` |

The renderer:

- **Pictures:** each layer is drawn into an RGBA16F frame buffer the size of the game view in device pixels, cleared
  to transparent. The pictures are premultiplied: sprites inside a layer blend their colour as on screen and accumulate
  coverage in the alpha. The picture is then drawn with (ONE, 1 − alpha).
- **Views:** each layer gets its own bgfx view (`kViewLayerFirst + n`). The view order runs them right after the clear
  (and after a 3D scene), **inner layers first**, before `kViewGame`. A layer's picture is drawn into its parent's view
  at the place `beginLayer` was called, among the quads, meshes and GPU particles there.
- **Limits:** at most 48 layers a frame; more are drawn straight through. Layers are not available in the top layer
  (`setLayer(1)`, over the UI).

Two engine changes came with the showcase:

- **Linear filtering for painted atlases.** An atlas page that says `filter: Linear` is now sampled linearly
  (`IRenderer::setTextureFilter`, set by the game's resource manager and by the editors). Every other atlas stays
  point-sampled pixel art, as before.
- **View > Held Clip** in the anim editor. It draws a weapon file's clip of the same name at the `hand` node, as the
  game's CharacterRig does, so a character's sword, staff and bow attacks can be watched while editing them.
  `anim_editor --render file.anim clip outdir t1,t2,… [held.anim]` saves frames through the game renderer
  (`TOMS_RENDER_ZOOM` zooms in after framing).

### 4.1 Performance (2026-10-30)

How a clip reaches the GPU:

- **Batching:** a run of quads and meshes with one texture and blending that has a mesh in it goes as ONE mesh
  (`DrawList::submit`; each quad becomes two triangles with its own colour, `Mesh::colors`). A run of quads only stays
  quads, which the sprite batch joins, also across clips. The renderer then joins consecutive mesh draws with the same
  texture and blending (`BgfxRenderer::drawMesh`), so a crowd of skinned characters is a few draws.
- **Skin rigs:** per skinned node and sprite, what does not change while the clip plays: the region, texture, mesh,
  UVs, the mesh at rest, its weights, each bone's pose index and inverse rest transform. A frame then only multiplies
  bones and sums weights. One cache per clip and atlas set, shared by every `AnimPlayer` playing it (a crowd holds one
  copy; it goes with the last player; a reload starts a new one). Automatic weights are also kept, by a hash of what they are made from, on
  every path (the editor's too).
- **Layers sized to their content:** a layer's view, clear and composite cover what it draws plus its effect's reach,
  not the whole screen.
- **No allocation a frame** for afterimages and the automatic motion blur (reused pose buffers), nor in `evaluate` for
  children with no draw order.

Measured with `toms_game --anim=assets/media/anim/brawler.anim#<clip> --anim-crowd=N --bench --no-vsync` (1600 × 900,
Direct3D 11, this PC):

| Crowd | Draw calls | Clips' draw (CPU) | Frame (CPU) | GPU |
|---|---|---|---|---|
| idle × 64, before | 705 | 3.1 ms | 4.7 ms | 4.2 ms |
| idle × 64, after | 129 | 2.5 ms | 3.8 ms | 3.3 ms |
| super_combo × 16, before | 354–692 | 1.4–2.8 ms | 3.9–4.1 ms | 3.8–3.9 ms |
| super_combo × 16, after | 116 | 0.5 ms | 1.2 ms | 1.2 ms |

Still per frame: skinning on the CPU, the lattice-deformed meshes (no rig: their rest shape moves), afterimages (their
skins take the full path), and the editor's stateless path (`evaluate` + `appendDraws`). 100 brawlers fit a 60 fps frame
on this PC (idle 5.9 ms, walk 3.8 ms, super combo 4.1 ms); `run` (three afterimages each, 100 blur layers of which 48
are drawn) takes 15.3 ms; 1000 run at 20–30 fps.

**Optimization options, not done yet** (the owner, 2026-10-30: kept for later), in the order they pay off:

| Option | What | When |
|---|---|---|
| Afterimages use the skin rigs | the ghost poses come in the clip's order, so the cache fits | before many characters run with trails |
| Rigs for lattice-deformed parts | keep their region and mesh lookups (the deform itself moves every frame) | idle-heavy crowds |
| Shadows in their own pass | a crowd's shadows (another atlas) between the bodies keep characters from batching together | crowds |
| **GPU skinning, static shared buffers** | each shared mesh uploaded once (rest positions, UVs, bone indices and weights); per character only its bone matrices; the CPU never touches a vertex, indices are never copied. Bones in a texture all characters share, so the cross-character batching stays | large crowds; the first step of the crowd mode |
| Crowd mode (baked animation texture) | as glTF crowds (docs/engine/08 §6): each clip baked per node per frame into a texture, one instanced draw per atlas page; per character only clip, time and place. Per-subtree effects fall back to the normal path | hundreds of characters |
| Flipbook level of detail | small or distant characters drawn as pre-rendered frames (the exporter's offscreen render into an atlas): one quad each | 1000 on screen, or weak devices |

## 5. The showcase: the brawler

| File | What |
|---|---|
| `tools/art/make_brawler_art.py` | The art: `pose` (an OpenPose skeleton), `concept` (ComfyUI), `parts` (cut into parts) |
| `tools/art/brawler_rig.json` | The joints as drawn, and each part's pivot and parent (written by `parts`) |
| `assets/media/atlas/brawler.atlas` / `.png` / `.mesh.json` | 11 parts in a 496 × 492 page, linear filtering; the arms', legs', torso's and sash's meshes |
| `tools/make_brawler_anim.py` | Writes the clips below |
| `assets/media/anim/brawler.anim` | idle, walk, run, punch, kick, hurt, victory, super_combo |
| `assets/media/anim/brawler_sword.anim`, `_staff.anim`, `_bow.anim` | The five clips of each weapon (named like `weapon_*.anim`), with the weapon at `hand` |

### 5.1 The art (ComfyUI)

1. **The pose is fixed first.** The script draws an OpenPose skeleton in a rig-friendly stance: arms held away from
   the body and legs apart, so the parts overlap as little as possible. ControlNet (union promax, openpose mode, full
   strength) makes Juggernaut XL put the joints there. The background is removed with Inspyrenet. With weaker control
   the arms hugged the body (the first four seeds); seed 55 was chosen.
2. **The parts are cut along the same bones.** Each opaque pixel goes to its nearest bone segment. The torso, head and
   pelvis are capsules, so a broad chest stays with the torso. A few rules cut the ponytail, the red sash's tail (by
   colour, in a band along it).
   - **One mesh per limb:** the upper arm and forearm are cut as one part, and so are the trousers and the boot. A first
     version had them as separate rigid parts: in the guard pose the elbows showed their cut ends, and the boots slid off
     the trouser cuffs as the shins tilted (the owner's report, 2026-10-30). The fists stay their own parts, at the
     glove's band.
   - **Joint overlap:** each part reaches 9 pixels into its **parent** only. The child is drawn over the parent, so a
     turned joint never shows a gap, and no part leaves stray pixels on another.
   - **Hidden trousers:** the trousers the sash hid are painted in (OpenCV Telea) and given to the leg, so the leg
     shows no hole when the sash swings.
   - **Clean edges:** the cut-out's soft edge kept the light background's colour, which showed as pale halos where
     parts overlap. Every pixel that is not fully solid takes the colour of the nearest solid one.
3. The parts are saved at half size: the figure is about 565 px tall, the size of an HD 2D fighter on a 1080p screen.

The picture faces the viewer with a slight three-quarter turn. A single drawn view cannot turn round (see §6).

### 5.2 The rig and the clips

- **Bones:** one group node per joint (waist, neck, ponytail tie, shoulders, elbows, wrists, hips, knees, ankles),
  each part's sprite at its joint.
- **Skinning:** each arm is one mesh bent by its shoulder, elbow and wrist; each leg (trousers and boot) one mesh bent
  by its hip, knee, ankle and toe (docs/engine/16). Elbows, knees and ankles fold smoothly. Each mesh is a child of its
  first joint, so an effect there covers the whole limb. The IK never folds an arm past about 100° (a wrist at least
  95 px from its shoulder): further, a skinned elbow crumples.
- **Lattices:** the chest breathes and the sash's tail flutters.
- **IK in the generator:** the poses are written as targets (where a foot or a wrist goes) and solved by two-bone IK
  into joint turns, so the feet stay planted.
- **Weapons:** the weapon is drawn at the fist's `hand` node, turned so it keeps the angle each pose asks for.

| Clip | What shows |
|---|---|
| idle | Breathing (torso lattice), the ponytail and sash swaying |
| walk, run | Feet planted by IK; the legs bend through their skins. run: afterimages on the body, a motion blur on the front leg |
| punch | The arm's subtree blurred by its own speed (`motionBlur`, auto); impact sparks |
| kick | The leg (trousers and boot) blurred as one subtree |
| hurt | The whole body flashes white as one silhouette (`flash` on `body`) |
| victory | The raised fist's arm glowing (HDR bloom) |
| super_combo | The screen dims; an outline aura on the whole body; a dash with additive afterimages; six blows, each limb blurred as it strikes; a flaming uppercut (a `glow` layer on the shoulder **nested in** the blur layer of the group around it, plus a flame flipbook of sprite keys); a landing blast |
| brawler_sword / staff / bow | ready / hold / idle, charge (the arm glows as it charges), attack (blurred), miss, attack_big (outline aura + afterimages); the weapon is CharacterRig's |

To regenerate: `python tools/art/make_brawler_art.py parts --pick 55`, then
`atlaspack pack Build/art_out/brawler/parts --out assets/media/atlas --name brawler --format atlas --filter Linear --padding 2 --extrude 1`,
then `python tools/make_brawler_anim.py` and `anim_editor --headless meshes assets/media/anim/brawler.anim`.

## 6. The answer: what the anim editor does that Spine does not, and what it lacks

**Beyond Spine:**

1. **Shaders by hierarchy:** blur, HDR glow, flash and outline on any subtree, nested and keyed (§1). Spine has slot
   blend modes and two-colour tint, but no per-bone post effects.
2. **Afterimages as a node setting** (§2). In Spine these are hand-made copies of the attachments.
3. **Meshes and weights made by the tools** from the sprite's alpha and the bones' rest positions (docs/engine/16).
   A new art style is re-meshed, and the motion stays.
4. **Lattice deform** that survives re-meshing. Spine's deform keys belong to one mesh.
5. **Weapons as separate files** bound at run time (CharacterRig).

**Still lacking**, found while making the showcase:

| Lack | Effect on this showcase | Possible fix |
|---|---|---|
| No IK in the editor or runtime | The poses were solved by IK in the Python generator. Done by hand in the editor, a planted foot is guessed joint by joint | Two-bone IK constraints on nodes (like Spine's IK), solved in `evaluate` |
| Keyframing in the editor is per node and per channel | 23 clips were written by a script, not by hand | Pose libraries, mirror pose, copy a pose across clips |
| A single drawn view | The art faces the viewer; a sideways punch or kick is the front art turned. The back arm is hidden behind the torso when it punches | Draw more views (side, back) and swap them with sprite keys, or draw parts for each view |
| No draw-order keys | The back arm stays behind the torso the whole time | An `order` key track |
| No root motion | The super combo's dash moves the body inside the clip and hops back at the end | A "root motion" event or node the game reads |
| Parts cut from one picture | Overlaps (the sash over the leg) needed painting in; deep bends show the cut | Generate each part on its own (ComfyUI inpainting per part) |
| Effects only in the game layer | No blur or glow on sprites drawn over the UI | Layers in `kViewTop` |
| Desktop (D3D11) checked only | Layers need RGBA16F render targets (RGBA8 fallback). Not yet checked on the web (WebGL2) or Android | Check when the owner asks for those versions |

**Verdict:** for 2D fighting-game animation with painted parts, the anim editor plus its runtime is **enough without
Spine**, and its effects go beyond it. The real gap is authoring speed: IK, pose tools and draw-order keys. The showcase
needed a script to write its clips.
