# 09 — Rendering: 2D + 3D Mixed (L3)

Part of the [Engine Blueprint](README.md). This doc covers the render layer for a game that mixes:
- 2D sprites, tilemaps, UI and **Spine** skeletons
- **glTF 3D models** with **shadows**
- 2D sprites/Spine *inside* a 3D scene, and 3D inside UI

Targets, all through **bgfx** (decided 2026-09-25): D3D11/D3D12/Vulkan on Windows, Vulkan/GL on
Linux, Metal on Apple, GLES/Vulkan on Android, and **WebGL2** on the web. Library facts are dated
2026-09 with sources in [07](07_THIRD_PARTY_LIBRARIES.md) and [11](11_BGFX_QT_ARCHITECTURE.md).

---

## 1. What TOMS has, and why it is replaced

- **Three hand-written backends** (`renderer.cpp` Vulkan, `renderer_webgl.cpp`, `renderer_webgpu.cpp`)
  behind `render_iface.h`. That interface supports **one sprite atlas + one font atlas**, with no
  render targets, scissor, blend modes, transforms or depth.
- **Every backend draws all sprites first, then all text**, so a panel can never cover text.
  `game_title_glue.cpp` works around this.
- **The WebGPU backend is broken:**
  - `flush()` clears and presents on every call, so the text pass wipes the sprites
  - `init` gets 1280×720 instead of the 1024×768 design size
  - it has no `updateFont`
- **Shader logic exists three times** (SPIR-V, GLSL ES, WGSL), plus a 15-float vertex layout.
- **No headless capture any more** (`savePNG` is a stub).
- `batch_renderer.h` (Vulkan) is solid, and its batching idea carries into 3.3.

Extending this to 3D + shadows + Spine across three APIs is the most expensive thing the project
could build itself. It is the clearest case for adopting.

## 2. Decision card: 3.1 RHI (graphics API abstraction)

| | Diligent Engine | sokol_gfx | WebGPU everywhere (Dawn) | ⭐ **bgfx** (chosen) | Filament (full renderer) |
|---|---|---|---|---|---|
| Licence | Apache-2.0 | zlib | BSD-3 | BSD-2 | Apache-2.0 |
| Desktop / Android | **Vulkan**, D3D11/12, GL/GLES | D3D11, GL, Metal; **Vulkan experimental** (added Dec 2025, still marked experimental) | via Dawn on **Vulkan** / D3D12 / Metal | Vulkan, D3D11/12, GL, Metal | Vulkan, GL/GLES, Metal |
| Apple | Metal backend **needs a commercial licence on iOS** → use **Vulkan via MoltenVK** | Metal | Metal via Dawn | Metal | Metal |
| Web | **WebGPU** + GL (WebGL) | **WebGL2 + WebGPU** | browser WebGPU through `emdawnwebgpu`; **no WebGL2 fallback** | **WebGL only** (its WebGPU is Dawn-native only) | **WebGL2**; WebGPU experimental |
| 3D extras | **DiligentFX**: glTF PBR renderer, shadows, post-FX (which FX components run on WebGPU/GL is **not documented; test first**) | none (build 3D) | none (build 3D) | examples only | best-in-class PBR, gltfio, many shadow filters |
| Fit with our own 2D + UI passes | one RHI for everything | one RHI | one API | one RHI | **owns its render loop**, so sharing a frame with our 2D/UI passes is awkward |
| Size / complexity | large | tiny, header-only | medium (big to build) | medium | large |
| Maintenance | active | very active | active (Google) | active | active |

**Decision (2026-09-25): bgfx.** This reverses the earlier Diligent pick. The reasons:

- **Browser WebGPU is no longer a requirement.** For a 2D tile game with light 3D, WebGL2 covers
  the web target, and bgfx's WebGL2 path (Emscripten) is mature. That removes the one reason bgfx
  was rejected.
- **One small, proven RHI** with a C++ API, D3D11/D3D12/Vulkan/GL on desktop, Metal on Apple (no
  commercial licence issue), GLES/Vulkan on Android. Its buffered, submit-per-view model matches
  our frame graph (one bgfx view per pass).
- **It embeds cleanly in Qt** through a native window handle, so the editor viewport and the
  shipped game run the same renderer ([11 §2](11_BGFX_QT_ARCHITECTURE.md#2-embedding-bgfx-in-the-qt-editor)).
- **The costs we accept:**
  - no built-in 3D renderer: PBR, CSM and skinning are built on bgfx, using its examples as the
    starting point (no DiligentFX)
  - shaders move to bgfx's `.sc` dialect and the `shaderc` tool (§2.1)
  - no browser WebGPU: if it is ever needed, re-open this card (sokol_gfx or Dawn)
  - no compute on the WebGL2 path, so web features must have a non-compute route
    (`bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE`)

**Rejected now:** Diligent (heavier to build and embed, and its main advantage, browser WebGPU, is
no longer needed); sokol_gfx (Vulkan still experimental); WebGPU-everywhere (no WebGL2 fallback);
Filament as the RHI (owns its loop). Filament remains a good *reference* for PBR and shadow filtering.

### 2.1 Shaders (one source)

- Author shaders once in bgfx's **`.sc` format** (GLSL-like with bgfx macros such as `SAMPLER2D`,
  `mul`, `$input/$output`), with a `varying.def.sc` per shader family and `bgfx_shader.sh` /
  `bgfx_compute.sh` includes.
- **`shaderc`** compiles each shader offline per backend: DXBC/DXIL (D3D11/12), SPIR-V (Vulkan),
  Metal, GLSL and **ESSL 300 for WebGL2**. The CMake build runs it as custom commands (bgfx.cmake
  provides `bgfx_compile_shaders`), and the binaries are resources (R.2) chosen at runtime by
  `bgfx::getRendererType()`.
- Shader permutations per quality tier are `#define`s passed to `shaderc` by the asset pipeline
  (L9.8) and cached as resources.
- The TOMS SPIR-V / GLSL ES / WGSL sprite shaders are replaced by one `vs_sprite.sc` / `fs_sprite.sc` pair.
## 3. The frame (3.2 frame graph)

A small frame graph declares passes and their render targets. It handles transient target reuse
and barriers, and it lets quality tiers remove passes.

```mermaid
flowchart LR
    SH["Shadow pass(es)<br/>CSM cascades / volumes"] --> OP["Opaque 3D<br/>glTF PBR, skinned"]
    OP --> TR["Transparent 3D<br/>+ sprites/Spine in 3D (depth-tested, sorted)"]
    TR --> FX["3D particles / FX"]
    FX --> PP["Post: tonemap, bloom, FXAA"]
    PP --> W2["2D world layer<br/>tilemap, sprites, Spine (ortho camera)"]
    W2 --> UI["UI layer<br/>RmlUi / widgets, text"]
    UI --> DBG["Debug overlay (ImGui, dev builds only)"]
    RT["Render-to-texture<br/>3D portrait in UI"] -.-> UI
```

Each pass is one or more **bgfx views** (`bgfx::setViewFrameBuffer`, `setViewRect`, `setViewMode`); the frame graph assigns view ids in pass order, and bgfx executes views in id order. A 2D-only scene simply has no 3D passes, so Magic Tower's current floors run through the same graph
at almost no cost.

## 4. Sub-system cards

| Card | Pri | Scope | ⭐ Recommendation | Alternatives |
|---|---|---|---|---|
| **3.3 2D renderer** | P0 | batched quads with **real draw order** (layer + sort key; text is an ordinary batch), 9-slice, scissor/clip rects (ScrollView), tint/blend modes, atlases from R.2 | **build** on bgfx transient vertex/index buffers, reusing the batching idea of `batch_renderer.h`; scissor via `bgfx::setScissor`, draw order via view mode `Sequential` or sort keys (M) | bgfx's `examples/common` nanovg backend for vector shapes |
| **3.4 Spine 2D** | P1 | skeleton playback, mixing, skins, events → the event bus; rendered by 3.3 (2D) or as quads in 3D (3.9) | **spine-cpp**. *Licence:* every developer who integrates it must own a **Spine Editor licence*; the runtime can then ship | **Rive** runtime (MIT; has WebGL/WASM, Vulkan, D3D, Metal backends). DragonBones (MIT) is **stale since 2018: avoid** |
| **3.5 glTF parser** | P1 | glTF 2.0/GLB: meshes, PBR materials, skins, morphs, animations; KHR_texture_basisu, KHR_mesh_quantization, EXT_meshopt | **fastgltf** (MIT, fast, C++17) | **cgltf** (MIT, single header); tinygltf (MIT, maintenance unclear) |
| **3.6 3D renderer** | P1 | forward (clustered later), unlit + PBR metal-rough, normal maps, GPU skinning, instancing, optional IBL | **build** a forward PBR on bgfx, starting from its `18-ibl`, `05-instancing` and `21-deferred` examples; bgfx has **no PBR example**, so metal-rough shading is written from the glTF reference (L) | Filament as a reference |
| **3.7 3D skeletal animation** | P2 | clip sampling, blending, optional IK, glTF skins | **ozz-animation** (MIT) | own sampler (M) |
| **3.8 Shadows** | P1 | see §5 | CSM + PCF, built from the bgfx `16-shadowmaps` example (cascades with hard/PCF/VSM/ESM sampling; re-check the details in its source) | volumes, VSM, blob |
| **3.9 2D/3D composition** | P1 | camera stack (3D perspective → 2D ortho → UI); **billboards / depth-tested sprite and Spine quads in 3D**, sorted with transparents; render-to-texture for 3D-in-UI; shared depth when 2D must occlude 3D | **build** on the frame graph (M) | — |
| **3.10 Particles / FX** | P1 | 2D + 3D emitters, pooled ([08 §6](08_RESOURCE_AND_LIFETIME.md#6-pools-for-objects-and-particles-r3)) | **Effekseer** (MIT) with the community bgfx renderer **efkbgfx** (cloudwu/efkbgfx; predefined materials work, user-defined materials are not supported) | own simple emitter (M); bgfx's `ps` particle example |
| **3.11 Post-processing** | P2 | tonemap, bloom, FXAA, color grading | **build** (S–M) as bgfx views over frame buffers | — |
| **3.12 Quality tiers** | P1 | see §6 | **build** (S) | — |

## 5. Shadows (3.8)

| Technique | Use | Web notes | Cost |
|---|---|---|---|
| ⭐ **Cascaded shadow maps + PCF** | default for directional light | works on **WebGL2** (depth textures + comparison samplers; check `BGFX_CAPS_TEXTURE_COMPARE_LEQUAL`) | L to build; the bgfx `16-shadowmaps` example is the start |
| Spot / point shadow maps | torches and similar | point lights need a cube map or 6 views, which is costly on web; limit count per tier | M |
| VSM / EVSM | soft shadows on desktop tier | needs float render targets (check WebGL2 extensions per device) | M |
| **Stencil shadow volumes** | optional crisp hard shadows for low-poly scenes | stencil is available on WebGL2 and WebGPU; **silhouette extraction must run on the CPU on WebGL2** (no compute); on D3D11+/Vulkan it can be a compute pass. bgfx has a `14-shadowvolumes` example. Cost grows with silhouette edge count; needs closed meshes | L; P2 |
| **Blob / projected shadows** | sprites and Spine in 3D, low tier | trivial everywhere | S |

The shadow debug views (cascade colors, the shadow map itself, volume wireframe) are editor panels
(L9.7).

## 6. Quality tiers (3.12)

| Tier | Chosen when | Shadows | Other |
|---|---|---|---|
| Desktop | bgfx D3D11/12 or Vulkan | 3–4 cascades, 2048², PCF 5×5 or VSM | MSAA 4×, full post |
| Mobile | Android/iOS | 2 cascades, 1024², PCF 3×3 | MSAA 2× or FXAA, reduced post |
| Web-WebGL2 | every browser (bgfx GLES backend) | 1 cascade, 1024², or blob only | FXAA, no bloom |

The tier is chosen from device capabilities (L1.4) at startup. It can be overridden in settings
(L2.5), and it also sizes pools and memory budgets ([08 §9](08_RESOURCE_AND_LIFETIME.md#9-budgets)).

## 7. Resources and tests

- **New resource types** (in [08 §10](08_RESOURCE_AND_LIFETIME.md#10-resource-types-covered)): mesh,
  material, glTF scene (a dependency tree), skeleton, animation clip, Spine skeleton + atlas, effect,
  shader permutation. Frame-graph render targets are recreated on resize or tier change through the
  deferred-destroy queue.
- **Pipeline** (L9.8): **gltfpack / meshoptimizer** (MIT) to optimize glTF, bgfx **texturec** for
  KTX/DDS textures (block-compressed per platform: BC on desktop, ETC2/ASTC on mobile/web), and
  bgfx **shaderc** for every shader. The 3D model path is in
  [11 §5](11_BGFX_QT_ARCHITECTURE.md#5-3d-models-what-bgfx-supports).
- **Tests** (Q.2):
  - offscreen golden images per tier for a 2D scene, a glTF + CSM scene and a mixed scene
  - the 100× load/unload soak includes a 3D scene
  - GPU timings per pass: bgfx per-view GPU times (`bgfx::getStats()->viewStats`) on every backend; Tracy CPU zones on desktop (Tracy cannot instrument the web build)
