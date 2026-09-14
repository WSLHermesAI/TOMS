// Tests for equipment actives (裝備主動技). Fixtures are the REAL data files, so this also proves the
// authored data is coherent: every active an item references must exist in data/actives.json.
#include "equipment_actives.h"

#include <cstdio>
#include <string>

using namespace toms;

static int g_checks = 0;
static int g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    const std::string activesPath = "data/actives.json";
    const std::string equipPath   = "data/equipment.json";

    auto defs = loadActiveDefinitions(activesPath);
    auto byItem = loadEquipmentActives(equipPath);

    // --- the two actives the ability doc defines ---
    CHECK(defs.size() == 2, "data/actives.json should hold 2 actives, got %zu", defs.size());
    CHECK(defs.count("a_qingxiao_edge") == 1, "a_qingxiao_edge must exist");
    CHECK(defs.count("a_sanctuary_echo") == 1, "a_sanctuary_echo must exist");
    if (defs.count("a_qingxiao_edge")) {
        const ActiveDefinition& d = defs.at("a_qingxiao_edge");
        CHECK(d.kind == "guaranteedCrit", "a_qingxiao_edge kind should be guaranteedCrit, got '%s'", d.kind.c_str());
        CHECK(d.damageMultiplier > 1.79f && d.damageMultiplier < 1.81f,
              "a_qingxiao_edge should be x1.8, got %.2f", d.damageMultiplier);
        CHECK(d.usesPerBattle == 1, "a_qingxiao_edge is once per battle");
    }
    if (defs.count("a_sanctuary_echo")) {
        const ActiveDefinition& d = defs.at("a_sanctuary_echo");
        CHECK(d.kind == "surviveLethal", "a_sanctuary_echo kind should be surviveLethal, got '%s'", d.kind.c_str());
        CHECK(d.usesPerBattle == 1, "a_sanctuary_echo is once per battle");
        CHECK(d.vfxFrames == 8, "the doc says the bell VFX is 8 frames, got %d", d.vfxFrames);
    }

    // --- data integrity: every referenced active must be defined ---
    int referenced = 0, missing = 0;
    for (const auto& kv : byItem)
        for (const std::string& id : kv.second) {
            ++referenced;
            if (defs.count(id) == 0) { ++missing; std::printf("  missing active: %s (on %s)\n", id.c_str(), kv.first.c_str()); }
        }
    CHECK(referenced == 2, "2 item->active links expected (the two doc items), got %d", referenced);
    CHECK(missing == 0, "every referenced active must exist in actives.json (%d missing)", missing);

    // --- which actives an equipped set grants ---
    auto set = activesForSet("qingxiao_blade", "", "soul_echo_bell", byItem);
    CHECK(set.size() == 2, "the doc set should grant 2 actives, got %zu", set.size());
    CHECK(!set.empty() && set[0] == "a_qingxiao_edge", "weapon slot comes first");
    auto none = activesForSet("cloth_robe", "wand", "", byItem);
    CHECK(none.empty(), "items with no actives grant none, got %zu", none.size());
    auto dup = activesForSet("qingxiao_blade", "qingxiao_blade", "qingxiao_blade", byItem);
    CHECK(dup.size() == 1, "the same active on several slots is granted once, got %zu", dup.size());

    // --- runtime: per-battle uses ---
    auto rt = makeBattleRuntime(defs);
    ActiveStatusEffects st;
    CHECK(canUseActive("a_qingxiao_edge", defs, rt, st), "available at the start of a battle");

    ActiveEffect e1 = useActive("a_qingxiao_edge", defs, rt, st);
    CHECK(e1.fired, "the first use fires");
    CHECK(e1.guaranteedCrit, "and it guarantees a crit");
    CHECK(e1.damageMultiplier > 1.79f && e1.damageMultiplier < 1.81f, "and deals x1.8");
    CHECK(!canUseActive("a_qingxiao_edge", defs, rt, st), "once per battle: spent");
    CHECK(!useActive("a_qingxiao_edge", defs, rt, st).fired, "a spent active cannot fire again");

    ActiveEffect e2 = useActive("a_sanctuary_echo", defs, rt, st);
    CHECK(e2.fired && e2.surviveLethal, "the bell keeps you at 1 HP");
    CHECK(!e2.guaranteedCrit, "the bell is not a crit active");

    // --- st_silence blocks every active ---
    ActiveStatusEffects silenced;
    silenced.silence = true;
    CHECK(!canUseActive("a_sanctuary_echo", defs, rt, silenced), "st_silence blocks a spent... (uses gone)");
    auto rt2 = makeBattleRuntime(defs);
    CHECK(!canUseActive("a_sanctuary_echo", defs, rt2, silenced), "st_silence blocks an unspent active");
    CHECK(!useActive("a_sanctuary_echo", defs, rt2, silenced).fired, "and firing it does nothing");

    // --- cooldowns, and st_echo skipping one ---
    std::map<std::string, ActiveDefinition> local;
    ActiveDefinition cool; cool.id = "a_cool"; cool.kind = "guaranteedCrit"; cool.usesPerBattle = 3; cool.cooldownSeconds = 5.0f;
    local["a_cool"] = cool;

    auto rt3 = makeBattleRuntime(local);
    ActiveStatusEffects plain;
    ActiveEffect c1 = useActive("a_cool", local, rt3, plain);
    CHECK(c1.fired && rt3["a_cool"].usesLeft == 2, "a cooldown active still spends a use");
    CHECK(rt3["a_cool"].cooldownLeft > 4.9f, "and goes on cooldown (%.2f)", rt3["a_cool"].cooldownLeft);
    CHECK(!canUseActive("a_cool", local, rt3, plain), "and cannot be used while cooling");
    CHECK(tickActives(rt3, 2.0f) == 0, "halfway through: nothing became ready");
    CHECK(tickActives(rt3, 3.5f) == 1, "past 5s: exactly one became ready");
    CHECK(canUseActive("a_cool", local, rt3, plain), "and it is usable again");
    CHECK(tickActives(rt3, 0.0f) == 0, "a zero dt tick is a no-op");

    // st_echo: "your next active does NOT go on cooldown", consumed by that one use
    auto rt4 = makeBattleRuntime(local);
    ActiveStatusEffects echo;
    echo.echoQueued = true;
    ActiveEffect c2 = useActive("a_cool", local, rt4, echo);
    CHECK(c2.fired, "with st_echo it still fires");
    CHECK(rt4["a_cool"].cooldownLeft == 0.0f, "st_echo kept it off cooldown (%.2f)", rt4["a_cool"].cooldownLeft);
    CHECK(!echo.echoQueued, "and st_echo was consumed by that use");
    ActiveEffect c3 = useActive("a_cool", local, rt4, echo);
    CHECK(rt4["a_cool"].cooldownLeft > 4.9f, "the NEXT use does go on cooldown (%.2f)", rt4["a_cool"].cooldownLeft);

    // --- unknown ids fail safe ---
    CHECK(!canUseActive("a_nonexistent", defs, rt, st), "an unknown active is never usable");
    CHECK(!useActive("a_nonexistent", defs, rt, st).fired, "and never fires");

    // --- an absent/malformed table behaves like no actives, never a crash ---
    CHECK(loadActiveDefinitions("data/definitely_not_here.json").empty(), "a missing table loads empty");
    CHECK(loadEquipmentActives("data/definitely_not_here.json").empty(), "and so does a missing item table");

    if (g_failed == 0) std::printf("equipment_actives_test: ALL PASS (%d checks)\n", g_checks);
    else               std::printf("equipment_actives_test: FAILED (%d checks, %d failed)\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
