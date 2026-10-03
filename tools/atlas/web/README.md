# Atlas tool, web version

The sprite atlas editor in the browser: `atlas_core` (packing, exporters, importers) compiled to
WebAssembly, plus a static UI with no build step (plain ES modules + CSS, no npm, no CDN).
Packing happens in the same C++ code as `atlaspack`. For the same input, the `.atlas` text and
page PNGs are byte-identical to the native tool's.

Every project in the web editor is **embedded** (like the old FM79979 PI editor, see
`core/atlas_store.h`): the images live in the packed atlas the project writes next to itself
(`<output.dir>/<name>.atlas` + PNG pages). Opening a project cuts every sprite back out of the
packed texture, so the original PNG folders are not needed. Folders of PNGs are only
**references** to import new or changed art from.

| File | What |
|---|---|
| `atlas_web.cpp` | embind API (`AtlasWeb` class + `cliMain`); holds the project with its images, undo snapshots |
| `CMakeLists.txt` | target `atlas_web` -> `atlas_web.js/.wasm`, copied with the UI into `<build>/site` |
| `index.html`, `style.css` | the page |
| `app.js` | state, undo/redo, toolbar, open/save/import/export/extract, image operations, drag and drop |
| `refs.js` | Import from references dialog, the "choose this folder" dialog used when opening |
| `sprites.js` / `canvas.js` / `props.js` / `anims.js` | Sprites tree / view + sprite edit mode / Properties / Animations |
| `wasm.js` | loads the module, the in-memory workspace (MEMFS), file collection from drops |
| `zip.js` | minimal zip writer (STORE, CRC32) for downloads, and reader (STORE; DEFLATE via `DecompressionStream`) for opening a saved .zip |
| `atlaspack.mjs` | `atlaspack` on Node with the same wasm |

## Build and serve

```
tools\build_atlas_web.cmd            (release; "debug" for Build\atlas-web-debug)
python tools\serve_web.py Build\atlas-web\site 8098
```
Open http://localhost:8098/. The page must be served over http, not opened as `file://`.
The script finds emsdk the same way `tools\build_web.cmd` does (`%EMSDK%`, `..\..\..\emsdk`,
`%USERPROFILE%\emsdk`, `C:\emsdk`, `D:\emsdk`) and uses the CMake and Ninja that ship with
Visual Studio. By hand:
```
emcmake cmake -S tools/atlas -B Build/atlas-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Build/atlas-web --target atlas_web
```
If you only changed UI files, `cmake --build Build/atlas-web --target atlas_web_site` copies them again.

## Using it

- **New** starts an empty embedded project (packed atlas next to the project file).
- **Open...** / **Open folder...** (or drop): pick the `.atlasproj` together with its packed atlas
  (`.atlas` + page PNGs), or its whole folder, or a `.zip` made by **Save**. The files are put in
  the in-memory workspace at their relative paths and the project opens with every image cut out
  of the packed atlas. A variant keeps its art in its own packed atlas (e.g. `../styles/dark16/atlas`):
  if that folder was not picked along, a dialog asks for it (or opens without that art). Picking a
  common parent folder brings everything in one go. A missing base atlas is reported as an error.
  A **folder project** (`sources`, made by `atlaspack pack`) is converted on open: its source
  folders must come along (or be chosen in the dialog); the images are taken in, the folders become
  references, and the Log and a toast say so. Save then writes it as an embedded project.
- **Save** (Ctrl+S) downloads ONE `.zip` with exactly what `saveProjectAll` writes: `<name>.atlasproj`,
  `<name>.atlas` + page PNGs, the `.plist` (always written), the other checked formats, and every
  variant's atlas at its relative place (`atlas/game.atlasproj`, `styles/dark16/atlas/game.atlas`...).
  Unzip it over the project folder to update it. If an image could not be packed (e.g. bigger than
  the maximum page) nothing is downloaded and the error is shown.
- **Export files** (Ctrl+E) downloads just the output formats (+ `.plist`) of the base art or of the
  variant picked in the toolbar, without the project.
- **Extract images...** downloads every image (optionally the child sprites too) as `<name>.png` in a zip.
- **Import atlas...:** pick a `.pi`, `.atlas` or TexturePacker `.json` file together with its PNG
  page(s). Each sprite is cut out into memory, child regions become child sprites, and the result
  opens as a new embedded project. **Save** writes it with its packed atlas.
- **Images:** **+ Files**, **+ Folder** or dropping PNGs copies the pixels in. The name is the file
  name without `.png`, or the path below the dropped folder (`ui/btn_ok`). An existing name asks
  Replace / Skip (with "apply to all"). With a variant picked in the toolbar, the PNGs become that
  variant's replacement art. Right-click a sprite (or use the buttons in Properties) for
  **Replace image...**, **Save image as PNG**, **Rename...** (F2; renames the image, its variant art,
  its settings, its children's parent and animation frames) and **Delete** (image, variant art,
  settings and children, after a confirmation).
- **References:** Properties > References lists the folders (paths relative to the project, kept in
  the project file; a variant has one `reference`). A browser cannot keep folder paths, so
  **Import from references...** asks for each folder once per session (Choose folder... or drop it)
  and loads it where the project expects it. The table shows New / Changed / Same with thumbnails
  of the reference file and of the current image; New and Changed are checked. **Import** applies
  the checked rows as one undo step. With a variant picked, its reference fills its replacement art.
- **View:** the wheel zooms around the cursor. Middle-drag, right-drag or Space+drag pans. F fits,
  1 shows actual size, PageUp/PageDown switch pages. Click to select, Ctrl/Shift+click adds to the
  selection, and dragging on an empty area draws a selection box. Drag an image to pin it to that
  place on the page.
- **Sprite edit mode:** double-click a sprite (or press Enter) to edit it:
  - drag on empty space to draw a new child sprite
  - drag a child sprite to move it, or drag its handles to resize it (arrow keys nudge it by 1 px, or 10 px with Shift)
  - drag the circle to move the pivot (it snaps to half pixels; hold Alt to place it freely)
  - drag the dashed cyan lines to move the 9-slice guides (turn on 9-slice in Properties first)
  - press Esc or click **Atlas** to go back
- **Undo/redo:** Ctrl+Z and Ctrl+Y (or Ctrl+Shift+Z). Every edit is one step, image changes
  included: each step is a snapshot of the C++ project (`AtlasWeb::snapshot/restore`; images are
  shared, so a step costs little). The last 200 steps are kept.
- The editor rebuilds about 100 ms after each change, and **Problems** lists the diagnostics. Click
  one to select its sprite.
- UI preferences (theme, panel sizes, collapsed folders, the open tab) are kept in localStorage.

## Headless on Node

```
node Build\atlas-web\site\atlaspack.mjs pack assets\media\sprites --out Build\atlas-out --format atlas,tp-json
node Build\atlas-web\site\atlaspack.mjs build game.atlasproj --all-variants
```
It takes the same arguments as `atlaspack` (`build`, `pack`, `import`, `info`, `new`, `embed`, `add`,
`refs`, `extract`, `formats`)
and returns the same exit codes. The real disk is mounted with NODEFS: the drive of the current
folder is mounted at `/<letter>` on Windows, or `/` is mounted at `/host` elsewhere. Relative
paths work unchanged, and absolute paths in the arguments are translated.

Limitations:
- Messages show the mounted paths (`/d/Work/...`).
- `--save-project` across drives writes relative paths through the mount points, which the native
  tool cannot read.
- It is slower than the native exe (single thread, NODEFS I/O). It needs Node 18 or newer.

## Limitations of the web editor

- Everything runs on the main thread. Fine for hundreds of sprites; very large projects stall the
  UI for the length of a build or a save.
- The project lives in memory. Reloading the page loses unsaved work; Save downloads a .zip (a
  browser cannot write back into the folder it was opened from).
- Reference folders are loaded per session; their paths in the project file are typed by hand (or
  default to the chosen folder's name).
- A variant whose packed atlas is not picked along can be opened without its art; saving then
  writes that variant without replacement art.
- Opening folders needs a Chromium- or Firefox-based browser (`webkitGetAsEntry` /
  `<input webkitdirectory>`). Zip entries other than STORE need `DecompressionStream`.
