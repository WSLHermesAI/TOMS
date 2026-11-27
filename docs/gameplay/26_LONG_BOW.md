# 26. The long bow: an aim line that grows (2026-11-27)

中文：[zh_TW/26_LONG_BOW.md](zh_TW/26_LONG_BOW.md).

The owner (2026-11-27): "while the player clicks anywhere on the screen, the aim line starts from the player's position
... while the player keeps pressing, the aim line should auto extend to the target by time and the weapon's level (Lv 1
30 % a second, Lv 2 35 % ...) ... when it reaches the red target it stops, then the player drags to the correct
position", then "make a new weapon for this gameplay: long bow". The bow keeps its classic pull ([16](16_SKILLS_AND_PLAYS.md)
§3.2); the **long bow** (`long_bow`) is a new weapon with this aim. **Built.**

## 1. How it plays

1. **Press anywhere** and hold: the aim line starts at the hero's **bow hand**, pointing at the red target.
2. **It grows** while held: a share of the distance to the target a second, by the long bow's level (§2).
3. **Dragging** back (away from the target) **lengthens** it (×1.8 the drag, the bow's `bowPull`) and **steers** it: a small
   drag turns it a little.
4. At the **target's distance it stops growing** (a click; the line's faint rest disappears). The aim **shakes**, more the
   longer it is held (from the start, as the bow's; less with DEX), so the player keeps the tip on the centre with small
   drags.
5. **Let go**: the arrow flies to the line's tip; how close it lands to the target's middle sets the damage (§3). There is
   no charge limit: each arrow has **4 s** (`bowTimeMs`). A touch let go at once (under 0.15 s, no drag) is not a shot;
   a shot let go short lands short (a miss).

**Through a monster's special attack** (the owner, 2026-11-27): the camera keeps its normal view while the danger cells
light and the monster powers up, so the line and the target stay where they are and can be shot; every frame the target
also follows its area round the monster and the line the hero's hand (`Game::f_FollowBowPlay`, `TouchPlay::f_FollowArea`).
Only for the attack's introduction ("必殺技！") does the camera come close, and **then the player cannot attack**: the hold
is taken away with no shot (`TouchPlay::f_CancelHold`; the arrow gets its 4 s again), presses do nothing and the play is
not drawn until the introduction is over. Tests: `scenario.battle_long_bow_boss_special`, `unit.touch_play_test`.

On the screen: a gold line from the bow hand to the tip, a faint line on to the target's distance while it grows, the
charge ring at the hand (the line's reach), the drag line, and the tip **gold** while it is in the two inner rings.

## 2. The growth by level (`weapon_play.json` `long_bow` `"aimGrow"`)

| Long bow level | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| Grows a second (share of the target's distance) | 30 % | 35 % | 40 % | 45 % | 50 % | 55 % | 60 % | 65 % | 70 % | 75 % |

A row `{ "lv": n, "perSec": x }` holds from its level up. A weapon with `"aimGrow"` rows gets this aim with the play
`pullRelease`; a weapon without (the bow) keeps the classic pull.

## 3. The damage: rings, DEX and STR (`battle.json` `"plays"`)

`d` = the tip's distance from the target's middle when let go, in the battle screen's design pixels (they scale with the
screen).

```
Dx = 1 + 0.6 × DEX / (DEX + 50)          DEX widens the rings (DEX 5: 1.05, 50: 1.30, 150: 1.45, never above 1.6)
x  = d / Dx
Sb = 1 + 0.3 × STR / (STR + 100)         STR's force on a shot that lands (STR 10: 1.03, 100: 1.15, 300: 1.23, never above 1.3)
damage x = tier(x) × Sb                  (a miss stays 0)
```

| x (px) | ≤ 5 | ≤ 15 | ≤ 50 | ≤ 80 | ≤ 120 | more |
|---|---|---|---|---|---|---|
| tier | ×1.2 perfect | ×1.0 | ×0.8 | ×0.5 | ×0.1 (a graze) | ×0 miss |

The tiers are steps. The damage x multiplies the attack (after the weapon's own result row). A **graze** (×0.1) does its
damage but breaks the combo; ×0.5 and better keep it. The innermost ring counts as perfect (the perfect chain, the feel).
Data: `"bowRings": [[5, 1.2], [15, 1.0], [50, 0.8], [80, 0.5], [120, 0.1]]`, `"bowDex": [0.6, 50]`, `"bowStr": [0.3, 100]`.

| Player | Tip off by | x | Tier | Damage |
|---|---|---|---|---|
| DEX 5, STR 7 | 4 px | 3.8 | ×1.2 | 122 % |
| DEX 5, STR 7 | 40 px | 38 | ×0.8 | 82 % |
| DEX 100, STR 100 | 6 px | 4.3 | ×1.2 | 138 % |
| DEX 100, STR 100 | 150 px | 107 | ×0.1 | 12 % |

## 4. The weapon

- `items.json` `long_bow` (長弓 / Long Bow): the bow's battle art (`hero_bow.anim`), price 90, sold in the item shop
  (`store.json`). Its name, words and battle hint in `text.json` (`item.long_bow.*`, `battle.play_hint_long_bow`: a
  weapon may have its own hint; `Game::f_PlayHint`).
- **In the new game's weapon trial** (the owner, 2026-11-27: "make it selectable in the game tutorial"): listed in
  `data/weapon_trial.json` (its choice "試試長弓 / Try the long bow", its line, its how-to in the trial fight); the first
  shot's ghost finger (the bow's "drag back") is not shown for it.
- `weapon_play.json` `long_bow`: the bow's levels, results and effects, its own ultimate (Taichi Spin, §6), its play round the
  monster (`"around": "monster"`, so the line crosses the field), and `"aimGrow"`.

## 6. Its ultimate: Taichi Spin (太極旋, `taichi_spin`)

The owner (2026-11-27): "the long bow's special attack shows a Taichi picture in the centre of the game, and the player
must keep rotating it: in 5 seconds as many turns as possible; 10 turns: 500 % of a normal attack's damage, 8: 300 %,
6: 200 %, 5: 180 %, 4: 160 %, 3: 150 %, 2: 120 %, 1: 110 %; a special effect like the bow's."

- **The play** (special_moves.json `taichi_spin`, `spinCircle`): a Taichi (`assets/media/ui/battle/taichi.png`) in the ring
  in the middle of the screen (`"around": "center"`, size 340), turning with the finger; **either way counts**
  (`"anyWay": true`; going back takes off), up to 10 turns in **5 s**. Under the ring: the turns and the damage so far
  ("6 圈・200%"). No turn: it fizzles.
- **The damage** (`"turnDamage"`): the highest row reached, times a normal attack:

  | Turns | 1 | 2 | 3 | 4 | 5 | 6–7 | 8–9 | 10 |
  |---|---|---|---|---|---|---|---|---|
  | Damage | 110 % | 120 % | 150 % | 160 % | 180 % | 200 % | 300 % | 500 % |

- **The show**: Cyclone Volley's (the hero's introduction with the banner "絕招發動！ 太極旋", his charge, ten arrows up, the
  meteor shower on the monster sharing the damage, the last blast), and his shout.
- Code: `SkillPlaySpec::m_bAnyWay`, `m_vTurnDamage`, `m_strPicture`, `f_TurnDamage`; `TouchPlaySettings::m_bSpinFree`,
  `TouchPlay::f_SpinAngle`; the picture's RCSS `.tp.spinpic.taichi`. Tests: `unit.touch_play_test` (turns either way, the
  table), `scenario.battle_ultimate_taichi` (six turns: x2.0 over ten hits).

## 5. Code and tests

- `TouchPlay` (touch_play.{h,cpp}): `m_fBowGrowPerSec` > 0 is the long bow; `f_AimDir`, `f_AimLength`,
  `f_TargetDistance`, `f_AimStopped`, `f_RingTier`, `f_TipTier`, the shot's `Hit::m_fMul`, `f_ShotMul`.
- `Game`: the rate, Dx and Sb into the play (`f_TouchSettingsFor`), the bow hand every frame and the stop's click
  (`f_StepTouchPlay`), the multiplier into the damage and the graze's combo break (`f_ResolveCombo`,
  `ComboResult::m_fShotMul`), the line on the screen (game_ui.cpp; RCSS `.tp.cross.on`).
- `WeaponPlay::f_AimGrowAt`.
- Tests: `unit.touch_play_test` (the growth, the stop, steering, the rings with DEX, a quick touch, a short shot),
  `unit.weapon_play_test` (the rows; the bow has none), `scenario.battle_long_bow` (held at Lv 5, let go on the
  middle: a perfect hit).
