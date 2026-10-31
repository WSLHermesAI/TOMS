# Team plan: a programmer, a game designer and an artist

How three people keep building TOMS together: who owns what, how each kind of content (UI, events, stages, art,
animations, effects, text) moves from an idea into the game, how the work is shared in Git without overwriting each
other, and what to do in the first weeks.

中文：[zh_TW/11_TEAM_PLAN.md](zh_TW/11_TEAM_PLAN.md)

---

## 1. The three roles

| Role | Owns | Mostly uses | Builds code? |
|---|---|---|---|
| **Programmer** | `src/`, `tools/` (code and scripts), `tests/` (C++ and CMake), the build, `Editor/bin`, the web publish | Visual Studio, `tools\build.cmd`, `tools\test.cmd` | yes |
| **Game designer** | the rules and numbers in `assets/data/` (items, enemies, weapons, store, skills, missions), the floors (`stages/`), the events, dialogue and story, the text (`text.json`), test scenarios (`tests/scenarios/`) | Stage & event editor, web editors, a text editor for JSON, the game (`Editor\TomsEditor.cmd`) | no |
| **Artist** | the raw art (`art_raw/`), the atlases (`assets/media/atlas`, `styles/`), animations (`assets/media/anim`), effects, fonts, the UI's look (`assets/media/ui/*.rcss`), sounds (`assets/media/sfx`) | Atlas, anim, particle and font editors, the AI pipeline (AIGamestyle), an image editor | no |

"Owns" means: that person decides, and changes to those files go through them. Anyone may *suggest* a change in
another person's area, but the owner makes or approves it.

Some work always needs two people. The rule: **the one who needs the change asks; the owner does it.**

| Shared work | Who |
|---|---|
| A new kind of screen, or a screen showing new information | designer: what it shows; artist: the look (`.rcss`); programmer: the data behind it (`ui_state.h`, `game_ui.cpp`) |
| A new kind of event, item effect or battle rule | designer: the rule and its data; programmer: the code that runs it |
| A new character or weapon with battle clips | artist: sprites and clips; designer: its numbers; programmer: only if it needs a new status or clip name |
| A new art style | artist ([03](../engine/03_ART_STYLES.md)); programmer: only if the style needs code |

---

## 2. Setting up each person

**Everyone:**

1. Install Git and **Git LFS**, run `git lfs install` once, then clone. Without LFS the editors in `Editor\bin` are
   tiny text files and Windows says *"This app can't run on your PC"*.
2. Install **Python 3** (the web editors, `make_ui_font.py`, `refresh_meshes.py` and the AI pipeline need it).
3. Read [Editor/README.md](../../Editor/README.md): every editor, how to start it, what it makes.

**Programmer:** the full build: [01_INSTALL_WINDOWS.md](01_INSTALL_WINDOWS.md), [09_NEW_MACHINE_SETUP.md](09_NEW_MACHINE_SETUP.md),
[02_VISUAL_STUDIO.md](02_VISUAL_STUDIO.md). The web build: [05_BUILD_WEB.md](05_BUILD_WEB.md).

**Designer and artist:** nothing to build. Use the ready-made editors in `Editor\` and run the game with
`Editor\TomsEditor.cmd`: it plays this repo's `assets`, so what you save is what you see. The programmer refreshes
`Editor\bin` when the code changes (§6).

**Artist, for AI art:** ComfyUI and the models in the AIGamestyle guide (`AIGamestyle/Doc/Reskin_Game_Art.md`),
[../engine/13_AI_RESTYLE.md](../engine/13_AI_RESTYLE.md), [../engine/14_AI_SPRITES.md](../engine/14_AI_SPRITES.md).

---

## 3. How each kind of content gets into the game

### 3.1 Seeing your change

| You changed | Shows in the running game |
|---|---|
| Art (atlas), animations, effects | by itself, within a second (hot reload: the editors and debug builds) |
| UI layout or style (`.rml`, `.rcss`) | as you type in the UI editor; in the game, as soon as the file is saved; **F8** opens the UI debugger |
| Game data (`assets/data/*.json`, stages, events, dialogue, text) | restart the game |

### 3.2 A floor (stage) — designer

1. `Editor\StageEventEditor.cmd`, pick the floor ([../gameplay/02_STAGE_EDITOR.md](../gameplay/02_STAGE_EDITOR.md)).
2. Paint walls and floor, place monsters, items, doors, keys, NPCs and events. The editor refuses what does not fit and
   lists problems at the bottom (unreachable objects, missing stairs, unknown events). Fix every ⛔ before saving.
3. Save, restart the game, walk the floor.
4. Big changes to many floors: agree with the others first; [../gameplay/06_FLOOR_REDESIGN.md](../gameplay/06_FLOOR_REDESIGN.md) is the plan.

### 3.3 Events, NPCs, dialogue, story — designer

- The event editor (in the Stage & event editor, or `Editor\WebEditors.cmd`): pools per act (`assets/data/events/`),
  event kinds, NPC dialogue (`dialogue/`, `npcs/`), story chapters (`story/`). Guides: [../gameplay/event_editor/README.md](../gameplay/event_editor/README.md),
  its tutorial [../gameplay/event_editor/04_TUTORIAL.md](../gameplay/event_editor/04_TUTORIAL.md), logic [06](../gameplay/event_editor/06_EVENT_LOGIC.md),
  NPC dialogue [08](../gameplay/event_editor/08_NPC_DIALOGUE.md).
- A reward mini-game (gold or crystals): add it to `assets/data/minigames.json` and start it from a dialogue choice
  (`{"type": "miniGame", "id": "..."}`); no code needed ([07](../gameplay/07_GAMEPLAY_PLAN.md) §16).
- `events/kinds.json` does not exist until someone edits an event kind in the Kinds tab; the game warns about it once
  and uses the built-in kinds. Creating it removes the warning.
- **An event that needs something the game cannot do yet** (a new action type, a new condition): write down what it
  should do and give it to the programmer.

### 3.4 Items, weapons, monsters, numbers — designer (+ artist for art)

- Items and weapons: `items.json`; weapon levels and the attack play: `weapon_play.json`; monsters: `enemies.json`;
  store, forge, skills, missions: their own JSON files. Their meaning is in [../gameplay/07_GAMEPLAY_PLAN.md](../gameplay/07_GAMEPLAY_PLAN.md).
- Every name or text the player sees is a key in `text.json` with all six languages (§5).
- A new monster or item needs a sprite: ask the artist for the sprite name first, and use that name.
- Lock important rules with a **test scenario**: a small JSON in `tests/scenarios/<name>/` that sets up a fight or a
  dialogue and says what must happen ([../gameplay/08_TEST_SCENARIOS.md](../gameplay/08_TEST_SCENARIOS.md)). The programmer runs them.

### 3.5 Sprites and atlases — artist

1. Put the PNG in `art_raw/sprites/` (32 × 32, transparent background), named as agreed with the designer. AI-made
   sprites: [../engine/14_AI_SPRITES.md](../engine/14_AI_SPRITES.md).
2. In the atlas editor open **`art_raw/atlas/game.atlasproj`** (never the cooked `assets/media/atlas/game.atlas`),
   *Import From References*, Save. Without a window:
   `atlaspack refs art_raw\atlas\game.atlasproj --import changed` and
   `atlaspack build art_raw\atlas\game.atlasproj --all-variants` ([../engine/04_ATLAS_TOOL.md](../engine/04_ATLAS_TOOL.md)).
3. After **any** atlas rebuild: `python tools\refresh_meshes.py` (the bent sprites' meshes).
4. Renaming or deleting a sprite that the data or UI still uses breaks the game: tell the designer and programmer.

### 3.6 Animations — artist

- The anim editor ([../engine/05_ANIMATION.md](../engine/05_ANIMATION.md), recipes [06](../engine/06_ANIMATION_RECIPES.md), bending sprites
  [../engine/17_SKINNING_TUTORIAL.md](../engine/17_SKINNING_TUTORIAL.md); Help > Skinning Tutorial in the editor).
- **Clip names are a contract with the code.** A character's clips have a `body` and a `hand` node; a weapon's clips are
  named per battle status (`idle`, `charge`, `attack`, `miss`, …) in its `battleAnim` in `items.json`. A new status or
  node name needs the programmer.
- Some files are written by scripts, and running the script again overwrites hand edits:

  | File | Script |
  |---|---|
  | `weapon_bow.anim` | `tools/make_bow_anim.py` |
  | `archer_bow.anim` | `tools/make_archer_anim.py` |
  | `weapon_sword.anim`, `fighter_sword.anim`, `weapon_staff.anim`, `caster_staff.anim` | `tools/make_weapon_anims.py` |

  Before editing one of these by hand, ask the programmer to retire its script, so nobody runs it again.
  (`tools/make_idle_clips.py` is different: it only adds the missing `idle_<id>` clips to `monster_idle.anim` and
  `npc_idle.anim`, and never touches a clip that is already there, so hand-made idle clips are safe.)
- Check a file before committing: `anim_editor --headless check <file.anim>`.

### 3.7 Effects, fonts, 3D — artist

- Particle effects: the particle editor ([../engine/07_PARTICLES.md](../engine/07_PARTICLES.md)).
- Picture fonts: the font editor ([09](../engine/09_FONT_TOOL.md), [10](../engine/10_FONT_TOOL_GUIDE.md)).
- The UI's text font is made from the text: after new characters appear in `text.json` or an `.rml`, run
  `python tools\make_ui_font.py` (otherwise those characters show as boxes on the web and phones).
- 3D models (the title scene): the glTF viewer ([../engine/08_GLTF.md](../engine/08_GLTF.md)).

### 3.8 The UI — all three

- Screens are `assets/media/ui/<screen>.rml` (structure) + `.rcss` (look), laid out at 1024 × 768 ([../gameplay/01_RMLUI.md](../gameplay/01_RMLUI.md)).
- **Artist:** colours, sizes, spacing, positions: the `.rcss` files, and the static parts of an `.rml`. Use the **UI
  editor** (`Editor\UiEditor.cmd`): the game beside the text, every change shown as you type, any screen through
  Preview, long text through Language, and Inspect to find which rule styles an element. Any text editor works too:
  the running game reloads a saved file by itself.
- **Designer:** which information and buttons a screen has: describe it, or sketch it in the `.rml`.
- **Programmer:** anything bound to game data (`{{...}}`, `data-if`, `data-for`, `data-event-click`): the values in
  `ui_state.h`, filled in `Game::buildUiState()`, button actions in `Game::uiEvent()` ([01](../gameplay/01_RMLUI.md) "Adding a screen").
- Sprites in the UI come from the game atlas by name (`<img sprite="coin"/>`): no UI change when the atlas is repacked.

### 3.9 Sounds — artist (designer decides when they play)

`assets/media/sfx/*.wav`. Replacing a file keeps its name and needs nothing else. A new sound for a new moment needs the
programmer to play it.

---

## 4. Git: sharing without overwriting each other

### 4.1 The everyday loop

1. **Before starting:** `git pull`.
2. Work in small steps. **Commit often**, one subject per commit, a message that says what changed
   ("stage 3: second key behind the slime", not "update").
3. **Before pushing:** `git pull` again, open the game once, then `git push`.
4. Larger work (a new system, a redesign of several floors, a new art style): a **branch** and a pull request on
   GitHub, so the others can look before it lands. Small data and art changes can go straight to `main`.
5. Never commit build output (`Build/`), personal settings, or a save from your own playing.

### 4.2 Files that conflict, and the rule for each

| File | Why it conflicts | Rule |
|---|---|---|
| `assets/data/text.json` (1400+ keys, six languages) | everyone adds text | Pull right before editing, push right after. Add new keys next to their group (`npc.*`, `item.*`, …), never re-sort or re-format the file. In a conflict keep **both** sides' keys |
| `assets/media/atlas/*` and `styles/*/atlas/*` (`.atlas`, `.png`, `.mesh.json`, `.mesh.bin`) | generated binaries: cannot be merged | **Only the artist** rebuilds and commits them. After a merge conflict: take either side, then rebuild from `art_raw` (§3.5) and commit the result |
| `art_raw/atlas/*.atlasproj` | rewritten on every save | only the artist |
| `assets/data/stages/*.json` | one floor = one file | one person per floor at a time: say which floors you are working on |
| `items.json`, `enemies.json`, `weapon_play.json` | the designer's | others ask the designer |
| `Editor/bin/**` (Git LFS, ~116 MB) | binaries | **only the programmer**, with `tools\package_editors.cmd` (§6) |
| `src/core/game/ui/ui_state.h`, `game_ui.cpp` | every new UI binding touches them | only the programmer |

**Never** resolve a conflict in a generated file by hand-editing it. Regenerate it.

### 4.3 The AIGamestyle submodule

The AI art pipeline lives in its own repository (`AIGamestyle/`). Commit there first, push it, then commit the new
pointer in TOMS. Everyone who pushes to it needs write access to that repository.

---

## 5. Text and languages

- Every player-visible string is a key in `assets/data/text.json` with **zh_TW, en, zh_CN, ja, ko, es**. A missing
  language falls back to another, so the game still runs, but write all six before a release.
- The designer writes zh_TW and en; the others can be drafted with an AI translator and checked by a native reader.
- After new text: `python tools\make_ui_font.py` (the artist or whoever added the text). The test
  `data.ui_font_covers_text` catches a forgotten run.
- The editors' own Chinese (menus, tooltips) is in `tools/studio_common/i18n/zh_TW/*.json`: the programmer's.

---

## 6. Builds, tests and releases (programmer)

- **Tests:** after a change, `tools\test.cmd` (only the tests the change can break); before a release or publish,
  `tools\test.cmd all` ([08_TESTS.md](08_TESTS.md)). Designers' scenarios and artists' asset checks run here too:
  pull, build, run, and tell the person whose change broke a test.
- **Data checks anyone can run without building:** the stage editor's Checks panel, `anim_editor --headless check`
  (in `Editor\bin`), `python tools\refresh_meshes.py --check`, `python tools\make_ui_font.py --check`.
- **Editors:** when code changes an editor, `tools\package_editors.cmd` refreshes `Editor\bin`; commit it as its own
  commit ("Editor: rebuilt for …") and tell the others to pull. It deletes files it does not ship from `Editor\bin`
  (for example a game run's `save\`); move those aside first.
- **The web version:** `tools\release_web.cmd`, then `tools\publish_web.cmd` (GitHub Pages, [07_PUBLISH_GITHUB_PAGES.md](07_PUBLISH_GITHUB_PAGES.md)).
  Publish from a commit everyone has pulled, after `tools\test.cmd all`.
- **Release rhythm:** once a week, the programmer builds `Editor\bin` and the web version from `main` and posts the link,
  so the designer and artist always test the same build players see.

---

## 7. Working rhythm

| When | What |
|---|---|
| Monday, 30 minutes | What each person does this week; which floors, files and sprite names are claimed; what each needs from the others |
| Every day | Pull in the morning, push at the end of the day; a line in the team chat for anything that blocks someone |
| Friday | The programmer builds and publishes; everyone plays the week's build for 15 minutes and writes down problems |
| Each piece of work | The progress report ([progress_report/](progress_report/PROGRESS_REPORT.md)) gets a line: what changed, who, what is left |

**Requests between roles** go in GitHub Issues (or one shared list), one request per issue, with who asked, who does it,
and what "done" means (a screenshot, a passing scenario, a sprite name).

---

## 8. The first four weeks

### Week 1 — everyone can work alone

| Role | Tasks |
|---|---|
| Programmer | Make sure the designer and artist can run everything in §2 on their PCs. Turn back on the tests switched off in `tests/test_switches.json` that pass, and restore the reference images and test data commit `795385a` deleted (or remove those tests), so `tools\test.cmd` means something again. Refresh `Editor\bin` and publish the web build |
| Designer | Play the whole start: New Game, the weapon master, stage 1, the peddler and the shrine. Write down every number that feels wrong (points per weapon level, wins per level, damage, rewards). Read [../gameplay/07_GAMEPLAY_PLAN.md](../gameplay/07_GAMEPLAY_PLAN.md) and the event editor tutorial |
| Artist | Follow the skinning tutorial ([17](../engine/17_SKINNING_TUTORIAL.md)) once. List the art the game still borrows: the bow is made of effect shapes, the sword and staff are generated placeholders, the player's battle character is the 32-pixel map sprite |

### Week 2 — the first real content

| Role | Tasks |
|---|---|
| Programmer | Give the old browser saves the starting bow (a save with no weapon); fix the attack track's UI warnings (a `data-for` binding, [01](../gameplay/01_RMLUI.md) "Adding a screen"); data reload without restarting the game, so the designer can tune live |
| Designer | Tune stage 1–3: numbers in `weapon_play.json`, `enemies.json`, the rewards in `minigames.json`; a test scenario for each rule you change |
| Artist | Real bow, sword and staff art and their clips (keeping the clip names); the battle screen's look (`battle.rcss`) |

### Weeks 3–4 — growing the game

| Role | Tasks |
|---|---|
| Programmer | The sword's and staff's own attack plays ([07](../gameplay/07_GAMEPLAY_PLAN.md) §5.2 polygon, §5.3 circles); splitting `text.json` per area so fewer people edit the same file; a GitHub Actions build that runs the tests on every push |
| Designer | Floors 4–10 with the stage editor and the redesign plan ([06](../gameplay/06_FLOOR_REDESIGN.md)); events and dialogue for act 1; named moves and mastery (§5.4, §6) as written rules for the programmer |
| Artist | Monsters and NPCs for act 1 (AI pipeline + hand cleanup); a skinned battle character with a few clips; new effects for hits and level-ups |

---

## 9. What would make working as three easier (programmer backlog)

| Now | Better | Why |
|---|---|---|
| Game data needs a restart to show | reload `assets/data` on a key (like F5 for the UI) | the designer tunes numbers many times a minute |
| One `text.json` for every string | one file per area (UI, items, NPCs, story) | fewer conflicts |
| Tests run only on the programmer's PC | GitHub Actions on every push | the designer and artist learn at once what they broke |
| Many tests switched off, some reference images missing | all on, all present | a passing run really means it works |
| The stage editor cannot start the game on the floor being edited | *Play from here* | the designer's shortest loop |
| Events that need code are found late | the event editor marks actions the game does not know | requests reach the programmer early |
