# 13 — Open decisions: what the owner has not decided yet

中文：[zh_TW/13_OPEN_DECISIONS.md](zh_TW/13_OPEN_DECISIONS.md).

Checked against the documents on 2026-11-04. This page collects them; the detail stays in the document each row links to.
**How it works** (a project rule, `CLAUDE.md`):

1. **You** write what you want in the **My Answer** column, in this file or in the zh_TW copy, in any words. Doc 14 §5 has the same column for its questions.
2. **You** ask Claude to check the decisions.
3. **Claude** reads both copies and doc 14 §5. An answer that is unclear, incomplete or in conflict with another gets a question back.
4. **When every answer is clear, Claude asks whether to do the work**, and starts only after your yes.
5. Once the work is decided, the answer goes into the document the row links to, and the row is removed from here.

An empty **My Answer** means not decided: nothing happens for that row, and its proposal is not applied.

## 1. Blocking: nothing is built until you decide

| Question | My proposal | Waits on it | Where | My Answer |
|---|---|---|---|---|
| **Battle formulas:** free expressions (`max(1, a.atk - t.def / 2)`) or fixed Dragon Quest-style formulas with numbers to tune? | none yet | the shared combat rules (11 §4.5) | [11](../gameplay/11_BATTLE_DATA.md) §5 O1, O2 |  |
| **Levels:** keep +1 to every stat per level as well as the points, or points only? | keep both | balance | [11](../gameplay/11_BATTLE_DATA.md) §5 O3 |  |
| **Q37 Skills:** two slots chosen before a fight, or today's one active button? Your note in doc 17 says each mastery has 5 moves chosen in the battle screen, which neither of these does. | the 5 moves, chosen in battle, no slots (changed from "two slots" on 2026-11-04) | R6 | [15](../gameplay/15_BATTLE_REVAMP.md) §9, [17](../gameplay/17_WEAPONS_AND_PLAYS.md) §6 |  |
| **Q31 Loot grind:** its own mode, or the tower below its last floor? Can it play itself? | its own mode from the hub, auto-battle on/off | R7 | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q32 Chests:** what opens them: a new item, gold or crystals? | a new "chest key" item | R7 | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q33 Gear bonuses:** per piece, a full set, or both? | both | R7 | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q59 Two-handed sword (14 §2.5):** does this memory play replace its V swipes from doc 17, or is it only for skills and the ultimate? | it replaces them as the normal attack | the two-handed sword | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |  |
| **Q60 Two-handed sword:** does each correct swipe hit at once with its own multiplier, or do all the swipes make one hit at the end with the last multiplier? | each swipe hits at once: ×1.2, then ×1.4, then ×1.6 | the two-handed sword | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |  |
| **Q61 Two-handed sword:** after a wrong swipe or when time runs out, do the hits so far count, or does the whole attack fail? | the hits so far count; the chain ends there | the two-handed sword | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |  |
| **Q62 Two-handed sword:** how big is N at each level, and how many directions (4 or 8)? Is N × X one shared time, or X for each swipe? | N = 3 at Lv 1, up to 6; 4 directions first; one shared time | the two-handed sword | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |  |
| **Q63 Two-handed sword:** Your short note says three prompts, 0.3 s each, drawn within one second. Is it the same play as §2.5, with 1 s to answer instead of N × X (0.9 s)? Do the directions hide while answering? Are they separate swipes, or one stroke (畫完)? | the same play: N = 3 to start, the answer time in data (1 s for N = 3), directions hidden, separate swipes | the two-handed sword | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |  |
| **Q64 Difficulty curve:** your notes go 第三層, 第五章, 第七層. With 14 bands of 5 floors (Q47), 第五章 would be F21–F25, after floor 7. Is it chapter 5, or floor 5? | chapter 5, F21–F25: 5-hit combos need that many levels; 第三層 and 第七層 stay floors | W10 | [17](../gameplay/17_WEAPONS_AND_PLAYS.md) §11, §18 |  |
| **Q65 Epic and legendary quests:** your note says each weapon (每個武器); doc 17 wrote each class. That is 6 quests or 24. | each class, because the moves come from the class's mastery | W8 | [17](../gameplay/17_WEAPONS_AND_PLAYS.md) §6, §18 |  |
| **Q66 Seeing resistances:** your note says the monster's description can mark them, and the card writes them after it is beaten; doc 17 wrote only once the card is held. Does the description show them before that? | no: until then the player learns from the damage (打下去不痛) | W5 | [17](../gameplay/17_WEAPONS_AND_PLAYS.md) §10, §18 |  |
| **Q67 Gear against resistances:** your note says the player switches weapons and gear (武器跟裝備); doc 17 has only weapons. What does gear do here, and can it change in battle? | gear changes between fights only; a piece can carry a bonus against a damage type | W5 | [17](../gameplay/17_WEAPONS_AND_PLAYS.md) §10, §18 |  |
| **Q69 Ocean in the battle:** after trying `ocean_viewer`, do you want it behind the battle? Which look, and do the fighters need something to stand on (a deck, a raft, rocks), or do they stand over the water? | yes, with a ship's deck in front so the fighters stand on something; the look you save in the viewer | the ocean background | [engine 21](../engine/21_OCEAN_VIEWER.md) §7 |  |
| **Q22 Test scenarios:** where are they shared (cloud bucket, Google Drive, own server)? | a cloud bucket (R2) | downloads (W39) | [09](../gameplay/09_DOWNLOADS_AND_DLC.md) §7 |  |
| **Q23 Uploads:** each developer's own key, or one shared key? | each their own | downloads (W39) | [09](../gameplay/09_DOWNLOADS_AND_DLC.md) §7 |  |
| **Q24 DLC:** may a pack change existing content, or only add? | add only | DLC (W39) | [09](../gameplay/09_DOWNLOADS_AND_DLC.md) §7 |  |
| **Q25 DLC:** our storage or the stores' DLC? Paid or free? | our storage, free first | DLC (W39) | [09](../gameplay/09_DOWNLOADS_AND_DLC.md) §7 |  |
| **Q26 Team workflow:** the board still says "wait for the owner's description", but [11_TEAM_PLAN](11_TEAM_PLAN.md) now exists. Is that plan the answer? | wait for you | W40 | [board](progress_report/PROGRESS_REPORT.md), [team plan](11_TEAM_PLAN.md) |  |
| **Image stages, question 2:** one id per event, or a few "event slot" ids? | one id per event | image stages | [04](../gameplay/04_IMAGE_STAGES.md) §6 |  |
| **Image stages, question 3:** convert all 70 floors at once, or as they are edited? | all at once | image stages | [04](../gameplay/04_IMAGE_STAGES.md) §6 |  |
| **Languages for release:** which are required? That makes an untranslated text a warning or an error. (Q51 settled only "for now": zh_TW and English.) | a warning | release | [event editor 02](../gameplay/event_editor/02_EDITOR_DESIGN.md) §6 |  |
| **Design-map cleanups:** do them, and in what order? Includes deciding on chapter beats. None blocks today's work. | not done | code structure | [19](../engine/19_DESIGN_MAP.md) |  |
| **Delete `combat.json` and `story.json`?** Nothing reads them. | kept | cleanup | [11](../gameplay/11_BATTLE_DATA.md) §5 O4, [19](../engine/19_DESIGN_MAP.md) |  |
| **The 3D fighter view:** build it later, or drop it? You said "try 3D later"; a look set to `"3d"` falls back to 2D. | not built | 3D fighters | [18](../gameplay/18_FIGHTER_VIEWS.md) |  |

## 2. Already in the game with my proposal: answer only to change it

These were not answered, so the proposal was built. Nothing waits on them.

| Question | What the game does now | Where | My Answer |
|---|---|---|---|
| **Q27** New plays replace the point track, or add to it? | replace, weapon by weapon; the old play stays as a setting | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q28** Bow: drag or hold? Do targets move? | drag back and let go | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q29** Sword: how are the directions shown? | shown, then hidden (reworked since by doc 16) | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q30** Do magic circles move from the hammer to the staff? | the staff has circles; the hammer is not built | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q34** How strong is the feel? | medium, in `battle.json`, with the reduce-effects setting | [15](../gameplay/15_BATTLE_REVAMP.md) §9 |  |
| **Q35** Grades and the S bonus? | S / A / B / C; S gives +20 % gold and a better card chance | [15](../gameplay/15_BATTLE_REVAMP.md) §9 |  |
| **Q36** The screen layout? | the play over the monsters; the guard bar only as an option | [15](../gameplay/15_BATTLE_REVAMP.md) §9 |  |
| **Q38** Which revamp step first? | R1 + R2 first (R1–R5 are built) | [15](../gameplay/15_BATTLE_REVAMP.md) §9 |  |
| **Q39** Sword's input window? | the last 0.5 s of each 1 s | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q40** Staff: a wrong circle? | a miss; the play goes on | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q41** Bow's charge limit? | the arrow goes by itself at full charge | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q42** Guard grades inside the "!"? | the first 40 % is a parry | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q43** How a skill is used? | Star Seal as the staff's ultimate first; skills from the list later | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q44** The training ground spends cards. OK? | 1 card a run, 3 runs a day, ×2 EXP and weapon uses | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q45** A move not finished in time? | part of the damage: done / total | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q46** Camera close-up? | per move in data; the setting shortens it | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q68** A special guard: your note binds it to the monster; is it fine on the move? | on the move (`moves.json` `guardPlay`), used by the lich and the ore golem | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §4.3, §9 |  |

## 3. Not questions, but waiting for you

| What | Where | My Answer |
|---|---|---|
| **Your verdict on the hero's look** after the tuning (bow, sword, staff) | `Build/art_out/hero_*/gif/`, [20](../engine/20_CHARACTER_ART.md) §5 |  |
| **Monster art:** the concept pictures and their picks. `tools/art/make_monster_concepts.py` (48 monsters in 16 teams) is not committed | `Build/art_out/monster_concepts/teams.png` |  |
| **What to build next** from doc 17's order W1–W10 | [17](../gameplay/17_WEAPONS_AND_PLAYS.md) |  |
| **Try the plays** in the game and say how they feel | [15](../gameplay/15_BATTLE_REVAMP.md), [16](../gameplay/16_SKILLS_AND_PLAYS.md) |  |
| **`tools\test.cmd all`:** not run since the C++ rename; most tests are switched off | [08](08_TESTS.md) |  |
| **The `smoke.battle` reference image:** make it once you like the battle screen's look | [08](08_TESTS.md) |  |
| **Publish the web build** (needs your GitHub sign-in, W18) | [07](07_PUBLISH_GITHUB_PAGES.md) |  |

The rest of the board's **Waiting to finish** list (W-numbers) is mostly tests on other devices, not decisions:
[progress_report/PROGRESS_REPORT.md](progress_report/PROGRESS_REPORT.md).

**Left out as settled:** the questions in [06](06_BUILD_ANDROID.md) §6 (built as option B), and the floor-list question in event
editor 02 §6 (the generator it depends on was removed, Q6).
