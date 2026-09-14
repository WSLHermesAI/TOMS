# Progress Report — Log Part 5 (2026-09-13)

> Archived log entries split out of `PROGRESS_REPORT.md` (the single file grew too large).
> That file still carries the live **Next Step** banner, the milestone status table, and
> **Open Questions / Blockers** — this file is chronological log entries only, read top to
> bottom, oldest first. See `PROGRESS_REPORT.md`'s Log Index for the full list of parts.

---

### 2026-09-13 — Design documents committed (70 floors / 15 endings / 50 enemies) and this status board updated

**Commits:** `af29333` "Story + art design document set (70 floors, 15 endings, 50 enemies, 20
equipment)" and `043c6c9` (this progress report update -- Next Step, milestone rows M10/M11 plus
S1-S8, the log entry, and the Phase S appendix in `IMPLEMENTATION_ROADMAP.md`). Both are pushed to
`main` and verified with `git ls-remote`.

**What is now committed (documents only -- no game code changed):**

| File | Content |
|---|---|
| `docs/STORY_BIBLE.md` v3 | 修仙 reincarnation main line; 70 floors = 10 acts x 7; one seal (and one system unlock) per act; 8 cross-act choices feeding `insight` / `resolve` / `humanity`; 15 endings via a priority condition tree; rebirth rules (half power, all items reset) |
| `docs/SIDE_STORIES.md` v3 | 10 flagship side stories, one per act on that act's 5th floor, all optional; two choice-gated (`ss_09`, `ss_10`), several mirroring main-line choices; per-floor minor event pools |
| `docs/STORY_DATA_SCHEMA.md` v3 | per-floor files `F01..F70`, `stage.story` blocks, 4 new condition types, maze scaling table to 63x42, `endings.json`, `cycles.json`, save v3, 16 validators |
| `docs/ART_AND_ABILITY_DESIGN.md` | 50 enemies (40 regular + 10 unique bosses, each with a full ComfyUI prompt), 20 equipment with gameplay values, 36 statuses, 1/2/4-grid footprint rules, 1,006 animation frames + 469 static images, 3 MB art budget |

**How the design got to this shape (owner-driven revisions, in order):** first pass was 10 floors
with 4 endings and a simple rescue premise; the owner then asked for **70 floors** with map size
growing per floor, **more than 10 endings** (many bad, few good, triggered by choices made on
different floors and by items never obtained) and **replay at half power with items reset**; then
asked for the art/ability document (50 enemies, 20 equipment, status count -> asset count, ComfyUI
prompts, unique bosses, shared-looking enemies differentiated by shader colour and per-part scale);
and finally that **one cell has 4 grids, so some enemies occupy 2 or 4 grids** as visual signalling
for boss/special-event -- that rule is now section 1.7 / 3.6 of the art document and is the reason
S1 is first in the order of work.

**Nothing in these documents is implemented.** They are the contract the next milestones build
against; the engine currently still ships the 11 hand-authored stages, the M6 press-and-hold battle
(now replaced upstream by battle v2) and no skill tree, forging, hub, endings resolver or rebirth.


### 2026-09-13 (later) — Source-layout refactor: `game.cpp` 3,104 -> 268 lines, camera is its own class

Owner request: *"optimize the code, especially one file over 800 lines, ex: camera system should not
be in same file, it should have its own file and class to make code easy to maintain."*

**What was wrong.** `src/game/game.cpp` was a single 3,104-line translation unit holding assets,
text/bar drawing, the whole scene draw, input routing, the backpack UI, shop + stage select + pause
menu, combat resolution, story/mission glue, title/save glue and the debug overlay. The camera's six
state fields (`camX_`, `camY_`, `camTargetX_`, `camTargetY_`, `viewCols_`, `cameraMode_`) and its
math were scattered through `Game`.

**What changed.**

1. **`toms::Camera` is now its own class in its own file** (`src/game/camera.h/.cpp`, 85 + 57 lines):
   mode (Follow/Rooms), zoom (`viewCols`, clamped to >= 4), current + target position, viewport
   conversion (tile size / visible columns / rows), edge clamping, small-maze centring, Rooms
   section alignment and the frame-rate-independent ease. It is deliberately free of Renderer/Stage/
   Player coupling — the owner of a Camera feeds it the viewport in pixels, the grid size and the
   focus tile — so it is pure math and unit-testable. `Game` keeps one adapter
   (`Game::cameraViewportTiles`) because only `Game` knows the renderer's size.
2. **`game.cpp` split by responsibility** into nine units, with `Game` unchanged as a class
   (`game.h` still declares everything; only the definition site moved):
   `game_assets.cpp` (224), `game_text_draw.cpp` (171), `game_scene_draw.cpp` (430),
   `game_input.cpp` (277), `game_inventory.cpp` (274), `game_store.cpp` (629),
   `game_combat.cpp` (150), `game_story.cpp` (162), `game_title_glue.cpp` (411). New shared headers:
   `game_helpers.h` (inline `C4` / `trParam` / `readJsonFile` / `cellSprite` / `entSprite` / sprite
   order / virtual-pad table / `g_textGame`), `game_condition.h` (`GameConditionContext`),
   `game_internal.h` (the include set the units share). Largest file is now 629 lines -- nothing
   above the 800-line threshold -- and `game.cpp` is 268.
3. **New test: `camera_test.cpp`** (30 checks) -- zoom-to-pixel mapping, viewport rows, clamping to
   >= 4 columns, edge clamping, centring when the maze is smaller than the viewport, Rooms section
   alignment, mode index round-trip, snap vs ease, no overshoot, settling exactly, and
   frame-rate independence (one 200 ms step == 4 x 50 ms).
4. **`texture_test` fixed** (the last accepted M0 gap): it failed to compile because the target
   lacked GLFW's include dir while it builds `renderer.cpp`; with that fixed it then failed 2 checks
   because it pointed at `assets/font_atlas.png`, which the 2026-09-11 multi-language pull deleted
   (bitmap atlas -> TTF subsetting). It now points at a tracked PNG. **17/17 test binaries pass.**
5. New reference doc: **`docs/CODE_LAYOUT.md`** (TC) -- per-file responsibilities, the boundary
   rules for where new code goes, and the `Camera` interface.

**Verification (behaviour must be unchanged).**
- `tower_vulkan` builds; `./build_web.sh webgl` builds the Emscripten target from the same source list.
- All 17 test binaries pass, including the new `camera_test: ALL PASS`.
- Native walk under Xvfb with real key events: title -> New Game -> dungeon, then hold Right/Down --
  **17.2%** of the frame changed (the maze scrolled; an idle frame differs by 1.5%, which is just the
  title pulse), the player sprite moved, and the HUD / virtual pad / bottom story line all still drew
  correctly.

No gameplay values, drawing order or data formats changed; this was a pure move of definitions plus
one extracted class. Next step is still **S1** (entity footprint), unchanged.

### 2026-09-13 (later still) — M2 closed on the browser backend: ImGui runs on WebGL2

Owner picked **M2** as the next piece of work, with the reasoning *"this won't change current
feature"*, and separately ruled **M5 out of the plan** (*"ImGui canny fit all UI features"*).
Both are now recorded above (M2 row / M5 row) and in `IMPLEMENTATION_ROADMAP.md`.

**What M2 was missing.** ImGui had only ever been wired for the Vulkan desktop backend
(`imgui_layer.cpp` = GLFW + Vulkan). The browser target had no ImGui at all — and no SDL/GLFW
either: `emscripten_main.cpp` drives the canvas through raw Emscripten HTML5 callbacks, so there was
no platform backend to reuse and none vendored for it.

**What was built.**
- `src/engine/imgui_web.{h,cpp}` — the missing browser backend: ImGui core + `imgui_impl_opengl3`
  in its ES3 flavour (WebGL2 needs no GL loader under Emscripten), plus a DOM event bridge feeding
  mouse / wheel / touch / keyboard / focus into ImGui's event queue. DisplaySize is the canvas
  *drawing-buffer* size (not the game's 1024x768 design space — the game maps design->buffer in its
  own shader, ImGui draws real pixels), and mouse coordinates are mapped CSS px -> buffer px.
- `emscripten_main.cpp` — ImGui comes up once the WebGL2 context is current; the frame is bracketed
  `beginFrame()` -> `update()` -> dev windows -> `draw()` -> `endFrame()` (ImGui renders last, on
  top); **F1 / F2** toggle the debug overlay and the styling spike, the same keys the desktop entry
  uses; `jsGamepad` and the key handler now yield to ImGui when a dev window owns the pointer or the
  keyboard, so dragging an ImGui slider cannot also walk the player.
- `game_scene_draw.cpp` / `game.h` — the F1 overlay, F2 spike and font-scale setter are compiled on
  every backend now. The ImGui toast windows and the ImGui stage-select window stay desktop-only on
  purpose (locale/CJK strings would render as tofu boxes with ImGui's built-in font, and the browser
  build already draws its own — enabling them would change what a browser player sees).
- `CMakeLists.txt` — the ImGui `FetchContent` moved out of the desktop-only block (the web target
  needs `${imgui_SOURCE_DIR}` too); the web target now compiles ImGui core + `imgui_impl_opengl3`
  with `IMGUI_IMPL_OPENGL_ES3`.

**Verified (real evidence, both platforms).**
- Browser (WebGL2, local build `575c5d2-20260913124217`): `window.__tomsReady === true`, canvas
  1024x768, F1 shows *TOMS Debug* with live game values (State: MainMenu/Explore, Stage: stage01,
  HP 120/120, ATK 12, DEF 4, LV 1, EXP 0, Gold 0, "Visible cells 13" straight out of the refactored
  `toms::Camera`), F2 shows the *M2 Styling Spike* panel over its scene-graph backdrop rect.
- Browser regression after the change: the on-screen pad still drives the player (pad-up moved tile
  y 14 -> 10, pad-left/down/up all moved; pad-right is walled at the map edge) and a click that lands
  *on* an ImGui window leaves the player where it was (the capture guard works).
- Native (Xvfb, real key events): Enter/Enter into the dungeon, then F1 + F2 -- both windows render
  over the maze; note the F1/F2 toggles are intentionally skipped while the title phase is up
  (`main.cpp` line 152), which is why they only appear in-game.
- Both builds green: native `tower_vulkan` + all 17 test binaries; `./build_web.sh webgl`.
- Cost, stated plainly: web wasm **1,960,362 -> 2,548,484 B (+588 KB)** now that ImGui is compiled
  into the browser build.

**Not done (and why):** ImGui on the **WebGPU** backend. `navigator.gpu.requestAdapter()` returns
`null` for every browser reachable from this environment, so an `imgui_impl_wgpu` wiring could be
written but never verified — and the WebGPU build is not the one deployed. Left as the single open
M2 item rather than guessed at.

**Deployed later the same day** (see the deploy log entry below): the live Pages site now serves this
M2 build plus S1.

### 2026-09-13 — S1 done: entity footprints (1/2/4 grids) + floor roamers

Owner's decision ("So this will change the code? If it is do it"), after S1 was explained as: let an
enemy occupy 2 or 4 grids so a boss visually reads as a boss, instead of every entity being one tile.

**New files (own files + classes, same discipline as the camera refactor):**
- `src/game/footprint.h` — the 佔格 rules from `docs/ART_AND_ABILITY_DESIGN.md` section 1.7, as pure
  logic: only 1x1/2x1/1x2/2x2 are legal (F1 -- a 3-grid entity cannot be aligned to the 2x2 cell
  subdivision); `footprintCovers()` (anchor = top-left occupied tile); `footprintSortKey()` (F5: the
  bottom-most occupied row); JSON parsing that **rejects** an illegal size (with a warning) instead of
  shipping an unplaceable enemy; and `resolveFootprint()` -- the single place that decides *stage-file
  override > character type table > 1x1* (F8), shared by the game and the test.
- `src/game/roamer.{h,cpp}` — `toms::Roamer`, the floor wanderer: wander with a heading, detect radius
  6 / lose radius 9 hysteresis, greedy chase (bigger gap first, other axis as fallback, then wander so
  a cornered roamer keeps moving). Walkability arrives through a `GridQuery` interface, so it never
  touches Stage/Game/Renderer and is unit-testable on a hand-written maze. Two rules are hard
  requirements, not tuning: a candidate step is only legal when the entity's **whole footprint** lands
  on walkable tiles inside the grid (F3/F4 -- no phasing through walls, no half-standing outside the
  maze), and its seed comes from the stage id, so reloading a floor reproduces the same wander.
- `src/game/footprint_test.cpp` — 88 checks: footprint math, JSON forms and rejections, resolution
  priority, and the roamer (400-turn no-phasing soak, boundary clamping, detect/chase, hysteresis,
  a pocket it cannot fit in, same-seed determinism, corridor advance) **plus a data validator over all
  11 shipped stages**.
- `data/footprints.json` — character-level tiers (F8): golem/demon 2x1, demonlord_vorkath 2x2 + the
  banner name. A stage file can still override any single tile with its own `footprints` map.

**Gameplay wiring:** all occupied tiles now block and bump (F6); a multi-grid monster is never walked
into -- the player fights from the adjacent tile and the boss keeps its cell (F4); drawing is one
y-sorted pass keyed on the bottom-most occupied row with the player in the same sort (F5), instead of
"entities in file order, player always last", which only read correctly while everything was one tile
tall; sprites are footprint-sized (F2) and the 4-grid/roamer tier gets a name banner drawn with the
game's own CJK-capable text renderer. Roamers take one turn per player turn, seeded from the stage id.

**Six monsters moved by one tile** (stage06/07/09/10): they stood in 1-tile nooks that cannot host
their character-level tier -- stage10's boss had no 2x2 room at all. The tiles were edited mechanically
(nearest fitting anchor), preserving the files' CRLF and staying a 6-line diff.

**Verified.**
- `footprint_test: ALL PASS (88 checks)`, including: *11 multi-grid entities across 11 stages*, no
  illegal sizes, nothing standing on a wall, no overlapping entities, and **the stairs stay reachable
  on every shipped stage with the enlarged monsters blocking** (the anti-softlock gate a generated
  floor will have to pass too).
- 18/18 test binaries pass; native `tower_vulkan` and `./build_web.sh webgl` both green.
- Native (Xvfb, real keys): the load log reports `footprints.json: 3 type(s) bigger than 1x1` and
  `stage_01: 2 entity(ies) occupy more than 1 grid`; the 2x1 golem draws two tiles wide; the 2x2 boss
  draws with its name banner; bumping the golem's **non-anchor** tile starts the fight (F6).
- Roamer, from the game's own turn log: `turn 1: (10,9)->(10,10) mode=wander`, ... `turn 7:
  (15,10)->(16,10)`, `turn 8: (16,10)->(15,10)` (corridor end -> reversed), ... `turn 13:
  (11,10)->(10,10) mode=chase player=(7,12)` -- one step per player step, heading kept, walls respected,
  and the mode flipping to chase at distance 6.
- Web (WebGL2, local build, browser harness): page runs; the 2x1 golem draws two tiles wide; stepping
  onto its anchor tile starts the battle **and the player stays at (5,13)** (bump, never inside the
  boss), `jsCombatInfo` reports active=1 / enemyHP 70, battle scene renders.

**Not done, on purpose:** no roamer is placed in shipped data yet -- the two designed ones (王座之影,
前世道兵王) are floor *events* that arrive with S2's generator, and the remaining roster's tiers (8
two-grid types, 12 four-grid) come with the art pass (S8). The engine, the type table and the
validator are in place, so both are data work now.

**Both temp demo edits reverted** (the stage01 demo monsters and a temporary roamer trace), so the
committed data is the real 11-stage set.

### 2026-09-13 — Deployed: S1 (footprints + roamers) is live on GitHub Pages

Owner: "Commit and push then deploy." Followed the `toms-web-deploy` recipe (that skill is the
reference for this pipeline).

- `git pull --ff-only origin main` -> already up to date (no upstream commits underneath).
- Build: `bash build_web.sh webgl` -> wasm 2,598,623 / js 355,067 / data 211,986 B,
  stamp **`e7366a0-20260913133106`**. The tracked `web/` copy was refreshed from `web-gl/` (all 8
  files verified byte-identical by md5) and committed to `main` (**`7845c23`**).
- gh-pages: worktree at `origin/gh-pages`, `scripts/deploy_clean.py` (current stamped set + the
  plain-named trio, kept the previously referenced `cbe8e17-...` set so a cached page cannot 404,
  pruned 3 stale sets, touched `.nojekyll`) -> **`c4346f9`** (`bc8ea05..c4346f9  HEAD -> gh-pages`).
- Live verification (`https://wslhermesai.github.io/TOMS/`), after the CDN caught up (~20 s):
  - page references `toms_web.e7366a0-20260913133106.js`; `<title>Tower of the Sorcerer</title>`,
    no `emscripten_logo`, no `id="output"`, no error banner element;
  - **every artifact returns 200 with a byte size equal to the local build** -- the three stamped
    files and the three plain-named copies (js 355,067 / wasm 2,598,623 / data 211,986);
  - driven in a real browser: `window.__tomsReady === true`, Enter/Enter into the dungeon, the
    on-screen pad's move path walked the player from (1,14) to (4,14) via `jsPlayerInfo`;
  - **S1 is genuinely live, not just the page**: the deployed build's own log reports
    `[assets] footprints.json: 3 type(s) bigger than 1x1`, and the served `.data` contains
    `monster:demonlord_vorkath` -- i.e. `data/footprints.json` is inside the bundle.
- Nothing else changed in the deploy; `main` stayed at the S1 commit for code and `7845c23` for the
  artifact copy.

### 2026-09-13 — S2 done: `gen_floors.py` generates the 70-floor data set (+ V7/V8/V9/V12)

Owner: "Do next (it should be S2?)" — yes, S2 is `tools/gen_floors.py` + the floor table, which
`STORY_DATA_SCHEMA.md` section 13 lists as its M1 ("70 層可生成、可走通").

**What was built.**
- `tools/gen_floors.py` — reads the section-5.1 / STORY_BIBLE section-5 floor table and writes, per
  floor: the **spec** (`data/story/floors/Fnn.json`, the section-4 schema) and, for the 60 non-boss
  floors, a **playable grid** (`Fnn.stage.json`) in the existing stage schema. Boss floors (F07, F14,
  … F70) carry `handAuthoredStage` pointing at the ten `data/stages/*.json` files section 3.2 maps
  them to, and get no generated grid. The maze reuses `tools/gen_mazes.py`'s Wilson + BFS (the doc
  requires reusing it), keeps that file's conventions (cell = 2x2 tiles with one wall between
  neighbours, so every passage is 2 tiles wide), carves the table's `rooms` as open clusters ("event
  containers") and opens `loops` extra walls; the remainder of the table's dims becomes wall padding on
  the right/bottom edge, so the dims come out exact for V9. Entities are placed at cell anchors with
  footprint-aware chars, and the door/key pair uses `reachable_without_edge` so the key is always on the
  entrance side.
- `tools/validate_story.py` — V7, V8, V9, V12 plus footprint legality/overlap and the side-story
  placement: 70 specs + 60 grids, **6745 checks, ALL PASS**. V8 is the interesting one: besides "one
  player start / stairs present / the whole maze reachable with doors open", it verifies that **every
  door's key is reachable with the doors still shut** — a softlock check.
- Engine: `loadStage()` gained one fallback (`data/story/floors/<id>.stage.json` after the two
  `data/stages/` forms), so generated floors load through the unchanged `parseStage`, in the file layout
  section 1.2 asks for.
- `footprint_test` (S1's C++ gate) now also validates the 60 generated grids **through the runtime's own
  parser**: 71 stage files, 430 multi-grid entities, all footprints legal/on floor/non-overlapping, and
  the stairs reachable with the bigger blockers. 18/18 test binaries pass.

**Two real bugs caught by the new checks (both fixed, which is the point of writing them).**
1. **Softlock on 2 of 60 floors**: `validate_story` V8 flagged `F40`/`F59` "key yellow sits behind its
   own door" — the generator's first key pick searched the main path without restricting it to the
   entrance side of the door. Fixed by reusing `gen_mazes.py`'s `reachable_without_edge` (same rule that
   file already applies).
2. **The design document contradicted itself**: section 5.1's printed `tile_counts()` formula does not
   reproduce its own table from tier 4 upward (F29 35x26 vs 35x24, F43 43x30 vs 45x30, F70 53x38 vs
   63x42). The table is what STORY_BIBLE prints and what V9 checks, so the generator uses the exact
   piecewise form the table implies and **the formula in the doc was corrected** with a note.

**Verified.**
- `python3 tools/gen_floors.py` → 70 specs, 60 grids, 10 boss floors, 419 multi-grid enemy placements.
- `python3 tools/validate_story.py` → ALL PASS (6745 checks).
- `footprint_test: ALL PASS (388 checks)` over 71 stage files; 18/18 test binaries pass.
- **Playable proof**: with a temporary two-line change (newGame loads `F64`), the largest generated
  floor boots in the real native build — HUD 「第 64 層」, bottom line 「第 64 層 (ch_10)」, 63x42 maze with
  2-tile corridors and open rooms, 2-grid demons and skeletons rendering, camera following while
  walking (17% frame change). Both temporary edits were reverted and the tree rebuilt clean.

**Known follow-ups (not blockers).**
- The HUD's stage counter still reads "(64/11)" because `totalStages` comes from the 11 hand-authored
  stage files; the runtime progression into the 70 floors is its own phase (S3 owns the story/act data
  that drives it).
- Enemy mixes, item tables and the event pools are provisional until S3 (chapters + `data/events/`);
  `meta.eventPoolSource` records which floors used the built-in pool, so the swap is auditable.
- Not deployed: the generated floors are not reachable from the live build yet (no progression), so a
  deploy would only add unused data weight.

### 2026-09-13 — S3 done: story v3 + the four new condition leaves + save v3 (choices actually persist)

Owner: "Do S3 and ensure all jobs has written into progress md file." S3 is the phase
STORY_DATA_SCHEMA.md section 13 lists as M2+M3 ("前 21 層有敘事與事件", "分歧與計數器可保存").

**Code.**
- `condition.h/.cpp` — `choiceMade`, `sideStoryState`, `counterAtLeast`, `cycleIndexAtLeast`. They are
  *defaulted* virtuals on `ConditionContext` rather than pure virtuals, so every existing context (and
  every test mock) keeps compiling and simply answers "no choice made / counter 0 / cycle 1" — a mock
  that does not care cannot accidentally open a gated screen. The other three leaves the section lists
  (`cultivationTier`, `memoryShards`, `endingSeen`) belong to the phases that create their data (S4/S7)
  and are not invented here.
- `src/game/run_state.{h,cpp}` — `toms::RunStoryState`, its own class like the camera and the roamer:
  choices (irreversible ones keep the first answer), counters clamped to the range declared in
  `counters.json` with a `displayAt` visibility rule, side-story states, run flags, memory shards, floor
  progress + `clearedFloors` (what `stageCleared` reads), death counts, `reset(keepShards)` for rebirth,
  and `writeInto/readFrom(RunSaveData)` so persistence is one pair of calls instead of field copying.
- `save_system.{h,cpp}` — schema version 3 for both files: run gains choices/counters/sideStories/flags/
  floor/clearedFloors/shards/deaths, meta gains cycleIndex/endingsSeen/hintsUnlocked. **Section 9's
  compatibility rule is implemented, not just documented**: a pre-v3 file loads with the new fields
  defaulted and `*versionMismatch` set for logging — loading is never refused for a version difference.
- `Game` wiring: owns a `RunStoryState`; `newGame()` resets it; `runSaveFromState()` writes it;
  `applyLoadedRun()` reads it; `GameConditionContext` answers all four leaves from it (its constructor
  now *requires* the run state, so no call site can silently forget it); `loadStage()` records the
  current floor; and the dialogue action runner gained `makeChoice` (with the option's `setFlags` /
  `counters`, mirroring the chapter entry) and `addCounter`.

**Data.** `story.json` v3 (chapters + seals; `arc[]` kept as the projection section 12 asks for);
`chapters/ch_01…ch_03` in the section-4.1 schema with the real content from STORY_BIBLE §3/§4.1
(`c_motive` with three options; `c_gate_seal` with break/unseal + insight); `flags.json` (25 declarations
covering the whole 8-choice arc so V1 passes now and later phases only add content); `counters.json`;
`events/pool_common.json` + `pool_act01…03.json` (the three documented seeds per act plus variants in the
same namespace, kinds per section 12.1); and the boss-floor `story` blocks in stage01…03.
`villager_elder.json` gained the act-1 question as a **gated** row — its `requires` is
`not any(choiceMade(c_motive, …))`, i.e. real data using the new leaf.

**i18n.** `tools/gen_story_i18n.py`: 453 referenced keys, 86 authored 繁中/English, the rest backfilled
from zh_TW and marked `_todo` (section 11.2); idempotent, and it repairs its own earlier placeholders.

**Verified (real runs, not builds).**
- `run_state_test: ALL PASS (45 checks)` — choices/counters/side stories/flags/shards/floors/deaths,
  first-answer-wins, clamps, rebirth resets, the schemaVersion-3 round trip, and the pre-v3 migration on
  both the run and the meta file.
- `condition_eval_test` extended per section 12 rule 5; **19/19 test binaries pass**.
- `validate_story: ALL PASS (6834 checks)` — now also V1 (every `setFlags` is declared), V2 (every
  `choiceMade` names a real choice+option), V6 (all 453 i18n keys exist) and chapter↔data consistency
  (floors/boss floor/boss stage match the specs, mix and item ids exist in enemies.json/items.json).
- Native, live: an **old `saveVersion: 1` slot still loads** (the migration rule in the wild — it came up
  on the generated F64 floor unchanged), and the act-1 dialogue runs.
- Browser (WebGL2 harness probes added for exactly this): `jsTalk` → 4 choices; `jsChoose(1)` → the
  motive question (3 choices); `jsChoose(0)` → `jsChoiceMade(c_motive, opt_know_self)` **0 → 1**,
  `flag_motive_know_self` = 1; reopening the dialogue shows **3** choices (the gate closed); `jsSaveNow()`
  then reading `/save/slot1.json` out of IDBFS shows `schemaVersion: 3`,
  `choices: {"c_motive": "opt_know_self"}`, `flags: {"flag_motive_know_self": true}`, `floor: F01`.

**Two real bugs caught while verifying.** (1) The confirmation node I wrote had no way out — a dialogue
node with neither `choices` nor a closing option can never be dismissed, because `chooseDialogue()` only
leaves the dialogue through a chosen option with an empty `next`; it now has a closing row like the
elder's other nodes. (2) The i18n collector could not see keys inside JSON *arrays* (e.g. a floor's
`ambient` list), so 235 keys would have been silently missing; fixed and re-run.

**Not done, on purpose:** the remaining three leaves (`cultivationTier`, `memoryShards`, `endingSeen`),
`ch_04…ch_10` content, and V3/V4/V5/V13/V14/V16 — each needs the data its own phase creates. Not
deployed: the live site still serves the S1 build.

### 2026-09-13 — Gap found by the owner: S1–S3 are built but not applied in play

Owner, playing the live build: *"Now all stages looks same, and story seems not apply … when will story
and new 70 levels will be applied"*. Both halves are correct and the reasons are in the source:

- the stage list scans only `data/stages/` (11 files) and progression follows `st.down` among them, so
  the 70 generated floors are never named by anything and never load in normal play;
- `theme` is read by **no code at all**, so every stage renders with the same tiles — the acts have no
  visual identity;
- the floor/chapter story keys exist but only `stage.story_note` is drawn, so the story is invisible
  apart from the one S3 dialogue row (which does work and does persist).

This is not a defect in S1–S3 — those phases were scoped as data + primitives — but the **wiring step
was missing from the roadmap**, and without it two of the three finished phases are invisible to a
player. It is now item **S3.5** above, ahead of S4. Nothing in S4 (skills/forge/hub/actives) was
started; that question is answered plainly in the report so the status board cannot imply otherwise.

### 2026-09-13 — S3.5 (a)+(b) done: the 70 floors are the run's progression source (+ two real bugs found)

Owner asked *"Now all stages looks same, and story seems not apply … when will story and new 70 levels will
be applied"* — this is the wiring step that made S2's floors part of the game rather than data on disk.
Owner chose the minimal cut: floors playable + an honest counter, no story-visual or per-act-visual changes.

**Built.** `toms::FloorTable` (new `src/game/floor_table.{h,cpp}`, its own class like the camera/roamer/run
state): parses the 70 specs, orders them by **walking the `nextFloor` chain** so `seq` always matches the way
the run progresses, keeps act/actIndex/indexInAct/role/seal/name-key, and resolves a floor's map
(`data/story/floors/Fnn.stage.json` for the 60 generated floors, `data/stages/stageNN.json` for the ten act
bosses). `loadStage(id)` routes floor ids through it, then sets `st.id`/`st.index`/`st.name` and — the actual
progression mechanism — `st.up = nextFloor`, `st.down = prev floor`, so the existing stair code carries the
player floor to floor with no new input handling. `totalStages`, the hub list and `newGame()`'s start floor
all read the table; a missing table falls back to the historical eleven stages.

**Two real bugs found while verifying (both fixed):**
1. **The S3 i18n keys were in the wrong place in `data/text.json`.** The runtime looks them up under
   `doc["strings"]` (`Locale::loadFromFile`); `tools/gen_story_i18n.py` had written all 463 keys at the
   **top level**, so `Locale::tr()` returned the key itself and every fallback in the calling code hid it —
   the floor names silently fell back to the map names. Fixed by retargeting the generator, migrating the
   466 misplaced entries into `strings`, and **fixing validator V6**, which had been checking the same wrong
   place and therefore passing vacuously.
2. **The floor-change prompt had no keyboard path on the web.** Its Enter/Esc handling lived only in
   `src/game/main.cpp` (the desktop entry point) even though the button is labelled "(Enter)"; in the
   browser a keyboard-only player was stuck at the prompt until they clicked the canvas. Added the same two
   bindings to the web key handler (`emscripten_main.cpp`), before the in-game-menu branch.

Also fixed in `floor_table.cpp` during development: an underflow in the filename suffix guard
(`name.size() - 11` on an 8-character name) that made the table crash on its first load.

**Verified.** `floor_table_test: ALL PASS (990 checks)`; 20/20 test binaries; `validate_story: ALL PASS
(6834 checks)` with V6 now reading `strings`. In a real browser (WebGL2): floor table present (70 floors),
New Game → F01 with the HUD reading 「第 1 層 (1/70)」, `jsFloorLinks()` = 2 on F01 and on the boss floor,
**F15's exit tile → the floor prompt → confirm → F16 loaded (16/70)** with the new floor's links correct,
and F07 loading the hand-authored act-1 map as 「村莊外緣・封印結 (7/70)」 (the floor's own name from
text.json, i.e. the i18n fix working end to end).

### 2026-09-13 — S3.5 (c)(d)(e): the floor's story shows, each act looks different, events are in the world

Owner: "Now do the c,d,e" (the three pieces left after the minimal cut). All three are in, with two of the
three verified end-to-end in the browser and one verified only as far as the harness let me walk.

**(c) story on screen — verified.** The footer shows the floor's own `intro` line for the first 8 steps and
then rotates its `ambient` lines (`storyLineIndex` in `floor_table.h`, pure logic, checked in
`floor_table_test`), falling back to the grid's `story_note` for hand-authored stages. An act card (act
title + floor name) appears for 2.6 s on each act's first floor — seen live on F08 ("actCard=1" at +0.5 s).
This is the first time the 463 authored story keys are actually on screen: until now only `story_note` was.

**(e) per-act visuals — verified.** `floorThemeTint()` in `floor_table.h` maps act → RGB and the maze tint
passes it to the tile quads. `themeAct` reads 1 on F01, 2 on F08 and 4 on F22, and the screenshots differ
plainly (warm village, green forest shade, amber market). **`theme` is no longer dead data**: this is the
answer to "all stages looks same", though with one atlas it is a palette rather than new art (S8's job).

**(d) floor events — placed and rendered, stepping not yet observed.** The generator now writes each event
slot as a real marker (legend kind `event:<id>`), the engine draws them as solid story plates (visible in the
screenshots as the gold-framed tiles), and stepping on one shows its text and applies a kind-based effect
(shard → `run_.addShard`, trap → HP, cache/relic → HP+gold, whisper → a flag). The placement and the drawing
are confirmed; **the step effect was not caught in the harness** — my grid→game coordinate mapping kept
landing the player beside the marker rather than on it, and each attempt costs a full page load. The branch
mirrors the item branch that demonstrably works (items are picked up, `道具x4`, gold rising), so the risk is
low, but it is not yet *seen*, and this log says so rather than claiming it.

**Fixed while doing this:** the act card's key was built from the raw act id (`ch_01`), but the i18n keys are
`story.ch01.title` — the underscore made every lookup miss, so the card silently never appeared. `FloorInfo`
now carries `actKey` ("ch01") and both the card and the hub row use it; `floor_table_test` checks `actKey`
against the act id with the underscore stripped, so the two forms cannot drift apart again.

### 2026-09-13 — Per-floor grid sizes (owner request) + the mobile UI ask recorded

Owner: *"each level should have different grid size, for example first level is 10x10 second level is
10x12, third is 14x14, higher level should have more grids, only boss level not follow this rule"*.

Done: `SIZE_RAMP` in `tools/gen_floors.py` replaces the per-act table (which gave all seven floors of an
act the same size -- the reason every level looked alike). 44 column + 26 row increments are spread over
the 69 steps, so **every floor has its own size and the tower never shrinks**: F01 19x16, F02 20x16,
F03 20x17 … F69 62x41, F70 63x42 (same span as before). Boss floors are exempt by construction -- the ten
act-boss floors have no generated grid, they load their hand-authored map. The generator pads targets that
are off its 3c+1 x 3r+1 cell lattice (at most ~2 tiles of wall at the far edge), which is what makes an
arbitrary monotonic ramp legal; the loop now reads the per-floor size instead of the act row's.

NOT done yet -- the second half of the same request: **the web build is not mobile-friendly**. The owner
reports everything too small on a phone, especially the dialogue text and the battle phase, "text and
buttons all too small". Not started. Options are laid out in the report's Next Step for the owner to
choose; the leading candidate is a device-adaptive *design resolution* (render the fixed 1024x768 design
at a smaller logical size on small screens, which scales up every element -- text, buttons, the pad --
with one change) plus a dialogue/battle layout pass.

### 2026-09-13 — Mobile pass: A (design size) + D (portrait) shipped; B held back; C is the remaining piece

Owner: *"current version is not friendly for mobile, every thing is too small, especially talking dialogue
is hard to read and battle phase is too small, the text the buttons all too small"* — then chose A+B, and
after seeing B's result asked for C and D.

**A — shipped and verified.** The design resolution is runtime-settable (`Renderer::setDesignSize`, wired
to `WebGLRenderer`) and the browser picks **768x576 instead of 1024x768** on a small viewport, so every
element — HUD, maze, pad, dialogue, battle — occupies 1.33x more of the same physical screen. Verified in
a 844x390 phone viewport: canvas buffer 768x576, CSS box 514x386, dialogue readable where it was not
before. Desktop is untouched. The CSS fit reads the canvas' own buffer size, so design and CSS cannot
disagree.

**D — shipped.** The game is landscape 4:3, so a phone held upright only showed a sliver; a full-screen
「請把裝置轉為橫向 · Rotate your device」 prompt now appears in portrait and clears on rotation, injected
with the rest of the web UI shim (no engine layout involved).

**B — held back on purpose.** The extra 1.25x font scale (`toms::g_uiScale`, applied in drawText AND
measureText so they cannot drift) makes the dialogue's speaker line **collide with its first choice row**:
those screens lay rows out on a fixed pixel pitch. It is implemented but left at 1.0 rather than shipped
overlapping.

**C — the remaining piece, scoped.** Scale the dialogue/battle row pitch, panel heights and button rects by
`g_uiScale`, then turn B on, and give the battle buttons and dialogue rows >= 44 px touch targets (the pad's
`GP[]` rects are drawn and hit-tested from one table, so they scale in one place). Sites to touch: the
dialogue box's speaker line and row pitch, the battle prompt/log lines and `atk/def/superBtnRect_`, and
`GP[]`. Then rebuild and re-verify the dialogue and battle screens at phone size before deploying.

Deployed: stamp `97fda93-20260913202839` (A+D) — the live site is on it.

