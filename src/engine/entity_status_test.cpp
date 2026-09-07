// entity_status_test.cpp — headless verification of the Entity Status System (Milestone 3).
// Exits 0 on success, 1 on any CHECK failure. Run: ./entity_status_test
#include "entity_status.h"
#include <cstdio>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

int main() {
    std::map<std::string, std::string> statusMap;

    // 1. an absent key defaults to Untouched.
    std::string k1 = entityStatusKey("stage_01", 5, 3);
    CHECK(getEntityStatus(statusMap, k1) == EntityStatus::Untouched, "absent key defaults to Untouched");

    // 2. key format matches the documented "<stageId>|<x>,<y>" convention.
    CHECK(k1 == "stage_01|5,3", "entityStatusKey format is '<stageId>|<x>,<y>'");

    // 3. set/get round-trips for every status value.
    EntityStatus all[] = { EntityStatus::Untouched, EntityStatus::Engaged, EntityStatus::Defeated,
                            EntityStatus::Collected, EntityStatus::Opened, EntityStatus::Hidden };
    for (auto s : all) {
        setEntityStatus(statusMap, k1, s);
        CHECK(getEntityStatus(statusMap, k1) == s, "set/get round-trips for this status");
    }

    // 4. toString/fromString round-trips for every status value.
    for (auto s : all) {
        CHECK(fromString(toString(s)) == s, "toString/fromString round-trips");
    }

    // 5. fromString of garbage input fails safe to Untouched, not a crash/exception.
    CHECK(fromString("TotallyBogus") == EntityStatus::Untouched, "unrecognized string defaults to Untouched");

    // 6. two different tiles on the same stage track independently.
    std::string k2 = entityStatusKey("stage_01", 2, 2);
    setEntityStatus(statusMap, k1, EntityStatus::Defeated);
    setEntityStatus(statusMap, k2, EntityStatus::Collected);
    CHECK(getEntityStatus(statusMap, k1) == EntityStatus::Defeated, "tile 1 status independent of tile 2");
    CHECK(getEntityStatus(statusMap, k2) == EntityStatus::Collected, "tile 2 status independent of tile 1");

    // 7. the same (x,y) on two different stages produce distinct keys.
    std::string k3 = entityStatusKey("stage_02", 5, 3);
    CHECK(k1 != k3, "same (x,y) on different stages produce different keys");

    if (g_fail == 0) { printf("entity_status_test: ALL PASS (18 checks)\n"); return 0; }
    printf("entity_status_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
