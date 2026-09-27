// mission_test.cpp — headless verification of the Mission System (Milestone 4).
// Exits 0 on success, 1 on any CHECK failure. Run: ./mission_test
#include "mission_system.h"
#include <cstdio>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

int main() {
    // 1. MissionKind / MissionState toString+fromString round-trip, and fail-safe defaults.
    for (auto k : {MissionKind::Side, MissionKind::Daily, MissionKind::Once})
        CHECK(missionKindFromString(toString(k)) == k, "MissionKind round-trips toString(k)");
    CHECK(missionKindFromString("bogus") == MissionKind::Side, "unrecognized MissionKind defaults to Side");

    for (auto s : {MissionState::Locked, MissionState::Available, MissionState::Active,
                   MissionState::Completed, MissionState::Claimed})
        CHECK(missionStateFromString(toString(s)) == s, "MissionState round-trips toString(s)");
    CHECK(missionStateFromString("bogus") == MissionState::Locked, "unrecognized MissionState defaults to Locked");

    // 2. MissionDefinition JSON round-trip.
    {
        MissionDefinition d;
        d.missionId = "m_scholar_favor";
        d.kind = MissionKind::Side;
        d.giver = "skeleton_scholar";
        d.prerequisites = nlohmann::json{{"type", "storyBeatAtLeast"}, {"value", 4}};
        d.objective = { "defeat", "golem_stone", 1 };
        d.rewardExp = 50; d.rewardGold = 20; d.rewardItemId = "gem_atk";

        auto j = toJson(d);
        MissionDefinition d2 = missionDefinitionFromJson(j);
        CHECK(d2.missionId == "m_scholar_favor", "MissionDefinition round-trip: missionId");
        CHECK(d2.kind == MissionKind::Side, "MissionDefinition round-trip: kind");
        CHECK(d2.giver == "skeleton_scholar", "MissionDefinition round-trip: giver");
        CHECK(d2.prerequisites == d.prerequisites, "MissionDefinition round-trip: prerequisites");
        CHECK(d2.objective.type == "defeat" && d2.objective.targetId == "golem_stone" && d2.objective.count == 1,
              "MissionDefinition round-trip: objective");
        CHECK(d2.rewardExp == 50 && d2.rewardGold == 20 && d2.rewardItemId == "gem_atk",
              "MissionDefinition round-trip: rewards");
    }

    // 3. MissionTracker JSON round-trip.
    {
        MissionTracker t;
        t.missionId = "m_daily_slimes"; t.state = MissionState::Active; t.progress = 2; t.lastResetDate = "2026-09-07";
        auto j = toJson(t);
        MissionTracker t2 = missionTrackerFromJson(j);
        CHECK(t2.missionId == "m_daily_slimes" && t2.state == MissionState::Active &&
              t2.progress == 2 && t2.lastResetDate == "2026-09-07", "MissionTracker round-trip");
    }

    // 4. rollDailyReset: side/once missions are never touched.
    {
        MissionDefinition side; side.kind = MissionKind::Side;
        MissionTracker t; t.state = MissionState::Active; t.progress = 3; t.lastResetDate = "2026-09-01";
        rollDailyReset(t, side, "2026-09-07");
        CHECK(t.state == MissionState::Active && t.progress == 3 && t.lastResetDate == "2026-09-01",
              "rollDailyReset is a no-op for a Side mission");

        MissionDefinition once; once.kind = MissionKind::Once;
        MissionTracker t2; t2.state = MissionState::Completed; t2.progress = 1; t2.lastResetDate = "";
        rollDailyReset(t2, once, "2026-09-07");
        CHECK(t2.state == MissionState::Completed && t2.progress == 1,
              "rollDailyReset is a no-op for a Once mission (never re-arms)");
    }

    // 5. rollDailyReset: a Daily mission rolls on a new day, is idempotent within the same day,
    //    and rolls again on the next new day.
    {
        MissionDefinition daily; daily.kind = MissionKind::Daily;
        MissionTracker t; t.state = MissionState::Locked; t.progress = 0; t.lastResetDate = "";

        rollDailyReset(t, daily, "2026-09-07");
        CHECK(t.state == MissionState::Available && t.progress == 0 && t.lastResetDate == "2026-09-07",
              "first rollDailyReset (never reset before) rolls to Available");

        t.state = MissionState::Active; t.progress = 2;   // simulate the player working on it today
        rollDailyReset(t, daily, "2026-09-07");
        CHECK(t.state == MissionState::Active && t.progress == 2,
              "rollDailyReset called again the same day is a no-op (doesn't clobber in-progress state)");

        rollDailyReset(t, daily, "2026-09-08");
        CHECK(t.state == MissionState::Available && t.progress == 0 && t.lastResetDate == "2026-09-08",
              "rollDailyReset on a new day resets progress and re-arms the mission");
    }

    // 6. applyProgressEvent: only active missions gain progress, and only on a matching event.
    {
        MissionDefinition def; def.objective = { "defeat", "golem_stone", 2 };

        MissionTracker locked; locked.state = MissionState::Locked;
        applyProgressEvent(locked, def, "defeat", "golem_stone");
        CHECK(locked.progress == 0, "applyProgressEvent: a Locked mission never gains progress");

        MissionTracker active; active.state = MissionState::Active; active.progress = 0;
        applyProgressEvent(active, def, "collect", "golem_stone");
        CHECK(active.progress == 0, "applyProgressEvent: wrong event type does not advance progress");
        applyProgressEvent(active, def, "defeat", "slime");
        CHECK(active.progress == 0, "applyProgressEvent: wrong targetId does not advance progress");
        applyProgressEvent(active, def, "defeat", "golem_stone");
        CHECK(active.progress == 1 && active.state == MissionState::Active,
              "applyProgressEvent: matching event advances progress, not yet complete");
        applyProgressEvent(active, def, "defeat", "golem_stone");
        CHECK(active.progress == 2 && active.state == MissionState::Completed,
              "applyProgressEvent: reaching objective.count flips state to Completed");
        applyProgressEvent(active, def, "defeat", "golem_stone");
        CHECK(active.progress == 2, "applyProgressEvent: progress never over-increments past objective.count");
    }

    if (g_fail == 0) { printf("mission_test: ALL PASS (28 checks)\n"); return 0; }
    printf("mission_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
