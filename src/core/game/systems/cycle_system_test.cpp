// cycle_system_test.cpp — headless verification of the rebirth config loader (M8, first slice).
// Exits 0 on success, 1 on any CHECK failure. Run: ./cycle_system_test
#include "cycle_system.h"
#include <cstdio>

using namespace toms;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_fails; printf("  FAIL: %s\n", msg); } } while (0)

int main() {
    // A missing file loads the documented bible defaults, never a crash or a refusal.
    CyclesConfig missing = loadCyclesConfig("data/does_not_exist.json");
    CHECK(missing.maxCycles == 9, "missing file: maxCycles defaults to 9");
    CHECK(missing.skillEffectScale == 0.5f, "missing file: skillEffectScale defaults to 0.5");
    CHECK(missing.statBonusScale == 0.5f, "missing file: statBonusScale defaults to 0.5");

    // The REAL data/story/cycles.json (run from the project root, matching every other data-file
    // test in this suite).
    CyclesConfig real = loadCyclesConfig("data/story/cycles.json");
    CHECK(real.maxCycles == 9, "data/story/cycles.json: maxCycles is 9, matching STORY_BIBLE.md §8's cap");
    CHECK(real.skillEffectScale == 0.5f, "data/story/cycles.json: skills carry at 0.5x effect");
    CHECK(real.statBonusScale == 0.5f, "data/story/cycles.json: atk/def bonus carries at 0.5x");

    if (g_fails == 0) { printf("cycle_system_test: ALL PASS (%d checks)\n", g_checks); return 0; }
    printf("cycle_system_test: %d/%d CHECK(s) FAILED\n", g_fails, g_checks);
    return 1;
}
