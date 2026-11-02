// hub_system_test.cpp — headless verification of the village hub's pure content/math (S6).
// Exits 0 on success, 1 on any CHECK failure. Run: ./hub_system_test
#include "hub_system.h"
#include <cstdio>
#include <set>

using namespace toms;

static int g_iChecks = 0, g_iFails = 0;
#define CHECK(cond, msg) do { ++g_iChecks; if (!(cond)) { ++g_iFails; printf("  FAIL: %s\n", msg); } } while (0)

int main() {
    // 1. HubLocationDefinition JSON round-trip.
    {
        HubLocationDefinition l_oD;
        l_oD.m_strId = "hub_test"; l_oD.m_strUnlockFlag = "hub.test";
        l_oD.m_jName = "Test Place"; l_oD.m_jDesc = "for testing";
        l_oD.m_strActionKind = "talk"; l_oD.m_strActionNpc = "someone";
        auto l_jJ = f_ToJson(l_oD);
        HubLocationDefinition l_oD2 = f_HubLocationFromJson(l_jJ);
        CHECK(l_oD2.m_strId == "hub_test" && l_oD2.m_strUnlockFlag == "hub.test", "round-trip: id/unlockFlag");
        CHECK(l_oD2.m_strActionKind == "talk" && l_oD2.m_strActionNpc == "someone", "round-trip: action");
    }
    CHECK(f_HubLocationFromJson(nlohmann::json(nullptr)).m_strId.empty(),
          "hubLocationFromJson(null) returns a default, not a throw");

    // 2. loadHubLocations against the REAL data/hub.json (run from the project root, matching
    //    every other data-file test in this suite).
    auto l_mapDefs = f_LoadHubLocations("data/hub.json");
    CHECK(l_mapDefs.size() == 1, "data/hub.json has its 1 authored location so far");
    CHECK(l_mapDefs.count("hub_village"), "hub_village exists");
    CHECK(l_mapDefs["hub_village"].m_strUnlockFlag == "hub.village", "hub_village gates on the hub.village run flag");
    CHECK(l_mapDefs["hub_village"].m_strActionKind == "talk" && l_mapDefs["hub_village"].m_strActionNpc == "villager_elder",
          "hub_village's action talks to villager_elder");
    CHECK(f_LoadHubLocations("data/does_not_exist.json").empty(), "a missing file loads to an empty map, not a crash");

    // 3. hubLocationUnlocked: gated purely by the caller-supplied flag predicate.
    std::set<std::string> l_setSetFlags = {"hub.village"};
    auto l_fnHasFlag = [&](const std::string& l_strF) { return l_setSetFlags.count(l_strF) != 0; };
    auto l_fnHasNone = [](const std::string&) { return false; };
    CHECK(f_HubLocationUnlocked(l_mapDefs, l_fnHasFlag, "hub_village"), "hub_village unlocked once hub.village is set");
    CHECK(!f_HubLocationUnlocked(l_mapDefs, l_fnHasNone, "hub_village"), "hub_village stays locked without hub.village");
    CHECK(!f_HubLocationUnlocked(l_mapDefs, l_fnHasFlag, "not_a_real_location"), "an unknown location id is never unlocked");

    if (g_iFails == 0) { printf("hub_system_test: ALL PASS (%d checks)\n", g_iChecks); return 0; }
    printf("hub_system_test: %d/%d CHECK(s) FAILED\n", g_iFails, g_iChecks);
    return 1;
}
