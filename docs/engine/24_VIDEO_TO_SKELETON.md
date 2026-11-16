# 24 — Character animation from video: text → video → skeleton → part atlas → `.anim` (plan)

中文：[zh_TW/24_VIDEO_TO_SKELETON.md](zh_TW/24_VIDEO_TO_SKELETON.md).

Status (2026-11-16): **step 1 (the video) is built in ComfyLive, in its window and headless** (§2.1); steps 2 to 6
are a plan. The owner's answers are in §6.

The goal: better animation for the player and the monsters. A local AI model makes a short video of one character
moving. The tools take the skeleton's motion out of the video, cut the character into parts (a part atlas, like
Spine's), and write a `.anim` clip. The anim editor then plays the clip beside the video, and the two show the same
motion.

## 1. Why

- Today's battle clips are posed by code from 4 paintings per weapon
  ([20_CHARACTER_ART.md](20_CHARACTER_ART.md) §5, `tools/art/make_hero_rig2d.py`). The owner's verdict on the first
  version was "a static picture with simple shaking"; the Spine-style rig is better but its motion is still invented
  by a script.
- A video model knows how a body moves: the weight shift before a strike, the follow-through, the cape's delay. The
  plan takes the **motion** from the video and keeps the **drawing** in our own rig.
- The playing half exists: bones, skinned meshes, the deform lattice and sprite swaps
  ([16_MESH_ANIMATION.md](16_MESH_ANIMATION.md), `.anim` version 3), and a cutter that splits a painted figure into 11
  parts along an OpenPose skeleton (`make_hero_rig2d.py`).

## 2. The pipeline

```
text prompt (+ a picture, later)
   |  step 1  ComfyLive, "graph": "video"                      AIGamestyle/ComfyLive
   v
frames (PNG, one character, fixed camera, flat background)
   |  step 2  DWPose on every frame, then clean-up              tools/art/video_skeleton.py
   v
skeleton.json (joints per frame) + an overlay GIF
   |  step 3  cut one rest frame into parts, pack               tools/art/video_parts.py
   v
<char>.atlas + .png (parts, pivots on joints)
   |  step 4  joints -> bone keys                               tools/art/video_solve.py
   v
<char>.anim (opens in the anim editor)
   |  step 5  reference layer + compare                         tools/anim
   v
"matches" or an error with the numbers
```

The script names are proposals. Everything a step writes goes to `Build/art_out/video/<char>/<clip>/`, except the
final atlas and `.anim`, which go to `assets/media/atlas/` and `assets/media/anim/`.

### Step 1: the video (ComfyLive)

- A new recipe kind in ComfyLive, used like the speech and sound-effect recipes
  ([23_SPEECH_AND_SOUND_EFFECTS.md](23_SPEECH_AND_SOUND_EFFECTS.md)):

  ```json
  {
      "graph": "video",
      "text": "a young archer in a blue tunic and a brown cape draws a bow and releases the arrow, full body, side view",
      "seconds": 3,
      "seed": 101
  }
  ```

  `ComfyLive.exe --make=hero_bow_attack.json --out=...` writes the frames as PNG files, a preview MP4 and the settings
  `.json` beside them.
- **Text only for now** (the owner). An optional `"image"` (the first frame, for example hero concept 606) comes
  later: with it the video shows the same person as the game's art. Without it every video is a new person, so
  **all the clips of one character must be cut from videos that look alike**, or the part set (step 3) is taken from
  one video and only the motion from the others.
- ComfyLive adds to every prompt what the later steps need: one character, the whole body in the frame, a fixed
  camera, a plain flat background, even light, no motion blur.
- **The model:** one that runs on the RTX 4070 (12 GB). First pick: **Wan 2.2 TI2V-5B** (about 10 GB, plus its text
  encoder and VAE; Apache 2.0; text → video and picture → video in one model, so the picture can be added later
  without another download). V0 checks it against what is current, that the installed ComfyUI can run it, and the
  licence, before downloading. The files go to `D:\Work\AI\models`; `tools\setup_ai_art.cmd` gets them for a new PC.
- Everything is local. No online video service is used (it would upload the project's pictures).
- The GPU is shared: ComfyLive already frees the other server's models before a run.

#### 2.1 Built (2026-11-16)

- **In the window:** `ComfyLive.exe`, **Built-in > Text to video (Wan 2.2 TI2V-5B)**, write the prompt, **Generate**.
  The video plays in a loop, with pause, one frame back and on, **Open frames folder** and **Save video...**.
- **Headless:** `ComfyLive.exe --make=examples\video_hero_bow_attack.json --out=Build\art_out\video\hero_bow\attack.mp4`
  writes `attack_frames\frame_0001.png ...`, `attack.mp4` and `attack.json` (the settings: open it to make the same
  video again). Three hero recipes are in `AIGamestyle/ComfyLive/examples/`: `video_hero_bow_idle`, `_attack`, `_hurt`.
- **The frames are the result** the next steps read; the MP4 (ffmpeg, `tools\get_ffmpeg.cmd`) is for looking at.
- **The check:** `ComfyLive.exe --video-test` (no ComfyUI): the recipe's rules, frames to PNG and back, the MP4.
- The full reference is `AIGamestyle/ComfyLive/README.md` "Video".

**Measured on this PC (RTX 4070, 12 GB):**

| Model | Size, length | Time | Peak VRAM | Licence |
|---|---|---|---|---|
| Wan 2.2 TI2V-5B | 1280 × 704, 49 frames (2 s) | 135 s (first run, with loading) | not measured | Apache 2.0 |
| Wan 2.2 TI2V-5B | 1280 × 704, 73 frames (3 s) | 212 s | not measured | Apache 2.0 |
| HyperFlow H3 Flash Q3_K_M | 640 × 352, 56 frames (2.3 s) | 35 s | 11.2 GB | MiniMax H3 Community License |
| HyperFlow H3 Flash Q3_K_M | 832 × 480, 73 frames (3 s) | 61 s | 10.5 GB | MiniMax H3 Community License |

- Both give one character, side on, on a plain grey background with a camera that does not move. The first Wan video
  had the figure about 120 px tall; the prompt's ending now asks for a figure that fills the frame, and it does.
- **HyperFlow H3 Flash** (the owner asked to try it, 2026-11-16) is the second built-in video graph
  (`"graph": "video-h3"`). It is faster here, at a smaller size. **Its licence does not cover the USA, the EU, the UK or
  South Korea and forbids showing its outputs there**, so nothing made with it goes into the game until the owner
  decides (a row in [../project/13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md)). The owner allowed test runs
  on this PC.
- **A reference picture as the first frame is built** (the owner asked for it, 2026-11-16): **Built-in > Picture to
  video** for either model, with a **Reference picture** box and **Browse...**; in a recipe, `"image": "<file>"`. The
  video takes the picture's shape. Tried with hero concept 606 (832 × 1216) and an idle prompt, 73 frames: Wan at
  800 × 1184 in 261 s, HyperFlow at 512 × 736 in 65 s; both kept the face, the clothes and the colours. The results
  are in `Build/art_out/video/hero_ref/`.
- **Remove background is built and on by default** (the owner, 2026-11-16): every frame is a PNG with only the
  character on a transparent background (the Inspyrenet node, per frame; about 40 s more for 73 frames at 512 × 736).
  `"remove-background": false` in a recipe, or the box in the form, keeps the background. Tried on the hero idle:
  the cut is clean around the hair, the hands and the cape, and the ground shadow is gone
  (`Build/art_out/video/hero_ref/idle_h3_cut_frames/`).
- **A second route from these frames (the owner's idea, 2026-11-16, not built):** pack the transparent frames into an
  atlas and play them as a picture sequence in a `.anim` clip (one node with a sprite key per frame), then tune it in
  the anim editor. It matches the video exactly and needs no skeleton, so it also suits monsters; it costs atlas
  space (a 3 s clip is 73 pictures) and a reskin has to repaint every frame. Still to write: a script that turns a
  frame folder into the atlas and the clip.
- **The reference pose can now be made from the concept** (2026-11-16): ComfyLive's **Edit pictures (Qwen Image 2.1
  Turbo)** turned concept 606 into the same boy seen from the side, standing ready with a bow, in 16 s
  (`Build/art_out/qwen/image_qwen_edit_hero_bow.png`). That model's licence is for research and evaluation only (Q99
  in [../project/13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md)), so the picture is a trial until the owner
  decides.
- **Reference pictures to video** (HyperFlow's ref2va model, 2026-11-16) does in one step what the two steps above
  do: from concept 606 alone it made the boy, seen from the side, drawing and releasing the bow
  (`Build/art_out/video/h3_test/ref_bow_frames`, 73 transparent frames in 88 s). The reference is not the first
  frame, so no side-on picture has to be made first. The same licence limit (Q98).
- Concept 606 faces the camera with the arms out and has no weapon. For the battle clips the reference should be a
  side-on ready pose holding the weapon, painted first (doc 20's pose script), since every clip starts from it.

### Step 2: the skeleton

- DWPose (already installed with `comfyui_controlnet_aux`) gives the body's joints in each frame, with a confidence
  per joint.
- Clean-up: a joint with low confidence is filled from its neighbours in time; left and right limbs that swap for a
  few frames are swapped back; the tracks are smoothed without rounding off a strike's fast frames.
- Output: `skeleton.json` (frame rate, picture size, the joints per frame) and `skeleton.gif` (the skeleton drawn
  over the video), which the owner can look at before going on.

### Step 3: the parts and the atlas

- **One part set** (the owner). The rest frame (the first frame, or the one closest to a neutral stance) is cut along
  its skeleton into the 11 parts of `make_hero_rig2d.py`: head, torso, cape, and near/far upper arm, forearm, thigh
  and shin. The cutter is reused, not rewritten.
- What a part hid in that frame (the torso behind an arm) is painted in by the installed SDXL model, so no hole shows
  when the arm moves away.
- Each part is centred on its joint and packed into `assets/media/atlas/<char>.atlas`. The meshes of the parts that
  bend are added by `anim_editor --headless meshes`, as for any other clip.

### Step 4: the clip

- Per frame: each bone's angle from its two joints, the root's position, and the bone's length as a scale (a limb
  pointing toward the camera looks shorter in the video; the scale follows it).
- The keys are reduced to those that matter (a tolerance in pixels), so the clip can be edited by hand in the anim
  editor and stays small.
- The clip is fitted to the battle's timers (attack 0.6 s, attack_big 0.8 s, …) and a looping clip (idle, charge) is
  closed so its last frame meets its first.
- The painted node is not named `body` (`Game::fitClipToCharacter`, doc 20 §3).

### Step 5: the anim editor plays it beside the video

- **A reference layer** (new): the video's frames drawn under the rig, following the playhead, with an opacity
  slider and a side-by-side switch. A small file beside the `.anim` names the frames' folder; the frames stay in
  `Build/` and are not shipped.
- **The compare** (new): `anim_editor --headless compare <file.anim> --clip <name>` draws the rig at each video frame
  and measures
  - the distance between each rig joint and the video's joint, as a share of the figure's height;
  - how much the rig's outline and the video figure's outline overlap.

  It writes a side-by-side GIF and the numbers.
- **If the rig cannot match, the work stops with an error** (the owner): the compare exits 1, names the worst frames
  and joints, and nothing is put in the game. There is no fall-back to playing the video's frames. The limits to
  start from (to be tuned on the first clips): mean joint distance over 3 % of the figure's height, or any joint
  over 8 %, or an outline overlap under 85 %.

### Step 6: in the game

The hero's weapon points at the new file in `items.json` `battleAnim`, as today. The battle's scenario tests for
that weapon run through `tools\test.cmd`.

## 3. What "the same as the video" means

- **The same motion, not the same pixels.** The rig is flat parts on bones. It follows the joints closely; cloth
  and hair move more simply than in the video.
- A body that turns toward the camera, or an arm pointing at the viewer, cannot be drawn by one flat part. The bone
  scale and the lattice cover small cases; a big one fails the compare, and the answer is then another video (a pose
  kept side-on), not more paintings, since the owner chose one part set for now.
- So the prompts ask for motion that stays in the picture's plane: a side view, no turning.

## 4. Order of work

| Phase | What is built | What the owner can judge | Stops when |
|---|---|---|---|
| V0 | the video model installed; ComfyLive's `video` recipe; 3 hero videos (bow idle, attack, hurt) | the videos | they are not good enough on 12 GB |
| V1 | step 2 | `skeleton.gif` on the 3 videos | the joints cannot be cleaned |
| V2 | step 3 | the part atlas and the rest-pose rig in the anim editor | — |
| V3 | steps 4 and 5 | the clips beside the videos, the compare's numbers | the compare fails (an error, §2 step 5) |
| V4 | the sword and the staff; `items.json`; the battle | the battle with the new clips | — |
| later | monsters; the picture as the video's first frame | | |

**The hero first** (the owner). Monsters wait: DWPose finds a human's joints, and the toad, the bear and the bun need
joints placed by hand on one frame and followed through the video, which is a different and less certain step.

## 5. Where things live

| What | Where | Note |
|---|---|---|
| The video recipe and its graph | `AIGamestyle/ComfyLive` (the owner) | the submodule is committed by the owner only |
| Skeleton, parts, solve | `tools/art/` in TOMS, Python, like the other art scripts | they read the frames ComfyLive wrote |
| Reference layer, compare | `tools/anim` (the anim editor) | with a test, selected by `tools/test_select.py` |
| Models | `D:\Work\AI\models` | downloaded, never uploaded |
| Work files | `Build/art_out/video/` | not in git |

## 6. The owner's decisions (2026-11-16)

| Question | Answer |
|---|---|
| The video's source | text, or a reference picture as the first frame (asked for and built 2026-11-16) |
| The video model | download one the RTX 4070 (12 GB) can run |
| Parts | one set for now |
| First character | the hero |
| When the rig cannot match the video | stop and show an error; no sprite-sheet fall-back |
| Where the video tool lives | `AIGamestyle/ComfyLive` |
| This document | in both languages |
