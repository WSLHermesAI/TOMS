# Progress Report — Log Part 2 (2026-09-08)

> Archived log entries split out of `PROGRESS_REPORT.md` (the single file grew too large).
> That file still carries the live **Next Step** banner, the milestone status table, and
> **Open Questions / Blockers** — this file is chronological log entries only, read top to
> bottom, oldest first. See `PROGRESS_REPORT.md`'s Log Index for the full list of parts.

---

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

