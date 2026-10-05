# Progress Report — Log Part 5 (2026-10-03 evening, 2026-10-05)

> Chronological log entries, oldest first. The live **Next Step**, status table, waiting list and
> open questions are in [PROGRESS_REPORT.md](PROGRESS_REPORT.md).

---

### 2026-10-03 (evening) — editor coordinate hints

Added after part 4 was written; committed by the owner in `0115a6a`.

- **What it does:** every editor canvas (atlas, anim, particle) shows x/y values along its bottom
  and left edges, with faint lines, as in Cocos Creator.
- **Step size:** follows the zoom, in steps of 1, 2 or 5 × 10ⁿ content pixels, at least 90 screen
  pixels apart: every 500 px zoomed out, every 10 or 1 px zoomed in.
- **Switch:** View > *Show Coordinates*, remembered per editor.
- **Code:** `tools/studio_common/CanvasView.cpp`.

### 2026-10-05 — anim editor, editor safety, glTF 3D in the engine, a 3D title

The owner asked for:
- per-node playback in the anim editor;
- easier clip switching and a solo view;
- automatic backups;
- then glTF support in the engine (skins, morphs, instancing, PBR), a viewer like
  gltf-viewer.donmccurdy.com, shadows, glTF cameras;
- and, as the first use in the game, a 3D scene behind the title.

Committed by the owner as `fedbd88` (anim editor), `306fb0f` (glTF render) and `8f26862` (title
scene). The publish to GitHub Pages is built and tested but not pushed (see the end).

**1. Anim editor** ([../15_ANIMATION.md](../15_ANIMATION.md), [../16_ANIMATION_RECIPES.md](../16_ANIMATION_RECIPES.md))

- **Children keep animating after their parent stops.**
  - This already worked; a new unit test proves it (`testChildOutlivesParent`).
  - The test file [../examples/anim_child_timing.anim](../examples/anim_child_timing.anim) shows
    it, and a fixed clip *Length* explains the "stops early" case.
- **Per-node playback**, saved in the `.anim` as `loop` / `stayAtLastFrame`:
  - **once and stay** (the default);
  - **once then hide**;
  - **loop**, over its subtree's key range.

  Looping nodes keep playing after the clip's own timeline ends, in the game (`AnimPlayer::poseTime()`)
  and in the editor (a preview time beyond the playhead). The test file's `popup` clip has all three.
- **The Clips dock:**
  - double-click (or Enter) opens a clip, and the open clip is shown in bold;
  - if the current clip has changes, it asks first: *Discard all changes to "A" and open "B"?* The
    discard is one undo step;
  - F2 renames;
  - deleting another clip keeps the open one.
- **Nodes tree:**
  - a **Playback** column (▶ ■ ↻, click to change);
  - a **Solo** column (◎): the viewport draws only that node and its children, in place, while the
    clip plays.
- **Selftests:** both anim editor selftests pass. The offscreen one has 107+ checks.

**2. Undo history and automatic backups** (atlas, anim and particle editors; `tools/studio_common/AutoBackup.*`)

- **Undo / redo:** already there. A new **History** dock lists every step; click one to go to it.
- **Backups:**
  - **When:** every 5 minutes if something changed, and after every 20 edits.
  - **Where:** `%LOCALAPPDATA%\TOMS\<Editor>\backups\<file>\`, never next to the file.
  - **How many:** the newest 20 are kept.
  - **Settings:** File > Backups (back up now, open the folder, settings).
  - **Atlas backups** are a folder (the project plus its packed atlas), written in the background;
    the project's real exports are never touched.
- **Tests:**
  - the anim selftest checks the edit count, the "keep" limit and the content;
  - the particle and atlas selftests check that a backup opens again.

**3. glTF 2.0 in the engine** ([../18_GLTF.md](../18_GLTF.md))

- **Loading** (`src/core/engine/gltf_model.*`, `toms_core`, C++17, no GPU):
  - through cgltf (MIT, fetched like the other dependencies);
  - meshes, PBR materials and textures (stb_image);
  - skins, up to 128 joints;
  - morph targets;
  - animations: linear, step and cubic spline;
  - cameras and `KHR_lights_punctual`;
  - `EXT_mesh_gpu_instancing`, `KHR_texture_transform`, `KHR_materials_unlit` / `emissive_strength`
    / `transmission` / `volume`.

  A file that requires Draco, meshopt or KTX2 is refused with a clear message. The `stb_image`
  implementation moved to its own file so tests can decode images without the game.
- **Drawing** (`src/engine/src/gltf_renderer.*`, new shaders `vs_mesh*`, `fs_mesh`, `fs_shadow`,
  `vs_line`/`fs_line`, for every backend):
  - **Materials:** PBR (GGX), normal maps (or a derived tangent frame), alpha mask / blend
    (sorted), glass as an approximation, ACES tone mapping.
  - **Animation and instancing:** GPU skinning, CPU morphing only when the weights change,
    instanced draws.
  - **Lights:** up to 4 lights: directional, point, spot.
  - **Shadows:**
    - one depth atlas: a tile per directional or spot light, 6 per point light;
    - 3×3 hardware PCF;
    - against acne, normal offsets plus a world-space step towards point and spot lights;
    - skinned, morphed and instanced meshes cast.
- **Bug found and fixed:** on Direct3D every model was lit from below.
  - **Cause:** the shader used `gl_FrontFacing`, which is backwards on D3D for glTF's
    counter-clockwise front faces.
  - **Found with:** the owner's chess comparison against the web viewer.
  - **Fix:** double-sided materials now flip by normal vs. view instead.
- **`gltf_viewer`** (`tools/gltf_viewer/`):
  - **Interaction:** drag & drop or Open; orbit / pan / zoom; arrow keys move the camera, + / −
    set its speed.
  - **Animations:** a checkbox per animation, time and speed sliders; morph sliders.
  - **Display:** grid, skeleton, bounds, wireframe, debug views (normals, base colour,
    metal/rough, UV, shadow).
  - **Lights:** an editor and rigs (default, point, spot, all), with gizmos; a ground plane to
    catch shadows.
  - **The file's cameras:** a list and the **C** key.
    - **Riding a camera:** left drag looks around and the wheel zooms, while the camera keeps
      following its animation; double-click looks ahead again.
    - **Leaving it:** right drag or the arrows switch to the free camera from the same spot.
  - **Panel:** model info, extensions, performance.
- **Test models:** self-made in `tests/gltf/` (`make_test_models.py`):
  - a skinned tube (linear and cubic spline);
  - a morph cube (linear and step);
  - 100 instanced boxes;
  - a PBR grid.

  `fetch_samples.py` downloads 12 Khronos samples to `Build/gltf_samples/`; all of them render
  correctly.

**4. The title screen's 3D background** (`src/game/src/title_scene.*`; [../18_GLTF.md](../18_GLTF.md) §3)

- **The scene:** `assets/media/models/VirtualCity.glb` (3 MB, copied from the old project's
  media) plays behind the title menu.
  - its animation loops;
  - it rides the file's 14 cameras with a cut every 10 s, horizons held level (several cameras
    bank 40–70°);
  - shadows on desktop; none on Android and the web.
- **Draw order:** `BgfxRenderer::setSceneViews()` orders the scene's bgfx views after the screen
  clear and before the sprites and the UI (`bgfx::setViewOrder`).
- **The menu over it:** the title page's opaque background becomes transparent only while the
  scene is drawn (`title.scene` → `body.scene`).
- **Option:** `toms_game --title-scene=<file|none>`.
- **Checked on:**
  - Direct3D 11, Vulkan and OpenGL: identical;
  - the web build in headless Chrome: the scene loads and renders through WebGL2.

**Verified:**

- **Unit:** `gltf_model_test` 52 checks; `anim_clip_test` 152 checks.
- **Screenshot tests (`smoke.gltf_*`):** 17, all passing.
  - **Skinning:** linear and cubic spline.
  - **Morph targets:** linear and step.
  - **Instancing, PBR, the normals view.**
  - **Shadows:** directional, point, spot, all three at once.
  - **Backends:** skin, PBR and all-shadows also on Vulkan and OpenGL.
- **`smoke.title`:** updated to the 3D title on purpose; it passes.
- **Web smoke tests:** 4 of 4 pass with the new build.
- **Full CTest:** 67 of 72 before the title reference was updated. The only failures were that
  title change and the 4 `smoke.stage*` (W16, failing since before 10-03).
- **Editor selftests:** pass, the particle one offscreen.

**Not verified:**

- The 3D title on Android and on a real phone browser.
- The viewer's mouse look-around by hand. It was tested with the same rotation through `--look=`.
- The new shadow / glTF shaders on D3D12.

**Still open:**

- **Publishing to GitHub Pages** (W18).
  - The package `8f26862-20261005145058` is built (`Build\dist\TOMS-web`, 17.7 MB) and passed the
    web tests.
  - The dry run shows only `gh-pages` changing.
  - The push needs the owner's GitHub sign-in: run `publish_web.bat nobuild` and answer **y**.
- **The legacy import bridge** (`.prt`/`.prtg` → `.particle`, `.pi`/`.mpdi` → atlas/`.anim`):
  still due.
- **Rendering not done yet:** HDR environment lighting (IBL) for glTF; cascaded shadow maps for
  large scenes; alpha-mask cut-outs in shadows.
- **The VirtualCity file** has white placeholder cubes (in the model itself).
