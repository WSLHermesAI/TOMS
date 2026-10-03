# 14 — Atlas tool (sprites → one texture)

Many small images become one texture plus a description file. The game draws every sprite from one
texture: fewer texture switches, one download on the web instead of thirty.

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
- **QPainter fallback:** View > *Preview with the Game Renderer* (on by default; applies after a
  restart), the offscreen `--selftest`, a failed bgfx start, and a standalone build of the atlas
  tool (no game engine) all draw with QPainter as before.
- **`--selftest-gpu`** (on screen) checks each editor with the game renderer and saves bgfx
  screenshots:
  - `atlas_editor --selftest-gpu project.atlasproj outdir`: the page, sprite-edit mode and back;
  - `anim_editor --selftest-gpu clip.anim outdir`;
  - `particle_editor --selftest-gpu effects.particle outdir`.


```bat
:: the editor (Qt): open the game's atlas, edit, Ctrl+S
Build\windows-release\bin\atlas_editor.exe assets\media\atlas\game.atlasproj

:: the same without a window
Build\windows-release\bin\atlaspack.exe refs  assets\media\atlas\game.atlasproj --import all
Build\windows-release\bin\atlaspack.exe build assets\media\atlas\game.atlasproj --all-variants
```

It replaces the old FM79979 `PI.exe` (TextureEditor, C++/CLI WinForms) and works the same way: the
project lives next to its packed texture, and the texture holds the images. It still reads and
writes `.pi`. Unlike PI.exe, the layout is automatic and there is a command line.

| Part | Where | What |
|---|---|---|
| `atlas_core` | `tools/atlas/core` | C++17 library, no Qt, no engine: project file, packing, export/import |
| `atlaspack` | `tools/atlas/cli` | the command line (headless) |
| `atlas_editor` | `tools/atlas/qt` | Qt 6 editor; `atlas_editor --headless <atlaspack arguments>` runs without a window |
| web editor | `tools/atlas/web` | the same core as WebAssembly, in the browser; `node atlaspack.mjs` is the headless web/Node version |
| `atlas_file.h` | `src/core/engine` | the game's reader for `.atlas` files (header only) |

## 1. The project: images inside the packed atlas

A project is a folder holding:

```
game.atlasproj     settings, child sprites, pivots, 9-slices, tags, animations, references (JSON)
game.atlas         the packed atlas: where each sprite is          ┐ the images live here:
game.png           the packed texture (game_1.png ... if more)     ┘ no other PNGs are needed
game.plist         the other output formats
```

- **Save** writes all of them. The editors' Save also always writes the `.plist` (for Cocos Creator).
- **Open** reads the JSON and cuts every sprite back out of the packed texture, at its original size,
  into memory. This is what PI.exe did. The original PNG files are not needed and can be deleted.
- **References** are folders the art came from (or will come from). The build never reads them.
  *Import From References* compares them with the project and lists each PNG as **new**,
  **changed** or **same**. You pick what to bring in, and Save stores it in the packed texture.
- **Add images** copies a PNG's pixels in; the PNG file is not kept or referenced.
- **Extract images** writes every sprite back out as a separate PNG.
- **Variants** (art styles) keep their replacement art the same way, in their own packed atlas, with
  their own reference folder.

```json
{
  "name": "game",
  "embedded": true,
  "settings":   { "maxWidth": 2048, "maxHeight": 2048, "padding": 2, "extrude": 1, "trim": false,
                  "powerOfTwo": false, "dedupe": true, "filter": "Nearest", "heuristic": "auto" },
  "output":     { "dir": ".", "formats": ["atlas", "plist"] },
  "references": [ { "path": "../sprites", "recursive": false, "prefix": "" } ],
  "sprites":    [
    { "name": "ui/frame", "split": [8, 8, 6, 6] },
    { "name": "ui/frame_corner", "parent": "ui/frame", "rect": [0, 0, 16, 12] },
    { "name": "floor", "pivot": [0.5, 1.0], "tags": ["map"] }
  ],
  "animations": [ { "name": "coin_spin", "loop": true, "frames": [ { "sprite": "coin", "time": 0.1 } ] } ],
  "variants":   [ { "id": "dark16", "reference": "../styles/dark16/sprites", "images": ["bat", "wall"],
                    "output": { "dir": "../styles/dark16/atlas", "formats": ["atlas", "plist"] } } ]
}
```

- **output.dir** `"."` = next to the project. That folder holds the packed atlas, so *Save As*
  somewhere else takes it along.
- **sprites**: settings for a sprite by name (pivot, 9-slice `split` = left, right, top, bottom,
  `tags`, `trim` on/off, `pin` = `[page, x, y]` to fix its place), or a **child sprite**.
- **variants**: `images` lists the sprites the style replaces. Their pixels are in the variant's own
  packed atlas; every other sprite keeps the original art.
- **Nothing is lost silently.** If an image cannot be packed (for example, it is bigger than the
  maximum page), Save stops with an error and writes nothing.

**Folder projects** (`"embedded"` absent) are the other kind: `"sources"` folders are scanned for
PNGs at every build, `"file"` adds a single PNG, and a variant's `"overrides"` folder replaces art.
`atlaspack pack` makes these, for "pack this folder" jobs in scripts. The editors convert one to an
embedded project when they open it, and `atlaspack embed` does the same from the command line.

### Child sprites (sprites without pixels)

A child names a rectangle inside another sprite: `"parent"` + `"rect": [x, y, w, h]` in the parent's
original (untrimmed) pixels. It has no image of its own, so a background can be reused in pieces
(dialog frame → corners and edges for a 9-slice, a tileset → single tiles) without copying pixels.

- Children of children are allowed; the rect is relative to its own parent.
- Trimming never cuts into a child: a parent keeps at least the area its children use.
- In a variant whose art is a different size, child rects scale with their root image.
- `"bake": true` copies the child's pixels into a sprite of its own, with its own padding and
  extrude, for pieces that must tile or be filtered without bleeding from the neighbours.
- Errors (the build stops, nothing is written): rect outside the parent, missing parent, a loop.

The FM79979 `.pi` format already had this ("PuzzleUnitChild"); the tool reads and writes it.

## 2. Output formats

Every format comes from the same build, so they always agree. Choose any set in `output.formats`.
The `atlas` format is always written for an embedded project (it is where the images are), and the
editors always add the `plist` (for Cocos Creator).

| Format | Files | Read by |
|---|---|---|
| `atlas` | `name.atlas` + pages | **TOMS**, libGDX, Spine 4 runtimes, Godot importers. The main format |
| `atlas-spine3` | `name.atlas` (or `name.spine3.atlas` next to `atlas`) | old runtimes that read fixed lines (Spine 3.x); no extensions |
| `plist` | `name.plist` per page | **Cocos Creator** (Sprite Atlas asset), cocos2d-x — TexturePacker format 3 |
| `tp-json` | `name.json` per page | Phaser, PixiJS (TexturePacker JSON hash; `animations` for Pixi) |
| `pi` | `name.pi` per page | FM79979 engine (`cPuzzleImage`), the old PI editor |
| `rcss` | `name.rcss` | RmlUi: an `@spritesheet` per page, for other RmlUi projects (TOMS makes its own in memory from the `.atlas`) |

Pages are PNG: `name.png`, `name_1.png`, … A second page is only made when the sprites do not fit the
maximum size.

### Extension fields

Standard parsers must still work, so every sprite — children too — is written as a normal region with
its real position. The extra data is added in the way each format allows, under an `x_` name:

```
game.png
size: 214, 178
format: RGBA8888
filter: Nearest, Nearest
repeat: none
ui/frame
  bounds: 0, 0, 64, 48
  split: 8, 8, 6, 6
ui/frame_corner
  bounds: 0, 0, 16, 12
  x_parent: ui/frame
  x_local: 0, 0, 16, 12
  x_pivot: 0.5, 1
  x_tags: ui
```

| Field | Meaning |
|---|---|
| `x_parent` / `x_local` | child sprite: parent name, rect in the parent's original pixels |
| `x_pivot` | pivot, 0..1 of the original size from the top-left (written when not 0.5, 0.5) |
| `x_tags` | tags |

libGDX and Spine 4 keep unknown region fields (`names`/`values`) and skip unknown page fields. In the
plist the same keys are extra entries in the frame dictionary (`x_parent`, `x_local` as
`{{x,y},{w,h}}`, `x_split`, `x_tags`), plus the standard `anchor` for the pivot. In the TexturePacker
JSON they are under `"x"` in each frame. In `.pi` a child is a normal `PuzzleUnit` plus a
`PuzzleUnitChild` element, exactly as the old editor wrote it.

Cocos Creator does not use `.atlas` for sprites (only for Spine skeletons): give it the `plist`.

Not verified yet: that Cocos Creator 3.x ignores the extra frame keys, and that the Java libGDX and
Spine runtimes load the extended `.atlas`. The keys follow both parsers' documented rules (unknown keys
are kept or skipped), but nobody has loaded the files in those engines yet. Do that before relying on
them there.

### Rules the output keeps

- Sprites are never rotated (the `.pi` loader has no rotation, and pixel art does not need it).
- Same input → same files, byte for byte, on every machine (and in the web build). Unchanged files
  are not rewritten, so their timestamps do not trigger rebuilds.
- `.pi`: `Count` = number of `PuzzleUnit`s; units sorted by name; `UV` is the half-open rect plus the
  exact `ShowPosInPI`; `OriginalSize ≥ Size`.

## 3. Packing settings

| Setting | Meaning |
|---|---|
| `maxWidth` / `maxHeight` | largest page; more sprites → more pages |
| `powerOfTwo`, `square`, `fixedSize` | page size rules. Without them the page shrinks to fit (binary search) |
| `padding` | empty pixels between sprites (really between them: the old editor's gap was one short) |
| `border` | empty pixels along the page edges |
| `extrude` | edge pixels repeated around each sprite, so filtering/scaling never samples a neighbour |
| `trim`, `alphaThreshold` | cut transparent edges; the original size and offset are kept in the output |
| `dedupe` | identical images (after trimming) share one place |
| `heuristic` | MaxRects rule: `auto` (try bssf, blsf, baf, bl, cp and keep the first that fits), or `shelf` (rows, like the old editor) |
| `premultiplyAlpha`, `filter` | written into the files for the engine |

Pinned sprites (`"pin"`, or dragged in the editor) keep their place; the rest are packed around them.

## 4. In TOMS

`assets/media/atlas/` is the project folder: `game.atlasproj` + `game.atlas` + `game.png` (+ `.plist`).
The `dark16` art style's packed atlas is in `assets/media/styles/dark16/atlas/`. The references are `assets/media/sprites` and `assets/media/styles/dark16/sprites`.

- **The game** (`Game::loadPrebuiltSpriteAtlas`, `src/core/game/core/game_assets.cpp`) loads
  `atlas/game.atlas` for the original art or `styles/<id>/atlas/game.atlas` for a style. It uploads
  the page (`IRenderer::loadSpriteAtlas`) and takes each sprite's UVs from it. A trimmed sprite still
  fills its tile, because `spriteQuad` uses the trim offset. The whole `toms::AtlasFile` (children,
  pivots, 9-slices, tags) is available through `Game::spriteAtlas()`, so code can look a sprite up by
  name. If the file is missing or lacks a sprite, the game logs why and builds the old grid atlas at
  start-up as before. Through the atlas the map scene is pixel-identical to the grid (checked with
  `image_diff`).
- **The files are committed; nothing is generated at build time.** Saving the project writes them.
  The CTest `atlas.assets_up_to_date` (`atlaspack build --check`) checks that the project opens from
  its packed atlas alone and that its other files (plist, the style's atlas) match.
- **Changing art**: edit the PNG in the reference folder, then in the editor run *Import From
  References* (it lists that PNG as *changed*), and Save. Without a window:
  `atlaspack refs assets\media\atlas\game.atlasproj --import changed`.
- **UI (RmlUi)**: the UI draws its icons from the same atlas. At start-up the game turns the loaded
  `game.atlas` into an RmlUi `@spritesheet` in memory (`_atlas.rcss`, [08](08_RMLUI.md)), and the `.rml`
  files name sprites (`data-attr-sprite`). Repacking the atlas never needs a UI change. Renaming or
  deleting a sprite that the code, `assets/data` or an `.rml` still uses fails `atlas_sprites_test`.
  The PNGs in `media/sprites` are now only references (and the fallback grid's source when no atlas
  exists).
- **A new art style**: add a variant to the project ([13](13_ART_STYLES.md), step 5).

## 5. Command line

```
atlaspack new     <OUT.atlasproj> [--source DIR]... [--format LIST] [options]
atlaspack add     <project> <png|folder>... [--prefix P] [--variant ID] [--replace]
atlaspack refs    <project> [--variant ID] [--import new|changed|all]
atlaspack extract <project> --out DIR [--variant ID] [--children]
atlaspack embed   <folder-project> [--save-as NEW.atlasproj] [--keep-output]
atlaspack build   <project> [--variant ID | --all-variants] [--out DIR] [--format LIST] [--strict] [--check]
atlaspack import  <file.pi|.atlas|.json> --project OUT.atlasproj [--images DIR] [--format LIST]
atlaspack pack    <folder>... --out DIR [--name NAME] [--format LIST] [options] [--save-project FILE]
atlaspack info    <file.pi|.atlas|.json>
atlaspack formats
```

| Command | Does |
|---|---|
| `new` | an embedded project; `--source` folders are imported and kept as references |
| `add` | copies PNGs (files or whole folders) into the project and saves it |
| `refs` | lists every reference PNG as new / changed / same; `--import` takes them in and saves |
| `extract` | every sprite (and with `--children` every child) as a separate PNG again |
| `embed` | a folder project → embedded; its folders become references |
| `build` | rebuilds and writes the output files; `--check` only compares |
| `import` | an embedded project from an existing `.pi` / `.atlas` / TexturePacker `.json` |
| `pack` | folders straight to atlas files (a folder project, for scripts) |

Options: `--max WxH --pot --square --padding N --border N --extrude N --no-trim
--alpha-threshold N --no-dedupe --heuristic auto|bssf|blsf|baf|bl|cp|shelf --filter Nearest|Linear`.
Exit codes: 0 ok, 1 warnings with `--strict` (or stale files with `--check`), 2 errors, 3 bad command
line. Messages are `file: error: sprite: text`, one per line. Paths and names may be any Unicode
(UTF-8 inside; on Windows the command line is read as UTF-16).

**Moving an old PI project over:**

```bat
atlaspack import old\UI.pi --project ui\ui.atlasproj     :: ui.atlasproj + ui.atlas + ui.png + ui.pi
```

Each unit is cut out at its original size, with its trimmed pixels put back at their offset.
`PuzzleUnitChild` units become child sprites, and sequence animations come along too. Triangle
meshes (`.ti`) and morphing (`.mx`) are not carried over (see 7). `--images DIR` writes the units
out as PNGs instead and makes a folder project.

## 6. Tests

`atlas_core_test` (CTest `atlas.core`):

- **Packer**: no overlaps, padding respected, inside the page, deterministic, pinned rects exact.
- **Builds**: trim, children (nested, under a trimmed parent, baked), dedupe, variants at 2× size.
- **Formats**: every format is exported and read back. The `.atlas` goes through the game's own
  `atlas_file.h`. Every sprite must draw exactly its source pixels.
- **Embedded projects**:
  - A folder project is embedded and saved elsewhere, its original PNGs are deleted, and it is
    reopened. Every sprite and every variant sprite must draw the same pixels.
  - Saving again rewrites nothing but the project file.
  - The reference statuses (new / changed / same) are correct.
  - An image too big to pack makes Save refuse and write nothing.
  - `extract` writes every sprite as a PNG.
  - A `.pi` is imported into an embedded project and saved back as `.pi`.
- **Golden**: `tests/data/9Slicing_TalkingDialob.pi`, from the old editor, is rebuilt pixel for pixel.

```bat
set ATLAS_PI_CORPUS=D:\Work\MagicTowerOfSoercer\MagicTower
Build\windows-release\bin\atlas_core_test.exe
```

This runs the same round trip (import → rebuild → `.pi` → compare what each unit draws) over every
`.pi` below that folder. All 95 readable files in the MagicTower tree pass. One file there is empty
and one has a missing PNG.

## 7. Left out on purpose

- **Triangle meshes and morphing** (`.ti`, `.mx`, the old Triangulator tab): an old FM79979 feature
  with no use in TOMS. The old PI.exe still edits them.
- **Rotation** in packing: see §2.
- **`.pib`/`.pngb`** (FM79979 Huffman-compressed copies) and **DDS**: not written. Plain `.pi` + `.png` load everywhere.
