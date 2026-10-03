# 17 — Particle effects (`.particle`)

**Status:** phase 1 is done (the format, the runtime `toms::fx`, the `fx` atlas, 11 example
effects, `toms_game --fx=`, tests). Next: the editor (phase 2). Sections 1 and 2 explain the
design; 3 and 4 describe what is built.

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
- **Preview:** `toms_game --fx=<file>#<effect>` draws an effect centred over whatever is on
  screen, seed 1, and replays a one-shot 0.5 s after it ends. Problems and missing sprites go
  to the log (`[fx] ...`).
- **Tests:**
  - `unit.particle_fx_test`: JSON, emission, motion, quads, seeds, checks, the examples against
    the real atlases, and the benchmark.
  - `smoke.fx`: `torch_fire` over the map, compared with `tests/golden/fx.png`.
  - `atlas.fx_up_to_date`: the fx atlas matches its project.

## 5. Editor UI (`particle_editor`, Qt)

It is built from the same parts as `anim_editor`: `studio_common` (Theme, Icons, CanvasView,
SpriteImageCache, Console), the Sprites dock with one tab per atlas, the undo stack and
`--headless check`. It is written as a plugin so it can join the studio app (atlas + anim +
particles) later.

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
| **2. Editor** | `particle_editor` (layout above): docks, gizmo, inspector modules, gradient and curve widgets, presets, timeline lanes, `--headless check` / `--render`, `--selftest` | selftest + screenshots; a new effect made without touching JSON |
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
