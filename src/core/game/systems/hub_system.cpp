#include "hub_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

namespace toms {

nlohmann::json f_ToJson(const HubLocationDefinition& l_oD) {
    nlohmann::json l_jJ;
    l_jJ["id"] = l_oD.m_strId; l_jJ["unlockFlag"] = l_oD.m_strUnlockFlag;
    l_jJ["name"] = l_oD.m_jName; l_jJ["desc"] = l_oD.m_jDesc;
    l_jJ["action"] = { {"kind", l_oD.m_strActionKind}, {"npc", l_oD.m_strActionNpc} };
    return l_jJ;
}

HubLocationDefinition f_HubLocationFromJson(const nlohmann::json& l_jJ) {
    HubLocationDefinition l_oD;
    if (!l_jJ.is_object()) return l_oD;
    l_oD.m_strId = l_jJ.value("id", std::string());
    l_oD.m_strUnlockFlag = l_jJ.value("unlockFlag", std::string());
    l_oD.m_jName = l_jJ.contains("name") ? l_jJ["name"] : nlohmann::json(l_oD.m_strId);
    l_oD.m_jDesc = l_jJ.contains("desc") ? l_jJ["desc"] : nlohmann::json("");
    if (l_jJ.contains("action") && l_jJ["action"].is_object()) {
        const auto& l_jA = l_jJ["action"];
        l_oD.m_strActionKind = l_jA.value("kind", std::string());
        l_oD.m_strActionNpc = l_jA.value("npc", std::string());
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
