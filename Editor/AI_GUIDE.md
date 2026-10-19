# Making TOMS assets (guide for AI agents)

People: see [README.md](README.md). 中文使用說明：[zh_TW/README.md](zh_TW/README.md).

This guide is for an AI agent asked to make or change game assets in this repo. Every asset is a text file (JSON or
BMFont text) plus PNGs, so you write the files directly, then **validate them with the command-line tools in
`Editor\bin`**. You never need to drive the editors' windows. The full specs are linked in each section; this page
holds the rules that most often go wrong and templates that pass the checks.

## 0. Ground rules

- **Run the tools from the repo root** (`Editor\Tools.cmd` opens a prompt there with `Editor\bin` on PATH), or call
  them as `Editor\bin\<tool>.exe`. Exit codes everywhere: **0** ok, **1** warnings only (or `--check` found stale
  files), **2** errors, **3** bad command line.
- **Always validate after writing.** A file that fails its check is not done. The checks reject unknown JSON fields,
  so a typo (`"colour"`) is an error, never silently ignored.
- **Paths inside asset files are relative to that file** and use `/`. Never write an absolute path.
- **Sprite references are `"atlasId:name"`**, where the atlas id is the `.atlas` file name without the extension
  (`game`, `fx`), unless the file's `atlases` list gives `{"id": ..., "path": ...}`.
- **JSON: 2-space indent, UTF-8 without BOM.** Keep the key order of existing files when editing them; the web
  editors rewrite files in that order and a reordered file makes a noisy diff.
- **Text the player reads goes in `assets/data/text.json`**, one string per language (`zh_TW` and `en` at least),
  never inline in other data files.
- **Documents you write** go to `docs/NN_NAME.md` with a Traditional Chinese copy in `docs/zh_TW/` (see `CLAUDE.md`);
  never upload anything online.
- **Testing:** after changing files, run `tools\test.cmd` (it picks only the tests your change can break) and report
  "changed-only: N of M tests" (`CLAUDE.md`).

## 1. Where things live

| Asset | Files | Spec | Validate |
|---|---|---|---|
| Game sprite atlas | `art_raw/atlas/game.atlasproj` (raw, not shipped) cooks `assets/media/atlas/game.atlas` + `.png`; source PNGs in `art_raw/sprites` | [14](../docs/14_ATLAS_TOOL.md) | `atlaspack build <proj> --check` |
| fx atlas (particle sprites) | `art_raw/atlas/fx.atlasproj` | [17](../docs/17_PARTICLES.md) §3 | same |
| Node animations | `assets/media/anim/*.anim`; examples `docs/examples/anim_*.anim` | [15](../docs/15_ANIMATION.md), recipes [16](../docs/16_ANIMATION_RECIPES.md) | `anim_editor --headless check <file>` |
| Particle effects | `*.particle`; examples `docs/examples/fx_recipes.particle` | [17](../docs/17_PARTICLES.md) | `particle_editor --headless check <file>` |
| Picture fonts | `NAME.fnt` + `NAME.png`; test fonts in `tests/font` | [22](../docs/22_FONT_TOOL.md) | `fontpack info <fnt>`, `fontpack preview` |
| 3D models | `assets/media/models/*.glb` | [18](../docs/18_GLTF.md) | `gltf_viewer <file> --frames=5 --screenshot=out.png` |
| Floors (maps) | `assets/data/story/floors/F??.stage.json` (grid) + `F??.json` (spec, events list) | [19](../docs/19_STAGE_EDITOR.md) | `node tools/web_editors/sync_floor_index.mjs --check` |
| Events, NPCs, dialogues | `assets/data/events/pool_*.json`, `floor_index.json`, `assets/data/npcs/*.json`, `assets/data/dialogue/*.json`, `assets/data/story/vars.json` | [event_editor/01](../docs/event_editor/01_DATA_MODEL.md), [06](../docs/event_editor/06_EVENT_LOGIC.md), [07](../docs/event_editor/07_KEY_GATES.md) | same, plus `tools\test.cmd` |
| Items, text | `assets/data/items.json`, `assets/data/text.json` | [07](../docs/event_editor/07_KEY_GATES.md) §1 | `tools\test.cmd` |
| Editor UI text | `tools/studio_common/i18n/zh_TW/*.json` | [24](../docs/24_EDITOR_LANGUAGE.md) | `python tools/studio_common/i18n/i18n_check.py --check` |

## 2. Sprite atlas (`atlaspack`)

Do not hand-write `.atlas` or the packed `.png`: they are outputs. Put PNGs in, let `atlaspack` pack.

```bat
:: add art to the game atlas (the PNGs are copied into the project) and rebuild its outputs
atlaspack add assets\media\atlas\game.atlasproj path\to\new_sprite.png --prefix ui/
atlaspack build assets\media\atlas\game.atlasproj
atlaspack build assets\media\atlas\game.atlasproj --check

:: changed a PNG in the reference folder (assets\media\sprites): bring the change in
atlaspack refs assets\media\atlas\game.atlasproj --import changed

:: a new atlas from a folder of PNGs
atlaspack new my\ui.atlasproj --source my\ui_pngs --extrude 1 --padding 2
```

- A sprite's **name** is its file name without `.png`, plus `--prefix`. Code, `assets/data` and `.rml` files look
  sprites up by name; renaming or deleting one that is used fails `atlas_sprites_test`.
- Per-sprite settings go in the project's `"sprites"` list (`pivot` 0..1, `split` = 9-slice `[l, r, t, b]`, `tags`,
  child sprites `{"name", "parent", "rect": [x, y, w, h]}`); after editing that JSON run `atlaspack build`.
- Packing settings: `padding` 2 and `extrude` 1 are right for almost everything. `trim` keeps the sprite's original
  size when drawn.

## 3. Node animation (`.anim`)

Template (passes `anim_editor --headless check` when saved under `assets/media/anim/`):

```json
{
  "version": 2,
  "atlases": ["../atlas/game.atlas"],
  "clips": [
    {
      "name": "slime_hop",
      "length": 0.6,
      "playCount": -1,
      "root": {
        "name": "slime",
        "sprite": "game:slime",
        "tracks": {
          "pos":   [{"t": 0, "v": [0, 0, 0], "ease": "quadraticOut"}, {"t": 0.3, "v": [0, -10, 0], "ease": "quadraticIn"}, {"t": 0.6, "v": [0, 0, 0]}],
          "scale": [{"t": 0, "v": [1.15, 0.85, 1], "ease": "backOut"}, {"t": 0.15, "v": [1, 1, 1]}, {"t": 0.55, "v": [1, 1, 1]}, {"t": 0.6, "v": [1.15, 0.85, 1]}],
          "event": [{"t": 0.6, "v": "land"}]
        }
      }
    }
  ]
}
```

- **Units:** time in seconds; `pos` in pixels, **y down**, z into the screen; `rot` in degrees, `[x, y, z]`, z turns
  in the screen plane (clockwise); `scale` `[x, y, z]`; `color` `[r, g, b, a]` 0..1.
- **Clip:** `length` (0 = up to the last key), `playCount` (-1 = forever), `stayAtLastFrame`.
- **Node:** `name`, `sprite`, rest values `pos` `rot` `scale` `color` `visible`, `pivot`, `order` (< 0 draws behind
  the parent), `blend` (`normal` / `add`), `inheritColor`, `loop`, `stayAtLastFrame`, `children`.
- **Tracks:** `pos` `rot` `scale` `color` interpolate; `sprite` `visible` `event` step. A key is
  `{"t", "v", "ease"?}`; `ease` shapes the segment that key **starts**: `linear`, `stepped`, `quadratic|cubic|quartic|
  quintic|sinusoidal|exponential|circular|bounce|elastic|back` + `In|Out|InOut`, or `[x1, y1, x2, y2]`.
- Keys after a fixed `length` never play (a warning). Every name in `sprite` must exist in that atlas (an error).
- Find sprite names: `atlaspack info assets\media\atlas\game.atlas`.

```bat
anim_editor --headless check assets\media\anim\my.anim
```

## 4. Particle effect (`.particle`)

Template (passes `particle_editor --headless check` when saved next to `docs/examples/fx_recipes.particle`; change the
atlas path to match where you save it):

```json
{
  "version": 1,
  "atlases": [{"id": "fx", "path": "../../assets/media/atlas/fx.atlas"}],
  "effects": [
    {
      "name": "ember_burst",
      "duration": 1.0,
      "emitters": [
        {
          "name": "embers",
          "maxParticles": 60,
          "emission": {"rate": 0, "bursts": [{"t": 0, "count": [24, 32]}]},
          "shape": {"type": "circle", "radius": 4},
          "life": [0.4, 0.8],
          "direction": -90,
          "spread": 60,
          "speed": [80, 160],
          "size": [10, 16],
          "color": [[1, 0.8, 0.4, 1], [1, 0.5, 0.2, 1]],
          "sprite": "fx:spark",
          "overLife": {
            "color": [{"t": 0, "v": [1, 1, 1, 1]}, {"t": 1, "v": [1, 0.3, 0, 0], "ease": "quadraticIn"}],
            "size": [{"t": 0, "v": 1}, {"t": 1, "v": 0.3}]
          },
          "forces": {"gravity": [0, 120], "drag": 1.5},
          "render": {"blend": "add"}
        }
      ]
    }
  ]
}
```

- **Units:** seconds, pixels, degrees; `direction` -90 = up (y down); `gravity` positive y = falls.
- A start value is a number or `[min, max]`; `color` is one colour or two to pick between. `overLife` curves have `t`
  0..1 and **multiply** the start values.
- `duration` 0 = endless. Bursts: `{"t", "count", "repeat", "interval"}`; a repeating burst needs an interval.
- fx atlas sprites: `dot`, `glow`, `spark`, `star`, `ring`, `smoke`, `flame_0`..`flame_3`, `square` (white: tint them
  with `color`).
- Blend: `add` for fire, sparks, magic, light; `normal` for smoke, dust, debris; `multiply` for shadows.
- Look at the result (a PNG strip of frames) before calling it done:

```bat
particle_editor --headless check my.particle
particle_editor --headless render my.particle#ember_burst Build\ember.png
```

## 5. Picture font (`fontpack`)

One PNG per character, all on the same baseline (bottom-aligned); the picture's width is the advance.

```bat
:: a folder = its PNGs; characters from the file names (0.png, dot.png, K.png ...) or from its chars.txt
fontpack build art\score --out assets\media\fonts\score.fnt --layout pot
:: or give the characters in the order of the files
fontpack build a.png b.png c.png --chars "0.," --out assets\media\fonts\score.fnt
fontpack preview assets\media\fonts\score.fnt --text "12,345.6K" --out Build\score_preview.png
fontpack info assets\media\fonts\score.fnt
fontpack split tests\font\total_score.fnt --out Build\total_score_pictures
```

- File names a character cannot be: `dot` `comma` `space` `slash` `times` (×) …, or `U+XXXX.png`.
- `--space N` (default: a space picture's width, else the `1` picture's, else 5), `--line-height N` (default: the
  tallest picture).
- Exit 1 from `preview` means the text uses characters the font lacks: check the list it prints.
- In code: `toms::BitmapFont` / `FontText` (`src/core/engine/bitmap_font.h`): `setText`, `setAlign`, `render`.

## 6. Floors, events, items

These are game data; the web editors are the normal way to change them, but the JSON is simple.

- **Floor grid** `F??.stage.json`: rows of characters plus a `legend` mapping each character to an object. `y` `b` `r`
  (doors) and `U` `D` (stairs) are fixed characters the game reads directly; everything else goes through the legend.
  A key gate is `"Q": "gate:<item id>"` and opens only for that item.
- **Floor spec** `F??.json`: `events` lists the event ids the floor can hold.
- **Event** (in a `pool_*.json`): `eventId` (unique across all pools), `kind` (`relic`, `whisper`, `cache`, `trap`, `rescue`, `merchant_echo`, `memory_shard`; more can be added in the event editor's **Kinds** tab), `text` / `title` /
  `desc` (keys in `text.json`), and optionally `requires` (e.g. `{"type": "runFlagSet", "flag": "event_<eventId>"}` = that event is done), `next` (event ids it wakes), `actions` (`give`, `addCounter`, `setFlag`). See
  [07](../docs/event_editor/07_KEY_GATES.md) §2–5 for a worked chain.
- **NPCs and talk events** ([event_editor/08](../docs/event_editor/08_NPC_DIALOGUE.md)): an NPC file in `assets/data/npcs/`
  (chat lines, greeting), events with `"trigger": {"on": "talk", "npc": id}` plus `dialogue` / `afterDialogue`, and
  dialogue files in `assets/data/dialogue/` whose choices carry `actions`, `finish`, `fire` and `id`. Results use one
  action list (`addStat` by any status or attribute, `give`, `take`, `setVar`, `addVar`, `addCounter`, `fireEvent`, …).
  Section 11 of that document is the recipe for connecting a bigger story; `tools\test.cmd` runs `talk_system_test`,
  which checks every NPC file, dialogue and action.
- **Story item** in `items.json`: `{"type": "story", "sprite", "tint": [r, g, b], "name", "desc", "important": true}`.
- **After changing floors or events, rebuild the event index:**

```bat
node tools\web_editors\sync_floor_index.mjs
node tools\web_editors\sync_floor_index.mjs --check
tools\web_editors\refresh_data.cmd      :: the web editors' copies of the data
```

- Try it in the game: `toms_game --goto=F04 --give=key_bellward --events-done=ev_a,ev_b` starts a new game on a
  floor with items given and events done (the game is in `Build\windows-release\bin`, not in `Editor\bin`).

## 7. 3D models

- Use `.glb` (or `.gltf` + `.bin` + images). Not supported: Draco, meshopt, KTX2/basisu compression (refused with a
  message; re-export without them).
- Supported and drawn: PBR metal-rough materials, skins (≤ 128 joints), morph targets, animations, instancing,
  `KHR_lights_punctual`, cameras, `KHR_physics_rigid_bodies`. The viewer's panel lists every extension, green when
  drawn, orange when ignored.
- Check a model without looking at a window:

```bat
Editor\bin\gltf_viewer.exe model.glb --frames=10 --screenshot=Build\model.png --no-ui
```

## 8. Checklist before you say "done"

1. The asset's validate command exits 0 (or 1 with warnings you have read and accept).
2. For anything visual, you rendered it to a PNG (`particle_editor --headless render`, `fontpack preview`,
   `gltf_viewer --screenshot`) and looked at the image.
3. `tools\test.cmd` passes; report "changed-only: N of M tests".
4. Player-facing text is in `text.json` in `zh_TW` and `en`.
5. No absolute paths in any file you wrote.
