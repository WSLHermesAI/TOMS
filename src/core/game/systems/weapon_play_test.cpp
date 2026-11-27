// weapon_play_test.cpp -- toms::WeaponPlay rules and the shipped data/weapon_play.json (weapon_play.h). Exits 0 on success.
// Runs in assets/ (tests/CMakeLists.txt), so the data file is "data/weapon_play.json".
#include "weapon_play.h"
#include "combo_play.h"
#include "play_type.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

int main() {
    // 1. The owner's example (2026-10-20): Lv 5 back to the start, Lv 10 back 5 points, Lv 15 back 4.
    {
        const nlohmann::json l_jJ = nlohmann::json::parse(R"({"missBack":[{"lv":15,"back":4},{"lv":5,"back":"start"},{"lv":10,"back":5}]})");
        const WeaponPlay l_oW = f_WeaponPlayFromJson("example", l_jJ);
        CHECK(l_oW.m_vMissBack.size() == 3 && l_oW.m_vMissBack[0].m_iLv == 5, "rules sorted by level");
        CHECK(f_MissBackPoints(l_oW, 1) == -1, "below every rule: back to the start");
        CHECK(f_MissBackPoints(l_oW, 5) == -1 && f_MissBackPoints(l_oW, 9) == -1, "Lv 5-9: back to the start");
        CHECK(f_MissBackPoints(l_oW, 10) == 5 && f_MissBackPoints(l_oW, 14) == 5, "Lv 10-14: back 5");
        CHECK(f_MissBackPoints(l_oW, 15) == 4 && f_MissBackPoints(l_oW, 99) == 4, "Lv 15+: back 4");
        CHECK(f_StepAfterMiss(l_oW, 5, 7) == 0, "Lv 5: step 7 -> the start");
        CHECK(f_StepAfterMiss(l_oW, 10, 7) == 2, "Lv 10: step 7 -> 2");
        CHECK(f_StepAfterMiss(l_oW, 15, 3) == 0, "never below the start");
    }
    // 2. Lookup: own entry, else "default", else the built-in rule.
    {
        WeaponPlayTable l_oT;
        CHECK(f_MissBackPoints(l_oT.f_Get("anything"), 30) == -1, "no file: the built-in rule (back to the start)");
        l_oT.m_mapWeapons["default"] = f_WeaponPlayFromJson("default", nlohmann::json::parse(R"({"missBack":[{"lv":1,"back":2}]})"));
        l_oT.m_mapWeapons["wand"] = f_WeaponPlayFromJson("wand", nlohmann::json::parse(R"({"missBack":[{"lv":1,"back":1}],"missWaitMs":400})"));
        CHECK(l_oT.f_Get("wand").m_iMissWaitMs == 400 && f_MissBackPoints(l_oT.f_Get("wand"), 1) == 1, "own entry");
        CHECK(f_MissBackPoints(l_oT.f_Get("unknown_sword"), 1) == 2, "default entry");
    }
    // 3. The shipped file: readable, every key a weapon of items.json (or unarmed / default), and the default
    //    entry follows the owner's example.
    {
        const WeaponPlayTable l_oT = f_LoadWeaponPlay("data/weapon_play.json");
        CHECK(!l_oT.m_mapWeapons.empty(), "data/weapon_play.json loads");
        std::ifstream l_oF("data/items.json");
        const nlohmann::json l_jItems = nlohmann::json::parse(l_oF, nullptr, false);
        std::set<std::string> l_setWeapons = {"unarmed", "default"};
        if (l_jItems.is_object())
            for (auto l_oIt = l_jItems.begin(); l_oIt != l_jItems.end(); ++l_oIt)
                if (l_oIt.value().is_object() && l_oIt.value().value("type", std::string()) == "weapon") l_setWeapons.insert(l_oIt.key());
        for (const auto& [l_strId, l_oW] : l_oT.m_mapWeapons) {
            if (!l_setWeapons.count(l_strId)) std::printf("FAIL: weapon_play.json key \"%s\" is not a weapon in items.json\n", l_strId.c_str());
            if (!l_setWeapons.count(l_strId)) g_iFail++;
        }
        const WeaponPlay& l_oD = l_oT.f_Get("default");
        CHECK(f_MissBackPoints(l_oD, 5) == -1 && f_MissBackPoints(l_oD, 10) == 5 && f_MissBackPoints(l_oD, 15) == 4,
              "the default entry is the owner's example");
    }

    // 4. The battle particles by level (the owner 2026-10-24): the highest entry at or below the level; every name in the
    //    shipped file is an effect of media/fx/battle.particle.
    {
        const WeaponPlay l_oW = f_WeaponPlayFromJson("bow", nlohmann::json::parse(
            R"({"fx":[{"lv":5,"hit":"h3"},{"lv":1,"release":"r1","hit":"h1"},{"lv":3,"hit":"h2"}]})"));
        CHECK(f_WeaponFxEntry(l_oW, 1) && f_WeaponFxEntry(l_oW, 1)->m_strHit == "h1" && f_WeaponFxEntry(l_oW, 1)->m_strRelease == "r1", "Lv 1: tier 1");
        CHECK(f_WeaponFxEntry(l_oW, 4)->m_strHit == "h2" && f_WeaponFxEntry(l_oW, 4)->m_strRelease.empty(), "Lv 4: tier 2, no release");
        CHECK(f_WeaponFxEntry(l_oW, 99)->m_strHit == "h3", "above every entry: the last");
        CHECK(f_WeaponFxEntry(f_WeaponPlayFromJson("none", nlohmann::json::object()), 3) == nullptr, "no fx: none");

        std::ifstream l_oF("media/fx/battle.particle");
        const nlohmann::json l_jFxFile = nlohmann::json::parse(l_oF, nullptr, false);
        std::set<std::string> l_setEffects;
        if (l_jFxFile.is_object() && l_jFxFile.contains("effects"))
            for (const auto& l_jE : l_jFxFile["effects"]) l_setEffects.insert(l_jE.value("name", std::string()));
        CHECK(!l_setEffects.empty(), "media/fx/battle.particle loads");
        const WeaponPlayTable l_oT = f_LoadWeaponPlay("data/weapon_play.json");
        CHECK(!l_oT.f_Get("default").m_vFx.empty(), "the default entry has fx (weapons without their own use it)");
        for (const auto& [l_strId, l_oWp] : l_oT.m_mapWeapons)
            for (const WeaponFx& l_oFx : l_oWp.m_vFx)
                for (const std::string& l_strName : {l_oFx.m_strRelease, l_oFx.m_strHit})
                    if (!l_strName.empty() && !l_setEffects.count(l_strName)) {
                        std::printf("FAIL: weapon_play.json %s fx \"%s\" is not in battle.particle\n", l_strId.c_str(), l_strName.c_str());
                        g_iFail++;
                    }
    }

    {   // the result rows by points matched (docs/gameplay/13 §4.2)
        const WeaponPlay l_oW = f_WeaponPlayFromJson("bow", nlohmann::json::parse(R"({ "results": [
            { "hits": "all", "name": "perfect", "damage": 1.5, "stunMs": 800 },
            { "hits": 2, "name": "great", "damage": 1.2 },
            { "hits": 0, "name": "miss" } ], "pace": "stamina" })"));
        const WeaponResult* l_pR = f_WeaponResultFor(l_oW, 3, 3, false);
        CHECK(l_pR && l_pR->m_strName == "perfect" && l_pR->m_iStunMs == 800, "all points: perfect");
        l_pR = f_WeaponResultFor(l_oW, 2, 3, false);
        CHECK(l_pR && l_pR->m_strName == "great", "2 of 3: great");
        l_pR = f_WeaponResultFor(l_oW, 3, 5, false);
        CHECK(l_pR && l_pR->m_strName == "great", "3 of 5: great (perfect needs all 5)");
        l_pR = f_WeaponResultFor(l_oW, 3, 3, true);
        CHECK(l_pR && l_pR->m_strName == "miss", "a failed combo: the 0 row");
        CHECK(f_WeaponResultFor(WeaponPlay{}, 3, 3, false) == nullptr, "no rows: none");
        CHECK(l_oW.m_strPace == "stamina", "a weapon's pace");
    }

    {   // the long bow (docs/gameplay/26): its aim line's growth by level; the bow keeps its classic pull
        const WeaponPlayTable l_oT = f_LoadWeaponPlay("data/weapon_play.json");
        const WeaponPlay& l_oL = l_oT.f_Get("long_bow");
        CHECK(l_oL.m_strId == "long_bow" && std::fabs(l_oL.f_AimGrowAt(1) - 0.30f) < 1e-4f && std::fabs(l_oL.f_AimGrowAt(2) - 0.35f) < 1e-4f &&
                  std::fabs(l_oL.f_AimGrowAt(10) - 0.75f) < 1e-4f, "the long bow grows 30 % a second at Lv 1, +5 % a level, 75 % at Lv 10");
        CHECK(l_oT.f_Get("bow").f_AimGrowAt(5) == 0.0f, "the bow has no growing line");
    }
    {   // data/weapon_trial.json (the owner, 2026-11-27): each weapon the trial offers exists, with its choice, its line and its how-to
        std::ifstream l_oF("data/weapon_trial.json"), l_oI("data/items.json");
        const nlohmann::json l_jT = nlohmann::json::parse(l_oF, nullptr, false), l_jItems = nlohmann::json::parse(l_oI, nullptr, false);
        const WeaponPlayTable l_oWp = f_LoadWeaponPlay("data/weapon_play.json");
        const nlohmann::json l_jList = l_jT.is_object() ? l_jT.value("weapons", nlohmann::json::array()) : nlohmann::json::array();
        CHECK(l_jList.size() >= 5, "the trial offers the bow, sword, staff, memory blade and long bow");
        for (const auto& l_jW : l_jList) {
            const std::string l_strId = l_jW.value("id", std::string());
            const nlohmann::json& l_jIt = l_jItems.contains("items") ? l_jItems["items"] : l_jItems;
            bool l_bOk = !l_strId.empty() && l_jIt.contains(l_strId) && l_oWp.m_mapWeapons.count(l_strId);
            for (const char* l_pszK : {"label", "about", "howTo"})
                for (const char* l_pszL : {"zh_TW", "en"})
                    l_bOk = l_bOk && l_jW.contains(l_pszK) && l_jW[l_pszK].contains(l_pszL) && !l_jW[l_pszK][l_pszL].get<std::string>().empty();
            if (!l_bOk) { std::printf("FAIL: weapon_trial.json \"%s\": not an item / weapon, or a label, about or howTo missing (zh_TW, en)\n", l_strId.c_str()); g_iFail++; }
        }
    }
    {   // the memory blade (the owner, 2026-11-27): all its commands at once (a chain), at most 5, shapes by level worth more
        const WeaponPlayTable l_oT = f_LoadWeaponPlay("data/weapon_play.json");
        const WeaponPlay& l_oM = l_oT.f_Get("memory_blade");
        CHECK(toms::f_PlayTypeFromString(l_oM.m_strPlay) == toms::PlayType::eSWIPE_CHAIN, "the memory blade plays a chain");
        bool l_bFive = true;
        for (int l_iLv = 1; l_iLv <= 10; l_iLv++) l_bFive = l_bFive && f_ComboSettings(l_oM, l_iLv).m_iPoints <= 5;
        CHECK(l_bFive, "never more than 5 commands");
        CHECK(l_oM.f_SwipeCuesAt(1).empty() && l_oM.f_SwipeCuesAt(8).size() == 3, "Lv 1: ways only; Lv 8: three shapes");
        CHECK(l_oM.f_CueDamage("square") > l_oM.f_CueDamage("circle") && l_oM.f_CueDamage("circle") > l_oM.f_CueDamage("way") &&
                  l_oM.f_CueDamage("way") == 1.0f, "a square is worth more than a circle, a circle more than a way");
    }
    if (g_iFail) { std::printf("weapon_play_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("weapon_play_test: all passed\n");
    return 0;
}
