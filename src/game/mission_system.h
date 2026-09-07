// mission_system.h — side/daily/one-time missions: static definitions, per-save progress
// tracking, and the daily-reset scheduler. See
// docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §8 and docs/IMPLEMENTATION_ROADMAP.md
// Milestone 4.
//
// Deliberately content-free this milestone: no data/missions.json exists yet (that's Milestone
// 8's job, per the roadmap's own "systems first, content later" split). This file is the
// schema + pure logic, fully exercisable and tested with synthetic definitions.
#pragma once
#include <json.hpp>
#include <string>

namespace toms {

enum class MissionKind { Side, Daily, Once };
const char* toString(MissionKind k);
MissionKind missionKindFromString(const std::string& s);   // unrecognized -> Side (fail-safe default)

enum class MissionState { Locked, Available, Active, Completed, Claimed };
const char* toString(MissionState s);
MissionState missionStateFromString(const std::string& s); // unrecognized -> Locked (fail-safe default)

// "defeat" (targetId = enemyId) or "collect" (targetId = itemId), per architecture-doc §8.1.
struct MissionObjective {
    std::string type;
    std::string targetId;
    int count = 1;
};

// Static content (would be loaded from data/missions.json once Milestone 8 authors it).
struct MissionDefinition {
    std::string missionId;
    MissionKind kind = MissionKind::Side;
    std::string giver;              // NPC id or stageId that surfaces it
    nlohmann::json prerequisites;   // a Condition Evaluator expression (see condition.h) — null = none
    MissionObjective objective;
    int rewardExp = 0;
    int rewardGold = 0;
    std::string rewardItemId;       // optional, empty = no item reward
};

// Per-save runtime progress for one mission.
struct MissionTracker {
    std::string missionId;
    MissionState state = MissionState::Locked;
    int progress = 0;
    std::string lastResetDate;      // "YYYY-MM-DD"; empty = never reset yet (daily missions only)
};

nlohmann::json toJson(const MissionDefinition& d);
MissionDefinition missionDefinitionFromJson(const nlohmann::json& j);

nlohmann::json toJson(const MissionTracker& t);
MissionTracker missionTrackerFromJson(const nlohmann::json& j);

// Daily-reset rollover (architecture-doc §8.3): if `def.kind == Daily` and `today` ("YYYY-MM-DD")
// differs from `tracker.lastResetDate`, resets progress to 0, sets state to Available, and
// stamps lastResetDate = today. No-op for Side/Once missions (they never reset) and a no-op if
// `today` matches the already-recorded lastResetDate (already rolled for today).
void rollDailyReset(MissionTracker& tracker, const MissionDefinition& def, const std::string& today);

// Progress-event hook, meant to be called from an EventBus subscriber (see event_bus.h's
// EnemyDefeated/ItemCollected). Increments tracker.progress (capped at objective.count) and
// flips state to Completed on reaching it, but ONLY when tracker.state == Active and
// (eventType, targetId) matches def.objective — a Locked/Available/Completed/Claimed mission
// never silently gains progress, and a non-matching event is always a no-op.
void applyProgressEvent(MissionTracker& tracker, const MissionDefinition& def,
                         const std::string& eventType, const std::string& targetId);

} // namespace toms
