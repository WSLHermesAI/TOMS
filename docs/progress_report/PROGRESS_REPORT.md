# Progress Report — Implementation Roadmap Tracking

> **Purpose:** the living status board for `docs/architecture/IMPLEMENTATION_ROADMAP.md`. Read the **Next Step**
> line first when resuming work — it always says exactly what to do next. Below that is a status
> table for every milestone, then a **Log** index (the dated entries themselves live in
> `1_PROGRESS_REPORT.md`, `2_PROGRESS_REPORT.md`, ... — split out, oldest first, once this file
> grew too large to work with as one piece), then an **Open Questions / Blockers** section listing
> anything found wrong or any choice point that needs a decision from the project owner before
> work continues (per standing instruction: stop and ask rather than deciding unilaterally on
> anything surprising or irreversible).

---

## ▶ Next Step

**Next action: M9 continued — `ch_05`–`ch_10`**, one chapter at a time. `ch_04` (first slice, owner-confirmed
scope) is done and verified live (see below); the same research-then-scope-then-build pattern applies to
whichever chapter comes next — check what the chapter's floors/stages already have from S2's generator, what
narrative text is still placeholder, and what (if anything) its side story needs from the dialogue/condition
layer before assuming it's "just content." `ch_05` is the natural next pick (unblocks the village hub's next
location, `hub.crypt_shrine`) but hasn't been scoped yet — check in with the owner before starting.

**Just closed:**
- **Stair alignment** (log part 7, 2026-09-16, owner request independent of the M9 content track):
  every one of the 70 floors' stairs now physically line up with its neighbor (floor N's
  stairs_up tile is the exact same (x, y) as floor N+1's stairs_down), and `Game::loadStage()`
  gained a `StageArrival` param so climbing/descending actually lands the player there instead of
  a fixed default spawn. New `tools/gen_stairs.py` regenerates all 70 floors' maze shape in
  strict chain order while preserving every floor's exact roster (same monsters/items/NPCs/
  events/door+key) and the existing size ramp — confirmed ch_04's own content (F26's NPC, its
  event tiles) survived intact. Two real generator bugs found via the test suite (a "big"
  monster's footprint spilling past its cell; several big monsters together sealing a route no
  single one would alone) fixed before shipping. Documented per the owner's own ask:
  `docs/story/STAIR_ALIGNMENT.md` + `STORY_DATA_SCHEMA.md` §5.3. Verified live: a real step onto
  a real stairs tile, a real Enter-key confirm, landing exactly on the matching tile both up and
  down, plus a Fresh debug jump to a floor with no `'@'` landing on a real stair instead of a
  guess.
- **M9, first slice: `ch_04` content (F22-F28)** (log part 7, 2026-09-16): `data/story/chapters/ch_04.json`,
  real narrative text for F22-F28 and the 23 event ids their maze data already referenced, and `ss_04`
  ("藏書閣的一頁") made fully real — `skeleton_scholar`'s dialogue actually placed on a stage for the first
  time (it existed but was unreachable dead content before this), extended with a return/study branch that
  grants a new skill (`s_yinqi_combo`) and completes the side story. Found and closed two structural gaps
  along the way: `data/events/pool_actNN.json` files are never loaded at runtime at all (events are resolved
  by substring-matching the id string directly), and `RunStoryState`'s own flags (`event_<id>`,
  `flag_page_returned`, etc.) had no condition-evaluator leaf able to read them back — added `runFlagSet`
  alongside the existing (differently-scoped) `storyFlagSet`. Also added `unlockSkill`/`setSideStoryState`
  dialogue verbs so a choice can grant a skill and resolve a side story in one action. Verified live: the
  gated 3rd dialogue choice genuinely appears only after the real trigger flag is set, the return/study
  choice flips the counter/flag/skill/side-story state together, and re-opening afterward confirms it can't
  be claimed twice.
- **M8, first slice: 輪迴 (rebirth)** (log part 7, 2026-09-16): `data/story/cycles.json` +
  `cycle_system.h/.cpp` (schema-faithful content, explicit C++ transformation — not a generic
  interpreter, matching `ending_system.cpp`'s own precedent), `Game::rebirth()` (skills kept at
  half effect for tier≥1 nodes, skill points + Super gauge cap halved-and-floored, memory shards
  kept, forge/actives knowledge needed zero new code since they're already meta-scoped, everything
  else reset, floor back to F01, `cycleIndex` +1), and a real two-choice ending screen
  (`rebirthOffered()` gates a second button, not just a flag). Fixed `CombatState::kSuperThreshold`
  being a compile-time constant (can't be "carried at half power") — it's now
  `RunStoryState::superMax()`, a real per-run save-persisted value, across all 6 of its old call
  sites. `e_13` (already reachable via M7) doubles as a fully real, live-testable path through the
  entire rebirth flow. Verified live via the real keyboard AND real simulated clicks on both
  buttons — cycleIndex advanced, skills carried at the correct halved value, stats/gold/equipment
  correctly reset, landed on a fully playable F01.
- **M7, first slice: the 15-ending resolver** (log part 7, 2026-09-16): `data/story/endings.json`
  (all 15, translated from `STORY_BIBLE.md`'s table into the existing condition DSL), a pure/tested
  `ending_system.h/.cpp` (`judgeEnding()` for F70-completion, `checkNamedEndings()` for the
  wipe/choice-triggered rows — deliberately separate per the schema's own §7.3), and a real
  full-screen ending scene wired into `finishCombatLose()`. Found and fixed a real, pre-existing
  bug while researching: `RunStoryState::noteDeath()` had existed since S3 but was never actually
  called, so `e_13` (12 non-boss deaths) could never fire — put the resulting behavior change
  (death now has a real permanent consequence for the first time) to the owner, who chose to wire
  it live. Closed two condition-DSL gaps (`always`, `memoryShards`/`deathsNonBoss` leaves) and one
  more structural one (`itemHeld` never recognized equipped gear, only inventory items) found
  while authoring the content. Verified live: 11 forced deaths → ordinary respawn each time, the
  12th → a real `e_13` ending screen, dismissed via both a real Enter keypress and a real
  simulated click, landing back on the title both times.
- **Equipment actives, first slice (M6)** (log part 7, 2026-09-16): `data/actives.json` (1 active,
  `a_twin_flash`, attached to the not-yet-reachable `twin_daggers`), `EquipmentDefinition.actives` +
  `equippedActives()`, a pure/tested `active_system.h/.cpp`, and a real 4th battle button
  (`Game::battleTapActive()`) wired into touch AND keyboard (`H`, new parity with F/G) on both
  platforms. The manually-activated shape (vs. a passive auto-trigger) was the owner's explicit
  choice, based on the status-effect system's own "use active"/cooldown wording. Along the way,
  confirmed (not assumed) that S4's skill-reset logic has no live bug: `RunStoryState::reset()` is
  only called from `newGame()`, never from the not-yet-built cycle-rebirth flow — but noted for
  M8 that the bible says skills should survive a rebirth at half power, not be wiped, so M8 will
  need its own reset logic. Verified live via the real `H` key AND a real simulated mouse click on
  the actual on-screen button (screenshots confirmed the armed-hint text and the button correctly
  disappearing once spent).
- **S6 first slice: the village hub** (log part 6, 2026-09-14): `data/hub.json` (1 location so far,
  `hub_village`), a pure/tested `hub_system.h/.cpp` (11 checks), `Game::activateHubLocation()`
  (dispatches by action kind — only `"talk"` exists yet, opens an NPC's dialogue directly from the
  hub), and a real in-game Village screen. Along the way, generalized the pause menu's Main page
  from a hand-picked `forgeOn ? 5 : 4` row count into `Game::mainMenuOrder()`, a pure function of
  both conditional flags — needed the moment a second conditional row (Village, alongside S5's
  Forge) existed. Owner explicitly declined to also gate the Store behind `hub.village` (would
  have changed early-game behavior M7's balance pass was tuned against) — this slice is additive
  only. Verified live via harness probes AND a real simulated UI click path (gear icon → Village →
  talk to the elder → real dialogue with live choices).
- **`src/game/` reorganized into 5 category folders** (log part 6, 2026-09-14): `core/` (the `Game`
  class + its translation-unit split), `systems/` (equipment/skill/forge/mission, run/story state,
  floor table, footprints+roamer, camera), `save/` (save_system/save_slots/game_settings), `ui/`
  (title_screen/ui_layout/localization), `audio/`. Pure filesystem/build reorg — no behavior change;
  every `#include` stayed a bare filename (all 5 folders added to the include search path instead),
  only `CMakeLists.txt`'s file list needed updating. Full rebuild + 23/23 tests verified clean on
  both desktop and web.
- **S5: forging** (log part 6, 2026-09-14): `data/forge.json` (3 recipes), a pure/tested
  `forge_system.h/.cpp`, `MetaSaveData.forgeRecipesKnown` (meta-scoped — survives rebirth, per
  STORY_BIBLE §8's rebirth table, unlike S4's run-scoped skills), the chapter-entry grant pipeline
  extended (not duplicated) to also grant recipes, `Game::tryCraft()` (deducts gold+materials,
  equips the result atomically), and a real in-game Forge screen — conditionally shown only once
  `hub.forge` is set (Skills' own choice was to always show; reasoned live that forging's own
  content doesn't earn that same always-visible treatment). Verified live via harness probes AND a
  real simulated UI click path (gear icon → Forge → craft row). Still ahead: the other two authored
  recipes aren't granted by any chapter yet (no ch_04+ data exists), and the hub screen itself.
- **S4's first slice** (log part 6, 2026-09-14): `data/skills.json` (3 lineages, root+tier-1 each),
  a pure/tested `skill_system.h/.cpp`, `RunStoryState` tracking, the chapter-entry grant pipeline
  (new — nothing had read a chapter's `grants` at runtime before this), live atk/def effects in
  combat, a real in-game Skills screen. Verified live; one real bug (grant-only roots were
  purchasable through the UI) caught and fixed the same session. Still ahead within S4 itself:
  deeper tiers per lineage (today ships root+1, not the full 功法三系 §3 describes), which stays
  the tree's own open item rather than blocking S5.
- **"C"**, the mobile UI-scale pass (log part 6, 2026-09-14): the battle screen's scale-up made
  consistent everywhere on that screen (it previously stopped partway through), two real overflow
  bugs the finished scale-up exposed are fixed, the dialogue box now scales the same
  bottom-anchored way, both the dialogue rows and the battle buttons clear the 44px touch-target
  guidance, and a real pre-existing `ui_layout_test` encoding bug is fixed.

**Still true, unrelated to either:**
1. **The browser build has no Stage Select at all** (that UI is behind `#ifndef __EMSCRIPTEN__`), so on the
   web the only way between floors is the stairs — fine for now, but a web hub means porting that screen.
2. Enemy mixes/item tables/event pools stay provisional past F21 (S3's own scope) until a later phase's
   chapter content reaches that far.

## Milestone status

| Milestone | Status | Notes |
|---|---|---|
| M0 — Baseline safety net | ✅ Done (gap closed 2026-09-13) | 5/5 headless tests build+pass+verified. `texture_test` was left broken by owner's choice; the 2026-09-13 refactor fixed its build (missing GLFW include dir) and its 2 failing checks (it pointed at `assets/font_atlas.png`, deleted by the TTF-font pull) — see log |
| M1 — Engine scaffolding | ✅ Done | Game State Machine, Event Bus, Meta/Run save schema — all built, wired minimally, and test-verified. See log. |
| M2 — UI framework bring-up | ✅ Done (Vulkan + WebGL2); WebGPU ⬜ | ImGui is wired into **both** backends that can be built and verified here. Desktop: `imgui_layer.cpp` (GLFW + Vulkan). Browser: `src/engine/imgui_web.{h,cpp}` — ImGui core + `imgui_impl_opengl3` in ES3 mode + an Emscripten DOM event bridge (mouse/wheel/touch/keyboard), because the web build has no SDL/GLFW at all. F1 (debug overlay) and F2 (styling spike) now toggle on web exactly as on desktop, and clicks that land on an ImGui window no longer reach the game underneath. **Verified visually on both platforms 2026-09-13** (browser screenshots of the overlay + spike over the title screen and the dungeon; native Xvfb screenshot of both windows) — which also closes the long-standing "styling spike correctness unconfirmed" gap: the NoBackground ImGui panel over the scene-graph 9-slice backdrop reads as one panel. **Still open:** ImGui on the **WebGPU** backend — `navigator.gpu.requestAdapter()` returns null in every browser available here, so it could be written but never seen; not started. **Also not done, now a recorded risk rather than a task:** font-atlas sharing — ImGui uses its built-in ASCII-only font, so locale strings in an ImGui window render as tofu boxes; that is why the toast/stage-select windows stay desktop-only and no player-facing screen uses ImGui. Cost: web wasm 1,960,362 → 2,548,484 B (+588 KB) with ImGui compiled in |
| M3 — World logic core | ✅ Done | Condition/Flag Evaluator, Entity Status System, Story Controller — all built, test-verified, AND retrofitted into real gameplay (door/key gate, dialogue `requires` gate, beat advancement on floor entry). See log. |
| M4 — Encounter Resolution + Mission System | ✅ Done | `EncounterKind` resolution (opt-in `dialogue_gate`, `direct_battle` stays default for all shipped content per owner's decision), dialogue `action` verbs (`give`/`setStoryFlag`/`enterBattle`/`startMission` — first real implementation of dialogue actions at all), Mission System (definitions/trackers/daily-reset/event-driven progress) wired to the Event Bus. See log. |
| M5 — Stage Select + UI migration | ✅ Done (scope reduced 2026-09-13 by owner decision — ImGui migrations **cancelled**) | Shipped: the Stage Select hub (keyboard `Tab`, per-stage locked/reason/new/completed/mission badges) and the Notification/toast system (Event Bus: new mission, daily reset, level up), both drawn with the game's own scene-graph UI. **Cancelled, not deferred:** migrating Inventory/Shop/Dialogue to ImGui. Owner: *"M5 should not in the plan because ImGui can't fit all UI features."* Those screens keep the hand-built `Node` UI — ImGui cannot express the art-heavy touch-first screens this game needs (custom 9-slice panels, atlas sprite grids, per-item icons, CJK at arbitrary size, the on-canvas virtual pad), and its built-in font would draw every locale string as tofu. ImGui stays M2's dev tooling only. The ImGui stage-select window still exists desktop-only (Tab) as dev parity; the player-facing hub is the game's own. |
| M6 — Equipment + Power Bar | ✅ Done | Power Bar math + Equipment System built/tested, THEN wired into live combat: real-time hold-duration input, the full Attack→Defense round flow, damage resolution, and rendering all replace the old auto-attack loop. Compiled, regression-tested, and smoke-tested via injected real key-hold/release cycles (win and lose paths both exercised) — **not yet visually confirmed by the owner**. See log. |
| M7 — Balance & checklist closure | ✅ Done | Whole-tower balance simulation (full-item and zero-item runs) against real `data/*.json`; dialogue `next`-chain integrity check across all files; design-doc checklists closed with evidence; found and fixed a real gap (Entity Status System built in M3 but never wired into live gameplay — floors didn't actually stay cleared). See log. |
| M8 — Content authoring | ✅ Done (with 3 small system additions the content needed to be reachable) | 9-item `data/equipment.json` sold through the Store (auto-equips), 3-mission `data/missions.json` (once/daily/side) offered and claimable through NPC dialogue, 3 monster types converted to `dialogue_gate`, Stage Select preview text for all 11 floors. Found and fixed 3 real gaps along the way (mission rewards never granted, equipment never loadable/equippable, `applyEquipmentStats` never called) plus 2 unrelated pre-existing dead-content bugs. See log. |
| M9 — Platform verification & polish | 🟡 In progress | Full regression now passes on **both** Windows (Debug+Release) and web (WebGL+WebGPU, Emscripten 6.0.9, actually executed under Node not just compiled) for the first time this project has ever had that confirmed. Owner also asked, mid-milestone, for all 11 stages' mazes to be regenerated via Wilson's algorithm with per-floor-growing size — done (`tools/gen_mazes.py`), same entity roster preserved exactly, tile size now dynamic per stage. **Still open:** manual floor-1-to-11 playthrough, a browser/WebGPU playthrough, an audio pass, a docs-accuracy check. See log. |
| M10 — Title phase (New Game / Continue / Settings) | ✅ Done | Title screen drawn with the game's own renderer (so it exists on web too), numbered save slots (`save/slotN.json`, atomic writes), settings (`save/settings.json`: language, slot count, font scale), a 6-language text table (`data/text.json` with built-in fallback), and the "start a new game in this empty slot?" confirm dialog (owner-reported: an empty slot previously did nothing). Verified natively under Xvfb with real key events, and again in a browser on the live site. |
| M11 — Web delivery pipeline | ✅ Done (deployed and verified live) | Root cause of the dead virtual keypad: the WebGL backend reported a 1280x720 design space while the canvas and the page's tap mapping were 1024x768, and `glViewport` used the design size inside a 768-tall buffer, shifting every drawn control 48px below its hit box. Fixed by splitting design space from drawing buffer. The published page is now a hand-maintained clean page (`web/clear.html`: no Emscripten logo, no status/spinner block, no debug console; real byte-level loading progress; animated title screen) that survives rebuilds, and artifact filenames are version-stamped per build so a cached `.data` can never mismatch a fresh page. Live: https://wslhermesai.github.io/TOMS/ |
| M12 — Source-layout refactor | ✅ Done | `game.cpp` 3,104 -> 268 lines, split into 9 responsibility units; `toms::Camera` extracted to `src/game/camera.{h,cpp}` with 30 unit checks (`camera_test`); shared helpers in `game_helpers.h` / `game_condition.h` / `game_internal.h`; new `docs/architecture/CODE_LAYOUT.md`; `texture_test` (the last accepted M0 gap) fixed -- 17/17 tests pass; native walk re-verified under Xvfb. No behaviour change. |
| S1 — Entity footprint (1/2/4 grids) + roamers | ✅ Done (2026-09-13) | Engine: `src/game/footprint.h` (legal sizes 1x1/2x1/1x2/2x2 per doc F1, tile coverage, F5 y-sort key, JSON parsing that rejects an illegal size instead of shipping it, and `resolveFootprint()` — the one place that decides stage-file override > character type table > 1x1) and `src/game/roamer.{h,cpp}` (`toms::Roamer`, pure logic behind a `GridQuery` interface: wander with a heading, detect radius 6 / lose radius 9 hysteresis, greedy chase, and a candidate step is only legal when the WHOLE footprint fits on walkable tiles, so it never phases through walls). Data: `data/footprints.json` (character-level tiers — golem/demon 2x1, demonlord_vorkath 2x2 + name). Gameplay: all occupied tiles block/bump (F6), a big monster is never walked into (pure bump: the player fights from the adjacent tile and the boss keeps its cell), one unified y-sorted draw pass with the player included (F5), footprint-sized sprites (F2) and a name banner for the 4-grid/roamer tier. Tests: `footprint_test` — 88 checks, incl. a validator over all 11 shipped stages (legal size, no entity on a wall, no overlap (F7), stairs still reachable with the bigger blockers = anti-softlock). Six monsters in four stage files stood in 1-tile nooks that cannot host their tier (stage10's boss had no 2x2 room) and were moved to the nearest fitting tile. Roamers are engine-ready but not yet placed in shipped data — the two designed ones (王座之影, 前世道兵王) arrive with S2. |
| S2 — `tools/gen_floors.py` + the 70-floor table | ✅ Done (2026-09-13) | `tools/gen_floors.py` emits **70 floor specs** (`data/story/floors/F01…F70.json`, the section-4 schema: act/indexInAct/seal/role, maze params, enemy mix+range+elites+boss, item table, sampled events, side-story hook, narrative keys, nextFloor, meta) plus **60 playable grids** (`Fnn.stage.json`, every non-boss floor) in the existing stage schema — boss floors are the ten hand-authored `data/stages/*.json` the section-3.2 table maps them to (recorded as `handAuthoredStage`). The maze reuses `tools/gen_mazes.py`'s Wilson + BFS (one implementation, same conventions: cell = 2x2 tiles, 2-tile passages), adds the table's rooms ("event containers") and extra loops, and pads the right/bottom edge with wall so the table's dims come out exact. `tools/validate_story.py` implements V7 (floor↔act↔nextFloor, boss floor → hand-authored stage), V8 (one player start, stairs up/down, maze fully connected, and each key reachable with the doors shut = anti-softlock), V9 (dims/rooms/loops/events/enemy range/elites/items/difficulty vs the table) and V12 (≥1 relic/whisper + ≥1 cache per floor), plus footprint legality/overlap and side-story placement — **6745 checks, ALL PASS**. The engine gained one resolution fallback (`loadStage` also looks in `data/story/floors/<id>.stage.json`), and `footprint_test` now validates the 60 generated grids through the runtime's own `parseStage` (71 stage files, 430 multi-grid entities) so generated data is gated by the game's parser, not just by Python. Verified natively by booting a new game into the generated F64 (63x42): playable, camera follows, 2-grid demons and rooms render (HUD 「第 64 層」). Provisional until their own phase: per-act enemy mixes / item tables (chapters, S3) and the event pools (S3) — the generator prefers `data/events/pool_*.json` the moment those exist. Docs corrected: the section-5.1 formula disagreed with its own table from tier 4 up, so it was replaced by the exact piecewise form the table implies. |
| S3 — Story data v3 + condition DSL + save v3 | ✅ Done (2026-09-13) | **DSL**: the four leaves the 70-floor story needs — `choiceMade`, `sideStoryState`, `counterAtLeast`, `cycleIndexAtLeast` — added to `src/engine/condition.{h,cpp}` (as *defaulted* virtuals on `ConditionContext`, so every existing context keeps compiling and answers "nothing chosen / cycle 1"), with cases in `condition_eval_test` and a state-backed context in the new `run_state_test`. **State**: `src/game/run_state.{h,cpp}` — `toms::RunStoryState`, the per-run truth for choices (first answer sticks), the three clamped counters (insight/resolve/humanity, `displayAt` from counters.json), side-story states, run flags, memory shards, floor progress + cleared floors, and death counts; `Game` owns one, `GameConditionContext` answers all four leaves from it, `newGame()` resets it and rebirth is `reset(keepShards=true)`. **Save v3**: `kRunSaveSchemaVersion = kMetaSaveSchemaVersion = 3`, the run file now carries choices/counters/sideStories/flags/floor/clearedFloors/shards/deaths and the meta file carries cycleIndex/endingsSeen/hintsUnlocked — and section 9's compatibility rule is implemented: a **pre-v3 file still loads**, with the new fields defaulted (verified live: an old `saveVersion: 1` slot loaded unchanged, and `run_state_test` covers the round trip + migration). **Data**: `data/story.json` v3 (chapters + seals + the old `arc[]` kept as the projection section 12 requires), `data/story/chapters/ch_01…ch_03` (section-4.1 schema: beats, grants, choices, side-story hooks, exit conditions, plus the `enemyMix`/`itemTable` section 6.2 reads), `data/story/flags.json` (25 declarations incl. the whole 8-choice arc) and `counters.json`; `data/events/pool_common.json` + `pool_act01…03.json` (authored, 8+8+8+10 events with the section-12.1 kinds); the boss-floor `story` blocks injected into stage01…03 (section 6.1 step 2). **i18n**: `tools/gen_story_i18n.py` fills `data/text.json` for every key the story data references (453 keys; 86 authored TC/EN, the rest backfilled + `_todo` per section 11.2) and is idempotent. **Generator**: `gen_floors.py` now reads the chapters for enemy mix/item table and the authored pools — floors F01–F21 report `contentSource: chapter` / `eventPoolSource: authored`, F22–F70 stay provisional as designed. |
| S3.5 — wire the 70 floors + story into play (**option B: (a)+(b) only**) | ✅ Done (2026-09-13) | `src/game/floor_table.{h,cpp}` — `toms::FloorTable`, the tower as ordered data: seq 1..70 derived by **walking the `nextFloor` chain** (not the file names), act/actIndex/indexInAct, role, seal, the i18n name key, and `mapRelPath()` (a normal floor → its generated grid, a boss floor → its hand-authored `stageNN.json`). `loadStage()` resolves floor ids through it and then sets `st.id`/`st.index`/`st.name` **and `st.up`/`st.down`** from the table, so the existing stair logic drives floor-to-floor progression with no new input code; `totalStages` and the hub list now come from the table too (70 floors, act titles as rows). `newGame()` starts on F01. Empty table ⇒ the old eleven-stage behaviour, still guarded. Verified: `floor_table_test` **990 checks** (chain order, prev/next, dangling links and cycles, 10 acts × 7, one boss map per act that exists, 60 grids with exactly one exit whose `connect.up` agrees with the table), **20/20 test binaries**, and in a real browser: New Game → F01 「第 1 層 (1/70)」, `jsFloorLinks()` = both links match the table, stepping onto F15's exit raised the floor prompt and confirming it loaded **F16 (16/70)** with the new floor's links, and the boss floor F07 loads the hand-authored act-1 map as 「村莊外緣・封印結 (7/70)」. |
| S3.5 (c)(d)(e) — story on screen, floor events, per-act visuals | ✅ Done (2026-09-13) | **(c)** the footer now shows the *floor's own* story line — its `intro` for the first 8 turns, then its `ambient` lines rotating as the player moves (`toms::storyLineIndex`, pure + tested) — with the grid's `story_note` as the fallback; plus an **act card** (act title + floor name, fading over 2.6 s) on each act's first floor. **(e)** `toms::floorThemeTint(actIndex, rgb)` gives every act a palette and the map tiles are tinted by it — one atlas, ten looks, no new art (S8 owns real tilesets); verified `themeAct` = 1/2/4 on F01/F08/F22 with visibly different tints. **(d)** every floor's `events` slots are now **real markers in the world** (the generator writes a per-slot legend kind `event:<id>`), drawn as solid story plates, and stepping on one shows its text (i18n, authored for acts 1-3) and applies an effect by kind — memory shards feed the run state, traps cost HP, caches/relics give HP+gold, whispers are remembered as flags. |
| S4 — Skill tree | 🟡 In progress (first slice done 2026-09-14; `s_yinqi_combo` added 2026-09-16) | `data/skills.json` (3 lineages × root+tier-1, plus `s_yinqi_combo` — a tier-2 `yinqi` node granted by `ch_04`'s `ss_04` side story, not purchased), pure/tested `skill_system.h/.cpp`, `RunStoryState` tracking + save v3 fields, the chapter-entry grant pipeline, live atk/def effects stacked into combat, a real in-game Skills screen. Verified live in a browser. Still ahead: `yuqi`/`faqi` still stop at tier 1 — the full 功法三系 tree STORY_BIBLE §3 describes needs more of ch_05+'s content. See log parts 6-7. |
| S5 — Forging | ✅ Done (2026-09-14) | `data/forge.json` (3 recipes), pure/tested `forge_system.h/.cpp`, `MetaSaveData.forgeRecipesKnown` (meta-scoped per STORY_BIBLE §8's rebirth table), the chapter-entry grant pipeline extended for recipes, `Game::tryCraft()`, a real conditionally-shown Forge screen. Verified live via harness probes and a real simulated UI click. Only `fr_gatewarden` is granted yet (ch_03) — the other two authored recipes await ch_04+ chapter data. See log part 6. |
| S6 — Village hub | 🟡 In progress (first slice done 2026-09-14) | `data/hub.json` (1 location: `hub_village`, gated on `hub.village`), pure/tested `hub_system.h/.cpp`, `Game::activateHubLocation()` (dispatches by action kind — only `"talk"` exists), a real in-game Village screen. The pause menu's Main page generalized from a hardcoded row count into `Game::mainMenuOrder()` (a pure function of both Forge's and Village's unlock flags) to support a second conditional row. Verified live via harness probes and a real simulated UI click. Store's unlock stays independent of `hub.village` (owner's explicit choice). Growth blocked on missing chapter content: the next location (`hub.crypt_shrine`, ch_05) needs a chapter file that doesn't exist yet. See log part 6. |
| M6 — Equipment actives (裝備主動技) | ✅ Done (first slice, 2026-09-16) | `data/actives.json` (1 active: `a_twin_flash`, on `twin_daggers`), `equippedActives()`, pure/tested `active_system.h/.cpp`, a real 4th battle button (`battleTapActive()`) wired into touch + keyboard (`H`) on both platforms. Manually-activated (a real button), not a passive auto-trigger — the owner's explicit choice. Verified live via the real key AND a real simulated click. Still ahead: the 3 actives actually named in the design docs need chapters (`ch_05`/`ch_07`/`ch_08`) that don't exist yet; no `activeSkillsKnown` meta field yet (deliberately deferred — no producer for it); only the `guaranteed_crit_next_attack` effect kind exists, not `a_sanctuary_echo`'s auto-trigger shape. See log part 7. |
| M7 — Endings (`endings.json` + resolver + UI) | ✅ Done (first slice, 2026-09-16) | `data/story/endings.json` (all 15), pure/tested `ending_system.h/.cpp` (`judgeEnding()`/`checkNamedEndings()`, per §7.2 vs §7.3's distinct algorithms), a real full-screen ending scene wired into `finishCombatLose()`. Fixed a real pre-existing bug (`RunStoryState::noteDeath()` was never called) that's what makes `e_13` (12 non-boss deaths) the one ending reachable with today's content — owner explicitly approved wiring it live. Closed 3 condition-DSL gaps found while authoring (`always`, `memoryShards`/`deathsNonBoss` leaves, and `itemHeld` never recognizing equipped gear). Verified live via 12 real forced deaths → a real ending screen → dismissed via both keyboard and a real click. Still ahead: 14/15 endings need F70 or `cycleIndex≥9`, neither reachable yet. See log part 7. |
| M8 — Rebirth (輪迴, `cycles.json`) | ✅ Done (first slice, 2026-09-16) | `data/story/cycles.json` + `cycle_system.h/.cpp`, `Game::rebirth()` (skills kept at half effect for tier≥1 nodes, skill points/Super gauge cap halved-floored, shards kept, forge/actives knowledge needed zero new code, everything else reset, +1 `cycleIndex`), a real two-choice ending screen (`rebirthOffered()`). Fixed `CombatState::kSuperThreshold` being a compile-time constant — now `RunStoryState::superMax()`, per-run and save-persisted. Verified live via real keyboard AND real clicks on both buttons; `e_13` doubles as a fully real end-to-end test path. Still ahead: `cultivationTier` halving (no underlying system to halve, named as a deliberate gap); `e_10`'s cap-reached path isn't exercised live yet (needs 8 real rebirths). See log part 7. |
| M9 — Full content (`ch_04`–`ch_10`) | 🟡 In progress (first slice: `ch_04` done, 2026-09-16) | `ch_04.json` (F22-F28), the `ev_library_*`/`ev_common_*` event text and floor narrative text ch_04 needed, `skeleton_scholar` NPC actually placed on a stage for the first time, and its dialogue extended with `ss_04`'s full return/study branch (new `unlockSkill`/`setSideStoryState` dialogue verbs, a new `runFlagSet` condition leaf). Verified live end-to-end. Still ahead: `ch_05`–`ch_10` (6 more chapters), which is what still blocks the village hub's next location (`hub.crypt_shrine`), the two remaining named actives (`a_sanctuary_echo`, `a_soul_echo`), 14 of the 15 endings, and a live test of `e_10`'s cap-reached path. See log part 7. |
| S8 — Art pipeline (shader variants, atlas, rigs) | ⬜ Not started | `docs/design/ART_AND_ABILITY_DESIGN.md` sections 1.3 / 1.5 / 8; 1,006 animation frames + 469 static images, 3 MB budget. |

Legend: ⬜ Not started · 🟡 In progress · 🟥 Blocked · ✅ Done

---

## Log

The dated log grew too large for one file and is now split by date into numbered parts, oldest
first — each part's own header repeats its date range. This index is the only thing that changed;
no log content was edited, only moved.

| Part | Date(s) | Covers |
|---|---|---|
| [1_PROGRESS_REPORT.md](1_PROGRESS_REPORT.md) | 2026-09-07 | M0–M5 (part 1) built; the Vulkan vertical-flip bug found+fixed; first owner screenshots confirm real rendering; F12 crash investigation |
| [2_PROGRESS_REPORT.md](2_PROGRESS_REPORT.md) | 2026-09-08 | UI font-size setting; M6 (Power Bar + Equipment) built then wired into live combat; M7 balance closure; a real battle-focus/victory-dismiss bug |
| [3_PROGRESS_REPORT.md](3_PROGRESS_REPORT.md) | 2026-09-09 | M8 content (equipment/missions/dialogue_gate); first successful web build; M9 started, mazes regenerated; two owner-reported bugs found+fixed (dialogue input, unwilling floor changes); the VK_ERROR_DEVICE_LOST crash root-caused; hold-to-move polish |
| [4_PROGRESS_REPORT.md](4_PROGRESS_REPORT.md) | 2026-09-11 to 2026-09-13 | Title phase (New Game/Continue/Settings); a real web delivery pipeline; upstream pulls; the 70-floor/15-ending/50-enemy story+art design document set |
| [5_PROGRESS_REPORT.md](5_PROGRESS_REPORT.md) | 2026-09-13 | The `game.cpp` source-layout refactor; ImGui on the browser backend; S1 (footprints+roamers, deployed live); S2 (`gen_floors.py`, 70-floor data); S3 (story v3, save v3); S3.5 (the 70 floors wired into real play, then story-on-screen/floor-events/per-act visuals); per-floor grid sizes; the mobile pass (A/D shipped, B held back, C scoped) |
| [6_PROGRESS_REPORT.md](6_PROGRESS_REPORT.md) | 2026-09-14 | "C" (the mobile UI-scale pass) finished — battle screen scale-up completed, dialogue box scaled, two real overflow bugs fixed; S4's first slice — the skill tree's foundation, chapter-entry grant pipeline, live combat effects, a real Skills screen, one real bug caught+fixed live; S5 — forging (`forge.json`, meta-scoped known recipes, `tryCraft`, a real conditionally-shown Forge screen), verified live via harness probes and a real simulated UI click; `src/game/` reorganized into 5 category folders (core/systems/save/ui/audio), zero behavior change, 23/23 tests verified; S6 first slice — the village hub (`hub.json`, `activateHubLocation`, a real Village screen), the Main-page row logic generalized to `mainMenuOrder()`, verified live via a real simulated click through to the elder's dialogue |
| [7_PROGRESS_REPORT.md](7_PROGRESS_REPORT.md) | 2026-09-16 | M6 first slice — equipment actives (`actives.json`, `equippedActives()`, a real 4th battle button wired into touch + keyboard on both platforms), verified live via the real `H` key and a real simulated click; a rebirth-scope note found while scoping (skills should survive a cycle-rebirth at half power per the bible, not be wiped — no live bug today, but relevant for the not-yet-built M8); M7 first slice — the 15-ending resolver (`endings.json`, `judgeEnding`/`checkNamedEndings`, a real full-screen ending scene), a real pre-existing bug fixed (`noteDeath()` never called, so `e_13` could never fire), verified live via 12 forced deaths reaching a real ending screen; M8 first slice — 輪迴 rebirth (`cycles.json`, `Game::rebirth()`, a real two-choice ending screen), `CombatState::kSuperThreshold` converted to a real per-run `RunStoryState::superMax()`, verified live end-to-end (skills kept at half power, stats/gold/equipment reset, real button clicks) via `e_13`'s already-reachable path; M9 first slice — `ch_04` content (`ch_04.json`, F22-F28's real narrative text, `ss_04` made fully real via a `skeleton_scholar` NPC that was dead content until now), two structural gaps found+closed (`pool_actNN.json` never loaded at runtime; `RunStoryState` flags had no condition leaf — added `runFlagSet`), new `unlockSkill`/`setSideStoryState` dialogue verbs, verified live end-to-end; stair alignment — all 70 floors' stairs now physically line up (new `tools/gen_stairs.py`, chain-generated, roster preserved), `Game::loadStage()`'s new `StageArrival` makes climbing/descending land exactly on the matching tile, two generator bugs (footprint spillage, multi-blocker softlock) found+fixed via the test suite, verified live with a real step onto a real stair and a real Enter confirm |

## Open Questions / Blockers

### 1. `texture_test` fails to build — pre-existing CMake bug, left as-is by owner's decision

**What's wrong:** `texture_test` compiles `renderer.cpp`/links against `renderer.h`, which
`#include`s `GLFW/glfw3.h` (`src/engine/renderer.h:7`). The `tower_vulkan` target gets GLFW headers
via `target_include_directories(tower_vulkan ... ${glfw_SOURCE_DIR}/include)`
(`CMakeLists.txt:98`), but the `texture_test` target's own `target_include_directories`
(`CMakeLists.txt:221-225`) never adds that path — so it fails with
`Cannot open include file: 'GLFW/glfw3.h'`. Pre-existing (nothing this session touched
`CMakeLists.txt` or `renderer.h`), not caused by the architecture/roadmap work.

**Decision (2026-09-07):** owner chose **"skip it, continue M0"** — left broken for now, noted
here as a known/accepted gap rather than fixed. The one-line fix (add
`${glfw_SOURCE_DIR}/include` to `texture_test`'s include dirs, mirroring `tower_vulkan`) is still
available any time it's wanted; re-raise it if a later milestone needs `texture_test` green (e.g.
if Milestone 6's `equipment_test` or similar ends up needing the real Vulkan texture path).
