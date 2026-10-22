# 15 — Node animations (`.anim`)

Animations built from the packed sprite atlas ([14](14_ATLAS_TOOL.md)): a tree of nodes, each with
position / rotation / scale / colour keys, sprite changes and events. Character moves, battle
scenes, simple effects. It replaces FM79979's MPDI (flat list of paths) with a node hierarchy:
moving a group moves everything in it, and the tree decides what draws on top.

**The game's renderer draws the editor views.** The atlas, anim and particle editors draw their
canvases with toms_game's own `BgfxRenderer` (`GameCanvasView` in `tools/studio_common`, library
`studio_bgfx`), so what an editor shows is pixel for pixel what the game shows:
- **Same rendering as the game:** point-sampled sRGB textures, straight-alpha and additive
  blending, the same sprite batch, and for particles the GPU simulation.
- **Game content** (the atlas page, the sprite being edited, the clip's sprites, the particles) is
  drawn by the renderer in the canvas's native window.
- **Editing visuals** (outlines, selection, handles, pivots, 9-slice guides, gizmos, labels,
  status text) keep their QPainter code and are drawn on top as a transparent overlay.
- **One bgfx per editor process.** It follows the canvas being shown: the atlas editor's page and
  sprite-edit canvases share it.
- **Coordinate hints** (View > *Show Coordinates*, on by default), as in Cocos Creator's scene view:
  faint lines with the x values along the bottom edge and the y values along the left edge. The
  step follows the zoom (1, 2, 5 × 10ⁿ content pixels, at least 90 screen pixels apart): every 500
  zoomed out, every 10 or every 1 zoomed in. Values are the game's coordinates (y grows downwards).
  With the mouse over the canvas, its position is shown too: "x, y" next to the cursor, faint guide
  lines through it, and its x and y as highlighted tags on the bottom and left edges among the values
  (tenths of a pixel when zoomed in 8× or more). Test: `studio.canvas_coordinates` (offscreen).
- **Undo / redo and backups** (all three editors):
  - **Undo / redo:** Edit > Undo / Redo (Ctrl+Z / Ctrl+Y, or the toolbar arrows) step through
    every edit.
  - **History dock** (View > History; it is a tab next to Properties): lists every step, oldest
    first. Click one to undo or redo up to it.
  - **Automatic backups:** a copy is written every 5 minutes (only when something changed) and
    after every 20 edits.
    - **Where:** `%LOCALAPPDATA%\TOMS\<Editor>ackups\<file>\<file>_<date-time>.<ext>`, never
      next to the file.
    - **How many:** the newest 20 per file are kept.
    - **Opening one:** atlas and image paths in a backup are absolute, so it opens from there.
      Save As puts it back. An atlas backup is a folder: the project and its packed atlas.
    - **Settings:** File > Backups > Settings (every N minutes, after N edits, how many to keep, on
      / off). The same menu has *Back Up Now* and *Open Backup Folder*.
- **QPainter fallback:** View > *Preview with the Game Renderer* (on by default; applies after a
  restart), the offscreen `--selftest`, a failed bgfx start, and a standalone build of the atlas
  tool (no game engine) all draw with QPainter as before.
- **`--selftest-gpu`** (on screen) checks each editor with the game renderer and saves bgfx
  screenshots:
  - `atlas_editor --selftest-gpu project.atlasproj outdir`: the page, sprite-edit mode and back;
  - `anim_editor --selftest-gpu clip.anim outdir`;
  - `particle_editor --selftest-gpu effects.particle outdir`.


**Status:** the runtime and file format (phase 1) and the editor `anim_editor` (phase 3, see
[Editor](#editor)) are done. Next: one plugin-based studio app holding both the atlas and the anim
editor (phase 2), and a multi-track timeline (phase 4).

**Writing clips by hand or with an AI** (fade in/out, moves, a step-in-swing-step-back attack):
see [16_ANIMATION_RECIPES.md](16_ANIMATION_RECIPES.md).

| Part | Where |
|---|---|
| data, `.anim` load/save, evaluation | `src/core/engine/anim_clip.h/.cpp` (`toms::anim`; `PoseCache`, `evaluate`) |
| playback + drawing | `src/core/engine/anim_player.h/.cpp` (`AnimPlayer`, `appendQuads`) |
| easing | `src/third_party/tweeny/easing.h` (Tweeny, MIT): Robert Penner's easings |
| editor | `tools/anim` (`anim_editor`, Qt 6), shared Qt code in `tools/studio_common` |
| tests | `anim_clip_test` (CTest `unit`), `smoke.anim` (screenshot), `anim.check_preview` |

## The file

```json
{
  "version": 2,
  "atlases": ["../media/atlas/game.atlas", "fx/slash.atlas",
              {"id": "dark16", "path": "../styles/dark16/atlas/game.atlas"}],
  "clips": [{
    "name": "slime_attack", "length": 0.6, "playCount": 1, "stayAtLastFrame": true,
    "root": {
      "name": "slime", "sprite": "game:slime",
      "tracks": {
        "pos":    [{"t": 0, "v": [0, 0, 0], "ease": "quadraticOut"}, {"t": 0.3, "v": [24, -8, 0]}, {"t": 0.6, "v": [0, 0, 0]}],
        "rot":    [{"t": 0, "v": [0, 0, 0]}, {"t": 0.3, "v": [0, 0, -10]}, {"t": 0.6, "v": [0, 0, 0]}],
        "scale":  [{"t": 0.3, "v": [1.2, 0.8, 1], "ease": "backOut"}, {"t": 0.6, "v": [1, 1, 1]}],
        "sprite": [{"t": 0, "v": "game:slime"}, {"t": 0.3, "v": "dark16:slime_bite"}],
        "event":  [{"t": 0.3, "v": "hit"}]
      },
      "children": [
        {"name": "shadow", "sprite": "game:shadow", "order": -1, "inheritColor": false},
        {"name": "fx", "sprite": "slash:slash_0", "blend": "add",
         "tracks": {"visible": [{"t": 0, "v": false}, {"t": 0.3, "v": true}, {"t": 0.45, "v": false}]}}
      ]
    }
  }]
}
```

- **`atlases`:** the `.atlas` files the sprites come from, **in lookup order**, each with an
  **id**. A plain path's id is its file name without `.atlas` (`fx/slash.atlas` → `slash`); an
  object `{"id": "...", "path": "..."}` names it otherwise -- needed when two atlases share a file
  name, like an art style's `game.atlas` next to the original's (written only when the id is not
  the default). Two atlases with one id are a parse error. Paths are always **relative to the
  `.anim` file**, so the `.anim` and its atlases live in related folders on the same drive; the
  editor never writes an absolute path. An older file's single `"atlas": "..."` is still read (as a
  one-entry list) and is written back as `"atlases"`.
- **Sprite references** (a node's `sprite`, the `sprite` track's values) are `"atlasId:name"` --
  that atlas's sprite, like MPDI's PIName + ImageName -- so one node can switch between sprites of
  different atlases key by key, and the same name can come from either atlas. A named atlas that
  lacks the sprite draws nothing (no fallback). A **bare** `"name"` (older files) is looked up in
  each atlas in turn, the first that has it winning. The editor writes every reference qualified.
  A game that draws with an `AtlasSet` that has no atlas with the id falls back to the bare lookup.
- **Clip:** `length` in seconds (0 or absent = up to the last key); `playCount` (-1 = loop forever); `stayAtLastFrame`.
- **Every node runs on the clip's one timeline.** A child's keys can start and end at other times
  than its parent's: after the parent's last key the parent holds its last pose and the child keeps
  animating (inside the parent's held transform). With `length` 0 the clip lasts until the last key
  of **any** node. A fixed `length` cuts every node off at that time: keys after it never play (the
  Problems dock warns: "key at …, after the clip's length").
- **Node:** `name`, `sprite` (absent = a group that only moves its children), rest values `pos`
  `rot` `scale` `color` `visible`, `pivot` (0..1 of the sprite, top-left = 0,0; absent = the
  atlas's `x_pivot`, else the centre), `order`, `blend` (`normal` / `add`), `inheritColor`, `loop`, `stayAtLastFrame`, `children`;
  in version 3 also `skin`, `lattice` and `meshGrid` (below, and [34_MESH_ANIMATION.md](34_MESH_ANIMATION.md)).
- **How a node plays** (each node on its own; its range is first..last key of its subtree):

  | Setting | After the node's last key | Use |
  |---|---|---|
  | default | holds its last pose: still drawn, nothing updates | parts that pop in and stay |
  | `"stayAtLastFrame": false` | hidden, with its children | a flash or burst that goes away |
  | `"loop": true` | plays first..last again, forever, **also after the clip has ended** | sparkles, a coin flying around, a bobbing arrow |

  - **Looping keeps going after the clip ends.** For a clip that plays once, the clip ends at its
    last key, as before: the game's `finished()` is true, and the other nodes hold or hide. The
    looping nodes keep playing for as long as the clip is shown (`AnimPlayer::poseTime()` keeps
    running).
  - **A looping group loops its whole subtree**, and the children's own settings apply inside the
    group's time.
  - **Event keys** fire on the clip's timeline only; they do not repeat with a looping node.
  - **Example:** the `popup` clip in [examples/anim_child_timing.anim](examples/anim_child_timing.anim).
- **Tracks:** `pos` `rot` `scale` as **x, y, z**, `color` (r, g, b, a 0..1) are interpolated; `sprite` `visible` `event` step. A channel without
  keys keeps the rest value. Before the first key it holds that key, after the last the last.
- **`ease`** on a key shapes the segment that key starts: `linear` (default), `stepped` (hold), or
  any of `quadratic` `cubic` `quartic` `quintic` `sinusoidal` `exponential` `circular` `bounce`
  `elastic` `back` + `In` / `Out` / `InOut` (https://easings.net), or a cubic Bézier
  `[x1, y1, x2, y2]` like CSS `cubic-bezier()`. `back`, `elastic` and `bounce` overshoot; colours are clamped.

**x, y, z:** position, rotation and scale each have three values, so the data is ready for a 3D
camera even though the game draws in 2D today.
- **Position:** pixels, y down, **z into the screen**.
- **Rotation:** degrees, not wrapped (0 → 720 is two turns). **z** turns the node in the screen
  plane (clockwise); **x** and **y** tilt it out of the plane. The order is z first, then y, then x
  (R = Rx · Ry · Rz), the same as the old engine's `cMatrix44::RotationMatrix`, so MPDI angles carry
  over unchanged.
- **Scale:** x, y, z; a negative x or y mirrors.
- **Drawing today:** the world transform is 3D (`NodePose::world`, a 4×4 matrix). The game and the
  editor draw it with an orthographic camera looking down z (`flatten()`): z does not move a sprite,
  and an x / y tilt foreshortens it (60° shows it at half size).
- **Older files:** version 1 files store `[x, y]` and a single `rot` number. They still load (z = 0,
  scale z = 1, the number is the z rotation) and are saved as version 2.

**Skins (version 3):** a sprite node can bend its sprite's triangle mesh with other nodes of the clip, its bones:
`"skin": {"bones": ["hip", "chest", "head"], "weights": "auto"}`. The mesh comes from the mesh file beside the sprite's
atlas (`game.atlas` → `game.mesh.json`, + `game.mesh.bin` when binary), shared by every clip that uses the atlas. The
anim tools add the meshes a clip needs to it: the editor when the clip is saved, or `anim_editor --headless meshes`
after an atlas is rebuilt or restyled. `"meshGrid": [cols,
rows]` (1–32 each) sets the grid the node's sprite is cut on; without it, 8-pixel cells. `"auto"` weights are made
from the rest pose; painted ones are `[[bone, weight, ...], ...]` per vertex. A file is saved as version 3 only when a
node has a skin, a lattice or a mesh grid, so other files still open in older builds. A skinned sprite without a mesh
draws as its quad (the check reports it as an error). In the editor it is the **Skin** field
under Properties (bone names, comma separated); under it a note says the sprite's mesh, its bones and weights, or
why it draws as a quad. **View > Show Skin** (Shift+K) draws, over every skinned node, its mesh, its points coloured
by the bones they follow (mixed by weight), and its bones with their names; select a bone and the points shade from
bright (follows it fully) to dark (not at all). Details: [34_MESH_ANIMATION.md](34_MESH_ANIMATION.md).

**Skin workflow in the editor.**
- **A rig is a sprite node with its bones under it** (`golem` > `hip` > `chest` > `head`). Moving the sprite then
  moves its mesh and bones together. A skin whose bones lie outside the node gets a warning in Properties and a
  **Move Bones Under This Sprite** button, which re-parents them without anything moving on screen.
- **Object mode (O)** and **Bones mode (B)**, on the toolbar and in the Edit menu, decide what a click in the viewport
  picks:
  - Object mode picks sprites, as before.
  - Bones mode shows the skin and picks only bones (a joint or a bone line), never the sprite under them. The
    Move / Rotate / Scale gizmo then works on the bone: with Auto-Key on it poses the bone (keys), with Auto-Key off it
    places the bone in the sprite (its rest pose).
- **Mesh grid** x / y (Properties, skinned or deformed nodes) sets the node's `meshGrid`: its sprite's mesh is cut
  again from its alpha on that many cells (1–32 each; without it, 8-pixel cells, so a 32-pixel sprite starts at
  4 × 4). More cells bend more smoothly. It is one undo step; the mesh is cut again at once and is added to the
  atlas's mesh file when the clip is saved, beside the other grids' meshes (`golem`, `golem@8x8`).
- **Lattice mode (L)** bends a sprite with its deform lattice (default 4 × 4; **Properties → Lattice** sets the size):
  drag a point and the deform is keyed at the playhead. The key list's **Deform** column lists the moved points
  (`5: 0, -2; 6: 0, -2`). See [34_MESH_ANIMATION.md](34_MESH_ANIMATION.md) §4.3.
- **Editing pauses playback.** A drag in the viewport or any edit (a typed value, a key) while the clip plays pauses it
  first, so with Auto-Key on the edit keys the frame on screen once instead of every frame the playhead passes.
- **The mouse wheel changes a number field only after you click it** (or Tab into it). Over an unfocused spin box,
  combo box or slider the wheel scrolls the panel around it instead (the Properties dock scrolls over its fields);
  scroll bars and lists always take the wheel. This holds in every Qt editor (`Theme.cpp`,
  `WheelNeedsFocus`).
- A hidden skinned node (rest `visible` off, a hidden parent, a visible key) shows no skin overlay and has no bones
  to pick.

**Space and order:** a node's world transform = parent's × translate · rotate · scale. Draw order is depth first: children with `order < 0` (by order,
then list position), the node's own sprite, then children with `order >= 0`. Colour multiplies
down the tree unless `inheritColor` is false; a hidden node hides its subtree.

## In the game

```cpp
toms::anim::AnimFile file;  toms::anim::parseAnim(text, file, &err);
toms::anim::AnimPlayer p;   p.play(file.find("slime_attack"));
// every frame:
p.update(dtMs);
for (const std::string& e : p.takeEvents()) { /* "hit": damage, sound ... */ }
p.draw(ren, game.spriteAtlas(), toms::anim::placement(x, y, scale));   // one atlas
// several: toms::anim::AtlasSet set; set.add(atlasA, "game", pageTexturesA); set.add(atlasB, "fx", pageTexturesB);
//          p.draw(ren, set, ...)  -- ids and lookup order = the file's "atlases"
```

Sprites of one atlas page share a texture (`Quad::texture`; the game's own sprite atlas is
`kSpriteAtlasTexture`), so a clip of any depth, rotated and scaled, still draws in a few batches
(`Quad` carries four corners for that; one more draw call only where the texture or additive
blending changes). Events fire once each, also across loop
wraps; events at time 0 fire on the first `update`.

**Only what has keys is updated.** Every node keeps its current position, rotation, scale,
colour, sprite and visibility (`PoseCache`, starting at its rest values), and each channel is
its own key array (`posKeys`, `rotKeys`, `scaleKeys`, `colorKeys`, `spriteKeys`, `visibleKeys`).
A frame advances only the arrays that have keys, each with a cursor (no search while playing
forward): a node with only sprite keys never recomputes its transform, a node without keys is
never touched after the first frame, and a track that holds a value (before its first key,
after its last, a stepped segment) costs one compare. A world transform / colour is rebuilt only
when the node's own values or a parent's changed, and `AnimPlayer` rebuilds a node's quad only
when its pose changed (or the placement / tint / atlas set did). `evaluate()` is the stateless
version (the editor's preview); the tests check that both give the same poses for any seek order.

**Try one:** `toms_game --anim=<file>#<clip>` plays a clip centred over whatever is on screen and
logs its events (`[anim] event hit`). `tests/smoke/anim_preview.anim` is the screenshot test's clip.

### Monster idle animations

Every monster on the map plays a looping idle clip instead of standing still. The clips are in
[`assets/media/anim/monster_idle.anim`](../assets/media/anim/monster_idle.anim), one per enemy, named
`idle_<enemy id>` (the id in `assets/data/enemies.json`): open the file in `anim_editor` to change one, or
add `idle_<id>` for a new enemy. The higher the enemy's level, the sillier its idle:

| Clip | Idle |
|---|---|
| `idle_slime` | slow squash-and-stretch breathing |
| `idle_bat` | a quick hover with wing-beat squeezes |
| `idle_skeleton` | sways, shivers, and chatters its teeth |
| `idle_golem` | heavy breaths, then a hop and a stomp that kicks up dust (fx `smoke`) |
| `idle_wraith` | floats, flickers and leans inside a pulsing purple glow (fx `glow`) |
| `idle_demon` | a little dance: two hops, turning to look the other way, sparks on each landing (fx `spark`) |
| `idle_demonlord_vorkath` | a belly laugh of quick bounces going red, a smug lean back, dizzy stars circling his head (fx `star`), a red aura |

How the game uses them (`Game::drawIdleAnim`, `game_scene_draw.cpp`):

- **Authoring box:** a clip is made around the monster's 32x32 sprite with the sprite's pivot at the bottom
  centre, at (0, 0); the sprite node sets `"pivot": [0.5, 1]`. Effects may reach outside the box a little.
- **Placement:** the game puts (0, 0) on the bottom centre of the monster's rect and scales the clip so the
  sprite fills that rect, exactly as the static sprite did. A 2x1 golem or a 2x2 Vorkath stretches the
  whole clip the same way.
- **Phase:** each monster runs at its own phase (from its tile), so a room of slimes does not breathe in step.
- **Atlases:** `game:` sprites come from the loaded game atlas, so the art style still applies; `fx:` sprites
  from `assets/media/atlas/fx.atlas`.
- **Fallback:** no file, no `idle_<id>` clip, or the runtime sprite grid (no prebuilt atlas): the monster is
  drawn as before.
- **Checked by** CTest `anim.check_monster_idle` (`anim_editor --headless check`).

### Doors opening

[`assets/media/anim/door_open.anim`](../assets/media/anim/door_open.anim) has one clip per door colour,
`door_open_yellow` / `_blue` / `_red` (0.55 s, played once): the key turns in the lock, the door jiggles,
squashes and sinks into the floor in a flash of its colour, with a few sparkles. When the player walks into a
door holding its key (`Game::openDoor`), the key is used, the clip plays on the door's tile, the tile becomes
floor, and the door is remembered as opened (`EntityStatus::Opened`): it stays open when the floor is visited
again and never costs another key. The player stays where they were on that turn. Placed like the idle clips
(bottom centre of the tile, scaled to the door sprite's size). CTest `anim.check_door_open`.

### The bow (weapon clips)

[`assets/media/anim/weapon_bow.anim`](../assets/media/anim/weapon_bow.anim) has a bow's four clips. There is no bow
art yet, so the bow is built from the fx atlas's plain shapes, tinted: a stretched `square` for the limbs, grip,
string, arrow shaft and fletching, `dot` tips, and `glow` / `ring` / `spark` / `smoke` / `star` effects. The bow faces
right with its grip at the clip's origin, about 95 px tall.

| Clip | Length | What it shows | Events |
|---|---|---|---|
| `idle` | 1.6 s, loops | the bow at rest with an arrow nocked, a slow bob, a glint on the top tip | |
| `charge` | 1.2 s, once, stays | the string draws back (0.6 s), trembling near full draw; then, for as long as the shot is held, the whole bow shakes with the strain, a gold aura pulses along it, sparks rise off the limbs and the arrowhead glows | `charged` at full draw |
| `attack` | 0.7 s, once | from full draw: the string snaps and rings, the arrow flies 400 px with a trail, a flash, the bow recoils | `release` at 0.06 s, `hit` at 0.42 s |
| `miss` | 1.0 s, once | from full draw: the arrow slips, tumbles short and drops, a puff of dust, the bow droops | `miss` at 0.08 s |

The file is written by `python tools/make_bow_anim.py`, which works out the string pieces' angles so the string always
meets the arrow's nock. Change the script and run it again, rather than editing the file by hand. No game screen plays
these clips on their own: the battle plays them at a character's hand (below). CTest `anim.check_bow`.

A looping node loops every key under it, so `charge` holds the bow twice: `drawing` (0–0.6 s, then hidden) and `held`
(shown from 0.6 s, the same pose inside `shake`, a looping group whose keys all lie in 0.6–1.0 s). The shaking goes on
after the clip ends without drawing the bow again. `STRAIN` in the script sets how hard it shakes.

### Characters and what they hold: two files, bound in code

A character's motion and the thing it holds are **two .anim files**, bound together by the game:

- [`assets/media/anim/archer_bow.anim`](../assets/media/anim/archer_bow.anim): **only the character** shooting a bow
  (`python tools/make_archer_anim.py`). Made with the `player` sprite, played by any character.
- [`assets/media/anim/weapon_bow.anim`](../assets/media/anim/weapon_bow.anim): **only the bow** (above).

Both have the clips `idle`, `charge`, `attack` and `miss`, timed alike. The character file's contract is two node
names:

```
archer
  lean            the body's motion from the feet: breathing, leaning back, recoil, flinch
    body          the character: pivot at the bottom centre, feet at (0, 0)      <- swapped per character
    hand          no sprite: where a held thing is drawn -- position, turn, scale <- moved per character
```

- **Binding:** `CharacterRig` (anim_player.h) plays a character clip and a held clip of the same name together.
  Every frame it draws the character, then the held clip at the character's `hand` node (`attachPlacement`: the hand's
  world transform), so the bow moves with the body's lean, recoil and shake. The hand's scale (0.32) draws the
  95-pixel bow at a 32-pixel character's size. Events come from both clips.
- **Any character:** `fitCharacter` (anim_clip.h) gives `body` another sprite and moves `hand` so it stays at the
  same place on that body: its height keeps its share of the visible body (head to feet), its x its distance from the
  body's right edge. Both are measured from the sprites' visible pixels (`visibleBox`, sprite_mesh.h). Every sprite of
  the game atlas is measured when the atlas loads. A character that still looks off gets a nudge in
  [`assets/data/character_fit.json`](../assets/data/character_fit.json): `"player_viking": {"hand": [0, -1]}`
  (pixels, x right, y down). An unlisted character needs nothing.
- **In battle:** a weapon in `items.json` names its two files, `"battleAnim": {"character": "anim/archer_bow.anim",
  "held": "anim/weapon_bow.anim"}` (the `bow` does). `"clips": {"idle": "ready", "charge": "raise", ...}` names the clip
  each status plays, for clips with their own names (the `sword` and `staff` do; a status not listed plays the clip of its
  own name). With it worn, the battle screen draws the player's character and
  the weapon in the player picture's place (the picture is hidden), on the clip the battle's status picks:

  | Status | When |
  |---|---|
  | `charge` | the attack bar waits for the tap (the bow held drawn and shaking, for as long as it waits) |
  | `attack` | a tap that hit, played to its end |
  | `miss` | a tap that dealt nothing, played to its end |
  | `idle` | the bar recovers for the next round, and after the fight |

  (`Game::updateBattleRig` / `drawBattleRig`, game_combat.cpp.) Scenarios: `battle_bow` (play it; its test taps once:
  `attack`), `battle_bow_charge` (`charge`) and `battle_bow_idle` (`idle`), [31](31_TEST_SCENARIOS.md).
- **See it:** `toms_game --anim=assets/media/anim/archer_bow.anim#charge --character=player_mage
  --held=assets/media/anim/weapon_bow.anim`. In the anim editor, **View > Preview as Character** shows the character
  file as another character (the view only; edits go to the clip as made).
- **Another weapon or action:** a character file with the same `body` / `hand` nodes and a held file with clips of the
  same names; a weapon names both in `battleAnim`.
- **Tested:** `unit.anim_character_test` (fitting every `player*` sprite; the held clip drawn at the hand and following
  its keys); CTest `anim.check_archer`; the editor self test previews the knight girl; the three scenarios.

## Editor

`anim_editor` (tools/anim/qt, built with the Qt editors) opens and saves `.anim` files. Its preview
evaluates the clip with the game's own `evaluate()` + `appendQuads()` and draws each quad as a
textured parallelogram with QPainter (additive = `CompositionMode_Plus`, tint = the sprite
multiplied by the colour), so it shows what the game draws.

```
anim_editor                                    the editor
anim_editor tests/smoke/anim_preview.anim      with a file open (Visual Studio: "anim_editor (preview clip)")
anim_editor --headless check x.anim [--atlas [id=]a.atlas]...
                                               parse + check sprites and keys; exit 0 ok (warnings allowed), 2 errors, 3 usage
anim_editor --headless meshes x.anim... [--atlas [id=]a.atlas]... [--default-atlas [id=]a.atlas]
            [--map from.atlas=to.atlas]... [--bin|--text] [--check] [--prune]
                                               add the clips' meshes to the mesh file beside each atlas they use
                                               (a.mesh.json); --prune: only what these clips need; --check: exit 1
                                               when a needed mesh is missing or stale (or, with --prune, extra);
                                               python tools/refresh_meshes.py runs it for every clip and art style
anim_editor --headless recipe x.anim --node <name> --recipe <id> [--clip <name>] [--start s] [--period s]
            [--strength k] [--out y.anim]      write a motion recipe's keys (docs/16 §7); exit 0 ok, 2 failed, 3 usage
anim_editor --headless recipe --list           the motion recipes
anim_editor --selftest x.anim outdir           automated check (with -platform offscreen); outdir must be on
                                               the atlases' drive, e.g. Build/anim_selftest
```

`--headless check` loads the file's `atlases` (relative to the `.anim`); each `--atlas` (repeatable,
relative to the working directory, id = its file name unless given as `id=path`) replaces that
list, in order. It reports an error per atlas that cannot be loaded; per sprite reference
`"id:name"` an error when no atlas has that id or that atlas lacks the name; per bare `"name"` an
error when no atlas has it and a warning when several do (only bare references: the first wins --
pick it in the Sprites dock or use Qualify Sprite References to store the atlas; qualified
references to such names are fine); a warning per absolute atlas path; and, reading the mesh file beside
each atlas, an error per skinned node's sprite that has no mesh in it.

**Sprite references in the editor.** Picking a sprite anywhere -- dragging it from the Sprites dock
into the viewport or onto a node, double-clicking it, Sprite Seq, the Properties sprite field, a
key list Sprite cell -- stores `"id:name"` for exactly the copy picked, including a name's copy in a
later atlas. Fields show references as `name (id)`; a bare legacy name shows as `name (auto: id)`
with the atlas the lookup picks now, and is left as it is until changed. Typing a bare name stores
it with the atlas the lookup picks. **Key > Qualify Sprite References** (also **Qualify** under the
Atlases list) turns every bare name in the file into `id:name` with the current lookup order, in
one undo step (names no atlas has stay bare).

**Atlases** (Properties dock, under Clip): the file's atlases in lookup order, each row its **id**,
the path as stored (relative to the `.anim`) and its sprite count, red when it did not load (the
tooltip says why). **+** adds one or several (the file dialog takes a multi-selection; also
**File > Add Atlas…**; files already in the list are skipped), **−** removes, **↑ / ↓** change the
lookup order (for bare names), **Qualify** stores the atlas with every bare name, **⟳** reloads
every atlas from disk (Ctrl+Shift+R); each change is one undo step.

- **Ids:** double-click an id to rename it: every `oldId:...` reference in every clip (node sprites
  and sprite keys) is rewritten in the same undo step. Ids are unique and must not be empty or
  contain `:`, `/`, `\`, `=` or spaces.
- **Adding** an atlas whose default id is taken (a second `game.atlas`, e.g. from
  `styles/dark16/atlas`) asks for an id, suggesting the folder above its folder (`dark16`); there
  are no silent duplicates. The same file twice is refused.
- **Removing** an atlas that references still use asks first, with their count: they become errors
  (Problems) until the atlas is back (undo) or they are changed.

Atlas paths are always stored relative:

- Adding an atlas to an **untitled** file first asks to save it ("Save the animation first: atlas
  paths are stored relative to the .anim file") and opens Save As in the atlas's folder; cancelled
  = the atlas is not added. An untitled file holds absolute paths only in memory (File > New keeps
  the current atlases), never in a saved file.
- **Save / Save As** rebase every atlas path onto the new location. When an atlas cannot be
  reached relatively (another drive), the save is **refused** with a message naming that atlas:
  save the `.anim` on the atlases' drive, or remove / replace that atlas. Adding an atlas from
  another drive to a saved file is refused the same way.
- A file that names an atlas with an **absolute path** still opens and loads it; Problems warns
  "absolute atlas path … stored relative on save", and the next save converts it (or refuses, as above).

| View | What it does |
|---|---|
| **Clips** | **double-click** (or Enter) opens a clip, shown in bold; when the open clip has changes since it was opened or saved, it asks *Discard all changes to “A” and open “B”?* (Yes discards, as one undo step; No stays); F2 or the toolbar renames; add / duplicate / rename / delete act on the highlighted clip; length (0 = up to the last key), plays (-1 = loop), stay at last frame |
| **Nodes** | the node tree with each node's rest sprite (`name (id)`): a **Playback** column (↻: click a row's icon for once and stay ▶ / once then hide ■ / loop ↻); a **Solo** column (◎: click a row's target to draw only that node and its children while the clip plays as usual: the parents still move them, their own sprites are hidden; click again, or open another clip, to end; preview only, not saved; the status line shows `SOLO: name`); add child / sibling / sprite node, duplicate, delete, rename (F2); **cut / copy / paste** (Edit menu or right-click: Ctrl+X / Ctrl+C / Ctrl+V pastes under the selected node, Ctrl+Shift+V after it) -- a node goes with its children, keys and skin, as text on the system clipboard (another editor window or a text file takes it); names the clip already has get `_2`, `_3`, ..., and a skin inside what is pasted (or duplicated) follows its own pasted bones, so a copied rig is a second rig; drag & drop to reparent or reorder (list order = draw order among equal `order`), eye = rest `visible`, `order` column |
| **Sprites** | one tab per atlas (titled with its id and sprite count, in lookup order), each listing that atlas's sprites with thumbnails; the filter applies to every tab (the titles then show how many match); each row is one atlas's copy and picking it stores that atlas (`id:name`); a name in more than one atlas has a small badge ("also in X; each copy is usable"); the filter matches `id:name`; drag a sprite into the viewport (a node under the selected node, at the drop point) or onto a node; double-click = the selected node's sprite (a sprite key at the playhead with auto-key) |
| **Viewport** | the clip at the playhead; click selects the topmost node; **Move (W)** / **Rotate (E)** (rot z) / **Scale (R)** gizmo. With **Auto-key (N, on by default)** a drag writes a key at the playhead, without it the rest value (a key exactly at the playhead is updated either way). Ctrl = pixel snap while moving, Shift = 15° steps / keep proportions; middle mouse pans, wheel zooms |
| **Properties** | clip settings; the **Atlases** list (above); the node's rest settings (name, sprite, pivot, order, blend, inherit colour, visible, **Playback**: once and stay / once then hide / loop); the values at the playhead (Pos, Rot and Scale as x, y, z; type x / y rotations here), each channel with a key button (◆ key here: click removes; ◇ animated: click keys the shown value; dotted: no track); the ease of the key at / before the playhead (any Tweeny ease, or Bezier… with a curve editor) |
| **Keys** | the MPDI-style key list (below) |
| **Events** | the selected node's event keys: add at the playhead, remove, edit time / name |
| **Timeline** | placeholder for the phase 4 multi-track timeline; takes dropped sprites (below) |
| **Problems / Log** | sprite references to an unknown atlas id or a name their atlas lacks, bare names in no atlas or (warning) in several, overlapping key times, keys past the clip's length, clip name clashes, atlases that do not load, absolute atlas paths, skinned sprites without a mesh (same checks as `--headless check`); the log shows opened / saved files and events fired during playback |

**Transport:** play / pause (Space), stop, loop preview, speed, time, a scrubber with the selected
node's key times (`,` / `.` jump between them). Playback is `AnimPlayer` (play count,
stay-at-last-frame, events once each).

**Sprite sequences by drag (MPDI style).** Select several sprites in a Sprites tab (Shift / Ctrl,
e.g. the frames of a sequence atlas) and drop them on a time area: the **scrubber** (start = the
time under the cursor, snapped to a key), a **Keys list** row (start = that row's time; below the
rows = the playhead) or the **Timeline** tab (start = the playhead). A dialog asks
"Insert N sprite keys on node '…'?" with the **start time** and the **time gap per key** (default
0.1 s; the last gap used is remembered), shows the resulting time range and how many existing
sprite keys at those times get replaced, and, when the last key is past a fixed clip length,
offers to extend it. **Yes** inserts one sprite key per sprite on the selected node, in list order
(one undo step); **No** changes nothing. Without a selected node it only says so.

**Key list.** One row per key time of the selected node (the union over its channels); the columns
Pos, Rot, Scale, Color, Sprite (`name (id)`, red when it does not resolve), Visible, Event show the key at that time or `·`, Ease the ease of
the row's interpolated keys (`mixed` when they differ). Selecting a row moves the playhead there;
several rows can be selected. Double-click a cell to edit it (an empty cell creates the key,
clearing a cell removes it; Time moves the row, Ease takes a name or `bezier(x1, y1, x2, y2)`).
Each channel is its own key array (`pos`, `rot`, `scale`, `color`, `sprite`, `visible`, `event`
tracks; a channel without keys keeps the node's rest value and is not stored), so a row only
holds the keys it needs: a node that only switches sprites has only sprite keys. No key action
creates a track you did not ask for: **Insert** keys the channel of the current column, or (no
column) the interpolated channels the node animates already (position for a node that animates
none). **Key animated channels together** (off by default): editing an interpolated cell also
keys the node's *other animated* channels at that time, to keep MPDI-style rows whole; it never
adds a new track.

| Tool | On the selected rows |
|---|---|
| Insert | keys at the playhead (the current column's channel, else the channels the node animates) |
| Delete | the rows' keys on every channel, or only the current column's channel (Del) |
| Set Time… | moves the rows, keeping their spacing; never puts two keys of a channel at one time (offers to shift the keys after them) |
| Even | spreads the rows evenly between the first and the last |
| Rescale… | scales every key time of the node, or of the whole clip (and its length), to a new duration |
| Ramp… | MPDI *AverageAssign*: a channel (or alpha) gets a value on the first and last row and is interpolated linearly by time between |
| Fade In / Out | alpha 0 → 1 / 1 → 0 across the rows |
| Sprite Seq | sprite keys cycling through the sprites selected in the Sprites dock (2 or more, in list order) |
| Ease… | the ease of every interpolated key in the rows |

**Key > Motion Recipe…** writes a short named motion for the selected node: lattice recipes (breathe, sway,
squash, jelly) on any sprite's deform lattice, bone recipes (marked "(bones)": bone sway, nod, swing) on the bones
of a skinned node. Pick the recipe, its Start (default: the playhead), Period and Strength; the recipe replaces the
keys it writes in that range, and applying it is one undo step. Loop the clip or the node for an idle that repeats.
See [16_ANIMATION_RECIPES.md](16_ANIMATION_RECIPES.md) §7.

Every edit is one undo step (drags and spin box edits fold into one); `*` in the title marks
unsaved changes. The editor's `AnimEditor` object creates the document, viewport, docks, menus and
toolbars for a host window, which is what the phase 2 studio app will load as a plugin; the theme,
icons and canvas base come from `tools/studio_common`, shared with the atlas editor.

## Converting MPDI (the old engine's animations)

**Convert → MPDI Folder to .anim…** in the editor converts every `.mpdi` under a folder, subfolders included. It
also converts the `.pi` sprite sheets they draw from into atlases (`.atlasproj` + `.atlas` + `.png` + `.plist`),
each one once. The results go next to the old files, or into another folder with the same subfolders. A report
lists the files written, what was approximated, and what could not be converted. **Open the First .anim** opens one
of the results. From the command line:

```bat
anim_editor --headless convert-mpdi <folder> [--out <dir>]
```

The code is `tools/anim/core/mpdi_convert.*`, shared by the menu and the command line; the `.pi` reader is the
atlas tool's. The rules follow the old engine's reader and renderer (`MPDIDataParse.cpp`, `SubMPDI.cpp`,
`Curve.cpp`):

| MPDI | .anim |
|---|---|
| the file, `ImageName` (its `.pi` files) | one `.anim`; one atlas per `.pi`, id = the `.pi`'s name |
| `MultiPathDynamicImage` | a clip: `TotalPlayTime` → length, `PlayCount` → playCount (-1 = forever), `StayAtLastFrame` → stayAtLastFrame |
| a path (`CueToStartCurvesWithTime`) | a node, keys at `StartTime` + each point's time |
| point position | the image centre: 1.1 and later store the top-left of the untrimmed image (+ half its original size); 1.0 and older store the centre. Linear |
| `LOD` 2 / 3 | the engine's smoothing (Chaikin corner cutting, LOD − 1 passes) baked into more position keys |
| `Size` / `Scale`, `Mirror` | scale = size ÷ the sprite's trimmed size; mirrored = negative x |
| `Angle` (z) / `Angles` (x, y, z) | rotation x, y, z, degrees (the same rotation order as the engine) |
| `Color` | colour (the oldest format's 0..255 → 0..1) |
| `PIName` + `ImageName` | sprite keys `"pi:image"`, stepped |
| `BehaviorUpdate` | size, rotation and colour linear between points; without it, stepped |
| `Loop` | the node loops; with a `StartTime`, a gate group keeps it hidden until then |
| `StartTime` > 0 | hidden until then |
| additive blending | destination `GL_ONE` or `GL_DST_ALPHA` → `blend: add` |
| `RotationPosOffset`, a non-centred `AnchorType` | a turning group with the image offset inside it, placed as the engine draws it |
| `HintPoint` | an event key (`sound:<name>` for sound hints) |

**Formats read:**
- the oldest files: no version, with `CurvePoint` / `CurveTime` lists and points as `w,h,angle,r,g,b,a,image,mirror`;
- 1.0 files that start with a `<Version>` element before `<MPDIList>`;
- versions 1.1, 1.2 and 1.3.

**Not converted, and reported as warnings:**
- a non-identity `CameraView`;
- `ViewPort` clipping;
- blend modes other than normal and additive.

**Checked on the old engine's own data** (`MagicTower/MagicSrc/FM79979`): 48 `.mpdi` found, 47 converted (312
clips, 3,220 paths), 76 `.pi` turned into atlases, in about 10 s.
- The one failure is a test file in an old editor build folder that names no sprite sheet.
- `--headless check` passes on 44 of the 47 results. The other 3 refer to images that are missing from their `.pi`
  files in the old data too, and the conversion reports them.

**Test:** `anim.mpdi_convert` (`tools/anim/tests/mpdi_convert_test.cpp`, 24 checks) converts the hand-made files
in `tests/mpdi/` and checks the keys against values worked out from the engine's rules. They cover:
- the oldest format and the `<Version>` header;
- top-left versus centred positions;
- trimmed scale and mirroring;
- Chaikin LOD;
- the start gate with a loop;
- additive blending and a sound hint;
- `CenterDown` and `RotationPosOffset`;
- `Angles` x, y, z.
