#include "mission_system.h"

namespace toms {

const char* toString(MissionKind k) {
    switch (k) {
        case MissionKind::Side:  return "side";
        case MissionKind::Daily: return "daily";
        case MissionKind::Once:  return "once";
    }
    return "side";
}

MissionKind missionKindFromString(const std::string& s) {
    if (s == "daily") return MissionKind::Daily;
    if (s == "once")  return MissionKind::Once;
    return MissionKind::Side;
}

const char* toString(MissionState s) {
    switch (s) {
        case MissionState::Locked:    return "locked";
        case MissionState::Available: return "available";
        case MissionState::Active:    return "active";
        case MissionState::Completed: return "completed";
        case MissionState::Claimed:   return "claimed";
    }
    return "locked";
}

MissionState missionStateFromString(const std::string& s) {
    if (s == "available") return MissionState::Available;
    if (s == "active")    return MissionState::Active;
    if (s == "completed") return MissionState::Completed;
    if (s == "claimed")   return MissionState::Claimed;
    return MissionState::Locked;
}

nlohmann::json toJson(const MissionDefinition& d) {
    nlohmann::json j;
    j["missionId"] = d.missionId;
    j["kind"] = toString(d.kind);
    j["giver"] = d.giver;
    j["prerequisites"] = d.prerequisites;
    j["objective"] = { {"type", d.objective.type}, {"targetId", d.objective.targetId}, {"count", d.objective.count} };
    j["rewardExp"] = d.rewardExp;
    j["rewardGold"] = d.rewardGold;
    j["rewardItemId"] = d.rewardItemId;
    return j;
}

MissionDefinition missionDefinitionFromJson(const nlohmann::json& j) {
    MissionDefinition d;
    if (!j.is_object()) return d;
    d.missionId = j.value("missionId", std::string());
    d.kind = missionKindFromString(j.value("kind", std::string("side")));
    d.giver = j.value("giver", std::string());
    d.prerequisites = j.contains("prerequisites") ? j["prerequisites"] : nlohmann::json();
    if (j.contains("objective") && j["objective"].is_object()) {
        auto& o = j["objective"];
        d.objective.type = o.value("type", std::string());
        d.objective.targetId = o.value("targetId", std::string());
        d.objective.count = o.value("count", 1);
    }
    d.rewardExp = j.value("rewardExp", 0);
    d.rewardGold = j.value("rewardGold", 0);
    d.rewardItemId = j.value("rewardItemId", std::string());
    return d;
}

nlohmann::json toJson(const MissionTracker& t) {
    nlohmann::json j;
    j["missionId"] = t.missionId;
    j["state"] = toString(t.state);
    j["progress"] = t.progress;
    j["lastResetDate"] = t.lastResetDate;
    return j;
}

MissionTracker missionTrackerFromJson(const nlohmann::json& j) {
    MissionTracker t;
    if (!j.is_object()) return t;
    t.missionId = j.value("missionId", std::string());
    t.state = missionStateFromString(j.value("state", std::string("locked")));
    t.progress = j.value("progress", 0);
    t.lastResetDate = j.value("lastResetDate", std::string());
    return t;
}

void rollDailyReset(MissionTracker& tracker, const MissionDefinition& def, const std::string& today) {
    if (def.kind != MissionKind::Daily) return;           // side/once missions never reset
    if (tracker.lastResetDate == today) return;             // already rolled for today
    tracker.progress = 0;
    tracker.state = MissionState::Available;
    tracker.lastResetDate = today;
}

void applyProgressEvent(MissionTracker& tracker, const MissionDefinition& def,
                         const std::string& eventType, const std::string& targetId) {
    if (tracker.state != MissionState::Active) return;      // only active missions gain progress
    if (def.objective.type != eventType) return;
    if (def.objective.targetId != targetId) return;
    if (tracker.progress >= def.objective.count) return;    // already capped
    tracker.progress++;
    if (tracker.progress >= def.objective.count) tracker.state = MissionState::Completed;
}

} // namespace toms
