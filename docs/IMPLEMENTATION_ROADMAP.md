# Implementation Roadmap — from current state to the full design

> **Status:** Proposed plan. This sequences the work described in
> `GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md` (system list, state machine, UI framework decision)
> plus the still-open items in `GAME_DESIGN_DOCUMENT.md` §16–17, `FIGHT_SCENE_DESIGN.md` §7, and
> `MAIN_BATTLE_SCENE_DESIGN.md` §7 into buildable, testable milestones. Each milestone lists what it
> depends on, what "done" looks like, and which of the architecture doc's 28 systems (§13) it
> delivers — so this can be picked up incrementally without re-deriving the plan.
>
> **Sequencing principle:** engine scaffolding that everything else depends on comes first
> (state machine, save schema, event bus, condition evaluator); the riskiest/most novel item
> (real-time Power Bar input) gets its own isolated milestone rather than being bundled into UI
> work; content authoring and balance passes are deliberately last, after the systems that make
> content *meaningful* (missions, stage select, entity status) exist.
>
> **Testing convention to keep:** every new system in this repo ships with a small headless
> `*_test` binary (see `node_test`, `font_test`, `texture_test`, `object_test`, `log_test`) that
> asserts behavior without rendering. Each milestone below names the test(s) it should add — this
> is not optional polish, it's how this project has verified every prior system.

---

## Milestone 0 — Baseline safety net

**Goal:** confirm the ground you're building on before adding to it.

- [ ] Run all existing headless tests (`node_test`, `font_test`, `texture_test`, `object_test`,
      `log_test`) on both the Windows and web build targets; fix anything already broken before
      layering new systems on top.
- [ ] Confirm the two HTML demos (`demos/fight_scene_demo.html`,
      `demos/main_battle_scene_demo.html`) still load and compute correctly — they're the only
      current executable spec for the Power Bar/Equipment math.

**Depends on:** nothing. **Unlocks:** everything else.

---

## Milestone 1 — Engine scaffolding (state machine, save schema, event bus)

The architecture doc's §4 (state machine), §13 #26 (save), and §13 #10 (event bus) are the
foundation nearly every later milestone assumes exists.

| Deliverable | Detail |
|---|---|
| Explicit `GameState` enum + transition table | Formalize the states already implicit in `game.cpp` (`Explore`/`CombatState`/dialogue/store) into the named states from architecture-doc §4.2, plus the new ones (`StageSelect`, `StageLoading`, `EncounterResolve`, `StageComplete`, `Ending`) as no-op stubs for now |
| Save schema v2: Meta save vs. Run save split | Per architecture-doc §13 #26 / `GAMEPLAY_ROGUELIKE_DATA_SCHEMA.md` §8–9 convention: Meta = story flags, unlocked stages, permanent stat bonuses, mission `claimed` set; Run = current stage, player state, per-stage entity status map. Add `schemaVersion` to both from day one (per `GAMEPLAY_ROGUELIKE_TECH_ARCH.md` §10) |
| Minimal Event Bus | A small pub/sub (subscribe-by-event-name, synchronous dispatch is fine at this scale) — first two events wired: `EnemyDefeated{enemyId}`, `ItemCollected{itemId}` (nothing subscribes yet; Milestone 4 is the first real consumer) |

**Test additions:** `state_machine_test` (transition table has no dead/unreachable states),
`save_test` (round-trip Meta/Run save through the new schema, including a version-mismatch case).

**Depends on:** Milestone 0. **Unlocks:** Milestones 3, 4, 5.

---

## Milestone 2 — UI framework bring-up (Dear ImGui, dev-overlay only)

This is architecture-doc §2.3 Phase 1 — deliberately the *lowest-risk* way to get ImGui wired into
all three renderer backends before anything shipped depends on it.

| Deliverable | Detail |
|---|---|
| ImGui backend wiring ×3 | Vulkan (desktop), WebGL2, WebGPU — using ImGui's official backends per architecture-doc §2.2's research; render submitted per the layer order in architecture-doc §3.3 (after scene-graph, before fade overlay) |
| Font atlas sharing | Feed ImGui the *same* CJK-capable atlas `font.cpp` already bakes, rather than building a second one — validates architecture-doc §2.3 claim #4 before anything depends on it |
| Dev-only debug overlay | Stat sliders (for hand-tracing bestiary fights live), a toggle for the existing `render_iface.h` node-filter/split-screen diagnostic, a log viewer over `log.h` |
| Styling spike | One throwaway panel styled with `ImGuiWindowFlags_NoDecoration` + transparent background + a scene-graph-drawn 9-slice behind it — **this validates the riskiest unverified claim in the architecture doc (§2.3) before Milestone 5 commits to it.** If this spike looks wrong, revisit the UI framework decision before migrating real screens. |

**Test additions:** none (visual-only); verify manually in all three build targets per the repo's
existing "verified rendering in browser" / screenshot practice.

**Depends on:** Milestone 0. **Can run in parallel with Milestone 1.**

---

## Milestone 3 — World logic core (Entity Status, Condition Evaluator, Story Controller)

Architecture-doc §5, §9, §7.

| Deliverable | Detail |
|---|---|
| `StageEntityStatus` | Per `stageId+entityId` status map (`Untouched/Engaged/Defeated/Collected/Opened/Hidden`), stored in the Run save from Milestone 1 |
| Condition/Flag Evaluator | Implement the `all`/`any`/`not` + leaf-type expression engine (§9.1); start with the leaf types the architecture doc names (`storyBeatAtLeast`, `storyFlagSet`, `itemHeld`, `missionComplete`, `missionActive`, `statAtLeast`, `stageCleared`) |
| Story Controller | `storyFlags` set + `currentBeat` int, wired to the existing `data/story.json` beats; beat advances exactly where `connect.up` already transitions floors today (no behavior change, just now recorded as data) |
| **Retrofit, don't just add:** reframe the existing door/key check and the existing dialogue `requires` gate to go through the new Condition Evaluator instead of their own bespoke checks | This is what makes §9's "one shared engine" claim true rather than aspirational — skipping this step means two gating systems exist side by side, which the architecture doc explicitly warns against |

**Test additions:** `condition_eval_test` (each leaf type + `all`/`any`/`not` nesting),
`entity_status_test` (status transitions + persistence round-trip).

**Depends on:** Milestone 1. **Unlocks:** Milestones 4, 5.

---

## Milestone 4 — Encounter Resolution + Mission System

Architecture-doc §5.3, §8. This is where the "enemy talks first, then a choice can start a new
story" mechanic and the daily/one-time mission control actually get built.

| Deliverable | Detail |
|---|---|
| `encounterKind` field | Add to stage tile data (`direct_battle` default / `dialogue_gate` / `story_trigger` / `merchant`); Encounter Resolution System reads it on tile-enter and routes to the right state from Milestone 1's state machine |
| Dialogue `action` verb extension | Add `action.enterBattle`, `action.setStoryFlag`, `action.startMission` alongside the existing `action.give` — implement as a small enum switch, matching the project's stated no-scripting preference |
| `data/missions.json` schema | `MissionDefinition` fields per architecture-doc §8.1 |
| `MissionTracker` + `MissionScheduler` | Per-save progress + the daily-reset rollover check (§8.3); wire to Milestone 1's Event Bus so `EnemyDefeated`/`ItemCollected`/a new `ChoiceMade` event drive mission progress with zero coupling back into Battle/Item/Dialogue code |

**Test additions:** `encounter_resolve_test` (all 4 `encounterKind` values route correctly),
`mission_test` (daily reset boundary, one-time missions never re-arm, progress ticks from events).

**Depends on:** Milestone 3. **Unlocks:** Milestone 5.

---

## Milestone 5 — Stage Select + first real UI migrations

Architecture-doc §10, §2.3 Phases 2–3.

| Deliverable | Detail |
|---|---|
| Stage Select screen (new) | Built directly in ImGui (per Milestone 2's validated styling approach); per-stage `locked`/`lockReason`/`isNew`/`isCompleted`/`missionBadgeCount` computed from Milestones 3–4's Condition Evaluator + Entity Status + Mission Tracker |
| Inventory + Shop migrated to ImGui | Replace the hand-built `Node`-tree construction (`NODE_SYSTEM.md`) with ImGui `ImageButton` grids + drag-drop, per architecture-doc §2.3 Phase 2 — behavior unchanged, only construction method changes |
| Dialogue box migrated to ImGui | Phase 3; wires the new `action` verbs from Milestone 4 into real UI |
| Notification/Toast system | Minimal — subscribes to the Event Bus for "new mission available," "daily reset," "level up" |

**Test additions:** none new (this is UI-construction, covered by manual verification); confirm the
existing `object_test`-style leak detector reports 0 leaks with the new ImGui-driven screens open
and closed repeatedly (regression check on the existing lifecycle-tracking discipline).

**Depends on:** Milestones 2, 3, 4.

---

## Milestone 6 — Equipment System + real-time Power Bar input

This is the highest-risk milestone — it's the one item flagged in `MAIN_BATTLE_SCENE_DESIGN.md` §6
as requiring a capability the engine doesn't have yet ("the current `main.cpp` is a scripted,
headless playthrough... does not yet have a real-time input loop capable of measuring
press-and-hold duration"). Isolate it so it doesn't block everything else.

| Deliverable | Detail |
|---|---|
| Real-time input poll | Add hold-start/release timestamp capture to the interactive (non-headless) build path — desktop first, then web (touch hold already has some precedent per the existing virtual-gamepad work) |
| Power Bar runtime | Implement the cubic ease-in speed curve + 3-zone geometry from `FIGHT_SCENE_DESIGN.md` §2, driven by real input instead of synthetic release percentages |
| `data/equipment.json` | Weapon/Armor/Talent schema per `MAIN_BATTLE_SCENE_DESIGN.md` §4.4; wire into §6.2's effective-stat stack |
| Battle Scene screen assembly | Layout per `MAIN_BATTLE_SCENE_DESIGN.md` §2–3: persistent HUD + actor sprites (scene-graph, unchanged) + active Power Bar (scene-graph, custom-drawn per architecture-doc §2.3 Phase 5 — **not** ImGui, for frame-timing safety) + HUD numbers/flee button (ImGui) |

**Test additions:** `power_bar_test` (headless: feed synthetic hold durations, assert
position/power curve matches the formulas exactly — this replaces the current HTML-demo-only
verification with an actual regression test), `equipment_test` (stat + bar-param modifier stack).

**Depends on:** Milestones 1, 2. **Can run in parallel with Milestones 3–5** (different subsystem,
shares only the state machine and save schema).

---

## Milestone 7 — Balance & checklist closure

Nothing new gets built here — this is closing every open checkbox the design docs already carry,
now that the systems exist to actually verify them.

- [ ] Explicit EXP level-up thresholds, replacing `GAME_DESIGN_DOCUMENT.md` §7's proposed-but-
      unvalidated table — validate against real per-floor monster/item placements.
- [ ] Hand-trace every mandatory fight winnable with stats obtainable on that floor or before
      (`GAME_DESIGN_DOCUMENT.md` §11.1 / §17).
- [ ] Confirm floor-10 boss (HP 400/ATK 34/DEF 14) beatable under typical growth, with and without
      equipment (`MAIN_BATTLE_SCENE_DESIGN.md` §7).
- [ ] Confirm every bestiary entry hand-traced at P≈44% Power Bar release still matches
      pre-Power-Bar balance (`FIGHT_SCENE_DESIGN.md` §7 / `MAIN_BATTLE_SCENE_DESIGN.md` §7).
- [ ] Confirm no dialogue `next` chain (including the new `action.startMission`/`setStoryFlag`
      branches from Milestone 4) points at a non-existent node/mission.
- [ ] Decide and implement: does a re-cleared floor (now reachable via Stage Select) repopulate
      regular enemies, or stay permanently cleared? (Architecture-doc §5.2, §15 — this has real
      economy-balance consequences and should be settled before content authoring in Milestone 8.)
- [ ] Confirm shop cost curve still keeps gold a top-up, not a primary growth path
      (`GAME_DESIGN_DOCUMENT.md` §9), given equipment now adds a second gold sink/reward path.

**Depends on:** Milestones 4, 6 (needs missions + equipment in place to balance against).

---

## Milestone 8 — Content authoring pass

Now that every system exists, write the actual content that uses it:

- [ ] Tag each existing enemy/NPC placement in `data/stages/*.json` with its `encounterKind`
      (most stay `direct_battle`; pick 2–3 existing story-relevant NPCs/enemies to convert to
      `dialogue_gate` as the first real examples of "talk first, then choose").
- [ ] Author the first content-complete `data/missions.json`: at least one `side`, one `daily`,
      one `once` mission, each exercising a different Condition Evaluator leaf type.
- [ ] Author `data/equipment.json`'s starting set — the 9 example items already specified in
      `MAIN_BATTLE_SCENE_DESIGN.md` §4.3 are ready to transcribe directly.
- [ ] Fill in every stage's Stage Select `preview` text (recommended stats) from the existing
      bestiary table.

**Depends on:** Milestones 4, 5, 6, 7 (needs the systems *and* the balance numbers settled first).

---

## Milestone 9 — Platform verification & polish

- [ ] Full regression pass of every headless test on Windows + web (Emscripten) build targets.
- [ ] Manual playthrough, floor 1 → floor 11 epilogue, on desktop.
- [ ] Manual playthrough (or as much as touch input supports) on the WebGPU/WebGL browser build,
      including the new Stage Select hub and a `dialogue_gate` encounter.
- [ ] Audio pass per `GAME_DESIGN_DOCUMENT.md` §15 (lowest-priority production item, cut first
      under time pressure per that doc's own ordering).
- [ ] Confirm `BUILD_WINDOWS.md` / `BUILD_WEB.md` still describe the actual build steps after all
      the above (new source files, new data files, ImGui as a new external dependency).

**Depends on:** everything above.

---

## Summary table — milestone → architecture-doc systems delivered

| Milestone | Systems from architecture-doc §13 |
|---|---|
| M1 | #11 Game State Machine, #10 Event Bus, #26 Save/Persistence |
| M2 | #9 UI Framework Layer |
| M3 | #13 Entity Status, #24 Condition Evaluator, #22 Story System |
| M4 | #14 Encounter Resolution, #23 Mission System |
| M5 | #25 Stage Select, #28 Notification/Toast, UI migration for #18 Inventory / #20 Economy / #21 Dialogue |
| M6 | #19 Equipment System, #15/#16 Battle/Damage Formula (Power Bar completion) |
| M7 | (balance closure — no new systems) |
| M8 | (content — no new systems) |
| M9 | (verification — no new systems) |

Everything not listed above (#1–8: Renderer, Scene-Graph, Batch Renderer, Texture/Font, Audio,
Logging, Input; #12 Stage System; #17 Progression; #27 HUD) is already Established and untouched
by this roadmap except where a milestone explicitly says "retrofit."

---

## Suggested order if working solo, one track at a time

**M0 → M1 → M2 → M3 → M4 → M5 → M6 → M7 → M8 → M9**, exactly as numbered above — each milestone's
"Depends on" line was written to make this the critical path. The one legitimate parallelization
opportunity is **M2 and M6 alongside M3/M4**: UI bring-up and the Power Bar/Equipment track touch
almost entirely different code and can be built by a second contributor (or in a second work
session) without blocking the world-logic track.
