#include "field_keys.h"    // toms::field: every data field by its constant
#include "forge_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

#include <algorithm>

namespace toms {

nlohmann::json f_ToJson(const ForgeRecipeDefinition& l_oD) {
    nlohmann::json l_jJ;
    l_jJ[toms::field::g_pszId] = l_oD.m_strId; l_jJ[toms::field::g_pszResultEquipmentId] = l_oD.m_strResultEquipmentId;
    l_jJ[toms::field::g_pszName] = l_oD.m_jName; l_jJ[toms::field::g_pszDesc] = l_oD.m_jDesc;
    l_jJ[toms::field::g_pszCost] = { {toms::field::g_pszGold, l_oD.m_iCostGold}, {toms::field::g_pszMaterials, l_oD.m_mapMaterials} };
    return l_jJ;
}

ForgeRecipeDefinition f_ForgeRecipeFromJson(const nlohmann::json& l_jJ) {
    ForgeRecipeDefinition l_oD;
    if (!l_jJ.is_object()) return l_oD;
    l_oD.m_strId = l_jJ.value(toms::field::g_pszId, std::string());
    l_oD.m_strResultEquipmentId = l_jJ.value(toms::field::g_pszResultEquipmentId, std::string());
    l_oD.m_jName = l_jJ.contains(toms::field::g_pszName) ? l_jJ[toms::field::g_pszName] : nlohmann::json(l_oD.m_strId);
    l_oD.m_jDesc = l_jJ.contains(toms::field::g_pszDesc) ? l_jJ[toms::field::g_pszDesc] : nlohmann::json("");
    if (l_jJ.contains(toms::field::g_pszCost) && l_jJ[toms::field::g_pszCost].is_object()) {
        const auto& l_jC = l_jJ[toms::field::g_pszCost];
        l_oD.m_iCostGold = l_jC.value(toms::field::g_pszGold, 0);
        if (l_jC.contains(toms::field::g_pszMaterials) && l_jC[toms::field::g_pszMaterials].is_object())
            for (auto& [l_strItemId, l_jCount] : l_jC[toms::field::g_pszMaterials].items())
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
