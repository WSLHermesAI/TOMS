# Progress Report — Log Part 4 (2026-10-03)

> Chronological log entries, oldest first. The live **Next Step**, status table, waiting list and
> open questions are in [PROGRESS_REPORT.md](PROGRESS_REPORT.md).

---

### 2026-10-03 — content tools: atlas, animation and particle editors, all on the game's renderer

The owner asked for three content editors: a sprite atlas packer, a node-animation editor, and a new
particle editor with a JSON format to replace the legacy one. The editors had to preview content
exactly as the game draws it, and particles had to use the GPU when the hardware supports it.
Committed by the owner as `c8b9b38` (atlas), `904cdaa` (anim) and `b8cf970` (particles). The work
after `b8cf970` is not committed yet: GPU particles, the CPU/GPU threshold and the editors on bgfx.

**1. Atlas tool** ([../14_ATLAS_TOOL.md](../14_ATLAS_TOOL.md), `tools/atlas/`)

- `core/`: a project file (`.atlasproj`) that lists the images inside the packed atlas, plus child
  sprites (sprites without pixels of their own).
- The packer, the image ops and the exporters: `.atlas` JSON with extension fields, `.plist` and XML.
- A Qt editor, `atlas_editor`: page canvas, sprite-edit canvas (pivot, 9-slice), docks, undo.
- A web build of the same core: `tools/atlas/web`.
- A command line and tests.
- The game loads several atlases at once.

**2. Node animations** ([../15_ANIMATION.md](../15_ANIMATION.md), `src/core/engine/anim_*`, `tools/anim/`)

- **The `.anim` format and runtime:** `AnimPlayer`.
- **Per-channel keys:** each node keeps its current position, scale, rotation and sprite. Each key
  holds only the channels it uses: position, scale, rotation, sprite, colour.
  - `PoseCache` advances one cursor per channel and updates only the animated tracks.
  - World transforms and colours are rebuilt only when something changed, so a node with only
    sprite keys never recomputes its transform.
- **`anim_editor`:** a viewport, a timeline, undo.
  - Add Atlas takes several files at once.
  - Dropping several sprites on the timeline asks for a time gap (default 0.1 s) and inserts one
    sprite key per sprite. This is for frame sequences, as MPDI had.
- **Recipes for hand- or AI-written clips** ([../16_ANIMATION_RECIPES.md](../16_ANIMATION_RECIPES.md),
  `docs/examples/anim_recipes.anim`):
  - fade in/out, moves, a hit flash and others;
  - a full battle example: step in, swing, step back;
  - a checklist to hand to an AI.

**3. Particles** ([../17_PARTICLES.md](../17_PARTICLES.md))

- **Design:** made after reading the legacy `ParticleEditor` and `GameplayUT/Render/Particle`.
- **Format:** a new `.particle` JSON format (owner approved).
  - Fixed modules per emitter.
  - Unknown fields are errors.
  - Curves over normalised life.
  - Every source and destination blend factor can be chosen (the owner asked for this), plus presets.
- **Runtime, `toms::fx` in `src/core/engine/particle_fx.*`:**
  - fixed 1/60 s steps and a seeded RNG, so `seek` is deterministic;
  - prewarm, bursts, shapes, forces, flipbooks.
- **`particle_editor` (`tools/particle/`):**
  - effects, inspector, timeline and sprite docks; snapshot undo; presets;
  - `--headless check/render`, `--selftest`, `--selftest-gpu`.
- **Examples:** 11 example effects (`docs/examples/fx_recipes.particle`) and an `fx` atlas of
  generated sprites (`tools/atlas/make_fx_sprites.py`).
- **The game:** `toms_game --fx=file#effect` previews an effect, and `--fps` / F3 shows a HUD
  (FPS, backend, sprite path, compute support, quads drawn/asked, live particles, GPU emitters).

**4. GPU sprite batching and GPU particle simulation**

- **The sprite batch has three paths:** instancing, compute expansion and CPU.
  - Auto picks instancing, then compute, then CPU.
  - All three use persistent dynamic buffers. This fixed 300 000 quads silently being dropped by
    transient buffers.
- **GPU simulation, as the owner asked:** the GPU updates position, colour and size, and its result
  feeds the vertices with no readback.
  - The CPU only spawns. Compute shaders `cs_fx_spawn` / `cs_fx_update` write vertices that
    `vs_sprite_cs` draws.
  - Curves are baked into 64-entry tables.
  - Live counts match the CPU simulation exactly; a unit test checks this every frame.
- **Measured** (D3D11, this PC):
  - 30 000 quads: instancing 1.37 ms, CPU 1.70 ms, compute expansion 1.76 ms.
  - 300 000 live particles: CPU simulation 33–35 FPS, GPU simulation 78–85 FPS.
- **When the GPU is used:**
  - A game setting `particleGpuThreshold`: 5000 on desktop, 3000 on Android/iOS. An emitter whose
    max particles is over it simulates on the GPU.
  - A per-emitter **Simulation** flag set in the editor: Auto (threshold), CPU or GPU.
  - Both apply only where compute is supported. Otherwise the CPU is used, as on the web (GLES 3.0).
  - The game option is `--fx-gpu-threshold=N`.

**5. Editors draw with the game's renderer** (`tools/studio_common/GameCanvasView`)

- The atlas, anim and particle canvases are drawn by toms_game's `BgfxRenderer` in the canvas's
  native window: the same sampling, blending, batch and GPU particles as the game.
- QPainter editing visuals (outlines, handles, gizmos, labels) are drawn on top as an overlay.
- There is one bgfx per process, and it follows the visible canvas.
- **QPainter fallback:** View > *Preview with the Game Renderer*, the offscreen selftests, and the
  standalone atlas build.
- **Right-drag pans every editor view** (the owner asked for this): a plain right click still opens
  the context menu. Middle-drag and Space+drag still work.

**6. Tests and Visual Studio**

- **One test runner:** the unit tests are merged into one runner, `toms_tests`. CTest still lists
  `unit.<name>`.
- **Visual Studio:** `launch.vs.json` was trimmed to the useful targets: the editors, `toms_tests`
  and the game's particle preview.
- **New tests:**
  - particle unit tests: 102 checks, including GPU vs CPU and the Simulation flag;
  - smoke tests `fx`, `fx_cpu`, `fx_compute`, `fx_gpu`, all against `tests/golden/fx.png`.

**Verified:**

- **CTest:** 46 of 50 pass. The 4 failures are `smoke.stage*`, and they already failed before
  today's work.
- **On screen (D3D11), each editor's `--selftest-gpu` passes:**
  - the atlas editor: the page, sprite-edit mode and back, with right-drag on both canvases;
  - the anim editor: frames are drawn, right-drag pans;
  - the particle editor.
- **Offscreen:** the selftests pass through the QPainter fallback.

**Not verified:**

- GPU particles on a phone (Vulkan/GLES 3.1) or on D3D12, Vulkan and OpenGL desktop.
- The editors on a HiDPI second monitor.

**Still open:**

- The legacy import bridge: `.prt`/`.prtg` → `.particle` and `.pi`/`.mpdi` → atlas/`.anim`. The
  owner asked to be reminded once the particle editor was done, and it is due now.
- Particles phase 2b: multiply, screen and custom blend in the renderer.
- Phase 3: effects on `.anim` nodes and events.
- Merging the three editors into one studio app.
- The overlay could be uploaded only when it changes.
