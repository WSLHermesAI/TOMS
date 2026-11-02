// mission_test.cpp — headless verification of the Mission System (Milestone 4).
// Exits 0 on success, 1 on any CHECK failure. Run: ./mission_test
#include "mission_system.h"
#include <cstdio>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

int main() {
    // 1. MissionKind / MissionState toString+fromString round-trip, and fail-safe defaults.
    for (auto l_eK : {MissionKind::Side, MissionKind::Daily, MissionKind::Once})
        CHECK(f_MissionKindFromString(f_ToString(l_eK)) == l_eK, "MissionKind round-trips toString(k)");
    CHECK(f_MissionKindFromString("bogus") == MissionKind::Side, "unrecognized MissionKind defaults to Side");

    for (auto l_eS : {MissionState::Locked, MissionState::Available, MissionState::Active,
                   MissionState::Completed, MissionState::Claimed})
        CHECK(f_MissionStateFromString(f_ToString(l_eS)) == l_eS, "MissionState round-trips toString(s)");
    CHECK(f_MissionStateFromString("bogus") == MissionState::Locked, "unrecognized MissionState defaults to Locked");

    // 2. MissionDefinition JSON round-trip.
    {
        MissionDefinition l_oD;
        l_oD.m_strMissionId = "m_scholar_favor";
        l_oD.m_eKind = MissionKind::Side;
        l_oD.m_strGiver = "skeleton_scholar";
        l_oD.m_jPrerequisites = nlohmann::json{{"type", "storyBeatAtLeast"}, {"value", 4}};
        l_oD.m_oObjective = { "defeat", "golem_stone", 1 };
        l_oD.m_iRewardExp = 50; l_oD.m_iRewardGold = 20; l_oD.m_strRewardItemId = "gem_atk";

        auto l_jJ = f_ToJson(l_oD);
        MissionDefinition l_oD2 = f_MissionDefinitionFromJson(l_jJ);
        CHECK(l_oD2.m_strMissionId == "m_scholar_favor", "MissionDefinition round-trip: missionId");
        CHECK(l_oD2.m_eKind == MissionKind::Side, "MissionDefinition round-trip: kind");
        CHECK(l_oD2.m_strGiver == "skeleton_scholar", "MissionDefinition round-trip: giver");
        CHECK(l_oD2.m_jPrerequisites == l_oD.m_jPrerequisites, "MissionDefinition round-trip: prerequisites");
        CHECK(l_oD2.m_oObjective.m_strType == "defeat" && l_oD2.m_oObjective.m_strTargetId == "golem_stone" && l_oD2.m_oObjective.m_iCount == 1,
              "MissionDefinition round-trip: objective");
        CHECK(l_oD2.m_iRewardExp == 50 && l_oD2.m_iRewardGold == 20 && l_oD2.m_strRewardItemId == "gem_atk",
              "MissionDefinition round-trip: rewards");
    }

    // 3. MissionTracker JSON round-trip.
    {
        MissionTracker l_oT;
        l_oT.m_strMissionId = "m_daily_slimes"; l_oT.m_eState = MissionState::Active; l_oT.m_iProgress = 2; l_oT.m_strLastResetDate = "2026-09-07";
        auto l_jJ = f_ToJson(l_oT);
        MissionTracker l_oT2 = f_MissionTrackerFromJson(l_jJ);
        CHECK(l_oT2.m_strMissionId == "m_daily_slimes" && l_oT2.m_eState == MissionState::Active &&
              l_oT2.m_iProgress == 2 && l_oT2.m_strLastResetDate == "2026-09-07", "MissionTracker round-trip");
    }

    // 4. rollDailyReset: side/once missions are never touched.
    {
        MissionDefinition l_oSide; l_oSide.m_eKind = MissionKind::Side;
        MissionTracker l_oT; l_oT.m_eState = MissionState::Active; l_oT.m_iProgress = 3; l_oT.m_strLastResetDate = "2026-09-01";
        f_RollDailyReset(l_oT, l_oSide, "2026-09-07");
        CHECK(l_oT.m_eState == MissionState::Active && l_oT.m_iProgress == 3 && l_oT.m_strLastResetDate == "2026-09-01",
              "rollDailyReset is a no-op for a Side mission");

        MissionDefinition l_oOnce; l_oOnce.m_eKind = MissionKind::Once;
        MissionTracker l_oT2; l_oT2.m_eState = MissionState::Completed; l_oT2.m_iProgress = 1; l_oT2.m_strLastResetDate = "";
        f_RollDailyReset(l_oT2, l_oOnce, "2026-09-07");
        CHECK(l_oT2.m_eState == MissionState::Completed && l_oT2.m_iProgress == 1,
              "rollDailyReset is a no-op for a Once mission (never re-arms)");
    }

    // 5. rollDailyReset: a Daily mission rolls on a new day, is idempotent within the same day,
    //    and rolls again on the next new day.
    {
        MissionDefinition l_oDaily; l_oDaily.m_eKind = MissionKind::Daily;
        MissionTracker l_oT; l_oT.m_eState = MissionState::Locked; l_oT.m_iProgress = 0; l_oT.m_strLastResetDate = "";

        f_RollDailyReset(l_oT, l_oDaily, "2026-09-07");
        CHECK(l_oT.m_eState == MissionState::Available && l_oT.m_iProgress == 0 && l_oT.m_strLastResetDate == "2026-09-07",
              "first rollDailyReset (never reset before) rolls to Available");

        l_oT.m_eState = MissionState::Active; l_oT.m_iProgress = 2;   // simulate the player working on it today
        f_RollDailyReset(l_oT, l_oDaily, "2026-09-07");
        CHECK(l_oT.m_eState == MissionState::Active && l_oT.m_iProgress == 2,
              "rollDailyReset called again the same day is a no-op (doesn't clobber in-progress state)");

        f_RollDailyReset(l_oT, l_oDaily, "2026-09-08");
        CHECK(l_oT.m_eState == MissionState::Available && l_oT.m_iProgress == 0 && l_oT.m_strLastResetDate == "2026-09-08",
              "rollDailyReset on a new day resets progress and re-arms the mission");
    }

    // 6. applyProgressEvent: only active missions gain progress, and only on a matching event.
    {
        MissionDefinition l_oDef; l_oDef.m_oObjective = { "defeat", "golem_stone", 2 };

        MissionTracker l_oLocked; l_oLocked.m_eState = MissionState::Locked;
        f_ApplyProgressEvent(l_oLocked, l_oDef, "defeat", "golem_stone");
        CHECK(l_oLocked.m_iProgress == 0, "applyProgressEvent: a Locked mission never gains progress");

        MissionTracker l_oActive; l_oActive.m_eState = MissionState::Active; l_oActive.m_iProgress = 0;
        f_ApplyProgressEvent(l_oActive, l_oDef, "collect", "golem_stone");
        CHECK(l_oActive.m_iProgress == 0, "applyProgressEvent: wrong event type does not advance progress");
        f_ApplyProgressEvent(l_oActive, l_oDef, "defeat", "slime");
        CHECK(l_oActive.m_iProgress == 0, "applyProgressEvent: wrong targetId does not advance progress");
        f_ApplyProgressEvent(l_oActive, l_oDef, "defeat", "golem_stone");
        CHECK(l_oActive.m_iProgress == 1 && l_oActive.m_eState == MissionState::Active,
              "applyProgressEvent: matching event advances progress, not yet complete");
        f_ApplyProgressEvent(l_oActive, l_oDef, "defeat", "golem_stone");
        CHECK(l_oActive.m_iProgress == 2 && l_oActive.m_eState == MissionState::Completed,
              "applyProgressEvent: reaching objective.count flips state to Completed");
        f_ApplyProgressEvent(l_oActive, l_oDef, "defeat", "golem_stone");
        CHECK(l_oActive.m_iProgress == 2, "applyProgressEvent: progress never over-increments past objective.count");
    }

    if (g_iFail == 0) { printf("mission_test: ALL PASS (28 checks)\n"); return 0; }
    printf("mission_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}
