# TOMS editors

中文：[zh_TW/README.md](zh_TW/README.md) · For an AI making assets: [AI_GUIDE.md](AI_GUIDE.md)

Ready-to-run editors for TOMS. Nothing has to be built or installed: double-click a launcher in this folder.

| Launcher | Program | Makes |
|---|---|---|
| `AtlasEditor.cmd` | `bin\atlas_editor.exe` | sprite atlases: many PNGs packed into one texture (`.atlasproj` → `.atlas` + `.png` + `.plist`) |
| `AnimEditor.cmd` | `bin\anim_editor.exe` | node animations (`.anim`): sprites moved, turned, scaled and coloured on a timeline |
| `ParticleEditor.cmd` | `bin\particle_editor.exe` | particle effects (`.particle`): fire, sparks, smoke, magic |
| `FontEditor.cmd` | `bin\font_editor.exe` | picture fonts (`.fnt` + `.png`) from one picture per character |
| `GltfViewer.cmd` | `bin\gltf_viewer.exe` | views 3D models (`.gltf` / `.glb`): animations, lights, cameras, physics |
| `UiEditor.cmd` | `bin\ui_editor.exe` | the game's screens (`.rml` / `.rcss`) beside the running game, every change shown as you type |
| `StageEventEditor.cmd` | `bin\stage_event_editor.exe` | the stage (floor map) and event editors as one Windows app, on this repo's `assets\data` |
| `WebEditors.cmd` | `..\tools\web_editors\serve.cmd` | the stage (floor map) and event editors, in the browser |
| `Tools.cmd` | | a command prompt with `atlaspack`, `fontpack` and the editors on PATH |

**Language:** every Qt editor has a **中文 / English** button at the right of its menu bar. The choice is kept for all
of them.

## 1. Before you start

- **Windows 10 or 11, 64-bit.** The Microsoft C++ runtime is included in `bin`, so nothing else has to be installed.
- **The repo must be cloned with Git LFS.** The programs in `bin` are stored in Git LFS. Run `git lfs install` once
  on the PC **before** cloning (or run `git lfs pull` after). Without it the `.exe` and `.dll` files are small text
  files, and Windows reports *"This app can't run on your PC"*.
- **Keep the folder inside the repo.** `UiEditor`, `StageEventEditor`, `WebEditors` and the particle editor's default fx atlas use
  `..\assets` and `..\tools`. The other editors only need `bin`; that folder can be copied anywhere on its own.
- **WebEditors needs Python 3** on PATH (it starts a small local web server) and Chrome or Edge.
- **GltfViewer and UiEditor need a Direct3D 11 graphics card** (any PC from the last ten years).

## 2. The editors

Each section is the short version; the linked document has every detail.

### Atlas editor — sprites into one texture ([docs/engine/04](../docs/engine/04_ATLAS_TOOL.md))

1. **File → New**, then **Add Images…** (PNGs or whole folders; or drop them on the window).
2. Set the packing on the right: max page size, **padding** (empty pixels between sprites), **extrude** (edge pixels
   repeated around each sprite so scaling never shows a neighbour's colour; 1 is enough unless sprites are drawn much
   smaller), **trim** (cut transparent edges; the sprite still draws at its original size), power of 2.
3. Optional per sprite: pivot, 9-slice split, tags, **child sprites** (a rectangle inside another sprite, no pixels
   copied).
4. **Save** writes `NAME.atlasproj` + `NAME.atlas` + `NAME.png` + `NAME.plist`. The PNGs you added are stored inside
   the packed texture, so the originals are not needed afterwards.

- **Change art:** edit the PNG in the project's reference folder, then **Import From References** (it lists the PNG as
  *changed*), and Save.
- **Bring in an old atlas:** **Import Atlas** reads `.pi`, `.atlas`, TexturePacker `.json` and Cocos `.plist`.
- The game's own atlas is `assets\media\atlas\game.atlasproj`.

### Anim editor — node animations ([docs/engine/05](../docs/engine/05_ANIMATION.md), recipes: [docs/engine/06](../docs/engine/06_ANIMATION_RECIPES.md))

1. **File → New**, then **Add Atlas…** for each atlas the sprites come from.
2. **Add Sprite Node** (or drag a sprite from the Sprites dock into the view) to make a node. Nodes form a tree; a child moves with its parent.
3. Pick a time on the timeline, change position, rotation, scale, colour or the sprite, and **Insert Key at Playhead**. Each key can
   have an **ease** (how the move speeds up and slows down).
4. Press play, or double-click a clip to play it. **Save** writes the `.anim`.

- Per node: **loop** (keeps playing, also after the clip ends), **stay at last frame** off (the node disappears after
  its last key), **blend add** (glows).
- **Event keys** (for example `hit`) tell the game when to deal damage or play a sound.
- **Convert → MPDI Folder to .anim…** brings in the old engine's `.mpdi` animations.
- The **Problems / Log** dock lists missing sprites and keys past the clip's end.

### Particle editor — effects ([docs/engine/07](../docs/engine/07_PARTICLES.md))

1. **File → New**, or open `docs\examples\fx_recipes.particle` (11 ready effects: torch fire, hit sparks, heal, snow…)
   and change one.
2. **Add Effect**, then **Add Emitter** (or **Add Preset**). An effect holds emitters. Per emitter: emission (rate per second and bursts), shape (point, line, box,
   circle, ring), life, direction and spread, speed, size, colour, the sprite, curves *over life* (colour, size,
   speed, spin), forces (gravity, drag, radial, orbit), a flipbook, and the blend (`add` for fire and light, `normal`
   for smoke and dust).
3. The preview plays all the time; drag the effect in the view to see it move.
4. **Save** writes the `.particle`. The **Problems / Log** dock lists what the game would refuse.

The default sprites are the fx atlas (`assets\media\atlas\fx.atlas`: dot, glow, spark, star, ring, smoke, flame).

### Font editor — picture fonts ([docs/engine/10](../docs/engine/10_FONT_TOOL_GUIDE.md))

1. **Add pictures…** (one PNG per character, or drop them on the window). Files named `0.png`, `K.png`, `dot.png`,
   `comma.png`, `space.png` get their character by themselves.
2. Type each picture's character in the **Character** column. Red cells (no character, or one used twice) must be
   fixed before saving.
3. Type a **test text**: the preview draws it the way the game will. Click a glyph in the **combined picture** to find
   its row.
4. Set the **page layout** (one row, tight, or tight power of 2), the **space width** and the **line height** (both
   start on *auto*).
5. **Save font as…** writes `NAME.fnt` + `NAME.png`. **Open font** loads an existing font to change it.

### glTF viewer — 3D models ([docs/engine/08](../docs/engine/08_GLTF.md))

Drop a `.gltf` / `.glb` on the window (or on `GltfViewer.cmd`).

| Input | Does |
|---|---|
| left drag / right or middle drag / wheel | orbit / pan / zoom |
| `Space` `F` `G` `K` `B` `W` `R` `H` | play-pause, frame, grid, skeleton, bounds, wireframe, auto-rotate, panel |
| `1`..`9` | play only that animation |
| `C`, or the camera buttons | the file's cameras (then back to the free camera) |
| click a mesh, then `T` / `E` / `S` | select its node, then move / rotate / scale it (not saved to the file) |

The panel shows what the model uses (green: drawn, orange: ignored), animations, morph targets, the file's lights or a
light rig, shadows, and physics.

### Toms editor — the game in a window

The game running in an editor window, with the event flow and the old stage list beside it. It reads this repo's
`assets` (the launcher points it there).

### UI editor — the game's screens ([docs/gameplay/01](../docs/gameplay/01_RMLUI.md) "The UI editor")

The UI files on the left, the text in the middle, the real game on the right. Type, pause, and the game shows the
change; nothing on disk changes until **Ctrl+S**. **Preview** picks the screen (the title, or a test scenario: a battle,
a dialogue, the store, ...), **Language** checks long text, **Inspect** (Ctrl+I) + a click in the game opens the
element's line and lists the style rules that reach it. Needs a Direct3D 11 graphics card.
New to it? **Help > UI Editor Tutorial** (F1, [docs/gameplay/10](../docs/gameplay/10_UI_EDITOR.md)) walks through it on a practice screen
(**Help > Open Practice Screen**).

### Stage & event editor — floors and events, as a Windows app ([docs/gameplay/02](../docs/gameplay/02_STAGE_EDITOR.md) section 6)

The web stage editor, native: the map is drawn by the game's renderer (animations play), on the live files in
`assets\data` (no folder to pick).

- The stage and event editors are tabs of one window, as in Visual Studio: drag a tab out to float that editor as a
  window of its own (□ maximises it), drag it back (or ⧉) to dock it. **View → Dock both as tabs** resets the layout;
  **Ctrl+1** / **Ctrl+2** switch.
- **Save** backs each file up to `Build\editor_backups\` first.
- The **Events** tab is the event editor, native too: events, their logic and text, floors and stairs, variables,
  kinds, NPCs with the talk preview, and dialogues. The two editors open each other's items directly.

### Web editors — floors and events ([docs/gameplay/02](../docs/gameplay/02_STAGE_EDITOR.md), [event editor](../docs/gameplay/event_editor/README.md))

`WebEditors.cmd` refreshes the editors' data from `assets\data`, opens the browser on `http://localhost:8000`, and
keeps a server running in its window (close the window, or press Ctrl+C, to stop it).

- Click **Open project folder…** and pick the TOMS folder: then **Save** writes straight into `assets\data` (each file
  is backed up to `Build\editor_backups\` first).
- **Stage editor:** paint walls, doors, keys, monsters, items, events and gates on each floor. The checks at the
  bottom catch unreachable objects, missing keys and broken stairs.
- **Event editor:** events, their text in every language, and how they link (*Active only when*, *Then fire*,
  *Then do → give*). The **NPCs** tab edits NPCs (name, greeting, chat lines, topic order) and has a **talk preview**;
  the **Dialogues** tab edits dialogues as cards ([docs/gameplay/event_editor/08](../docs/gameplay/event_editor/08_NPC_DIALOGUE.md)).

## 3. When something goes wrong

| Problem | Fix |
|---|---|
| *"This app can't run on your PC"*, or a tiny `.exe` | The LFS files were not downloaded: `git lfs install`, then `git lfs pull` in the repo |
| *"VCRUNTIME140.dll was not found"* | A file is missing from `bin`: `git lfs pull`, or `git checkout -- Editor/bin` |
| *"no Qt platform plugin could be initialized"* | `bin\platforms\qwindows.dll` is missing: the same fix |
| WebEditors closes at once | Python 3 is not on PATH; install it from python.org with *Add to PATH* ticked |
| The particle editor has no sprites | Open an atlas in its Sprites dock, or keep `Editor` inside the repo so it finds `assets\media\atlas\fx.atlas` |

## 4. Updating this folder (for whoever builds the editors)

```bat
tools\package_editors.cmd             :: build windows-release, then refresh Editor\bin and Editor\BUILD_INFO.md
tools\package_editors.cmd --no-build  :: refresh from the last build
tools\package_editors.cmd --check     :: only list what is missing, extra or out of date (exit 1 if anything is)
```

- It copies the 8 programs, the Qt 6 DLLs and plugins they load, the shader compiler DLLs (`dxcompiler`, `dxil`) and
  the Microsoft C++ runtime of the installed Visual Studio. Debug files and test programs are left out.
- [BUILD_INFO.md](BUILD_INFO.md) records the commit, the date and every file with its checksum.
- `Editor/bin/**` is stored in Git LFS (`.gitattributes`). Run `tools\test.cmd all` before committing a new release.
- The launchers and these documents are written by hand; the script never touches them.
