# Tutorial: skinning, deforming and weight painting in the anim editor

This walks you through bending a sprite in the anim editor, one step at a time:

- **Skinning**: the sprite bends with bones.
- **Deforming**: the sprite is squashed and stretched by a lattice.
- **Weight painting**: you choose which bone moves which part of the sprite.

You need no programming. The example uses the golem sprite that is already in the game's atlas. When you finish you
will have a golem that sways at the waist, nods its head and breathes.

How it works inside, and every option, is in [34_MESH_ANIMATION.md](34_MESH_ANIMATION.md). The basics of the editor
(clips, nodes, keys) are in [15_ANIMATION.md](15_ANIMATION.md).

---

## 0. Words used here

| Word | What it means |
|---|---|
| **Node** | One item of an animation: a sprite, or an empty "group" that other nodes hang under. The **Nodes** panel shows them as a tree. |
| **Bone** | An empty node that bends a sprite. A bone runs from its own point to the next bone under it, and is drawn as a coloured line. |
| **Skin** | The list of bones that bend a sprite. A sprite with a skin is **skinned**. |
| **Mesh** | The sprite cut into small triangles, so it can bend. The editor makes it for you from the sprite's picture. |
| **Weight** | How much a point of the mesh follows a bone: 0 = not at all, 1 = fully. Each point's weights add up to 1. |
| **Lattice** | A grid of handles over a sprite. Moving a handle squashes or stretches the picture around it. |
| **Key** | A value saved at a time on the timeline. The editor moves smoothly between keys. |
| **Playhead** | The current time, shown on the **Transport** bar at the bottom. Click or drag it to move. |
| **Auto-Key** (N) | On: every change you make writes a key at the playhead (you are *animating*). Off: changes set how things are at rest (you are *building*). |

Keys you will use a lot:

| Key | Does |
|---|---|
| O / B / L / P | Object, Bones, Lattice and Weights mode |
| W / E / R | Move, Rotate and Scale tool |
| N | Auto-Key on or off |
| K | Insert a key at the playhead |
| Space | Play / pause |
| F | Fit the view |
| Ctrl+Z | Undo |
| Right-drag, middle-drag or Space+drag | Pan the view; the mouse wheel zooms |

---

## 1. Open the tutorial's start file

The tutorial comes with two files in `docs\examples\tutorial\`:

| File | What is in it |
|---|---|
| `golem_start.anim` | where you begin: the golem sprite on a `stage` node (which shows it 4 times bigger), a 1-second looping clip `idle`, nothing else |
| `golem_done.anim` | where you end: the bones, the skin, painted weights, the sway, the nod and the breath. Open it whenever you want to compare |

1. Start the editor: `Editor\AnimEditor.cmd`, or `Build\windows-release\bin\anim_editor.exe`.
2. **Help > Open Tutorial Start File**. The golem appears, standing on the view's centre line.
3. **File > Save As…** and save it in the same folder as `my_golem.anim`. Now you work on your own copy, and the start
   file stays as it is for next time. Keep it in that folder: the file finds the game's atlas from there.

**Help > Skinning Tutorial** (F1) opens this page. **Help > Open Tutorial Finished File** opens the finished golem.

> **Starting your own animation from nothing?** **File > New**, **File > Save As…** (the editor stores the atlas's
> location next to the file, so save first), **File > Add Atlas…** and pick `assets\media\atlas\game.atlas`. Then
> drag a sprite from the **Sprites** panel into the view, and press **F2** to name its node.

---

## 2. Look around

1. In the **Nodes** panel you see `stage` and, under it, `golem`. Click `golem`: the **Properties** panel shows its
   sprite, position and the empty **Skin** field.
2. Press **Space**: nothing moves yet. The clip has no keys.
3. Turn the mouse wheel to zoom, and right-drag to pan. **F** fits the view again.

---

## 3. Make the bones

The bones go **under** the sprite in the Nodes tree: then moving the golem moves its bones with it. Each bone goes
under the one before it, so they make a chain:

```
golem          (the sprite)
  hip          (bone 1: the waist)
    chest      (bone 2)
      head     (bone 3)
```

1. Select `golem` in the Nodes panel.
2. **Node > Add Child Node** (Ctrl+Shift+N). Press **F2** and name it `hip`.
3. With `hip` selected, add another child and name it `chest`.
4. With `chest` selected, add another child and name it `head`.

The bones are empty nodes: they have no picture.

---

## 4. Skin the sprite

1. Select `golem`.
2. In **Properties**, type the bones into **Skin**, separated by commas: `hip, chest, head`. Press Enter.
3. Read the note under it. It should say something like *"Mesh: 25 points, … triangles. 3 bone(s), weights
   automatic."* That means the editor cut a mesh from the golem and worked out the weights itself.

If the note says something else:

| The note says | Do this |
|---|---|
| *No mesh for this sprite …* or *… rotated in its atlas …* | That sprite cannot bend; pick another sprite. |
| *Not a node of this clip: …* | A bone name is misspelled. Fix the Skin text. |
| A warning that the bones are outside the sprite | Click **Move Bones Under This Sprite**. Nothing moves on screen; the bones now sit under the golem. |

**Mesh grid** (also in Properties) sets how finely the mesh is cut. The default is one cell every 8 pixels. More cells
bend more smoothly. Try 6 × 8 to see the difference, then press Ctrl+Z: in this tutorial keep the default, because
the finished file's painted weights belong to the default mesh.

---

## 5. Place the bones (at rest)

This is where the bones sit when the golem is standing still. The automatic weights are worked out from these
positions, so place them with care.

1. Turn **Auto-Key off** (N). The Properties panel shows *auto-key OFF*.
2. Switch to **Bones mode** (B). The bones appear as coloured lines with a dot at each joint. In this mode a click
   picks only bones, never the sprite.
3. Turn on **View > Show Skin** (Shift+K). The mesh is drawn over the golem; each point takes the colour of the bones
   it follows.
4. Drag each bone's joint into place with the **Move** tool (W):
   - `hip`: low on the body, at the waist (the finished file: **Pos** 0, -6);
   - `chest`: higher, in the middle of the body (0, -8: 8 pixels above the hip);
   - `head`: at the neck (0, -8 above the chest).

   Each point's colour changes as you move the bones: that is the automatic weights following them. To type exact
   positions, type them into **Pos** in Properties instead. A bone's position is counted from the bone above it.

---

## 6. Animate the bones

Now make the golem move.

1. Turn **Auto-Key on** (N).
2. In the **Clips** panel, set the clip's **Length** to `1` second.
3. **A key at the start.** Put the playhead at 0. Select `chest` (click its joint) and press **K**. That saves its
   resting angle at 0 s.
4. **The bend.** Move the playhead to 0.5 s. Pick the **Rotate** tool (E) and drag the circle around `chest` to tilt it
   about 14°. A key is written at 0.5 s; hold Shift to turn in 15° steps.
5. **Back to rest.** Move the playhead to 1.0 s, select `chest` and type `0` into its **Rot** z in Properties.
   Auto-Key writes that as a key at 1.0 s.
6. Do the same for `head`: keys at 0 and 1 s, and about -12° at 0.6 s, a little after the chest, for a nod.
7. Press **Space** to play. Turn on **Loop Preview** in the Transport bar to keep it going.

**Shortcut:** **Key > Motion Recipe…** writes ready-made motions for you. Select `golem` and pick:

- `bone_sway` to sway the bones;
- `nod` to nod the last bone (the head);
- `swing` to swing them.

Each recipe is one undo step, so try them and undo the ones you don't like.

---

## 7. Fix the bend with weight painting

Play the clip and look where the golem bends badly: the waist folding like paper, the head pulling the shoulders
with it. Weight painting fixes that.

1. Move the playhead to the frame where the bend looks worst, and pause. You can paint at any frame; seeing the
   problem helps.
2. Switch to **Weights mode** (P).
3. Click the **joint** (the dot) of the bone you want to fix, for example `chest`. Its weights are shown as colours:

   | Colour | The point… |
   |---|---|
   | blue | does not follow this bone |
   | cyan, green | follows it a little / half |
   | yellow | follows it mostly |
   | red | follows it fully |

4. Move the mouse over the mesh. The text at the top left names the nearest point and its weights, for example
   *golem point 12: hip 0.62, chest 0.38*. It says *(automatic weights)* until you paint.
5. **Paint:**
   - **Drag** to make the points follow this bone more (the brush circle is orange);
   - **Ctrl+drag** to make them follow it less (blue circle). The weight you take goes to the nearest other bone;
   - **Shift+drag** to smooth: each point moves towards the average of its neighbours (grey circle). Use it to
     soften a hard edge between two bones.
6. **The brush:**
   - **`[` and `]`** make it smaller or larger; the **Brush** box on the toolbar sets an exact size;
   - **Strength** (0.01 – 1) is how much one pass changes. The dotted inner ring shows it. Start around 0.3 for
     gentle changes.
7. Check the result: switch bones by clicking another joint, play the clip (Space), and paint again where needed.
   Each stroke is one undo step (Ctrl+Z).
8. Changed your mind? **Edit > Reset to Automatic Weights** removes all the painted weights of that skin.

To see painted weights done, open the finished file (**Help > Open Tutorial Finished File**), press P and click each
joint: the feet follow only the hip, the waist is shared by the hip and the chest, the shoulders mostly follow the
chest (so they stay put when the head nods), and the top of the head follows only the head.

**Good habits:**

- Paint the bone that should **win** an area. If the head drags the shoulders, select `chest` and paint the
  shoulders red, rather than painting the head bone off them.
- Leave a soft change (green and yellow) between two bones. A hard blue-to-red edge makes a sharp fold.
- Paint **after** you are happy with the mesh grid. Changing the grid, or a new picture with a different shape,
  makes a new mesh, and the painted weights go back to automatic.

---

## 8. Squash and stretch with the lattice (deforming)

The lattice works on any sprite, skinned or not. It is good for breathing, squashing on landing, or jelly.

1. Switch to **Object mode** (O) and select `golem`.
2. Switch to **Lattice mode** (L). A grid of yellow handles appears over the golem.
3. In **Properties > Lattice**, choose the grid size. 4 × 4 is a good start; more handles give finer control.
4. **A key at the start, with nothing moved.** Put the playhead at 0. In the **Keys** panel, click a cell of the
   **Deform** column, then press **K**.
5. **The breath in.** Move the playhead to 0.5 s. Drag the top row of handles up a few pixels. A moved handle turns
   orange, and a Deform key is written at the playhead. (Lattice drags always write keys, whatever Auto-Key is set
   to.)
6. **The breath out.** Move the playhead to 1.0 s, click the Deform column again and press **K**: back to nothing
   moved.
7. Press Space to watch it breathe.

To fine-tune, the Keys panel's **Deform** column shows the moved handles as text, for example
`5: 0, -2; 6: 0, -2`, meaning handles 5 and 6 are moved 2 pixels up. You can edit that text directly.

**Shortcut:** **Key > Motion Recipe…** on the golem has lattice recipes too: `breathe`, `sway`, `squash`, `jelly`.

**Lattice and bones together:** the lattice bends the picture first, then the bones carry it. Drag the lattice
while the bones are at rest (at a frame where they are not turned); otherwise the handles do not line up with the
picture.

---

## 9. Save and use it in the game

1. **File > Save** (Ctrl+S). The editor also adds the golem's mesh to the atlas's mesh file
   (`assets\media\atlas\game.mesh.json` and `.bin`). The game reads the meshes from there; you don't do anything
   else.
2. Check the file:
   ```
   anim_editor --headless check docs\examples\tutorial\my_golem.anim
   ```
   It reports a misspelled bone, or a skinned sprite without a mesh.
3. After the atlas is rebuilt or restyled (new art), refresh every mesh in one go:
   ```
   python tools\refresh_meshes.py
   ```
   The painted weights stay as long as the new mesh has the same number of points; otherwise that sprite goes back
   to automatic weights, and you paint again.

---

## 10. When something looks wrong

| Problem | Why, and what to do |
|---|---|
| The sprite does not bend, it just moves | It has no skin (Properties > Skin is empty), or no mesh (read the note under Skin). |
| Bones mode shows nothing | Bones are shown only for skinned sprites. Skin it first (step 4). Also check the sprite is visible and not hidden by Solo. |
| Moving a bone moves its rest, not a key | Auto-Key is off (N). That is right while placing bones (step 5); turn it on to animate (step 6). |
| A click in Weights mode selects a bone instead of painting | You clicked on a joint dot. Start the stroke a little away from it. |
| Weights mode shows no colours | Select a bone of the skin: click its joint, or pick it in the Nodes panel. The colours show only skins that the selected bone is in. |
| Painting changed two sprites | The brush paints every skin under it that has the selected bone. Use **Solo** in the Nodes panel to show only one. |
| My painted weights are gone | The mesh changed (a new Mesh grid or new art), so they went back to automatic. Paint again. |
| The lattice handles do not sit on the picture | The bones are turned at this frame. Drag the lattice at a frame where the bones are at rest. |
