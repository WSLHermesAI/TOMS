# 16. Skills and plays from reusable parts (the owner's ideas, 2026-10-31)

The owner's new ideas from 2026-10-31, put in order: the three weapon plays reworked, monsters whose attacks have a
rhythm the player answers, special guard plays, a training ground for cards, and special moves such as the staff's
pentagram. They follow the battle revamp ([15](15_BATTLE_REVAMP.md), built through R5) and the weapon plays of
[14](14_WEAPON_PLAY_AND_LOOT.md).

**The owner's main requirement: the features must be reusable.** A special move is data: its name, its numbers and its
own animation. A new move is a new entry in a file, not new code. So this document first describes **five building
blocks** (§2), then builds every idea out of them (§3–§6).

Built on 2026-10-31 (§10; the moves' file is `special_moves.json`). The open questions are in §9.

## 1. The owner's notes (as written)

> 劍：每次可以攻擊每個方向會出現一秒鐘的提示，會有那種彈一下出現再消失的特效。劍每次方向都會攻擊一次。劍攻擊每次攻擊中的指定方向
> 攻擊只有1秒鐘沒輸入就是換下一個，這邊有的不會只有方向有的可能是三角形或是圓形，每次一秒鐘的等待但是只有0.5秒（可調整）可以輸入指令。
>
> 弓箭：靶會移動0.5秒然後在固定，並且瞄準越久抖動越大。弓箭需要教學，手指點任意地方反方向滑動，並且蓄力有秒數限制，越久震動越大但是
> 攻擊力也越強，會根據玩家的str決定可以拉多久，根據dex看會不會讓方向控制震動。普通拉弓就一下，招式才需要連續瞄準，並且要有particles的特效。
>
> 魔杖：數字順序會慢慢消失？快要可以點的時候會有驚嘆號0.5秒。魔杖每次點擊都會放魔法。魔杖不要有點一下才開始數數字，時間到就開始數，
> 並且數完就消失0.5秒然後在一起出現。
>
> 防禦：防禦的話會中斷玩家的攻擊，並且只有在怪物頭上出現驚嘆號時點才有效。有的怪物攻擊會0.5、0.5每隔固定時間攻擊，也有連續攻擊，
> 這邊要讓玩家決定要怎麼防禦或是攻擊。怪物需要設定攻擊的集氣以及攻擊次數跟每次攻擊的間隔。有的高級怪物不只要點防禦按鈕，還會進入
> 特殊防禦階段的遊玩，可能是畫五角星或是連點好幾下之類的。防禦有沒有進特殊遊玩是跟怪物綁定。
>
> 卡片：有卡片之後可以去特殊練習場直接指定怪物做連續攻擊好幾場的特殊升級玩法，讓收集怪物卡片變成能在練習場升級（每天有次數限制，
> 並且會消耗掉卡片數量）。
>
> 招式：玩家武器特殊招式，例如魔杖會出現一個事先做好的五角星動畫，先出現一個五角星然後particles會照五角星的繪畫方向邊移動邊噴出，
> 每到一個定點會做一個更誇張的爆炸特效，五個點都表演完，整體暗下倒數計時提醒玩家畫五角星，每完成一個頂點就一樣做一個爆炸特效並跑出
> 招式名字，例如super attack，每完成一個頂點螢幕會出現1/5的字，都集滿招式名都出現，玩家角色會播放對應的動畫來攻擊敵人，這邊先設定
> 大絕招會連續攻擊10下，或是拉近攝影機特寫玩家的特殊招式動畫，然後攻擊力是普通攻擊的N倍，先寫十之後可以調整。

## 2. Five building blocks

Each block is one piece of code with its settings in data. Every play, guard and move below is these blocks, set up
differently.

| # | Block | What it does | Its settings (data) | Used by |
|---|---|---|---|---|
| B1 | **Cue track** | A row of cues, one after another. Each cue pops in (a short bounce), waits, and opens an **input window**; no input in time moves on to the next. Each answered cue can hit at once | per cue: what to do (a way, a shape, a tap, a spot); `cueMs` (1000), `inputMs` (500), where the window sits in the cue, the pop effect | sword, staff, monster attacks, special guards, moves |
| B2 | **Gesture** | Reads the finger: a tap, a swipe and its way, a drag back and let go (a pull), a hold, quick taps (a count), and **a drawn shape**: its corners reached in order (a triangle, a circle, a pentagram) | per shape: its corners (points from 0 to 1), the order, how close a corner must be; the swipe's tolerance; the pull's limits | all plays |
| B3 | **Warning** | An "!" over a monster, or over a spot, `warnMs` (500) before something can be answered. The answer only counts **while it shows** | `warnMs`, the mark's look (a sprite or effect) | guard, staff, monster attacks |
| B4 | **Show** | A timed list of visual beats, the same for a move's demo and its finish. Each beat: play a particle effect (also along a path, spraying as it moves), a burst at a point, a character clip, a banner or text, screen dim, a countdown, a camera close-up, a shake | per beat: the time, the kind, its file (`battle.particle`, an `.anim` clip), its numbers | moves, ultimates, special guards |
| B5 | **Skill** | **One entry in `special_moves.json`**: a name, the play (B1 + B2), the shows (B4), the reward per step, the finish (hits, damage multiplier). A player's move, a weapon's ultimate and a monster's special guard are all skills | see §6.2 | everything that has a name |

How they are put together, for the staff's pentagram (§6.1):

```
Skill "Star Seal" ──► Show "demo"   (B4: the pentagram drawn by particles, a burst at each corner)
                  ──► Play          (B2 shape "pentagram", 5 corners in order, inside a countdown)
                        each corner ──► Show "step"  (B4: a burst, "1/5", the name's next part)
                  ──► Show "finish" (B4: dim, close-up, the character's clip, 10 hits)
                  ──► Damage        (normal attack × 10, split over the 10 hits)
```

What exists today and can be reused: the hint and hit particles (`battle.particle`, `playBattleFx`), node effects
([../engine/18_NODE_EFFECTS.md](../engine/18_NODE_EFFECTS.md)), the character's clips with the weapon at the hand
(CharacterRig, e.g. `attack_big`), banners, hit stop, shake and screen dim (15, R1/R4), the touch plays
(`touch_play.h`, R5). **New:** the cue track (B1), drawn shapes (B2), the warning mark (B3), the show timeline (B4) as
data, `special_moves.json` (B5), and a battle camera close-up.

## 3. The weapons' plays, reworked

| | Built today (15 R5) | The new idea |
|---|---|---|
| Sword | all arrows shown; one result after the last cut | **a cue track**: one prompt at a time, each hits on its own |
| Bow | a fixed target per arrow; the pull's length aims | the target **moves 0.5 s, then stops**; the pull **charges** (longer = stronger and shakier); one pull for a normal attack |
| Staff | a tap starts the beat; numbered circles close in turn | **starts by itself**; the numbers count, hide, then the circles return together; "!" before each one; each tap casts |

### 3.1 Sword: one cut per prompt

- A **cue track** (B1). Each prompt pops in over the monster with a bounce and stays **1 s** (`cueMs`). Inside it,
  only **0.5 s** (`inputMs`, adjustable) takes input. No input: it passes, and the next prompt comes.
- A prompt is a **way** (one of 8) at first. Later prompts can be **shapes**: a triangle, a circle (B2).
- **Each prompt answered is one hit** at once, with its own number and effect. The attack ends when the prompts run out.
- The weapon's level sets the number of prompts and which kinds can come (ways only at Lv 1, shapes later).

### 3.2 Bow: aim, draw, release

- **The target** moves for **0.5 s** when it appears, then stays still.
- **The pull** (B2): touch **anywhere** and drag the other way (the slingshot), then let go. The aim goes opposite
  to the drag, as today.
- **Charging:** the longer the pull is held, the stronger the shot, up to a limit. Two stats decide it:
  - **STR** sets how long the pull can be held (its charge limit). Past it, the shot goes by itself, or the charge
    drops (Q41).
  - **DEX** sets how much the aim shakes. The shake grows the longer the player aims; high DEX keeps it small.
- **A normal attack is one pull, one arrow.** Only a **move** (a skill) asks for several aims in a row, with particle
  effects on each arrow.
- **A tutorial** the first time the bow attacks: a ghost finger shows "touch, drag back, let go" (once per save,
  and again from the settings).

### 3.3 Staff: count, hide, tap in order

As the owner's note reads (Q40 confirms):
1. The play **starts by itself** when the attack is ready (no tap to start).
2. The circles appear **with their numbers** and **count in order**: 1, 2, 3…, each one lit in turn.
3. When the count ends, they **all hide for 0.5 s**, then **come back together** without their numbers.
4. The player taps them **in the order remembered**. An **"!" shows 0.5 s** (B3) before each circle can be tapped.
5. **Each tap casts a spell** at once (one hit, its effect from the circle to the monster).

With the blocks: a cue track (B1) whose cues are spots, the warning mark (B3) on each, a tap (B2), and a show (B4)
for the count and the hide.

## 4. Monsters: an attack rhythm the player answers

### 4.1 An attack is a charge, then hits with gaps

Today a monster's move (`moves.json`) has a wind-up (`time`), and jabs have `hits`. The new idea gives every attack a
rhythm the player can read:

| Setting | Meaning | Example |
|---|---|---|
| `chargeMs` | its wind-up (its intent shows meanwhile) | 1500 |
| `hits` | how many hits | 3 |
| `gapsMs` | the time before each next hit (one value, or one per gap) | `[500, 500]`: steady; `[150, 150, 600]`: a fast burst, then a late one |
| `warnMs` | the "!" over its head before each hit (B3) | 500 |
| `guardPlay` | a skill id: a special guard play instead of the button (§4.3) | `"guard_pentagram"` |

```json
"triple_bite": { "kind": "jabs", "damage": 0.5, "chargeMs": 1500, "hits": 3, "gapsMs": [500, 500], "warnMs": 500 },
"frenzy":      { "kind": "jabs", "damage": 0.3, "chargeMs": 1200, "hits": 5, "gapsMs": [150, 150, 150, 600] },
"doom_seal":   { "kind": "spell", "damage": 2.5, "chargeMs": 2500, "hits": 1, "guardPlay": "guard_pentagram" }
```

This is one cue track (B1) on the monster's side: each hit is a cue with its warning. The player chooses: guard
each hit, or attack in the long gaps and take a hit.

### 4.2 The guard

- **The guard only counts while the "!" shows** over the monster (B3). Earlier or later is a miss (no block).
- **Guarding interrupts the player's own attack** (the running play stops, as the monster's jabs already stop it).
  So guarding has a cost, and choosing between attack and guard matters.
- The parry / block grades of 15 §3.2 can stay inside the "!" window (the first part a parry, the rest a block), or
  the "!" can be a single "block" (Q42).

### 4.3 Special guard plays

- Some strong monsters' attacks ask for **a play** instead of the guard button: draw a pentagram, tap quickly N
  times, swipe the shown ways.
- **It belongs to the monster's move** (`guardPlay`, a skill id). The same skill can be used by several monsters.
- The result decides the damage: all done = no damage (and maybe a stagger); part done = part of the damage.

## 5. The training ground: cards that level you up

- From the card collection, the player **picks a monster** whose card they hold and fights it **several times in a
  row** (a run of fights, as the weapon arena does).
- It is a **special way to level up**: the wins give EXP and the weapon's uses (its level), maybe more than usual.
- **A daily limit** (runs per day) and **each run spends cards** of that monster.
- It reuses the arena (07 phase 0) for the fights in a row, and the card collection (07 §3, 14 §3) for what it costs.
- **This changes an earlier decision.** On 2026-10-20 the owner said cards are only a collection for now; spending
  them is new (Q44).

## 6. Special moves: data, each with its own show

### 6.1 The staff's pentagram, step by step

| Step | What happens | Block |
|---|---|---|
| 1 Demo | A pentagram appears. Particles **move along its lines in drawing order**, spraying as they go | B4 (a path effect) |
| 2 | At **each corner**, a bigger burst | B4 |
| 3 | After the 5th corner, the screen **dims** and a **countdown** starts: "draw the pentagram" | B4 |
| 4 Play | The player draws it. **Each corner reached** (in order) gives a burst, **"1/5" … "5/5"**, and the next part of the move's name ("SUPER ATTACK" shown a part at a time) | B2 shape, B4 per step |
| 5 Finish | All five: the whole name shows; the character **plays the move's own clip**, with a **camera close-up**; **10 hits** in a row | B4 |
| 6 Damage | **Normal attack × N** (N = 10 to start, in data), shared over the hits. Fewer corners: less | B5 |

Countdown out: the corners done so far count (3/5 → 3/10 of the damage, or a weaker finish; Q45).

### 6.2 The same thing as data (`special_moves.json`)

```json
"star_seal": {
  "name": "skill.star_seal",                 "_": "a text.json key: the move's name, in every language",
  "weapon": "staff", "use": "ultimate",      "_use": "ultimate (super charge full), skill (from the skill list), guard (a monster's guardPlay)",
  "play": { "gesture": "shape", "shape": "pentagram", "timeMs": 6000, "cornerRadius": 40 },
  "demo":   [ { "at": 0,    "do": "pathFx", "shape": "pentagram", "fx": "star_trail", "ms": 2000 },
              { "at": "corner", "do": "fx", "fx": "star_burst" },
              { "at": 2200, "do": "dim", "value": 0.6 }, { "at": 2200, "do": "countdown" } ],
  "step":   [ { "do": "fx", "fx": "star_burst_big" }, { "do": "count" }, { "do": "namePart" } ],
  "finish": [ { "do": "name" }, { "do": "camera", "zoom": 1.6, "on": "player", "ms": 1500 },
              { "do": "clip", "clip": "ultimate_star" }, { "do": "hits", "count": 10, "gapMs": 120 } ],
  "damage": 10
}
```

**Another move is another entry.** The same blocks, other values, its own animation:

| Move | Weapon | Play | Show | Finish |
|---|---|---|---|---|
| Star Seal | staff | draw a pentagram | particles along the star | 10 hits, ×10 |
| Tiger Wind | sword | a cue track: 4 ways, then a triangle | a slash effect per cut | the `attack_big` clip, 4 hits, ×4 |
| Falcon Rain | bow | aim 3 moving targets in a row | an arrow trail per shot | arrows from the sky, 6 hits, ×5 |
| Guard Pentagram | (a monster's) | draw a pentagram before its countdown | the monster's seal glowing | all 5: no damage |
| Guard Mash | (a monster's) | tap 12 times in 2 s | a cracking shield | damage × (1 − taps / 12) |

The battle editor ([13](13_BATTLE_EDITOR.md)) gets a **Skills** tab to make and try them, like the monster and weapon
tabs; the move's animation is made in the anim editor and its effects in the particle editor, then named here.

## 7. What changes in what exists

| Today | Becomes |
|---|---|
| `touch_play.h` (bow, slice, circles; one result) | the three plays rebuilt on B1–B3; each cue can hit on its own |
| `moves.json` `time`, `hits` | `chargeMs`, `hits`, `gapsMs`, `warnMs`, `guardPlay` (the old fields still read) |
| the timed guard (15 §3.2: press just before the hit) | valid only while the "!" shows; it stops the player's attack |
| the ultimate (15 R4: a harder play, ×2 / ×4) | a skill from `special_moves.json` with its own play, show and damage |
| the result rows' named moves (`weapon_play.json` `"move"`) | stay for normal attacks; moves have their names in `special_moves.json` |
| the card collection | also a cost: the training ground spends cards |
| no battle camera | a close-up of a fighter (zoom and pan, eased back) |

## 8. Proposed order

| Step | What | Needs |
|---|---|---|
| S1 | **The blocks:** cue track, drawn shapes, warning mark (pure modules with unit tests, like `touch_play.h`) | — |
| S2 | **The sword** on the cue track (ways, then shapes); **the staff** (count, hide, tap in order, cast per tap) | Q40 |
| S3 | **The bow:** moving target, charge by STR, shake by DEX, the tutorial | Q41 |
| S4 | **Monster rhythm and the "!" guard;** the guard stops the attack | Q42 |
| S5 | **The show timeline and `special_moves.json`;** the battle camera; Star Seal as the first move; the editor's Skills tab | Q43, Q45 |
| S6 | **Special guard plays** (pentagram, mash) as monster skills | — |
| S7 | **The training ground** | Q44 |

S1 and S2 first: the blocks are what everything else reuses, and the sword and staff show them working.
A quick HTML prototype (as for 15) can try S2–S5 before they are built, if wanted.

## 9. Questions for the owner

| # | Question | Proposal if no answer |
|---|---|---|
| Q39 | **The sword's prompt:** 1 s each with a 0.5 s input window. Where is the window: the **last** 0.5 s (see it, then answer) or the **first**? | the last 0.5 s; both in data |
| Q40 | **The staff, as read in §3.3:** the numbers count in order, hide 0.5 s, the circles come back together without numbers, and the player taps in the remembered order. Is that right? Does a wrong order end the play or only miss that circle? | yes; a wrong circle is a miss and the play goes on |
| Q41 | **The bow's charge limit (STR):** past it, does the arrow **go by itself**, or does the charge **fall back** (held too long, weaker)? | it goes by itself at full charge |
| Q42 | **The guard with "!":** keep parry and block grades inside the "!" (early part = parry), or one result (blocked)? | keep the grades: the first 40 % of the "!" is a parry |
| Q43 | **Which moves first,** and how a skill is used: only as the ultimate (super charge), or also skills from the skill list with their own cost? | Star Seal as the staff's ultimate first; skills from the list later |
| Q44 | **The training ground:** spending cards changes the 10-20 rule (cards only a collection). OK? How many cards a run, how many runs a day, and what does it give (more EXP? weapon uses?) | OK; 1 card a run, 3 runs a day, ×2 EXP and weapon uses |
| Q45 | **A move not finished in time:** the corners done count for part of the damage, or the move fails? | part: done / total of the hits and damage |
| Q46 | **The camera close-up:** every ultimate, or only some moves (data)? And the "Reduce shaking and flashes" setting turns it off? | per move in data; the setting shortens it |

## 10. Built (2026-10-31)

S1 to S7 are in the game, with the proposals of Q39 to Q46 as the answers until the owner says otherwise. The moves'
data is **`data/special_moves.json`** (`data/skills.json` is the skill tree's, so this document's B5 file got its own
name).

| Part | In the game | Where |
|---|---|---|
| B1 cue track | cues one after another: pop, wait, an input window (the last 0.5 s of each 1 s), an optional "!" | `cue_track.h` |
| B2 gestures | shapes by name (pentagram, triangle, square, circle, line), traced on the screen corner by corner, or drawn freely anywhere at any size | `gesture.h` |
| B3 "!" | over a monster before each hit (`battle.rml` `.foe .warn`), over the staff's circles, and the guard's window | `Game::foeWarning` |
| B4 + B5 shows and moves | `special_moves.json`: name, play, demo / step / finish shows, damage. Beats: pathFx, fx, dim, banner, name, namePart, count, camera, clip, hits, shake, sound | `special_moves.h`, `game_skills.cpp` |
| Sword | one prompt at a time, popping in; only its last 0.5 s takes the answer; each answer is a hit at once. A triangle prompt from Lv 3, a circle from Lv 5 (`weapon_play.json` `shapeCues`) | `touch_play.h` Slice |
| Bow | the target moves 0.5 s, then stops; the pull charges up to 1.5 s + 40 ms per STR (then the arrow goes, Q41); the aim shakes 10 px a second held, less with DEX; a normal attack is one arrow; a ghost-finger tutorial until the first shot | `touch_play.h` Bow, `battle.json` `"plays"` |
| Staff | starts by itself: the circles count (0.45 s each), hide 0.5 s, come back with an "!" for 0.5 s, then are tapped in order (0.9 s per circle); a wrong circle misses the due one (Q40); each tap casts at once | `touch_play.h` Circles |
| Monster rhythm | `moves.json` `chargeMs`, `hits`, `gapsMs`, `warnMs`; a run of hits keeps its rhythm through the attack pause. New moves: `triple_bite` (forest wolf), `frenzy` (vampire bat) | `resolveEnemyClockFire`, `stepFoes` |
| The guard | `battle.json` guard mode `"warn"` (now the default): only while the "!" shows, its first 40 % a parry (Q42), the rest a block; with no "!": too early, locked 0.4 s; a press stops the player's attack. `"timing"` and `"bar"` stay as options | `guardWarn`, `battleTapDefense` |
| Special guards | `moves.json` `guardPlay`: `doom_seal` (lich: draw the seal in 5 s), `ground_pound` (ore golem: 12 taps in 2.2 s); the damage x (1 - share), all done: a stagger | `special_moves.json` `guard_pentagram`, `guard_mash` |
| Moves | the ultimate is the weapon's move when it has one: **Star Seal** (staff: the pentagram drawn by particles, a burst at each corner, dim, trace it in 6 s, "1/5" and the name part by part, a close-up, 10 hits, x10), **Tiger Wind** (sword: 4 ways and a triangle, 4 hits, x4), **Falcon Rain** (bow: 3 moving targets, 6 hits, x5). A move cuts in over the normal play; the fight waits during it; a part done gives that part (Q45) | `battleTapSuper`, `startSkill` |
| Camera | a close-up beat: the fighters and effects scaled about the player or the monster, eased in and out; "Reduce shaking and flashes" makes it smaller (Q46) | `drawBattleRig` |
| Training ground | the Cards tab's Train button: 1 card, 3 runs a day, 3 fights, EXP x2 and weapon wins +2 a win (Q44), the HP given back after | `battle.json` `"training"`, `startTraining` |

**Damage:** each sword cut and staff spell hits at once for half a normal hit shared over the answers; the play's end
deals the rest of the attack, so an attack's total is as before.

**Tests:** unit `touch_play_test` (rewritten), `play_blocks_test` (new: shapes, cue track, moves), `battle_rules_test`
(the "warn" guard, gaps). Scenarios `battle_play_bow / _slice / _circles` (updated), `battle_guard_warn`,
`battle_guard_warn_early`, `battle_rhythm`, `battle_special_guard`, `battle_ultimate_star`, `training_cards`; new
scenario fields `"battle": {"super": true}` and `"cards"` ([08](08_TEST_SCENARIOS.md)). `battle_parry` and
`battle_guard_early` keep testing the `"timing"` guard.

**Not yet:** the battle editor's tab for moves; moves from the skill list (Q43: later); a keyboard way to play the touch
plays; art of their own for the moves (they use `attack_big` and the effects made by `tools/make_battle_fx.py`).
