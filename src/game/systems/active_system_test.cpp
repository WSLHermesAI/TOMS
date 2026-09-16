// active_system_test.cpp — headless verification of equipment actives' pure content (S7, first
// slice). Exits 0 on success, 1 on any CHECK failure. Run: ./active_system_test
#include "active_system.h"
#include <cstdio>

using namespace toms;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_fails; printf("  FAIL: %s\n", msg); } } while (0)

int main() {
    // 1. ActiveSkillDefinition JSON round-trip.
    {
        ActiveSkillDefinition d;
        d.id = "a_test"; d.name = "Test Active"; d.desc = "for testing";
        d.effectKind = "guaranteed_crit_next_attack"; d.damageMult = 2.0f;
        auto j = toJson(d);
        ActiveSkillDefinition d2 = activeSkillFromJson(j);
        CHECK(d2.id == "a_test", "round-trip: id");
        CHECK(d2.effectKind == "guaranteed_crit_next_attack", "round-trip: effect kind");
        CHECK(d2.damageMult == 2.0f, "round-trip: damage multiplier");
    }
    CHECK(activeSkillFromJson(nlohmann::json(nullptr)).id.empty(),
          "activeSkillFromJson(null) returns a default, not a throw");

    // 2. loadActiveSkills against the REAL data/actives.json (run from the project root, matching
    //    every other data-file test in this suite).
    auto defs = loadActiveSkills("data/actives.json");
    CHECK(defs.size() == 1, "data/actives.json has its 1 authored active so far");
    CHECK(defs.count("a_twin_flash"), "a_twin_flash exists");
    CHECK(defs["a_twin_flash"].effectKind == "guaranteed_crit_next_attack",
          "a_twin_flash's effect is guaranteed_crit_next_attack");
    CHECK(defs["a_twin_flash"].damageMult > 1.0f, "a_twin_flash's damage multiplier is a real bonus, not a no-op");
    CHECK(loadActiveSkills("data/does_not_exist.json").empty(), "a missing file loads to an empty map, not a crash");

    if (g_fails == 0) { printf("active_system_test: ALL PASS (%d checks)\n", g_checks); return 0; }
    printf("active_system_test: %d/%d CHECK(s) FAILED\n", g_fails, g_checks);
    return 1;
}
