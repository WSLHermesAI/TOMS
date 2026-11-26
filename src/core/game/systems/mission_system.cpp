#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_names.h"   // toms::state, toms::battle_event, ... (no hard-coded name)
#include "mission_system.h"

namespace toms {

const char* f_ToString(MissionKind l_eK) {
    switch (l_eK) {
        case MissionKind::Side:  return "side";
        case MissionKind::Daily: return toms::mission::g_pszDaily;
        case MissionKind::Once:  return toms::mission::g_pszOnce;
    }
    return "side";
}

MissionKind f_MissionKindFromString(const std::string& l_strS) {
    if (l_strS == toms::mission::g_pszDaily) return MissionKind::Daily;
    if (l_strS == toms::mission::g_pszOnce)  return MissionKind::Once;
    return MissionKind::Side;
}

const char* f_ToString(MissionState l_eS) {
    switch (l_eS) {
        case MissionState::Locked:    return toms::mission::g_pszLocked;
        case MissionState::Available: return toms::mission::g_pszAvailable;
        case MissionState::Active:    return toms::mission::g_pszActive;
        case MissionState::Completed: return toms::mission::g_pszCompleted;
        case MissionState::Claimed:   return toms::mission::g_pszClaimed;
    }
    return toms::mission::g_pszLocked;
}

MissionState f_MissionStateFromString(const std::string& l_strS) {
    if (l_strS == toms::mission::g_pszAvailable) return MissionState::Available;
    if (l_strS == toms::mission::g_pszActive)    return MissionState::Active;
    if (l_strS == toms::mission::g_pszCompleted) return MissionState::Completed;
    if (l_strS == toms::mission::g_pszClaimed)   return MissionState::Claimed;
    return MissionState::Locked;
}

nlohmann::json f_ToJson(const MissionDefinition& l_oD) {
    nlohmann::json l_jJ;
    l_jJ[toms::field::g_pszMissionId] = l_oD.m_strMissionId;
    l_jJ[toms::field::g_pszKind] = f_ToString(l_oD.m_eKind);
    l_jJ[toms::field::g_pszGiver] = l_oD.m_strGiver;
    l_jJ[toms::field::g_pszPrerequisites] = l_oD.m_jPrerequisites;
    l_jJ[toms::field::g_pszObjective] = { {toms::field::g_pszType, l_oD.m_oObjective.m_strType}, {toms::field::g_pszTargetId, l_oD.m_oObjective.m_strTargetId}, {toms::field::g_pszCount, l_oD.m_oObjective.m_iCount} };
    l_jJ[toms::field::g_pszRewardExp] = l_oD.m_iRewardExp;
    l_jJ[toms::field::g_pszRewardGold] = l_oD.m_iRewardGold;
    l_jJ[toms::field::g_pszRewardItemId] = l_oD.m_strRewardItemId;
    return l_jJ;
}

MissionDefinition f_MissionDefinitionFromJson(const nlohmann::json& l_jJ) {
    MissionDefinition l_oD;
    if (!l_jJ.is_object()) return l_oD;
    l_oD.m_strMissionId = l_jJ.value(toms::field::g_pszMissionId, std::string());
    l_oD.m_eKind = f_MissionKindFromString(l_jJ.value(toms::field::g_pszKind, std::string("side")));
    l_oD.m_strGiver = l_jJ.value(toms::field::g_pszGiver, std::string());
    l_oD.m_jPrerequisites = l_jJ.contains(toms::field::g_pszPrerequisites) ? l_jJ[toms::field::g_pszPrerequisites] : nlohmann::json();
    if (l_jJ.contains(toms::field::g_pszObjective) && l_jJ[toms::field::g_pszObjective].is_object()) {
        auto& l_jO = l_jJ[toms::field::g_pszObjective];
        l_oD.m_oObjective.m_strType = l_jO.value(toms::field::g_pszType, std::string());
        l_oD.m_oObjective.m_strTargetId = l_jO.value(toms::field::g_pszTargetId, std::string());
        l_oD.m_oObjective.m_iCount = l_jO.value(toms::field::g_pszCount, 1);
    }
    l_oD.m_iRewardExp = l_jJ.value(toms::field::g_pszRewardExp, 0);
    l_oD.m_iRewardGold = l_jJ.value(toms::field::g_pszRewardGold, 0);
    l_oD.m_strRewardItemId = l_jJ.value(toms::field::g_pszRewardItemId, std::string());
    return l_oD;
}

nlohmann::json f_ToJson(const MissionTracker& l_oT) {
    nlohmann::json l_jJ;
    l_jJ[toms::field::g_pszMissionId] = l_oT.m_strMissionId;
    l_jJ[toms::field::g_pszState] = f_ToString(l_oT.m_eState);
    l_jJ[toms::field::g_pszProgress] = l_oT.m_iProgress;
    l_jJ[toms::field::g_pszLastResetDate] = l_oT.m_strLastResetDate;
    return l_jJ;
}

MissionTracker f_MissionTrackerFromJson(const nlohmann::json& l_jJ) {
    MissionTracker l_oT;
    if (!l_jJ.is_object()) return l_oT;
    l_oT.m_strMissionId = l_jJ.value(toms::field::g_pszMissionId, std::string());
    l_oT.m_eState = f_MissionStateFromString(l_jJ.value(toms::field::g_pszState, std::string(toms::mission::g_pszLocked)));
    l_oT.m_iProgress = l_jJ.value(toms::field::g_pszProgress, 0);
    l_oT.m_strLastResetDate = l_jJ.value(toms::field::g_pszLastResetDate, std::string());
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
