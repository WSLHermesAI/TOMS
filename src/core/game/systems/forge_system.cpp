#include "forge_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

#include <algorithm>

namespace toms {

nlohmann::json f_ToJson(const ForgeRecipeDefinition& l_oD) {
    nlohmann::json l_jJ;
    l_jJ["id"] = l_oD.m_strId; l_jJ["resultEquipmentId"] = l_oD.m_strResultEquipmentId;
    l_jJ["name"] = l_oD.m_jName; l_jJ["desc"] = l_oD.m_jDesc;
    l_jJ["cost"] = { {"gold", l_oD.m_iCostGold}, {"materials", l_oD.m_mapMaterials} };
    return l_jJ;
}

ForgeRecipeDefinition f_ForgeRecipeFromJson(const nlohmann::json& l_jJ) {
    ForgeRecipeDefinition l_oD;
    if (!l_jJ.is_object()) return l_oD;
    l_oD.m_strId = l_jJ.value("id", std::string());
    l_oD.m_strResultEquipmentId = l_jJ.value("resultEquipmentId", std::string());
    l_oD.m_jName = l_jJ.contains("name") ? l_jJ["name"] : nlohmann::json(l_oD.m_strId);
    l_oD.m_jDesc = l_jJ.contains("desc") ? l_jJ["desc"] : nlohmann::json("");
    if (l_jJ.contains("cost") && l_jJ["cost"].is_object()) {
        const auto& l_jC = l_jJ["cost"];
        l_oD.m_iCostGold = l_jC.value("gold", 0);
        if (l_jC.contains("materials") && l_jC["materials"].is_object())
            for (auto& [l_strItemId, l_jCount] : l_jC["materials"].items())
                if (l_jCount.is_number_integer()) l_oD.m_mapMaterials[l_strItemId] = l_jCount.get<int>();
    }
    return l_oD;
}

std::map<std::string, ForgeRecipeDefinition> f_LoadForgeRecipes(const std::string& l_strPath) {
    std::map<std::string, ForgeRecipeDefinition> l_mapOut;
    nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
    if (!l_jJ.is_object()) return l_mapOut;
    for (auto& [l_strId, l_jV] : l_jJ.items()) {
        ForgeRecipeDefinition l_oD = f_ForgeRecipeFromJson(l_jV);
        if (l_oD.m_strId.empty()) l_oD.m_strId = l_strId;   // tolerate a file that relies on the key, not a repeated "id"
        l_mapOut[l_strId] = l_oD;
    }
    return l_mapOut;
}

bool f_CanCraft(const std::map<std::string, ForgeRecipeDefinition>& l_mapDefs,
              const std::vector<std::string>& l_vKnown, const std::vector<std::string>& l_vInv,
              int l_iGold, const std::string& l_strRecipeId) {
    auto l_itIt = l_mapDefs.find(l_strRecipeId);
    if (l_itIt == l_mapDefs.end()) return false;
    if (std::find(l_vKnown.begin(), l_vKnown.end(), l_strRecipeId) == l_vKnown.end()) return false;   // not learned
    if (l_iGold < l_itIt->second.m_iCostGold) return false;
    for (auto& [l_strItemId, l_iNeed] : l_itIt->second.m_mapMaterials) {
        int l_iHave = (int)std::count(l_vInv.begin(), l_vInv.end(), l_strItemId);
        if (l_iHave < l_iNeed) return false;
    }
    return true;
}

} // namespace toms
