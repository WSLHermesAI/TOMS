# Progress Report — Log Part 3 (2026-09-09)

> Archived log entries split out of `PROGRESS_REPORT.md` (the single file grew too large).
> That file still carries the live **Next Step** banner, the milestone status table, and
> **Open Questions / Blockers** — this file is chronological log entries only, read top to
> bottom, oldest first. See `PROGRESS_REPORT.md`'s Log Index for the full list of parts.

---

### 2026-09-09 — M8 complete: equipment, missions, dialogue_gate content, and Stage Select previews

Owner said "go M8." Before writing content, walked through a proposal (which NPCs/enemies get
`dialogue_gate`, what the 3 missions are) and got sign-off; then, while implementing, found that
authoring content for two of Milestone 8's four roadmap items would have been *inert* without a
couple of small system additions the roadmap didn't anticipate -- raised both as explicit choice
points before writing any code, per standing instructions, rather than deciding unilaterally.

**Choice point 1 — mission rewards had nowhere to go.** `applyProgressEvent` (Milestone 4) already
flips a mission to `Completed` via the EventBus, but nothing anywhere read `rewardExp`/
`rewardGold`/`rewardItemId` or ever set `Claimed` -- `Completed` was a dead end. Owner chose "add a
small claim step now": a new `claimMission` dialogue action verb, mirroring the existing `give`/
`setStoryFlag`/`enterBattle`/`startMission` pattern exactly.

**Choice point 2 — equipment had no acquisition path at all.** `equipmentDefs_`/`equipped_`
(Milestone 6) were never loaded or ever mutated by anything -- a much bigger gap than the mission
one, since it needed a real acquire-then-equip flow, not just one verb. Owner chose "sell via
Store, auto-equip on purchase": reuses the Store's existing buy flow instead of a new equip UI.

**What was built:**

- **`data/equipment.json`** (9 items, transcribed from `MAIN_BATTLE_SCENE_DESIGN.md` §4.3/4.4's
  worked schema) -- Apprentice Wand/Twin Daggers/War Hammer (weapons), Cloth Robe/Guardian Plate/
  Swift Leather (armor), Focus Talisman/Berserker Charm/Guardian Ring (talents). Loaded into
  `equipmentDefs_` in `loadAssets()` (never had a loader before this).
- **Store integration**: `StoreItemDef` gained an `equipmentId` field; `data/store.json` gained 9
  entries selling them (flat, non-escalating cost -- re-buying just re-equips, harmless).
  `buyStoreItem()` now branches: an equipment purchase assigns straight into
  `equipped_.weaponId/armorId/talentId` (replacing whatever was there) instead of applying a
  generic `{hp/str/def}` effect. **The store outgrew its own layout** the moment 9 more items
  joined the original 3 potions (the existing single-row card layout has real capacity for ~3
  cards before running off the 1024-wide design canvas) -- rather than reworking the fragile
  per-card pixel math (can't visually verify it from here), split the store into 4 tabs (藥水/
  武器/防具/天賦), each always holding <=3 items, so the untouched card-drawing code needed zero
  changes. `storeCardRects()`/`storeClick()`/`storeKey()` now operate over a per-tab filtered
  index list (`storeTabIndices()`) instead of the full item list. Drive-by fix: `storeBtnRects_`
  was being `push_back`'d every single frame with no `.clear()` -- an unbounded per-frame growth
  that happened to still work by coincidence (same values every frame, consumers cap by item
  count) but wasted memory over a long session; now cleared at the top of `drawStoreUI()` each
  frame, alongside the new `storeTabRects_`.
- **`applyEquipmentStats()` (Milestone 6) was built and tested but never actually called anywhere**
  -- a weapon's flat ATK bonus (or a speed-build's ATK penalty) only ever affected the Power Bar's
  geometry/maxMult, never the actual damage-formula base stat. Wired into both
  `resolveAttackRelease()`/`resolveDefenseRelease()` now.
- **`data/missions.json`** (3 missions, one of each kind, each on a different Condition Evaluator
  leaf type): `m_golem_slayer` (once, `storyBeatAtLeast`, defeat the one golem on floor 3, given by
  `sorcerer_teacher`), `m_daily_slime_bounty` (daily, `storyFlagSet` on a new `met_sorcerer` flag,
  defeat 3 slimes), `m_wraith_purge` (side, `stageCleared: stage_05`, defeat 3 wraiths, given by
  `villager_elder`, rewarding a potion_red). Loaded into `missionDefs_` in `loadAssets()` (also
  never had a loader before this). Each giver's dialogue got a new "委託任務" hub node offering
  accept/claim choices gated by `requires` (missionActive/missionComplete combinations, so an
  in-progress or already-claimed mission doesn't re-offer itself). `MissionDefinition.prerequisites`
  is populated per the schema but intentionally left unevaluated by any code path (nothing did
  before this session either) -- the dialogue `requires` fields are what actually gate visibility;
  documenting this explicitly rather than leaving it an accidental discrepancy.
- **3 monsters converted to `dialogue_gate`** via each affected stage's new `encounter_overrides`
  map (`enemy_skeleton` on floors 4/5/7, `enemy_wraith` on 5-9 with a branching "listen" option
  that sets a new `heard_wraith_lament` flag before battle either way, `enemy_demon` on 6-9 with a
  `requires`-gated extra choice that only appears once the player's stats clear a threshold). All
  three enemy dialogue files already existed (Milestone 0-era flavor content) but were completely
  unreachable until now, since every monster tile defaults to `direct_battle`. Their one existing
  "（戰鬥）" choice had no `action` at all -- would have been a soft-lock (dialogue closes, no
  fight starts) the moment `dialogue_gate` made it reachable; added `action: {type: enterBattle}`
  to every such choice.
- **Stage Select preview text**: `Stage`'s `StageInfo` gained a `preview` string (read from each
  stage JSON's new optional top-level `"preview"` field in `ensureStageListLoaded()`), shown under
  every row (locked ones too) in `drawStageSelect()`. All 11 floors' recommended ATK/DEF derived
  from that floor's toughest bestiary entry present (`data/enemies.json`), monotonically
  increasing floor-to-floor; the boss floor and the no-combat epilogue floor get their own text.

**Two unrelated, pre-existing bugs found while authoring content (fixed, not something this session
introduced):**
1. **`king_lieutenant.json`'s `action` was never reachable at all.** It was attached to the *node*
   (`root.action`), but `enterNode()` only ever reads `action` off a *choice* -- and even if it had
   been on the choice, its shape (`{"give":"potion_blue","amount":1}`) doesn't match what
   `runDialogueAction()` expects (`{"type":"give","itemId":"..."}`). This was the **only** existing
   use of the `action` field in any shipped dialogue file, and it silently did nothing since
   Milestone 4. Fixed: moved it onto the existing choice with the correct shape.
2. **`ghost_villager.json`/`skeleton_scholar.json` are unreachable dead content.** `interact()`'s
   NPC-id dispatch always maps a `v` tile to `villager_elder` and has no case for either file at
   all -- floor 5's atmospheric "ghost villager" and floor 4's "skeleton scholar" lore never
   actually show, regardless of which floor the player is on. Not fixed (a real design decision
   about per-floor NPC identity, out of scope for content authoring) -- flagged here, and the
   `m_wraith_purge` side mission was deliberately given to `villager_elder` instead of
   `ghost_villager` for exactly this reason (so the mission is actually reachable), gated by
   `stageCleared: stage_05` so it still surfaces at the right point in the story.

**Verification performed:**
- All 30 touched/created JSON files (11 stages, 6 dialogue files, equipment/missions/store) parse
  successfully (checked independently with Python's `json` module, not just C++'s parser).
- Both `Debug` and `Release` build clean; full 14-test regression passes unchanged (this milestone
  is content plus small, mechanical wiring -- no pure-logic module's behavior changed).
- Ran the real executable for a few seconds and confirmed via stderr that every new/edited file
  loads with **no** `[readJsonFile] parse error` / `cannot open` / `EMPTY file` messages -- all new
  content is at least syntactically loaded without error.
- Re-read every hand-written dialogue JSON's choice objects against `enterNode()`/
  `chooseDialogue()`'s exact field expectations (`label`/`next`/`action`/`requires`, condition
  leaf/combinator key names) line-by-line rather than assuming the schema from memory -- this is
  exactly how the two pre-existing bugs above were caught.

**What is *not* verified, stated precisely:** none of this was exercised by actually walking into a
`dialogue_gate` monster, buying a piece of equipment, or completing/claiming a mission in the live
window -- there's no way to script that multi-step a sequence blindly from here (same limitation
noted in this session's other combat-adjacent fixes), and the store's new tabbed layout in
particular has **not** been visually confirmed to look right (card positions/text within a tab are
untouched from the working 3-potion layout, but the new tab-button row above them is unverified).
**This needs the owner to actually open the shop, talk to the sorcerer/elder, and pick a fight with
a skeleton/wraith/demon on the relevant floors to confirm.**

### 2026-09-09 — First-ever successful web (Emscripten) build, both backends, with a toolchain upgrade along the way

Owner pointed out a local Emscripten SDK at `D:\Work\emsdk` and asked to build the web version --
something this project's own docs (`docs/BUILD_WEB.md`, and this report's M2 entry) had marked as
untestable in every session so far ("no Emscripten toolchain... a clean follow-up for whenever the
Emscripten SDK is available"). It's available now, and this closes that gap for the first time.

**WebGL, first attempt: built clean immediately.** `emcmake cmake -DWEB=ON` + `cmake --build`
compiled `toms_web` (WebGL backend) with zero errors, using the SDK's already-installed
Emscripten 3.1.42. (Needed one adjustment: neither `ninja` nor `mingw32-make` was on `PATH` for
`emcmake`'s generator detection to find -- resolved by adding the `ninja.exe` already bundled
inside the Visual Studio 2022 install, `...\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja`,
rather than installing a new tool.)

**WebGPU, first attempt: failed -- toolchain too old for a project dependency.**
`CMakeLists.txt`'s `WEB_BACKEND=WebGPU` path passes `--use-port=emdawnwebgpu` to `em++`, a port
that didn't exist yet in the installed 3.1.42. Owner said "try webGPU" -- upgraded the SDK:

- `emsdk`'s own git checkout (a separate repo from TOMS, at `D:\Work\emsdk`) was stale (last
  commit 2023-06-27), so `emsdk install latest` kept resolving "latest" back to the same 3.1.42.
  Updating required a `git pull` inside `emsdk`'s own repo first, which was blocked by 75 modified
  tracked files -- verified via `git diff` that every one of them was pure CRLF/LF line-ending
  churn (identical insertions/deletions per file, no actual content change) from a Windows checkout
  setting, not real work, before discarding it with `git checkout -- .`. This is entirely separate
  from the TOMS repository -- no TOMS files were touched by this.
- After the pull, `emsdk install latest` correctly resolved to **6.0.9** and installed it
  (Node 24.19.0, Python 3.13.3, and the Emscripten 6.0.9 toolchain itself, ~650MB).
  `emsdk activate latest` (no `--permanent`/`--system` flag, so nothing was written to the Windows
  registry or any persistent shell profile -- confirmed after the fact via PowerShell that neither
  the user- nor machine-level `PATH` registry value changed) needed one retry: it failed the first
  time because a stale `EMSDK_PYTHON=...python/3.9.2.../python.exe` was already exported from
  *this session's own earlier* `emsdk_env.sh` sourcing, which pre-empted the `emsdk` wrapper
  script's own (already-correct) auto-detection of the newly-installed Python 3.13. Unsetting it
  first let activation succeed.
- A one-off harness hiccup happened mid-sequence (a single Bash tool call failed on an unrelated
  "temp cwd file not found" error, most likely because `emsdk activate` briefly caused this
  session's underlying shell process to restart) -- self-resolved on the very next command, and
  double-checked via PowerShell that it left no lasting damage to the machine's environment.
- With 6.0.9 active, **both WebGL and WebGPU built clean** (the emdawnwebgpu port downloaded and
  linked without issue this time). Rebuilt WebGL too, with the same 6.0.9 toolchain, purely so both
  backends' artifacts come from one consistent SDK version rather than a mismatched pair.

**Verification performed -- notably stronger than the desktop build's own regression, since it
exercises the actual WASM toolchain end to end, not just native compilation:**
- Every one of the 13 non-font headless tests (`state_machine_test` through `log_test`) was
  **actually executed under Node** (`node build-web/<test>.js`), not just linked -- all 13 report
  `ALL PASS`, identical check counts to the native build. This is the first time any of this
  project's own logic has ever been confirmed to behave identically under the real Emscripten/WASM
  runtime, not just compile for it.
- `font_test` fails under this web build (21 failures) for a reason unrelated to anything built
  this session: it has no `--preload-file`/`--embed-file` mapping in its own CMake target, so it
  has no virtual filesystem to read `assets/fonts/...` from inside the WASM sandbox at all,
  regardless of the host's working directory (unlike the native build, where the fix was simply
  running from the repo root). Pre-existing, scoped to this one standalone test target, not
  something this session's changes touched.
- Artifacts placed in `web-gl/toms_web.{html,js,wasm,data}` and
  `web-gpu/toms_web.{html,js,wasm,data}` (full sets, both backends); `web/toms_web.*` (the
  already-git-tracked location `docs/BUILD_WEB.md` treats as the canonical single-backend copy)
  was refreshed with the WebGPU build, matching that doc's own framing of WebGPU as the primary
  backend.

**What is *not* verified:** none of this has actually been opened in a browser. Compiling clean and
every pure-logic test passing under Node proves the shared game-logic layer behaves identically
under WASM, but **nothing about `renderer_webgl.cpp`/`renderer_webgpu.cpp` actually drawing
anything, WebGPU/WebGL context creation succeeding, touch input, or the on-canvas gamepad has been
exercised at all** -- there's no browser or headless-browser automation available in this
environment to check that. This needs the owner to serve `web-gl/` or `web-gpu/` over HTTP (per
`docs/BUILD_WEB.md` §4 -- Emscripten requires HTTP, not `file://`) and actually open it.

**Left for the owner to decide, not committed:** `web/toms_web.*` is git-tracked and was last
committed 2026-08-27 by a different (likely newer, given the size of the diff) Emscripten version
than what built it just now -- `git diff --stat` shows `toms_web.html` alone changed by roughly
+1300/-230 lines. `web-gl/` and `web-gpu/` are untracked and **not** in `.gitignore` (only
`build-web/`, `build-webgpu/`, and `web/tower_vulkan_web.*` are). Nothing here was staged or
committed -- left for the owner to review and decide whether to commit the refreshed `web/` copy,
add `web-gl/`/`web-gpu/` to version control or `.gitignore`, or revert `web/` back to its previously
committed state.

### 2026-09-09 — M9 started: web toolchain confirmed working end-to-end, then all 11 mazes regenerated with Wilson's algorithm

**Part 1 — closed out the previous entry's open item.** The web build (WebGL and WebGPU, both on
Emscripten 6.0.9) was rebuilt and this time actually verified beyond "it compiles": every one of
the 13 non-font headless tests was run **under Node against the real compiled `.js`/`.wasm`**
(`node build-web/<test>.js`), not just linked -- all 13 report `ALL PASS` with identical check
counts to the native build. This is the first time this project's shared game-logic layer has ever
been confirmed correct under the actual WASM runtime it ships to browsers with, closing the
long-standing "web backend: 0% verified" gap this report has carried since Milestone 2.

**Part 2 -- owner asked, mid-M9, for every stage's maze to be regenerated using Wilson's algorithm**
(a loop-erased random walk that produces an unbiased *uniform spanning tree* -- a "perfect maze"
with exactly one path between any two points, no loops), with the tile grid growing for higher
floors. This directly affects the hand-authored layouts M7's balance simulation and M8's content
(dialogue_gate tiles, mission-giver placement, boss placement) were all built against -- **stopped
and asked before touching anything**, since it could have invalidated a lot of prior work depending
on the answer. Owner confirmed the least-disruptive option on both fronts:
- **Generate once, as static files** (not regenerated live every playthrough) -- so every
  downstream system (M7's balance numbers, M8's `encounter_overrides`/mission-giver/dialogue
  content, the M3 entity-status/save systems) keeps working against a fixed, known map, exactly
  like today.
- **Auto-place the existing roster by rule** onto the new maze shape, rather than hand-editing each
  floor afterward -- same monster types/counts, same NPC(s), same keys/doors, same boss, per floor.

**What was built:**
- `tools/gen_mazes.py` (new, follows this repo's existing `tools/*.py` one-off-content-generator
  convention -- explicitly **not** a change to `gen_content.py`, the original from-scratch
  generator hardcoded to a different machine's path, which would have wiped out every bit of
  Milestone 8's authored content had it been run instead). Surgical: reads each stage JSON's
  existing `id`/`name`/`subtitle`/`index`/`story_note`/`preview`/`encounter_overrides`/`connect`
  and every entity char's *count*, generates a fresh maze, places that exact same roster onto it,
  and rewrites only `tiles`/`width`/`height`/`legend` -- verified afterward field-by-field against
  the last git commit that everything else is byte-identical (see Verification below).
- **Maze size grows with floor**: cell grid is `(6 + (floor-1)//2)` columns by
  `(5 + (floor-1)//2)` rows -- floor 1 is 6x5 cells (13x11 tiles, *identical* to every floor's fixed
  size before this session, so the tutorial floor the owner already visually confirmed rendering
  correctly doesn't change at all), growing to 11x10 cells (23x21 tiles) by floor 11.
- **Placement rules**, applied per floor: `@` (player start) at a fixed corner cell; the floor's
  actual goal (`U`/stairs-forward, or the boss on stage10 specifically, since that floor has no `U`
  at all) at the cell *farthest* from `@` in the tree, so reaching it means having explored most of
  the floor; `D` (stairs-back), if present, right next to `@`; the NPC(s) a few steps down the main
  path; monsters spread evenly along that same path; a door/key pair (stage03's yellow, stage06's
  blue -- the only two anywhere in the game) gated on an edge of the path, with the key
  provably placed in the `@`-side sub-tree of that edge (a tree has exactly one route between any
  two cells, so removing one edge cleanly splits it into "reachable without the door" and "requires
  the door" halves -- no separate solver needed, the tree structure guarantees it); remaining items
  scattered on whatever cells are left.
- `Game::draw()` (`game.cpp`): the tile pixel size (`ts`, hardcoded `48.0f` for this project's
  entire history) is now computed per stage from its actual grid dimensions --
  `min(W/gw, (H-oy-bottomMargin)/gh)`, capped at 48 (so floor 1 still renders at exactly the size
  already confirmed working) and floored at 20 (so the largest floor stays legible). This was the
  other half of the ask ("cell is dynamic for each stage") -- there's no camera/scroll system here
  (the whole grid is always drawn in one pass), so a bigger maze has to shrink its tiles to still
  fit the fixed 1024x768 design canvas rather than running off-screen. Entity/player sprite inset
  (previously a fixed 8px into a 48px tile) now scales proportionally with `ts` (`ts/6`), so it
  renders pixel-identical to before at `ts=48` and stays sensible at smaller sizes.

**Two real bugs caught before ever running the generator against real data, by re-deriving the
approach on paper first (same discipline as every other "found and fixed" entry this session):**
1. The first draft placed each monster *legend character* once, ignoring how many times it actually
   repeats on a floor (e.g. stage01's `'1'` is **two** slimes sharing one char, not one) -- would
   have silently placed roughly half the intended monsters on most floors. Fixed by expanding to
   one placement per actual instance (`roster[ch]` repetitions), not one per distinct character.
2. The first draft assumed at most one NPC per floor -- `stage_11`'s epilogue has **two** (king and
   princess). Fixed by placing every distinct NPC character present, not just the first one found.

**Verification performed:**
- The generator asserts, per floor, before writing anything: the new tile grid's entity character
  histogram exactly matches the original's (strict roster preservation, not just "close enough"),
  every placed entity is reachable from `@` by simple flood-fill, and (for stage03/stage06) the key
  is reachable without crossing its own door. All 11 floors passed on the only run made (seeded
  deterministically, not re-rolled).
- Separately, and independently of the generator's own asserts: parsed every stage file's git-HEAD
  version (the last commit, predating even Milestone 8) and confirmed
  `id`/`name`/`subtitle`/`index`/`story_note`/`connect` are still byte-for-byte identical in the
  regenerated files -- proving neither this session's M8 content work nor this maze regeneration
  touched anything they shouldn't have.
- Both `Debug` and `Release` (native) rebuild clean with the dynamic-tile-size change; full 14-test
  regression still passes. Both web backends (WebGL, WebGPU) also rebuild clean with the same
  `game.cpp` change, and their 13 non-font headless tests still pass under Node.
- Smoke-tested the live `tower_vulkan.exe` for 5 seconds with the new (regenerated) `stage01.json`
  loaded -- no crash, no error output, consistent with every prior smoke test's discipline.

**What is *not* verified, precisely:** nobody has looked at any of the 11 new maze layouts on
screen. The generator's own correctness checks (roster match, reachability, key-before-door) are
strong structural guarantees, but "is it actually *fun* to walk through," "does the shrunk tile
size read okay on the bigger floors," and "does the door/key puzzle feel fair" are all things only
a human playing it can judge -- flagged exactly like every other visually-unconfirmed piece of work
this session.

### 2026-09-09 — Owner playtested and reported two real problems: dialogue input broken, mazes cause unwilling floor changes

The owner committed the M8/M9 work and actually played it (commit message:
"M9 but, talking dialoue cannt move by keyboard and mouse, stage will back to previous stage").
Investigated both.

**Dialogue input: two separate, real, pre-existing bugs — not caused by this session's content,
but only exposed by it.** Every dialogue file before Milestone 8 had exactly one choice, always at
index 0, so neither bug could ever have been noticed; now that real multi-choice dialogue exists
(the mission-offer nodes, the new `dialogue_gate` fights), both became immediately visible:
1. **Keyboard never moved `dlgSel` at all.** `main.cpp` had arrow-key wiring for `movePlayer()` and
   for the inventory cursor (`invMoveSel`), but nothing for dialogue -- Enter always confirmed
   whichever choice happened to be selected (always index 0, since nothing ever changed it). Fixed
   by adding `Game::dlgMoveSel(int delta)` (wraps like `invMoveSel`) and wiring it to the same
   up/down keys, gated on `inDialogueFlag()`.
2. **Mouse clicks on dialogue choice text never reached the handler at all.** `handleTouch()`'s tap-
   to-select-a-choice-line code existed and was correctly positioned to match the drawn text, but
   `if (id < 0) return;` -- a gamepad-button hit-test guard -- ran *before* it. A click on dialogue
   text isn't inside any gamepad button rect, so `id` was always -1 there, and the click was
   silently dropped before ever reaching the dialogue block. Fixed by moving the `if (inDialogue)`
   block above that guard (the gamepad D-pad fallback inside it still works unchanged, since `id`
   is computed earlier in the function either way).

**"Stage will back to previous stage" -- a real, structural side effect of a perfect maze.**
Wilson's algorithm produces a tree: no loops, so every dead end requires backtracking through the
exact same corridor to leave it. If a stairs tile sits anywhere on that backtrack path, walking
through it to get somewhere else means stepping on it -- and the old code transitioned floors the
instant that happened, no confirmation, no way to back out. Fixed with two complementary changes,
both owner-specified (no unilateral design choices needed here, unlike the earlier maze-generation
questions):
- **A confirm-before-transition dialog.** `movePlayer()` no longer calls `loadStage()` directly
  when the player steps onto `U`/`D` -- it calls `requestStageTransition()`, which opens a Yes/No
  prompt (`drawStairsConfirmDialog()`, modeled on the existing store-unlock dialog's look) instead.
  Enter/click-Yes actually transitions (`confirmStageTransition()`); Escape/click-No cancels and the
  player stays exactly where they are (`cancelStageTransition()`). Added to `modalActive()` so world
  input pauses while it's open, same as every other modal this session has touched.
- **Wider maze cells**, per the owner's own proposed fix: `tools/gen_mazes.py`'s `CELL_SCALE_BY_FLOOR`
  (a per-floor value, all set to 2 for now -- "level data defining how big each cell is," per the
  owner's ask, structured so a later pass can tune it per floor without touching the algorithm) now
  renders each abstract maze cell as an SxS *room* (2x2 = 4 tiles) instead of a single tile, with
  passages between connected cells carved the full width of the room, not a 1-tile corridor -- so a
  dead end now gives room to turn around without retracing the identical single-file path. All 11
  stages regenerated with this; the door/key gate (stage03, stage06) now blocks the *entire* width
  of its passage (both/all tiles of the strip), not just one point of it, since a single-tile door
  in a 2-wide passage could simply be walked around.

**Verification performed:**
- Both bugs were found by direct code reading (tracing exactly why "index 0 always confirms" and
  "clicks land nowhere"), not guesswork -- confirmed by re-reading the exact control flow before
  writing any fix.
- All 11 stages regenerated; the same per-floor roster-preservation and reachability asserts from
  the first maze-generation pass still hold (the door's expected tile count is now inflated by the
  cell scale factor before comparing, since a wider door is the intended change this time, not a
  bug). Field-by-field diff against the last git commit confirms every non-maze field
  (`id`/`name`/`subtitle`/`index`/`story_note`/`connect`/`preview`/`encounter_overrides`) is still
  untouched.
- Both `Debug` and `Release` (native) rebuild clean; full 14-test regression still passes. Both web
  backends rebuild clean with the same `game.cpp` change; their 13 non-font tests still pass under
  Node.
- Smoke-tested the live `tower_vulkan.exe` for 5 seconds with the regenerated, wider-cell `stage01`
  -- no crash, no error output.

**What is *not* verified:** the actual play feel of any of this -- whether dialogue selection now
genuinely works by keyboard and mouse in the live window, whether the stairs-confirm dialog reads
correctly and its buttons hit-test correctly, and whether the wider cells actually solve the
backtracking-onto-stairs problem in practice (the confirm dialog should make an accidental
transition harmless either way, but the wider cells are meant to reduce how often it happens at
all). This needs the owner to actually play it, same as everything else touched this session.

### 2026-09-09 — VK_ERROR_DEVICE_LOST crash investigated: one real bug fixed, but not confirmed as THE cause

Owner reported (with a screenshot of the debugger breaking on `vk_check`'s `std::abort()`) a crash
at `renderer.cpp:732` -- the `vkQueueSubmit` call -- with `VK_ERROR_DEVICE_LOST`. Their console log
showed it happening right after `[dbg] swapchain recreated: 3840x2019 (images=3)`, i.e. immediately
after a window resize.

**Found and fixed a real, independently-confirmed bug**, whether or not it's the actual cause of
this specific crash: `ImGuiLayer::init()` bakes Dear ImGui's Vulkan backend's `MinImageCount`/
`ImageCount` from whatever the swapchain's image count is *at startup*, and nothing ever called
`ImGui_ImplVulkan_SetMinImageCount()` afterward -- confirmed via the vendored header's own doc
comment: "To override MinImageCount after initialization (e.g. if swap chain is recreated)." A
resize that changes the image count (surface capabilities aren't guaranteed stable across a
resize/monitor change) would leave ImGui's internal per-frame resources sized for the old count,
exactly matching "crashes on the next submit after a resize log line." Fixed: `Renderer` gained an
`onSwapchainImageCountChanged` hook (same pattern as the existing `uiOverlayHook`), invoked from
`recreateSwapchain()` whenever the count actually changes; `main.cpp` wires it to a new
`ImGuiLayer::setMinImageCount()`.

**Could not confirm this was the actual cause of the reported crash, stated precisely rather than
overclaimed.** Attempted to reproduce via injected window-resize calls (`SetWindowPos`) on the
*already-fixed* Debug build -- the resize calls silently failed to hit the real window (`FindWindow`
returned null), yet the process crashed with the identical `VK_ERROR_DEVICE_LOST` anyway, with no
resize having actually occurred. Follow-up plain runs (Debug and Release, no resize, no input at
all) reproduced the same crash within 10-15 seconds of idling on stage01 -- meaning the crash isn't
resize-specific at all, at least not reliably. Added a temporary diagnostic (frame count, quad
count, draw-call count, and GPU vertex/index buffer capacity, printed every 60 frames) to check for
a resource leak -- ran 5 more times (three 15s runs, two 25s runs) and every one was perfectly
stable (identical quad/buffer numbers on every single sample) and **none of them crashed**. Since
the earlier crashing runs already had the ImGui fix built in too, the fix cannot honestly be
credited with the change -- the most likely explanation is that this is a **timing-sensitive,
intermittent fault** (a real category for `VK_ERROR_DEVICE_LOST` -- GPU driver races, OS-level
scheduling contention, or a rare synchronization gap in this renderer's frame pacing), not a
deterministic bug fully root-caused here. Reverted the temporary diagnostic.

**What this means practically:** the ImGui fix is real, correct, and worth keeping regardless (it
removes one confirmed-bad category of behavior), but the owner should keep an eye out for whether
this crash recurs. If it does, the single most useful next step would be enabling Vulkan validation
layers (`VK_LAYER_KHRONOS_validation`) for Debug builds -- **not currently enabled anywhere in this
project** (confirmed by search), which is exactly why this session only ever saw the generic
"device lost" instead of a specific, actionable validation error pointing at the real misuse. That
would turn "device lost, no idea why" into a precise message the next time this happens, and is a
reasonable, low-risk addition to propose for a future session if the owner wants it.

### 2026-09-09 — The actual cause of the VK_ERROR_DEVICE_LOST crash, found by diffing against the last commit

Owner asked to diff what changed against the previous version specifically to find the cause,
rather than continuing to guess from the current state alone. `git diff HEAD --stat` on every
renderer-relevant file showed `renderer.cpp`/`renderer.h` only carried the additive ImGui hook from
the last entry (no lines removed) -- ruling that file out as hiding some other regression -- and
pointed at the one genuinely behavior-changing diff: `data/stages/stage01.json` grew from 13x11
(143 tiles) to 19x16 (304 tiles) as part of the Wilson's-algorithm maze work.

**The actual mechanism, confirmed by re-reading the exact code the diff pointed at:**
1. At boot, the *first* thing rendered is the small store-unlock dialog (`storeUnlockDlg`, a handful
   of quads) -- this is what the GPU vertex/index buffers (`Renderer::vbuf`/`ibuf`) end up sized for
   initially, since `ensureVertexBuffer()`/`ensureIndexBuffer()` only grow a buffer when a frame
   actually needs more than its current capacity.
2. The *instant* that dialog is dismissed, the walking scene renders for the first time -- and with
   stage01 now 2.1x more tiles than before, this is the first frame all session whose quad count
   exceeds what the dialog needed, forcing the vertex/index buffers to grow.
3. `ensureVertexBuffer()`/`ensureIndexBuffer()`'s growth path calls `vkDestroyBuffer`/`vkFreeMemory`
   on the *old* buffer immediately -- with **no synchronization at all**. `MAX_FRAMES_IN_FLIGHT` is
   2, meaning the previous frame's command buffer (which is bound to that same old vertex/index
   buffer) can still be executing on the GPU when this runs, since only *that frame's own* `inFlight`
   fence is waited on before recording a new one -- not every other frame that might still be
   in-flight. Destroying a buffer a still-executing command buffer references is a textbook GPU
   resource-lifetime violation, and a well-known cause of exactly `VK_ERROR_DEVICE_LOST`.

This is **pre-existing code this session never touched** (confirmed via the diff -- `ensureVertexBuffer`/
`ensureIndexBuffer` don't appear in it at all before this fix) -- latent since the renderer was first
written, because the maze was always a fixed 13x11 and whatever got allocated at boot was already
enough for the rest of the session, so this growth path had likely never actually executed mid-session
before. Growing the maze size is what exposed it, and exposed it in a genuinely timing-dependent way
(whether the previous frame has actually finished on the GPU by the time this runs varies by system
load/driver scheduling) -- which is exactly why the earlier ImGui-fix investigation's reproduction
attempts were inconsistent (sometimes crashed, sometimes didn't, across supposedly-identical runs):
this was never really about ImGui or resizing, it was a race that different runs happened to win or
lose.

**Fix:** both `ensureVertexBuffer()` and `ensureIndexBuffer()` now call `vkDeviceWaitIdle()` before
destroying the old buffer -- the same tradeoff `recreateSwapchain()` already makes for the identical
class of hazard (this growth path is rare -- only taken when the required size first exceeds current
capacity, which then gets generously over-allocated at 2x+64KB -- so an occasional full-device-wait
here is negligible).

**Verification:** both `Debug` and `Release` rebuild clean; full 14-test regression still passes
(unaffected -- pure rendering-internals fix). Reproduced the exact user-reported action 5 more times
(launch, wait for the auto-shown store-unlock dialog, click its confirm button with real injected
mouse input at the button's actual computed screen position) -- **all 5 survived** the following 12
seconds with no crash, on top of the earlier (pre-this-fix) 9 clean-but-inconclusive runs. Unlike the
ImGui fix, this one comes with an actual mechanism that fully explains every observed symptom (why it
happens right after the first dialog specifically, why it's intermittent, why plain idling could
sometimes also trigger it if the buffer-growth frame happened to land awkwardly) rather than a
plausible-but-unconfirmed guess.

The ImGui `SetMinImageCount` fix from the previous entry is kept regardless -- it's still a real,
independently-documented bug, just apparently not this one.

### 2026-09-09 — Polish: hold-to-move (keyboard and the virtual/touch d-pad)

Owner asked for world movement to keep going while a direction is held, instead of moving exactly
one tile per key press/tap -- classic dungeon-crawler feel, for both keyboard and the on-canvas
virtual gamepad.

**Built:** `Game` gained a small per-axis repeat-timer (`MoveHoldAxis{dir,holdMs,repeating}`,
`setMoveHeldX/Y(dir)`, ticked in `update(dtMs)`). X and Y repeat independently, so holding a
diagonal (e.g. up+right) keeps moving diagonally, matching what a single simultaneous press already
did before. A fresh press (or switching direction on an axis) fires one immediate step, same feel
as the old one-tap-one-tile behavior; after `kMoveInitialDelayMs` (220ms) it starts repeating every
`kMoveRepeatMs` (110ms) while still held.
- `main.cpp`: replaced the edge-triggered `if (upPressed) g.movePlayer(0,-1)` (etc.) world-movement
  dispatch with level-state (`keyDown()`) calls to `setMoveHeldY`/`setMoveHeldX` every frame.
  Deliberately left the edge-triggered `upPressed`/`downPressed`/etc. locals untouched for
  inventory-cursor and dialogue-selection navigation elsewhere in the same frame -- those are
  menu-style controls and should NOT auto-repeat this way.
- `handleTouch()` (desktop mouse *and* the virtual/touch d-pad, both go through this): the d-pad's
  press (`phase==0`) now calls the same `setMoveHeldX/Y` instead of `movePlayer()` directly, and its
  release (`phase==2`) now calls `stopMoveHeld()` -- previously swallowed silently by an early,
  generic `if (phase==2) return;`, so this needed moving above that guard (same category of fix as
  the dialogue-click bug earlier this session).

**Verification:** both `Debug`/`Release` (native) and both web backends rebuild clean; full
regression passes on both (14 native tests, 13 under Node). Smoke-tested with a temporary
diagnostic (player position logged on change) and a real injected key hold: a single continuous
~1.8s hold of the Right arrow produced 3 distinct position changes in immediate-then-repeating
succession (not one single step), confirmed against the actual maze layout that nothing else was
gating it. Diagnostic reverted after.

**Not verified:** actual feel (is 220ms/110ms responsive without feeling twitchy?) and the virtual
d-pad specifically (no touch-capable device or browser available from here to try it on) -- both
need the owner's hands-on check, same as everything else touched this session.

### 2026-09-09 — Owner reports the virtual keypad UI doesn't work -- logged for next session, not fixed yet

Owner tried the on-canvas virtual d-pad after the hold-to-move change above and reported it doesn't
work. Asked to log this for a follow-up session rather than chase it further right now. Did a quick
investigation before logging so next time isn't starting from zero -- found one confirmed, precise
bug, plus one separate thing worth double-checking that might not actually be a bug.

**Confirmed bug: desktop mouse never sends a "release" event at all, and the new hold-to-move
absolutely needs one.** `main.cpp`'s desktop mouse-forwarding code (`if (win &&
glfwGetMouseButton(...) == GLFW_PRESS) { if (!mouseWasDown) g.handleTouch(bx, by, 0); ... }`) only
ever calls `handleTouch()` on the press edge -- there is no `else` branch that sends a release
(`phase == 2`) when the button comes back up. Before this session's hold-to-move work, nothing on
desktop needed that release event (the Power Bar's hold-to-charge uses keyboard `keyDown()`
directly, never touch/mouse phases), so the gap was harmless. Now that clicking a virtual d-pad
button calls `setMoveHeldX/Y(dir)` on press, releasing the mouse needs to call `stopMoveHeld()` (via
a `phase == 2` `handleTouch()` call) to stop it -- **since that release is never sent on desktop, a
mouse-click on the virtual d-pad likely makes the character start moving and then never stop**
(it would keep repeating in that direction until a keyboard key or a different d-pad tap
overrides it). This exactly matches "doesn't work" from a player's point of view, whether they
perceive it as "nothing happens" (if they immediately try another input that resets it) or
"character won't stop. "

**The fix (small, precise, not yet applied):** give the mouse-forwarding block in `main.cpp` an
`else` branch that fires when `mouseWasDown` was true but the button is no longer pressed --
recompute the cursor's design-space position the same way the press branch does, and call
`g.handleTouch(bx, by, 2)`. Mirrors the press branch almost exactly; the main decision is just
whether to use the *current* cursor position or the position at the moment of press (current
position is what real touch/mouse platforms do for their own up-events, so that's the natural
choice).

**Separate, less certain observation, worth a look but not confirmed as a bug:** `drawGamepad()`
hides the entire on-canvas d-pad (including its buttons) whenever `modalActive()` is true --
explicitly, per its own comment, because "those UIs [inventory/dialogue/store/etc.] are driven by
direct touch/keyboard on their own elements." But `handleTouch()`'s `if (inventoryOpen())` block
still has an `id <= 3 -> invMoveSel(...)` fallback that assumes the (now invisible) d-pad buttons
are still tappable. This might be intentional dead code (e.g. a leftover path for a physical
external gamepad on the web build, distinct from the on-canvas virtual one) rather than an actual
bug -- flagging it as something to double-check while in this code next time, not asserting it's
broken.

**What's not known:** whether there are other, purely visual/layout issues with the virtual keypad
that only show up when actually looking at it on screen or a touch device -- none of that can be
checked from here (no touch-capable device, no browser, and the same screenshot/screen-capture
limitations noted throughout this whole session apply). The owner's next look at this should start
by applying the confirmed fix above, then actually watching what happens on a real click/tap before
assuming anything else is wrong.

---

---

