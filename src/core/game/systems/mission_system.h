// mission_system.h — side/daily/one-time missions: static definitions, per-save progress
// tracking, and the daily-reset scheduler. See
// docs/architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §8 and docs/architecture/IMPLEMENTATION_ROADMAP.md
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
const char* f_ToString(MissionKind l_eK);
MissionKind f_MissionKindFromString(const std::string& l_strS);   // unrecognized -> Side (fail-safe default)

enum class MissionState { Locked, Available, Active, Completed, Claimed };
const char* f_ToString(MissionState l_eS);
MissionState f_MissionStateFromString(const std::string& l_strS); // unrecognized -> Locked (fail-safe default)

// "defeat" (targetId = enemyId) or "collect" (targetId = itemId), per architecture-doc §8.1.
struct MissionObjective {
    std::string m_strType;
    std::string m_strTargetId;
    int m_iCount = 1;
};

// Static content (would be loaded from data/missions.json once Milestone 8 authors it).
struct MissionDefinition {
    std::string m_strMissionId;
    MissionKind m_eKind = MissionKind::Side;
    std::string m_strGiver;              // NPC id or stageId that surfaces it
    nlohmann::json m_jPrerequisites;   // a Condition Evaluator expression (see condition.h) — null = none
    MissionObjective m_oObjective;
    int m_iRewardExp = 0;
    int m_iRewardGold = 0;
    std::string m_strRewardItemId;       // optional, empty = no item reward
};

// Per-save runtime progress for one mission.
struct MissionTracker {
    std::string m_strMissionId;
    MissionState m_eState = MissionState::Locked;
    int m_iProgress = 0;
    std::string m_strLastResetDate;      // "YYYY-MM-DD"; empty = never reset yet (daily missions only)
};

nlohmann::json f_ToJson(const MissionDefinition& l_oD);
MissionDefinition f_MissionDefinitionFromJson(const nlohmann::json& l_jJ);

nlohmann::json f_ToJson(const MissionTracker& l_oT);
MissionTracker f_MissionTrackerFromJson(const nlohmann::json& l_jJ);

// Daily-reset rollover (architecture-doc §8.3): if `def.kind == Daily` and `today` ("YYYY-MM-DD")
// differs from `tracker.lastResetDate`, resets progress to 0, sets state to Available, and
// stamps lastResetDate = today. No-op for Side/Once missions (they never reset) and a no-op if
// `today` matches the already-recorded lastResetDate (already rolled for today).
void f_RollDailyReset(MissionTracker& l_oTracker, const MissionDefinition& l_oDef, const std::string& l_strToday);

// Progress-event hook, meant to be called from an EventBus subscriber (see event_bus.h's
// EnemyDefeated/ItemCollected). Increments tracker.progress (capped at objective.count) and
// flips state to Completed on reaching it, but ONLY when tracker.state == Active and
// (eventType, targetId) matches def.objective — a Locked/Available/Completed/Claimed mission
// never silently gains progress, and a non-matching event is always a no-op.
void f_ApplyProgressEvent(MissionTracker& l_oTracker, const MissionDefinition& l_oDef,
                         const std::string& l_strEventType, const std::string& l_strTargetId);

} // namespace toms
