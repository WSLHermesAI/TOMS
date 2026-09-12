# Battle System v2 — Real-Time Auto-Bar Design

> **Status:** Proposed, not implemented. This revises an earlier 3-concept draft of this same
> document after direct feedback: none of the three original concepts were right. The corrected
> requirements, as given:
> - The player is **never gated** on a readiness/ATB gauge — Attack and Defend are both available
>   at all times, with no "wait for your turn" of any kind.
> - **Attack and Defend are independent and concurrent** — the UI must support multi-touch, so a
>   player can act on both at once (e.g. one thumb on each).
> - The enemy side of the original draft was correct and is kept as-is: the enemy attacks on a
>   fixed real-time timer, every `N` seconds, independent of anything the player does.
> - The **input model itself changes**: instead of "hold a button to start the marker moving,
>   release to freeze it" (today's shipped Power Bar, `FIGHT_SCENE_DESIGN.md` §2), the marker now
>   **moves on its own, continuously**, and a single **tap** freezes it wherever it currently is.
>   After a tap, that bar pauses for `N` seconds before resuming its automatic movement — this
>   removes the need to manage a press-and-hold duration at all; the only skill is *when* to tap,
>   which is easier to perform (particularly on touch) while remaining a genuine timing test.
>
> This does not touch `MAIN_BATTLE_SCENE_DESIGN.md`'s equipment system or damage formulas — the
> zone geometry (`greenHalf`/`blueOuter`/`redOuter`) and the `computeAttackDamage`/
> `computeDefenseDamage` math in `src/engine/power_bar.cpp` are reused exactly as they exist today.
> What changes is only *how the marker moves* and *when the player is allowed to act on it*.
>
> **Live demo:** [`demos/battle_v2_realtime_demo.html`](demos/battle_v2_realtime_demo.html) — open
> directly in a browser, no build step. Two independently auto-moving bars (Attack, Defense) plus
> a real-time enemy clock; tap either bar any time, including both at once on a touchscreen.

## 1. Why change it — what's actually missing today

Confirmed by reading the current implementation (`CombatState`/`Phase` in `src/game/game.h:57-74`,
resolved in `Game::resolveAttackRelease`/`resolveDefenseRelease` in `src/game/game.cpp:2416-2474`),
today's battle scene is a skill-timed Power Bar minigame, but its round structure has three gaps
relative to what's being asked for:

1. **No player choice, no concurrency.** Every round is unconditionally Attack Bar, then Defense
   Bar, in that fixed order — never both available at once, never a real choice.
2. **No enemy-side clock.** There is no attack-interval stat anywhere in `EnemyInst`
   (`src/game/game.h:34-37`) or `data/enemies.json` — the enemy only ever "acts" the instant the
   player releases the Attack Bar. This is the one gap the original draft correctly identified and
   solved, and it's unchanged here: the enemy needs a real, independent, wall-clock timer.
3. **No multi-hit charge resource.** Nothing persists across rounds; every Power Bar press is
   independent. (Addressed by the Super-Attack Gauge, §4 — unchanged from the original draft and
   still additive/compatible with the design below.)

The design below closes all three without introducing randomness — every timer is fixed, not a
dice roll, consistent with this project's no-RNG pillar (`GAME_DESIGN_DOCUMENT.md` §1/§16).

## 2. Core loop

Three real-time elements run **simultaneously and independently** for the whole fight — nothing
pauses anything else:

```
ATTACK bar:    marker auto-bounces forever between the two edges, cubic ease-in per leg
               (identical slow -> speeding-up -> BOOST feel as today's hold-and-release bar,
               just replayed every leg instead of once per press)
               ─ tap ATTACK anytime -> freezes marker where it is -> resolves damage to enemy
                 -> bar pauses (frozen, dimmed) for attackCooldownMs -> resets to the start edge
                 and resumes auto-moving

DEFENSE bar:   identical auto-bounce, its own independent geometry/speed (as today, Defense has
               a shorter rampTime than Attack -- FIGHT_SCENE_DESIGN.md §5)
               ─ tap DEFEND anytime -> freezes marker -> banks the resulting power as a "shield"
                 (does not need an incoming hit to exist yet -- see §3) -> bar pauses for
                 defenseCooldownMs -> resumes

ENEMY clock:   fixed timer, fires every enemyIntervalMs (e.g. 4.0s), completely independent of
               either bar's state or cooldowns
               ─ on fire -> consumes whatever shield is currently banked (if any) and applies
                 mitigated damage to the player; if nothing is banked, the hit lands in full
```

Because both bars are always live and neither blocks the other, this is naturally multi-touch: on
a touchscreen, ATTACK and DEFEND are two separate buttons/targets and each fires its own
press event independently, so tapping both within the same instant (two fingers) works with no
special-cased input code — it's just two ordinary, unrelated button presses.

## 3. The "shield" — how pre-defense actually resolves

Tapping DEFEND doesn't need to know when the next enemy hit is coming — it locks in a power value
immediately (from the auto-mover's current position, using the unchanged
`powerFromPosition`/`zoneFromPosition` math) and **banks it** as a pending shield. The enemy
clock, whenever it fires, spends the current shield (if any) via the existing, unchanged
`computeDefenseDamage(incoming, bankedPower)` and clears it — a second DEFEND tap before the enemy
fires simply overwrites the banked value with the newer one (most recent tap wins; no stacking).
If the player never taps DEFEND between one enemy hit and the next, the following hit resolves
with `power = 0`, i.e. full unmitigated damage — identical to whiffing the bar entirely today. This
means:

- Good play is proactively tapping DEFEND every `defenseCooldownMs` or so, aiming for the moving
  marker's Green zone, so a shield is (almost) always banked by the time the enemy clock fires.
- The interesting decision is entirely about **when** to spend attention on ATTACK vs. DEFENSE,
  since both bars are independently on cooldown and the enemy clock doesn't wait for either —
  greedily tapping ATTACK back-to-back while ignoring DEFENSE means eating full damage every
  `enemyIntervalMs`.

## 4. Super-Attack Gauge (unchanged from the original draft, still additive)

- A `superCharge` counter (0..5) on `CombatState`, +1 per successful ATTACK tap that deals
  damage > 0.
- At `superCharge == 5`, a Super Attack button becomes available: tapping it deals a guaranteed
  strong hit (`computeAttackDamage(baseHit, 100, maxMult)` — today's Perfect-tier ceiling, no
  timing required) and resets the counter to 0. It does not interrupt or share a cooldown with the
  regular ATTACK bar — it's a separate, always-or-never-available action.

## 5. Tuning defaults

| Parameter | Value |
|---|---|
| Enemy attack interval | 4.0s |
| Attack bar speed/geometry | unchanged from `FIGHT_SCENE_DESIGN.md` §5 (`v0=15, vmax=200, rampTime=1.8s`) |
| Defense bar speed/geometry | unchanged from `FIGHT_SCENE_DESIGN.md` §5 (`v0=18, vmax=220, rampTime=1.4s`) |
| Attack cooldown after a tap | 1.5s |
| Defense cooldown after a tap | 1.5s |
| Super Attack threshold | 5 successful hits |

All five are starting points for playtesting, not final balance — the same caveat
`MAIN_BATTLE_SCENE_DESIGN.md` §4.3 already carries for its equipment numbers.

## 6. Suggested next step

If this direction is confirmed, the actual implementation would touch:

- `src/game/game.h`/`game.cpp` — `CombatState` gains two independent auto-moving bar states (each
  with its own position/direction/leg-timer/cooldown-until fields) instead of the current
  press/release `Phase` enum, plus a `bankedDefensePower` field and an always-ticking
  `enemyClockMs`.
- `data/enemies.json` (+ `EnemyInst`, `game.h:34-37`) — a new `atkIntervalMs` field per enemy.
- `src/game/main.cpp` / `src/engine/emscripten_main.cpp` — input handling changes from
  press-duration tracking to simple single-tap detection per bar, and needs to accept two
  simultaneous touch points instead of the current single-touch-target assumption.
- `docs/MAIN_BATTLE_SCENE_DESIGN.md` — update §2 (screen layout: both bars are now on-screen and
  interactive simultaneously, not "only one bar interactive at a time") and §3 (round flow, no
  longer round-based at all).

No `src/`, `data/`, or build changes have been made as part of this task — this document and its
demo are the only output.
