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

**Next action: M9 continued — `ch_05`–`ch_10`**, one chapter at a time (same research-then-scope-
then-build pattern `ch_04` used). `ch_05` is the natural next pick (unblocks the village hub's next
location, `hub.crypt_shrine`) but hasn't been scoped yet — check in with the owner before starting.
Separately, the art/UI polish pass (monster/item sprite quality, text size, background color) that
prompted the branch reconciliation below is in progress; not yet finished.

**A branch reconciliation happened here (2026-09-17), not a new feature.** Two independent
development streams had been running against this repo in parallel without either knowing about
the other — this session's own work (see log part 7: M6 equipment actives, M7 endings, M8 rebirth,
M9 `ch_04` content + stair alignment) and a separate branch on `github.com/WSLHermesAI/TOMS` (5
commits: equipment actives' own first slice, plus 4 "Mobile UI scale" steps). A `git merge` of the
two auto-resolved cleanly for most files but, for `CMakeLists.txt` specifically, took one side's
edit wholesale for a block both sides had inserted into — which silently dropped this session's
`ending_system`/`cycle_system` build entries (M7/M8 have no counterpart on the other branch at all,
so this was pure collateral, not a real conflict) and reverted this file's own banner to the other
branch's older state. Reconciled by hand, item by item:
- **Endings (`ending_system.h/.cpp`) and rebirth (`cycle_system.h/.cpp`)**: purely this session's
  work, nothing to merge — just re-added the three dropped `CMakeLists.txt` lines (source + the two
  test targets) verbatim.
- **Equipment actives**: kept, both sides had built the SAME feature independently and differently.
  The other branch's `equipment_actives.h/.cpp` is the more complete pure-logic layer (real
  per-active uses-per-battle + cooldown timers + `st_echo`/`st_silence` status hooks, 41 tests) but
  had never been wired into combat; this session's `active_system.h/.cpp` was simpler but WAS
  already wired into a real battle button (keyboard `H`, touch, HUD). Kept the richer logic,
  deleted `active_system.*`, and rewired `battleTapActive()`/the HUD button/the touch hit-test onto
  `canUseActive()`/`useActive()`. This is a genuine capability upgrade, not just a swap:
  `surviveLethal` (`a_sanctuary_echo`, lethal hit leaves 1 HP) now actually applies in
  `resolveEnemyClockFire()`, which nothing had wired before on either branch.
- **Mobile UI scale (`ui_root.h`, `dialogue_layout.h`)**: purely additive, kept as-is — a different
  scaling axis (`UiRoot` scales UI element geometry/hit-boxes; `g_uiScale`, this session's own
  text-size knob, scales font glyph size) that doesn't overlap with anything this session touched.
- Verified live post-reconciliation: equip `qingxiao_blade`, start a real battle, tap the active —
  uses-left drops 1→0, `nextAttackGuaranteedCrit` arms, a second tap same battle is correctly inert.
  Full regression: 30/30 test binaries pass (both `ending_system_test`/`cycle_system_test` AND the
  incoming `equipment_actives_test`/`ui_root_test`/`dialogue_layout_test` all present and green);
  both `tower_vulkan` and `toms_web` compile clean.

**Just closed (this session, before the reconciliation above):**
- **M9 first slice — `ch_04` content + stair alignment** (log part 7, 2026-09-16): see log for
  full detail — `ch_04.json`/F22-F28 narrative content, `ss_04` made real, a new `runFlagSet`
  condition leaf; separately, all 70 floors' stairs now physically align (`tools/gen_stairs.py`)
  and `Game::loadStage()`'s `StageArrival` lands the player on the matching tile.
- **M8 rebirth, M7 endings, M6 equipment actives (first slice)** (log part 7, 2026-09-16): see log
  — `cycles.json`/`Game::rebirth()`, the 15-ending resolver, and equipment actives' original first
  slice (now superseded in content by the reconciliation above, but the combat-wiring pattern it
  established survived into the reconciled version).
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
| S4 — Skill tree | 🟡 In progress (first slice done 2026-09-14) | `data/skills.json` (3 lineages × root+tier-1, 6 nodes), pure/tested `skill_system.h/.cpp`, `RunStoryState` tracking + save v3 fields, the chapter-entry grant pipeline (new: nothing had read a chapter's `grants` at runtime before this), live atk/def effects stacked into combat, a real in-game Skills screen. Verified live in a browser. Still ahead: deeper tiers per lineage (today ships root+1), the real 功法三系 tree STORY_BIBLE §3 places at ch_04/F25 (no chapter data yet). See log part 6. |
| S5 — Forging | ✅ Done (2026-09-14) | `data/forge.json` (3 recipes), pure/tested `forge_system.h/.cpp`, `MetaSaveData.forgeRecipesKnown` (meta-scoped per STORY_BIBLE §8's rebirth table), the chapter-entry grant pipeline extended for recipes, `Game::tryCraft()`, a real conditionally-shown Forge screen. Verified live via harness probes and a real simulated UI click. Only `fr_gatewarden` is granted yet (ch_03) — the other two authored recipes await ch_04+ chapter data. See log part 6. |
| S6 — Village hub | 🟡 In progress (first slice done 2026-09-14) | `data/hub.json` (1 location: `hub_village`, gated on `hub.village`), pure/tested `hub_system.h/.cpp`, `Game::activateHubLocation()` (dispatches by action kind — only `"talk"` exists), a real in-game Village screen. The pause menu's Main page generalized from a hardcoded row count into `Game::mainMenuOrder()` (a pure function of both Forge's and Village's unlock flags) to support a second conditional row. Verified live via harness probes and a real simulated UI click. Store's unlock stays independent of `hub.village` (owner's explicit choice). Growth blocked on missing chapter content: the next location (`hub.crypt_shrine`, ch_05) needs a chapter file that doesn't exist yet. See log part 6. |
| Equipment actives (裝備主動技) | ✅ Done (2026-09-17) | The fourth of `STORY_DATA_SCHEMA.md`'s four systems, fully wired: `data/actives.json` (`a_qingxiao_edge` guaranteedCrit x1.8, `a_sanctuary_echo` surviveLethal), `src/game/systems/equipment_actives.h/.cpp` (real uses-per-battle + cooldown + `st_echo`/`st_silence` status hooks, 41 tests), a real 4th battle button (keyboard `H`, touch, HUD) wired through `canUseActive()`/`useActive()`, and both effect kinds actually applying in combat (`guaranteedCrit` on the next attack, `surviveLethal` clamping a lethal hit to 1 HP in `resolveEnemyClockFire()`). Reconciled from two independent implementations of this same system — see the Next Step section's reconciliation note for the full story. Still ahead: the two side-story items (`qingxiao_blade`/`soul_echo_bell`) need their acquisition path (ss_09/ss_08, not authored yet). |
| M7 — Endings (`endings.json` + resolver + UI) | ✅ Done (first slice, 2026-09-16) | `data/story/endings.json` (all 15), pure/tested `ending_system.h/.cpp`, a real full-screen ending scene wired into `finishCombatLose()`. See log part 7. Still ahead: 14/15 endings need F70 or `cycleIndex≥9`, neither reachable yet. |
| M8 — Rebirth (輪迴, `cycles.json`) | ✅ Done (first slice, 2026-09-16) | `data/story/cycles.json` + `cycle_system.h/.cpp`, `Game::rebirth()`, a real two-choice ending screen. See log part 7. |
| M9 — Story content expansion (`ch_04`–`ch_10`) | 🟡 In progress (first slice: `ch_04` done, 2026-09-16; stair alignment done, 2026-09-16) | `ch_04.json` (F22-F28) + `ss_04` made fully real; separately, all 70 floors' stairs now physically align and `Game::loadStage()` lands the player on the matching tile (`tools/gen_stairs.py`, `StageArrival`). See log part 7. Still ahead: `ch_05`–`ch_10` (6 more chapters). |
| S8 — Art pipeline (shader variants, atlas, rigs) | 🟡 In progress (procedural placeholder pass, 2026-09-17) | `docs/design/ART_AND_ABILITY_DESIGN.md`'s real pipeline (ComfyUI/SDXL-generated pixel art, 1,006 animation frames + 469 static images) is still not started — no image-generation tool available in this environment. In the meantime, `tools/make_sprites.py` (new, supersedes `make_item_icons.py`/`make_missing_sprites.py`) regenerates all 30 shipped sprites with a shared outline+shading pass instead of flat single-tone fills, and fixes a real content bug (`boss_demonlord.png` was byte-identical to `demon.png` — the final boss looked exactly like a trash mob). Also in progress: UI text size (`g_uiScale` default raised 1.0→1.15) and background color (the duplicated clear-color literal in `renderer.cpp`/`renderer_webgl.cpp` unified into one `kBackgroundClearColor` constant in `render_iface.h`). |

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
| [7_PROGRESS_REPORT.md](7_PROGRESS_REPORT.md) | 2026-09-16 to 2026-09-17 | Equipment actives' original first slice, the 15-ending resolver, 輪迴 rebirth, `ch_04` story content + `ss_04`, all 70 floors' stairs made to physically align — **note:** this log part's own prose labels these "M6"/"M7"/"M8"/"M9", reusing numbers this table had already assigned to older, unrelated milestones (see rows above) back when this part was written; read it by feature name, not by that number. Also: reconciling this session's work with a parallel branch's own equipment-actives + mobile-UI-scale work (2026-09-17, see the Next Step section above) |

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
