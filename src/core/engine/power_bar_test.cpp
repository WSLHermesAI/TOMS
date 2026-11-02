// power_bar_test.cpp — headless verification of the Power Bar minigame math (Milestone 6).
// Exits 0 on success, 1 on any CHECK failure. Run: ./power_bar_test
#include "power_bar.h"
#include <cstdio>
#include <cmath>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

static bool f_Near(float l_fA, float l_fB, float l_fEps = 0.01f) { return std::fabs(l_fA - l_fB) < l_fEps; }

int main() {
    PowerBarParams l_oDef;   // default geometry: v0=15,vmax=200,rampTime=1.8,green=15,blue=35,red=60

    // 1. At t=0 (no hold at all), the marker hasn't moved from its starting position.
    CHECK(f_SimulatePosition(l_oDef, 0.0f) == 0.0f, "simulatePosition(0) starts at position 0");

    // 2. The marker never leaves its travel range [0, 2*redOuter], across a range of hold times.
    bool l_bEverOutOfRange = false;
    for (float l_fT = 0.0f; l_fT <= 6.0f; l_fT += 0.037f) {
        float l_fPos = f_SimulatePosition(l_oDef, l_fT);
        if (l_fPos < -0.01f || l_fPos > 2.0f * l_oDef.m_fRedOuter + 0.01f) l_bEverOutOfRange = true;
    }
    CHECK(!l_bEverOutOfRange, "marker position always stays within [0, 2*redOuter]");

    // 3. Determinism: the same held duration always simulates to the exact same position,
    //    regardless of how many times it's computed -- the design's core promise.
    float l_fP1 = f_SimulatePosition(l_oDef, 1.234f);
    float l_fP2 = f_SimulatePosition(l_oDef, 1.234f);
    CHECK(l_fP1 == l_fP2, "simulatePosition is deterministic for the same heldSeconds");

    // 4. powerFromPosition: center = 100, and the documented boundary values at each zone edge.
    float l_fCenter = l_oDef.m_fRedOuter;
    CHECK(f_Near(f_PowerFromPosition(l_oDef, l_fCenter), 100.0f), "power at dead center is 100");
    CHECK(f_Near(f_PowerFromPosition(l_oDef, l_fCenter + l_oDef.m_fGreenHalf), 71.0f), "power at green edge is 71");
    CHECK(f_Near(f_PowerFromPosition(l_oDef, l_fCenter - l_oDef.m_fGreenHalf), 71.0f), "power at green edge (other side) is 71");
    CHECK(f_Near(f_PowerFromPosition(l_oDef, l_fCenter + l_oDef.m_fBlueOuter), 31.0f), "power at blue edge is 31");
    CHECK(f_Near(f_PowerFromPosition(l_oDef, l_fCenter + l_oDef.m_fRedOuter), 0.0f), "power at the bar's own edge (redOuter) is 0");
    CHECK(f_Near(f_PowerFromPosition(l_oDef, l_fCenter - l_oDef.m_fRedOuter), 0.0f), "power at the bar's own edge, other side, is 0");

    // 5. zoneFromPosition classifies each region correctly.
    CHECK(f_ZoneFromPosition(l_oDef, l_fCenter) == PowerZone::Green, "zone at center is Green");
    CHECK(f_ZoneFromPosition(l_oDef, l_fCenter + l_oDef.m_fGreenHalf) == PowerZone::Green, "zone at green edge is still Green (inclusive)");
    CHECK(f_ZoneFromPosition(l_oDef, l_fCenter + l_oDef.m_fGreenHalf + 1) == PowerZone::Blue, "zone just past green edge is Blue");
    CHECK(f_ZoneFromPosition(l_oDef, l_fCenter + l_oDef.m_fBlueOuter) == PowerZone::Blue, "zone at blue edge is still Blue (inclusive)");
    CHECK(f_ZoneFromPosition(l_oDef, l_fCenter + l_oDef.m_fBlueOuter + 1) == PowerZone::Red, "zone just past blue edge is Red");
    CHECK(f_ZoneFromPosition(l_oDef, l_fCenter + l_oDef.m_fRedOuter) == PowerZone::Red, "zone at the bar's own edge is Red");

    // 6. computeAttackDamage: the documented power_mult curve, and the P>=97 Perfect bonus.
    CHECK(f_ComputeAttackDamage(10, 0.0f) == 2, "P=0 -> 0.2x multiplier (ceil(10*0.2)=2)");
    // P=100 also satisfies the P>=97 Perfect condition, so the +25% bonus stacks on top of the
    // 2.0x ceiling: ceil(10*2.0)=20, then ceil(20*1.25)=25.
    CHECK(f_ComputeAttackDamage(10, 100.0f) == 25, "P=100 -> 2.0x multiplier AND the Perfect bonus both apply");
    // P~=44% should reproduce the old deterministic base_hit almost exactly (GAME_DESIGN_DOCUMENT's
    // documented emergent property) -- verified here at baseHit=8 exactly as docs/design/FIGHT_SCENE_DESIGN.md's
    // own worked example does.
    CHECK(f_ComputeAttackDamage(8, 44.0f) == 8, "P~=44% reproduces the old base_hit (8) almost exactly");
    CHECK(f_ComputeAttackDamage(10, 97.0f) > f_ComputeAttackDamage(10, 96.0f),
          "crossing the P>=97 Perfect threshold increases damage discontinuously");
    // Equipment can raise the ceiling above the 2.0x baseline (docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.3,
    // e.g. War Hammer maxMult=2.6); the 2-arg call above already covers the 2.0x default.
    // Same Perfect-bonus stacking at P=100: ceil(10*2.6)=26, then ceil(26*1.25)=33.
    CHECK(f_ComputeAttackDamage(10, 100.0f, 2.6f) == 33, "a weapon's higher maxMult raises the P=100 ceiling, Perfect bonus still stacks");
    CHECK(f_ComputeAttackDamage(10, 0.0f, 2.6f) == 2, "maxMult doesn't change the P=0 floor (still 0.2x for any weapon)");

    // 7. computeDefenseDamage: P=0 is exactly the old unmitigated `incoming` (pure upside design),
    //    and P>=99 is Perfect Guard (zero damage).
    CHECK(f_ComputeDefenseDamage(7, 0.0f) == 7, "P=0 -> full incoming damage (whiffing is exactly as safe as no Power Bar)");
    CHECK(f_ComputeDefenseDamage(10, 50.0f) == 5, "P=50 -> 50% mitigation");
    CHECK(f_ComputeDefenseDamage(10, 99.0f) == 0, "P>=99 -> Perfect Guard, zero damage");
    CHECK(f_ComputeDefenseDamage(10, 98.0f) > 0, "P=98 (just under Perfect Guard) still takes some damage");

    // A talent's mitigation floor (e.g. Guardian's +10%) guarantees at least that much
    // mitigation even on a total whiff, but Perfect Guard still overrides it entirely.
    CHECK(f_ComputeDefenseDamage(10, 0.0f, 0.10f) == 9, "a 10% floor still applies on a total whiff (P=0): ceil(10*0.9)=9");
    CHECK(f_ComputeDefenseDamage(10, 50.0f, 0.10f) == 5, "a floor below the actual power% does nothing (50% already beats the 10% floor)");
    CHECK(f_ComputeDefenseDamage(10, 99.0f, 0.10f) == 0, "Perfect Guard still zeroes damage even with a mitigation floor set");

    if (g_iFail == 0) { printf("power_bar_test: ALL PASS (29 checks)\n"); return 0; }
    printf("power_bar_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}
