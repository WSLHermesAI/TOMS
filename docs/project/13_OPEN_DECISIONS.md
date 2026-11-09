# 13 — Open decisions: what the owner has not decided yet

中文：[zh_TW/13_OPEN_DECISIONS.md](zh_TW/13_OPEN_DECISIONS.md).

Checked against the documents on 2026-11-04. This page collects them; the detail stays in the document each row links to.
**How it works** (a project rule, `CLAUDE.md`):

1. **You** write what you want in the **Answer** column, in this file or in the zh_TW copy, in any words. **Default** is what happens or stays if nobody decides; **Yours** is my recommendation. Doc 14 §5 has its own My Answer column for its questions.
2. **You** ask Claude to check the decisions.
3. **Claude** reads both copies and doc 14 §5. An answer that is unclear, incomplete or in conflict with another gets a question back.
4. **When every answer is clear, Claude asks whether to do the work**, and starts only after your yes.
5. Once the work is decided, the answer goes into the document the row links to, and the row is removed from here.

An empty **Answer** means not decided: nothing happens for that row, and its recommendation is not applied.

## 1. Blocking: nothing is built until you decide

| Question | Default | Yours | Answer | Waits on it | Where |
|---|---|---|---|---|---|
| **Battle formulas:** free expressions (`max(1, a.atk - t.def / 2)`) or fixed Dragon Quest-style formulas with numbers to tune? | Not built; waits for you | No preference |  | the shared combat rules (11 §4.5) | [11](../gameplay/11_BATTLE_DATA.md) §5 O1, O2 |
| **Levels:** keep +1 to every stat per level as well as the points, or points only? | Not built; waits for you | keep both |  | balance | [11](../gameplay/11_BATTLE_DATA.md) §5 O3 |
| **Q37 Skills:** two slots chosen before a fight, or today's one active button? Your note in doc 17 says each mastery has 5 moves chosen in the battle screen, which neither of these does. | Not built; waits for you | the 5 moves, chosen in battle, no slots (changed from "two slots" on 2026-11-04) |  | R6 | [15](../gameplay/15_BATTLE_REVAMP.md) §9, [17](../gameplay/17_WEAPONS_AND_PLAYS.md) §6 |
| **Q31 Loot grind:** its own mode, or the tower below its last floor? Can it play itself? | Not built; waits for you | its own mode from the hub, auto-battle on/off |  | R7 | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |
| **Q32 Chests:** what opens them: a new item, gold or crystals? | Not built; waits for you | a new "chest key" item |  | R7 | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |
| **Q33 Gear bonuses:** per piece, a full set, or both? | Not built; waits for you | both |  | R7 | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |
| **Q59 Two-handed sword (14 §2.5):** does this memory play replace its V swipes from doc 17, or is it only for skills and the ultimate? | Not built; waits for you | it replaces them as the normal attack |  | the two-handed sword | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |
| **Q60 Two-handed sword:** does each correct swipe hit at once with its own multiplier, or do all the swipes make one hit at the end with the last multiplier? | Not built; waits for you | each swipe hits at once: ×1.2, then ×1.4, then ×1.6 |  | the two-handed sword | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |
| **Q61 Two-handed sword:** after a wrong swipe or when time runs out, do the hits so far count, or does the whole attack fail? | Not built; waits for you | the hits so far count; the chain ends there |  | the two-handed sword | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |
| **Q62 Two-handed sword:** how big is N at each level, and how many directions (4 or 8)? Is N × X one shared time, or X for each swipe? | Not built; waits for you | N = 3 at Lv 1, up to 6; 4 directions first; one shared time |  | the two-handed sword | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |
| **Q63 Two-handed sword:** Your short note says three prompts, 0.3 s each, drawn within one second. Is it the same play as §2.5, with 1 s to answer instead of N × X (0.9 s)? Do the directions hide while answering? Are they separate swipes, or one stroke (畫完)? | Not built; waits for you | the same play: N = 3 to start, the answer time in data (1 s for N = 3), directions hidden, separate swipes |  | the two-handed sword | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |
| **Q64 Difficulty curve:** your notes go 第三層, 第五章, 第七層. With 14 bands of 5 floors (Q47), 第五章 would be F21–F25, after floor 7. Is it chapter 5, or floor 5? | Not built; waits for you | chapter 5, F21–F25: 5-hit combos need that many levels; 第三層 and 第七層 stay floors |  | W10 | [17](../gameplay/17_WEAPONS_AND_PLAYS.md) §11, §18 |
| **Q65 Epic and legendary quests:** your note says each weapon (每個武器); doc 17 wrote each class. That is 6 quests or 24. | Not built; waits for you | each class, because the moves come from the class's mastery |  | W8 | [17](../gameplay/17_WEAPONS_AND_PLAYS.md) §6, §18 |
| **Q66 Seeing resistances:** your note says the monster's description can mark them, and the card writes them after it is beaten; doc 17 wrote only once the card is held. Does the description show them before that? | Not built; waits for you | no: until then the player learns from the damage (打下去不痛) |  | W5 | [17](../gameplay/17_WEAPONS_AND_PLAYS.md) §10, §18 |
| **Q67 Gear against resistances:** your note says the player switches weapons and gear (武器跟裝備); doc 17 has only weapons. What does gear do here, and can it change in battle? | Not built; waits for you | gear changes between fights only; a piece can carry a bonus against a damage type |  | W5 | [17](../gameplay/17_WEAPONS_AND_PLAYS.md) §10, §18 |
| **Q69 Ocean in the battle:** after trying `ocean_viewer`, do you want it behind the battle? Which look, and do the fighters need something to stand on (a deck, a raft, rocks), or do they stand over the water? | Not built; waits for you | yes, with a ship's deck in front so the fighters stand on something; the look you save in the viewer |  | the ocean background | [engine 21](../engine/21_OCEAN_VIEWER.md) §7 |
| **Q73 The gun:** a fourth class with 4 types as doc 17 has, or one weapon to start? Which damage type, and what is its play? | Not built; waits for you | one gun after B7; ranged damage; its play decided then |  | part 1, B7 | [19](../gameplay/19_THREE_PART_PLAN.md) §2.3 |
| **Q74 Doc 17's order:** hold W4 (12 weapon types) and W7 (quiver, volley, charge) until the new plays are tried? | Not built; waits for you | yes; keep W5 and W6 |  | doc 17 | [19](../gameplay/19_THREE_PART_PLAN.md) §2.4 |
| **Q75 Floor goals:** which kinds first: collect, rescue, hunt, choose, timed? And which chapter is the first pattern? | Not built; waits for you | collect and hunt, in chapter 1 |  | part 2 | [19](../gameplay/19_THREE_PART_PLAN.md) §3 |
| **Q76 Tower height:** today 70 floors in 14 bands (Q47); the picture has bosses to floor 100. Grow to 100? | Not built; waits for you | keep 70; bosses at the band ends; the picture is only a look |  | part 3 | [19](../gameplay/19_THREE_PART_PLAN.md) §4 |
| **Q77 Lottery:** free crystals only, or can crystals also be bought? Are limited items only from the lottery? | Not built; waits for you | free only until the game is finished; limited items can also come from the big challenge |  | part 3 | [19](../gameplay/19_THREE_PART_PLAN.md) §4 |
| **Q78 Stronger each run:** are abilities bought after a death added to rebirth, or do they replace it? What do they cost? | Not built; waits for you | added, sharing rebirth's reset, paid in a currency kept over runs |  | part 3 | [19](../gameplay/19_THREE_PART_PLAN.md) §4 |
| **Q79 The big challenge:** "三次通關" means what: three tower clears, chapters or floors? A loss takes how many points, and can they be won back? | Not built; waits for you | every three chapters; a loss takes no points for good, only a timed weakness |  | part 1, B7 | [19](../gameplay/19_THREE_PART_PLAN.md) §2.3 |
| **Q80 Territory:** are buildings the same thing as monster cards or something new? Where is the land: the village hub? | Not built; waits for you | new items found in the tower, placed in the village hub |  | part 3 | [19](../gameplay/19_THREE_PART_PLAN.md) §4 |
| **Q81 Companions:** the picture has a toad and a rabbit beside the hero. Are they only art, or do monsters join you and fight? | Not built; waits for you | only art on this screen for now |  | part 3 | [19](../gameplay/19_THREE_PART_PLAN.md) §4 |
| **Q22 Test scenarios:** where are they shared (cloud bucket, Google Drive, own server)? | Not built; waits for you | a cloud bucket (R2) |  | downloads (W39) | [09](../gameplay/09_DOWNLOADS_AND_DLC.md) §7 |
| **Q23 Uploads:** each developer's own key, or one shared key? | Not built; waits for you | each their own |  | downloads (W39) | [09](../gameplay/09_DOWNLOADS_AND_DLC.md) §7 |
| **Q24 DLC:** may a pack change existing content, or only add? | Not built; waits for you | add only |  | DLC (W39) | [09](../gameplay/09_DOWNLOADS_AND_DLC.md) §7 |
| **Q25 DLC:** our storage or the stores' DLC? Paid or free? | Not built; waits for you | our storage, free first |  | DLC (W39) | [09](../gameplay/09_DOWNLOADS_AND_DLC.md) §7 |
| **Q26 Team workflow:** the board still says "wait for the owner's description", but [11_TEAM_PLAN](11_TEAM_PLAN.md) now exists. Is that plan the answer? | wait for you | No preference |  | W40 | [board](progress_report/PROGRESS_REPORT.md), [team plan](11_TEAM_PLAN.md) |
| **Image stages, question 2:** one id per event, or a few "event slot" ids? | Not built; waits for you | one id per event |  | image stages | [04](../gameplay/04_IMAGE_STAGES.md) §6 |
| **Image stages, question 3:** convert all 70 floors at once, or as they are edited? | Not built; waits for you | all at once |  | image stages | [04](../gameplay/04_IMAGE_STAGES.md) §6 |
| **Languages for release:** which are required? That makes an untranslated text a warning or an error. (Q51 settled only "for now": zh_TW and English.) | Not built; waits for you | a warning |  | release | [event editor 02](../gameplay/event_editor/02_EDITOR_DESIGN.md) §6 |
| **Design-map cleanups:** do them, and in what order? Includes deciding on chapter beats. None blocks today's work. | not done | No preference |  | code structure | [19](../engine/19_DESIGN_MAP.md) |
| **Delete `combat.json` and `story.json`?** Nothing reads them. | kept | No preference |  | cleanup | [11](../gameplay/11_BATTLE_DATA.md) §5 O4, [19](../engine/19_DESIGN_MAP.md) |
| **The 3D fighter view:** build it later, or drop it? You said "try 3D later"; a look set to `"3d"` falls back to 2D. | not built | No preference |  | 3D fighters | [18](../gameplay/18_FIGHTER_VIEWS.md) |

## 2. Already in the game with my proposal: answer only to change it

These were not answered, so the proposal was built. Nothing waits on them.

| Question | Default (what the game does now) | Yours | Answer | Where |
|---|---|---|---|---|
| **Q27** New plays replace the point track, or add to it? | replace, weapon by weapon; the old play stays as a setting | Keep it |  | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |
| **Q28** Bow: drag or hold? Do targets move? | drag back and let go | Keep it |  | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |
| **Q29** Sword: how are the directions shown? | shown, then hidden (reworked since by doc 16) | Keep it |  | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |
| **Q30** Do magic circles move from the hammer to the staff? | the staff has circles; the hammer is not built | Keep it |  | [14](../gameplay/14_WEAPON_PLAY_AND_LOOT.md) §5 |
| **Q34** How strong is the feel? | medium, in `battle.json`, with the reduce-effects setting | Keep it |  | [15](../gameplay/15_BATTLE_REVAMP.md) §9 |
| **Q35** Grades and the S bonus? | S / A / B / C; S gives +20 % gold and a better card chance | Keep it |  | [15](../gameplay/15_BATTLE_REVAMP.md) §9 |
| **Q36** The screen layout? | the play over the monsters; the guard bar only as an option | Keep it |  | [15](../gameplay/15_BATTLE_REVAMP.md) §9 |
| **Q38** Which revamp step first? | R1 + R2 first (R1–R5 are built) | Keep it |  | [15](../gameplay/15_BATTLE_REVAMP.md) §9 |
| **Q39** Sword's input window? | the last 0.5 s of each 1 s | Keep it |  | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |
| **Q40** Staff: a wrong circle? | a miss; the play goes on | Keep it |  | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |
| **Q41** Bow's charge limit? | the arrow goes by itself at full charge | Keep it |  | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |
| **Q42** Guard grades inside the "!"? | the first 40 % is a parry | Keep it |  | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |
| **Q43** How a skill is used? | Star Seal as the staff's ultimate first; skills from the list later | Keep it |  | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |
| **Q44** The training ground spends cards. OK? | 1 card a run, 3 runs a day, ×2 EXP and weapon uses | Keep it |  | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |
| **Q45** A move not finished in time? | part of the damage: done / total | Keep it |  | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |
| **Q46** Camera close-up? | per move in data; the setting shortens it | Keep it |  | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §9 |
| **Q68** A special guard: your note binds it to the monster; is it fine on the move? | on the move (`moves.json` `guardPlay`), used by the lich and the ore golem | Keep it |  | [16](../gameplay/16_SKILLS_AND_PLAYS.md) §4.3, §9 |
| **Q82** Which monsters are **big** (their attacks fly at the hero)? | bosses, and `"big": true` in enemies.json: only the stone golem | Mark the large ones you want (golem, demon?) |  | [20](../gameplay/20_BATTLE_EFFECTS.md) §5 |
| **Q83** The **target line** "目前攻擊目標": only with two or three monsters, or also with one? | only with two or three | Keep it |  | [20](../gameplay/20_BATTLE_EFFECTS.md) §6 |
| **Q84** The **staff's spells grow** along the chain: how fast? | k^1.6: of five, the first about 3 %, the last about 41 % of the play | Keep it; lower it (1.2) if the first spells feel useless |  | [20](../gameplay/20_BATTLE_EFFECTS.md) §3 |
| **Q85** The **"!" before tapping** the staff's circles is off. Keep it off? | off (`circleWarnMs` 0) | Keep it |  | [20](../gameplay/20_BATTLE_EFFECTS.md) §2 |
| **Q86** The 9 new **sounds** are synthesised placeholders. Replace them with recorded ones? | placeholders | Replace when you have recorded sounds; same names, OGG |  | [20](../gameplay/20_BATTLE_EFFECTS.md) §8 |
| **Q87** The staff's damage grows **within one attack**. Should it also grow with the **combo across attacks**? | within one attack only | Keep it; the sprays already grow with the combo |  | [20](../gameplay/20_BATTLE_EFFECTS.md) §3 |

## 3. Not questions, but waiting for you

| What | Default | Yours | Answer | Where |
|---|---|---|---|---|
| **Your verdict on the hero's look** after the tuning (bow, sword, staff) | Waiting for you | — |  | `Build/art_out/hero_*/gif/`, [20](../engine/20_CHARACTER_ART.md) §5 |
| **Monster art:** the concept pictures and their picks. `tools/art/make_monster_concepts.py` (48 monsters in 16 teams) is not committed | Waiting for you | — |  | `Build/art_out/monster_concepts/teams.png` |
| **What to build next** from doc 17's order: W1–W3 are built (§19), W4 is next, then W5–W10 | Waiting for you | — |  | [17](../gameplay/17_WEAPONS_AND_PLAYS.md) |
| **Try the plays** in the game and say how they feel | Waiting for you | — |  | [15](../gameplay/15_BATTLE_REVAMP.md), [16](../gameplay/16_SKILLS_AND_PLAYS.md) |
| **`tools\test.cmd all`:** not run since the C++ rename; most tests are switched off | Waiting for you | — |  | [08](08_TESTS.md) |
| **The `smoke.battle` reference image:** make it once you like the battle screen's look | Waiting for you | — |  | [08](08_TESTS.md) |
| **Publish the web build** (needs your GitHub sign-in, W18) | Waiting for you | — |  | [07](07_PUBLISH_GITHUB_PAGES.md) |

The rest of the board's **Waiting to finish** list (W-numbers) is mostly tests on other devices, not decisions:
[progress_report/PROGRESS_REPORT.md](progress_report/PROGRESS_REPORT.md).

**Left out as settled:** the questions in [06](06_BUILD_ANDROID.md) §6 (built as option B), and the floor-list question in event
editor 02 §6 (the generator it depends on was removed, Q6).
