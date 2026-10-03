# 17 — Particle effects (`.particle`)

**Status:** phases 1 and 2 are done.
- **Phase 1:** the format, the runtime `toms::fx`, the `fx` atlas, 11 example effects,
  `toms_game --fx=`, tests.
- **Phase 2:** the editor `particle_editor`; section 5 describes it.

Next: phase 2b (blend modes in the renderer) and phase 3 (effects on `.anim` nodes). Sections 1
and 2 explain the design; 3–5 describe what is built.

A new particle system for TOMS: a JSON file format, a 2D
runtime that draws from the packed sprite atlases ([14](14_ATLAS_TOOL.md)) like `.anim`
([15](15_ANIMATION.md)), and a Qt editor that shares code with the anim and atlas editors. It
replaces FM79979's particle system (`Core/GameplayUT/Render/Particle`, the WinForms
`ParticleEditor`, `.prt` / `.prtg` XML).

## 1. What the legacy system does (and what to keep)

**Runtime** (`FM79979Engine/Core/GameplayUT/Render/Particle`, ~4k lines)

- `cPrtEmitter`: one texture, a fixed pool (`MaxParticleCount`), and emission settings:
  - **Gap** (seconds between shots) × **Amount** (particles per shot) × **EmitCount**
    (number of shots; 0 = endless).
  - **Velocity** (`cPrtVelocityInitSetVelocity`): a direction vector plus a random offset.
  - **Blending**: raw GL blend enums. **Primitive**: GL quads or points.
- Behaviour comes from **policies**, kept in two ordered lists. **Init** policies run once when a
  particle is born; **Act** policies run every frame:

  | Policy | Init / Act | What it does |
  |---|---|---|
  | `cPrtLifeInitrSetLife` | init | life = min + random(range) |
  | `cPrtLifeActDyingByGameTime` | act | life -= dt. **Without it, particles never die**, which is a trap. |
  | `cPrtColorInitrSetColor` / `...SetRandomColor` | init | a colour, or random between the colour and white |
  | `cPrtColorActBlending` | act | colour ± rate·dt, clamped |
  | `cPrtColorActBlendingByLife` | act | lerp from the start colour to the target colour over life |
  | `cPrtColorActBlendingBy2Color` | act | start → colour 1 (first half of life) → colour 2 (second half) |
  | `cPrtSizeInitSetSize` | init | size, optionally × random(0..r) |
  | `cPrtSizeActBlending` | act | size ± rate·dt |
  | `cPrtRotateInitRotate` / `cPrtRotateActRotate` | init / act | an angle, then angle += speed·dt (± random) |
  | `cPrtStartPositionInitBySquareRange` | init | random point in a box |
  | `cPrtStartPositionInitByFrame` | init | attach to a 3D model frame (3D only) |
  | `cPrtVelocityActAcceleration` | act | speed up along the start direction |
  | `cPrtVelocityActDircctionChange` | act | per axis: slow down to a stop time, then a new speed |
  | `cPrtVelocityActBySatelliteAction` | act | orbit-like offset (buggy: it adds the position to itself) |
  | `cPrtTextureActDynamicTexture` | act | flipbook frames from a `.pi` over the particle's life |

- `cParticleEmitterGroup` / `cParticleEmiterWithShowPosition` (`.prtg`): a group places one or
  more emitters. Each has a position, a direction, a start and end time, loop, and optionally a
  curve **path** (`.path` file) to move along.

**Files.** Each emitter's settings are comma-separated strings packed into XML attributes, so
the meaning depends on the order of the numbers:

```xml
<Particle TPVersion="1.0">
  <TextureList Name0="Default.png" />
  <Emiter Name="Fire" Data="0.032,0,1,10000,772,772,7,0.00,-17.00,0.00,1,20.00" Texture="Default">
    <InitPolicy Type="cPrtLifeInitrSetLife" Data="0.50,1.00,1" />
    <InitPolicy Type="cPrtSizeInitSetSize" Data="70.00,100.00" />
    <ActPolicy Type="cPrtLifeActDyingByGameTime" />
    <ActPolicy Type="cPrtColorActBlendingBy2Color" Data="1,1,1,1,0,0,0,0" />
  </Emiter>
</Particle>
```

**Editor** (WinForms): one long form with a policy list, one sub-form per policy type, raw
GL blend dropdowns, and 3D camera options (perspective, rotation X). The group editor is a
separate window.

**Problems to fix**

1. **Unreadable data:** positional comma strings, and GL enum numbers (`772`, `7`).
2. **Behaviour depends on list order and on remembering policies:** a missing `DyingByGameTime`
   means immortal particles, and two colour policies fight each other.
3. **Most "act" policies are rates** (± per second), not targets. "Fade to 0 by the end of life"
   can't be stated directly, and the result changes with the life length.
4. **One loose texture per emitter, outside any atlas:** every emitter is its own draw call and
   its own texture.
5. **3D-only parts** (model frames, perspective camera, compute shader, point primitives) that
   TOMS does not use.
6. **Paths live in a separate `.path` file,** while TOMS already has a better tool for motion
   (`.anim`).

**Keep:** pooled particles; emission by rate plus shots; life / colour / size / rotation /
velocity behaviours; flipbook frames; groups of emitters with offsets and start/end times;
one-shot versus looping effects.

## 2. New design in short

- **A fixed set of modules instead of policy lists.** An emitter always has the same sections:
  Emission, Shape, Start values, Over-lifetime curves, Forces, Flipbook and Render. A section
  is used when present in the file and on when its checkbox is ticked in the editor. Nothing
  depends on order, and nothing is needed just to make particles die.
- **Ranges instead of "random" flags.** Any start value is a number or `[min, max]`.
- **Curves over normalised life (0..1) instead of rates.** They use the same keys and ease names
  as `.anim` (Tweeny easings and Bézier), so "fade out over the last 30%" is one curve that
  works for any life length.
- **Sprites from the atlases,** referenced as `"id:name"` exactly like `.anim`, using the same
  `AtlasSet`. Effects batch with everything else drawn from the same page.
- **2D only:** pixels, y down, degrees, clockwise rotation, the same conventions as `.anim`.
- **Blend by preset name or any src/dest pair** (section 3a). bgfx takes any blend factors; the
  renderer's `Quad::additive` becomes a small blend id, and batches split on it.
- **Deterministic:** each effect instance has a seed, so the editor preview, `toms_game` and the
  screenshot tests always produce the same particles.
- **Effects play with animations:** an `.anim` node can carry an effect that follows the node
  (this replaces `.prtg` paths), and an anim event can fire a one-shot burst.

## 3. File format (`.particle`, JSON)

One file holds several **effects**. An effect is a named group of **emitters** (what `.prtg` was).
The extension is `.particle`; not `.fx`, which Visual Studio treats as a shader.

```jsonc
{
  "version": 1,
  "atlases": ["../atlas/game.atlas", {"id": "fx", "path": "../atlas/fx.atlas"}],   // as .anim
  "effects": [
    {
      "name": "torch_fire",
      "duration": 0,              // seconds; 0 = endless (until stopped)
      "loop": false,              // with a duration: start over at the end (bursts fire again)
      "prewarm": 1.0,             // seconds simulated before the first frame (an already-burning fire)
      "seed": 0,                  // 0 = a new seed each play; otherwise fixed
      "emitters": [
        {
          "name": "flames",
          "offset": [0, 0],         // from the effect's origin, px
          "start": 0, "stop": 0,    // active window in effect time (stop 0 = until the effect ends)
          "space": "world",         // world: particles stay where they were born; local: they move with the effect
          "maxParticles": 120,

          "emission": {
            "rate": 40,                                   // per second while active
            "bursts": [ { "t": 0, "count": [8, 12], "repeat": 0, "interval": 0.5 } ]
          },
          "shape": { "type": "circle", "radius": 6, "arc": [0, 360], "edge": false, "outward": false },
          //  point | line {length, angle} | box {size: [w, h]} | circle {radius, arc} | ring {radius, thickness, arc}
          //  edge: only on the outline (box, circle); outward: fly away from the centre instead of
          //  "direction" (a point: every direction; spread still applies)

          "life":      [0.5, 0.9],    // seconds
          "direction": -90,           // degrees; -90 = up (y down)
          "spread":    25,            // ± degrees around direction
          "speed":     [40, 70],      // px / s
          "size":      [20, 28],      // px (width; height follows the sprite's aspect)
          "rotation":  [0, 360],      // start angle, degrees
          "spin":      [-90, 90],     // degrees / s
          "color":     [[1, 1, 1, 1], [1, 0.85, 0.7, 1]],   // random between two colours (or one)
          "sprite":    "fx:flame",

          "overLife": {               // t = normalised life 0..1; keys and eases as in .anim
            "color": [ {"t": 0,   "v": [1, 0.9, 0.5, 0]},
                       {"t": 0.15,"v": [1, 0.7, 0.2, 1]},
                       {"t": 1,   "v": [0.5, 0.1, 0, 0], "ease": "quadraticIn"} ],   // multiplies "color"
            "size":  [ {"t": 0, "v": 0.6, "ease": "quadraticOut"}, {"t": 1, "v": 1.4} ],   // × start size
            "speed": [ {"t": 0, "v": 1}, {"t": 1, "v": 0.3} ],                            // × start speed
            "spin":  [ {"t": 0, "v": 1}, {"t": 1, "v": 0} ]                               // × start spin
          },
          "forces": {
            "gravity":    [0, -60],   // px / s² (negative y = rises)
            "drag":       0.8,        // 1/s, velocity *= e^(-drag·dt)
            "radial":     [0, 0],     // px / s² away from the emitter (min..max)
            "tangential": [0, 0]      // px / s² around the emitter, clockwise (orbits; replaces "satellite")
          },
          "flipbook": {               // replaces cPrtTextureActDynamicTexture
            "frames": ["fx:flame_0", "fx:flame_1", "fx:flame_2"],
            "mode": "overLife",       // overLife: all frames across the life; fps: loop at "fps"
            "fps": 12, "randomStart": true
          },
          "render": {
            "blend": "add",           // normal | add | multiply | screen | {"src": "srcAlpha", "dst": "one"}
            "order": 0,               // between emitters of this effect (higher = in front)
            "alignToVelocity": false, // rotate each particle to face its motion (sparks, streaks)
            "oldestOnTop": false      // default: newer particles draw over older ones
          }
        }
      ]
    }
  ]
}
```

**Rules**

- Any field may be left out; the defaults give white, size 16, life 1 s, 10/s, speed 50, upward,
  no forces, `normal` blend, 100 particles at most.
- **Unknown fields are errors** (`"colour"` is reported with where it is), so a typo never does
  nothing silently.
- A start value is a number or `[min, max]`. For `color`, it is one colour or two to pick
  between.
- Curve keys are `{t, v, ease?}` with `t` from 0 to 1, as in `.anim`. Before the first key the
  value is the first key's value; after the last key it is the last key's value. Over-life
  values multiply the start values (colour per channel, size, speed, spin).
- Sprites are `"atlasId:name"` or a bare `"name"`, looked up through the same `AtlasSet` as
  `.anim`.
- `checkParticles` (shown by `--fx` in the log, and later in the editor's Problems):
  - **Errors:** unknown sprites, curve `t` outside 0..1 or twice the same, `stop` before
    `start`, `min` above `max`, no sprite, a repeating burst without an interval, duplicate
    effect names.
  - **Warnings:** rate × life above `maxParticles` (some particles are not born), a burst bigger
    than the pool, `dstAlpha` factors, `loop` without a duration.
- `multiply`, `screen` and custom pairs are stored and checked now, but draw as `normal` until
  phase 2b (only `add` has its own blend state yet).

**Effect timing.** An effect with a `duration` stops making particles at the end, and is
`finished()` when the last one dies (with `loop`, it starts over instead). Bursts fire at
emitter time `t`, then `repeat` more times every `interval` (`-1` = for as long as the emitter
is active).

**Examples:** [examples/fx_recipes.particle](examples/fx_recipes.particle) has 11 effects:
- `torch_fire` (flipbook flames + embers + smoke), `hit_sparks`, `slash_ring`, `heal`
  (local space)
- `magic_circle` (orbits), `smoke_puff`, `dust_landing`, `coin_burst` (game sprites),
  `level_up`, `snow`, `dark_aura` (multiply)

They use the **fx atlas**, `assets/media/atlas/fx.atlasproj`: white, tintable `dot`, `glow`,
`spark`, `star`, `ring`, `smoke`, `flame_0..3` and `square`, generated by
`tools/atlas/make_fx_sprites.py` and packed with `atlaspack`.

### 3a. Blending

Blending decides how a particle's colour (**src**) combines with what is already on screen
(**dst**): `result = src × srcFactor + dst × dstFactor`.

| Preset | src, dst factors | Looks like | Use for |
|---|---|---|---|
| `normal` | srcAlpha, 1 − srcAlpha | paint on top, covers what is behind | smoke, dust, debris, leaves |
| `add` | srcAlpha, one | adds light, can only brighten, overlaps glow | fire, sparks, magic, glows |
| `multiply` | dstColor, 0 (alpha → white) | darkens like a tinted glass, white = no change | shadows, scorch marks, dark smoke |
| `screen` | one, 1 − srcColor | brightens softly, never blows out to pure white | soft light, mist, heal glow |

Any other combination is `{"src": f, "dst": f}` with the factors `zero`, `one`, `srcColor`,
`invSrcColor`, `srcAlpha`, `invSrcAlpha`, `dstColor`, `invDstColor`, `dstAlpha`, `invDstAlpha`.
This also covers every legacy GL pair. The editor shows the presets in a dropdown, and "Custom…"
opens two factor dropdowns with a live preview over light and dark backgrounds.

Notes:
- Textures are straight alpha (`fs_sprite.sc`), so `multiply` / `screen` fade with alpha by the
  shader mixing the colour toward white / black first. That is a small shader uniform, not a
  new pipeline.
- `.anim` nodes get the same choice. Today they have `"blend": "add"`; the presets and custom
  pairs are added the same way, and the old values keep working.
- `dstAlpha` factors depend on the render target having alpha. The screen does not, so they
  behave like `one` / `zero`, and the editor warns.

**How an `.anim` uses an effect** (phase 3):

```jsonc
// on a node: an effect that follows the node (position, rotation, colour, visibility)
{ "name": "torch", "sprite": "torch", "effect": "fx/fire.particle#torch_fire" }
// as an event: a one-shot burst at the node's position when the time passes the key
"tracks": { "event": [ { "t": 0.28, "v": "fx:fx/hit.particle#spark_burst" } ] }
```

This replaces `.prtg` paths: to move an emitter along a curve, animate a node and attach the
effect to it.

## 4. Runtime (`src/core/engine/particle_fx.h/.cpp`, `toms::fx`)

- `ParticleFile` with `parseParticles` / `particlesToJson`, mirroring `anim_clip`.
```cpp
toms::fx::ParticleFile file;   toms::fx::parseParticles(text, file, &err);
toms::fx::EffectInstance fx;
fx.setTransform(toms::anim::placement(x, y));   // first: prewarm makes particles where it is
fx.play(file.find("torch_fire"), seed);           // seed 0 = the effect's, else a new one
// every frame:
fx.setTransform(toms::anim::placement(x, y));    // when it moves
fx.update(dtSeconds);
fx.appendQuads(atlases, camera, nullptr, quads);  // then ren->drawSprite(q) for each
if (fx.finished()) ...                            // a one-shot is done
```

- `ParticleFile` with `parseParticles` / `particlesToJson` / `checkParticles`, mirroring
  `anim_clip`. The atlases and `"id:name"` sprites go through the same `AtlasSet` as `.anim`.
- `EffectInstance`:
  - **API:** `play`, `stop(clear)`, `update`, `seek(t)` (a fresh play to time t with the same
    seed, for the editor's scrubbing), `emitting`, `finished`, `liveCount`.
  - **Storage:** one pool per emitter, structure-of-arrays, sized at `maxParticles`, with no
    allocation after `play`. Dead particles are compacted out, so the pool stays in birth order
    (draw order).
- `appendQuads`: four corners, UVs, tint, additive and texture, the same `Quad` that `.anim`
  produces. Emitters draw by `order`. A particle's size is the width; its height follows the
  sprite's aspect.
- **Fixed 1/60 s steps** (time left over carries to the next update; at most 8 steps per
  update), and all randomness comes from the instance's seed. The same seed gives the same
  particles at any frame rate: 60 × 1/60 s and 40 × 0.025 s match.
- **Cost:** 2,000 live particles take 0.075 ms per frame (update + quads, native release build;
  printed by `particle_fx_test`).
- **Hardware path (the renderer, not the particles):** the sprite batch that draws particles,
  `.anim` nodes and sprites builds its vertices on the GPU.
  - **How:** instancing, `vs_sprite_inst.sc`. One 64-byte instance per quad (its four corners, uv
    rect, tint) instead of four CPU-written vertices and six indices.
  - **Where:** it is used whenever the backend has the instanced program, which today is every one
    TOMS runs on (D3D11/12, Vulkan, OpenGL, GLES3 / WebGL2). This bgfx no longer has an
    instancing cap: every backend it supports can instance.
  - **Fallback:** without it (or with `toms_game --no-instancing`) the CPU path runs.
  - **Same pixels:** both paths are pixel-identical on all four Windows backends; `smoke.fx_cpu`
    keeps them so.
  - **This replaces FM79979's `cParticleBatchRender`.** That one used a compute shader for the same
    vertex expansion and then read the vertices back to the CPU; instancing needs no compute (not in
    WebGL2) and no readback.
  - **Three paths** (`--sprite-path=`, all pixel-identical, each kept so by a screenshot test):
    - **instancing** (the default)
    - **compute:** `cs_sprite.sc` expands quads into a GPU vertex buffer the draw reads; this is
      FM79979's `cParticleBatchRender` without its readback.
    - **cpu**
  - **Measured** (D3D11, F3 line, every particle alive and drawn):

    | Live particles | CPU vertices | GPU instancing | GPU compute |
    |---|---|---|---|
    | 30,000 | 1.70 ms (588 FPS) | 1.37 ms (730 FPS) | 1.76 ms (569 FPS) |
    | 300,000 | 27.1–27.8 ms (36–37 FPS) | 19.3–21.8 ms (46–52 FPS) | 19.0–20.2 ms (50–53 FPS) |

    "auto" picks instancing: it is as fast as compute and works everywhere.
  - **All paths use persistent GPU buffers** that grow as needed. Before, the CPU and instancing
    paths used bgfx's per-frame transient buffers (a few MB): at 300,000 quads they dropped about
    256,000 quads every frame, so only about 44,000 were drawn. The F3 line shows
    `drawn/asked quads` and the effect's live particles, so a drop cannot hide again.
  - **Where the CPU time goes** (`particle_fx_test`, 30,000 particles): about 0.23 ms for the
    motion (update) and about 1.2 ms building quads (colour / size curves, rotation, corners). GPU
    simulation (below) removes both.

**GPU simulation** (big emitters; `cs_fx_spawn.sc` + `cs_fx_update.sc`)

- **What moves to the GPU:** the whole particle update. That is position, velocity, gravity,
  drag, radial / tangential forces, colour / size / speed / spin over life (curves baked into
  64-entry tables), the flipbook frame and the rotation, plus the quad. The vertices are written
  straight into the buffer the draw reads, and nothing is read back.
- **What stays on the CPU:** spawning (when, where, the seeded random values, a free slot), so a
  GPU emitter makes the same particles as the CPU would. The CPU knows when each slot frees,
  because a particle's life is fixed at birth, so `liveCount()`, `finished()` and the pool cap
  work as before (`particle_fx_test`: equal live counts every frame).
- **Which emitters:** `EffectInstance::setGpuSimulation(renderer, threshold)` sets it up, and
  each emitter's `"simulation"` decides:

  | `"simulation"` (set in the particle editor, Render > Simulation) | Simulated on |
  |---|---|
  | `"auto"` (default) | the GPU when `maxParticles` is above the game's threshold, else the CPU |
  | `"gpu"` | the GPU whenever the device has compute shaders, else the CPU |
  | `"cpu"` | always the CPU |

- **The threshold** is the game setting `particleGpuThreshold` (`save/settings.json`; 0 = never).
  The default is **5000 on desktop and 3000 on phones**. `toms_game --fx-gpu-threshold=N`
  overrides it for one run.
- **Where it runs:** D3D11/12, Vulkan, OpenGL 4.3. The browser (WebGL2) and GLES3 phones have no
  compute shaders, so every emitter simulates on the CPU there. The particle editor's preview
  always simulates on the CPU.
- **Matches the CPU:** the fire, magic circle and snow, simulated on the GPU, differ from the CPU
  by at most a few colour levels on D3D11/12, Vulkan and OpenGL (the curve tables, float
  rounding). `smoke.fx_gpu` checks it against the same reference image as `smoke.fx`.
  - **Draw order differs:** particles of one GPU emitter draw in slot order, not birth order. That
    is invisible with `add` blending, but can differ slightly where `normal`-blended particles
    overlap.
- **Measured** (300,000 live particles, D3D11): GPU simulation 78–85 FPS (11.7–12.8 ms), CPU
  simulation 33–35 FPS (28.7–30.4 ms), about 2.4× faster.
- **F3 line:** shows `GPU n/m emitters (>threshold)`.
- **Preview:** `toms_game --fx=<file>#<effect>` draws an effect centred over whatever is on
  screen, seed 1, and replays a one-shot 0.5 s after it ends. Problems and missing sprites go
  to the log (`[fx] ...`).
- **Tests:**
  - `unit.particle_fx_test`: JSON, emission, motion, quads, seeds, checks, the examples against
    the real atlases, and the benchmark.
  - `smoke.fx`: `torch_fire` over the map, compared with `tests/golden/fx.png`.
  - `atlas.fx_up_to_date`: the fx atlas matches its project.

## 5. Editor UI (`particle_editor`, Qt)

```
particle_editor [file.particle]                                       the editor
particle_editor --headless check file.particle                        parse + check; exit 0 ok, 2 errors, 3 usage
particle_editor --headless render file.particle#effect out.png [frames] [every]
                                                                      a contact sheet (8 moments, 0.15 s apart)
particle_editor --selftest file.particle outdir                       automated check (-platform offscreen; QPainter preview)
particle_editor --selftest-gpu file.particle outdir                   the same for the game-renderer preview (on screen)
```

**The preview uses the game's renderer.** The viewport is a `GameCanvasView` (shared with the atlas
and anim editors, `tools/studio_common`): a native window that toms_game's `BgfxRenderer` draws
into (`ParticleViewportGpu.cpp`), with the gizmo and status line as a QPainter overlay on top:
- **Same as the game:** the same sprite batch, textures (point-sampled sRGB), blending and GPU
  particle simulation. Emitters set to `"gpu"`, or `"auto"` ones above the toolbar's **GPU sim
  above** value (5000, like the desktop game setting; 0 = never), are simulated on the GPU, just
  as the game would.
- **Also drawn through that renderer:** the background, grid, origin and gizmo (as solid quads).
  The status line is bgfx debug text: backend, FPS, live particles, GPU-simulated emitters and
  draw calls.
- **When it falls back to QPainter (CPU simulation):** View > *Preview with the Game Renderer*
  (off; applies after a restart), the offscreen selftest, a failed bgfx start, and
  `--headless render`.
- **Checked by `--selftest-gpu`:** Direct3D 11 at 60 FPS (vsync); the gizmo works in the native
  window. GPU vs CPU simulation in the editor:
  - magic circle and hit sparks are identical within a few colour levels;
  - the torch differs on 0.18% of pixels: a flipbook frame can switch on a tiny age difference,
    and normal-blend smoke draws in slot order;
  - snow differs on 0.47%: after 300 steps of random swirl, flakes drift by about a pixel.

Visual Studio: the **"particle_editor (recipes)"** target. `--headless render` is the quick way to
review an effect someone (or an AI) wrote without opening the editor.

**What is built** (tools/particle/qt; the sketch below was the plan, and the editor follows it):

- **Effects dock:** the effects and their emitters.
  - The checkbox hides an emitter in the preview only; double-click / F2 renames.
  - The toolbar adds effects and emitters, duplicates, deletes, and moves emitters up / down.
- **Viewport:** the game's own simulation (`EffectInstance`) drawn as the game draws it.
  - **Gizmo** of the selected emitter: drag the centre square to move it, the arrow for its
    direction (Shift: 15° steps), the arc ends for the spread, the round handle for the shape's
    size.
  - **Moving the effect:** a click fires the effect there; Ctrl+drag moves it while it plays
    (world-space particles stay behind); double-click puts it back at 0,0.
  - **View:** right-drag / middle / Space+drag pans, the wheel zooms.
  - **Sprites dropped** into the viewport make a new emitter there (several: a flipbook).
  - **Status line:** time, live particles, quads, batches, step time, seed.
- **Playback toolbar:**
  - Play / pause (Space), Restart (R), Step (.).
  - Replay one-shots, speed 0.1×–2×.
  - The preview seed and New Seed; background (checker, dark, light, a picture); grid.
  - After every edit the preview replays to the same moment with the same seed, so a change
    shows at once, without starting over.
- **Inspector:** one collapsible section per module.
  - Ranges are min–max with a link (linked = one value).
  - **Colour over life** is a gradient bar: drag stops, double-click a stop for its colour,
    double-click the bar to add one, right-click deletes.
  - **Size / speed / spin over life** are curve graphs: drag keys, double-click adds,
    right-click deletes, and the ease of the selected key is set below.
  - **Bursts** are a table. **Flipbook frames** take dropped sprites and are reordered by
    dragging.
  - **Blend:** a preset, or "custom…" with src / dst factors.
  - **Shape:** shows only the fields its type uses.
- **Sprites dock:** one tab per atlas, with thumbnails.
  - Double-click: the selected emitter draws that sprite.
  - Drag a sprite into the viewport for a new emitter, or onto the Flipbook frames.
  - + / − add or remove atlases (stored relative to the `.particle`, like `.anim`).
- **Timeline:** a ruler to scrub (paused; replayed with the seed, so exact) and one lane per
  emitter with its start..stop bar and burst ticks. Drag the bar's ends or middle.
- **Problems / Log:** `checkParticles` live (double-click selects the emitter), plus opened /
  saved files.
- **Effect > Add Preset:** any of the 11 recipe effects (built into the editor), with the
  atlases they need.
- **Undo / redo** for every edit; a held spin arrow or a drag is one step.

**Not yet:** the anim editor does not show effects (phase 3), and multiply / screen / custom
blends draw as normal (phase 2b), in the editor as in the game.

The plan's layout sketch:

```
┌ File  Edit  Effect  View  Help ───────────────────────────────────────────────────────────────┐
│ [New ▾ preset] [Open] [Save] │ ▶ Play  ⏸  ⟲ Restart  ⏭ Step │ Speed 1× │ Loop ☑ │ Seed 1234 🎲 │
├──────────────────┬─────────────────────────────────────────────┬──────────────────────────────┤
│ Effects          │                                             │ Inspector: flames            │
│ ▾ torch_fire     │                                             │ ▾ ☑ Emission                  │
│    👁 flames      │            (viewport: CanvasView)           │    Rate   [ 40 ]/s            │
│    👁 smoke       │                                             │    Bursts  t  count  repeat    │
│    👁 embers      │          ·  ·  ✦ ·                          │            0  8–12   0   [+]  │
│ ▸ hit_spark      │        ·  ✦✦✦  ·      ← particles           │ ▾ ☑ Shape  [circle ▾]         │
│ ▸ heal           │          ╲ │ ╱        ← spread arc           │    Radius [6]  Arc [0]–[360]  │
│                  │           ⊕──→      ← emitter + direction    │ ▾ Start values                │
│ [+ Emitter]      │          ( ◯ )      ← shape handle           │    Life  [0.5]–[0.9] s  🔗     │
│ [Duplicate] [🗑]  │                                             │    Speed [40]–[70]  Dir [-90°]│
│                  │  BG: ▣ checker ▾  Grid ☑  Map shot ☐         │    Spread [25°] ◔             │
│                  │  live 87/120 · quads 87 · batches 2 · 0.2ms │    Size [20]–[28]  Rot  Spin  │
│                  │                                             │    Color [■]–[■]  Sprite [🔥] │
├──────────────────┴─────────────────────────────────────────────┤ ▾ ☑ Over life                 │
│ Sprites: [game] [fx] ── (thumbnails, drag onto Sprite / Flip-  │    Color ▕██████▓▓▒░▏ gradient│
│          book fields or into the viewport for a new emitter)   │    Size  ╭──╮ curve           │
│ Timeline: flames ████████████████  smoke   ░░████████  (start/ │    Speed ╲___ curve           │
│           embers ▮ ▮ ▮ bursts      stop bars; scrub)           │ ▸ ☐ Forces  ▸ ☐ Flipbook      │
│ Problems / Log                                                 │ ▾ Render  Blend [add ▾] ...   │
└────────────────────────────────────────────────────────────────┴──────────────────────────────┘
```

- **Effects tree:** effects and their emitters, with eye (hide) and solo toggles, rename,
  duplicate and drag to reorder (draw order). There is one-click New from preset:
  - fire, smoke, sparks, hit burst, heal sparkles, magic circle, snow, rain, dust puff, coin
    burst, level-up pillar.
- **Viewport:**
  - The emitter shows as a gizmo: drag ⊕ to move it, drag the arrow to set direction and speed,
    drag the arc ends to set spread, and drag the shape handles to resize.
  - Click anywhere to fire the effect there (for bursts). Ctrl+drag moves the effect's origin to
    see `world` versus `local` space trails.
  - Background: checker, solid colour, or a game screenshot, to judge additive effects on
    the real art.
  - Stats line: live particles, quads, batches (texture or blend changes), update time.
- **Inspector:**
  - One collapsible section per module, each with an enable checkbox (Unity-style).
  - **Range widget:** min–max with a link toggle, so one value can mean "no random".
  - **Gradient editor** for colour and alpha over life: add, move or delete stops; colour picker;
    the ease per stop.
  - **Curve editor** for size, speed and spin over life, reusing the `.anim` Bézier/ease dialog.
  - Sprite fields accept drops from the Sprites dock. A flipbook takes several selected sprites,
    as in the anim editor's sprite sequence.
- **Timeline:** one bar per emitter (start/stop window, burst ticks) over the effect's duration.
  Drag the bars and the ticks, and scrub to preview at any time (simulation from 0 with the seed,
  so scrubbing is exact).
- **Problems:** the same checks as `--headless check`.
- **Headless:**
  - `particle_editor --headless check x.particle`
  - `--render x.particle#effect --frames N out.png`: a contact sheet for reviews and AI-made
    effects.
- **Also:** undo/redo for every edit, autosave of unsaved work, and recent files.

## 6. Legacy → new mapping (for a converter, if wanted later)

| Legacy | New |
|---|---|
| Emitter `Data`: gap, emitCount, amount, max | `emission.rate` = amount / gap (emitCount 0); a finite emitCount becomes `bursts` (count = amount, repeat = emitCount − 1, interval = gap) |
| `Data` velocity x, y, z + random offset | `direction` = atan2(y, x), `speed` = length, `spread` ≈ atan(offset / length) |
| src/dest blend `770,1` / `770,771` / `772,772` | `add` / `normal` / `normal` (+ warning) |
| `LifeInitrSetLife min, range, random` | `life: [min, min + range]` (or `min`) |
| `LifeActDyingByGameTime` | (always on) |
| `ColorInitrSetColor` / `SetRandomColor c` | `color: c` / `color: [c, [1, 1, 1, 1]]` |
| `ColorActBlendingByLife c` | `overLife.color` start → c (as a multiplier relative to the start colour) |
| `ColorActBlendingBy2Color c1, c2` | `overLife.color` keys at 0, 0.5 = c1, 1 = c2 |
| `ColorActBlending rate` | `overLife.color` key at 1 = start ± rate·life (approximate, warned) |
| `SizeInitSetSize x, y, random` | `size: [x·(1 − r), x]` (aspect from y/x) |
| `SizeActBlending rate, add` | `overLife.size` key at 1 = 1 ± rate·life / size (approximate) |
| `RotateInitRotate z` / `RotateActRotate z, random` | `rotation: z` / `spin: z` (± random) |
| `StartPositionInitBySquareRange w, h` | `shape: box [2w, 2h]` |
| `VelocityActAcceleration a` | `overLife.speed` ramp, or `forces.radial` |
| `VelocityActDircctionChange` | `overLife.speed` to 0 at the stop time, then `forces.gravity` = the new speed |
| `VelocityActBySatelliteAction` | `forces.tangential` (the legacy one is buggy; warned) |
| `TextureActDynamicTexture` (`.pi` frames) | `flipbook.frames` (`.pi` images imported into an atlas with `atlaspack import`) |
| `.prtg` group: emitter, position, direction, start/end, loop | one effect with emitters at `offset`, `direction`, `start`/`stop` |
| `.prtg` path (`.path` curve) | an `.anim` node with `pos` keys and the effect attached |
| model frames, perspective, compute shader, points, KillOutRange | dropped (3D only) |

There are 7 legacy files (`MagicTower/.../Media/ParticleData`: Fire, Glyph, Smoke, Snow,
Stage1 `.prt`, and Snow and Stage1 `.prtg`). A converter (`atlaspack`-style CLI,
`particle_editor --import x.prt`) is small. As with `.pi`/`.mpdi`, it is listed for later.

## 7. Phases

| Phase | What | Done when |
|---|---|---|
| **1. Runtime + format** ✅ | `toms::fx` (parse/write, `EffectInstance`, `appendQuads`); `particle_fx_test` (JSON round trip, determinism with a seed, life/curve/force math, pool limits, burst timing); `toms_game --fx=` preview and a smoke screenshot test; a `docs/examples/fx_recipes.particle` checked by ctest, plus a recipes section like [16](16_ANIMATION_RECIPES.md) | ctest green; the fire, sparks and heal presets render in the game |
| **2. Editor** ✅ | `particle_editor` (layout above): docks, gizmo, inspector modules, gradient and curve widgets, presets, timeline lanes, `--headless check` / `--render`, `--selftest` | selftest + screenshots; a new effect made without touching JSON |
| **2b. Blend modes** | `Quad` blend id + bgfx state per pair, `multiply`/`screen` shader mix, the same `blend` field for `.anim` nodes | batches split by blend; a golden image per preset |
| **3. Anim integration** | `"effect"` on anim nodes (follows the world transform, colour and visibility), `fx:` events for bursts; shown in the anim editor's viewport; the battle `attack` example gets a hit spark | a battle clip with sparks plays the same in both editors and the game |
| **4. Studio** | particles become the third plugin of the studio app (atlas + anim + particles) | one app, three editors |
| **5. (optional) Legacy import** | `.prt` / `.prtg` → `.particle`, using the mapping table | the 7 legacy files convert, with warnings where the behaviour is approximate |

## 8. Decisions (2026-10-03)

1. **Extension:** `.particle`.
2. **Blending:** presets plus any src/dest pair (3a). Waiting for confirmation.
3. **Order:** the editor before the anim integration: phase 1 (the runtime the editor previews
   with), then 2 and 2b, then 3.
4. **Legacy import:** later. A simple bridge (`.prt`/`.prtg`, and `.pi`/`.mpdi`) is to be offered
   once the editor is done.
5. **Viewport navigation, in every editor (atlas, anim, particle):** right-mouse drag pans, not
   only the middle button. It goes in the shared `CanvasView` and is added to the particle
   editor from the start.
