# Progress Report — Implementation Roadmap Tracking

> **Purpose:** the living status board for `IMPLEMENTATION_ROADMAP.md`. Read the **Next Step**
> line first when resuming work — it always says exactly what to do next. Below that is a status
> table for every milestone, then a dated log of what actually happened, then an **Open
> Questions / Blockers** section listing anything found wrong or any choice point that needs a
> decision from the project owner before work continues (per standing instruction: stop and ask
> rather than deciding unilaterally on anything surprising or irreversible).

---

## ▶ Next Step

**M7 is done, and one real post-M7 bug report from the owner is now fixed too (see the latest log
entry): winning a fight didn't let go of input focus, and Enter/mouse-click couldn't dismiss the
victory screen.** Three separate root causes, all fixed: `modalActive()` now also covers `cs.won`
(so the walking scene, Tab, B, and Escape all correctly stay inert during the victory pause),
Enter/Space now has a real dismiss path (`Game::dismissVictory()`, mirroring what mouse/touch
already had), and `showBattle`'s old "does the log contain 倒下" fallback check — which
accidentally matched the WIN message too, not just the lose message it was meant for — was
replaced with the actual, unambiguous `cs.resultPauseMs` signal.

**This absolutely needs the owner to check by playing** — win a fight, confirm arrow
keys/Tab/B/Escape do nothing until you dismiss it, then confirm both Enter and a mouse click
actually return you to the walking scene. An automated injected-input smoke test was attempted but
hit the same known "present loop doesn't reliably advance under this environment's automated
driving" limitation already logged back in M2/M5 — so this is verified by code trace + a clean
build + full regression only, not by an actual play-through, same as most combat-facing work this
session.

**Also still waiting on the owner to look at, from before this fix:** the Power Bar combat feel in
general (M6), the Stage Select/notification screens (M5), and M7's entity-status fix specifically —
walk into a monster or pick up an item, leave the floor (stairs or Tab → Stage Select), come back,
confirm it's still gone. Also still open: the F12/Steam-overlay test (add
`DISABLE_VK_LAYER_VALVE_steam_overlay_1=1` to Debugging → Environment in Visual Studio, F5, press
F12), and Milestone 5's deferred Inventory/Shop/Dialogue ImGui migration.

**Next milestone up (once the above is confirmed): M8 (Content authoring)** — every system built
in M3/M4/M6/M7 (Entity Status, Mission System, Equipment System, dialogue `action` verbs,
`dialogue_gate` encounters) is real, tested, and now live-wired, but has **no actual content**
using it yet (no `data/missions.json`, no `data/equipment.json`, no stage sets `dialogue_gate`).
That's what M8 is for.

**Everything currently in the repo builds clean and passes its full regression** (both `Debug`
and `Release`, 14/14 headless tests) as of the last commands run this session.

---

## Milestone status

| Milestone | Status | Notes |
|---|---|---|
| M0 — Baseline safety net | ✅ Done (with 1 known, accepted gap) | 4/5 headless tests build+pass+verified; `texture_test` left broken, by owner's choice — see log |
| M1 — Engine scaffolding | ✅ Done | Game State Machine, Event Bus, Meta/Run save schema — all built, wired minimally, and test-verified. See log. |
| M2 — UI framework bring-up | 🟡 Done except web backends (deferred, no Emscripten toolchain) | Dear ImGui wired into `tower_vulkan` as a dev-only F1 overlay + the M5-prerequisite styling spike (F2), both compiled + smoke-tested. **Visual correctness of the styling spike is still unconfirmed** — screenshot capture attempted and abandoned as unreliable in this environment; see log. |
| M3 — World logic core | ✅ Done | Condition/Flag Evaluator, Entity Status System, Story Controller — all built, test-verified, AND retrofitted into real gameplay (door/key gate, dialogue `requires` gate, beat advancement on floor entry). See log. |
| M4 — Encounter Resolution + Mission System | ✅ Done | `EncounterKind` resolution (opt-in `dialogue_gate`, `direct_battle` stays default for all shipped content per owner's decision), dialogue `action` verbs (`give`/`setStoryFlag`/`enterBattle`/`startMission` — first real implementation of dialogue actions at all), Mission System (definitions/trackers/daily-reset/event-driven progress) wired to the Event Bus. See log. |
| M5 — Stage Select + UI migration | 🟡 Half done | Stage Select hub (Tab to open) + Notification/toast system built, compiled, and smoke-tested (not yet owner-visually-confirmed). **Still not done:** migrating Inventory/Shop/Dialogue from hand-built `Node`-tree UI to ImGui — deferred as its own follow-up chunk since it replaces already-working features. See log. |
| M6 — Equipment + Power Bar | ✅ Done | Power Bar math + Equipment System built/tested, THEN wired into live combat: real-time hold-duration input, the full Attack→Defense round flow, damage resolution, and rendering all replace the old auto-attack loop. Compiled, regression-tested, and smoke-tested via injected real key-hold/release cycles (win and lose paths both exercised) — **not yet visually confirmed by the owner**. See log. |
| M7 — Balance & checklist closure | ✅ Done | Whole-tower balance simulation (full-item and zero-item runs) against real `data/*.json`; dialogue `next`-chain integrity check across all files; design-doc checklists closed with evidence; found and fixed a real gap (Entity Status System built in M3 but never wired into live gameplay — floors didn't actually stay cleared). See log. |
| M8 — Content authoring | ⬜ Not started | |
| M9 — Platform verification & polish | ⬜ Not started | |

Legend: ⬜ Not started · 🟡 In progress · 🟥 Blocked · ✅ Done

---

## Log

### 2026-09-07 — M0 started

- Built the four platform-independent headless tests in `Release` config via
  `cmake --build build --config Release --target node_test object_test log_test font_test
  texture_test`:
  - `node_test.exe` — **built successfully**
  - `object_test.exe` — **built successfully**
  - `log_test.exe` — **built successfully**
  - `font_test.exe` — **built successfully**
  - `texture_test.exe` — **build failed**, unrelated to any change made this session — see blocker
    below.
- Ran the four built executables directly:
  - `node_test.exe` → `node_test: ALL PASS (8 checks)`
  - `object_test.exe` → `object_test: ALL PASS (8 groups)` (includes an intentional leak-detection
    sample in its output — expected, part of the test, not a real leak)
  - `log_test.exe` → `log_test: ALL PASS`
  - `font_test.exe` → `font_test: ALL PASS`
- Spot-checked both HTML demos by extracting their embedded JS and executing the actual formula
  functions headlessly in Node (v24.2.0) rather than just reading the source:
  - `fight_scene_demo.html`: `computePower()` verified against all 5 zone boundaries (center→100,
    greenHalf edge→71, blueOuter edge→31, redOuter edge→0, both sides) — all matched
    `FIGHT_SCENE_DESIGN.md` §2 exactly. Attack/Defense damage formulas verified at P=0/44/97/100 —
    confirmed the doc's claim that P≈44% reproduces the pre-Power-Bar `base_hit` (8 dmg) almost
    exactly. Perfect (P≥97, ×1.25) and Perfect Guard (P≥99, 0 dmg) thresholds both fire correctly.
  - `main_battle_scene_demo.html`: confirmed its `WEAPONS`/`ARMORS` tables (wand/daggers/hammer,
    robe/plate/leather) match `MAIN_BATTLE_SCENE_DESIGN.md` §4.3's example-item table exactly
    (stat bonus, `v0/vmax/rampTime`, zone radii, `maxMult`), and its power/mitigation formulas are
    the same functions as the fight-scene demo (already verified above).
- **Result: M0 baseline confirmed sound.** Everything the roadmap depends on as "already working"
  actually is, except the one known gap below.

### 2026-09-07 — M1 done: Game State Machine, Event Bus, Meta/Run save schema

New files (all additive; nothing existing was rewritten, only two 1-line hooks added into
`game.cpp` — see below):
- `src/engine/game_state.h` — `GameState` enum (14 states) + a legal-transition table matching
  `GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md` §4.2's flow + `GameStateMachine` (current state +
  bounded history, `tryTransition()` rejects anything not in the table).
- `src/engine/event_bus.h` — a small type-indexed pub/sub `EventBus` (`subscribe<T>`/`publish<T>`),
  plus the two concrete payloads architecture-doc §8.4 named: `EnemyDefeated{enemyId,stageId}`,
  `ItemCollected{itemId,stageId}`. A process-wide `globalEventBus()` singleton, mirroring the
  existing `TextureManager` singleton convention.
- `src/game/save_system.h/.cpp` — `MetaSaveData` (permanent: story flags, unlocked stages, claimed
  one-time missions, permanent stat bonuses) and `RunSaveData` (current stage, player snapshot,
  per-tile entity status map), both with `schemaVersion`, JSON (de)serialization, a
  version-mismatch-detection flag (no migration logic yet — out of M1 scope), and
  `writeJsonAtomic`/`readJsonFileSafe` (temp-file+rename write; safe read matching
  `stage.h`'s `parseStage` convention). Deliberately has **zero dependency on `Game`/`Player`/
  `Stage`** so it stays buildable without Vulkan/GLFW — the Game-side bridging (actually reading/
  writing a save file from live gameplay) is deferred to Milestone 5, where a real "Continue"
  button first needs it.
- Three new headless tests, one per deliverable: `state_machine_test.cpp` (14/14 checks pass;
  asserts no dead-end states, all 14 states reachable from `Boot` via BFS, illegal transitions are
  rejected without mutating state/history), `event_bus_test.cpp` (multi-subscriber ordering, type
  isolation, independent-instance isolation), `save_test.cpp` (Meta + Run round-trip through JSON,
  an unrecognized `schemaVersion` is flagged, and an actual atomic-write-then-read round-trip
  through a real file on disk, including confirming the `.tmp` file doesn't linger).
- Minimal, additive wiring into the real game (`src/game/game.h`/`game.cpp`): `Game::currentState()`
  derives a `GameState` from the existing `cs.active`/`inDialogue`/`storeOpen` flags (pure
  read-only classification, changes no behavior); `EventBus::publish` calls added at the two real
  call sites the architecture doc named — the WIN branch of `resolveCombatRound()` publishes
  `EnemyDefeated`, and the item-pickup branch of `movePlayer()` publishes `ItemCollected`. Nothing
  subscribes yet (as specified) — these are inert until Milestone 4's Mission System listens.
- `CMakeLists.txt`: added the three test targets (`state_machine_test`, `event_bus_test`,
  `save_test`), following the existing target/include-dir pattern exactly.

**Found-and-fixed bug (in this session's own new code, not pre-existing):** the first draft of all
three new tests used `assert()` for checks. Since the only configured build (`vs2022-x64` /
Release) compiles with `NDEBUG` defined, `assert()` is a **no-op in Release** — the tests would
have silently reported "ALL PASS" regardless of whether any check actually failed. Caught this by
noticing an unexplained stderr line during a manual run, investigated, and confirmed by rewriting
all three tests to the project's own established `CHECK(cond, msg)` + `g_fail` counter pattern
(already used by `node_test.cpp`/`object_test.cpp` for exactly this reason) instead of `assert()`.
Re-ran after the fix — all three still pass, this time genuinely verified. **Lesson for future
milestones: never use bare `assert()` in a new `*_test.cpp` here — always use the `CHECK` macro
pattern.**

**Full regression after M1:** all 7 headless tests (`state_machine_test`, `event_bus_test`,
`save_test`, `node_test`, `object_test`, `log_test`, `font_test`) pass; `tower_vulkan` itself still
builds clean with the two additive hooks in place.

**Deferred, not forgotten:** Game-side save/load wiring (an actual "write the Meta/Run save to
disk" call from live gameplay) — intentionally left for Milestone 5, per the roadmap.

### 2026-09-07 — M2 done for desktop (Vulkan); web backends explicitly deferred

**Scope decision (owner, this session):** M2 asks for Dear ImGui wired into all 3 renderer
backends (Vulkan desktop, WebGL2, WebGPU). This environment has no Emscripten toolchain
(`emcc`/`$EMSDK` both absent), so the web backends cannot be built or tested here at all. Owner
chose **"Desktop (Vulkan) only for now"** — the WebGL2/WebGPU ImGui backend wiring is not started,
not attempted, and not guessed at; it's a clean follow-up for whenever the Emscripten SDK is
available in a session (either install it here, or hand this off to a session that already has
it). Nothing in this milestone's work blocks that follow-up — it's purely additive on top of what
exists.

**What was built (Vulkan desktop only):**
- Vendored Dear ImGui via `FetchContent`, pinned to **v1.90.9** (mirrors the existing GLFW
  `FetchContent` precedent in `CMakeLists.txt`). Chose this tag specifically to land on a
  well-understood, stable Vulkan-backend API surface rather than the newest tag.
- `src/engine/imgui_layer.h/.cpp` — a small `ImGuiLayer` class wrapping the GLFW+Vulkan backends:
  `init()` (creates its own dedicated `VkDescriptorPool` so it never competes with the sprite/text
  renderer's own `dsPool` for capacity), `shutdown()`, `newFrame()`/`endFrame()`
  (`ImGui::NewFrame()`/`ImGui::Render()`), `renderDrawData(cmd)` (`ImGui_ImplVulkan_RenderDrawData`,
  called from inside an already-active render pass).
- `src/engine/renderer.h`/`.cpp`: added one hook point, `Renderer::uiOverlayHook` (a
  `std::function<void(VkCommandBuffer)>`), invoked once — right before `vkCmdEndRenderPass` in
  the normal (non-split-screen) path of `Renderer::end()` — so `Renderer` never needs to know
  ImGui exists; `main.cpp` wires `imguiLayer.renderDrawData` into it.
- `src/game/game.h`/`.cpp`: `Game::drawDebugOverlay()` (guarded `#ifndef __EMSCRIPTEN__`, exactly
  like the existing `Audio`/`AudioStub` split in this same file) — an ImGui window showing the
  derived `GameState`, current stage, player stat sliders (HP/MaxHP/ATK/DEF/Level/EXP/Gold — for
  hand-tracing bestiary fights live, per the roadmap), a node-filter combo wired to the existing
  `IRenderer::setNodeFilter` diagnostic, and the last combat log line. A full `log.h`-backed
  scrolling log viewer was scoped out for time — this shows the last combat-log line only, which
  is the more immediately useful half for balance work; noted as a small future addition, not a
  gap in the milestone's actual ask (dev-only debug overlay).
- `src/game/main.cpp`: constructs `toms::ImGuiLayer`, initializes it once the Vulkan renderer is
  ready, adds an **F1** toggle for `showDebugOverlay` (default **off**), calls
  `newFrame()`/`drawDebugOverlay()`/`endFrame()` each loop iteration before `g.draw()`, and
  `imguiLayer.shutdown()` before the renderer is torn down.
- `CMakeLists.txt`: `tower_vulkan` now also compiles ImGui's core (`imgui.cpp`, `imgui_draw.cpp`,
  `imgui_tables.cpp`, `imgui_widgets.cpp`) and its GLFW+Vulkan backend sources, plus the two new
  `imgui_layer` files.

**Found-and-fixed API mismatches (against my own first draft, caught by actually reading the
vendored header instead of trusting memory further):** my first draft assumed the *older* classic
ImGui Vulkan-backend shape (`ImGui_ImplVulkan_Init(&info, renderPass)` as two arguments, and a
manual `ImGui_ImplVulkan_CreateFontsTexture(commandBuffer)` + `DestroyFontUploadObjects()` pair for
the one-time font upload). The actual v1.90.9 header already has `RenderPass` as an
`ImGui_ImplVulkan_InitInfo` struct field (single-argument `Init`), and
`ImGui_ImplVulkan_CreateFontsTexture()` takes **no arguments** — the backend manages its own
internal command pool/queue submission for the font upload now, and there's no separate
"destroy upload objects" call (the corresponding cleanup happens automatically inside
`ImGui_ImplVulkan_Shutdown()`). Caught this from the first build's compiler errors, read
`imgui_impl_vulkan.h`/`.cpp` directly to get the real signatures rather than guessing again, fixed
`imgui_layer.cpp` and the `main.cpp` call site (dropped an now-unnecessary `cmdPool` parameter),
and rebuilt clean.

**Verification performed (compile + smoke-test; no visual/screenshot confirmation — see caveat
below):**
- `tower_vulkan` builds clean with the full ImGui integration linked in.
- Ran the real windowed executable for a sustained 5-second window with the overlay left at its
  real default (**off**) — process stayed alive the whole time (no crash, no early exit), no
  `[imgui]`-prefixed error lines in stdout/stderr.
- Ran it again for another 5 seconds with `showDebugOverlay` temporarily forced to `true` at the
  source level (reverted immediately after) — this actually exercises every `ImGui::SliderInt`/
  `ImGui::Combo`/`ImGui::Text` call in `drawDebugOverlay()` every frame; again, no crash, no
  `[imgui]` error lines, sustained the full 5 seconds.
- Full regression: all 7 headless tests still pass after these changes.

**Known verification gap (be precise about this, don't oversell it):** this environment cannot
produce a screenshot of the windowed Vulkan renderer (`Renderer::savePNG` is explicitly a no-op
in windowed/swapchain mode — see the code comment at `src/engine/renderer.cpp` around
`savePNG`), and there is no way to send a real F1 keypress to the live window from here. So:
**"the overlay compiles, initializes without error, and runs for 5 sustained seconds with its
exact widget-drawing code path exercised" is confirmed; "it visually looks correct on screen" is
not** — that needs a human (or a future session with real display/screenshot access) to actually
look at it once. Flagging this precisely rather than claiming full visual verification.

**Scope gap (M2), called out rather than silently skipped:** the roadmap's M2 also lists a "styling
spike" — one throwaway panel with `ImGuiWindowFlags_NoDecoration` + a transparent background +
a scene-graph-drawn 9-slice behind it, specifically to de-risk the hybrid rendering approach
(architecture-doc §2.3) *before* Milestone 5 commits real screens to it. That spike was not done
this session — the debug overlay built here uses ImGui's normal decorated/opaque window style,
which does not yet prove the transparent-window-over-9-slice technique works. This isn't blocking
anything today (M3/M4 don't need it), but **Milestone 5 should not start** until this spike is
actually done and checked — added as an explicit prerequisite note for whoever picks up M5.

### 2026-09-07 — M3 done: Condition/Flag Evaluator, Entity Status System, Story Controller

**What was built (all three, standalone and headlessly tested first, then wired into real
gameplay):**
- `src/engine/condition.h/.cpp` — the shared Condition/Flag Evaluator: `ConditionContext`
  (abstract interface: `storyFlagSet`/`storyBeat`/`itemHeld`/`statValue`/`missionComplete`/
  `missionActive`/`stageCleared`) and `evaluate(json, ctx)`, recursively handling `all`/`any`/
  `not` combinators and 7 leaf types over plain JSON (no custom AST, matching this project's
  existing "simple explicit JSON, not embedded logic" convention). Null/absent condition = always
  true (no requirement); anything malformed or an unrecognized leaf type fails **closed** (denies
  access rather than silently granting it).
- `src/engine/entity_status.h/.cpp` — `EntityStatus` enum (`Untouched/Engaged/Defeated/Collected/
  Opened/Hidden`) + `entityStatusKey(stageId,x,y)` ("`stageId|x,y`", matching the format
  `RunSaveData::entityStatus` already used since Milestone 1) + get/set helpers over that same
  `map<string,string>` shape, so no new save-schema field was needed — this reuses M1's map
  directly rather than inventing a parallel one.
- `src/game/story_controller.h` — `setStoryFlag`/`hasStoryFlag`/`advanceStoryBeat`, operating
  **directly on `MetaSaveData`** (added one field, `currentBeat`, to the struct M1 already built)
  rather than a new parallel "StoryController" struct — one source of truth, not two.
  `advanceStoryBeat` is monotonic (never regresses, e.g. if a later Stage Select hub lets the
  player revisit an earlier floor).
- Three new headless tests, one per module, 51 checks total, all genuinely passing (using the
  `CHECK`-macro convention from Milestone 1's lesson, not `assert()`):
  `condition_eval_test.cpp` (24 — every leaf type, `all`/`any`/`not`, nesting, fail-closed
  behavior on malformed/unknown input), `entity_status_test.cpp` (18 — key format, round-trips
  for every status value, independent tiles/stages, safe fallback on garbage input),
  `story_controller_test.cpp` (9 — monotonic beat advance, flag set/has, no duplicate flags).
  `save_test.cpp` also extended (+1 check) to cover the new `currentBeat` field's round-trip.

**The retrofit (the part of M3 that touches live, un-automated-tested gameplay code — done
carefully, verified by re-reading the diff line-by-line plus a runtime smoke test, not just
"it compiled"):**
- `Game` gained one new private member, `toms::MetaSaveData meta_` (in-memory only this
  milestone — an actual save file on disk is still Milestone 5's job, exactly as M1's log already
  flagged) and a file-local `GameConditionContext` (in `game.cpp`, adapts live `Player`+
  `MetaSaveData` to `ConditionContext` — built from already-public `Player` fields, so it needed
  no new friendship or public API surface on `Game`).
- **Door/key gate retrofit** (`Game::movePlayer`): replaced the three inline
  `if (c=='y' && pl.key_yellow<=0) return;`-style checks with one `toms::evaluate({"type":
  "itemHeld", "itemId": doorColorKey, "count":1}, ...)` call. Deliberately verified this is
  **behaviorally identical**, not just "compiles": same door characters gate the same way, every
  other character still skips the whole block untouched, the key-decrement lines after it are
  completely unchanged.
- **Dialogue `requires` gate** (`Game::enterNode`): this was actually a **net-new feature**, not
  a retrofit of existing behavior — checked first and confirmed **zero** shipped
  `data/dialogue/*.json` file currently sets a `requires` field on any choice, so
  `c.contains("requires")` is false for 100% of existing content today. This means the change is
  provably a no-op for everything that ships right now, while making the capability available
  the moment new content wants to use it (exactly the "one shared engine, reused everywhere"
  outcome M3 was asking for, achieved here without ever having a second, rival implementation to
  reconcile).
- **Story beat advancement** (`Game::loadStage`): one added line,
  `toms::advanceStoryBeat(meta_, st.index)`, right after the new stage parses — matches the
  already-documented rule (`GAME_DESIGN_DOCUMENT.md` §5: floors map 1:1 to story beats) that today
  only existed implicitly in "which floor the player is standing on."
- `CMakeLists.txt`: added `save_system.cpp`/`condition.cpp`/`entity_status.cpp` to **both**
  `tower_vulkan` and `toms_web`'s source lists (not just the tests) — `game.cpp` now calls into
  all three unconditionally (no `__EMSCRIPTEN__` guard, since none of this is Vulkan-specific),
  so the web target's CMake config needed the same sources or a future `-DWEB=ON` build would
  fail to link. This wasn't build-tested (still no Emscripten toolchain here — same caveat as
  M2), but it's a small, structurally-necessary, low-risk mirror of what `tower_vulkan` already
  needed.

**Verification performed:**
- Full regression: all **10** headless tests pass (`condition_eval_test`, `entity_status_test`,
  `story_controller_test`, `save_test`, `state_machine_test`, `event_bus_test`, `node_test`,
  `object_test`, `log_test`, `font_test`).
- `tower_vulkan` builds clean with the retrofit compiled in, including `game.cpp`'s changes to
  `movePlayer`/`enterNode`/`loadStage`.
- Ran the real executable for a sustained 5-second window — no crash, no error/exception/abort
  lines, `stage01` loads successfully (which exercises `loadStage`'s new `advanceStoryBeat` call
  on the very first frame).

**Known verification gap, stated precisely:** the door-gate and dialogue-`requires` code paths
inside `movePlayer`/`enterNode` are **not exercised by the smoke test above** — that test never
sends movement/interaction input (no way to send real keystrokes to the live window from this
environment, same limitation noted in M2). Confidence in their correctness rests on (a) the
behavioral-identity argument for the door retrofit above, (b) the code being read back
line-by-line after writing it, and (c) `condition_eval_test`'s 24 checks proving the underlying
`evaluate()` engine itself is correct — but **not** on an automated or manual play-through
actually walking into a locked door or a gated dialogue choice. Worth a real play-test pass once
someone can interact with the window directly.

### 2026-09-07 — M4 done: Encounter Resolution System + Mission System

**Choice point raised and resolved before writing code:** found that `data/dialogue/enemy_*.json`
flavor-line files already exist for every monster but are completely unused by `movePlayer`
(every monster tile goes straight to combat today) — building `dialogue_gate` support meant
deciding whether to make it the default for all monsters (finally using that dormant data, but
changing the feel of every fight in the shipped game) or keep it opt-in. **Owner chose: keep
`direct_battle` as the default** — `dialogue_gate` is fully built and tested but not applied to
any existing content this session.

**What was built:**
- `src/engine/encounter.h/.cpp` — `EncounterKind` (`DirectBattle`/`DialogueGate`/`StoryTrigger`/
  `Merchant`) + `resolveEncounterKind(entityKind, overrideStr)`: a recognized override always
  wins, otherwise falls back to a kind-derived default (`monster:` → DirectBattle, `npc:` →
  StoryTrigger — both match today's actual behavior exactly). An *unrecognized* override string
  falls through to the kind-derived default rather than silently becoming DirectBattle regardless
  of entity type. 20 tests passing.
- `src/game/stage.h`: `Entity` gained one new optional field, `encounterOverride`, parsed from an
  optional top-level stage-JSON `"encounter_overrides"` map (keyed by tile char) — guarded so
  every existing stage file (none of which set this) parses identically to before.
- `src/game/mission_system.h/.cpp` — `MissionDefinition`/`MissionTracker`/`MissionObjective`,
  JSON (de)serialization, `rollDailyReset` (daily missions reset on a new date, side/once
  missions never touched, idempotent within the same day), `applyProgressEvent` (only an
  `Active` mission gains progress, only on a matching objective, capped at the target count,
  flips to `Completed` on reaching it). Deliberately content-free — no `data/missions.json` yet,
  that's Milestone 8's job; this is pure, fully-tested schema + logic. 28 tests passing.
- **Found a second dormant, documented-but-unimplemented feature while wiring this in**: dialogue
  choice `action` handling (the `give`/door/flag verbs `FIGHTING_TALKING_DESIGN.md` §2.1 already
  describes as "established") turned out to not exist in code at all — `dlgChoices` only ever
  stored `{label, next}`, silently discarding any `action` field. Since this was pure
  infrastructure gap (nothing depends on `action` being ignored — no dialogue file's behavior
  changes by finally reading a field it already had), implemented it properly rather than
  papering over it: `DialogueChoice` now carries `action` alongside `label`/`next`, and
  `Game::runDialogueAction` dispatches four verbs as a small explicit switch (matching this
  project's no-scripting-hook convention): `give` (applies an item immediately via the existing
  `applyItem`), `setStoryFlag`, `enterBattle` (resolves a pending `dialogue_gate` encounter),
  `startMission` (activates a live `MissionTracker` by id, even with no `MissionDefinition`
  content loaded yet — tracker *state* is real and forward-compatible).
- `Game` gained: `pendingEncounterEnemy_`/`hasPendingEncounter_` (so `action.enterBattle` knows
  what to fight), `missionDefs_`/`missionTrackers_` (empty defs map until Milestone 8, but a real,
  live tracker map), `wireMissionEvents()` (subscribes `EnemyDefeated`/`ItemCollected` handlers to
  the global EventBus **once**, at the end of `loadAssets` — inert today since `missionDefs_` is
  empty, becomes live the instant content defines a matching objective, with zero coupling back
  into Battle/Item code).
- `GameConditionContext::missionActive`/`missionComplete` **upgraded from the Milestone 3 stubs**
  (which always returned `false`) to real lookups against the live tracker map — closes a gap
  explicitly flagged as future work in M3's log entry.
- `event_bus.h` gained a third event, `ChoiceMade{npcId, choiceLabel}`, published from
  `chooseDialogue` on every confirmed choice.
- `movePlayer`'s monster branch now calls `resolveEncounterKind` before deciding
  combat-vs-dialogue; since every shipped stage resolves to `DirectBattle`, this is provably a
  no-op for all current content, exactly like M3's door-gate retrofit.
- `CMakeLists.txt`: `encounter.cpp`/`mission_system.cpp` added to `tower_vulkan` **and**
  `toms_web` sources (same reasoning as M3's `save_system.cpp`/`condition.cpp` — `game.cpp` calls
  into them unconditionally now), plus the two new test targets.

**Found-and-fixed bug in this session's own new code (caught on a deliberate re-read, not by a
test — flagging exactly how it was caught):** the new `dialogue_gate` branch in `movePlayer`
initially called `startDialogue("enemy_" + e.id)` **without** first setting `dlgNpc` the way the
existing NPC branch always does. Since `chooseDialogue` publishes `ChoiceMade{dlgNpc, ...}`,
this would have carried a stale/wrong `npcId` for any future `dialogue_gate` encounter — not a
crash (nothing consumes `ChoiceMade` yet), but a real correctness bug waiting to surface the
moment something does. Fixed by setting `dlgNpc = "enemy_" + e.id;` before the `startDialogue`
call, matching the existing NPC branch's own convention. Re-verified with a full rebuild + smoke
test after the fix.

**Verification performed:**
- Full regression: all **12** headless tests pass (`encounter_resolve_test`, `mission_test`, plus
  the 10 from M0–M3).
- `tower_vulkan` builds clean with the full retrofit compiled in.
- Ran the real executable for a sustained 5-second window after the `dlgNpc` fix — no crash, no
  error/exception lines, `stage01` loads successfully.

**Known verification gap, same honesty standard as M2/M3:** the `dialogue_gate` branch, the four
dialogue `action` verbs, and mission progress-tracking are **not exercised by the smoke test**
above — none of them can be, since no shipped content uses `dialogue_gate` or sets any `action`,
and there's still no way to send real input to the live window from this environment. Confidence
rests on: the underlying `encounter_resolve_test`/`mission_test` unit coverage (48 checks between
them), the "provably a no-op for existing content" argument for the retrofit parts, and a careful
line-by-line re-read of the new code (which is exactly how the `dlgNpc` bug above was caught).
Real play-testing of `dialogue_gate` content specifically has to wait until Milestone 8 actually
authors a stage that uses it.

### 2026-09-07 — M2 styling spike built (visual confirmation still outstanding)

Closing the prerequisite M2 left open: a throwaway panel proving a transparent, undecorated
ImGui window laid exactly over a scene-graph-drawn backdrop reads as one seamless panel rather
than two overlapping things — the assumption Milestone 5's whole hybrid UI approach rests on.

**What was built:**
- `Game::drawStylingSpike()` (new, `#ifndef __EMSCRIPTEN__`-guarded): opens an ImGui window with
  `ImGuiWindowFlags_NoTitleBar | NoResize | NoMove | NoScrollbar | NoBackground | NoCollapse` +
  `SetNextWindowBgAlpha(0.0f)`, positioned/sized via `SetNextWindowPos`/`SetNextWindowSize` at a
  rect centered in the window, containing real text + a working `ImGui::Button` (so the spike
  proves interaction works through the arrangement, not just that it draws).
- `Game::drawStylingSpikeBackdrop()` (new, deliberately **unguarded** so it compiles into both
  the desktop and web builds — see the comment at its declaration in `game.h` for why): draws two
  solid-tint quads (an outer "border" + an inset "fill") at the exact same rect via the existing
  `Quad{solid=true}` + `ren->drawSprite()` primitive already used by `drawBar`/the dialogue box
  background — a cheap two-rect stand-in for real 9-slice art, since the point of this spike is
  the *coordinate-alignment and transparency technique*, not authoring actual art assets.
- Wired into `main.cpp` behind a new **F2** toggle, same pattern as F1's debug overlay.
- The backdrop draw call had to be inserted into **5** separate early-return branches inside
  `Game::draw()` (combat/battle/dialogue/inventory/store all `return` after their own
  `ren->end()`) — done as one `replace_all` edit on the exact `ren->end();` string (confirmed via
  grep there were exactly 5 occurrences, all inside `draw()`, before doing it), then a few
  small indentation cleanups.

**A real state bug, caught before it ever ran (not by luck — by tracing the frame sequence on
paper before building):** the ImGui-content half (`drawStylingSpike()`) and the backdrop half
(`drawStylingSpikeBackdrop()`) run at two different points in the frame (the former before
`g.draw()`, from `main.cpp`; the latter inside `g.draw()`) and only communicate via a
`stylingSpikeVisible_` bool. The first draft only ever set that bool to `true` (inside
`drawStylingSpike()`) and never back to `false` — meaning once F2 was pressed once, the backdrop
would stay visible forever, surviving even after pressing F2 again. Fixed by having `main.cpp`
call `g.setStylingSpikeVisible(false)` unconditionally every frame, immediately before the
conditional `if (showStylingSpike) g.drawStylingSpike()` call that flips it back to `true` when
the toggle is actually on.

**The screenshot detour (attempted, then deliberately abandoned):** to finally close the
"visually unconfirmed" gap this progress report has carried since M2, I tried to get an actual
screenshot of the running game window using Windows GDI (`Add-Type` + `System.Drawing` +
`CopyFromScreen`) via PowerShell, entirely outside the renderer's own `savePNG` (which is a
no-op in windowed mode, as noted in M2's log). This **did not work reliably in this environment**:
- The first attempt used `Process.MainWindowHandle`, which turned out to point at a console/
  terminal window, not the actual GLFW-created window — the captured image showed console log
  text, not the game.
- Enumerating windows properly (`EnumWindows` + `GetClassName`) did find the real window
  (class `GLFW30`, title "Tower of the Sorcerer") — but a `CopyFromScreen` capture of *that*
  window's rect returned content from a **completely different application** (a VS Code git
  commit dialog, including incidentally the machine's configured git author email). This means
  GDI's screen-capture in this environment is not reliably reading from whatever the GLFW window
  actually displays — likely a remote-session/virtual-display quirk, not a bug in the game.
- **Both screenshot files were deleted immediately** once the second one turned out to contain
  unrelated, incidentally-sensitive desktop content (a git author email) that had nothing to do
  with this task — capturing arbitrary desktop content is out of scope and the files served no
  further purpose once identified as capturing the wrong thing.
- **Verdict: abandoned this approach rather than keep trying.** It's not a reliable path to
  visual verification here, and continuing to poke at screen capture risks grabbing more
  unrelated content. Reverted the temporary "force the spike on" test flag back to its real
  default (`false`, F2-toggled) once the process-stability part (no crash, GLFW window creates
  successfully) was confirmed via the process running through two full screenshot-attempt cycles
  without erroring.

**Verification performed:**
- Full regression: all 12 headless tests still pass, unchanged by this work (it's pure new
  rendering code, no existing logic touched).
- `tower_vulkan` builds clean with the spike compiled in.
- Confirmed via process enumeration that the real GLFW window (`class=GLFW30`,
  `title="Tower of the Sorcerer"`) is created successfully and the process survives multiple
  seconds of runtime with the spike's code path actively running (both the ImGui content and the
  backdrop-drawing were exercised — `showStylingSpike` was temporarily forced `true` during the
  screenshot attempts) — no crash, no `[imgui]`/error/exception output in any of these runs.

**What's still actually unconfirmed, stated precisely one more time:** whether the transparent
ImGui window and the scene-graph backdrop **visually align and read as one panel** — the entire
point of the spike — has **not** been checked by anything with eyes. Code review says the
coordinates are computed from the same `stylingSpikeRect_` values written once per visible frame
and read by both halves, so they *should* match, but a coordinate-space mismatch (e.g. HiDPI
`DisplayFramebufferScale` vs. raw framebuffer pixels — a real, known risk category for exactly
this kind of ImGui/custom-renderer coexistence) would only show up visually, not in a compile or
a crash-free run. **Whoever has an actual working screenshot/display pipeline for this game
should press F2 and look, before Milestone 5 commits real screens to this technique.**

### 2026-09-07 — Screenshot verification follow-up: found a deeper cause, not just a wrong window

Owner asked to pause M5 and fix screenshot verification before doing more UI work. Made real
progress diagnosing *why* it wasn't working, but did not end up with a usable screenshot pipeline
— the root cause looks like it's the Vulkan render loop, not the screenshot method.

**Fixed the "wrong window" problem:** switched from `CopyFromScreen` (samples whatever's on
screen at a rect — proved unreliable, see the M2 entry above) to `PrintWindow` with
`PW_RENDERFULLCONTENT` (asks the window to render its own content directly via DWM, regardless
of focus/occlusion). This reliably captured the *correct* window (title bar "Tower of the
Sorcerer" visible, correct dark clear-color background matching `renderer.cpp`'s clear value) —
a real improvement, no more capturing unrelated applications.

**But the captured frame never changes.** Three captures at increasing delays (2.5s, 5s, 4s)
returned **pixel-identical** images: the initial clear-color background plus one small
light-blue-gray rectangle in the bottom-right corner, and nothing else — no map tiles, no HUD
text, no player sprite. Consistent, reproducible, not flaky.

**Also found and fixed a real focus bug along the way:** `SetForegroundWindow` was silently
failing every time (Windows blocks it when called from a non-focused background process — a
well-known OS restriction, not a bug in this project). Worked around it with the standard
"prime the input state with a harmless simulated keypress first" trick, then verified via
`GetForegroundWindow()` that the game window genuinely became the true OS-level foreground
window. Re-captured with `CopyFromScreen` once actually focused — the wrong-application-content
problem was gone (no more VS Code), but the rendered content was still just that same static
initial frame.

**Working theory (not yet confirmed): the Vulkan present loop may never actually be completing
a draw in this environment.** `Renderer::beginFrame()` can return `false` and skip the frame
(window resize/out-of-date/minimized) — if something about how the window is created/hosted here
makes it look permanently "out of date" for swapchain purposes, `g.draw()` would simply never
run past the very first frame, forever, with **no crash and no error output**, exactly matching
everything observed: the process stays alive indefinitely, logs nothing further, and the display
never advances. This would explain every result across all of today's and the previous session's
smoke tests — none of them actually proves gameplay rendering works past frame 1, only that the
process doesn't crash.

**Cleaned up:** all temporary screenshot/log files from this diagnostic pass were deleted —
nothing kept beyond what's written here.

**This is now flagged as an open question for the owner rather than something to keep
investigating solo**, since diagnosing/fixing the Vulkan swapchain loop itself is a materially
different task than "take a screenshot," and better scoped with the owner's input on how to
proceed (see the conversation for the actual question asked).

### 2026-09-07 — Real bug found and fixed: a global vertical-flip in the Vulkan desktop shader

Owner reported (from their own direct observation of the running game — they can see it even
though this session still cannot) that the "道具商店已經開放" (store unlocked) dialog's layout
order looked reversed: the button appearing above the text instead of below it.

**Root cause, found by derivation rather than guesswork:** `assets/shaders/sprite.vert` (the
*only* vertex shader the desktop Vulkan renderer uses, for every sprite, HUD element, and glyph)
computed `ndc.y = 1.0 - (y/res.y*2.0)`. Worked through the Vulkan viewport-transform math from
the spec by hand: with a standard (non-negative-height) viewport — confirmed via
`src/engine/renderer.cpp`, all three `VkViewport` setups use `vp.y=0`/positive `vp.height`, no
`VK_KHR_maintenance1` flip trick anywhere — Vulkan's NDC is **Y-down** (opposite of OpenGL/WebGL,
which this formula's sign actually matches). The net effect: pixel `y=0` (intended screen top)
was landing at the window's bottom, and `y=res.y` (intended bottom) at the top — a full vertical
flip of *everything* the desktop build draws, not just this one dialog.

**Why this was never caught until now:** this exact formula has been in the shader since the
project's very first commit (verified via `git show` at each historical revision) — this is not
something introduced this session. The project's "verified rendering"/"vision-confirmed" history
(`WORKING_LOG.md`, README commits) appears to have always been about the **browser/WebGL build**
(`renderer_webgl.cpp`, a separate shader with the OpenGL-convention-correct sign) — the desktop
Vulkan interactive build (`tower_vulkan.exe`) most likely has never been carefully visually
inspected by a human before the owner did so just now. This session's own repeated smoke tests
("no crash for 5 seconds") never caught it either, because a flipped image still runs without
crashing or logging any error — only a human looking at the actual window would ever notice.

**Fix:** one line, `ndc.y = y / pc.res.y * 2.0 - 1.0` (removes the OpenGL-convention negation),
with a comment explaining why, so nobody re-introduces this by "simplifying" it back. Recompiled
to SPIR-V via `glslangValidator` (the .spv is what actually ships — CMake only auto-compiles it
from source when the .spv file is *missing*, so this had to be done manually), rebuilt
`tower_vulkan`, confirmed the freshly-compiled `.spv` was copied next to the executable (newer
timestamp than the source compile).

**Verification performed:** full 12-test regression still passes (untouched by this — it's a
shader/rendering-only change), `tower_vulkan` builds clean, and a 5-second smoke test shows no
crash and no Vulkan validation-layer complaints. **This is a global fix affecting every screen
the desktop build draws, not just the store dialog** — flagged to the owner to re-check broadly
(not only the one dialog they originally reported) since this session still cannot visually
confirm it either way.

### 2026-09-07 — Owner sent a real screenshot; found the build was stale, plus two more real bugs

The owner shared an actual screenshot of the store-unlock dialog (garbled/overlapping text,
button in the wrong position, and — critically — no response to mouse or keyboard clicks on the
confirm button) taken by running `main.cpp` directly from VS Code.

**First finding: wrong build entirely.** `Debug/tower_vulkan.exe` and `Debug/sprite.vert.spv`
were both dated **Sep 4** — three days before this session started, predating every fix made
today (the shader fix included). This session had only ever built `--config Release`; VS Code's
"run main.cpp" launch almost certainly defaults to the Debug configuration, which nobody had
rebuilt. The screenshot was very likely showing the *original*, pre-session state, not today's
work at all.

**Second finding, while getting Debug rebuilt: an unrelated CMake/toolchain break appeared
mid-session.** Both `--config Debug` and (re-tested to confirm) `--config Release` started
failing with a Visual Studio instance-resolution error. Diagnosed via `vswhere -all`: a **second
Visual Studio install, "Visual Studio Community 2026" (internal version 18.9), had been added**
alongside the original, still-fully-intact VS 2022 (17.14) — not something this session did.
CMake's cached generator instance became ambiguous between the two. Fixed by reconfiguring with
an explicit `-DCMAKE_GENERATOR_INSTANCE` pointing at the original 2022 install (a safe,
non-destructive metadata regeneration — no source or project files touched, and the fetched
GLFW/ImGui checkouts under `build/_deps` were preserved). Both configs build clean again after
this.

**Third finding: a real, separate input bug — `keyPressed()` is a stateful edge-trigger that
breaks when queried twice for the same key in one frame.** `main.cpp`'s `keyPressed(key)` helper
mutates its own debounce record as a side effect on *every* call, not just when it returns true.
Several keys were being queried more than once per frame across different `if` blocks (movement
keys for both `movePlayer` and the inventory cursor; Enter for interact/dialogue, inventory-use,
*and* the store confirm; Escape for both quitting and the store's own close-handler) — every
check after the first for the same physical key silently read as "not pressed" that frame, even
though the key really was down. This exactly explains "keyboard click has no response": Enter
was always consumed by the generic interact/dialogue check (line 118, ahead of the store block at
line 137) before the store-unlock dialog's own Enter-handler ever ran. It also meant **Escape
always quit the entire game outright**, even with a dialog open, instead of closing it — worse
than just "unresponsive." Fixed by querying each physical key exactly once per frame into a
local variable, with every block referencing that cached value instead of re-calling
`keyPressed()`; also gave Escape real modal-aware behavior (closes the store, then inventory,
before falling through to quit only when nothing is open).

**Fourth finding: desktop mouse clicks were never wired to the store/dialogue/inventory UI at
all.** `Game::handleTouch()`/`storeClick()` (which already correctly handle all the store-unlock
dialog's hit-testing, in a documented 1024x768 buffer-space contract) were only ever invoked from
the web/touch input path (`emscripten_main.cpp`) — `main.cpp`'s desktop loop had no
`glfwGetMouseButton`/cursor-position handling calling into them at all. Added a debounced
left-click forward, converting the cursor position from framebuffer pixels into the same
1024x768 buffer space `handleTouch` expects.

**Found-and-fixed bug in this fix itself, caught immediately by the compiler on rebuild:** the
first draft declared `static bool mouseWasDown` *inside* the `if (mouse button down)` branch,
making it invisible to the paired `else` branch that also needed it (a real scoping mistake, not
a logic error) — build failed with `'mouseWasDown': undeclared identifier`. Fixed by hoisting the
`static` declaration above the `if`/`else`, rebuilt clean.

**Verification performed:** full 12-test regression passes on the rebuilt Release binaries; both
`Debug` and `Release` configurations now build clean after the CMake generator fix; a 5-second
smoke test of the freshly-built **Debug** executable (the one actually being launched) shows no
crash and no error output; confirmed `Debug/sprite.vert.spv`'s timestamp is now current.

**Still unconfirmed by this session** (same limitation as ever): whether the store-unlock
dialog's layout now reads correctly top-to-bottom, and whether Enter/mouse-click now actually
confirm it. That needs the owner to relaunch and look, same as before — but this time against a
build that actually contains all of today's fixes.

### 2026-09-07 — Owner confirmed the shader fix worked; new ask: aspect-correct viewport on resize

Owner sent two real screenshots of the freshly-rebuilt game — **the vertical-flip fix worked**:
HUD at top, map in the middle, dialogue text at the bottom, all reading correctly top-to-bottom.
First real visual confirmation this session has gotten of anything in the live window.

**New request, with a reference implementation supplied** (`cOpenGLRender::
SetAcceptRationWithGameresolution`, from the owner's own FM79979-heritage codebase this project
already ports patterns from): when the window resizes, the game's internal resolution should
stay fixed, and only the *viewport* should scale/letterbox to fit the new window size while
preserving aspect ratio — not reflow/rescale the actual game content.

**What was actually happening before this fix:** `Game::draw()` already draws everything at
fixed absolute pixel sizes (`ts = 48.0f` tile size, hardcoded font sizes like 16/18/22/24) — it
was never literally "stretching" content. But `Renderer::width()/height()` (which all of
`game.cpp`'s layout math reads) returned the **live, actual window size**, and the Vulkan
viewport always spanned the full window with **no aspect-ratio handling at all** beyond a
`glfwSetWindowAspectRatio(window, 16, 9)` lock on dragged-edge resizing — which only closed off
one symptom (free-form stretching) while leaving the actual "fixed resolution, scaled+letterboxed
viewport" behavior missing entirely, and would have distorted content on any window shape other
than exactly 16:9.

**Fix — ported the reference pattern:**
- `Renderer` gained a fixed design resolution, `kDesignW=1024, kDesignH=768` (matching the
  1024x768 "buffer space" convention `handleTouch`/the web build's `toBP()` already used for
  touch input — nice, unplanned consistency win).
- `Renderer::width()/height()` (the `IRenderer` overrides all of `game.cpp`'s layout code reads)
  now return these **fixed constants**, not the live window size — every existing layout
  computation in `game.cpp` (tile centering, HUD/dialogue positions, the store dialog, etc.)
  automatically now operates against a fixed logical canvas, unchanged in `game.cpp` itself.
- Added `Renderer::computeAspectFitViewport(deviceW, deviceH, targetW, targetH)` — a direct port
  of the referenced `SetAcceptRationWithGameresolution`'s math (uniform scale = min(scaleX,
  scaleY), centered) — and used it to compute the actual `VkViewport` at both real per-frame
  render sites (the normal windowed path and the `TOMS_SPLIT=1` diagnostic path, kept consistent)
  instead of always spanning the full window. The vertex shader's push-constant `res` now also
  always carries the fixed design resolution, matching what `width()/height()` report.
- Removed the `glfwSetWindowAspectRatio(window, 16, 9)` lock — no longer needed now that any
  window shape gets a correct letterboxed/pillarboxed fit; the window can now be resized freely
  to any aspect ratio.
- Letterbox/pillarbox bars need no special clear-color handling — the render pass already clears
  the *entire* framebuffer before the viewport-scoped draws run, so the bars simply show the
  existing dark clear color for free.
- Added `Renderer::deviceToDesign(deviceX, deviceY, ...)`, the inverse mapping (device/window
  pixel → fixed design-space coordinate, correctly returning "not found" for a point inside a
  letterbox bar), and rewired the mouse-click forwarding added earlier today to use it instead of
  its previous naive `/framebufferSize*1024` math (which ignored letterboxing entirely and would
  have mismapped clicks the moment the window wasn't exactly 4:3). Also fixed that click handling
  to account for window-vs-framebuffer coordinate scale (HiDPI displays), which the first draft
  didn't handle either.

**Known, accepted gap — not fixed, scope-noted rather than silently left implicit:** the Dear
ImGui dev overlays (F1 debug overlay, F2 styling spike) are **not** aware of the new letterboxed
viewport — ImGui's own backend sizes itself against the real window/framebuffer, entirely
separate from the custom Vulkan viewport this fix scoped to the game's own sprite/text
rendering. This means those two dev-only tools may be positioned slightly off on a window whose
size differs noticeably from 1024x768, until/unless ImGui is also taught about the letterbox
(a separate, dev-tool-only follow-up — not blocking, since neither overlay is shipped UI).

**Verification performed:** full 12-test regression passes (this is rendering-only, no test
exercises it directly); both `Debug` and `Release` build clean; a 5-second smoke test of the
freshly-built Debug executable shows no crash and no error output.

**Owner confirmed: it works.** Resizing the window to different shapes now keeps the game at its
fixed design resolution and correctly letterboxes/pillarboxes instead of reflowing or stretching.
This closes out the whole shader-flip → stale-build → keyboard-debounce → mouse-wiring →
aspect-fit-viewport chain of fixes that started from one screenshot — **all confirmed working by
the owner directly**, the first real end-to-end visual verification this session has had for
anything in the live desktop window.

### 2026-09-07 — M5 (part 1): Stage Select hub + Notification system

With real visual verification now possible, went back to M5's actual deliverables — split into
the purely-additive half (this entry) and the higher-risk "replace working UI" half (deferred,
see Next Step).

**Notification/Toast system** (architecture-doc §13 #28):
- `Game::notifications_` — a simple `{message, remaining_ms}` queue; `pushNotification()` adds an
  entry (3-second lifetime); `update()` decrements and prunes expired ones every frame.
- `Game::drawNotifications()` (ImGui, always drawn when non-empty, not a dev-only toggle like
  F1/F2) — stacks toasts top-right below the HUD/store icon.
- Wired to two real triggers: **level-up** (both level-up sites — `applyItem`'s exp-item branch
  and `resolveCombatRound`'s combat-win branch — push a "升級了！LV N" toast) and **daily mission
  reset** (`Game::rollDailyMissions()`, called once per session in `loadAssets`, wraps Milestone
  4's pure `toms::rollDailyReset` with a local-date wall clock and pushes a toast on transition to
  `Available`). The daily-mission trigger is real, wired, and correct, but inert today since
  `missionDefs_` is still empty until Milestone 8 loads actual mission content — consistent with
  this session's established "systems now, content later" pattern for missions.

**Stage Select hub** (architecture-doc §10):
- `Game::ensureStageListLoaded()` scans `data/stages/` once, caching each stage's id/name/index.
- **Real unlock tracking, not a stub:** `Game::loadStage()` now records every stage id it ever
  loads into `meta_.unlockedStages` (already existed as a field since Milestone 1, previously
  unused for this purpose). `GameConditionContext::stageCleared()` — a stub returning `false`
  since Milestone 3 — now does a real lookup against that list, closing another of the
  intentionally-deferred M3/M4 stubs.
- `Game::drawStageSelect()` (ImGui) lists every stage: locked (previous stage not yet reached,
  with a hover tooltip explaining why, using `ImGuiHoveredFlags_AllowWhenDisabled` since
  `BeginDisabled()` suppresses hover reporting by default), "已到達" (reached) if visited before,
  "NEW" if unlocked but never visited. Selecting one calls the existing `loadStage()` — no changes
  to stage-loading itself.
- Opened/closed via a new **Tab** key (only opens when nothing else is modal, mirroring the
  existing "B opens the shop" convention); added to `modalActive()`'s flag set and to the
  Escape-priority chain (`main.cpp`'s Escape handler: store, then Stage Select, then inventory,
  then quit) established during the earlier keyboard-debounce fix.
- Boot behavior is intentionally unchanged — the game still auto-loads `stage01` on launch; Stage
  Select is an in-game hub for replaying reached floors, not (yet) a mandatory title-screen gate,
  matching the roadmap's own scoping note that a Main Menu is separate, not-yet-built work.

**Verification performed:** full 12-test regression passes (this milestone adds no new pure-logic
module, so no new tests — the additions are UI/orchestration wiring over already-tested pieces:
`stageCleared` over M1's `unlockedStages` field, `rollDailyMissions` over M4's tested
`rollDailyReset`); both Debug and Release build clean; smoke-tested with the Stage Select screen
temporarily forced open (via a one-line test call, reverted immediately after) to actually
exercise its widget-drawing code, not just the toggle path — no crash, no error output, same
verification discipline as M2's debug-overlay check.

**Still unconfirmed by this session:** visual correctness of both new screens (layout, whether the
lock/reached/NEW badges read correctly, whether the toast stacks position sensibly) — needs the
owner to press Tab and level up once to see a toast, same as every other UI piece this session.

### 2026-09-07 — "F12 crashes the game" investigation: unreproduced, but found a real build-system landmine

Owner reported pressing F12 while the game was running (launched via debugging in Visual Studio)
caused a crash. F12 isn't bound to anything in this project's code (only F1/F2 are handled).

**Attempted to reproduce directly** by launching `tower_vulkan.exe`, focusing its real window,
and injecting actual F12 keydown/up events via `keybd_event` — both after a short delay and
repeatedly across the whole boot window (15 presses spanning ~3 seconds from process start).
**Could not reproduce it** — the process survived every attempt, no crash, no error output.

**Follow-up from the owner reframed the question**: they're debugging via Visual Studio directly
(opening the project / solution, not running `main.cpp` from VS Code as earlier assumed) and
asked how to regenerate `tower_vulkan.sln` / use `CMakeLists.txt` to build and debug. Trying to
answer that by reconfiguring surfaced a real, separate problem:

**Found: the `build/` directory's CMake cache was corrupted with a generator mismatch.**
`cmake --preset vs2022-x64` failed with `Does not match the generator used previously: Ninja` —
confirmed via `CMakeCache.txt`: `CMAKE_GENERATOR:INTERNAL=Ninja`, even though this project's only
generator is `Visual Studio 17 2022`. Something (very likely a different tool — e.g. an editor's
CMake integration that defaults to Ninja, such as VS Code's CMake Tools extension) had
configured the *same* `build/` directory with a different generator at some point outside this
session's own commands, since nothing this session ran ever specified Ninja. **This is a
plausible real explanation for "debugging feels broken/crashes unpredictably"**: if the owner's
Visual Studio was pointed at a `build/` directory whose cache/generated project files didn't
consistently match what was last actually compiled, debugging could easily hit stale PDBs,
mismatched binaries, or outright configure failures — independent of any actual F12 key bug.

**Fixed two things:**
1. **Permanently pinned the Visual Studio instance** in `CMakePresets.json`'s `vs2022-x64` preset
   (`CMAKE_GENERATOR_INSTANCE` cache variable → the VS 2022 Community install path) — this is the
   same fix applied ad hoc via a command-line flag earlier today (when the VS 2026 install first
   caused this ambiguity), now made permanent so it survives a fresh configure/clone rather than
   needing to be redone by hand. (First attempt used the presets-schema `generatorInstance` field
   directly, which the IDE's own preset-schema validation rejected as unsupported at this file's
   declared schema version — switched to the equivalent plain `cacheVariables` form, which has no
   such restriction.)
2. **Cleared the corrupted cache** (`build/CMakeCache.txt` + `build/CMakeFiles/` — pure generated
   metadata; did not touch `build/_deps/`'s already-fetched GLFW/ImGui source checkouts or any
   project source) and reconfigured clean with the now-pinned preset. Rebuilt both Debug and
   Release from scratch (full rebuild, since the deleted `CMakeFiles/` held all incremental-build
   dependency tracking) — both succeed, full 12-test regression passes.
3. Documented both the instance-pin and the cache-corruption recovery steps in
   `docs/BUILD_WINDOWS.md` §7.4, including a note that alternating between two different tools
   (e.g. Visual Studio's own "Open Folder" CMake integration and a different editor's CMake
   plugin) configuring the *same* `build/` directory is the likely way to reproduce this again,
   with a suggested fix (point them at separate binary directories).

**Still an open question at first:** whether the original F12 report was about this stale/
mismatched build state, or a genuinely separate issue. Owner then clarified they launch via F5
in Visual Studio (not `main.cpp` from VS Code) and the crash is real and reproducible for them
that way — narrowing the search continued in the same conversation turn (see next entry).

### 2026-09-07 — F12 crash: found the real, concrete lead (Steam's Vulkan overlay), not yet confirmed

F12 is not bound to anything in this project's code (only F1/F2 are handled, both project-owned).
Checked what else on this specific machine could be intercepting it:

- **No RenderDoc** (the other common suspect for a Vulkan-app F12 hotkey) installed.
- **Confirmed Steam's Vulkan overlay layer IS registered as a global implicit layer** on this
  machine: `HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers` lists
  `D:\Program Files (x86)\Steam\SteamOverlayVulkanLayer64.json` (plus a second Steam layer,
  Fossilize, and an Epic Online Services overlay layer — none of this is specific to this
  project; it's global machine state affecting every Vulkan process). An implicit layer loads
  into **every** Vulkan application automatically, regardless of whether that app was launched
  through Steam.
- **F12 is Steam's default in-game screenshot hotkey.** A custom/hand-rolled Vulkan renderer
  (this project) crashing when Steam's overlay hooks into it on that hotkey is a well-known,
  common real-world compatibility category — not unique to this codebase.
- Read the actual layer manifest (`SteamOverlayVulkanLayer64.json`) to get the **official,
  documented** per-process opt-out rather than guessing an env var name:
  `disable_environment: { "DISABLE_VK_LAYER_VALVE_steam_overlay_1": "1" }`.

**Not yet confirmed either way** — owner hadn't tested it before signing off for the day. The
test (set that env var in Visual Studio's Debugging → Environment field for `tower_vulkan`, F5,
press F12) is queued as the top item in ▶ Next Step above. Deliberately did **not** disable
Steam's overlay globally or bake the env var into the project's own build config — that's a
machine-wide/user preference decision (affects every other game/app using Steam overlay too),
not something to change unilaterally without the owner first confirming this is actually the
cause.

### 2026-09-08 — UI font-size setting (owner request, after confirming Tab/Stage Select works)

Owner confirmed Tab opens Stage Select, but its labels read too small. Added a real, adjustable
font-scale setting rather than just bumping a hardcoded number:

- `Game::uiFontScale_` (default `1.5x`, in-memory only — no settings file exists yet, so this
  resets to default each launch, same "persistence is later work" caveat as `meta_`).
- `Game::applyUiSettings()` sets ImGui's built-in `io.FontGlobalScale` from it — called once per
  frame from `main.cpp`, immediately after `imguiLayer.newFrame()` and before anything else
  draws that frame, so the scale is always in effect before any ImGui window is built.
- A "介面設定 UI Settings" collapsible section inside the Stage Select window (the closest thing
  to a menu screen that exists) with a `SliderFloat` (0.5x–2.5x) and a reset-to-default button.
  Deliberately scoped to ImGui-rendered UI only (Stage Select, notifications, debug overlay) —
  does not touch the separate scene-graph HUD/dialogue text pipeline, which has its own
  already-reasonably-sized fixed pixel values and would be a larger, separate change.

**Verification:** full 12-test regression passes; both Debug and Release build clean; smoke-tested
with the settings section temporarily forced open (`ImGuiTreeNodeFlags_DefaultOpen`, reverted
immediately after) to actually exercise the slider/button widget code, not just the collapsed
toggle — no crash, no error output.

### 2026-09-08 — M6 foundation: Power Bar math + Equipment System (pure/tested, not yet wired to live combat)

Owner said "go next milestone" — started M6 (Equipment System + Power Bar minigame + real-time
input), the roadmap's own highest-risk milestone, since it needs a capability the engine doesn't
have yet (real-time hold-duration input, distinct from the discrete "was this key just pressed"
model everything else uses) and replaces the currently-working auto-attack combat loop. Built and
thoroughly tested the safe, pure-math foundation first; deliberately have **not** touched live
combat yet (see the open question below).

- `src/engine/power_bar.h/.cpp` — `simulatePosition()` (the cubic ease-in marker simulation,
  integrated in small fixed steps rather than the caller's variable frame `dt`, so two
  press/release timestamps that hold for the same duration always resolve identically — the
  design's own stated promise), `powerFromPosition()`/`zoneFromPosition()` (the piecewise-linear
  zone math), `computeAttackDamage()`/`computeDefenseDamage()` (FIGHT_SCENE_DESIGN.md §4's
  formulas, generalized to take a weapon's `maxMult` rather than hardcoding the 2.0x baseline).
  26 tests.
- `src/game/equipment_system.h/.cpp` — `EquipmentDefinition` (weapon/armor/talent, JSON
  round-trip), `applyEquipmentStats()` (flat atk/def stack), `effectiveAttackBar()`/
  `effectiveDefenseBar()`/`effectiveMaxMult()` (equipment's Power Bar parameter stack), and the
  three talent effects as small explicit functions (Focus/Berserker/Guardian) rather than a
  scripting hook — matching this project's established convention. Verified against the actual
  worked example in `MAIN_BATTLE_SCENE_DESIGN.md` §5 (War Hammer + Guardian Plate → ATK 20/DEF 9).
  Content-light by design (no real `data/equipment.json` yet — that's Milestone 8's job, same
  split already used for missions). 30 tests.

**Found-and-fixed bug in my own test, not the code** (caught by the test run itself, not a
silent pass): `computeAttackDamage(10, 100.0f)` was asserted to equal 20, but P=100 also
satisfies the P≥97 Perfect-bonus condition, so the documented +25% correctly stacks on top —
the real answer is 25 (worked out by hand: `ceil(10×2.0)=20`, then `ceil(20×1.25)=25`). Fixed the
test's expected values (both the 2.0x and 2.6x-maxMult cases) rather than the code, since the
code was implementing the documented formula correctly and the test's assumption was wrong.

**Verification:** all 14 headless tests pass (12 from before + these 2 new ones); `tower_vulkan`
(both Debug and Release) still builds clean, unaffected — neither new module is wired into
`game.cpp`/`main.cpp` yet, by design, pending the decision below.

**Open question, not yet decided:** whether to proceed into the risky remainder of M6 now —
adding real-time hold-duration input tracking to `main.cpp` and replacing the live
`resolveCombatRound()` auto-attack loop with the Power-Bar-driven one — or stop here with a
solid, tested foundation and take stock first, given this replaces the game's core, currently-
working combat mechanic. Raised to the owner in this session; not decided as of this entry.

### 2026-09-08 — M6 complete: live combat now runs on the Power Bar minigame

Owner chose "wire it into live combat now." Replaced `Game::resolveCombatRound()` (the old
automatic 700ms-timer auto-attack) with a player-timed round, matching FIGHT_SCENE_DESIGN.md §6's
pseudocode exactly: `CombatState` gained a `Phase` enum (`AwaitAttackPress -> AttackCharging ->
AttackResultPause -> [enemy alive?] AwaitDefensePress -> DefenseCharging -> DefenseResultPause ->
back to AwaitAttackPress`), plus `charging`/`chargeMs`/`lastPosition`/`lastPower`/`lastDamage`/
`resultPauseMs` fields. `ticks` (the old auto-timer accumulator) was removed outright rather than
left as dead code, once confirmed nothing else referenced it.

- **Real-time input** (the capability the engine didn't have before): `main.cpp` now also tracks
  the action button's (Enter/Space) *held* state via the already-existing `keyDown()` level-query
  (separate from the edge-triggered `keyPressed()` everything else uses), calling
  `Game::battleChargeStart()`/`battleChargeRelease()` on the press/release edges. Both are
  no-ops whenever combat isn't active or a charge isn't currently allowed, so `main.cpp` doesn't
  need to track battle phase itself. `Game::update()` accumulates `chargeMs` while charging and
  counts down `resultPauseMs` after each release, *regardless* of `cs.active` (a release that
  ends the fight sets `cs.active=false` in the same call that starts the pause — gating the
  countdown on `cs.active` would have frozen it forever; see the found-bug note below). The
  touch/web input path (`handleTouch`) got the same wiring — untested (no Emscripten toolchain
  here), but it replaces code that was already a complete no-op during combat (`interact()` is
  blocked by `modalActive()`), so there was nothing working to regress.
- **Damage resolution**: `resolveAttackRelease()`/`resolveDefenseRelease()` call
  `simulatePosition()` → `powerFromPosition()`/`zoneFromPosition()` → `computeAttackDamage()`/
  `computeDefenseDamage()`, feeding in `effectiveAttackBar()`/`effectiveDefenseBar()`/
  `effectiveMaxMult()` from the (currently always-baseline, nothing equipped yet) Equipment
  System, plus the two wired talent effects (Berserker's Red-zone-deals-0, Guardian's mitigation
  floor). If the Attack Bar's release kills the enemy, the round ends there — no Defense Bar that
  round, matching "the enemy retaliates after every hit except the killing blow" precisely
  (something the *old* code didn't actually do — see below).
- **Rewards/respawn logic preserved exactly**: extracted the old win/lose handling (gold/exp,
  level-up + notification, boss-warp to stage_11, `EnemyDefeated` event, respawn via
  `loadStage(curStage)`) into `finishCombatWin()`/`finishCombatLose()` unchanged, so both new
  release-resolution functions reach the *same* established reward/respawn behavior instead of
  duplicating it.
- **Rendering**: `Game::drawPowerBar()` draws the five-zone bar (red/blue/green/blue/red) plus a
  marker via the same raw-`Quad`/`ren->drawSprite()` technique as the existing HP bars —
  deliberately *not* ImGui, so the marker's frame timing is never at the mercy of ImGui's own
  frame pacing, per the architecture doc's Phase 5 guidance. Live position while charging
  (recomputed from `chargeMs` every frame), frozen at `lastPosition` during the result pause.

**A genuine, minor, pre-existing inconsistency fixed as a side effect, not introduced:** the old
`resolveCombatRound()` applied the enemy's retaliation *every* round unconditionally, including
the round that killed the enemy — contradicting `FIGHTING_TALKING_DESIGN.md`'s own documented
formula ("怪物反擊 `hits_to_kill - 1` 次"). The new Attack-then-conditionally-Defense structure
naturally fixes this: a kill on the Attack Bar release skips the Defense Bar entirely that round.

**Two real bugs found and fixed in my own draft before/while building, not after:**
1. My first draft nested the `resultPauseMs` countdown inside `if (cs.active)` in `update()` —
   but `cs.active` is set to `false` in the *same call* that starts the pause on a win/lose, so
   the countdown would never run, freezing `resultPauseMs` forever. Caught by re-reading the
   control flow before building, not by a test (there is no live-combat test — see the
   verification note below). Fixed by moving the countdown outside that guard.
2. Related: the old code's defeat message (`cs.log = "你倒下了..."`) was set and then
   *immediately* overwritten to `""` within the same synchronous call — since `update()` fully
   completes before `draw()` runs each frame, that message was **never actually visible** in the
   shipped game (a likely-unintentional quirk, not a deliberate design choice: `showBattle`'s
   render condition includes "log contains 倒下", which is why it needed clearing *at all* — to
   stop the battle overlay from getting stuck open forever, not to hide the message on purpose).
   The new phase system naturally lets it display for the 300ms result-pause instead before
   clearing — a small, deliberate, positive behavior change, called out explicitly here rather
   than shipped silently. (A win's victory text is unaffected — it correctly stays visible until
   the player dismisses it, exactly as before.)
- Also added a `mitigationFloor` parameter to `power_bar.h`'s `computeDefenseDamage()`
  (default `0.0f`, fully backward-compatible) to support Guardian's flat mitigation floor
  without duplicating the formula inline in `game.cpp`; added 3 more test cases for it.

**Verification performed:**
- Full 14-test regression passes (`power_bar_test` now 29 checks, `+3` for the new
  `mitigationFloor` parameter).
- Both `Debug` and `Release` builds are clean.
- **Actually exercised the live combat flow**, not just compiled it: added a temporary
  environment-variable-gated test hook (`TOMS_TEST_COMBAT`, reverted immediately after) that
  force-starts a fight against a configurable synthetic enemy at launch, then drove it through
  real injected hold/release key-event cycles (`keybd_event`, the same technique used for the
  earlier screenshot/F12 investigations) — one run with a weak enemy (likely win path), one with
  a very high-attack enemy that should reliably kill the player on its first Defense Bar release
  (lose/respawn path). Both survived multiple full charge/release cycles with no crash.
- **What this does *not* confirm**, stated precisely: whether the bar actually looks right on
  screen, whether the damage numbers "feel" balanced, or whether input timing feels responsive —
  none of that is checkable from here. This needs the owner to actually fight something and look.

### 2026-09-08 — M7 complete: balance simulation, checklist closure, and a real gameplay gap found + fixed

Owner said "go M7." The roadmap scopes this as validation-only — no new systems, just closing open
checklist items in the design docs using systems that now actually exist (M3's Condition Evaluator,
M4's Mission/Encounter systems, M6's Power Bar/Equipment). Three parts:

**1. Whole-tower balance simulation (computational, not hand-traced).** Wrote a Python script that
loads the real `data/enemies.json`/`data/stages/*.json`/`data/combat.json` and simulates a full
floor 1→10 climb (including the boss) under the exact deterministic formulas
`FIGHTING_TALKING_DESIGN.md`/`FIGHT_SCENE_DESIGN.md` document, run twice:
- **Full item collection** (every `gem_atk`/`gem_def`/`potion_*` picked up): comfortable margins
  the whole way, including the floor-10 boss (Vorkath, HP 400/ATK 34/DEF 14) — player arrives at
  ATK 58/DEF 34 and takes only ~9 total damage from that fight.
- **Zero item collection**: the player dies by floor 6, and again at the boss — confirmed this is
  the *documented, intended* trade-off (gems/potions are meant to be load-bearing), not a balance
  bug. Exactly which specific gems are hard-required for a minimal-but-survivable route is a
  separate map-connectivity-analysis question, not answered by this simulation — left as a noted,
  still-open residual item (see `FIGHTING_TALKING_DESIGN.md` §4).
- (Caught and fixed a bug in the simulation script itself along the way — a 6-value return
  unpacked into 5 variables in the level-up-check loop — before trusting any of its output.)

**2. Dialogue integrity check.** Programmatically checked every `data/dialogue/*.json` file's
`choice.next` references for dangling targets (a `next` pointing at a node id that doesn't exist
in that file). Checked all 15 files — none found. Also confirmed (as a byproduct) that zero
shipped dialogue files use Milestone 4's `action` field yet — consistent with M4's own log entry.

**3. Design-doc checklist closure**, each item marked closed only against something concrete found
this session or earlier, not just assumed:
- `GAME_DESIGN_DOCUMENT.md` §7 — replaced the old "Proposed" cumulative level-up-curve table
  (which never matched what the code actually does) with a section documenting the *real*,
  shipped formula (`need = level × 30`, `src/game/game.cpp`'s `finishCombatWin`/`applyItem`), plus
  a new "Balance verification pass" subsection recording the simulation results above. §17's
  checklist updated with `[x]`/`[ ]` status per item.
- `FIGHTING_TALKING_DESIGN.md` §4 — all four items closed with the evidence above.
- `MAIN_BATTLE_SCENE_DESIGN.md` §7 — the P≈44%-reproduces-old-baseline claim closed (already
  numerically verified back in M0's log entry); equipment-schema-validation marked *partially*
  done (the system is implemented and wired into live combat since M6, but there is no real
  content or equip/unequip UI yet — that's M8); gear-specific items explicitly marked blocked on
  M8 content rather than silently left ambiguous.
- `FIGHT_SCENE_DESIGN.md` §7 — closed the "does losing return you to the floor entrance with stats
  retained" item: confirmed `finishCombatLose()` (extracted unchanged during M6) calls the exact
  same `loadStage(curStage)` respawn the original pre-Power-Bar code did.

**4. A real gap found while closing the floor-repopulation question, then fixed.** The design
docs assume (and this session confirmed with the owner via `AskUserQuestion`, choosing **"stay
permanently cleared"**) that a monster you've beaten or an item you've picked up stays gone if you
leave the floor and come back — whether via stairs or Milestone 5's new Stage Select hub. Checking
this against the actual code found it **wasn't true**: `Game::loadStage()` calls
`st = parseStage(path)` (confirmed via grep — the only call site) unconditionally, which rebuilds
every `Entity` fresh from the stage's JSON file on *every* load, silently resetting every
monster/item's `consumed` flag back to `false`. Every floor reload — stairs included, not just the
new Stage Select shortcut — was already quietly repopulating monsters and items; this predates
Stage Select and just had never been exercised by any test or been visually obvious before.

**Fix — wired in the Milestone 3 Entity Status System for real** (it existed, tested, but was never
actually connected to gameplay before this):
- `Game` gained a new private member, `entityStatus_` (`std::map<std::string,std::string>`, same
  in-memory-only caveat as `meta_`/`missionTrackers_` — an on-disk save is still later work), keyed
  by `toms::entityStatusKey(stageId, x, y)` exactly as `RunSaveData::entityStatus` (Milestone 1)
  already assumes, so this slots into that existing schema shape rather than inventing a new one.
- `Game::movePlayer()`'s item-pickup branch now calls `toms::setEntityStatus(..., Collected)`
  right alongside the existing `e.consumed = true` / tile-clear it already did.
- `Game::finishCombatWin()` now calls `toms::setEntityStatus(..., Defeated)` right alongside its
  existing monster-removal lines, for the same reason.
- `Game::loadStage()`, immediately after `st = parseStage(path)`, now walks the freshly-parsed
  `st.entities` and re-applies any previously-persisted `Defeated`/`Collected` status: matching
  monster/item entities get `consumed = true` and their tile cleared **before** anything else in
  `loadStage()` runs (story-beat advancement, Stage Select unlock tracking, etc.), so the rest of
  the function sees an already-correctly-cleared floor.
- Doors are **deliberately not covered** by this fix — scoped to exactly what the owner's decision
  was about (monsters/items). Reusing an already-unlocked door still consumes another key on every
  revisit today; that's a separate, pre-existing, still-open gap, called out explicitly rather than
  silently bundled in or silently ignored.

**A second, unrelated real bug found while building this fix — a genuine, pre-existing latent
compile-time error, invisible until this exact change:** `src/engine/encounter.h` (Milestone 4) and
`src/engine/entity_status.h` (Milestone 3) both declared a free function `toms::fromString(const
std::string&)` — same namespace, same parameter type, **different return type**
(`EncounterKind` vs `EntityStatus`). C++ cannot overload on return type alone; this is flatly
illegal and always was, but no single `.cpp` file had ever `#include`d both headers together until
`game.h` needed `entity_status.h` for this fix (it already pulled in `encounter.h` transitively).
The very first build attempt failed with `C2556`/`C2371` redefinition errors, pointing at exactly
this. Fixed by renaming the older-in-intent-but-narrower one, `encounter.h`'s `fromString`, to
`encounterKindFromString` (declaration, definition, and the one test file that called it,
`encounter_resolve_test.cpp`) — `entity_status.h`'s `fromString` keeps its name since nothing
outside `entity_status.cpp`/`entity_status_test.cpp` called it under the ambiguous name anyway.
`game.cpp` itself never called either `fromString` directly (confirmed by grep before assuming this
rename was safe), so this is a pure, uncontroversial rename with no behavior change anywhere.

**Verification performed:**
- Both `Debug` and `Release` build clean (`tower_vulkan` target and the full test-target set).
- All 13 non-asset-path-dependent headless tests pass unchanged:
  `state_machine_test`(7), `event_bus_test`(5), `save_test`(18), `condition_eval_test`(24),
  `entity_status_test`(18), `story_controller_test`(9), `encounter_resolve_test`(20, including the
  renamed-function calls), `mission_test`(28), `power_bar_test`(29), `equipment_test`(30),
  `node_test`(8), `object_test`(8 groups), `log_test`. `font_test` also passes when run from the
  repo root (it reads a relative asset path — failing when run from `build/Release/` is a
  working-directory artifact of the test itself, not a regression; confirmed by re-running it from
  the root, where it passes).
- `texture_test` still fails to build — the same **pre-existing, owner-accepted gap** from M0
  (missing GLFW include dir on that one test target), unrelated to any change this session.

**What is *not* verified, stated precisely rather than assumed:** the actual runtime behavior of
the fix — walk up to a monster, beat it, leave the floor, come back, confirm it's still gone (and
the same for an item) — cannot be exercised from here the way M6's combat fix was (that used
injected real key-hold/release cycles against a synthetic enemy; a "defeat monster, take stairs,
return" sequence is a much longer, multi-screen interaction that isn't practical to script blindly
without visual feedback). Confidence rests on: careful code reading (the two write-sites mirror the
already-tested `entity_status_test.cpp` key format exactly), the read-site placement being the only
call site of `parseStage` (confirmed by grep, so there's no second path that could bypass this),
and the fact that this compiles and the full regression still passes. **This needs the owner to
actually clear a floor and come back to confirm it visually/experientially**, same honesty standard
as every other live-gameplay change this session.

### 2026-09-08 — Real bug report from the owner: battle scene didn't hold input focus, victory screen couldn't be dismissed

Owner sent a real screenshot of a won fight (骷髏兵/skeleton, "會心一擊！造成 14 點傷害，敵人倒下
了！(按任意鍵繼續)") and reported two things: (1) while a battle is on screen, the walking-scene
underneath should not respond to any input at all — it should be exclusively focused on the
current phase; and (2) after winning, neither pressing Enter nor clicking the mouse anywhere would
dismiss the victory screen. Traced this to **three separate, real bugs**, all in the post-combat
"victory pause" state specifically (the mid-fight Power Bar phases were already correctly gated):

**Bug 1 — `modalActive()` didn't know about `cs.won`.** `Game::modalActive()` (`game.h`) gated
world input on `cs.active || inDialogue || invOpen || storeOpen || storeUnlockDlg ||
stageSelectOpen_` — but a win sets `cs.active = false` and `cs.won = true` (see
`finishCombatWin()`), so the moment a fight is won, `modalActive()` starts returning `false` again
even though the victory overlay is still on screen. Concretely, during that pause: arrow keys moved
the player underneath the overlay (`movePlayer()`'s own `modalActive()` guard no longer held),
Tab could open Stage Select, `B` could open the shop, and — worse — **Escape would quit the game
outright** instead of being a no-op (its `else if (!g.modalActive()) break;` branch in `main.cpp`
now incorrectly fired). This is exactly the "stage-walking scene should not respond to any input"
report. **Fix:** added `cs.won` to `modalActive()`'s condition.

**Bug 2 — no keyboard path ever cleared `cs.won`.** `handleTouch()` (mouse/touch) already had
`if (cs.won && phase == 0) { cs.won = false; return; }`, but `main.cpp`'s Enter/Space handler went
straight to `g.interact()` (NPC dialogue only) with no `cs.won` check at all — so "press any key to
continue" never worked from a keyboard, only from a click. **Fix:** added a `Game::dismissVictory()`
method (clears `cs.won` and `cs.log`) and check `g.combatWon()` first in `main.cpp`'s Enter/Space
handler, before falling through to dialogue/`interact()`. `handleTouch()`'s existing dismiss line
now calls the same shared method instead of setting the flag directly.

**Bug 3 — a fragile substring check kept the overlay open even after a successful dismiss.**
`Game::draw()`'s `showBattle` flag fell back to `cs.log.find("倒下") != std::string::npos` to keep
the battle overlay visible during the brief (300ms) result-pause after a **loss** (when both
`cs.active` and `cs.won` are already `false`, since the lose message needs a moment to be readable
before it disappears — see `update()`'s own comment on this from Milestone 6). The problem: the
**win** message also contains that exact substring — "...敵人倒下了！" ("the enemy fell down") — so
even after Bug 2's fix correctly cleared `cs.won`, `showBattle` stayed `true` forever afterward,
because the old victory log text never stopped matching. This is the actual, final reason
"clicking/pressing Enter anywhere cannot leave the battle scene" — the dismiss logic did fire, but
the screen didn't care. **Fix:** replaced the substring scan with the real, unambiguous signal that
was already being tracked for exactly this purpose: `cs.resultPauseMs > 0` (set to 300 by both
`resolveAttackRelease`/`resolveDefenseRelease`, ticked down every frame in `update()` regardless of
`cs.active`/`cs.won`). `showBattle` is now `cs.active || cs.won || cs.resultPauseMs > 0` — covers
mid-fight, the win pause (indefinite, until dismissed), and the brief lose pause (exactly 300ms,
then auto-closes), with no text-matching involved at all.

**Verification performed:**
- Both `Debug` and `Release` build clean; full 14-test regression still passes unchanged (none of
  these three bugs touch any pure-logic module the tests cover — this is UI/input-gating only).
- Attempted a live smoke test using the same technique that verified Milestone 6 (a temporary,
  env-var-gated hook force-starting combat against a synthetic 1-HP dummy enemy, then driving it
  via real injected `keybd_event` press/release cycles for Enter and an arrow key, with the
  injected keys' actual receipt confirmed in the process's own stdout — `enterDown` was seen
  transitioning `0→1→0` exactly as sent). However, **this specific run hit the same environment
  limitation already documented in this report's M2/M5 log entries**: the Vulkan frame-submission
  debug output stopped advancing after only a handful of frames, before the enemy's death (and
  therefore the whole win→block→dismiss→unblock sequence) could actually be exercised — consistent
  with "the present loop doesn't reliably keep advancing when this specific automated/remote-driven
  method is doing the driving," not a code issue (the owner's own real, hands-on sessions have
  never shown this). The temporary test hook and its diagnostic prints were fully reverted after
  this attempt — nothing test-only remains in `main.cpp`/`game.h`.

**What is *not* verified, precisely:** the actual live behavior of all three fixes together — that
movement/Tab/B/Escape are genuinely inert while the victory screen is up, and that both Enter and a
mouse click now genuinely return to the walking scene. Confidence rests on: a very precise,
line-by-line trace of the exact reported symptoms back to their root causes (each of the three bugs
explains a specific piece of the report, not a guess), the change being small and narrowly scoped
(one new field in one boolean expression, one new method, one substring-check replacement), and the
full regression staying green. **This needs the owner to actually win a fight and try Enter/click
to confirm**, same honesty standard as every other interactive fix this session.

---

---

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
