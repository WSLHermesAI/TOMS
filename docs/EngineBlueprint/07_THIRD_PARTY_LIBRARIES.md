# 07 — Third-Party Library Shortlist

Part of the [Engine Blueprint](README.md). **The default is to adopt a reliable library, and build
only where none fits.** Every pick must build from CMake with **both MSVC and Emscripten**
(Visual Studio requirement, [05](05_DEV_WORKFLOW_VS.md)).

**Research date: 2026-09-24; RHI and editor rows revised 2026-09-25** (bgfx + Qt decision, [11](11_BGFX_QT_ARCHITECTURE.md)). Confidence is marked per row:
- **✔** checked on the official page when this doc was written
- **~** from prior knowledge, likely correct, not re-checked
- **?** could not be confirmed. Re-check before depending on it

Libraries move fast. Re-verify the ✔/~ rows at each milestone start.

Verdicts: ⭐ recommended · ○ alternative · ✗ avoid.

---

## 1. Platform, files, packaging

| Library | Licence | Web / WASM | MSVC + vcpkg | Status | Verdict | Conf. |
|---|---|---|---|---|---|---|
| **SDL3** (app, window, input, gamepad, touch, DPI, pref path, Storage API) | Zlib | ✓ Emscripten | ✓ / `sdl3` | active | ⭐ 1.1–1.4, 2.1 | ~ (how its Storage API persists on web: ?) |
| GLFW | Zlib | via Emscripten port | ✓ | active | ○ desktop-only feel; no mobile | ~ |
| **vcpkg** (manifest mode) | MIT | `wasm32-emscripten` is a **community** triplet (per-port support varies) | VS-integrated | active | ⭐ with FetchContent as fallback | ✔ |
| **PhysicsFS** (mounts, zip) | Zlib | ? | ✓ / `physfs` | slow | ○ for 2.1 | ~ / web ? |
| **miniz** (zip) | MIT | ✓ | ✓ | active | ⭐ 2.2 packages | ~ |
| enkiTS / Taskflow (jobs) | Zlib / MIT | needs `-pthread` + cross-origin isolation, or a single-thread fallback | ✓ | active | ⭐ / ○ 1.6 | ~ |
| `std::filesystem::rename` | (STL) | ✓ | MSVC implements it with `MoveFileExW(… MOVEFILE_REPLACE_EXISTING)`, so it **replaces** the target. C `rename` **fails** if the target exists | — | ⭐ fix for 2.3 | ✔ |

## 2. Rendering

| Library | Licence | Backends (status) | Web | Verdict | Conf. |
|---|---|---|---|---|---|
| **bgfx** (+ bx, bimg; tools shaderc, texturec, geometryc) | BSD-2-Clause | D3D11/12, GL, GLES, Metal, Vulkan, WebGL2 (Emscripten); WebGPU only via Dawn native | ✓ **WebGL2** | ⭐ **RHI 3.1, chosen 2026-09-25** ([09 §2](09_RENDERING_2D_3D.md#2-decision-card-31-rhi-graphics-api-abstraction)). CMake via **bgfx.cmake** (`bgfx_compile_shaders`) | ✔ |
| Diligent Engine | Apache-2.0 | D3D11/12, GL/GLES, **Vulkan**, Metal (**iOS Metal needs a commercial licence**), **WebGPU** (Win/Linux/mac/Web) | ✓ WebGPU + GL | ○ was the RHI pick until 2026-09-25; re-open only if browser WebGPU becomes a requirement | ✔ |
| DiligentFX (glTF PBR, shadows, post-FX) | Apache-2.0 | follows Diligent | — | ✗ (Diligent-only) | ✔ |
| **sokol_gfx** | Zlib | GL 3.3, GLES3/WebGL2, D3D11, Metal, WebGPU (via emdawnwebgpu since 2025-06); **Vulkan experimental** (added 2025-12, still marked experimental 2026-09) | ✓ WebGL2 + WebGPU | ○ the fallback if browser WebGPU is ever required | ✔ |
| Dawn (WebGPU native) | BSD-3 | D3D11/12, Vulkan, Metal, GL | browser WebGPU through **emdawnwebgpu** (`--use-port=emdawnwebgpu`); `webgpu.h` is stable, `webgpu_cpp.h` is not | ○ "WebGPU everywhere" (no WebGL2 fallback) | ✔ / native ~ |
| wgpu-native | MIT OR Apache-2.0 | Vulkan, Metal, D3D12 (+GL) | use emdawnwebgpu on web | ○; needs a Rust toolchain | ✔ |
| Filament | Apache-2.0 | GL 4.1+, GLES 3.0+, Metal, Vulkan, WebGL2, **WebGPU experimental**; gltfio; shadows PCF/EVSM/DPCF/PCSS + cascades | ✓ WebGL2 | ✗ as the RHI (owns its loop); ○ reference | ✔ |
| **bgfx shaderc** | BSD-2-Clause | `.sc` → DXBC/DXIL, SPIR-V, Metal, GLSL, ESSL (WebGL2) | offline tool | ⭐ the only shader compiler needed | ✔ |
| Slang, SPIRV-Cross, Tint/Naga | Apache-2.0 / BSD-3 / MIT+Apache | cross-compilers | offline tools | ✗ no longer needed (shaderc covers every bgfx backend) | ✔ |
| **fastgltf** | MIT | — | ✓ | ⭐ 3.5 | ~ |
| cgltf / tinygltf | MIT / MIT | — | ✓ | ○ / ○ (tinygltf maintenance ?) | ~ |
| **meshoptimizer + gltfpack** | MIT | — | offline + runtime decode | ⭐ pipeline | ~ |
| Draco | Apache-2.0 | — | ✓ | ○ (slow-moving; prefer meshopt) | ~ |
| **ozz-animation** | MIT | — | ✓ (Emscripten samples) | ⭐ 3.7 | ~ |
| **spine-cpp** | Spine Runtimes License: **every developer integrating it needs their own Spine Editor licence** | — | ✓ | ⭐ 3.4 if you use Spine | ✔ |
| **Rive runtime** | MIT | Metal, Vulkan, D3D11/12, GL/WebGL | ✓ WASM | ○ 3.4 (free alternative) | ✔ |
| DragonBonesCPP | MIT | — | — | ✗ stale (no activity since 2018) | ✔ |
| **Effekseer** | MIT | DX9/11/12, Metal, Vulkan, GL, WebGL; EffekseerForWebGL is built with Emscripten | ✓ | ⭐ 3.10 | ✔ |
| **efkbgfx** (cloudwu) | MIT (check repo) | Effekseer renderer on bgfx; predefined materials work, **user-defined materials not supported** | follows bgfx | ⭐ with Effekseer | ✔ / licence ~ |
| **Basis Universal / KTX-Software** | Apache-2.0 | — | ✓ (transcoder) | ⭐ small web textures | ~ |
| stb_image / stb_image_write / stb_rect_pack | Public domain / MIT | — | ✓ | ⭐ (in use) | ~ |
| **glm** | MIT | — | ✓ | ⭐ 0.1 | ~ |

## 3. Text and UI

| Library | Licence | Notes | Verdict | Conf. |
|---|---|---|---|---|
| **Browser Canvas 2D** (no library) | — | TOMS `Font::buildFromCanvas` already renders glyphs with the browser's system font, **so no font download on web** | ⭐ 4.2 web | ✔ (in repo) |
| **FreeType** | FTL (BSD-style with a credit clause) OR GPL-2.0+ | the rasterizer for file fonts; ✓ Emscripten | ⭐ 4.2 desktop/mobile | ~ |
| **HarfBuzz** | "Old MIT" | shaping; ✓ Emscripten | ⭐ 4.3 (when Latin quality matters) | ~ |
| **libunibreak** | Zlib | UAX #14 line breaking (CJK) | ⭐ 4.3 | ~ |
| msdf-atlas-gen | MIT | offline SDF/MSDF atlases | ○ 4.4 | ~ |
| **RmlUi** | MIT | HTML/CSS-like UI; **`ninepatch` decorator**; data binding; custom render interface; Emscripten via its SDL + GL3 backend; renderers GL2/3, Vulkan, SDL GPU, DX11/12 | ⭐ L5 (decision card with "own widgets") | ✔ |
| Yoga | MIT | flexbox layout for our own widgets | ○ 5.1 | ~ |
| Clay | Zlib | single-header C layout | ○ 5.1 | ~ |
| NoesisGUI | commercial | XAML UI | ✗ cost | ~ |
| **Dear ImGui** | MIT | in-game debug overlays through bgfx's imgui backend (`examples/common/imgui`); dev builds only | ⭐ `ENGINE_DEBUGUI` (in use) | ~ |
| **ImGuizmo**, **ImPlot** | MIT | gizmos / plots inside the bgfx viewport overlay | ○ | ~ |
| imgui-node-editor, imnodes | MIT | node graphs | ✗ replaced by QtNodes in the Qt editor | ✔ |

### 3.1 Editor application (Qt, desktop only, never shipped)

| Library | Licence | Notes | Verdict | Conf. |
|---|---|---|---|---|
| **Qt6** (Widgets, Core, Gui) | LGPLv3 / GPL / commercial | editor host; `QWidget::winId()` gives the native handle bgfx renders into | ⭐ 9.1 | ✔ |
| **Qt Advanced Docking System** | LGPL-2.1 (commercial also offered) | VS-style docking, Qt5 + Qt6 | ⭐ 9.1 | ✔ |
| **QtNodes** (paceholder/nodeeditor) | BSD-3-Clause | node-graph editor, Qt5 + Qt6, maintained by its author in spare time | ⭐ 9.4 / O.4 | ✔ |
| Qt Charts | GPLv3 / commercial | profiler graphs in the editor (internal tool, so GPL is acceptable) | ○ | ~ |
| QtPropertyBrowser | LGPL (Qt Solutions) | unmaintained | ✗ build the inspector on `QTreeView` | ~ |

## 4. Object model, data, services

| Library | Licence | Notes | Verdict | Conf. |
|---|---|---|---|---|
| **flecs** | MIT | ECS + meta/reflection addon + JSON serializer + hierarchy; ✓ Emscripten | ⭐ O.1/O.2 (less code to write) | ~ |
| **EnTT** | MIT | ECS + `entt::meta` + `entt::dispatcher` / resource cache | ○ O.1/O.2 (more control) | ~ |
| Boost.Describe | BSL-1.0 | macro reflection, MSVC OK | ○ O.2 | ~ |
| reflect-cpp | MIT | C++20 reflection-based serialization, MSVC supported | ○ | ~ |
| refl-cpp | MIT | slow / stale | ✗ | ~ |
| **nlohmann/json** | MIT | in use | ⭐ 0.6 | ~ |
| **json-schema-validator** (pboettch) | MIT | JSON Schema on nlohmann | ⭐ 0.6 / 7.4 | ~ |
| **miniaudio** | Unlicense OR MIT-0 | in use; ✓ Emscripten (Web Audio) | ⭐ 7.6 | ~ |
| SoLoud | Zlib | ✓ Emscripten; slow-moving | ○ | ~ |
| FMOD | commercial | has a web build | ○ if budget allows | ~ |
| **inkcpp** | MIT | full ink 1.1 support; active | ⭐ 9.5 if Ink is chosen (Emscripten ?) | ✔ |
| Yarn Spinner | Yarn Spinner Public License (custom) | the official C++ runtime lives only in the **Unreal plugin, pre-release**; community C++/Rust runtimes exist | ✗ for now | ✔ |
| **LDtk** + LDtkLoader | MIT / Zlib | JSON with a published schema | ⭐ 9.3 | ~ |
| Tiled + tmxlite | editor GPL-2.0 (libtiled BSD-2) / Zlib | output is yours | ○ 9.3 | ~ |
| Lua 5.4 + sol2 | MIT / MIT | optional scripting | ○ 7.8 | ~ |
| AngelScript | Zlib | needs `AS_MAX_PORTABILITY` on WASM (slower) | ✗ for web | ~ |

## 5. QA and tooling

| Library | Licence | Notes | Verdict | Conf. |
|---|---|---|---|---|
| **doctest** / Catch2 | MIT / BSL-1.0 | both run through CTest, and VS Test Explorer lists CTest tests | ⭐ Q.1 | ~ |
| **Playwright** | Apache-2.0 | web end-to-end tests | ⭐ Q.3 | ~ |
| **Tracy** | BSD-3 | desktop profiling (CPU, GPU, memory). **Its client cannot instrument the Emscripten build** (open issues; no browser TCP). The viewer itself runs in WASM | ⭐ Q.5 desktop only | ✔ |
| sentry-native | MIT | desktop crash reports | ⭐ Q.5 | ~ |
| Live++ | commercial | C++ hot reload | ○ | ~ |
| MSVC C++ Hot Reload | built into VS 2022 | small edits in Debug | ⭐ | ~ |
| RenderDoc | MIT | GPU capture (desktop) | ⭐ | ~ |

## 6. Recommended starter stack

| Layer | Pick |
|---|---|
| Platform | SDL3 |
| RHI | **bgfx** via bgfx.cmake (D3D11/12 / Vulkan / Metal / GL; **WebGL2** on web) |
| Shaders | bgfx `.sc` → **shaderc** per backend |
| 2D / 3D assets | stb_image, bimg / **texturec** (KTX/DDS), fastgltf, meshoptimizer, ozz-animation ([11 §5](11_BGFX_QT_ARCHITECTURE.md#5-3d-models-what-bgfx-supports)) |
| Animation / FX | spine-cpp (if licensed) or Rive; Effekseer + efkbgfx |
| Editor | **Qt6** + Qt Advanced Docking System + QtNodes, bgfx viewport (desktop only, not shipped) |
| Text | Canvas 2D on web (existing), FreeType (+ HarfBuzz, libunibreak) elsewhere |
| UI | RmlUi (game UI) + Dear ImGui (in-game debug overlays only) |
| Object model | flecs (or EnTT) |
| Data | nlohmann/json + json-schema-validator |
| Files | SDL3 pref path / Storage + IDBFS on web, miniz packages |
| Audio | miniaudio |
| Content tools | LDtk (or Tiled), Blender, Ink/Inky |
| QA | doctest + CTest, Playwright, Tracy (desktop), sentry-native, RenderDoc |
| Packages | vcpkg manifest (+ FetchContent) |

**What this saves**, compared with the Build efforts on the cards (AI-assisted, rough):
- platform layer: L–XL
- RHI with every backend (bgfx): XL, and the largest saving. The 3D renderer and shadows are still built on it (L), starting from the bgfx examples
- UI widget library: L
- ECS + reflection + serializer: L
- glTF + skeletal animation: L
- particles: M
- Spine / 2D skeletal: XL if built

Together that is several months of work the project does not have to write or maintain.

## 7. Licence obligations

| Obligation | Libraries |
|---|---|
| **Credit in the game/credits screen** | FreeType (FTL) |
| **Paid licence per developer** | Spine Editor (for spine-cpp); Live++; FMOD (if used) |
| **LGPL** (editor only, never shipped to players) | Qt6, Qt Advanced Docking System: dynamic linking; obligations only apply if the editor is distributed outside the team |
| **Copyleft tools** (fine: the output is not covered) | Tiled editor (GPL), LibreSprite (GPL) |
| **Keep licence texts** | ship a `THIRD_PARTY_LICENSES.txt` generated from `vcpkg.json` (vcpkg installs copyright files per port) |
