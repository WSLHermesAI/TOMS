# Fight Scene Design — Attack / Defense Power Bar

> **Status:** Proposed. This replaces the purely passive "both sides auto-punch every 700ms"
> combat animation described in `FIGHTING_TALKING_DESIGN.md` §1 with an **active, skill-timed**
> version of the same fight. It does not touch the underlying stat model (`atk`/`def`/`hp` from
> `data/enemies.json` / `combat.json`) or the game's no-RNG promise (see
> `GAME_DESIGN_DOCUMENT.md` Pillar 1) — it adds a **player-controlled variance** on top of the
> existing deterministic base formula, not a random one. Two outcomes with the same press/release
> timestamps always resolve identically.
>
> **Live demo:** open [`demos/fight_scene_demo.html`](demos/fight_scene_demo.html) in a browser —
> press-and-hold each bar's button, release, and see the resulting damage computed live from the
> formulas below. **Diagram:** [`demos/fight_scene_bar_diagram.svg`](demos/fight_scene_bar_diagram.svg).

## 1. Why change it

The current fight scene (`FIGHTING_TALKING_DESIGN.md` §1.3) is a fixed 700ms-per-round animation
with no player input at all once combat starts — the outcome is 100% predetermined the moment the
player walks into the monster tile. That's correct for the *strategic* layer (Pillar 1: the player
should know the outcome before committing), but it means the fight *itself* — the 2–5 seconds of
actual animation — is pure spectation. The Power Bar below keeps the strategic layer intact
(the player still reads stats and plans a route beforehand) while giving the *execution* of each
round something to do with their hands: a timing skill-check that rewards good reflexes without
introducing any randomness the player didn't control.

## 2. Core mechanic — the Power Bar

A horizontal bar laid out **symmetrically around its center**: red at both far edges, blue
flanking the middle, green in the center — matching a classic "stop the swinging needle in the
middle" timing bar rather than a one-directional ramp. Its total length isn't a fixed 0–100 —
it's exactly `2 × redOuter` (see below), so the whole bar scales with that one parameter:

```
 0          red/blue boundary       CENTER (=redOuter)      blue/red boundary       2×redOuter
 |———— RED ————|———— BLUE ————|———————— GREEN ————————|———— BLUE ————|———— RED ————|
```

**Movement:** while the player holds the button, a marker starts at position **0** (the left
edge) and moves right, **bouncing between 0 and `2×redOuter`** for as long as the button stays
down. Speed
does **not** ramp up linearly — it follows a cubic ease-in curve so the feel is distinctly
three-staged: crawling at first, then visibly speeding up, then a late **boost** where it jumps to
near-max speed all at once:

```
progress = min(1, t / rampTime)            // t = seconds since press began, 0..1
speed(t) = V0 + (Vmax - V0) × progress³    // cubic ease-in
position += direction × speed(t) × dt      // direction flips on hitting 0 or 100
```

Because it's a cube, the first half of `rampTime` only accounts for ~12% of the speed range —
that's the deliberately "very slow" opening window, good for a calm, low-precision release near
an edge (Red). The next chunk (50–80% of `rampTime`) is where it visibly **speeds up** — roughly
another 40% of the speed range — this is the "aim for Blue/Green" skill window. The final 20% of
`rampTime` alone covers the remaining ~49% of the speed range: a sudden, unmistakable **boost**
into near-max speed, after which the marker stays capped at `Vmax` for as long as the button is
still held. A UI cue (e.g. the charge-state label flashing "BOOST!") should trigger once
`speed(t) ≥ 0.85 × Vmax`, so the player *feels* the transition, not just experiences it as a
number.

**Release freezes the marker.** Power is then derived from **how close the frozen position is to
the center**, not from the raw position itself. The bar has **three independently adjustable
radii**, measured outward from center — `greenHalf`, `blueOuter`, and `redOuter` — and critically,
**`redOuter` also defines the bar's own edge**, i.e. its half-width. There is no separate, hidden
"100%" endpoint: whatever `redOuter` is set to *is* where the marker bounces:

```
center = redOuter                               // the bar spans [0, 2×redOuter]
dist    = |position - center|                    // 0 at dead center … redOuter at either edge
```

Power is a piecewise-linear function of `dist` against the three radii, so that **whatever the
three values are, green always means power 71–100%, blue always 31–70%, red always 0–30%**,
exactly per the original spec:

```
if dist <= greenHalf:  power = 100 - 29 × (dist / greenHalf)                              // 100 → 71
elif dist <= blueOuter: power = 70  - 39 × ((dist-greenHalf)/(blueOuter-greenHalf))        // 70 → 31
else:                    power = 30  - 30 × ((dist-blueOuter)/(redOuter-blueOuter))         // 30 → 0
```

**Default geometry: `greenHalf = 15`, `blueOuter = 35`, `redOuter = 60`** → the bar spans 0–120
(center at 60); green is the center 30 units wide (25% of the bar), blue is 20 units on each side
(33% total), and red — now the *largest* band, deliberately — is 25 units on each side (42% of the
bar), matching a reference layout where the red edges read as the widest visible zones.

**All three radii are exactly what equipment is allowed to change** (§4 of
`MAIN_BATTLE_SCENE_DESIGN.md`) — a weapon doesn't just change `V0`/`Vmax`/`rampTime` (how fast the
marker moves), it can also widen or narrow the green/blue bands, *and* stretch or shrink the bar's
total length via `redOuter` (a bigger `redOuter` means a longer bar to cross at the same raw
speed, which reads as calmer/heavier; a smaller one reads as compact/frantic) — independent of the
speed curve. Widening green is a real mechanical buff (more of the bar rewards top-tier power),
not just a visual difference.

This is the classic "stop the moving needle on the target" power-meter used in golf/pitching/
reload-style minigames — the tension comes from the fact that **the needle starts slow and only
gets fast late**, so an early bail-out is easy and safe (near an edge, Red), a mid-hold release
aimed at Blue/Green requires real but manageable timing, and holding out for a *late, precise*
center release means fighting the needle right as it enters its Boost — the hardest, highest-risk
window of the whole charge. That three-act shape (calm → tension → chaos) is the skill expression,
not just a speed number.

## 3. Two bars: Attack and Defense

A combat round is now:

1. **Attack Bar** — the player charges and releases to determine how hard *their* hit lands.
2. If the enemy survives, **Defense Bar** — the player charges and releases to determine how well
   they block the enemy's retaliation.
3. Repeat until either side's HP reaches 0.

This mirrors the existing "player hits, enemy retaliates" round structure
(`FIGHTING_TALKING_DESIGN.md` §1.3) exactly — it just turns each half of the round into a timed
input instead of an automatic exchange.

## 4. Damage formulas

Both bars build on the **existing, unchanged** base formula from `combat.json`:

```
base_hit = max(1, attacker.atk - target.def)     // same formula as today
incoming = max(1, enemy.atk   - player.def)       // same formula as today
```

### Attack Bar → damage dealt

```
power_mult = 0.2 + 1.8 × (P_atk / 100)     // 0.2× at P=0 … 2.0× at P=100
dmg_dealt  = ceil(base_hit × power_mult)
if P_atk >= 97:  dmg_dealt = ceil(dmg_dealt × 1.25)     // Perfect bonus
```

Landing at **P ≈ 44%** (solidly inside Blue) reproduces the *old* deterministic `base_hit` almost
exactly (0.2 + 1.8×0.44 ≈ 1.0×) — a nice emergent property, not a hard-coded rule: an average,
unskilled player lands close to today's balance, a skilled player who reliably reaches Green
meaningfully outdamages it, and a player who panics and releases in early Red underperforms it.
**This means every floor's existing balance (bestiary HP values, boss fight, etc.) still holds as
the baseline** — the Power Bar shifts the distribution around that baseline rather than replacing
it.

### Defense Bar → damage taken

```
mitigation = P_def / 100                     // 0% at P=0 … 100% at P=100
dmg_taken  = ceil(incoming × (1 - mitigation))
if P_def >= 99:  dmg_taken = 0                // Perfect Guard
```

Here P=0 (whiffing the bar entirely / releasing instantly) reproduces the *old* system's full,
unmitigated `incoming` damage — so Defense is a **pure upside** layered on top of the old passive
baseline: doing nothing is exactly as safe as today's auto-combat, and any real attempt at timing
strictly helps.

### Perfect Guard counter (optional, recommended)

A Perfect Guard (P_def ≥ 99) not only zeroes the incoming hit, it should apply a small bonus to
the *next* Attack Bar round (e.g. +10% power_mult, or an automatic +10 flat bonus damage) — a
risk/reward payoff for players skilled enough to consistently land the hardest timing window,
without touching the underlying stat model.

## 5. Tuning defaults

| Parameter | Attack Bar | Defense Bar |
|---|---|---|
| `V0` (start speed, %/s) | 15 | 18 |
| `Vmax` (cap, %/s) | 200 | 220 |
| `rampTime` (s to reach Vmax) | 1.8 | 1.4 |
| `greenHalf` (green radius from center) | 15 | 15 |
| `blueOuter` (blue outer radius from center) | 35 | 35 |
| `redOuter` (red outer radius = the bar's own edge) | 60 | 60 |
| Perfect threshold | power ≥97% | power ≥99% |
| Boost UI cue triggers at | ~1.4s held (speed ≥0.85×Vmax) | ~1.1s held |

Defense has a shorter `rampTime` than Attack — guarding is meant to reach its Boost phase sooner,
making it the tighter, higher-tension skill-check of the two, consistent with it being the "pure
upside" bar (§4). All six numbers per bar (`V0`/`Vmax`/`rampTime`/`greenHalf`/`blueOuter`/
`redOuter`) are exactly the values equipment is allowed to modify — see
`MAIN_BATTLE_SCENE_DESIGN.md` §4 (Equipment System): `V0`/`Vmax`/`rampTime` change the shape of
the slow→ramp→boost speed curve, while `greenHalf`/`blueOuter`/`redOuter` change both how wide the
target bands are *and* how long the bar itself is (since `redOuter` is the edge) — a weapon or
armor can tune the speed axis and the geometry axis independently (e.g. long-ramp-but-narrow,
short-ramp-but-wide are both valid, distinct builds).

## 6. Round pseudocode

```
function fightRound(player, enemy):
    P_atk = runPowerBar(ATTACK_PARAMS)
    dmg = computeAttackDamage(player.atk, enemy.def, P_atk)
    enemy.hp -= dmg
    if enemy.hp <= 0: return WIN

    P_def = runPowerBar(DEFENSE_PARAMS)
    dmg = computeDefenseDamage(enemy.atk, player.def, P_def)
    player.hp -= dmg
    if player.hp <= 0: return LOSE

    return CONTINUE   // next round: Attack Bar again
```

## 7. Open tuning questions

- [ ] Should the marker's bounce direction/speed be visible *before* the player presses (a
      "preview" tick) or should the very first press always be a blind 0→100 sweep? (Recommend:
      blind first press, matches "reflexes," but revisit after playtesting.)
- [ ] Is a flat `0.2×–2.0×` multiplier range appropriate for every enemy, or should the boss
      (Vorkath, `data/enemies.json`) narrow the window (harsher Vmax/accel) to keep the final
      fight tense even at high player stats?
- [ ] Should losing (HP → 0) still return the player to the floor entrance with stats retained,
      per the existing `combat.json: "lose"` rule? (Recommend: yes, unchanged — this system changes
      *how* a round resolves, not the death/retry contract.)
- [ ] Should `greenHalf`/`blueOuter`/`redOuter` ever shrink to near-0 for a specific enemy (e.g. the boss) so
      that even the best available equipment can't make center-hits trivial — i.e. should enemies,
      not just gear, get a say in zone geometry? (Currently only equipment tunes geometry, per
      `MAIN_BATTLE_SCENE_DESIGN.md` §4 — worth revisiting once the boss fight is played by hand.)
