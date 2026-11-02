// entity_status_test.cpp — headless verification of the Entity Status System (Milestone 3).
// Exits 0 on success, 1 on any CHECK failure. Run: ./entity_status_test
#include "entity_status.h"
#include <cstdio>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

int main() {
    std::map<std::string, std::string> l_mapStatusMap;

    // 1. an absent key defaults to Untouched.
    std::string l_str1 = f_EntityStatusKey("stage_01", 5, 3);
    CHECK(f_GetEntityStatus(l_mapStatusMap, l_str1) == EntityStatus::Untouched, "absent key defaults to Untouched");

    // 2. key format matches the documented "<stageId>|<x>,<y>" convention.
    CHECK(l_str1 == "stage_01|5,3", "entityStatusKey format is '<stageId>|<x>,<y>'");

    // 3. set/get round-trips for every status value.
    EntityStatus l_arrAll[] = { EntityStatus::Untouched, EntityStatus::Engaged, EntityStatus::Defeated,
                            EntityStatus::Collected, EntityStatus::Opened, EntityStatus::Hidden };
    for (auto l_eS : l_arrAll) {
        f_SetEntityStatus(l_mapStatusMap, l_str1, l_eS);
        CHECK(f_GetEntityStatus(l_mapStatusMap, l_str1) == l_eS, "set/get round-trips for this status");
    }

    // 4. toString/fromString round-trips for every status value.
    for (auto l_eS : l_arrAll) {
        CHECK(f_FromString(f_ToString(l_eS)) == l_eS, "toString/fromString round-trips");
    }

    // 5. fromString of garbage input fails safe to Untouched, not a crash/exception.
    CHECK(f_FromString("TotallyBogus") == EntityStatus::Untouched, "unrecognized string defaults to Untouched");

    // 6. two different tiles on the same stage track independently.
    std::string l_str2 = f_EntityStatusKey("stage_01", 2, 2);
    f_SetEntityStatus(l_mapStatusMap, l_str1, EntityStatus::Defeated);
    f_SetEntityStatus(l_mapStatusMap, l_str2, EntityStatus::Collected);
    CHECK(f_GetEntityStatus(l_mapStatusMap, l_str1) == EntityStatus::Defeated, "tile 1 status independent of tile 2");
    CHECK(f_GetEntityStatus(l_mapStatusMap, l_str2) == EntityStatus::Collected, "tile 2 status independent of tile 1");

    // 7. the same (x,y) on two different stages produce distinct keys.
    std::string l_str3 = f_EntityStatusKey("stage_02", 5, 3);
    CHECK(l_str1 != l_str3, "same (x,y) on different stages produce different keys");

    if (g_iFail == 0) { printf("entity_status_test: ALL PASS (18 checks)\n"); return 0; }
    printf("entity_status_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}
