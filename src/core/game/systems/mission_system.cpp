#include "mission_system.h"

namespace toms {

const char* f_ToString(MissionKind l_eK) {
    switch (l_eK) {
        case MissionKind::Side:  return "side";
        case MissionKind::Daily: return "daily";
        case MissionKind::Once:  return "once";
    }
    return "side";
}

MissionKind f_MissionKindFromString(const std::string& l_strS) {
    if (l_strS == "daily") return MissionKind::Daily;
    if (l_strS == "once")  return MissionKind::Once;
    return MissionKind::Side;
}

const char* f_ToString(MissionState l_eS) {
    switch (l_eS) {
        case MissionState::Locked:    return "locked";
        case MissionState::Available: return "available";
        case MissionState::Active:    return "active";
        case MissionState::Completed: return "completed";
        case MissionState::Claimed:   return "claimed";
    }
    return "locked";
}

MissionState f_MissionStateFromString(const std::string& l_strS) {
    if (l_strS == "available") return MissionState::Available;
    if (l_strS == "active")    return MissionState::Active;
    if (l_strS == "completed") return MissionState::Completed;
    if (l_strS == "claimed")   return MissionState::Claimed;
    return MissionState::Locked;
}

nlohmann::json f_ToJson(const MissionDefinition& l_oD) {
    nlohmann::json l_jJ;
    l_jJ["missionId"] = l_oD.m_strMissionId;
    l_jJ["kind"] = f_ToString(l_oD.m_eKind);
    l_jJ["giver"] = l_oD.m_strGiver;
    l_jJ["prerequisites"] = l_oD.m_jPrerequisites;
    l_jJ["objective"] = { {"type", l_oD.m_oObjective.m_strType}, {"targetId", l_oD.m_oObjective.m_strTargetId}, {"count", l_oD.m_oObjective.m_iCount} };
    l_jJ["rewardExp"] = l_oD.m_iRewardExp;
    l_jJ["rewardGold"] = l_oD.m_iRewardGold;
    l_jJ["rewardItemId"] = l_oD.m_strRewardItemId;
    return l_jJ;
}

MissionDefinition f_MissionDefinitionFromJson(const nlohmann::json& l_jJ) {
    MissionDefinition l_oD;
    if (!l_jJ.is_object()) return l_oD;
    l_oD.m_strMissionId = l_jJ.value("missionId", std::string());
    l_oD.m_eKind = f_MissionKindFromString(l_jJ.value("kind", std::string("side")));
    l_oD.m_strGiver = l_jJ.value("giver", std::string());
    l_oD.m_jPrerequisites = l_jJ.contains("prerequisites") ? l_jJ["prerequisites"] : nlohmann::json();
    if (l_jJ.contains("objective") && l_jJ["objective"].is_object()) {
        auto& l_jO = l_jJ["objective"];
        l_oD.m_oObjective.m_strType = l_jO.value("type", std::string());
        l_oD.m_oObjective.m_strTargetId = l_jO.value("targetId", std::string());
        l_oD.m_oObjective.m_iCount = l_jO.value("count", 1);
    }
    l_oD.m_iRewardExp = l_jJ.value("rewardExp", 0);
    l_oD.m_iRewardGold = l_jJ.value("rewardGold", 0);
    l_oD.m_strRewardItemId = l_jJ.value("rewardItemId", std::string());
    return l_oD;
}

nlohmann::json f_ToJson(const MissionTracker& l_oT) {
    nlohmann::json l_jJ;
    l_jJ["missionId"] = l_oT.m_strMissionId;
    l_jJ["state"] = f_ToString(l_oT.m_eState);
    l_jJ["progress"] = l_oT.m_iProgress;
    l_jJ["lastResetDate"] = l_oT.m_strLastResetDate;
    return l_jJ;
}

MissionTracker f_MissionTrackerFromJson(const nlohmann::json& l_jJ) {
    MissionTracker l_oT;
    if (!l_jJ.is_object()) return l_oT;
    l_oT.m_strMissionId = l_jJ.value("missionId", std::string());
    l_oT.m_eState = f_MissionStateFromString(l_jJ.value("state", std::string("locked")));
    l_oT.m_iProgress = l_jJ.value("progress", 0);
    l_oT.m_strLastResetDate = l_jJ.value("lastResetDate", std::string());
    return l_oT;
}

void f_RollDailyReset(MissionTracker& l_oTracker, const MissionDefinition& l_oDef, const std::string& l_strToday) {
    if (l_oDef.m_eKind != MissionKind::Daily) return;           // side/once missions never reset
    if (l_oTracker.m_strLastResetDate == l_strToday) return;             // already rolled for today
    l_oTracker.m_iProgress = 0;
    l_oTracker.m_eState = MissionState::Available;
    l_oTracker.m_strLastResetDate = l_strToday;
}

void f_ApplyProgressEvent(MissionTracker& l_oTracker, const MissionDefinition& l_oDef,
                         const std::string& l_strEventType, const std::string& l_strTargetId) {
    if (l_oTracker.m_eState != MissionState::Active) return;      // only active missions gain progress
    if (l_oDef.m_oObjective.m_strType != l_strEventType) return;
    if (l_oDef.m_oObjective.m_strTargetId != l_strTargetId) return;
    if (l_oTracker.m_iProgress >= l_oDef.m_oObjective.m_iCount) return;    // already capped
    l_oTracker.m_iProgress++;
    if (l_oTracker.m_iProgress >= l_oDef.m_oObjective.m_iCount) l_oTracker.m_eState = MissionState::Completed;
}

} // namespace toms
