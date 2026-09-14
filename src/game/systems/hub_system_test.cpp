// hub_system_test.cpp — headless verification of the village hub's pure content/math (S6).
// Exits 0 on success, 1 on any CHECK failure. Run: ./hub_system_test
#include "hub_system.h"
#include <cstdio>
#include <set>

using namespace toms;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_fails; printf("  FAIL: %s\n", msg); } } while (0)

int main() {
    // 1. HubLocationDefinition JSON round-trip.
    {
        HubLocationDefinition d;
        d.id = "hub_test"; d.unlockFlag = "hub.test";
        d.name = "Test Place"; d.desc = "for testing";
        d.actionKind = "talk"; d.actionNpc = "someone";
        auto j = toJson(d);
        HubLocationDefinition d2 = hubLocationFromJson(j);
        CHECK(d2.id == "hub_test" && d2.unlockFlag == "hub.test", "round-trip: id/unlockFlag");
        CHECK(d2.actionKind == "talk" && d2.actionNpc == "someone", "round-trip: action");
    }
    CHECK(hubLocationFromJson(nlohmann::json(nullptr)).id.empty(),
          "hubLocationFromJson(null) returns a default, not a throw");

    // 2. loadHubLocations against the REAL data/hub.json (run from the project root, matching
    //    every other data-file test in this suite).
    auto defs = loadHubLocations("data/hub.json");
    CHECK(defs.size() == 1, "data/hub.json has its 1 authored location so far");
    CHECK(defs.count("hub_village"), "hub_village exists");
    CHECK(defs["hub_village"].unlockFlag == "hub.village", "hub_village gates on the hub.village run flag");
    CHECK(defs["hub_village"].actionKind == "talk" && defs["hub_village"].actionNpc == "villager_elder",
          "hub_village's action talks to villager_elder");
    CHECK(loadHubLocations("data/does_not_exist.json").empty(), "a missing file loads to an empty map, not a crash");

    // 3. hubLocationUnlocked: gated purely by the caller-supplied flag predicate.
    std::set<std::string> setFlags = {"hub.village"};
    auto hasFlag = [&](const std::string& f) { return setFlags.count(f) != 0; };
    auto hasNone = [](const std::string&) { return false; };
    CHECK(hubLocationUnlocked(defs, hasFlag, "hub_village"), "hub_village unlocked once hub.village is set");
    CHECK(!hubLocationUnlocked(defs, hasNone, "hub_village"), "hub_village stays locked without hub.village");
    CHECK(!hubLocationUnlocked(defs, hasFlag, "not_a_real_location"), "an unknown location id is never unlocked");

    if (g_fails == 0) { printf("hub_system_test: ALL PASS (%d checks)\n", g_checks); return 0; }
    printf("hub_system_test: %d/%d CHECK(s) FAILED\n", g_fails, g_checks);
    return 1;
}
