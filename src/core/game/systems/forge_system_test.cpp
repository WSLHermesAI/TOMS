// forge_system_test.cpp — headless verification of forging's pure content/math (S5).
// Exits 0 on success, 1 on any CHECK failure. Run: ./forge_system_test
#include "forge_system.h"
#include <cstdio>

using namespace toms;

static int g_iChecks = 0, g_iFails = 0;
#define CHECK(cond, msg) do { ++g_iChecks; if (!(cond)) { ++g_iFails; printf("  FAIL: %s\n", msg); } } while (0)

int main() {
    // 1. ForgeRecipeDefinition JSON round-trip.
    {
        ForgeRecipeDefinition l_oD;
        l_oD.m_strId = "fr_test"; l_oD.m_strResultEquipmentId = "war_hammer";
        l_oD.m_jName = "Test Blueprint"; l_oD.m_jDesc = "for testing";
        l_oD.m_iCostGold = 15; l_oD.m_mapMaterials = {{"gem_atk", 2}, {"gem_def", 1}};
        auto l_jJ = f_ToJson(l_oD);
        ForgeRecipeDefinition l_oD2 = f_ForgeRecipeFromJson(l_jJ);
        CHECK(l_oD2.m_strId == "fr_test" && l_oD2.m_strResultEquipmentId == "war_hammer", "round-trip: id/result");
        CHECK(l_oD2.m_iCostGold == 15, "round-trip: gold cost");
        CHECK(l_oD2.m_mapMaterials.size() == 2 && l_oD2.m_mapMaterials.at("gem_atk") == 2 && l_oD2.m_mapMaterials.at("gem_def") == 1,
              "round-trip: materials");
    }
    CHECK(f_ForgeRecipeFromJson(nlohmann::json(nullptr)).m_strId.empty(),
          "forgeRecipeFromJson(null) returns a default, not a throw");

    // 2. loadForgeRecipes against the REAL data/forge.json (run from the project root, matching
    //    every other data-file test in this suite).
    auto l_mapDefs = f_LoadForgeRecipes("data/forge.json");
    CHECK(l_mapDefs.size() == 3, "data/forge.json has its 3 authored recipes");
    CHECK(l_mapDefs.count("fr_gatewarden") && l_mapDefs.count("fr_twin_edge") && l_mapDefs.count("fr_hammer_of_dawn"),
          "the 3 named recipes exist");
    CHECK(l_mapDefs["fr_gatewarden"].m_strResultEquipmentId == "guardian_plate", "fr_gatewarden crafts guardian_plate");
    CHECK(l_mapDefs["fr_gatewarden"].m_mapMaterials.at("gem_def") == 2, "fr_gatewarden needs 2 gem_def");
    CHECK(f_LoadForgeRecipes("data/does_not_exist.json").empty(), "a missing file loads to an empty map, not a crash");

    // 3. canCraft: every gating rule, against the real loaded content.
    std::vector<std::string> l_vKnown = {"fr_gatewarden"};
    std::vector<std::string> l_vFullInv = {"gem_def", "gem_def", "potion_red"};
    CHECK(f_CanCraft(l_mapDefs, l_vKnown, l_vFullInv, 30, "fr_gatewarden"), "known + enough gold + enough materials -> craftable");
    CHECK(!f_CanCraft(l_mapDefs, {}, l_vFullInv, 999, "fr_gatewarden"), "an unlearned recipe is never craftable, however rich you are");
    CHECK(!f_CanCraft(l_mapDefs, l_vKnown, l_vFullInv, 29, "fr_gatewarden"), "one gold short blocks it");
    CHECK(!f_CanCraft(l_mapDefs, l_vKnown, {"gem_def"}, 30, "fr_gatewarden"), "one material short blocks it");
    CHECK(!f_CanCraft(l_mapDefs, l_vKnown, l_vFullInv, 999, "not_a_real_recipe"), "an unknown recipe id is never craftable");
    CHECK(f_CanCraft(l_mapDefs, l_vKnown, {"gem_def","gem_def","gem_def"}, 30, "fr_gatewarden"),
          "extra materials beyond what's required are still fine");

    if (g_iFails == 0) { printf("forge_system_test: ALL PASS (%d checks)\n", g_iChecks); return 0; }
    printf("forge_system_test: %d/%d CHECK(s) FAILED\n", g_iFails, g_iChecks);
    return 1;
}
