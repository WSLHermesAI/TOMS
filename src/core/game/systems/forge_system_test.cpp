// forge_system_test.cpp — headless verification of forging's pure content/math (S5).
// Exits 0 on success, 1 on any CHECK failure. Run: ./forge_system_test
#include "forge_system.h"
#include <cstdio>

using namespace toms;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_fails; printf("  FAIL: %s\n", msg); } } while (0)

int main() {
    // 1. ForgeRecipeDefinition JSON round-trip.
    {
        ForgeRecipeDefinition d;
        d.id = "fr_test"; d.resultEquipmentId = "war_hammer";
        d.name = "Test Blueprint"; d.desc = "for testing";
        d.costGold = 15; d.materials = {{"gem_atk", 2}, {"gem_def", 1}};
        auto j = toJson(d);
        ForgeRecipeDefinition d2 = forgeRecipeFromJson(j);
        CHECK(d2.id == "fr_test" && d2.resultEquipmentId == "war_hammer", "round-trip: id/result");
        CHECK(d2.costGold == 15, "round-trip: gold cost");
        CHECK(d2.materials.size() == 2 && d2.materials.at("gem_atk") == 2 && d2.materials.at("gem_def") == 1,
              "round-trip: materials");
    }
    CHECK(forgeRecipeFromJson(nlohmann::json(nullptr)).id.empty(),
          "forgeRecipeFromJson(null) returns a default, not a throw");

    // 2. loadForgeRecipes against the REAL data/forge.json (run from the project root, matching
    //    every other data-file test in this suite).
    auto defs = loadForgeRecipes("data/forge.json");
    CHECK(defs.size() == 3, "data/forge.json has its 3 authored recipes");
    CHECK(defs.count("fr_gatewarden") && defs.count("fr_twin_edge") && defs.count("fr_hammer_of_dawn"),
          "the 3 named recipes exist");
    CHECK(defs["fr_gatewarden"].resultEquipmentId == "guardian_plate", "fr_gatewarden crafts guardian_plate");
    CHECK(defs["fr_gatewarden"].materials.at("gem_def") == 2, "fr_gatewarden needs 2 gem_def");
    CHECK(loadForgeRecipes("data/does_not_exist.json").empty(), "a missing file loads to an empty map, not a crash");

    // 3. canCraft: every gating rule, against the real loaded content.
    std::vector<std::string> known = {"fr_gatewarden"};
    std::vector<std::string> fullInv = {"gem_def", "gem_def", "potion_red"};
    CHECK(canCraft(defs, known, fullInv, 30, "fr_gatewarden"), "known + enough gold + enough materials -> craftable");
    CHECK(!canCraft(defs, {}, fullInv, 999, "fr_gatewarden"), "an unlearned recipe is never craftable, however rich you are");
    CHECK(!canCraft(defs, known, fullInv, 29, "fr_gatewarden"), "one gold short blocks it");
    CHECK(!canCraft(defs, known, {"gem_def"}, 30, "fr_gatewarden"), "one material short blocks it");
    CHECK(!canCraft(defs, known, fullInv, 999, "not_a_real_recipe"), "an unknown recipe id is never craftable");
    CHECK(canCraft(defs, known, {"gem_def","gem_def","gem_def"}, 30, "fr_gatewarden"),
          "extra materials beyond what's required are still fine");

    if (g_fails == 0) { printf("forge_system_test: ALL PASS (%d checks)\n", g_checks); return 0; }
    printf("forge_system_test: %d/%d CHECK(s) FAILED\n", g_fails, g_checks);
    return 1;
}
