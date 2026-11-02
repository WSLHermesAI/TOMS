// cycle_system_test.cpp — headless verification of the rebirth config loader (M8, first slice).
// Exits 0 on success, 1 on any CHECK failure. Run: ./cycle_system_test
#include "cycle_system.h"
#include <cstdio>

using namespace toms;

static int g_iChecks = 0, g_iFails = 0;
#define CHECK(cond, msg) do { ++g_iChecks; if (!(cond)) { ++g_iFails; printf("  FAIL: %s\n", msg); } } while (0)

int main() {
    // A missing file loads the documented bible defaults, never a crash or a refusal.
    CyclesConfig l_oMissing = f_LoadCyclesConfig("data/does_not_exist.json");
    CHECK(l_oMissing.m_iMaxCycles == 9, "missing file: maxCycles defaults to 9");
    CHECK(l_oMissing.m_fSkillEffectScale == 0.5f, "missing file: skillEffectScale defaults to 0.5");
    CHECK(l_oMissing.m_fStatBonusScale == 0.5f, "missing file: statBonusScale defaults to 0.5");

    // The REAL data/story/cycles.json (run from the project root, matching every other data-file
    // test in this suite).
    CyclesConfig l_oReal = f_LoadCyclesConfig("data/story/cycles.json");
    CHECK(l_oReal.m_iMaxCycles == 9, "data/story/cycles.json: maxCycles is 9, matching STORY_BIBLE.md §8's cap");
    CHECK(l_oReal.m_fSkillEffectScale == 0.5f, "data/story/cycles.json: skills carry at 0.5x effect");
    CHECK(l_oReal.m_fStatBonusScale == 0.5f, "data/story/cycles.json: atk/def bonus carries at 0.5x");

    if (g_iFails == 0) { printf("cycle_system_test: ALL PASS (%d checks)\n", g_iChecks); return 0; }
    printf("cycle_system_test: %d/%d CHECK(s) FAILED\n", g_iFails, g_iChecks);
    return 1;
}
