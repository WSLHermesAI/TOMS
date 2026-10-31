# 15 — The battle revamp: one loop the player loves (design)

> 中文版：[zh_TW/15_BATTLE_REVAMP.md](zh_TW/15_BATTLE_REVAMP.md)

**Status:** design, 2026-10-31 (the owner: "revamp the battle scene: from the gameplay documents, integrate some ideas
together, make the player love the battle"). Nothing in it is built in the game yet; §9 has the questions.

**Try it:** [battle_editor/revamp_prototype.html](battle_editor/revamp_prototype.html) is a fast playable prototype (open
it in a browser; it works offline): the loop, the three weapon plays, break, chain, the ultimate's cut-in, the feel and
the result screen, with switches for Q34–Q37. Its numbers are made up; it is for feeling the ideas, not for tuning.

The ideas are not new. They are spread over four documents, some built, some only written down:
[07](07_GAMEPLAY_PLAN.md) (the pillars, the weapons, named moves), [11](11_BATTLE_DATA.md) (fighters, statuses, story
fights), [13](13_BATTLE_EDITOR.md) §6 (ten ideas for a real-time strategic battle) and [14](14_WEAPON_PLAY_AND_LOOT.md)
(each weapon its own play, the loot grind). The engine has new tools too: node effects (glow, motion blur, trails;
[../engine/18_NODE_EFFECTS.md](../engine/18_NODE_EFFECTS.md)). This document puts them together into **one battle**,
in an order of work where each step is something a player can feel.

## 1. Why players love a battle, and where ours is today

A battle players come back to has, every few seconds:

| What the player gets | Ours today |
|---|---|
| **Something to read:** the monster's next move, so a choice exists | built: the intent, as words over the monster (13 §9.2) |
| **A choice:** attack now, guard, wait, use a skill | partly: attack and guard; skills are one "active" button |
| **A play of the hands that is the weapon's own** | not yet: the bow, the sword and the staff share one point track (14 §2) |
| **A big moment:** a break, a burst, a finisher with its name | partly: break and "Perfect!" exist; no finisher, no name, no cut-in |
| **Feel:** every hit lands with weight | small: a number rises, the monster flinches |
| **A reward that shows growth:** the weapon levels up, a new move is near, a card drops | partly: the weapon's wins count in the background; a card notice |

The two biggest gaps: **the battle does not feel** (no hit stop, no shake, one sound per hit) and **it does not pay
off** (no moment at the end that shows what the player earned and what comes next). Both are cheap compared to the
weapon plays, so they come first (§8).

The screen is also busy: the enemy clock, the attack track, the guard bar, the super charge, the active button and up
to three intents all ask for the eye at once (§6).

## 2. The loop of one fight

Every fight, from a slime to Vorkath, follows the same rhythm, so a player learns it once:

```
   READ ───────► ANSWER ───────► PLAY ───────► BREAK ───────► BURST ───────► REWARD
 the intent    guard / parry /   the weapon's   the gauge     the ultimate,   grade, weapon XP,
 over the      dodge, or attack  own play       fills: the    a named move,   the card, a chest key,
 monster       before it lands   (14 §2)        monster is    a cut-in        "next level: a new move"
 (13 §6.1)     (13 §6.2)                        stunned       (§3.4)          (§3.6)
                                                (13 §6.4)
```

A normal fight lasts **20–40 s** (two to four exchanges); a boss **60–120 s** with phases (§3.5). Mobile time (07,
pillar 4): one fight fits a short break.

## 3. The pieces, put together

Each piece names where it came from and what is built.

### 3.1 Read: the monster tells you what it will do

- **Intent icons** instead of words (13 §6.1, step 4 still to make): a fist (strike), a raised hammer (heavy), three
  marks (jabs), a glowing rune (spell), a shield (guard), with the ring filling to the moment it lands. Words stay in a
  small line under the icon for the first battles, then go (a setting).
- **The last 0.5 s** before a heavy blow or a spell: the monster glows (node-effect `glow`, 18) and a short sound
  plays. The moment to parry is seen and heard, not guessed from a bar.
- **One clock per monster, no global clock bar:** the "Enemy attack timer" row goes (it shows only the nearest one).

### 3.2 Answer: one guard button, three outcomes

- Built (13 §9.2): a guard softens a hit; a perfect guard **parries** a heavy blow (stagger, ×2 damage); jabs cut a
  combo; a spell breaks under a good combo; a guard breaks under a full charge.
- **Simpler to read:** the guard bar becomes **one big guard button** whose ring shows its timing window; the result
  pops as a word at the player: *Blocked*, *Parry!*, *Too early*. (The five-zone bar stays as an option in
  `battle.json`.)
- **Weapons with a role** (13 §6.5): the bow can shoot during a monster's wind-up without being hit (its range); the
  sword's perfect counts as a parry; the staff's full charge breaks guards and spells. Picking the weapon for the
  monster becomes a decision, and the weapon trial (07 §15) already teaches them.

### 3.3 Play: each weapon its own play

From 14 §2 (the owner's notes, 2026-10-30), in the order 14 §4 proposed:

| Weapon | Play | Level adds | Ultimate |
|---|---|---|---|
| Bow | drag back and let go at a target (14 Q28) | more arrows per shot | targets appear in different places; all hit = the critical volley |
| Sword | swipe across the enemy, one cut per swipe | more cuts per attack | swipe the directions shown; then a memory chain |
| Staff | tap circles in time (Ouendan) | more circles, faster | draw a magic circle |

All three report on **one result scale**, miss / hit / great / perfect, so `weapon_play.json` `results`, their
particles and the `attack_big` clips (13 §10) keep working, and **named moves** (07 §5.4) name the result:
*Tiger Wind, four hits* when played well, *Tiger's head, snake's tail* when played badly. A miss sets the combo back
by the weapon level's rule (07 §5.6), as now.

### 3.4 Break and burst: the big moment

- **Break** (built, 13 §6.4): matched points fill the gauge; full, the monster is stunned 3 s and takes ×1.5.
- **New: the break window is a moment.** The screen dims a little, the monster flashes white (node-effect `flash`),
  "BREAK!" crosses the screen, and the music hits a beat. The player knows: *now*.
- **The ultimate** (the super charge, 14's 絕招) becomes each weapon's **finisher**: a cut-in (the hero's face or
  weapon sliding across, 0.6 s), then the weapon's ultimate play (§3.3), then the `attack_big` clip with trails and
  glow (18). Used inside a break window it does ×2.
- **Perfect chain** (13 §6.9): perfects in a row raise a counter by the player (*Chain ×3*); each step adds damage and
  fills the super charge faster. A miss resets it. This is what good players chase.

### 3.5 Monsters with a story: phases

- Built: monster events (13 §9.1): at HP below N %, a line, a stat change, a transform, a summon.
- **Use them in every boss and some elites:** the golem's runes blaze at 50 % (faster, a new move), at 20 % it becomes
  its core; Vorkath roars and calls a second form. A phase change is a moment too: a short pause (0.8 s), a line, the
  monster's `special` clip.
- **Weak points and elements** (13 §6.7): a monster's card shows its weakness (fire, ice, pierce); hitting it does more
  and shows *Weak!*. Collecting the card teaches it, so the card collection (07 §3) feeds the battle.

### 3.6 The reward: the end of a fight is a payoff

A result screen of 2–3 s, skippable with a tap:

```
        ★ ★ ★   S                      (grade: time, damage taken, perfects)
   Hunter's Bow  Lv 3  ████████░░  8 / 10   ← the weapon's wins to its next level
   Next level: 4 points, a double shot         ← what the next level adds (07, pillar 2)
   + 12 gold   + 7 EXP   🂠 Slime card (2)     + 1 chest key (14 §3)
```

- **The weapon's progress is shown every fight** (07 pillar 2, "players should want the next level to see the new
  move"). Today it counts silently.
- **The grade** (S / A / B / C) rewards playing well, not only winning: an S gives a little more (gold, the card's
  chance). It is the "one more try" hook.
- **Drops** feed the loot grind later (14 §3): a chest key from some monsters, the special chest from special ones.

### 3.7 Before the fight: a small choice

- **Skills on cooldown, chosen before** (13 §6.6): two slots from the skill tree's actives (*a shield for the next
  hit*, *slow time 3 s*, *a free perfect*), each with its cooldown ring. Real time, but when to use them is the choice.
- The card's weakness (§3.5) shows on the encounter, so the player can switch weapon before engaging.

### 3.8 Feel: every hit lands

From 13 §6.10, all cheap and all tunable in `battle.json`:

| Effect | When | Amount (first guess) |
|---|---|---|
| **Hit stop** (both sides freeze) | a great / perfect hit, a parry, a break | 40 / 70 / 90 ms |
| **Screen shake** | a heavy hit taken, a perfect, a break, the finisher | 3–10 px, 150–300 ms |
| **Damage number by tier** | every hit | size and colour by result; *Weak!*, *Parry!*, *Chain ×3* words |
| **Sound by tier** | every hit | hit / great / perfect / parry / break each its own |
| **Monster flinch** | every hit landed | built (hurt clip) |
| **Trails and glow** | the finisher, a perfect | node effects (18) on the weapon's clip |
| **Slow motion** | the last hit of a battle | 0.4 s at 30 % speed, then the result screen |

A **"Reduce shaking and flashes"** setting turns shake, flash and slow motion down (comfort and accessibility).

## 4. The screen

Portrait (a phone held in one hand) and landscape keep the same pieces; 12 places them.

```
 portrait                                   landscape
 ┌─────────────────────────┐                ┌──────────────────────────────────────────────┐
 │ [⚔ 2.1s]  [🛡 3.0s]      │ intents        │ [⚔ 2.1s] [🛡]              Chain ×3   ☰     │
 │   MONSTER    MONSTER    │                │ HERO        ─────────►     MONSTER  MONSTER  │
 │ ▓▓▓▓▓▓░░  ▓▓▓▓░░        │ HP + break     │ ▓▓▓▓▓▓▓▓    weapon play     ▓▓▓▓░░  ▓▓▓░░    │
 │                         │                │             over the                         │
 │   (the weapon play      │                │             monsters                         │
 │    happens up here,     │                │ [skill][skill]          [GUARD]   [ATTACK]   │
 │    over the monsters)   │                │ ◉◉◉○○ super                                 │
 │   HERO   ▓▓▓▓▓▓▓▓ HP    │                └──────────────────────────────────────────────┘
 │ Chain ×3                │
 │ [skill] [skill]  ◉◉◉○○  │
 │ [  GUARD  ] [ ATTACK  ] │ thumbs
 └─────────────────────────┘
```

- **Two big panels** for the thumbs: **guard** is a button; **attack is not** (the owner, 2026-10-31: pressing attack and
  then playing the move is one step too many). When the attack is ready, the weapon's play simply waits over the
  monster and the first touch starts it; the attack panel only shows *ready* or *recovering* (its ring filling). The
  skills and the super charge sit beside them; the ultimate is a button and replaces a waiting play.
- **The weapon play happens over the monsters**, where the eyes already are, not in a strip at the bottom.
- **Gone:** the global enemy clock row, the five-zone guard bar (an option), the long log line (a short word pops
  where things happen instead; the log stays in the battle editor).

## 5. What it reuses, and what is new

| Reused as is | Changed | New |
|---|---|---|
| intents, answers, break, results, pace, monster events, looks, moves (13 §9) | the guard bar → one button (option) | intent icons (step 4 of 13) |
| 1–3 monsters, targeting (13 §4.6) | the battle screen's layout (`battle.rml` / `.rcss`, 12) | hit stop, shake, slow motion, number styles, tier sounds |
| the weapon levels and growth (07 §6, §13) | the "Perfect!" text → named moves | the result screen and grade |
| `attack_big`, the result-tier particles (13 §10) | | the break moment, the finisher cut-in |
| node effects, trails (18) | | each weapon's play (14 §2) |
| the cards (07 §3) | | weaknesses, skills loadout, chest keys |
| the battle editor (13), to tune all of it | | `battle.json` `"feel"` block |

Every number above goes into data (`battle.json` `"feel"`, `weapon_play.json`, `moves.json`, `enemies.json`) and is
tried in the battle editor before it ships.

## 6. What stays as it is

- The battle stays **real time** with **no menus** during a fight.
- The rules built in 13 §9 stay; this changes how they are shown and felt, and adds the weapon plays on top.
- Monsters without `"moves"` still fight as today, so old floors keep working while the content catches up.

## 7. Measuring "players love it"

Without players to ask yet, the battle editor's log answers it (13 §4.5):

- **Fight length:** 20–40 s normal, 60–120 s boss.
- **Decisions per fight:** at least three moments where guard, attack and skill were all possible and one was right.
- **Perfect rate** for a practised player: 40–60 % (higher: too easy; lower: frustrating).
- **"One more try":** an S grade within three tries of a new monster.

Later: a short playtest per step (five people, watch, ask "what did you like, what confused you").

## 8. Order of work

Each step is something a player feels, and each can ship on its own:

| Step | What | Size | Needs |
|---|---|---|---|
| R1 | **Feel:** hit stop, shake, numbers and words by tier, tier sounds, slow motion on the last hit; the reduce-effects setting | small | Q34 |
| R2 | **The reward:** the result screen with the grade, the weapon's progress and the next level's teaser, drops | small | Q35 |
| R3 | **The screen:** two thumb buttons, intent icons, no global clock, the play over the monsters (portrait and landscape) | medium | Q36 |
| R4 | **Break and burst:** the break moment, the perfect chain, the finisher cut-in with node effects | medium | — |
| R5 | **The bow's own play** (14 §2.1) with named moves, then the sword (§2.2) and the staff (§2.3) | large | 14 Q27–Q30 |
| R6 | **Choices:** skills loadout, weaknesses on cards | medium | Q37 |
| R7 | **Story fights:** phases for every boss and some elites; the loot grind's chest keys (14 §3) | medium | 14 Q31–Q33 |

R1 and R2 first: in a few days the battle feels and pays off, with nothing else changed.

## 9. Questions for the owner

| # | Question | Proposal if no answer |
|---|---|---|
| Q34 | **Feel:** how strong? Hit stop and shake can be subtle (mobile puzzle) or loud (action game) | medium (the first guesses in §3.8), all in `battle.json`, and the reduce-effects setting |
| Q35 | **Grade:** S / A / B / C by time, damage taken and perfects, with a small bonus for S? | yes, the bonus +20 % gold and a better card chance |
| Q36 | **The screen:** the weapon play over the monsters and two thumb buttons (§4), the five-zone guard bar only as an option? | yes, portrait first |
| Q37 | **Skills:** two slots chosen before a fight (from the skill tree's actives), or keep today's one "active" button? | two slots |
| Q38 | **Order:** start with R1 + R2 (feel and reward), or with the bow's own play (R5)? | R1 + R2 first |

The open questions of 14 (Q27–Q33: the weapon plays and the loot grind) are needed before R5 and R7.
