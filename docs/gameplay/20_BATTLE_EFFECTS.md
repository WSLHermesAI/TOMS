# 20. The battle's feel: effects and sounds (the owner's ideas, 2026-11-09)

中文：[zh_TW/20_BATTLE_EFFECTS.md](zh_TW/20_BATTLE_EFFECTS.md).

The owner's notes on how attacks look and sound, put in order and **built the same day** (Windows desktop). The numbers are in
`data/battle.json` `"feelFx"`, the effects in `media/fx/battle.particle` (made by `tools/make_battle_fx.py`), the sounds in
`media/sfx/` (made by `tools/make_battle_sfx.py`). The points left open are Q82–Q87 in
[../project/13_OPEN_DECISIONS.md](../project/13_OPEN_DECISIONS.md) §2: built on a proposal, answer only to change them.

## 1. The owner's notes (as written)

> 魔杖的提示要炫一點越後面出現的提示特效要越誇張然後依照出現的順還要把音效的pitch調高然後一口氣把提示弄消失，再次出現時會用0.1秒的間隔依照順序出現，此時還有數字已經能點，全部數圈圈都出現完數字立刻消失
>
> 選怪物攻擊那邊，黃色的底線很醜，應該直接讓怪物的名字那邊下面多個，目前攻擊目標的資訊
>
> 再次檢查網頁版的md5是否有啟動
>
> 較大的怪獸攻擊要有特殊動畫，類似有東西射出去攻擊
>
> 玩家的攻擊particles小招式也要噴，連段越多噴越多，魔杖前面幾招攻擊力很小，但是連段越多攻擊力要越強，particles要噴個爽
>
> 劍的話要有那種殘影刀光放大縮小加粒子
>
> 弓箭集氣要有粒子越來越多的聚集，噴出去要有爆炸效果
>
> 怪獸特殊攻擊要有特效
>
> (and, asked with the notes) while a monster attacks, it should not be only a simple animation: add special effects and sound.

## 2. The staff's hints

| Step | What happens |
|---|---|
| Count | Each circle lights with its number and an effect (`wand_hint_1`..`4`). **Each is bigger than the one before** (a higher tier, and drawn 8 % larger each), and its bell (`sfx.wand_hint`) is **two semitones higher** each time |
| Hide | All go **at once**, each with a pop (`wand_vanish`, `sfx.wand_vanish`) |
| Back | They come back **one by one, 0.1 s apart** (`circleReturnMs`), **with their numbers**, and can **already be tapped**; a circle not back yet cannot |
| Tap | Once the last is back, **the numbers go**; the rest are tapped from memory |

The old "!" before tapping (16 §3.3) is off: `circleWarnMs` is 0. A value above 0 brings it back (Q85).

## 3. Hits: more particles with the combo; the staff grows stronger

- **Every hit sprays** on the monster (`combo_spray_1`..`5`): the tier rises every 2 hits of the combo (`sprayComboStep`), and it is drawn
  bigger as the combo grows. The staff's sprays are one tier higher.
- **The staff's spells grow along the chain** (Q84): the k-th spell's share of the play's damage is k^1.6 (`damageRamp`). Five circles:
  the first does about 3 % of the play, the last about 41 %. The play's total is as before. Each spell's zap (`sfx.wand_cast`) is a step
  higher too.

## 4. The sword and the bow

- **Sword:** each cut leaves a slash along the swipe: long streaks that grow and shrink, with two afterimages after it, and sparks
  (`sword_slash`, `sfx.sword_slash`, a little higher as the combo grows).
- **Bow:** while the bow is pulled, particles are **drawn in** to the arrow every 90 ms, more and faster as the charge grows
  (`bow_gather_1`..`3`), with a hum that rises (`sfx.bow_charge`). Where the arrow hits it **explodes** (`bow_explode`,
  `sfx.explosion`), bigger for a fuller pull.

## 5. The monsters' attacks

Every attack now has an effect, and most a sound, on top of the monster's clip and its look's sound (`looks.json`). By kind
(`"feelFx"` `"foeAttack"`):

| Kind | When | Effects |
|---|---|---|
| strike | a plain attack of a normal monster | claw streaks on the hero (`monster_claw`) |
| big | any attack of a **big** monster (Q82) | a wind-up on the monster, a **bolt flies** at the hero (`monster_bolt`), a blast where it lands, a launch and a boom |
| spell | a spell | a casting glow, a **magic orb flies**, a violet blast |
| special | a heavy blow, or an attack with a special guard play (16 §4.3) | a big smash: rings, a slash, sparks, stars |
| bigSpecial | the same from a big monster | a bigger bolt, then the smash |

- **Big:** a boss, or `"big": true` in `enemies.json`. So far only the stone golem has it.
- A projectile flies in 0.18 s (`flightMs`), so it lands as the damage number appears (`hit_number_delay_ms`). A hero standing clear of
  the danger cells (19 §2.1) is not hit: the bolt flies on and fades.
- Impacts land on the hero's chest.

## 6. The target

The yellow bar under the targeted monster is gone. With two or three monsters, the line **目前攻擊目標** ("Current target") shows
under the target's name (`battle.target_now` in `text.json`, Q83).

## 7. The web build's "md5"

The game has no md5 check. What keeps players from mixing an old file with a new page is the **version stamp** in every file name
(docs/project/07 §3): the live site, checked on 2026-11-09, loads `toms_game.fbb5cad-20261107132554.js` (and its `.wasm`, `.data`, and the
multithreaded `toms_game_mt.*`), and its `version.json` is number 4. GitHub Pages also sends an `ETag` (a hash of each file) and a 10-minute
cache time. A hash in the file name instead of the commit and time would change nothing in practice.

## 8. Built, and what was checked

| Part | Where |
|---|---|
| Sound at a pitch and volume | `Audio::f_Play(key, pitch, volume)` (`src/core/game/audio`) |
| Circles back one by one; each answer's order and swipe angle | `touch_play.*` (`f_CirclesShown`, `f_CircleNumbersShown`, `Hit::m_iOrder`, `m_fAngle`) |
| Hints, sprays, slash, bow, staff ramp | `game_skills.cpp` (`f_WandCues`, `f_HitSpray`, `f_BowGather`, `f_PlayHit`) |
| Monster attacks, projectiles | `game_combat.cpp` (`f_FoeAttackFx`, `f_LaunchBattleFx`, `f_UpdateBattleFx`) |
| The target line | `battle.rml`, `battle.rcss`, `UiBattle::m_strTargetLabel` |
| 25 effects, 9 sounds | `make_battle_fx.py` (`feel_effects`), `make_battle_sfx.py` (synthesised placeholders, Q86) |

Checked by recording hidden battle runs: the staff's hints and their return, the target line with three monsters, the golem's bolt, and
each new effect played at the hero (a scenario's `"rules"` can set `"feelFx"`). **Not seen in a real battle:** the sword's slash, the
sprays and the bow's gathering and explosion, because no test scenario lands a player hit (the touch plays have no keyboard way yet); they
were seen as effects only. **Not heard:** every test run is muted (a project rule), so the sounds were only checked to decode.

`unit.touch_play_test` was updated for the new circles but is switched off, so it was compiled, not run.
