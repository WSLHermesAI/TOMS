# 18 — glTF 3D models

The engine loads and draws **glTF 2.0** models (`.gltf` with its `.bin` and images, or a single
`.glb`). That includes skinned characters, morph targets, GPU instancing and PBR materials, on
every backend the game runs on (Direct3D 11/12, Vulkan, OpenGL). `gltf_viewer` is a test viewer for
it, in the spirit of [gltf-viewer.donmccurdy.com](https://gltf-viewer.donmccurdy.com/).

```
Build\windows-release\bin\gltf_viewer.exe tests\gltf\skin_tube.gltf
python tools\gltf_viewer\fetch_samples.py           Khronos sample models -> Build\gltf_samples\
Build\windows-release\bin\gltf_viewer.exe Build\gltf_samples\Fox.glb
```
In Visual Studio: the launch targets *gltf_viewer (skinned test model)* and *gltf_viewer (Fox sample)*.

## 1. What is supported

| Area | Supported |
|---|---|
| Geometry | triangles (and strips / fans), lines, points; 16/32-bit indices or none; every accessor type: normalized and quantized integers are read as floats, sparse accessors; normals computed (flat) when missing |
| Vertex data | position, normal, tangent, 2 UV sets, `COLOR_0`, `JOINTS_0` / `WEIGHTS_0` |
| Materials | metallic-roughness PBR: base colour, metal/rough, normal, occlusion and emissive textures and factors; alpha `OPAQUE` / `MASK` / `BLEND`; double-sided; vertex colours |
| Skins | joints and inverse bind matrices, up to **128 joints per skin**, skinned on the GPU |
| Morph targets | any number of targets (position, normal, tangent), morphed on the CPU when the weights change |
| Animation | translation, rotation, scale, morph weights; `LINEAR` (rotation: slerp), `STEP`, `CUBICSPLINE`; several animations at once |
| Scenes | node hierarchy (TRS or matrix), the default scene, mirrored nodes (negative scale) |
| Files | `.glb`, `.gltf` + `.bin`, data URIs, external PNG / JPEG / BMP / TGA images |
| Extensions | `EXT_mesh_gpu_instancing`, `KHR_materials_unlit`, `KHR_texture_transform`, `KHR_materials_emissive_strength`, `KHR_mesh_quantization`, `KHR_materials_pbrSpecularGlossiness` (as base colour + roughness), `KHR_materials_transmission` + `KHR_materials_volume` (glass, approximated: see-through with its reflections, no refraction of what is behind) |

**Not yet supported:**
- **Compressed files are refused** with a message naming the extension, when the file *requires*
  it: `KHR_draco_mesh_compression`, `EXT_meshopt_compression`, `KHR_texture_basisu` (KTX2). To use
  such a file, re-export it without them, e.g. with `gltf-transform`.
- **Optional extensions are listed but not drawn:** for example clearcoat, sheen and
  `KHR_lights_punctual`. They appear in `Model::ignoredExtensions` and in orange in the viewer.
- **`KHR_texture_transform`:** one transform per material, taken from the base colour texture (or
  else the normal texture) and applied to all of its textures.
- **No image-based lighting yet** (no HDR environment maps).

Checked with the Khronos samples Fox, CesiumMan, RiggedFigure, BrainStem, AnimatedMorphCube,
MorphStressTest, SimpleInstancing, DamagedHelmet, MetalRoughSpheres, AlphaBlendModeTest,
BoxAnimated and InterpolationTest (`fetch_samples.py` downloads them).

## 2. Code

| Part | File | What it does |
|---|---|---|
| Loading and posing | `src/core/engine/gltf_model.h/.cpp` (`toms_core`, C++17, no GPU) | `loadModel()` → `gltf::Model` (plain CPU data, through [cgltf](https://github.com/jkuhlmann/cgltf)); `gltf::Pose`: animations, world matrices, joint matrices, morph weights, bounds |
| Drawing | `src/engine/src/gltf_renderer.h/.cpp` (`toms_bgfx`) | `GltfRenderer`: uploads a `Model`, draws a `Pose` |
| Shaders | `src/engine/shaders/vs_mesh.sc`, `vs_mesh_inst.sc`, `fs_mesh.sc`, `mesh_common.sh`, `vs_line.sc`, `fs_line.sc`, `varying_mesh.def.sc` | compiled for every backend and embedded (`ShaderProgram::Mesh`, `MeshInstanced`, `Line`) |
| Viewer | `tools/gltf_viewer/main.cpp` | SDL3 + ImGui panel |
| Test models | `tests/gltf/*.gltf`, made by `tools/gltf_viewer/make_test_models.py` | self-contained (embedded buffers and textures) |

**Using it from game code:**
```cpp
gltf::Model model;                              // keep it alive while it is drawn
gltf::loadModel("assets/media/models/hero.glb", model, &error);
GltfRenderer renderer;   renderer.init();       // once, after bgfx starts
auto gpu = renderer.upload(model);              // buffers + textures
gltf::Pose pose;         pose.bind(&model);     // one per character on screen

// every frame
pose.reset();
pose.apply(walk, std::fmod(t, model.animations[walk].duration));   // or blend(i, t, weight)
pose.updateWorld();
GltfRenderer::Frame f;  f.view = view;  f.proj = GltfRenderer::projection(45, aspect, 0.1f, 100);  f.eye = eye;
f.srgbOut = false;       // the desktop backbuffer is sRGB; true on the web
f.lights = {...};        // up to 4 GltfRenderer::Light (directional / point / spot); default: key + fill
f.boundsMin = ...; f.boundsMax = ...;   // what the directional shadow must cover (the scene and its floor)
renderer.begin(viewId, f, width, height);       // uses views viewId .. viewId + GltfRenderer::kViews - 1
renderer.draw(*gpu, pose, placement);           // casts and receives shadows
renderer.draw(*floorGpu, floorPose, floorPlacement, /*castShadows*/ false);
```

**How it draws:**
- **One vertex layout** for every primitive: position, normal, tangent, two UVs, colour, joints,
  weights. Missing data gets neutral values. Without tangents, the shader builds a tangent frame
  from screen-space derivatives.
- **Skinning:** the joint matrices are `world(joint) × inverseBind`, in the `u_joints[128]`
  uniform. A skinned primitive is drawn with only the placement matrix, as the glTF spec says.
- **Morph targets:** morphed into a dynamic vertex buffer, only when a node's weights change.
  - Limitation: a mesh shared by two nodes with *different* weights in the same frame shows the
    last node's weights on both.
- **Instancing (`EXT_mesh_gpu_instancing`):** one instanced draw per primitive. Each instance's
  matrix is `world × instance` (for a skinned node, `placement × instance`), in bgfx instance data.
- **Lighting:**
  - up to 4 lights: directional, point and spot (GGX / Smith / Schlick); point and spot lights
    fall off with 1/d² and their range (`KHR_lights_punctual`'s window), spots between their
    inner and outer cones;
  - the file's own lights (`KHR_lights_punctual`) come from `Pose::lights()`, placed by their
    nodes;
  - hemisphere ambient (sky / ground), and a studio-like reflection with a bright horizon, so
    metals read as metal;
  - an analytic environment BRDF, exposure, and ACES tone mapping.
- **Shadows** (each light can cast; on by default for the key light):
  - **One shadow atlas**, a square depth texture (`Frame::shadowMapSize`, default 4096), split
    into tiles: a directional light takes 1 tile, a spot light 1, a point light 6 (one 90° view per
    cube face; the shader picks the face from the direction to the light). At most 24 tiles; they
    shrink as more lights cast.
  - **Fitting:** a directional light's orthographic view covers `Frame::boundsMin..boundsMax`; a
    spot light's view is its cone; point and spot views reach to their range (or the far side of
    the bounds).
  - **The shadow pass** draws every opaque or masked triangle primitive into each tile with the
    colour pass's vertex shader (`fs_shadow.sc`), so skinned, morphed and instanced meshes cast
    exactly what is drawn. Blended and glass materials cast no shadow.
  - **Sampling:** 3×3 taps of hardware 2×2 PCF (soft edges), kept inside the tile.
  - **Against acne:** a normal offset of about one shadow texel (more where the light grazes the
    surface, which also hides the facets of low-poly curved meshes). Point and spot lights also
    step one texel towards the light, in world units: a depth-buffer bias would grow far too big
    away from a perspective light. `Frame::shadowBias` (viewer: *shadow bias*,
    `--shadow-bias=`) scales both.
  - **Caps:** `GltfRenderer::shadowsSupported()`; without a renderable + sampleable depth format
    everything draws unshadowed.
- **Alpha:** `MASK` discards below the cutoff. `BLEND` draws go to the second view, sorted far to
  near, without depth writes. Glass (transmission) is blended there too, premultiplied, so its
  reflections stay bright.
- **Back faces:** a double-sided material flips the normal when it faces away from the camera. The
  shader does not use `gl_FrontFacing`: Direct3D calls clockwise triangles "front", glTF calls
  counter-clockwise ones front, so that flag is backwards on D3D (it once lit every model from
  below).

## 3. The viewer

| Input | Does |
|---|---|
| drop a `.gltf` / `.glb`, or *Open...* | loads it, frames it (over the playing animations' range) |
| left drag / right or middle drag / wheel | orbit / pan / zoom |
| `Space` `F` `G` `K` `B` `W` `R` `H` | play-pause, frame, grid, skeleton, bounds, wireframe, auto-rotate, panel |
| `1`..`9` | play only that animation |

**The panel:**
- **Model:** counts (vertices, triangles, nodes, meshes, materials, textures, skins and joints,
  morph targets, instances), and every extension, green when drawn and orange when ignored, plus
  warnings.
- **Animation:** play / pause, speed, a time slider, and a checkbox per animation (several can
  play together).
- **Morph targets:** a slider per weight, which overrides the animation for that weight.
- **Display:** grid, axes, bounds, skeleton, wireframe, background, field of view, and the views
  lit / normals / base colour / metal-rough / UV / shadow (the first shadowed light's term).
- **Lights and shadows:**
  - shadows on / off, a *ground* (a floor under the model that catches the shadows), the atlas
    size, the shadow bias;
  - the file's own lights (`KHR_lights_punctual`), when it has some;
  - otherwise a rig: *default* (key + fill), *point*, *spot*, *all*, or your own. Each light has
    a type, colour, intensity, cast shadows, then direction (yaw / pitch), or position, range and
    cones. Up to 4; `+ directional` / `+ point` / `+ spot` add one;
  - exposure, ambient, sky and ground colours, tone mapping.

  The lights are drawn in the view: an arrow for a directional light, a star for a point light, a
  cone for a spot light.
- **Performance:** fps, backend, draw calls, instances, skinned draws, morph uploads, shadow tiles
  and shadow draws.

**Command line** (also used by the tests):
- `--anim=<index|name|none>`, `--time=<s>` (freezes the animations at that time);
- `--frames=<n> --screenshot=<png>`;
- `--yaw= --pitch= --zoom=`, `--size=WxH`, `--no-ui`, `--debug=<0..5>`;
- `--light=default|point|spot|all|file`, `--shadows=0|1`, `--ground=0|1`, `--shadow-bias=<x>`;
- `--renderer=d3d11|d3d12|vulkan|opengl`.

## 4. Tests

- **Unit test `gltf_model_test`:** 47 checks on the test models.
  - **Skin:** rest-pose joint matrices are identity; slerp at 0.25 s; the cubic spline's Hermite
    values; CPU-skinned bounds.
  - **Morph:** weights for linear and step interpolation; CPU morph results.
  - **Instancing:** 100 instance matrices and their bounds.
  - **Materials:** factors, sRGB, the embedded PNG, the texture transform, emissive strength,
    unlit, alpha modes.
  - **Glass and lights:** `KHR_materials_transmission` + volume read; a `KHR_lights_punctual` spot
    (colour, intensity, range, cones) placed by its node.
  - **Errors:** a missing file; a required extension we cannot read.
- **Screenshot tests `smoke.gltf_*`:** `gltf_viewer` renders a frozen frame, compared with
  `tests/golden/gltf_*.png`.
  - **Scenes:** skin (linear and cubic), morph (linear and step), instancing, PBR, the normals
    view, and shadows: directional (`gltf_shadow_dir`), point (`gltf_shadow_point`), spot
    (`gltf_shadow_spot`), all three at once (`gltf_shadow_all`). Every scene stands on the ground
    with the default key light's shadow.
  - **Backends:** all on Direct3D 11; skin, PBR and all-shadows also on Vulkan and OpenGL,
    against the same images.
  - **Running them:** `ctest --preset windows-release -L gltf`.
  - **After an intended change:** `TOMS_UPDATE_GOLDEN=1 ctest ... -L gltf`, then look at the
    images.
- **Changing a test model:** edit `make_test_models.py`, run it, then update the references.
