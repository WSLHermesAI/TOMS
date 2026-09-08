// power_bar.h — the Attack/Defense Power Bar minigame's pure math: the cubic ease-in marker
// simulation, the zone/power computation, and the resulting damage formulas.
// See FIGHT_SCENE_DESIGN.md §2/§4 and docs/IMPLEMENTATION_ROADMAP.md Milestone 6.
//
// Deliberately pure and deterministic: simulatePosition(heldSeconds) integrates from t=0 in
// small fixed steps rather than accumulating the caller's variable per-frame dt, so two
// press/release timestamps that hold for the same duration always resolve to the exact same
// position/power/damage, regardless of actual frame timing -- the design's own stated promise
// ("the added variance is player-timed, not seeded").
#pragma once
#include <cstdint>

namespace toms {

// The 6 numbers equipment is allowed to tune (MAIN_BATTLE_SCENE_DESIGN.md §4): v0/vmax/rampTime
// shape the slow->speeding-up->Boost speed curve; greenHalf/blueOuter/redOuter are the three
// zone radii from the bar's center, and redOuter also defines the bar's own edge/length
// (the marker bounces within [0, 2*redOuter]).
struct PowerBarParams {
    float v0 = 15.0f, vmax = 200.0f, rampTime = 1.8f;
    float greenHalf = 15.0f, blueOuter = 35.0f, redOuter = 60.0f;
};

enum class PowerZone { Red, Blue, Green };

// Simulates where the marker is after being held for `heldSeconds`, starting from position 0
// moving in the +direction, bouncing between 0 and 2*redOuter. `stepSeconds` is the internal
// integration step (not the caller's frame dt) -- smaller is more precise but slower; the
// default (1ms) is precise enough that visually-relevant rounding never differs from a much
// smaller step.
float simulatePosition(const PowerBarParams& p, float heldSeconds, float stepSeconds = 0.001f);

// Distance-from-center -> power 0..100, piecewise-linear so that whatever the three radii are,
// green always means power 71-100, blue always 31-70, red always 0-30 (FIGHT_SCENE_DESIGN.md §2).
float powerFromPosition(const PowerBarParams& p, float position);

// Which of the three bands `position` falls in.
PowerZone zoneFromPosition(const PowerBarParams& p, float position);

// Attack Bar -> damage dealt (FIGHT_SCENE_DESIGN.md §4). `baseHit` is the unchanged
// max(1, attacker.atk - target.def) formula, computed by the caller. `maxMult` is the power
// ceiling at P=100 -- 2.0 (the baseline default) unless a weapon raises it
// (MAIN_BATTLE_SCENE_DESIGN.md §4.3, e.g. Twin Daggers 2.2, War Hammer 2.6); the multiplier
// curve is power_mult = 0.2 + (maxMult-0.2) * (P/100), which reduces to the original
// 0.2 + 1.8*(P/100) at the baseline maxMult=2.0.
int computeAttackDamage(int baseHit, float powerPercent, float maxMult = 2.0f);

// Defense Bar -> damage taken. `incoming` is the unchanged max(1, enemy.atk - player.def)
// formula, computed by the caller. Perfect Guard (power >= 99) zeroes it entirely, regardless
// of `mitigationFloor`. `mitigationFloor` is a minimum mitigation percentage (0.0-1.0) a talent
// can guarantee even on a whiffed bar (MAIN_BATTLE_SCENE_DESIGN.md §4.2's Guardian talent: a
// flat +10% floor) -- 0.0 (the default) reproduces the original no-floor behavior exactly.
int computeDefenseDamage(int incoming, float powerPercent, float mitigationFloor = 0.0f);

} // namespace toms
