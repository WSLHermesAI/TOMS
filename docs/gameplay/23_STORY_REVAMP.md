# 23. The story and events revamp: the orphan who climbs the tower (2026-11-20)

中文：[zh_TW/23_STORY_REVAMP.md](zh_TW/23_STORY_REVAMP.md).

The owner (2026-11-20): the battle is "6 of 10", the story and events "1 of 10". Most events are a hello and nothing to do.
The game needs a real RPG feeling: a funny story, someone to save, a reason for revenge, events worth playing. The owner
asked which pitch of [rpg_story_concept_book.html](rpg_story_concept_book.html) is closest to the game, and chose the
direction below. **This is a plan; S1 (the tools), chapters 1–5 (§4.1–4.5) are built, chapters 6–7 are not.** The open questions are in §9 and in
[13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md).

## 1. Where the game is now

| | Count | What the player gets |
|---|---|---|
| NPC conversations (`assets/data/dialogue/`) | 80 | **63 are a greeting only** ("Good day, traveller. I'm the farmer." → goodbye) |
| Tower events (`assets/data/events/pool_*.json`) | 140 | **126 are a title and a line of text**, nothing to do |
| The written story (`story.json`, `story/`) | 10 chapters × 7 floors, 10 seals, 10 endings, rebirth cycles | A cultivation-novel plot (qi, golden core, inner demon; a past life betrayed by the senior brother, the final boss). About 7 floors are playable; almost none of the plot reaches the player |

The systems under it are good and stay: dialogue nodes with choices and actions, NPC lines with conditions, story flags,
counters and vars, side stories, the endings evaluator, the event editor, the village hub, the store, the forge, the
monster cards.

## 2. Which pitch is closest (the owner's choice: B + Liora)

| Pitch | Matches the game | Does not match |
|---|---|---|
| A. Save the Last Princess | Princess Liora is held at the tower's top; a light tone | The hero has no reason of his own to climb |
| **B. The Orphan Who Became a God** | The hero is a young boy; the village already has a **farmer and a farmer's wife** (his adoptive parents), an elder, a weapon master; the **monster cards** are close to its Monster Soul system; one opening gives both "save someone" and "revenge" | Needs a reason the parents are in the tower (the raiders take them up it) |
| C. The Hero Who Wasn't Supposed to Exist | Closest to **today's written story** (past life, betrayal, his own past self as a boss) | Dark and complicated; never reaches the player |
| D. My Little Sister Is the Final Boss | An escort and protect feeling | No sister; a new cast |
| E. The Villain Is the Player | — | A meta story fights a tower-climbing loop |

**Chosen (the owner, 2026-11-20): B, keeping Princess Liora** with A's twist (she is not helpless: the strongest person in
the kingdom, her power sealed). The concept book's "Luna, the mysterious girl" role goes to Liora.

## 3. The story

**Premise.** An orphan boy (working name **Leo**, Q110) lives happily with the farmer couple who raised him. One night
masked soldiers, **the Collectors**, burn the village and carry his parents and half the villagers up the **Sorcerer's
Tower**. Their captain (working name **Grey Mask**, Q111) says: *"Find the child. The prophecy says he is the last key."*
Leo wakes among fallen monsters: something in him woke up. He climbs the tower to bring his family home, and to make the
one who burned the village pay.

**The twist, step by step.** His parents knew he is the last of the **Godborn**, an old race that can take in the power
of the creatures it defeats, and hid him so he could have a normal childhood. The Collectors gather gifted children for
whoever rules the top of the tower. The Sorcerer King **Vorkath** (kept from today's lore) is not what the village thinks:
he sealed the **Star of Peace** to hold back something worse, an old god that wants a Godborn body. Liora, held at the top,
holds the key to that seal.

**Tone.** The main plot is sincere (family, loss, revenge or mercy). The comedy comes from the cast, the side quests and
the monsters, never from mocking the main plot. The emotional payoff, when he finds his mother: *"We never wanted you to
become a god. We just wanted you to have a happy childhood."*

**Themes.** Family is made by love, not blood. Power does not decide what a person is worth. Being a hero means choosing
whom to protect, even the one you came to take revenge on.

### 3.1 The cast (story companions only: battles stay the hero's, the owner's choice)

| Who | Role | Data today |
|---|---|---|
| **Leo** | the hero; stubborn, funny, always hungry; takes in monster powers (the monster cards) | the player |
| **The farmer and the farmer's wife** | his adoptive parents; taken in chapter 1, found in chapter 6 | `farmer`, `farmer_wife` (greeting only) |
| **The elder**, **the weapon master** | the village's survivors; the base, the first weapon | `villager_elder`, `weapon_master` |
| **Princess Liora** | held at the top; met on the way (letters, a voice, a mirror); her power sealed; the key to the Star | `princess`, `princess_liora` |
| **Kai** | a royal knight sent to "contain the dangerous child"; a rival, then the closest friend | `knight_errant` |
| **Pip** | a greedy little monster merchant; joins because "this kid will be rich"; runs the shop, gives the comic quests | `chest_peddler` / `peddler` |
| **Grey Mask** | the Collectors' captain who burned the village: the revenge target; met again and again | new |
| **Vorkath** | the Sorcerer King; the enemy at first, more complicated later | `demonlord_vorkath` |

## 4. Seven chapters, about 5 floors each (35 floors)

Each chapter uses the stage looks already made (`assets/data/stages/`).

| # | Chapter | Floors | Look | Story | The player does |
|---|---|---|---|---|---|
| 1 | **The Burning Village** | F01–F05 | Village Outskirts | A peaceful morning (chores, the weapon master's trial), the night raid, the parents taken, his power wakes, the elder sends him up | the tutorial; 2 rescues; first boss: a Collector sergeant |
| 2 | **The Forest Path** | F06–F10 | Forest Path | Kai comes to arrest him, Pip tries to sell him a sword; first trace of the parents (his mother's scarf) | the quest board opens; first choice (Kai) |
| 3 | **The Gate of Lies** | F11–F15 | Gatehouse, Library | The Collectors' **ledger of children**: the village was not random; a letter from Liora | an investigation quest; a library puzzle |
| 4 | **The Buried Ones** | F16–F20 | Crypt, Cavern | Old monsters recognise a Godborn; what his power is; why his parents hid him | monster-soul quests; a hard boss |
| 5 | **The Collectors' Barracks** | F21–F25 | Barracks | **Grey Mask** at last; the captured villagers; revenge or mercy | a mass rescue; **the big choice** |
| 6 | **The Sanctum** | F26–F30 | Sanctum | **His parents found**; Liora's true power; the old god's voice | the family scene; Liora's quest |
| 7 | **The Final Choice** | F31–F35 | Throne Antechamber, Throne, Star of Peace | Vorkath; the old god; what to do with the Star and with his own power | the last bosses; the ending |

### 4.1 Chapter 1 as built (S2, 2026-11-20)

| Where | What happens | Data |
|---|---|---|
| New Game | **The morning at home**: Mom, Dad, the stew, the birthday; Dad sends Leo to the weapon master | `dialogue/prologue_morning.json` (opened by the start story when it exists) |
| The trial | The weapon master's birthday present: try the weapons on the toad, keep one | `weapon_master.json` (its intro rewritten) |
| After the choice | **The night raid**: the Collectors, Grey Mask's "the last key", the parents taken, the blow, the power wakes, the elder sends Leo up. The village opens; the main quest and the baker's start; the last answer is mercy or vengeance | `prologue_night.json` |
| F01 | Tobi in a barrel: **rescue** (he goes home); Mom's hairpin: **scene**; the Collectors' order "the marked one, alive": **scene** | `npcs/village_boy`, `ev_c1_hairpin`, `ev_c1_collector_order` |
| F02 | **Sir Wobbles**, the slime knight in a bucket: **comic** (a potion) | `ev_c1_sir_wobbles` |
| F03 | The baker tied to a post: **rescue** (the bakery opens, the elder's quest); the shy shepherd girl: **talk N** (the 5th talk: the orchard key) | `ev_c1_rescue_baker`, `ev_c1_shepherd_key` |
| F04 | The weapon master's whetstone: **quest**; the hungry Collector soldier: **choice** (spare: mercy; threaten: vengeance) | `ev_c1_whetstone`, `ev_c1_hungry_soldier` |
| F05 | The old orchard's gate (the shepherd girl's key): the healer and the sheep: **rescue** (the healer opens); **Grey Mask on the stairs**, then the floor boss | `gate:key_orchard`, `ev_c1_rescue_healer`, `ev_c1_greymask` |
| The village | The elder (the baker's reward, the tower, **the chapter's end**: "your mother walked in holding a glowing baby"); the weapon master's yard (his quest); the bakery (bread for 8 gold); the healer (healing for 10 gold); Tobi's barrel (hints) | `hub.json`, `villager_elder`, `weapon_master_village`, `baker`, `healer`, `village_boy_home` |

Three quests: **Bring Mom and Dad Home** (the main one), **Bring the Baker Home**, **The Weapon Master's Whetstone**. Three new
counters: mercy, vengeance, trust (`story/counters.json`). The events are in `events/pool_ch01.json`. Code: a choice that
opens another dialogue keeps it open; a rescued villager leaves the floor; New Game opens `prologue_morning` first;
scenarios can expect a quest's state (`"missions"`). Tests: `story_ch1_prologue`, `_night`, `_tobi`, `_shepherd_key`,
`_orchard`, `_soldier`, `_whetstone`, `_end`.

**Cleaned up (2026-11-20):** the text-only events left on F02, F03 and F05 (the chapel dust, the ledger, the well, the
merchant's echo) and F02's old villager are gone; F01–F10 have new names and entry lines; `story.json` and the chapter files
say chapter 1 is F01–F05 (boss F05) and chapter 2 F06–F10 (boss F10); F06 and F07 belong to chapter 2. Kept: the wall
caches (HP and gold), the bell chain (a working key puzzle), the owner's own "beat the first floor's boss" event. The F05
boss is the Gravewarden with the boss introduction. Still old: the floor counter "/70" (the tower is still 70 floors in
the floor table), the other 4 languages (Q116).

### 4.2 Chapter 2 as built (2026-11-20)

| Where | What happens | Data |
|---|---|---|
| F06 the Root Garden | **Kai**, a royal knight, comes to arrest "the dangerous child": a calm answer (trust) or a dare (he trips); he gives his **quest**: beat three forest wolves. **Mom's scarf** with Dad's note "Leo, eat properly": **scene** | `npcs/knight_errant` (c2_kai_meet, then c2_kai_watch), `ev_c2_scarf` |
| F07 (the hand-made map) | **Pip**, a greedy little monster merchant with a monocle: an "investment" (10 gold) or "sounds like a scam"; either way he **moves to the village** and opens a stall (the store, the chest game) | `npcs/chest_peddler` (c2_pip), `hub_pip` (pip_shop) |
| F08 the Misty Wood | The blacksmith in a cage ("kick it, the lock's junk"): **rescue** (the forge: an attack gem for 30 gold) | `npcs/blacksmith` (c2_blacksmith), `hub_blacksmith` |
| F09 the Charcoal Path | The cartographer drawing an escape route: **rescue** (who is still held; a girl singing at the top); **Sir Wobbles** again, toasted, worried about his king: **comic** | `npcs/cartographer`, `hub_cartographer`, `ev_c2_wobbles` |
| F10 the Snare Wood | **Kai caught in a snare**: free him (trust; he joins; his quest paid if done) or walk away (less trust; he frees himself and joins, furious): **choice**. **A letter from "L"**: "Don't trust Grey Mask. I am waiting at the top." | `ev_c2_kai_snare`, `ev_c2_liora_letter` |
| The village | The elder after F10's boss: Kai and Pip, and the letter: "only one L in the old stories: Princess Liora" (c2.done) | `villager_elder` |

The quest: **Kai's Test: Three Wolves** (`m_c2_wolves`, a `defeat` objective). The rescue action takes a notice (`"text"`:
Pip "moved into the village"). Tests: `story_ch2_kai`, `_pip`, `_pip_shop`, `_snare`, `_end`.

### 4.3 Chapter 3 as built (2026-11-20)

| Where | What happens | Data |
|---|---|---|
| F11 the Outer Gate | The innkeeper tied to a stool, worried about her stew: **rescue** (the inn: stew for 5 gold, L's letters to read again). **The ledger's first page** in a brazier: "Village: Oakhollow": the **quest** The Ledger of Children starts | `npcs/innkeeper`, `hub_inn`, `ev_c3_page1` |
| F12 the Toll Hall | **Page two**: "the one marked child, to the Master himself". **Pip** sells a 99% accurate library map ("the exit is the entrance"): **comic** | `ev_c3_page2`, `ev_c3_pip_map` |
| F13 the Clerks' Rooms | The scholar buried under books ("not that one, it's a first edition!"): **rescue** (the village: the Godborn). **The clerk who wrote the names**: spare him (mercy), make him write a confession for the king (trust; Kai keeps it), or smash his inkwell (vengeance): **choice** | `npcs/scholar`, `hub_scholar`, `ev_c3_clerk` |
| F14 (the hand-made map) | **The talking book's riddle** ("what gets dirtier the more you wash?" water; one second try): the **puzzle** gives the last page. **The whole ledger** at once (the var `ledger_pages` reaching 3): "the marked child, Leo", and one line above: **Mira, Kai's little sister** | `ev_c3_riddle`, `ev_c3_ledger_whole` |
| F15 the Gatehouse Roof | **Liora's second letter**: the Master is not the Sorcerer King but something older; the parents are alive and brave; Grey Mask was once one of the village. The **Grimoire Warden** (a stronger Grimoox, with the boss introduction) | `ev_c3_letter2`, `enemies.json` `grimoox_warden` |
| The village | The elder: twelve years ago a young man came with Leo's mother, then left; ask the scholar about the Godborn (c3.done; the ledger quest paid: 30 gold, 30 EXP, a DEF gem) | `villager_elder` |

F11–F14 belong to chapter 3 now (the castle look on F11–F13 and F15). Scenarios can now set event variables (`"vars"`)
and quests in a state (`"missions"`). Tests: `story_ch3_ledger`, `_clerk`, `_rescues`, `_end`.

### 4.4 Chapter 4 as built (2026-11-21)

| Where | What happens | Data |
|---|---|---|
| F16 the Stair Down | The dwarf the Collectors made dig "until he hit the sleeping things": **rescue** (the dwarf's anvil: a DEF gem for 30 gold). **The voice in the wall**: "little light... you smell like the old masters": **scene** | `npcs/dwarf`, `hub_dwarf`, `ev_c4_whisper` |
| F17 the Sleeping Hall | **The Slime King in chains** ("We are so ticklish"): free him with care (mercy) or ask what's in it for you (Pip: "Deal!"); Sir Wobbles dubs Leo "Sir Leo of the Bucket", two potions: **comic** (the payoff of his thread) | `ev_c4_slime_king` |
| F18 the Wyrm's Den | **The stone wyrm**, a Buried One with glowing eyes: "Child of the Godborn. The last one." Its **quest**: defeat three crystal spiders | `ev_c4_wyrm`, `m_c4_wyrm` |
| F19 the Dreaming Cave | **The vision** (only after the test; before, the tile waits): a snowy night twelve years ago, Mom running with the glowing baby, and a youth with a scar on his chin: "Sis, I'll protect him." The wyrm: "She hid you in an ordinary village. That was her gift." The test paid | `ev_c4_vision` (`requires` missionComplete) |
| F20 the Deep Gate | The archer scout from the capital: **rescue** (the lookout: Grey Mask sits alone at night with a child's wooden horse; he flinches at the name "Bran"). **The mural** of the Godborn and the Buried Ones against a black hand from the sky: **scene**. **The Sleeping Colossus** (a stronger Cragtortle, with the boss introduction) | `npcs/archer_scout`, `hub_archer`, `ev_c4_mural`, `buried_colossus` |
| The village | The elder hears about the youth with the scar: "**Bran. Your mother's younger brother.** He adored you... ten years ago he left for the capital and never came back." (c4.done) | `villager_elder` |

F16–F20 belong to chapter 4 (the crypt look on F16–F17, the cave on F18–F20). Scenarios can expect a flag left unset
(`"notFlags"`). Tests: `story_ch4_wyrm`, `_vision`, `_vision_locked`, `_slime_king`, `_dwarf`, `_end`.

### 4.5 Chapter 5 as built (2026-11-21)

| Where | What happens | Data |
|---|---|---|
| F21 the Barracks Gate (the hand-made map) | **The ambush**: the soldier spared in chapter 1 warns of the nets and gives a potion; otherwise the nets fall (-30 HP): a **payoff**. **Pip's** "barely worn" Collector uniform (10 gold; it is pink): **comic** | `ev_c5_ambush`, `ev_c5_pip_uniform` |
| F22 the Drill Yard | **The sleeping guard** ("Mom, five more minutes"): the third talk drops the cell keys: **talk N** | `npcs/town_guard`, `ev_c5_guard_key` |
| F23 the Cells | The cell gate (the keys): the miller, the weaver, the fisherman and the tailor: **mass rescue** (the village square); if the Slime King was freed, **Sir Wobbles' slime army** glues the guards down. "Your parents were taken to the Sanctum" | `gate:key_cells`, `ev_c5_cells`, `hub_square` |
| F24 the Captain's Quarters | **Mira**, digging out with a spoon: the reunion with Kai (trust), she goes to the village. **The Collectors' captain**: with the clerk's confession Kai reads "take the Collectors' own children next" and the soldiers lay down their arms (potions); without it, a fight (-20 HP): a **payoff** | `ev_c5_mira`, `hub_mira`, `ev_c5_captain` |
| F25 Grey Mask's Hall | The wooden horse Leo chewed as a baby. **Grey Mask** (the boss, with the boss introduction; the Chronoknight's look for now), the only way to the stairs. Then **the unmasking**: Bran, the mother's brother; the Master threatened to burn the village, he served to protect them, and failed. **The big choice**: kill him (vengeance + 2; Leo takes in his power, ATK + 3), send him home to make amends (mercy + 2; he rebuilds the burned houses), or the king's justice (trust + 2) | `ev_c5_horse`, `grey_mask`, `ev_c5_unmask`, `hub_bran` |
| The village | The elder's line for each choice (c5.done) | `villager_elder` |

The choice is the one §6's endings read (`c5.bran_killed`, `c5.bran_spared`, `c5.bran_justice`). F21–F25 belong to chapter
5 (the castle look). Tests: `story_ch5_ambush_warned`, `_ambush_nets`, `_guard_key`, `_cells`, `_mira`, `_captain`,
`_unmask`, `_end`.

## 5. Events that do something

**The aim: every floor has at least one real event, and no NPC only says hello** (an aim of the plan, not a project rule: the owner, Q112). Six kinds replace the
text-only events:

| Kind | What happens | Example |
|---|---|---|
| **Rescue** (grows the village) | A captive on a floor: free them (beat their guard, find a key, a short play); they go home to the village and open something there | The baker freed on F03 → the village bakery (food that heals before a fight) |
| **Quest** (a reward) | Someone asks for something: bring an item, beat a named monster, find a lost pet; a real reward or a new event | The weapon master's lost whetstone on F04 → a weapon level |
| **Choice** (a consequence) | Spare or fight, give or keep, trust or not; it changes later events, the companions and the ending | Spare a starving Collector soldier on F07: he warns you before the barracks ambush in chapter 5 |
| **Story scene** | A short scene on a floor: the villain, a clue, Liora's voice, a memory of the parents | Grey Mask watching from the stairs of F05 |
| **Comic side quest** | Recurring absurd requests | Pip's "investment opportunities"; a slime that wants to become a knight; a chicken that only follows the hungry |
| **Talk N times** (the owner, 2026-11-20) | Talking to the same NPC again and again: on the Nth talk they give something special, **a key that opens a gate** or **a special weapon** (the dialogue system already counts talks: `talkCountAtLeast`, and gives: `give`) | The shy shepherd girl on F02: on the 5th talk she gives the key to the old orchard gate |

**Village growth.** The village starts burnt and nearly empty. Each rescued villager comes home and opens a service or a
quest: baker (food), healer (healing), blacksmith (the forge), Pip (the shop), cartographer (the floor map), innkeeper
(rest, letters), fortune teller (hints), bard (the music room). The village visibly fills up as the player climbs: the
reward for every rescue is seen, not only counted.

**What each chapter needs (the content check):** 5 floors, at least 5 real events, 1–2 rescues, 1 quest, 1 choice, 1
comic side quest, the boss with a scene before and after, and every NPC met with a line that changes as the story goes on.

## 6. Choices and endings

Three new counters (the story data already has counters, `story/counters.json`; today's are `humanity`, `insight`, ...):
**mercy** (spared, forgiven), **vengeance** (punished, taken back), **trust** (Kai's and Liora's trust). The big choice in chapter 5 (Grey Mask) and the last one in
chapter 7 decide the ending, with the counters:

| Ending | Roughly |
|---|---|
| **Home** (the good one) | the family rescued, Grey Mask spared or brought to justice, Liora trusted: the Star restarted, the village rebuilt |
| **The Avenger** (bittersweet) | the family rescued, Grey Mask killed in anger: the village saved, the boy changed |
| **The God** (dark) | power taken over family: the old god's body |
| **Secret** | everyone rescued and every side quest done |

The rebirth cycles of today's story are not part of this plan (Q114).

## 7. What the data and the code need

| Exists (keep) | To build |
|---|---|
| Dialogue nodes, choices, actions (`give`, `setStoryFlag`, `enterBattle`, `tryWeapon`, `chooseWeapon`, `miniGame`) | A **`rescue` action**: the NPC goes home to the village, its service unlocks |
| NPC lines with conditions (`requires`: flags, stage, items) | A **quest log**: start, progress (item held, monsters beaten, flag), finish, reward; shown in the player menu's Events tab |
| Story flags, counters, vars; side stories; the endings evaluator | **The village roster**: the village shows who is home and what they offer |
| The event editor (events on floors, kinds, NPC dialogue) | **Event kinds** rescue / quest / choice / scene / comic in `kinds.json`, each with its fields in the editor |
| Floors, stage looks, monsters, the boss introduction | **Floor triggers for scenes** (step on a cell → a scene) and a scene's portraits |
| The monster cards | The cards' powers shown as the hero's Godborn power (the story's name for them) |

The 70-floor tower, the 10 seals and their cultivation names, and the 140 text events are replaced, not deleted at once:
each chapter's new content replaces the old floors it covers (Q115).

## 8. Order of work

| Step | What | Done when |
|---|---|---|
| S1 | The tools: `rescue` action, the quest log, the village roster, the new event kinds in the editor | A test scenario rescues an NPC, finishes a quest and sees the village change |
| S2 | **Chapter 1 complete**: the morning, the raid, F01–F05, 2 rescues, 1 quest, 1 choice, a comic quest, the boss with scenes | Played from New Game to the chapter's end; every floor has a real event |
| S3 | Chapters 2–3 | the same check |
| S4 | Chapters 4–5 (Grey Mask, the big choice) | the same check |
| S5 | Chapters 6–7, the endings | every ending reachable in a test |
| S6 | Every NPC rewritten (lines that change with the story); the old text events removed | no greeting-only NPC left |

**S1, built (2026-11-20):**
- Dialogue and event actions (`story_actions.h`): **`rescue`** `{npc}` (the run flag `home.<npc>`, the notice "{name} went home to the village!", the quests waiting for it); **`startDialogue`** `{dialogue}` (a floor scene opens a dialogue); **`openStore`**; **`heal`** `{cost}`.
- **Quests** (`missions.json`, `mission_system.h`): a **`flag`** objective (a run or story flag set: a rescue's `home.<npc>`, a choice's flag); a quest whose flag is already set is done when taken; notices when taken, done ("go back and tell them") and claimed, with its title; **saved with the run** (`RunSaveData::m_jMissions`).
- **The village grows**: a `hub.json` place with `unlockFlag` `home.<npc>` opens when that villager is home (the first: `hub_bakery`, the baker).
- **Event kinds** in `events/kinds.json`: quest, choice, scene, comic, talk_n (rescue was there).
- A sample quest, `m_rescue_baker` (the elder: bring the baker home; 20 gold, 10 EXP).
- Tests: `scenario.story_rescue_quest` (take the quest, free the baker, the quest done, claim it, heal: gold 0 → 15), `scenario.story_village_bakery` (the bakery opens with `home.baker`); `unit.save_test` has a quest save round trip (switched off, not run).
- Not yet: the quest log's own page (quests show in the Events tab as missions did), a village screen with faces (the village is still its menu list), scene portraits.

Each step: English and Traditional Chinese text first (the other languages after), scenario tests for its rescues,
quests and choices, and the owner plays it before the next.

## 9. Questions for the owner

Also in [13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md).

| Question | Default | Yours | Answer |
|---|---|---|---|
| **Q110** The hero's name? | Leo (里歐) | Leo, and the player may rename him on New Game | **Default** (the owner, 2026-11-20): Leo (里歐), no renaming |
| **Q111** The villain who burned the village? | "Grey Mask" (灰面), the Collectors' captain | Keep; his face shown in chapter 5 (someone the village knew) | **Yours** (the owner, 2026-11-20): Grey Mask; his face shown in chapter 5, someone the village knew |
| **Q112** The rule "every floor has a real event, no greeting-only NPC" into CLAUDE.md? | not a rule | Make it a rule | The owner, 2026-11-20: **not a rule**; a sixth event kind instead, "talk N times" (§5) |
| **Q113** Start with S1 (the tools) or with S2 (chapter 1's writing, then the tools it needs)? | S1 then S2 | S1 and S2 together, chapter 1 driving what the tools need | **Default** (the owner, 2026-11-20): S1 then S2 |
| **Q114** Today's rebirth cycles (cycles.json, memory shards)? | dropped from the story (the code stays) | Drop for now; maybe a New Game+ later | **Default** (the owner, 2026-11-20): dropped from the story; the code stays |
| **Q115** Today's 70-floor lore (10 seals, cultivation terms like 引氣 in the skill tree)? | replaced chapter by chapter | Replace; keep the skill tree's mechanics with new names that fit the Godborn | **Default** (the owner, 2026-11-20): replaced chapter by chapter |
| **Q116** Languages for the new story text? | zh_TW + en first, the other 4 after each chapter | Keep | **Default** (the owner, 2026-11-20): zh_TW + en first, the other 4 after each chapter |
