# Progress Report — Log Part 6 (2026-09-14)

> Archived log entries split out of `PROGRESS_REPORT.md` (the single file grew too large).
> That file still carries the live **Next Step** banner, the milestone status table, and
> **Open Questions / Blockers** — this file is chronological log entries only, read top to
> bottom, oldest first. See `PROGRESS_REPORT.md`'s Log Index for the full list of parts.

---

### 2026-09-14 — "C" step 1, done between sessions and not logged until now

Four commits landed after the entry above without a matching log update (a gap, not a decision) —
recorded here so the history stays honest: `f4ecfc3` built `src/game/ui_layout.h` (the design-size/
scale-aware layout primitive — `s()`/`xAt`/`yAt`/`yFromBottom`/`midX`/`midY`/`centred()`/
`battleBox()`/`wrap()` — plus `ui_layout_test.cpp`, 30 checks) and, separately, a first pass at
scaling the battle screen itself (`BS = 1.40`, a local centre-relative scale — the owner's actual
ask was simpler than "C"'s original design-resolution framing: *"make the battle scene's UI
bigger, it's hard to see on mobile"*), applied only partway through that screen's draw code.
`150a98c` audited the scene graph for a **different, owner-corrected** approach — *"scale a UI
parent node so the UI objects get bigger, at an UNCHANGED game resolution — never by lowering the
resolution, that softens the whole render, maze included"* — and added `Node::SetLocalScale`/
`LocalPoint`/`LocalRect` (the inverse transform a scaled node's hit-testing needs) with
`node_transform_test` (11 checks). `8b700d6`/`45250fd` fixed a real mobile pad regression
(pointer mapping now reads the buffer size; the pad's plates grow 1.20x via `kPadScale`/
`gpadBtn()`, one shared function for drawing and hit-testing). Net effect on "A": still
**disabled** (`if (false && ...)` in `emscripten_main.cpp`) — re-enabling a shrunk design broke
the pad once already and nothing since has re-earned that risk.

### 2026-09-14 — "C" finished: the battle screen's scale-up made consistent, the dialogue box scaled too

Owner: "do C". Picked up from the state above rather than restarting it — step 1 built the tools
(`ui_layout.h`, the node inverse-transform proof) but, per its own commit messages, deliberately
touched no more draw calls than the battle screen's already-half-applied `BS` scale. Decided
**not** to route this through either of step 1's two primitives: `ui_layout.h`'s variable design
size (`W`/`H`) is moot while "A" stays disabled (design is always 1024x768), and a real
scene-graph `Node`-based UI (the 150a98c approach) would mean rebuilding the dialogue/battle/pad
screens on the `Node`/`GameObject` tree instead of their current immediate-mode `Quad`/`drawText`
calls — a much larger, riskier rewrite than "make the existing screens legible on a phone" needs.
Instead extended the pattern already proven working for the battle screen (a local, unconditional
scale constant multiplying positions-from-a-fixed-anchor and sizes, applied consistently) to
the one screen that never got it.

**Battle screen — finished, not just extended.** `BS = 1.40` now reaches every element on that
screen: `drawAutoBar`'s own power-bar geometry (`drawPowerBar`'s y/height and its cooling-dim
overlay) and its button label, and the entire Super-Attack Gauge block (label, charge dots, and
the ready button + its hit rect, `superBtnRect_`) — all of these were still at their pre-scale
literal position/size, the reason the bar looked thin above an already-enlarged button and the
gauge looked untouched next to everything above it. Found and fixed two real overflow bugs the
finished scale-up exposed (confirmed in real screenshots, not just reasoning about the numbers):
- **The enemy HP label clipped off the right edge of the canvas** ("史萊姆 H" cut off mid-word) —
  exactly the clipping the original `f4ecfc3` commit message flagged and deferred ("part of the
  battle wiring step"). Its position and the bar it labels are both anchored through the same
  centre-relative `SX()`, so the label already sat directly after the bar — the bug is that
  `SX(cx+560)` is already most of the way to the design's right edge, and `BS=1.40` pushed the
  label's (unmeasured) text width the rest of the way off-screen. Fixed by measuring the actual
  label string via the existing `measureText()` and clamping its start x so it can never leave the
  canvas, rather than guessing a wider magic-number gap that a longer name/HP value could still
  overflow.
- **The player HP label drifted onto its own bar** — a bug the screenshots caught that neither
  commit message had named. The player's bar is anchored at the raw (unscaled) `cx` with a scaled
  *width* (`cx + S(200)`), but its label used `SX(cx+210)` — a different anchor convention
  entirely, and at `BS=1.40` that resolves to a smaller number than the bar's own actual right
  edge, so "你 HP 120" rendered on top of the green bar rather than beside it. Fixed by anchoring
  the label to where the bar it labels actually ends (`pBarEndX + S(10)`) instead of an
  independently-scaled offset that can drift apart from it.

**Dialogue box — now scales too, the same way, for the first time.** Bottom-anchored, matching the
pad's own rule (`ui_layout.h`'s `yFromBottom` names it, even though the dialogue code below doesn't
call that function directly): what must survive growing is the box's *distance from the bottom
edge*, not its top — the battle modal's centre-relative growth would push a bottom panel further
toward the edge instead of opening more room to read. New shared helpers in `game_helpers.h`
(`kDialogueScale = 1.40`, `dialogueBoxRect()`, `dialogueRowY()`) are called from both
`game_scene_draw.cpp`'s draw code and `game_input.cpp`'s hit-test — the same "single source of
truth" rule the pad and the battle rects already followed, so a future layout change here can't
quietly make a visible row stop responding to taps. The hit-test's per-row tap zone is now a flat
44px tall (was ±12px = 24px total, with no margin for a finger at all) — independent of the
(now larger, ~34px) visual row pitch, so adjacent rows' zones overlap by a few px, the ordinary
trade-off a short vertical list makes for real touch targets.

**A genuine, pre-existing bug found and fixed along the way, unrelated to any of the above but
directly in the way of trusting the result:** `ui_layout_test`'s own CJK wrap-invariant check
(`"15 CJK glyphs ... should need >= 3 lines"`) was failing — 2 lines instead of 3. Traced it by
re-implementing `wrap()`'s exact algorithm in Python against the same 15-glyph string and getting
3 lines, proving the algorithm in `ui_layout.h` is correct. The actual cause: the test's CJK string
was a plain (non-`u8`) narrow string literal, and MSVC had been silently mis-encoding it — every
one of those 15 code points is unrepresentable in the compiler's active code page (1252, Western
Windows), which it was already warning about at every build (`C4566`) without anyone connecting
that warning to this test's result. `u8"..."`-prefixing the literal fixes it (well-defined UTF-8
regardless of the active code page) — `ui_layout_test` is back to its originally-claimed 30/30.

**Verified.** Full regression: 20/20 test binaries pass (`ui_layout_test` now genuinely, not
vacuously; `texture_test` still takes noticeably longer than the others to exit in this
environment but does exit 0 — a pre-existing, unrelated observation, not investigated further
here). Both `tower_vulkan` and the web build (`toms_web`) compile clean. Browser, headless Chrome,
real gameplay (not just formulas): opened `villager_elder`'s dialogue (4 real choices), tapped the
second row at its new scaled screen position and it correctly selected+confirmed *that* choice,
advancing into the real `c_motive` question with its own 3 choices (screenshot-confirmed, not
assumed); started a real fight via the debug hook, tapped the battle screen's new scaled Attack
button and the enemy's HP genuinely dropped (24 → 21, confirming the drawn rect and the hit-tested
rect agree at the new scale); screenshotted the fixed battle screen at both desktop size and a
phone viewport (844×390) — both HP labels sit cleanly beside their bars now, enemy HP fully
on-screen, power bar/button/gauge all visually consistent in size. Not deployed — the owner did not
ask for a deploy this session, and this progress report's own convention (see the S1 deploy log
entry) is to treat "commit and push then deploy" as its own explicit ask, not a step to take
unprompted after a fix.

### 2026-09-14 — S4 first slice: the skill tree's foundation, live combat effects, a real Skills screen

Owner: "before start S4 please update progress to PROGRESS_REPORT.md then start." Scoped the slice before
writing code (recorded in the Next Step banner at the time): the foundation, not the full 功法三系 tree
`STORY_BIBLE.md` §3 describes at ch_04/F25 — that chapter has no data yet, so the full tree can't be built
regardless of how much time this slice spent.

**What "the skill tree" turned out to actually require, found while scoping (not assumed):** re-reading
`ch_01.json`/`ch_02.json`/`ch_03.json`, each chapter's `grants` (`{"skills":["s_yinqi"],"skillPoints":1,
"hub":[...],"unlocks":[...]}`) had **never been read by the runtime at all** — chapters are only ever
consumed by the offline Python tools (`gen_floors.py`/`validate_story.py`), not `game.cpp`. So `s_yinqi`/
`s_yuqi`/`s_faqi` (STORY_BIBLE §3: 引氣/禦氣/法氣, the three lineages' roots, unlocked ch_01/02/03
respectively) were dormant strings with nothing to grant them — building the tree meant building the
chapter-entry grant pipeline first, not assuming it already existed.

**Content.** `data/skills.json`: 3 lineages × (root + one tier-1 node) = 6 skills, i18n name/desc in all 6
languages matching every other content file's convention. Roots cost 0 (chapter-granted, not purchased);
tier-1 nodes cost 1–2 and require their lineage's root.

**Code — pure, tested first (this project's standing pattern).** `src/game/skill_system.h/.cpp`: same
shape as `equipment_system.h` on purpose (a content struct + pure stack math, no Game dependency) —
`loadSkillDefs()`, `canUnlockSkill()` (the one gating predicate: exists, not owned, prerequisites owned,
enough points), `applySkillEffects()` (stacks atk/def on top of `applyEquipmentStats`'s own out-params, one
formula instead of two). `skill_system_test.cpp`, 23 checks. `RunStoryState` gained `skillPoints_`/
`skillsOwned_` (+`addSkillPoints`/`unlockSkill`/`hasSkill`, mirroring `shards_`'s existing shape exactly),
wired into `RunSaveData`/save v3 (no schema bump needed, same `.value(key, default)` migration rule as
every other S3/S4 field) and `reset()` (skills do NOT survive rebirth, matching every other run-scoped
field — only shards do, per section 8). `run_state_test.cpp` +7 checks (round-trip, pre-v3 migration
defaults, points-never-negative, double-unlock-is-a-no-op, rebirth clears skills).

**The chapter-entry pipeline (the actually-new plumbing).** `Game::applyChapterGrants(chapterId)`
(`game_story.cpp`): reads `data/story/chapters/<id>.json`, grants `skillPoints`/`skills` (free, cost
bypassed — a grant is never subject to `canUnlockSkill`), records `hub`/`unlocks` as run flags for a later
phase to read (same "wire the data even if nothing consumes it yet" move this project made for missions/
equipment before their own UIs existed). Idempotent via a reserved flag (`_chapter_granted.<id>`) — and,
importantly, **not** gated to "first floor of the act" the way the act-card visual is: it runs on every
floor-load of every chapter, so a save resumed mid-chapter (Continue landing on, say, F05, which never
re-triggers "this is the first floor") still backfills ch_01's grant correctly. A chapter id with no file
yet (ch_04+) marks itself granted-with-nothing rather than erroring, so that content can land later without
retroactively firing mid-session.

**Live combat effects.** `toms::applySkillEffects` called alongside `toms::applyEquipmentStats` at all
three sites that compute effective atk/def (`resolveAttackTap`/`resolveEnemyClockFire`/`battleTapSuper` in
`game_combat.cpp`) — skills and gear are one stack now, not two a caller could forget to add. `Game::
effectiveAtk()/effectiveDef()` (`game.h`) expose the real, fully-stacked number read-only, for the UI and
for verification (base `pl.atk`/`pl.def` themselves never change from either layer, so probing those would
have shown nothing).

**A real, minimal Skills screen**, not just wired data — added as a fourth row on the existing gear-icon
menu (`InGameMenuPage::Skills`, alongside Main/Settings): a flat 6-row list in story order (`Game::
skillMenuOrder()`, a pure function of `skillDefs_` so draw and input can never disagree about which id is
at which row — the same principle this project already applies to the pad/battle rects and the dialogue
rows), each row showing `[x]`/`[ ]`/`[--]` (owned / affordable / locked) plus its cost, a points-available
header, and a Back button reusing the Settings page's own `igmBackRect_` (Settings and Skills are never
open together, so there's nothing to disambiguate). `Game::tryUnlockSkill()` re-validates `canUnlockSkill`
itself rather than trusting the caller, so a stale/duplicate UI click can never double-spend or bypass a
prerequisite.

**A real bug caught live, not just in a unit test (fixed the same session).** First browser pass showed
`禦氣`/`法氣` (ch_02/ch_03's not-yet-granted roots) rendered as *unlockable* the moment the player had even
1 point — `canUnlockSkill` only checked cost/prerequisites/already-owned, and a root's cost is 0 (because a
grant is free), which the predicate misread as "free to buy any time." Root nodes are grant-only by design
(`applyChapterGrants` calls `RunStoryState::unlockSkill` directly, bypassing `canUnlockSkill` on purpose);
fixed by having `canUnlockSkill` refuse any `tier <= 0` node outright, added as an explicit regression check
in `skill_system_test.cpp` ("a root is never purchasable, however many points"), re-verified live
afterward — 禦氣/法氣 now render `[--]`, and `jsUnlockSkill("s_yuqi")`/`("s_faqi")` both correctly return 0.

**Verified.** New harness probes (`jsSkillInfo`, `jsHasSkill`, `jsUnlockSkill`, `jsPlayerInfo` which==2/3 for
`effectiveAtk`/`effectiveDef`), mirroring the existing `jsDebugBattle`/`jsChoose`-style convention. Full
regression: **21/21 test binaries pass** (`skill_system_test` new; `run_state_test` 52, up from 45;
`texture_test` excluded per its own pre-existing, unrelated slow-exit note). Both `tower_vulkan` and
`toms_web` compile clean. Browser, headless Chrome, real gameplay: New Game → `jsSkillInfo` shows 1 point /
1 skill owned, `jsHasSkill("s_yinqi")` = 1, `effectiveAtk` = 13 (12 base + 1); opened the Skills screen via
real keyboard nav (Escape → Down ×2 → Enter) and screenshotted it — 引氣 marked `[x]`, the rest correctly
locked; `jsUnlockSkill("s_yinqi_1")` spent the point, `effectiveAtk` became 15 (+2 more), a second call
correctly no-ops (still 0 points, still owned once); `jsUnlockSkill("s_yuqi_1")` correctly blocked (missing
prerequisite); re-screenshotted after the tier-0 fix — 禦氣/法氣 now show `[--]` as intended. Not deployed
(same standing rule as the "C" entry above — deploy is its own explicit ask).

**Not done, on purpose, and named so nobody assumes otherwise:** deeper tiers per lineage past tier-1; the
real 功法三系 tree UI/gating STORY_BIBLE §3 places at ch_04/F25 (that chapter has no data yet — this
screen is reachable from ch_01 instead, an explicit simplification); `hub`/`unlocks` flags are recorded but
still read by nothing (S5's forge/hub UI is what reads them); no way to VIEW a locked skill's full
name/description before it's owned (today's `[--]` row only shows the name + cost, matching the store's own
"show what you can afford" convention, not a tooltip/detail view).

### 2026-09-14 — S5: forging (`data/forge.json`, `ForgeRecipeDefinition`, a real Forge screen)

Owner: "so next step is S5? if it is do it." Scoped to the forging slice `docs/story/STORY_DATA_SCHEMA.md`
§13's M4→M5 order names first (the hub/village screen `hub.json` reads the same `hub.*` run flags S4 already
records but stays its own, not-yet-started task — forging doesn't need it to exist).

**A scoping decision made by re-reading the source doc, not by assuming symmetry with S4.**
`STORY_BIBLE.md` §8's rebirth table says forge blueprints/active skills *"保留圖譜與已獲得的主動技知識"*
(keep the blueprint/knowledge, but re-obtain the materials/gear) — the opposite of S4's skills, which reset
every run. So `forgeRecipesKnown` lives on `MetaSaveData` (permanent, survives rebirth), not on
`RunStoryState` next to `skillsOwned_` the way the shape might have suggested by default.

**Content.** `data/forge.json`: 3 recipes (`fr_gatewarden`→`guardian_plate`, `fr_twin_edge`→`twin_daggers`,
`fr_hammer_of_dawn`→`war_hammer`), each with a gold cost and a `materials` map (`gem_atk`/`gem_def` counts),
full i18n name/desc in all 6 languages. Only `fr_gatewarden` is granted this slice (`ch_03.json`'s `grants`
gained `"forgeRecipes": ["fr_gatewarden"]`) — the other two exist as content so the menu's "shape" (locked
rows, same convention the Skills screen already established) is visible before every recipe is reachable.

**Code — pure, tested first, same shape as `skill_system.h/.cpp` on purpose.** `src/game/forge_system.h/.cpp`:
`ForgeRecipeDefinition` (id/resultEquipmentId/costGold/materials/name/desc), `loadForgeRecipes()`,
`canCraft()` (recipe exists, known, enough gold, enough of each material in the inventory — `std::count`
against a plain `vector<string>` inventory, matching how items are already stored). `forge_system_test.cpp`,
15 checks. `MetaSaveData` gained `forgeRecipesKnown` (+ `toJson`/`metaFromJson`, the same
`.value(key, default)` migration rule as every other meta field); `run_state_test.cpp` +2 checks (the field
round-trips, and a pre-S5 meta file loads with an empty list rather than refusing).

**The chapter-entry pipeline, extended rather than duplicated.** `Game::applyChapterGrants()` gained a
`grants.forgeRecipes` block (`game_story.cpp`) sitting between the existing skills block and the hub/unlocks
block: dedups by hand against `meta_.forgeRecipesKnown` (a plain vector, not a set — same shape the rebirth
table already implies: small, append-mostly, order doesn't matter) rather than assuming the idempotency flag
alone is enough, since a `MetaSaveData` field (unlike `RunStoryState`) can legitimately be touched across many
runs within one cycle, not just once per chapter visit.

**`Game::tryCraft(recipeId)`** (`game_story.cpp`): re-validates `canCraft()` itself (same "never trust the
caller" rule as `tryUnlockSkill`) — deducts gold, removes each required material from `pl.inv` by count (a
loop over `std::vector::erase`, mirroring how the inventory already stores items — no new container), then
equips the result via `equipmentDefs_`'s slot, the exact `switch` `buyStoreItem()` already uses. Gold/materials
spend and the equip happen in the same call, so a UI bug can never spend one without the other landing.

**A real Forge screen**, added as a fifth row on the gear-icon menu (`InGameMenuPage::Forge`) — but unlike
Skills (always shown, even before any node is grantable), **conditionally shown**: `Game::forgeMenuUnlocked()`
reads `run_.flag("hub.forge")`, the same flag S4's chapter-grant pipeline already records from `ch_03.json`'s
`grants.hub`. Reasoned live during this slice that an always-visible-but-always-empty Forge screen (Skills'
own choice) would be worse UX here specifically because forging has no root/tier-0 "something to look at"
the way the skill tree does before ch_01 grants a lineage — so the Main page's row count/index is conditional
on this flag (`inGameMenuMove/Activate/Back/Click` all branch on `forgeMenuUnlocked()` rather than a fixed
row count), the one real structural difference from the Skills page's pattern. `Game::forgeMenuOrder()`
mirrors `skillMenuOrder()`'s "pure function of the defs map, so draw and input can't disagree" rule, but with
plain id order — recipes have no lineage-grouping concept to sort by. Each row shows `[x]`/known-and-craftable
`[ ]`/known-but-short `[!]`/unknown `[--]` plus cost, a gold-on-hand header, and a Back button reusing
`igmBackRect_` (Skills/Settings/Forge are never open together).

**Verified.** New harness probes (`jsForgeInfo`, `jsHasRecipe`, `jsCraft`, `jsAddGold`, `jsAddItem`), mirroring
the S4 skill probes' convention exactly. Full regression: **22/22 test binaries pass** (`forge_system_test`
new, 15/15; `run_state_test` 54, up from 52; `texture_test` excluded per its own pre-existing, unrelated
slow-exit note) — run from the repo root, since several of these tests resolve `data/...` relatively and a
first pass run from `build/Debug` produced misleading failures that had nothing to do with this change (a
tooling gotcha worth naming so it isn't mistaken for a regression next time). Both `tower_vulkan` and
`toms_web` compile clean (`CMakeLists.txt` needed `forge_system.cpp` added alongside `skill_system.cpp` in
both the desktop and web target source lists — missed on the first build attempt, caught immediately by a
linker error, not silently shipped). Browser, headless Chrome, real gameplay — both through direct harness
calls and through an actual simulated UI click path (dispatched real `pointerdown`/`pointerup` events through
the same JS handler a real tap goes through, not a direct C++ call): jumped to F15 (`jsGoStage`, ch_03's first
floor) to fire the real chapter grant — `jsForgeInfo(0)` (known count) went 0→1, `jsHasRecipe("fr_gatewarden")`
→1, `fr_twin_edge` correctly still 0; `jsCraft` correctly refused with no gold/materials; after
`jsAddGold(100)` + two `jsAddItem("gem_def")`, `jsCraft("fr_gatewarden")` succeeded, gold 100→70,
`effectiveDef` rose from 4 to 10 (armor +5, plus +1 from `s_faqi`'s own chapter-3 grant landing in the same
`jsGoStage` call — both stacking correctly through the existing `applyEquipmentStats`/`applySkillEffects`
pipeline); re-crafting with materials spent correctly refused. Then, independently, opened the real menu by
clicking the HUD gear icon, clicked through Main→Forge, and clicked the 守護者鎧甲 row directly — gold
dropped 100→70 from that click alone and the row's mark changed from `[ ]` to `[!]`, screenshot-confirmed at
each step (Main page now shows all 5 rows incl. 鍛造; the Forge page shows one craftable known recipe and two
correctly-locked `[--]` unknowns). Not deployed (same standing rule as S4's entry above — deploy is its own
explicit ask).

**Not done, on purpose, and named so nobody assumes otherwise:** `fr_twin_edge`/`fr_hammer_of_dawn` are
authored but not yet granted by any chapter (no ch_04+ data exists yet to grant them from); the village/hub
screen (`hub.json`) that `hub.forge`/`hub.village` flags were always meant to eventually feed a richer
"visit the forge NPC" flow through — today's Forge screen is reached the same lightweight way as Skills (the
gear-icon menu), not a hub visit; no confirm-before-spend dialog on craft (mirrors the Skills screen and the
Store's own "tap = commit" convention, not a gap specific to forging).

### 2026-09-14 — `src/game/` reorganized into 5 category folders (core/systems/save/ui/audio)

Owner: "please make src/game source code move to new folders by category to make it easy find out the
code." A pure filesystem/build reorg — no behavior, gameplay, or interface change of any kind.

**Why now, not earlier.** S1-S5 kept adding self-contained units (`skill_system`, `forge_system`,
`run_state`, `floor_table`, `roamer`, `camera`, …) on top of the 2026-09-13 `game.cpp` split, and by this
session `src/game/` had grown to **59 files flat in one directory** — the original refactor (§1 of
`docs/architecture/CODE_LAYOUT.md`) solved "one file is too long"; this reorg solves the newer problem,
"which of 59 files has the thing I'm looking for."

**Categorization** (documented in full, with the reasoning per folder, in `docs/architecture/CODE_LAYOUT.md`
§1.5 — not repeated here): `core/` (16 files — the `Game` class itself: `game.h` + every `game_*.cpp`,
`main.cpp`, `stage.h`), `systems/` (27 files — self-contained gameplay subsystems, each with its own test:
equipment/skill/forge/mission, run/story state, floor table, footprints+roamer, camera), `save/` (7 files —
persistence: `save_system`, `save_slots`, `game_settings`), `ui/` (7 files — `title_screen`, `ui_layout`,
`localization`), `audio/` (2 files — `Audio.h/.cpp`). All moves done via `git mv` (history preserved) except
the 3 not-yet-committed `forge_system.*` files from the same session's S5 work, moved with plain `mv`.

**The one real design decision: keep every `#include` unchanged.** Rewriting the physical location of 59
files could have meant touching every `#include "foo.h"` across the tree to add its new folder prefix (the
"textbook" approach) — but the ask was specifically about *finding files on disk*, and doing so risked a
long tail of easy-to-miss cross-includes across `game.h`/`game_internal.h`/etc. Instead, `CMakeLists.txt`
gained `GAME_CORE`/`GAME_SYSTEMS`/`GAME_SAVE`/`GAME_UI`/`GAME_AUDIO` (each `${GAME_DIR}/<folder>`) and a
combined `GAME_INCLUDE_DIRS` list added to every target's `target_include_directories` that used to just
list `${GAME_DIR}` — since the 59 filenames were already unique in the old flat layout, adding all five
folders to the include search path resolves every existing bare `#include "foo.h"` exactly as before,
because the compiler's quote-form lookup already tries the including file's own directory first and only
falls through to `-I` dirs for a cross-folder include. Zero `#include` lines needed to change; only
`CMakeLists.txt`'s **file list** (which `.cpp` feeds which target) needed each of its 85 `${GAME_DIR}/x`
references updated to the right `${GAME_CATEGORY}/x`.

**A real mistake caught immediately by rebuilding, not shipped.** The first pass at updating
`CMakeLists.txt` (a scripted replace) also accidentally rewrote the six `set(GAME_CORE ...)` /
`set(GAME_INCLUDE_DIRS ...)` definition lines themselves (an off-by-one in which lines the script was told
to skip), which would have made every category variable resolve to a nonsense nested path. Caught before
any build was attempted by re-reading the diff, not by a failed build — fixed by hand, then verified.

**Verified.** Full reconfigure + rebuild: `tower_vulkan` (Debug) and `toms_web` both compile and link clean
with zero include-path errors. **23/23 test binaries pass**, including `texture_test` (which needs to run
from the directory the shader `.spv` files were copied next to — a pre-existing cwd quirk unrelated to this
reorg, reconfirmed rather than assumed). `docs/architecture/CODE_LAYOUT.md` gained a new §1.5 describing the
five folders and updated its file-responsibility table's paths; `tools/subset_fonts.py`'s one comment
reference to `game.cpp` updated to `core/game.cpp`. Historical progress-report log entries (this file
included, above this entry) were deliberately **not** rewritten to the new paths — they are a frozen record
of what was true when written, same rule this project already applied to the docs-folder reorg earlier this
session.

### 2026-09-14 — S6 first slice: the village hub (`data/hub.json`, a real Village screen)

Owner: "so next step... yes" to reading `docs/story/STORY_BIBLE.md` for what the hub is and scoping a first
slice before writing code — the same "design it, then build" shape S4/S5 needed.

**What research actually found, before any scoping.** The bible is thin here on purpose: a "村莊樞紐"
(village hub) unlocks at F7 alongside 引氣 (ch_01), and a later "軍營" (barracks) upgrade at F43 (ch_07) —
no NPC roster, no service list, no `hub.json` schema (`STORY_DATA_SCHEMA.md` only shows one example grant,
`"hub": ["hub.crypt_shrine"]`, for a chapter that doesn't exist yet). Checking what the runtime already does
with this found a real, until-now-invisible gap: `ch_01.json` grants `hub.village` and `ch_03.json` grants
`hub.forge` as run flags, `hub.forge` already gates the Forge screen S5 built, but **`hub.village` was read
by nothing** — the Store, which thematically belongs in a village, actually unlocks via its own unrelated
`data/store.json` `unlockstage` field (currently floor 1), with no connection to the story's hub system at
all.

**The one real scope decision, put to the owner rather than assumed:** should this slice finally tie the
Store's unlock to `hub.village`, matching the fiction more literally (no village = no shop)? Owner chose
**no** — leave the Store's existing unlock rule alone, since M7's balance pass was tuned against an
early-open Store and F1-F6 players would otherwise lose shop access they have today. This slice is
additive only.

**Content & code — same shape as S4/S5 on purpose.** `data/hub.json`: one authored location so far
(`hub_village`, gated on `hub.village`, i18n name/desc, `action: {kind: "talk", npc: "villager_elder"}`).
`src/game/systems/hub_system.h/.cpp`: `HubLocationDefinition` + `loadHubLocations()` + a pure
`hubLocationUnlocked()` predicate (takes a `std::function<bool(const std::string&)>` flag-check callback
rather than `RunStoryState` directly, keeping it as dependency-free as `skill_system.h`/`forge_system.h`).
`hub_system_test.cpp`, 11 checks. Unlike skills/forge, a hub location has no cost and nothing to "own" — no
new `MetaSaveData`/`RunSaveData` field was needed; unlocking is just reading the run flag its `unlockFlag`
already names.

**A real design generalization, not just a copy-paste of the Forge page.** Forge (S5) added its Main-page
row via a hand-picked `forgeOn ? 5 : 4` row count. Adding Village as a SECOND independent conditional row
would have meant a second hardcoded special case (4 branches: neither/village-only/forge-only/both) — so
this slice replaced that with `Game::mainMenuOrder()` (`game.h`/`game_story.cpp`), a pure function
returning the Main page's actual row list from the two flags, and rewrote `inGameMenuMove/Activate/Back/
Click`+`drawInGameMenu`'s Main-page branch to index through it instead of literal row numbers. Forge's own
behavior is unchanged (still row 3 whenever Village is absent) — this was a refactor to the mechanism, not
the Main page's existing labels/order. `Game::activateHubLocation()` mirrors `tryUnlockSkill`/`tryCraft`'s
"never trust the caller" shape: re-checks `hubLocationUnlocked()` itself, dispatches by `actionKind`
(`"talk"` → closes the pause menu, then `startDialogue()` — the two were never designed to render at once
— an unrecognized kind is a safe no-op so a future content typo can't crash). The Hub sub-page itself
(`InGameMenuPage::Hub`) mirrors Forge's list layout exactly (`[ ]`/`[--]` rows via `hubMenuOrder()`, a pure
id-order function), even though with only one authored location there is nothing to show dimmed yet — the
list UI exists now so a second location (e.g. `hub.crypt_shrine`, ch_05, not yet authored) is purely a data
addition later, not new UI code.

**Verified.** New harness probes (`jsHubUnlocked`, `jsActivateHub`), mirroring the S4/S5 probes' convention.
Full regression: **24/24 test binaries pass** (`hub_system_test` new, 11/11; everything else unchanged).
Both `tower_vulkan` and `toms_web` compile clean. Browser, headless Chrome, real gameplay: New Game starts
on F01 (ch_01) — `jsHubUnlocked("hub_village")` was already 1 with zero walking (ch_01's grant fires on the
very first `loadStage()` call), `jsActivateHub("hub_village")` opened a real dialogue (`jsDialogueInfo`
went 0→1, 4 choices), an unknown location id was a safe no-op both for unlock-check and activate. Then,
independently, a real simulated UI click path (not direct C++ calls): clicked the HUD gear icon, screenshot
showed the Main page's now-5 rows (Save/Settings/Skills/**Village**/Back — Forge correctly absent at F01,
since `hub.forge` isn't granted until ch_03), clicked Village into the Hub sub-page (showed exactly one row,
"[ ] 村莊"), clicked that row and the pause menu closed into the *real* dialogue window with 長老's actual
opening line and 3 live choices, screenshot-confirmed at every step. Not deployed (same standing rule as
S4/S5's entries above — deploy is its own explicit ask).

**Not done, on purpose, and named so nobody assumes otherwise:** the Store's unlock stays independent of
`hub.village` (the owner's explicit choice this session); `hub.crypt_shrine`/the barracks upgrade
(ch_05/ch_07) aren't authored — no chapter data exists that far yet; the Hub page has no locked-row visual
proof yet (nothing to dim until a second location exists) — the dimming code path is written but only
exercised once that content lands.

### 2026-09-14 — small cleanup: the pause menu's sub-page panel math, deduplicated

Owner spotted `float w = 420, h = 260;` in `Game::drawStylingSpike()` (an M2 dev-tool "spike" window, F2
toggle, desktop-only — used exactly once, testing whether an ImGui NoBackground window can align with a
scene-graph-drawn backdrop) and asked whether magic-number UI literals like it are common enough across the
codebase to deserve a shared constant or a data file. Answered directly: that specific one isn't — it's a
single-use throwaway experiment, not a pattern, so naming it would be an abstraction with no second caller
to justify it. But the broader check found a real one: `game_store.cpp`'s Settings/Skills/Forge/Hub
sub-pages (four pages, three of them added this very session across S4/S5/S6) each **copy-pasted the exact
same 4-line panel-sizing formula** (`bh = 130 + n*(rowH+rowGap) + 70`, first row at `panel.y+96`, rows inset
30px) — the precise "shared math copied to two .cpp files" case `docs/architecture/CODE_LAYOUT.md`'s own
boundary rule #2 already says belongs in `game_helpers.h`, just not yet applied to this corner. It was
already the root cause of two of this session's own test-script bugs (mis-deriving a row's Y coordinate by
hand while writing the S5/S6 verification scripts).

**Fix:** one new `computeSubPageLayout(W, H, rowCount, rowH=56, rowGap=10, panelW=460)` in `game_helpers.h`
(alongside the file's existing single-source-of-truth helpers — `gpadBtn()`, `dialogueBoxRect()`), returning
the panel rect + first row's `(x,y)`. All four sub-pages now call it instead of retyping the formula;
Settings passes its own `rowH=48, panelW=420` (a real, pre-existing difference from the other three, kept
exactly as it was). UI values stay C++ constants, not a JSON file — these are engine layout math (design
resolution pixel geometry), not designer-tunable content the way skill costs/forge materials are, so a data
file would be process overhead with no one who'd actually edit it.

**Verified.** Full regression: 24/24 test binaries still pass. Both builds compile clean. Screenshotted all
four sub-pages (Settings/Skills/Village/Forge) after the refactor and confirmed each is pixel-identical to
before — same formula, one fewer place it could drift out of sync.

