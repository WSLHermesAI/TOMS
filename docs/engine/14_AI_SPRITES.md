# 14 — New TOMS sprites with AI (monsters, NPCs, doors, items, floor themes)

How the game got about 230 new 32 × 32 sprites from the local AI pipeline in `AIGamestyle/` (the same ComfyUI set-up
as the game reskin; setting it up on a PC: [AIGamestyle/Doc/Reskin_Setup_New_PC.md](../../AIGamestyle/Doc/Reskin_Setup_New_PC.md)), how they became draft types the stage editor can place, and the small game
features that let them show: any atlas sprite usable by name, a gate drawn with its own door, floor themes and the
player's look.

## 1. What was made (the owner's choices, 2026-10-17)

| Category | New | Becomes |
|---|---|---|
| Monsters | 60, in 6 tiers of 10 (forest, cave, undead, fire, ice, castle) | `enemies.json` entries with draft stats by tier |
| NPCs | 50 (villagers, traders, guards, nobles, clergy, travellers) | an NPC file each + a one-page dialogue |
| Doors + keys | 10 + 10 (emerald, amethyst, silver, bronze, obsidian, frost, flame, bone, rune, sun) | a key item that opens its own gate; the gate is drawn with its own door |
| Event items | 50 (amulet, map, sealed letter, lantern, spell book …) | `items.json` story items (kept, not used up) |
| Floor themes | 10 × (wall, floor, stairs up, stairs down) | a floor picks a theme in the stage editor |
| Player looks | 10 (mage, rogue, paladin, ranger …) | the `playerLook` setting |

Choices: **pictures plus draft types** (the types are marked `"draft": true`: stats and text are first drafts to
rebalance and rewrite); **60 monsters and 50 NPCs**; **an own picture per new door** (a small game change);
**floor themes** (pictures and the feature).

## 2. How a sprite is made

TOMS sprites are 32 × 32 pixel art: flat colours, a thick dark outline, chunky shapes, a transparent background. Two
ways, by category (`AIGamestyle/_pipeline/tools/make_sprites.py`):

**New things from a description** (monsters, NPCs, players, items):
1. SDXL base with the **pixel-art-xl LoRA** draws the description at 1024 × 1024 on a plain background. The prompt asks
   for a chibi, chunky, low-detail sprite (creatures and people) or a single-object icon (items).
2. **IP-Adapter** follows a sheet of the game's own sprites of that kind (the creatures for creatures, the gems /
   potions / keys for items, weight 0.3), so the new ones look like the old ones. Too strong a reference turned an item
   into a character and greyed the colours; 0.3 with a per-kind sheet works.
3. **Inspyrenet** (a ComfyUI node) removes the background.
4. Shrunk to fit 30 × 30 inside the 32 × 32 tile (feet on the bottom row for creatures, centred for things), colours
   brought back to bold (shrinking blends them), alpha made hard, the palette cut to 8 colours, and a 1-pixel
   near-black outline drawn around it.

**Variants of things the game already has** (doors, keys, stairs, walls, floors): **derived** from the game's own
sprite, so a new door has the exact shape of the yellow door and sits on the map the same way:
1. The base sprite (e.g. `door_yellow`) is turned grey (its own colours would win over the description: an "emerald"
   door stayed yellow) and enlarged to 1024.
2. img2img (strength 0.8) with the pixel-art LoRA toward the description; ControlNet canny holds its shape.
3. Shrunk back to 32 × 32 with the base's **exact silhouette**; the base's near-black outline pixels stay near-black.

Each sprite's 1024 picture is kept beside it (`<id>_big.png`) for checking or redoing.

## 3. The plan

`AIGamestyle/_pipeline/style_profiles/make_toms_sprites_plan.py` holds every sprite: its id, a one-line description,
and its English and Traditional Chinese names. Running it writes `toms_sprites.json`, which both tools read:

```
cd AIGamestyle\_pipeline\style_profiles
python make_toms_sprites_plan.py                       # after editing a description or a name
cd ..\tools
python make_sprites.py ..\..\output\toms_sprites --plan ..\style_profiles\toms_sprites.json        # what is not made yet
python make_sprites.py ..\..\output\toms_sprites --plan ..\style_profiles\toms_sprites.json --only m_yeti --force   # redo one
python sprite_sheet.py sheet.png ..\..\output\toms_sprites --big                                  # look at them
python add_toms_types.py ..\..\output\toms_sprites    # into the game: sprites, manifest, draft types, names
Build\windows-release\bin\atlaspack.exe build art_raw\atlas\game.atlasproj --all-variants      # rebuild the atlas
python tools\refresh_meshes.py                       # the sprite meshes, cut again from the new pixels (docs/engine/16)
```

A category in the plan has its `style` (the prompt's start), `reference` (which game sprites the look follows),
`mode` and, for derived ones, `base` and `denoise`.

## 4. What the game and the editor gained

- **Any atlas sprite is drawable by name.** The game used to know only its 30 built-in sprites by name (a type naming
  another one drew as floor). Now every sprite on the atlas page gets a layer when the atlas loads (`game_assets.cpp`),
  so a type's `"sprite"` can be any of the new ones. The atlas's first page is the sprite texture; it holds all of them.
- **A gate draws its own door.** A key item with `"door": "door_emerald"` makes its gate show that picture (no colour
  wash, and it just opens instead of playing the yellow door's clip). A gate without one is the washed yellow door as
  before. The stage editor draws gates the same way now (before, it showed the key's picture).
- **The palette** sorts story items: one that `"opens"` a gate is a gate key (Doors and Keys); the others are event
  items (Items). **+** on Doors / Keys writes `"opens"`.
- **Floor themes.** A stage file's `"theme"` (e.g. `"ice"`) makes the game draw `wall_ice`, `floor_ice`,
  `stairs_up_ice`, `stairs_down_ice` where the atlas has them. The stage editor's inspector has **Floor theme** (the
  themes found in the sprite list) and draws the map with it.
- **The player's look**: `settings.json`'s `"playerLook"` (e.g. `"player_mage"`) is drawn on the map and in the menus.
  There is no Settings-page row for it yet (it would change the Settings screens and their reference screenshots).

## 5. Results

**In the game (2026-10-19).** All 230 sprites are in `art_raw/sprites` and on the game atlas (260 sprites, one
502 × 682 page; `atlaspack refs --import new` brings new files into the embedded project, then `build --all-variants`).
The data:

| Made | Where |
|---|---|
| 60 monsters | `enemies.json`, draft stats by tier |
| 50 NPCs | `npcs/<id>.json` + `dialogue/<id>.json`, one chat line each |
| 50 event items | `items.json`, type `story` |
| 10 gate keys | `items.json`, `"opens": "gate:key_<theme>"`, `"door": "door_<theme>"` |
| names, descriptions | `text.json` (zh_TW, en) |

The UI font subset was rebuilt for the new names (2292 → 2431 characters).

**The review.** Every sprite was looked at on a contact sheet. About 50 were redone:
- creatures that came out as people, and sheets of several small figures, with sharper descriptions ("no arms, no legs,
  not a person"; "one single character") and new seeds;
- a best-of-5 seed search for the hardest ones;
- the flame door and its key stayed grey in every try, so they were recoloured from their own shading into a fire
  gradient (outline kept).

The first tries are kept in `AIGamestyle/output/toms_sprites/_first/`.

**Simple animation.** `tools/make_idle_clips.py` gives every monster and draft NPC without one a looping idle clip,
picked from its name:

| Motion | For | Count |
|---|---|---|
| hover: bob up, wings squeeze | bat, pixie, ghost, drake, owl … | 14 |
| squash and stretch | slimes, worms | 4 |
| slow breathing | golem, troll, mammoth, yeti … | 15 |
| sway left / right | skeleton, knight, mummy, lich … | 15 |
| small hop | everything else | 12 |
| gentle breath | the 50 new NPCs (`npc_idle.anim`) | 50 |

Clips already there (the original seven monsters, hand-made in anim_editor) are never touched. Each clip's length is
nudged by its id, and the game offsets every tile's loop, so a room does not move in step. NPCs can now have an idle
clip (`idle_<npc id>` in `assets/media/anim/npc_idle.anim`, resource `anim.npc_idle`); the five shipped NPCs have none,
so their screenshots stay the same. Edit any clip in anim_editor; `make_idle_clips.py` only adds missing ones.

Checked on a test floor (ice theme, eight new monsters, four NPCs, the emerald and flame gates, items): everything
draws, every monster and NPC moves between frames, the gates show their own doors.

## 6. Known limits

- **32 pixels is small**: a detailed description loses detail; descriptions say what the creature is in a few words.
  Some sprites need a redo with another seed (`--only <id> --force --seed N`).
- **Draft types**: stats follow the tier, not a design; NPCs have one chat line; event items have a generic
  description. They are marked `"draft": true` for rewriting.
- **Wall and floor tiles are not seamless** by construction; they repeat like the original ones (a 32-pixel tile).
- **Idle clips are simple** (one motion each, from the name); hand-make better ones in anim_editor.
- **Tile letters `y` / `b` / `r` are the coloured doors** in a stage file: do not use them for anything else.
