// Tests for equipment actives (裝備主動技). Fixtures are the REAL data files, so this also proves the
// authored data is coherent: every active an item references must exist in data/actives.json.
#include "equipment_actives.h"

#include <cstdio>
#include <string>

using namespace toms;

static int g_iChecks = 0;
static int g_iFailed = 0;
#define CHECK(cond, ...) do { ++g_iChecks; if (!(cond)) { ++g_iFailed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    const std::string l_strActivesPath = "data/actives.json";
    const std::string l_strEquipPath   = "data/items.json";   // the all-item file: weapons, armor, talents included

    auto l_mapDefs = f_LoadActiveDefinitions(l_strActivesPath);
    auto l_mapByItem = f_LoadEquipmentActives(l_strEquipPath);

    // --- the two actives the ability doc defines ---
    CHECK(l_mapDefs.size() == 2, "data/actives.json should hold 2 actives, got %zu", l_mapDefs.size());
    CHECK(l_mapDefs.count("a_qingxiao_edge") == 1, "a_qingxiao_edge must exist");
    CHECK(l_mapDefs.count("a_sanctuary_echo") == 1, "a_sanctuary_echo must exist");
    if (l_mapDefs.count("a_qingxiao_edge")) {
        const ActiveDefinition& l_oD = l_mapDefs.at("a_qingxiao_edge");
        CHECK(l_oD.m_strKind == "guaranteedCrit", "a_qingxiao_edge kind should be guaranteedCrit, got '%s'", l_oD.m_strKind.c_str());
        CHECK(l_oD.m_fDamageMultiplier > 1.79f && l_oD.m_fDamageMultiplier < 1.81f,
              "a_qingxiao_edge should be x1.8, got %.2f", l_oD.m_fDamageMultiplier);
        CHECK(l_oD.m_iUsesPerBattle == 1, "a_qingxiao_edge is once per battle");
    }
    if (l_mapDefs.count("a_sanctuary_echo")) {
        const ActiveDefinition& l_oD = l_mapDefs.at("a_sanctuary_echo");
        CHECK(l_oD.m_strKind == "surviveLethal", "a_sanctuary_echo kind should be surviveLethal, got '%s'", l_oD.m_strKind.c_str());
        CHECK(l_oD.m_iUsesPerBattle == 1, "a_sanctuary_echo is once per battle");
        CHECK(l_oD.m_iVfxFrames == 8, "the doc says the bell VFX is 8 frames, got %d", l_oD.m_iVfxFrames);
    }

    // --- data integrity: every referenced active must be defined ---
    int l_iReferenced = 0, l_iMissing = 0;
    for (const auto& l_pairKv : l_mapByItem)
        for (const std::string& l_strId : l_pairKv.second) {
            ++l_iReferenced;
            if (l_mapDefs.count(l_strId) == 0) { ++l_iMissing; std::printf("  missing active: %s (on %s)\n", l_strId.c_str(), l_pairKv.first.c_str()); }
        }
    CHECK(l_iReferenced == 2, "2 item->active links expected (the two doc items), got %d", l_iReferenced);
    CHECK(l_iMissing == 0, "every referenced active must exist in actives.json (%d missing)", l_iMissing);

    // --- which actives an equipped set grants ---
    auto l_vSet = f_ActivesForSet("qingxiao_blade", "", "soul_echo_bell", l_mapByItem);
    CHECK(l_vSet.size() == 2, "the doc set should grant 2 actives, got %zu", l_vSet.size());
    CHECK(!l_vSet.empty() && l_vSet[0] == "a_qingxiao_edge", "weapon slot comes first");
    auto l_vNone = f_ActivesForSet("cloth_robe", "wand", "", l_mapByItem);
    CHECK(l_vNone.empty(), "items with no actives grant none, got %zu", l_vNone.size());
    auto l_vDup = f_ActivesForSet("qingxiao_blade", "qingxiao_blade", "qingxiao_blade", l_mapByItem);
    CHECK(l_vDup.size() == 1, "the same active on several slots is granted once, got %zu", l_vDup.size());

    // --- runtime: per-battle uses ---
    auto l_mapRt = f_MakeBattleRuntime(l_mapDefs);
    ActiveStatusEffects l_oSt;
    CHECK(f_CanUseActive("a_qingxiao_edge", l_mapDefs, l_mapRt, l_oSt), "available at the start of a battle");

    ActiveEffect l_oE1 = f_UseActive("a_qingxiao_edge", l_mapDefs, l_mapRt, l_oSt);
    CHECK(l_oE1.m_bFired, "the first use fires");
    CHECK(l_oE1.m_bGuaranteedCrit, "and it guarantees a crit");
    CHECK(l_oE1.m_fDamageMultiplier > 1.79f && l_oE1.m_fDamageMultiplier < 1.81f, "and deals x1.8");
    CHECK(!f_CanUseActive("a_qingxiao_edge", l_mapDefs, l_mapRt, l_oSt), "once per battle: spent");
    CHECK(!f_UseActive("a_qingxiao_edge", l_mapDefs, l_mapRt, l_oSt).m_bFired, "a spent active cannot fire again");

    ActiveEffect l_oE2 = f_UseActive("a_sanctuary_echo", l_mapDefs, l_mapRt, l_oSt);
    CHECK(l_oE2.m_bFired && l_oE2.m_bSurviveLethal, "the bell keeps you at 1 HP");
    CHECK(!l_oE2.m_bGuaranteedCrit, "the bell is not a crit active");

    // --- st_silence blocks every active ---
    ActiveStatusEffects l_oSilenced;
    l_oSilenced.m_bSilence = true;
    CHECK(!f_CanUseActive("a_sanctuary_echo", l_mapDefs, l_mapRt, l_oSilenced), "st_silence blocks a spent... (uses gone)");
    auto l_mapRt2 = f_MakeBattleRuntime(l_mapDefs);
    CHECK(!f_CanUseActive("a_sanctuary_echo", l_mapDefs, l_mapRt2, l_oSilenced), "st_silence blocks an unspent active");
    CHECK(!f_UseActive("a_sanctuary_echo", l_mapDefs, l_mapRt2, l_oSilenced).m_bFired, "and firing it does nothing");

    // --- cooldowns, and st_echo skipping one ---
    std::map<std::string, ActiveDefinition> l_mapLocal;
    ActiveDefinition l_oCool; l_oCool.m_strId = "a_cool"; l_oCool.m_strKind = "guaranteedCrit"; l_oCool.m_iUsesPerBattle = 3; l_oCool.m_fCooldownSeconds = 5.0f;
    l_mapLocal["a_cool"] = l_oCool;

    auto l_mapRt3 = f_MakeBattleRuntime(l_mapLocal);
    ActiveStatusEffects l_oPlain;
    ActiveEffect l_oC1 = f_UseActive("a_cool", l_mapLocal, l_mapRt3, l_oPlain);
    CHECK(l_oC1.m_bFired && l_mapRt3["a_cool"].m_iUsesLeft == 2, "a cooldown active still spends a use");
    CHECK(l_mapRt3["a_cool"].m_fCooldownLeft > 4.9f, "and goes on cooldown (%.2f)", l_mapRt3["a_cool"].m_fCooldownLeft);
    CHECK(!f_CanUseActive("a_cool", l_mapLocal, l_mapRt3, l_oPlain), "and cannot be used while cooling");
    CHECK(f_TickActives(l_mapRt3, 2.0f) == 0, "halfway through: nothing became ready");
    CHECK(f_TickActives(l_mapRt3, 3.5f) == 1, "past 5s: exactly one became ready");
    CHECK(f_CanUseActive("a_cool", l_mapLocal, l_mapRt3, l_oPlain), "and it is usable again");
    CHECK(f_TickActives(l_mapRt3, 0.0f) == 0, "a zero dt tick is a no-op");

    // st_echo: "your next active does NOT go on cooldown", consumed by that one use
    auto l_mapRt4 = f_MakeBattleRuntime(l_mapLocal);
    ActiveStatusEffects l_oEcho;
    l_oEcho.m_bEchoQueued = true;
    ActiveEffect l_oC2 = f_UseActive("a_cool", l_mapLocal, l_mapRt4, l_oEcho);
    CHECK(l_oC2.m_bFired, "with st_echo it still fires");
    CHECK(l_mapRt4["a_cool"].m_fCooldownLeft == 0.0f, "st_echo kept it off cooldown (%.2f)", l_mapRt4["a_cool"].m_fCooldownLeft);
    CHECK(!l_oEcho.m_bEchoQueued, "and st_echo was consumed by that use");
    ActiveEffect l_oC3 = f_UseActive("a_cool", l_mapLocal, l_mapRt4, l_oEcho);
    CHECK(l_mapRt4["a_cool"].m_fCooldownLeft > 4.9f, "the NEXT use does go on cooldown (%.2f)", l_mapRt4["a_cool"].m_fCooldownLeft);

    // --- unknown ids fail safe ---
    CHECK(!f_CanUseActive("a_nonexistent", l_mapDefs, l_mapRt, l_oSt), "an unknown active is never usable");
    CHECK(!f_UseActive("a_nonexistent", l_mapDefs, l_mapRt, l_oSt).m_bFired, "and never fires");

    // --- an absent/malformed table behaves like no actives, never a crash ---
    CHECK(f_LoadActiveDefinitions("data/definitely_not_here.json").empty(), "a missing table loads empty");
    CHECK(f_LoadEquipmentActives("data/definitely_not_here.json").empty(), "and so does a missing item table");

    if (g_iFailed == 0) std::printf("equipment_actives_test: ALL PASS (%d checks)\n", g_iChecks);
    else               std::printf("equipment_actives_test: FAILED (%d checks, %d failed)\n", g_iChecks, g_iFailed);
    return g_iFailed == 0 ? 0 : 1;
}
