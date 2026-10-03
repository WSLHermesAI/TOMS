# 15 — Node animations (`.anim`)

Animations built from the packed sprite atlas ([14](14_ATLAS_TOOL.md)): a tree of nodes, each with
position / rotation / scale / colour keys, sprite changes and events. Character moves, battle
scenes, simple effects. It replaces FM79979's MPDI (flat list of paths) with a node hierarchy:
moving a group moves everything in it, and the tree decides what draws on top.

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
  "version": 1,
  "atlases": ["../media/atlas/game.atlas", "fx/slash.atlas",
              {"id": "dark16", "path": "../styles/dark16/atlas/game.atlas"}],
  "clips": [{
    "name": "slime_attack", "length": 0.6, "playCount": 1, "stayAtLastFrame": true,
    "root": {
      "name": "slime", "sprite": "game:slime",
      "tracks": {
        "pos":    [{"t": 0, "v": [0, 0], "ease": "quadraticOut"}, {"t": 0.3, "v": [24, -8]}, {"t": 0.6, "v": [0, 0]}],
        "scale":  [{"t": 0.3, "v": [1.2, 0.8], "ease": "backOut"}, {"t": 0.6, "v": [1, 1]}],
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
- **Node:** `name`, `sprite` (absent = a group that only moves its children), rest values `pos`
  `rot` `scale` `color` `visible`, `pivot` (0..1 of the sprite, top-left = 0,0; absent = the
  atlas's `x_pivot`, else the centre), `order`, `blend` (`normal` / `add`), `inheritColor`, `children`.
- **Tracks:** `pos` `scale` (x, y), `rot` (degrees, clockwise, not wrapped: 0 → 720 is two turns),
  `color` (r, g, b, a 0..1) are interpolated; `sprite` `visible` `event` step. A channel without
  keys keeps the rest value. Before the first key it holds that key, after the last the last.
- **`ease`** on a key shapes the segment that key starts: `linear` (default), `stepped` (hold), or
  any of `quadratic` `cubic` `quartic` `quintic` `sinusoidal` `exponential` `circular` `bounce`
  `elastic` `back` + `In` / `Out` / `InOut` (https://easings.net), or a cubic Bézier
  `[x1, y1, x2, y2]` like CSS `cubic-bezier()`. `back`, `elastic` and `bounce` overshoot; colours are clamped.

**Space and order:** pixels, y down. A node's world transform = parent's × translate · rotate ·
scale; a negative scale mirrors. Draw order is depth first: children with `order < 0` (by order,
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
anim_editor --selftest x.anim outdir           automated check (with -platform offscreen); outdir must be on
                                               the atlases' drive, e.g. Build/anim_selftest
```

`--headless check` loads the file's `atlases` (relative to the `.anim`); each `--atlas` (repeatable,
relative to the working directory, id = its file name unless given as `id=path`) replaces that
list, in order. It reports an error per atlas that cannot be loaded; per sprite reference
`"id:name"` an error when no atlas has that id or that atlas lacks the name; per bare `"name"` an
error when no atlas has it and a warning when several do (only bare references: the first wins --
pick it in the Sprites dock or use Qualify Sprite References to store the atlas; qualified
references to such names are fine); and a warning per absolute atlas path.

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
| **Clips** | add / duplicate / rename / delete clips; length (0 = up to the last key), plays (-1 = loop), stay at last frame |
| **Nodes** | the node tree with each node's rest sprite (`name (id)`): add child / sibling / sprite node, duplicate, delete, rename (F2), drag & drop to reparent or reorder (list order = draw order among equal `order`), eye = rest `visible`, `order` column |
| **Sprites** | one tab per atlas (titled with its id and sprite count, in lookup order), each listing that atlas's sprites with thumbnails; the filter applies to every tab (the titles then show how many match); each row is one atlas's copy and picking it stores that atlas (`id:name`); a name in more than one atlas has a small badge ("also in X; each copy is usable"); the filter matches `id:name`; drag a sprite into the viewport (a node under the selected node, at the drop point) or onto a node; double-click = the selected node's sprite (a sprite key at the playhead with auto-key) |
| **Viewport** | the clip at the playhead; click selects the topmost node; **Move (W)** / **Rotate (E)** / **Scale (R)** gizmo. With **Auto-key (N, on by default)** a drag writes a key at the playhead, without it the rest value (a key exactly at the playhead is updated either way). Ctrl = pixel snap while moving, Shift = 15° steps / keep proportions; middle mouse pans, wheel zooms |
| **Properties** | clip settings; the **Atlases** list (above); the node's rest settings (name, sprite, pivot, order, blend, inherit colour, visible); the values at the playhead, each channel with a key button (◆ key here: click removes; ◇ animated: click keys the shown value; dotted: no track); the ease of the key at / before the playhead (any Tweeny ease, or Bezier… with a curve editor) |
| **Keys** | the MPDI-style key list (below) |
| **Events** | the selected node's event keys: add at the playhead, remove, edit time / name |
| **Timeline** | placeholder for the phase 4 multi-track timeline; takes dropped sprites (below) |
| **Problems / Log** | sprite references to an unknown atlas id or a name their atlas lacks, bare names in no atlas or (warning) in several, overlapping key times, keys past the clip's length, clip name clashes, atlases that do not load, absolute atlas paths (same checks as `--headless check`); the log shows opened / saved files and events fired during playback |

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

Every edit is one undo step (drags and spin box edits fold into one); `*` in the title marks
unsaved changes. The editor's `AnimEditor` object creates the document, viewport, docks, menus and
toolbars for a host window, which is what the phase 2 studio app will load as a plugin; the theme,
icons and canvas base come from `tools/studio_common`, shared with the atlas editor.
