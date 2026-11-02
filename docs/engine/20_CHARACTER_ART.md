# 20 — Character art for the battle: painted key poses (B), and 3D (C) as the fallback

中文：[zh_TW/20_CHARACTER_ART.md](zh_TW/20_CHARACTER_ART.md).

The owner (2026-11-02, [../gameplay/17_WEAPONS_AND_PLAYS.md](../gameplay/17_WEAPONS_AND_PLAYS.md) Q58):
- **The player's look:** hero_m concept 606.
- **The approach:** a battle character must look dynamic, and one picture cut into parts and assembled in the anim
  editor is not enough. Try **B (painted key poses)** first. If it is not good enough, try **C (a 3D glTF model)**.
- **New PCs:** whatever is needed must be installable on a new PC (this page and `tools\setup_ai_art.cmd`).

## 1. The three ways compared

| | A. One picture cut into parts | B. Painted key poses | C. 3D glTF model |
|---|---|---|---|
| How | concept → parts → anim editor (the brawler, [18](18_NODE_EFFECTS.md) §5) | each pose its own painting of the same character, played as sprite keys with tweens and effects | concept → 3D mesh → rig → animations → the glTF renderer in battle |
| Dynamic directions | one facing only | any pose that is painted | fully free; camera close-ups |
| Fits the painted look | yes | best: every frame is a painting | needs a painterly or toon shader |
| Main risk | flat, paper-doll | details drift between paintings (IP-Adapter holds the face and costume) | rough AI mesh (hands, face, back); no local texturing; rigging and animating need new tools |
| New tools | none | none | Hunyuan3D-2 model, Blender (§4) |

## 2. B: what it needs

Everything is installed by **`tools\setup_ai_art.cmd`** (on a new PC: `setup_new_pc.bat` asks for it; see
[../project/09_NEW_MACHINE_SETUP.md](../project/09_NEW_MACHINE_SETUP.md)).

| Part | What | Where it goes |
|---|---|---|
| GPU | an NVIDIA card with 8 GB+ (this PC: RTX 4070, 12 GB; a picture takes about 20 s) | — |
| ComfyUI | the server, started by `AIGamestyle\_pipeline\tools\start_comfyui_with_flags.bat` (set `COMFY_EXTRA_ARGS=--disable-auto-launch` to skip its question) | `AIGamestyle\_comfyui\ComfyUI`, port 8188 |
| Models folder | ComfyUI reads models from the folder set in `ComfyUI\extra_model_paths.yaml` (this PC: `D:\Work\AI\models`) | — |
| Checkpoint | **Juggernaut XL v9** `Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors` (7.1 GB, Hugging Face RunDiffusion/Juggernaut-XL-v9) | `checkpoints` |
| ControlNet | ControlNet Union SDXL ProMax `controlnet-union-sdxl-promax.safetensors` (2.5 GB): the OpenPose skeleton puts the body in the pose | `controlnet` |
| IP-Adapter | `ip-adapter-plus_sdxl_vit-h.safetensors` (0.8 GB): keeps the same person and costume | `ipadapter` |
| CLIP Vision | `CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors` (2.5 GB), needed by IP-Adapter | `clip_vision` |
| Custom nodes | ComfyUI_IPAdapter_plus, comfyui_controlnet_aux, ComfyUI-Inspyrenet-Rembg (the background removal) | `ComfyUI\custom_nodes` |
| Python | Pillow and numpy for the scripts (`pip install pillow numpy`) | — |

All the models together (with the sprite workflow's) are about 20 GB. Model **downloads** are fine under the project's
rule; nothing of the project is uploaded. Online 3D services (Meshy, Tripo, Rodin nodes in ComfyUI) are **not** used.

## 3. B: how it is made

1. **Pick the look:** `python tools/art/make_brawler_art.py concept --char hero_m --seeds 505,606,707,808`. The pictures
   are in `Build/art_out/hero_m/`. The owner picked 606.
2. **Paint the poses:** `python tools/art/make_hero_poses.py --weapon bow --seeds 33,44`. For each pose of the weapon
   (`POSES` in the script), a skeleton drawn for that pose and the chosen concept as the IP-Adapter reference paint one
   picture per seed. The output is `Build/art_out/hero_bow/<pose>_<seed>.png`, a `_cut.png` with the background
   removed, and `sheet.png`.
3. **Pick the best seed per pose**, and repaint a weak pose with new seeds (`--pose attack --seeds 55,66`).
4. **Make the clips:** `python tools/art/make_hero_anim.py --weapon bow --pick idle=55 --pick charge=44 --pick miss=44
   --pick attack_big=44`. Every picked pose is cut with one shared frame (so the feet stay on one line), packed into
   `assets/media/atlas/hero_bow.atlas`, and played as sprite keys with short tweens (squash, lunge, recoil) in
   `assets/media/anim/hero_bow.anim`: idle, charge, attack, miss, attack_big, hurt. The bow's `battleAnim` in
   `items.json` is `{"character": "anim/hero_bow.anim"}` (no held weapon: it is painted). The clips open in the anim
   editor for fine tuning; the weapon effects and hit flash come from the battle as before.
   - A pose that does not paint well is made from a good one: the release is the full draw with a snap and a recoil;
     the hurt is the ready pose tilted back.
   - The painted node is **not named `body`**: the battle puts the player's map sprite on a node `body`
     (`Game::fitClipToCharacter`, for the clips any character wears), which would hide the painting.
   - Clip lengths follow the battle's timers, not the art: `attack_big` is 0.8 s like the other bow clips
     (`tests/scenarios/battle_bow_idle` expects the bow lowered by then).

**Results so far (the bow, 2026-11-02):**

| Pose | Painted well | Pick |
|---|---|---|
| idle (ready) | yes | 55 or 66 |
| charge (full draw) | yes | 44 |
| miss (stumble) | yes | 44 |
| attack_big (leaping power shot) | yes | 44 |
| attack (release) | no: after three rounds the bow lands in the wrong hand | made from charge 44 (step 4) |
| hurt (recoil) | no: the figure keeps aiming | made from idle (step 4) |

The same person shows in every pose. Some poses (a release, a recoil) are hard to get from a skeleton and a text prompt.
In a real battle (2026-11-02) the hero draws in all the bow's states, and battle_bow, battle_bow_charge and
battle_bow_idle pass with these clips.

## 4. C: what it would need (if B is not good enough)

The owner judged B not good enough (2026-11-02: "a static picture with simple shaking") and chose C.

| Part | What | Note |
|---|---|---|
| Image to 3D | **Hunyuan3D-2** shape model `hunyuan3d-dit-v2-0.fp16.safetensors` (4.9 GB, Hugging Face tencent/Hunyuan3D-2, Tencent Hunyuan community license), in the models folder's `checkpoints`, run by ComfyUI's built-in Hunyuan3D nodes; installed by `tools\setup_ai_art.cmd` | makes a mesh from one picture; the built-in nodes give no texture |
| Texture | Hunyuan3D-2 Paint, or baking the painting onto the mesh | the paint model needs more GPU memory; check on 12 GB first |
| Rigging and animation | **Blender** (free; `winget install BlenderFoundation.Blender`) | an auto-rig add-on, then the clips by hand or from a licensed local animation set |
| In the game | the battle draws the glTF fighters with the existing glTF renderer (skinning, PBR), between the backdrop and the UI, with a painterly or toon shader | the title scene already draws glTF |

**The trial (2026-11-02):**

```
python tools/art/make_hero_3d.py --seed 1
Build\windows-release\bin\gltf_viewer.exe Build\art_out\hero_3d\hero_1.glb
```

The script centres concept 606's figure on a white square, has ComfyUI make the shape (about 40 s on the RTX 4070:
142k vertices, 383k triangles), and colours each vertex with the concept's colour seen from the front (COLOR_0).

- **Shape:** good. The body, head, hair, cape, belt, pouch and boots all have depth; the cape is a separate sheet
  behind the body.
- **Colours (first try):** right from the front only; the side and the back were streaks.

**Colours on every side (2026-11-02, the owner: "ok do it").** The script now paints the other sides with the
installed models (no new download): the mesh's depth seen from the back, each side and the back-left (`VIEWS`) makes
ControlNet Union (depth mode) paint that view on the same body, with IP-Adapter (concept 606) for the person and the
clothes. Each vertex blends the views that see it (the concept for the front), weighted by how squarely each view
sees it; unseen vertices take their nearest neighbour's colour; the colours are averaged with the neighbours twice.
The pictures are `Build/art_out/hero_3d/view_<name>_<seed>.png`; the four views from all sides are
`Build/art_out/hero_3d/views.png`.

What it took:
- **The cape's colour:** the painted views put the concept's blue tunic on the cape until the prompt weighted it
  (`CAPE`, `(…brown leather cape…:1.5)`) and IP-Adapter went down to 0.6. The seeds that paint it brown are the
  defaults (`--view-seeds 9,9,9,8`); the back-right view painted a face, so it is left out.
- **The cape's speckles:** the cape is thinner than the shape's grid, so the surface net leaves faces wound both ways
  and edges shared by 3-4 faces. The normals ignore the winding (the main axis of the faces' normals, turned outward)
  and the faces are rewound to agree; a sheet seen by a view takes that view's colour on both of its faces.
- **The surface:** 10 rounds of Taubin smoothing (`--smooth`).

Still rough: a light patch on the forehead (from the front projection), soft detail on the back (the painted views
are blended), and 383k triangles.

**Skeleton and clips (2026-11-02, the owner: "yes").** Blender 5.2 LTS installed with
`winget install BlenderFoundation.Blender` (the script finds it under Program Files, or set `TOMS_BLENDER`).

```
python tools/art/make_hero_rig.py            (about 15 s)
Build\windows-release\bin\gltf_viewer.exe Build\art_out\hero_3d\hero_rigged.glb --anim=draw
```

`make_hero_rig.py` puts the concept's OpenPose joints (`make_brawler_art.JOINTS`: the concept was painted on them) on
the mesh, x and y from the picture, the depth from the mesh, plus a chain down the back of the cape
(`joints.json`). Blender (`tools/art/hero_rig_blender.py`, headless) then:

1. **Cleans and cuts the mesh down:** a voxel remesh (the surface net's mesh has edges shared by 3-4 faces, which
   cannot be decimated), the colours copied back, decimated to about 38k faces, small loose pieces deleted (with them
   the automatic weights fail).
2. **Bakes the colours into a texture** (2048², `hero_rigged_colour.png`, packed into the GLB): the cut-down mesh is
   unwrapped and the full mesh's colours baked onto it (Cycles). Vertex colours on 20k vertices blurred the face.
3. **Builds the skeleton** (24 bones: hips, spine, chest, neck, head, arms, legs, 3 cape bones, the string) and
   **skins** the mesh with Blender's automatic weights.
4. **Adds the bow** in the left hand (rigid) and its string, whose middle follows the right hand while drawing.
5. **Poses the clips** by aiming bones (a small two-bone IK for the arms), bakes them, and exports one glTF animation
   per clip: `idle` (2 s, breathing), `draw` (1.8 s: raise, full draw, hold, release, back to idle), `charge` (1.2 s,
   held at full draw with a tremble).

The archer stands with the chest to the camera and shoots toward +x, the screen's right, where the enemy stands in
battle. GIFs of the clips: `Build/art_out/hero_3d/draw.gif`, `idle.gif`, `charge.gif`.

Still rough: the face is soft (the colours come from the 832 × 1216 concept, where the face is about 150 px
wide), the head turned at full draw shows mostly hair, and the clips are posed by code (they can be refined in
Blender by hand: open the GLB).

**Not done yet:** drawing the glTF fighter in the battle (the existing glTF renderer, between the backdrop and the UI),
the sword and staff clips, a sharper face (project the concept straight into the texture, or paint a close-up of the
face).
