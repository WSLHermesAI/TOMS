#include "field_keys.h"    // toms::field: every data field by its constant
#include "hub_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

namespace toms {

nlohmann::json f_ToJson(const HubLocationDefinition& l_oD) {
    nlohmann::json l_jJ;
    l_jJ[toms::field::g_pszId] = l_oD.m_strId; l_jJ[toms::field::g_pszUnlockFlag] = l_oD.m_strUnlockFlag;
    l_jJ[toms::field::g_pszName] = l_oD.m_jName; l_jJ[toms::field::g_pszDesc] = l_oD.m_jDesc;
    l_jJ[toms::field::g_pszAction] = { {toms::field::g_pszKind, l_oD.m_strActionKind}, {toms::field::g_pszNpc, l_oD.m_strActionNpc} };
    return l_jJ;
}

HubLocationDefinition f_HubLocationFromJson(const nlohmann::json& l_jJ) {
    HubLocationDefinition l_oD;
    if (!l_jJ.is_object()) return l_oD;
    l_oD.m_strId = l_jJ.value(toms::field::g_pszId, std::string());
    l_oD.m_strUnlockFlag = l_jJ.value(toms::field::g_pszUnlockFlag, std::string());
    l_oD.m_jName = l_jJ.contains(toms::field::g_pszName) ? l_jJ[toms::field::g_pszName] : nlohmann::json(l_oD.m_strId);
    l_oD.m_jDesc = l_jJ.contains(toms::field::g_pszDesc) ? l_jJ[toms::field::g_pszDesc] : nlohmann::json("");
    if (l_jJ.contains(toms::field::g_pszAction) && l_jJ[toms::field::g_pszAction].is_object()) {
        const auto& l_jA = l_jJ[toms::field::g_pszAction];
        l_oD.m_strActionKind = l_jA.value(toms::field::g_pszKind, std::string());
        l_oD.m_strActionNpc = l_jA.value(toms::field::g_pszNpc, std::string());
    }
    return l_oD;
}

std::map<std::string, HubLocationDefinition> f_LoadHubLocations(const std::string& l_strPath) {
    std::map<std::string, HubLocationDefinition> l_mapOut;
    nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
    if (!l_jJ.is_object()) return l_mapOut;
    for (auto& [l_strId, l_jV] : l_jJ.items()) {
        HubLocationDefinition l_oD = f_HubLocationFromJson(l_jV);
        if (l_oD.m_strId.empty()) l_oD.m_strId = l_strId;   // tolerate a file that relies on the key, not a repeated "id"
        l_mapOut[l_strId] = l_oD;
    }
    return l_mapOut;
}

bool f_HubLocationUnlocked(const std::map<std::string, HubLocationDefinition>& l_mapDefs,
                          const std::function<bool(const std::string&)>& l_fnHasFlag,
                          const std::string& l_strLocationId) {
    auto l_itIt = l_mapDefs.find(l_strLocationId);
    if (l_itIt == l_mapDefs.end()) return false;
    if (l_itIt->second.m_strUnlockFlag.empty()) return true;   // no gate declared -> always reachable
    return l_fnHasFlag(l_itIt->second.m_strUnlockFlag);
}

} // namespace toms
