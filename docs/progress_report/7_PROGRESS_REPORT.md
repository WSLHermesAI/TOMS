# Progress Report — Log Part 7 (2026-09-16)

> Archived log entries split out of `PROGRESS_REPORT.md` (the single file grew too large).
> That file still carries the live **Next Step** banner, the milestone status table, and
> **Open Questions / Blockers** — this file is chronological log entries only, read top to
> bottom, oldest first. See `PROGRESS_REPORT.md`'s Log Index for the full list of parts.

---

### 2026-09-16 — equipment actives, first slice: a real 4th battle button (`data/actives.json`)

Owner: "yes" to reading `ART_AND_ABILITY_DESIGN.md`/`STORY_BIBLE.md`/`STORY_DATA_SCHEMA.md` for what
equipment actives are and scoping a first slice before writing code — the fourth of the project's four
systems (技能樹／鍛造／裝備主動技／村莊樞紐), and the only one with zero code against it until now.

**What research found: two different trigger shapes hiding under one name.** The three actives named in
the docs don't all behave the same way. `a_qingxiao_edge` ("每場一次必會心，傷害 ×1.8") reads as something
the player presses — a buff armed for the next attack. `a_sanctuary_echo` ("受致命傷保留1HP，每場1次") reads
as fully automatic — it fires on its own when the player would otherwise die, no button at all. Separately,
the status-effect system already has `st_echo`/`st_silence` worded as "下一個主動技不進冷卻" / "無法使用
主動技" — "use" and "cooldown" language that only makes sense if actives are a real player-pressed battle
action, and `STORY_DATA_SCHEMA.md` independently says actives are "built on top of" the bar mechanic. Put
this fork to the owner rather than guessing: **manual button** (a real 4th battle action, matching the
status-effect wording) was chosen over passive auto-trigger.

**A second, quieter finding, not blocking today's work but worth recording.** Re-reading `STORY_BIBLE.md`
§8's actual rebirth table (as opposed to the paraphrase used when scoping S4) shows learned skills **survive**
a cycle-rebirth at half effect ("已學技能：保留，但每個技能的數值效果 ×0.5") — they are not wiped. Checked
whether S4's `RunStoryState::reset()` had this backwards: it doesn't, because `reset()` is only ever called
from `Game::newGame()` (a genuinely new game), never from a death or a real cycle-rebirth (S7's rebirth flow
doesn't exist yet). No bug today, but S7, when it's built, will need its own reset logic that keeps skills at
half power rather than reusing today's full-wipe `reset()`.

**Content: no reachable attachment point exists yet, so a placeholder was authored, not a real one.** All
three named actives are `ss_05`/`ss_08`/`ss_09` rewards behind chapters that don't exist (`ch_05`/`ch_07`/
`ch_08` have no data) — the same content-blocker shape S6's hub hit with `hub.crypt_shrine`. Rather than wait,
authored a new placeholder in the same spirit as `a_qingxiao_edge` but attached to `twin_daggers`
(`fr_twin_edge`'s forge result — already authored in S5, never granted by any chapter, so untouched by
players today): `a_twin_flash` ("疾影斬"), same guaranteed-crit-next-attack shape, damage ×1.8.

**Code — pure content first, same shape as skill/forge/hub systems.** `equipment_system.h` gained
`EquipmentDefinition::actives` (a plain id list — almost every item's list is empty) and
`equippedActives(EquippedSet, defs)` (unions whatever the equipped weapon/armor/talent grant; +4 checks in
`equipment_test.cpp`, 34 total). New `src/game/systems/active_system.h/.cpp`: `ActiveSkillDefinition`
(id/effectKind/damageMult/name/desc) + `loadActiveSkills()` — deliberately has **no** separate "known" list
(unlike `forgeRecipesKnown`): nothing in this slice's content grants an active independently of the gear that
carries it, so a persistence layer with zero producers would be premature. `active_system_test.cpp`, 9 checks.

**Wiring into real combat.** `CombatState` gained `activeUsed` (once-per-battle spent flag, matching every
named active's "每場一次" wording — not a real cooldown timer) and `nextAttackGuaranteedCrit`/
`nextAttackCritDamageMult` (the armed buff). `Game::battleTapActive()` (mirrors `tryUnlockSkill`/`tryCraft`'s
"never trust the caller" shape): no-op unless equipped gear grants an unused active; dispatches by
`effectKind` (only `guaranteed_crit_next_attack` exists; an unrecognized kind is a safe no-op, matching
`activateHubLocation`'s same rule, and doesn't spend `activeUsed`). `resolveAttackTap()` now checks the armed
flag: forces `power = 100` (a scripted perfect release, not a well-timed one) and — deliberately — bypasses
the Berserker red-zone-zeroes-attacks rule too, since "guaranteed" shouldn't depend on real marker timing;
then multiplies the resulting damage by `damageMult` and clears the flag.

**A real 4th battle button, not just wired data.** Added `activeBtnRect_`, drawn in `game_scene_draw.cpp`
right below the Super gauge — same "no button at all until it's actually available" convention Super already
uses, rather than a permanently-visible-but-disabled one. A persistent gold hint line ("下一擊必定命中要害")
sits near the Attack bar while the buff is armed, since `cs.log` alone gets overwritten by the very next
exchange (the enemy's own clock can fire before the player taps Attack) and the player needs to still see the
buff is live. Wired into every existing input path: `handleTouch` (touch/mouse hit-test, gated the same way as
the draw condition), and — a real gap this slice closed, not an afterthought — **keyboard parity on both
platforms**: `H` now triggers `battleTapActive()` in both `main.cpp` (desktop) and `emscripten_main.cpp`
(browser), alongside the existing F(Defend)/G(Super) bindings. Without this, a keyboard-only desktop player
would have had no way to use an active at all, unlike every other battle action.

**Verified.** New harness probes (`jsEquip` — a debug-only direct-equip bypass of Store/Forge, since
`twin_daggers` isn't purchasable/craftable through any authored content yet; `jsActiveInfo`). Full regression:
**25/25 test binaries pass** (`active_system_test` new, 9/9; `equipment_test` 34, up from 30). Both
`tower_vulkan` and `toms_web` compile clean. Browser, headless Chrome, real gameplay — through the actual `H`
key handler, not a bypass call: equipped `twin_daggers`, started a fight, confirmed `activeUsed`/crit-armed
were both 0 before anything; pressed `H` — `activeUsed`→1, crit armed→1; pressed `H` again — stayed armed
(no double-arm); pressed real Enter (the existing `battleTapAttack()` binding) — crit flag consumed, a 24-HP
slime dropped to −21 in one hit (the guaranteed-crit multiplier landing regardless of the auto-bar's live
position); pressed `H` a third time — stayed unarmed (spent, not re-triggerable this battle). Then,
independently, a real simulated mouse click on the actual on-screen button (not a direct function call) armed
the buff exactly the same way, and screenshots confirmed all three visual pieces: the combat log line
("已灌注疾影斬"), the persistent gold hint ("下一擊必定命中要害") appearing near the Attack bar, and the
button itself correctly disappearing once spent. Not deployed (same standing rule as every prior S4–S6
entry — deploy is its own explicit ask).

**Not done, on purpose, and named so nobody assumes otherwise:** the three actives actually named in the
design docs (`a_qingxiao_edge`, `a_sanctuary_echo`, `a_soul_echo`) aren't authored — their side stories
(`ss_05`/`ss_08`/`ss_09`) need chapters that don't exist yet; no `activeSkillsKnown` meta-scope field (see
above — deliberately deferred, not forgotten, until real content needs it); only one `effectKind`
(`guaranteed_crit_next_attack`) is implemented — `a_sanctuary_echo`'s auto-trigger shape (survive lethal
damage once) was explicitly deferred by the owner's own choice this session, not started; no visual buff icon
beyond the text hint (matches this project's established "text first, sprite/VFX later" convention for new
mechanics).

### 2026-09-16 — M7 first slice: the 15-ending resolver, and a real death-triggered ending screen

Owner: "yes" to reading `STORY_BIBLE.md`'s endings section and `STORY_DATA_SCHEMA.md` §7 to scope a first
slice, continuing the same day's "next step" chain (equipment actives → this).

**What research found.** The endings system is unusually well-specified: 15 endings (3 good, 11 bad/neutral,
1 hidden), each with a real condition tree in the *same* DSL `condition.h` already implements, and a clean
"first match wins down a priority list, guaranteed unconditional fallback" resolver algorithm
(`STORY_DATA_SCHEMA.md` §7.2). But 14 of the 15 require either F70 (doesn't exist — only F01-21/ch_01-03 are
authored) or `cycleIndex≥9` (M8's rebirth isn't built) — so almost the entire system is, by content, not yet
reachable. Exactly **one** ending is genuinely reachable today: `e_13` ("凡人之死", 12 non-boss deaths). That
depended on a real, pre-existing bug found while tracing it: `RunStoryState::noteDeath()` has existed since
S3 (schemaVersion 3's `deathsTotal`/`deathsNonBoss` fields, save-compatible and everything) but **nothing
had ever called it** — the counter has been permanently stuck at 0 since S3 shipped. Put the resulting real
behavior change to the owner rather than deciding unilaterally: fixing the counter means a 12th death now
actually ends the run (today, death has zero permanent consequence). Owner chose to wire it live.

**Content.** `data/story/endings.json`: all 15 endings, each translated from `STORY_BIBLE.md`'s table into
the condition DSL (`resolutionOrder`: array order; `fallback`: `e_05`). `data/text.json` gained 30 keys
(`story.ending.e01..e15.name/.text`, zh_TW authoritative + a real English translation, matching the
established narrative-content convention — inline `{code:text}` objects, not the system-content files' own
convention) plus `ending.dismiss_hint`.

**Two condition-DSL gaps found and closed while authoring the content, not left as TODOs.** `{"type":
"always"}` (the guaranteed-fallback leaf the schema's own example shows) didn't exist -- added to
`condition.cpp`/tested. `memoryShards` (e_07's condition) and `deathsNonBoss` (e_13's) didn't exist either --
each got its own `ConditionContext` virtual (defaulted to 0, matching S3's leaves) + a `GameConditionContext`
override reading `RunStoryState`. A third, more structural gap: several endings' `itemHeld` conditions name
*equipment* ids (`qingxiao_blade`, `emperor_bone_armor`) -- but equipping something was never added to
`pl.inv` (equipment is its own registry, not an inventory item; see `buyStoreItem()`/`tryCraft()`), so
`itemHeld` could never recognize an equipped item no matter what was worn. Fixed by giving
`GameConditionContext` a required `EquippedSet` constructor argument (matching the class's existing "required,
not defaulted" philosophy for `run`) and checking it in `itemHeld()` -- untestable live today since none of
those items are authored as real content yet, but now correct for whenever they are.

**Code — pure resolver, built on top of the existing evaluator, not a second DSL.** New
`src/game/systems/ending_system.h/.cpp`: `EndingDefinition` + `loadEndings()` + two resolver functions, kept
deliberately separate because the schema itself keeps them separate (`STORY_DATA_SCHEMA.md` §7.3's own
table): `judgeEnding()` walks the full 15 in order and always returns something (the F70-completion judge --
authored and tested, not yet reachable by real play); `checkNamedEndings()` walks only an explicit
caller-given id list and *never* falls back (the wipe/choice-triggered rows) -- deliberately, since the
fallback ending's own easily-satisfied condition would otherwise wrongly end the run on literally every
death. `ending_system_test.cpp`, 14 checks, including a case specifically proving `checkNamedEndings` never
matches an id outside its own given order even when that id's condition would hold.

**Wiring into real combat.** `finishCombatLose()` now calls `run_.noteDeath(cs.enemy.boss)` (the actual bug
fix) then `checkNamedEndings(endingsTable_, {"e_10","e_13"}, ctx)` *before* the ordinary respawn path; a
match calls the new `Game::triggerEnding()` (records `meta_.endingsSeen`, flushes the save immediately) and
skips the respawn entirely. A real full-screen ending scene (`drawEndingScreen()`, scene-graph primitives so
it renders identically on web and desktop, not ImGui) gets the exact same "checked before anything else,
nothing renders behind it" priority the title screen already has, wired into `modalActive()` so every other
input path correctly goes inert. Dismissal (tap-anywhere, or Enter/Space/Escape on both platforms' keyboard
paths) always returns to the title -- M8's rebirth isn't built, so there is nothing else to offer yet.

**Verified.** New harness probes (`jsForceLose` -- runs the real `finishCombatLose()` path without needing
to actually lose 12 real fights first, `jsEndingInfo`, `jsActiveEndingId`). Full regression: **26/26 test
binaries pass** (`ending_system_test` new, 14/14; `condition_eval_test` 29, up from 24). Both `tower_vulkan`
and `toms_web` compile clean. Browser, headless Chrome, real gameplay: 11 forced losses left `deathsNonBoss`
at 11 with no ending (ordinary respawn each time, matching "avoid dead ends" -- this isn't a hair-trigger);
the 12th correctly resolved to `e_13`, screenshot-confirmed showing "凡人之死" in gold, the real flavor text,
and the dismiss hint; dismissed via a real Enter keypress and screenshot-confirmed landing back on the real
title screen; separately, a real simulated mouse click anywhere on screen also dismissed it (not just the
keyboard path). Not deployed (same standing rule as every prior entry this session).

**Not done, on purpose, and named so nobody assumes otherwise:** 14 of the 15 endings are authored but not
reachable by real play yet (F70/`cycleIndex≥9` don't exist); `e_09` (a specific F69 choice) references a
choice id (`c_f69_hear_him_out`) that doesn't exist in any authored chapter -- a placeholder in the same
spirit as S5/S6/S7's other not-yet-reachable content; `e_14`'s "no main-line choice made" condition only
checks the 5 flags `ch_01`/`ch_03` can actually set today -- it will need extending as `ch_02`/`ch_04`+ add
their own choices; M8's rebirth offer (`allowsRebirth`) is recorded on every ending but nothing reads it yet.

### 2026-09-16 — M8 first slice: 輪迴 (rebirth), a real "start the next cycle" flow

Owner: "do M8", continuing the same day's chain (M6 actives → M7 endings → M8). Went straight to
implementation, reusing the §8 research already done while scoping M6/M7.

**A real design payoff from that earlier research, not just background reading.** M7's own research had
already found that `RunStoryState::reset()`'s comment ("skills are re-earned like every other run-scoped
progress") was an assumption from S4 that doesn't match `STORY_BIBLE.md` §8's actual carry table (skills are
KEPT, halved). M8 is where that gets fixed for real: `reset()` itself is left alone (its contract stays "a
full wipe, optionally keeping shards" -- simple, unconditional), and `Game::rebirth()` is the one place that
calls it and then explicitly restores what the bible says survives, at half power. Also good news found while
scoping: since `e_13` (12 deaths) is the one ending already reachable through real play (M7), and the bible
says every ending except `e_10` allows rebirth, `e_13` doubles as a fully real, live-testable path through
the ENTIRE rebirth flow -- no F70/cycleIndex≥9 content gap to work around this time.

**What actually carries over, decided item by item against §8's table, not assumed.** Skills: kept
(`RunStoryState::skillsOwned_` copied across the wipe), but a TIER≥1 node's own bonus is halved-and-floored
from now on (`applySkillEffects()` gained a `rebirthScale` parameter, defaulted to 1.0 so every pre-M8 caller
is unaffected) -- a tier≤0 root (a pure grant, never purchased, e.g. 引氣) is explicitly exempt, matching the
bible's own carve-out ("純解鎖型技能...保留全效"). Skill points and the Super-Attack Gauge's cap: both
halved-and-floored (min 1 for the gauge) -- the gauge's cap stopped being the compile-time
`CombatState::kSuperThreshold` and became `RunStoryState::superMax()` (a real per-run, save-persisted value)
since a compile-time constant can't be "carried at half power" by definition; all 6 of its old call sites
(combat resolution, the input hit-test, the HUD draw) now read the dynamic value. Atk/def: the LEVEL-UP bonus
over the starting stat gets halved (`newAtk = 12 + (oldAtk-12)/2`), not the starting stat itself -- named
`kStartingAtk`/`kStartingDef`/`kStartingHp` constants now back both `newGame()` and `rebirth()`'s math instead
of the literal `12/4/120` living independently in two places. Memory shards: already `reset(keepShards=true)`'s
job, unchanged. Forge blueprints and known actives: needed **zero new code** -- they already live on `meta_`
(`MetaSaveData`), which `rebirth()` never touches except `cycleIndex`, so S5/S7's own meta-scope choices
already got this right by construction. Reset: equipment (back to `wand`), consumables/materials/gold/keys,
main-line flags/choices/counters/side-stories (via `reset(true)` itself), floor back to F01. `cycleIndex`: +1
on `meta_`.

**Content.** `data/story/cycles.json`, authored to the full documented schema shape (`maxCycles`, the
per-field `carry`/`reset` tables) for fidelity/future tooling -- but `cycle_system.h/.cpp`'s loader only reads
the few real numbers this slice's explicit C++ transformation actually needs (`maxCycles`, `skillEffectScale`,
`statBonusScale`), the same "content matches the doc, the code that applies it is explicit, not a generic
interpreter" split `ending_system.cpp` already established for the resolver. `cultivationTier` is in the file
(schema fidelity) but not read by the loader -- no "修為層數" progression system exists anywhere in the
codebase to halve, so implementing that leaf would mean inventing a whole mechanic no other content uses; named
here as a deliberate, not accidental, gap.

**A real two-choice ending screen, not just a flag flip.** `Game::rebirthOffered()` is the one place that
checks both "does this ending even allow it" (`allowsRebirth`) and "is the cycle cap already reached"
(`meta_.cycleIndex < maxCycles`) -- draw code and every input handler call this one function so they can never
disagree about whether a second button exists. When offered, the ending screen shows two real buttons
("入輪迴"/"返回標題", explicit rects, hit-tested the same way the in-game menu's language-confirm Yes/No
already is) instead of the single tap-anywhere dismiss M7 shipped; when the cap is hit or the ending is `e_10`,
it falls back to that same single-exit screen unchanged. Keyboard parity on both platforms: Enter/Space
confirms the offered path (calls `rebirth()`, which re-checks `rebirthOffered()` itself and falls back to
dismissing if it somehow isn't valid), Escape always declines to the title -- extending the same F/G/H-style
binding discipline M6 established for the battle actions.

**Verified.** New harness probes (`jsRebirth`, `jsCycleInfo` for cycleIndex/skillPoints/superMax/skillsOwned/
gold/rebirthOffered). Full regression: **27/27 test binaries pass** (`cycle_system_test` new, 6/6;
`run_state_test` 58, up from 54; `skill_system_test` 26, up from 23). Both `tower_vulkan` and `toms_web`
compile clean. Browser, headless Chrome, real gameplay: unlocked a real tier-1 skill (effectiveAtk 12→15),
reached `e_13` via 12 forced deaths (same path M7 verified), confirmed `rebirthOffered()`→1 and the two-button
screen rendered (screenshot-confirmed); called the real `rebirth()` -- cycleIndex 1→2, `skillsOwned` KEPT (2,
not wiped), `superMax` 5→2, effectiveAtk 15→14 (root's +1 stayed full, tier-1's +2 floored to +1), gold reset
to 0, landed back on a fully playable F01 (screenshot-confirmed: HUD shows ATK 12/DEF 4/LV 1/GOLD 0, the
elder's dialogue re-fires exactly as a fresh ch_01 visit would). One thing the numbers caught, not a bug: post-
rebirth `skillPoints` read 1, not the halved-to-0 that felt like the obvious guess at first -- `loadStage("F01")`
(rebirth's own last step) naturally re-triggers ch_01's chapter grant, since `reset(true)` wipes the
`_chapter_granted.*` flags along with everything else -- exactly the intended "re-earn progression by
replaying the story" design, confirmed by tracing it rather than assumed. Then, independently of the keyboard
path, real simulated mouse clicks on the actual on-screen Rebirth button (cycleIndex advanced) and the actual
Title button (declined, cycleIndex unchanged) both worked. Not deployed (same standing rule as every prior
entry this session).

**Not done, on purpose, and named so nobody assumes otherwise:** `cultivationTier` halving (no underlying
system exists to halve); the "輪迴錄" meta UI (`cycleIndex`/`endingsSeen`/`hintsUnlocked` already exist as
data per S3, but nothing displays them as a screen yet); side-story RESET on rebirth is `reset(true)`'s
existing full wipe of `sideStories_` -- correct per §8's table, but not re-verified against real side-story
content since none is authored yet; `e_10`'s own "no more rebirth" path (`allowsRebirth: false`, cycle cap
reached) is implemented and unit-tested but not reachable live yet (needs `cycleIndex` to actually hit 9,
i.e. 8 real rebirths in a row -- not exercised end-to-end this session, only via `ending_system_test`'s mocked
context).

### 2026-09-16 — M9 first slice: `ch_04` content (F22-F28), scoped down from the full 7-chapter push

Owner confirmed "ch_04 only, F22-F28" as M9's first slice after being asked to scope it (the full push --
7 chapters, all of F22-F70 -- was flagged as too large to take on blind). Research before writing anything
found the real shape of the remaining work was much narrower than "author a chapter": F22-F28's mazes,
enemies, items and event *markers* already existed (S2's `gen_floors.py`), and `data/story.json`'s chapter
index already summarized `ch_04`. What was actually missing was the chapter detail file itself, all of its
narrative text, and -- found only by tracing the runtime code, not assumed from the docs -- two structural
gaps that made a chapter's story content only partially real.

**Two discoveries that reframed the work before any content got written.** First: `data/events/pool_actNN.json`
(the per-act event pool files S3 authored) are **never loaded by the game at all** -- a floor event's effect and
text are derived purely by substring-matching the event id itself (`game_input.cpp`: `"shard"`→memory shard,
`"trap"`→HP cost, `"cache"`/`"relic"`→HP+gold, else→flag-only) and its text is looked up directly as
`<id>.text` in `text.json`. So "author ch_04's events" isn't "write a pool file" (that would have zero effect)
-- it's "make sure every event id F22-F28's maze data already references has a real text.json entry," which
is what got done: 23 of the 27 unique ids referenced were missing real text entirely (4 already had it from
being reused base ids). Second: `skeleton_scholar.json` (the boss-hint NPC dialogue) already existed with
real content, but **the NPC was never placed on any stage's tile grid** -- nothing pointed a legend
character at `npc:skeleton_scholar`, so the whole dialogue file was unreachable dead content until now (added
the `"S": "npc:skeleton_scholar"` tile to F26's grid near the side-story hook's own documented cell, plus the
`skeleton_scholar` id to both of `game_input.cpp`'s NPC-dispatch branches, which only recognized the five
hub NPCs by name before this).

**A third, deeper gap: RunStoryState's own flags were unreadable by the condition evaluator.** `ss_04`
("藏書閣的一頁") needed to gate a dialogue choice on "has the player found the torn page" -- but the only
flag-reading leaf, `storyFlagSet`, reads `MetaSaveData`'s flags, while the floor event that marks "found it"
(`run_.setFlag("event_" + evId)`, pre-existing code, untouched) and a choice's own `setFlags` (e.g.
`flag_page_returned`) both write to `RunStoryState` instead -- a completely separate namespace with no leaf
able to read it back at all. Added `runFlagSet` (`condition.h`/`.cpp`, `GameConditionContext`, defaulted like
every other S3/M7 leaf so no existing context breaks), tested in `condition_eval_test` alongside
`storyFlagSet` specifically to prove they're different namespaces, not just added for ch_04 use.

**Also added, since ss_04's reward is "a skill grant tied to a story choice," which no dialogue verb could
do before:** `unlockSkill` and `setSideStoryState` verbs on `Game::runDialogueAction()` (mirrors
`applyChapterGrants()`'s own free-skill-grant path, just reachable from a choice instead of a chapter's
`grants` block), plus `grantSkill`/`completeSideStory` fields bundled directly onto `makeChoice`'s own action
(so one dialogue choice records the main-line decision, the counter, the flag, the skill, AND the side-story
completion in one action object -- matching `makeChoice`'s existing "the option's effects travel with the
action" precedent instead of inventing a multi-action-per-choice schema).

**Content.** `data/story/chapters/ch_04.json` (matches `ch_01`/`ch_03`'s exact shape: beats/grants/choices/
sideStoryHooks/exitConditions/enemyMix/itemTable). `data/skills.json` gained `s_yinqi_combo` (tier 2, cost 0
-- a dialogue grant, not a purchase, same reasoning `applyChapterGrants`' roots already established).
`skeleton_scholar.json` extended with a real branch: a `runFlagSet`-gated 3rd root choice → a confront node →
交還 (`insight+1`, `flag_page_returned`) / 自行研讀 (`resolve+1`, `flag_page_kept`), both granting
`s_yinqi_combo` and marking `ss_04` `completed` -- gated shut again afterward
(`{"not": {"type":"sideStoryState",...}}`, the same idiom `villager_elder.json`/`sorcerer_teacher.json`
already use for "don't re-offer a claimed reward"). `text.json`: 23 new event `.text` entries (15
`ev_common_*` variants, 8 `ev_library_*` -- the latter written to actually carry ss_04's own beats: shelf
searches, a torn page, a fresh fingerprint) plus real `name`/`intro`/`ambient.1`/`ambient.2` for all of
F22-F28 (28 keys, replacing 100%-placeholder `_todo` content) and the 4 new `story.ch04.*` keys (title +
the `c_library_page` choice's prompt/options).

**Verified.** New harness probes (`jsStoryFlag`, `jsSetFlag`/`Game::debugSetFlag` -- skips re-walking an
already-proven trigger, same reasoning as `jsForceLose`, `jsSideStoryState`). Full regression: **28/28 test
binaries pass** (`skill_system_test`'s data-count assertion updated 6→7 for the new `s_yinqi_combo` node,
still 26/26 checks; `condition_eval_test` +2 for `runFlagSet`). Both `tower_vulkan` and `toms_web` (webgl release AND the
`WEB_DEBUG` dev build, needed because the release build's "cleaned" `toms_web.html` -- no `#status` element,
an intentional owner-requested strip -- throws in `setStatus()` before the WASM runtime ever finishes
initializing under headless Chrome; the dev build keeps the element) compile clean. Browser, headless
Chrome, real gameplay: jumped to F26 (`jsGoStage`), confirmed the dialogue's 3rd choice is genuinely absent
before the torn-page flag and genuinely appears after (`jsDialogueInfo` choice count 2→3), walked the real
confront node (screenshot-confirmed: real rendered Chinese dialogue text, not a raw id), chose 交還 →
`choiceMade("c_library_page","opt_return")`, `insight` counter, `flag_page_returned`, and `hasSkill
s_yinqi_combo` all flipped true in the same call, `sideStoryState("ss_04")` became `"completed"`
(screenshot-confirmed epilogue line), and re-opening the dialogue afterward confirmed the choice is gone
again (back to 2) -- no re-grant possible. F28 (`ch_04`'s boss floor) still loads and reports the correct
70-floor sequence number, confirming the existing floor/boss-stage mapping was untouched.

**Not done, on purpose:** ch_05-ch_10 (explicitly out of scope for this slice); `data/events/pool_act04.json`
(would have zero runtime effect until pools are actually loaded, per the discovery above -- lower priority
than the `text.json` entries that do matter); the `flag_truth_sought` mirror SIDE_STORIES.md describes for
ss_04 (an extra monologue hinting at `ss_05`) -- that flag can only ever be set by `ch_05`'s own choice, which
doesn't exist in this slice's scope, so it would be unreachable dead content exactly like the pre-existing
`skeleton_scholar.json` was before this session; the deeper "功法三系" skill tree S4 originally flagged as
waiting on `ch_04` is only one node deeper for one lineage (`yinqi`) via `ss_04` -- `yuqi`/`faqi` still stop at
tier 1.

### 2026-09-16 — Stair alignment: every floor's stairs now physically line up, and arrival lands on them

Owner request, independent of the M9 content track: today each of the 70 floors' stairs are placed
independently, so a player who climbs up and a player who spawns fresh land on the same spot only by luck.
Owner asked for three things: (1) higher floors get bigger grids -- already true (`docs/story/STORY_DATA_SCHEMA.md`
§5.1b's `SIZE_RAMP`, confirmed unchanged, not touched this pass), (2) floor N's stairs_up tile must be the
EXACT same (x, y) as floor N+1's stairs_down tile, and (3) walking those stairs must actually land the player
there, not at a fixed default spawn. Scoped via three quick questions before touching anything, given the
blast radius (70+11 files, all of them already carrying real narrative content from S3/M4-M9): both the 60
procedural floors and the 11 hand-authored boss stages, keep today's size ramp as-is, and -- the one that
mattered most -- keep every floor's exact roster (same monsters/items/NPCs/events/door+key, same counts),
reshuffle ONLY the layout. That last answer is what made this safe to do the same day as ch_04's own content
work: F26's `skeleton_scholar` NPC and its 6 event tiles came out the other end of the regenerator fully
intact, just moved.

**New tool: `tools/gen_stairs.py`.** Regenerates all 70 floors' maze SHAPE in strict F01->F70 order, threading
one piece of state forward: the cell coordinate the previous floor's stairs_up landed on becomes THIS floor's
forced entrance (stairs_down goes exactly there). This works because a maze cell's rendered tile position is a
pure function of `(row, col, scale)` -- never the grid's overall size -- so a coordinate valid on a smaller
floor stays valid on a same-or-larger one. One real wrinkle found while building it: the 10 hand-authored boss
stages do NOT grow in lockstep with the procedural size ramp (F06's cell grid is bigger than F07/`stage01.json`
right after it), so a floor's own goal-cell candidates have to be filtered to whatever ALSO fits the *next*
floor's (possibly smaller) grid, or the hand-off goes out of bounds. `stage_11.json` is excluded entirely --
it's a standalone epilogue reached only via `Game::finishCombatWin()`'s boss-defeat warp, never by walking
stairs.

**Two real generator bugs found and fixed via the existing test suite, not assumed away.** First pass broke
`footprint_test`'s F7 checks (`data/footprints.json`'s "big" 2x1/2x2 monsters ending up partly on a wall or
overlapping a neighbor) -- the fix places big entities at their maze cell's own top-left corner instead of its
center tile, so their whole footprint stays inside the guaranteed-floor 2x2 cell instead of spilling past it.
Second: a pure Wilson spanning tree has exactly one path between any two cells, so a single big monster sitting
on a corridor cell could seal off everything beyond it -- some floors have SIX of these (F15). Fixed by adding
a handful of extra non-tree edges as alternate routes (never the door's own edge, which would defeat the
key/door puzzle) AND keeping big monsters off any cell that would disconnect entrance from goal even with
those loops present, checked cumulatively as each one is placed (several together can seal a route no single
one would alone). `floor_table_test`/`footprint_test` both needed small updates too, not because they were
wrong before but because the premise changed: F01 (the chain's head) now correctly has ZERO stairs_down tiles
instead of a permanently-inert one, and most floors no longer carry an `'@'` at all (arrival is now via the
matching stair), so the anti-softlock reachability check needed a real fallback start point instead of assuming
`'@'` always exists.

**Engine side: `Game::loadStage()` gained a `StageArrival` parameter** (`Fresh`/`FromBelow`/`FromAbove`,
`game.h`). `confirmStageTransition()` is the only caller that passes anything but `Fresh` -- climbing up passes
`FromBelow` (land on the new floor's stairs_down, the physical stairwell coming from below), descending passes
`FromAbove` (land on its stairs_up). Every other `loadStage()` call site (new game, rebirth, Stage Select, the
`jsGoStage` debug hook) keeps `Fresh`, which now falls back to whichever stair a floor DOES have instead of the
old crude `(1, height-2)` guess when there's no `'@'` -- meaningful since that's now true of every floor but F01.

**Documented per the owner's own request**: `docs/story/STAIR_ALIGNMENT.md` (English, the engineering reference
for the algorithm/engine change) plus a new `docs/story/STORY_DATA_SCHEMA.md` §5.3 (Chinese, matching that
document's existing voice and its own owner-instruction subsections like §5.1b).

**Verified.** Two independent Python cross-checks over all 70 regenerated files confirm zero stair mismatches
and (separately) that every floor's roster count matches what was there before. Full regression: **28/28 test
binaries pass** (`floor_table_test` and `footprint_test` updated for the new-but-correct premises above; every
other suite unaffected). Both `tower_vulkan` and `toms_web` (webgl release AND the `WEB_DEBUG` dev build)
compile clean. Browser, headless Chrome, real gameplay -- not a data-only check: warped next to F01's real
stairs_up tile and took one real step onto it (the actual `movePlayer` trigger, not a shortcut), confirmed the
transition prompt, pressed the real Enter key, and landed on F02 at the EXACT tile F01's stairs_up sat at
(screenshot-confirmed: a normal, walkable room, not a wall). Walked back down the same way and landed back on
F01 at its own stairs_up tile (the FromAbove path). Separately confirmed a Fresh jump (`jsGoStage`) to F10 --
which has no `'@'` at all -- lands on a real stair tile instead of a guess.

**Not done, on purpose:** re-tuning the size ramp itself (owner explicitly chose to keep it); a full 70-floor
manual playthrough (spot-checked F01/F02/F10 live, plus the two independent whole-tower data cross-checks
above); `data/story/floors/F26.json`'s (the SPEC file, not the regenerated `.stage.json`) `sideStoryHooks[].cell`
documentation field, which still names the pre-regeneration coordinate -- harmless since nothing reads it at
runtime (a fact already established during M9/ch_04), but now stale; left as a known, named gap rather than
silently fixed.

### 2026-09-17 — Branch reconciliation: two independent equipment-actives implementations merged

Discovered mid-task, while polishing art/UI (monster/item sprites, text size, background color) per an
owner ask: rebuilding to verify the art changes failed to LINK, with errors entirely unrelated to anything
just touched. Root cause: `git merge` had pulled in a branch from `github.com/WSLHermesAI/TOMS` -- 5 commits
built independently and in parallel to this session's own work, with no awareness of each other. The merge
itself completed cleanly (no conflict markers), but for `CMakeLists.txt` specifically it took one side's
edit wholesale for a block both sides had inserted new lines into, which silently dropped this session's
`ending_system.cpp`/`cycle_system.cpp` build entries and both their test targets, and separately reverted
this file's own "Next Step" banner to the other branch's older content.

**Reported to the owner rather than silently patched** (per standing instruction: a surprising repo state
gets a decision, not a guess) -- asked to "look at both and decide." Findings, feature by feature:

- **Endings/rebirth**: purely this session's own work; the other branch has no counterpart at all. The
  `CMakeLists.txt` drop was pure collateral damage from the conflict-resolution mechanics above, not a real
  competing design. Fixed by re-adding the three lines verbatim.
- **Equipment actives**: a genuine collision -- both branches independently built the SAME S6/M6 slice
  (the fourth of the four story-driven systems) starting from the same 2026-09-14 common ancestor, under
  different names. The incoming branch's `equipment_actives.h/.cpp` turned out to be the more complete pure
  logic (real per-active uses-per-battle + cooldown timers, `st_echo`/`st_silence` status hooks, 41 tests)
  but was never wired into combat (its own progress report said so explicitly: "Next action: wire equipment
  actives into combat"). This session's `active_system.h/.cpp` was simpler (no cooldowns, a single
  `activeUsed` bool) but WAS already wired into a real battle button (`H` key, touch, HUD, verified live
  during the original M6 slice). Decision: keep the richer logic, delete `active_system.*`, rewire
  `Game::battleTapActive()` / the HUD button / the touch hit-test onto `canUseActive()`/`useActive()`.
  `CombatState` traded `activeUsed`/kept `nextAttackGuaranteedCrit`/`nextAttackCritDamageMult` for
  `activeRuntime`/`activeStatus` (the new per-battle state) plus a new `survivalArmed` flag. This is a real
  capability upgrade, not a like-for-like swap: `surviveLethal` (`a_sanctuary_echo` -- a lethal hit leaves
  1 HP) now actually applies, in `resolveEnemyClockFire()`, which NEITHER branch had wired before today.
  `data/actives.json`/`data/equipment.json` needed no reconciliation -- the incoming branch's authored
  content (`a_qingxiao_edge`/`a_sanctuary_echo` on `qingxiao_blade`/`soul_echo_bell`) already fully replaced
  this session's placeholder `a_twin_flash`, with no dangling references either way.
- **Mobile UI scale** (`ui_root.h`/`dialogue_layout.h`, 4 commits): purely additive, kept as-is untouched.
  Confirmed it does NOT overlap with this session's own `g_uiScale` bump (see below) -- `UiRoot` scales UI
  element geometry and hit-boxes (buttons/panels), a completely different axis from `g_uiScale`, which
  scales font glyph size. They compose, they don't conflict.

**Verified.** Full regression: **30/30 test binaries pass** (`ending_system_test`/`cycle_system_test`
restored, `equipment_actives_test`/`ui_root_test`/`dialogue_layout_test` from the incoming branch all
present and green; the stray pre-reconciliation `active_system_test.exe` binary was a stale build artifact,
not a real failure). Both `tower_vulkan` and `toms_web` compile clean. Browser, headless Chrome, real
gameplay: `jsEquip("qingxiao_blade")` -> real battle -> `jsTapActive()` -- uses-left 1->0, `nextAttackGuaranteedCrit`
armed 0->1, a second same-battle tap correctly inert (stays at 0, doesn't go negative) -- confirming the
rewired combat path behaves identically to how the original M6 slice was verified, just now backed by the
richer logic.

### 2026-09-17 — Art/UI polish: procedural sprite quality, text size, background color

Owner ask, three parts, each scoped with a quick clarifying question first (no image-generation tool is
available in this environment, so real pixel art per `ART_AND_ABILITY_DESIGN.md`'s ComfyUI pipeline was
never on the table -- confirmed with the owner before starting, who chose the procedural-improvement path).

**Monster/item art.** New `tools/make_sprites.py` (Pillow-based -- a new, deliberate dependency exception;
raw pixel-plotting the outline/shading math for 30 sprites by hand is exactly what a real drawing library
is for) regenerates every one of the 30 sprites `SPRITE_ORDER` actually loads (`game_helpers.h`), replacing
`make_item_icons.py`/`make_missing_sprites.py` (kept only as historical reference, both now marked
superseded). Shared kit: a 1px dark outline stamped on every silhouette edge (readability against the
floor/wall tiles, which sit in a similar dark tonal range as several monsters), and a top-lit highlight/
shadow band within each shape instead of the old flat single-tone fills. Found and fixed a real content
bug while looking at the sheet: `boss_demonlord.png` was byte-identical to `demon.png` -- the final boss
looked exactly like a regular trash mob. Redrawn bigger, spikier (shoulder spikes + a center horn), and a
darker/richer palette than the regular demon -- one iteration was needed here: a first pass's horn color
was too light and ended up washing out the whole silhouette instead of reading as accents, caught by
re-rendering and looking at it, not assumed correct from the code. Also fixed `npc_king`'s crown and
`npc_sorcerer`'s hat, both of which turned out invisible on the first pass too (a self-intersecting
"zigzag" polygon for the crown filled as almost nothing under PIL's winding rule) -- replaced with simple
non-self-intersecting shapes. `npc_handmaiden` (previously an off-model lavender egg blob, unlike every
other NPC's actual person silhouette) is now built on the same person rig as the rest of the cast.
`assets/sprites/manifest.json` corrected to match `SPRITE_ORDER` exactly (it had been stale/incomplete --
missing `exp_up`/`scroll`/`npc_handmaiden` -- since it isn't actually read by the C++ loader, only
`SPRITE_ORDER` is).

**Text size.** `g_uiScale` (`game_text_draw.cpp`) raised from 1.0 to 1.15 -- one flat, always-on multiplier
already read by every `drawText`/`measureText` call, so every screen's text reads a bit bigger with a
one-line change. Found while touching it: the comment above this variable claimed "the browser sets 1.25 on
small screens," but nothing anywhere actually assigns it at runtime -- a real, still-open gap (small-screen
detection was never wired to this specific knob), named here rather than silently "fixed" by inventing that
wiring as a side effect of an unrelated ask.

**Background color.** The real player-facing clear color (`glClearColor`/`VkClearValue`, NOT the
`TOMS_SPLIT=1` debug-only quadrant-diagnostic colors, which stay untouched on purpose) was already the same
literal value duplicated separately in `renderer.cpp` (Vulkan) and `renderer_webgl.cpp` (WebGL) -- not
actually mismatched, just not a single source of truth. Unified into one `kBackgroundClearColor` constant
in `render_iface.h` (a header both backends already include, even though they never link into the same
binary), same deep neutral dark value as before.

**Verified.** All 30 regenerated sprites confirmed 32x32 RGBA (the atlas loader's hard requirement) via a
script check. Visual review via composited sprite sheets at each iteration (not just "the script ran without
error") -- this is what caught the boss palette and the invisible crown/hat before calling it done. Full
regression: 30/30 test binaries pass (none of these three changes touch anything a test exercises directly,
so this is a "did I break anything else" check, not a feature-specific one). Both `tower_vulkan` and
`toms_web` compile clean.

**Not done, on purpose:** real hand-authored or AI-generated pixel art (`ART_AND_ABILITY_DESIGN.md`'s own
scope, blocked on an image-generation tool this environment doesn't have); wiring small-screen detection to
`g_uiScale` (the "still-open gap" named above); per-locale background/theme variants (out of scope, never
asked for).
