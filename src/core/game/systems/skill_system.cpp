#include "field_keys.h"    // toms::field: every data field by its constant
#include "skill_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

#include <algorithm>
#include <cmath>

namespace toms {

nlohmann::json f_ToJson(const SkillDefinition& l_oD) {
    nlohmann::json l_jJ;
    l_jJ[toms::field::g_pszId] = l_oD.m_strId; l_jJ[toms::field::g_pszLineage] = l_oD.m_strLineage; l_jJ[toms::field::g_pszTier] = l_oD.m_iTier; l_jJ[toms::field::g_pszCost] = l_oD.m_iCost;
    l_jJ[toms::field::g_pszRequires] = l_oD.m_vRequires;
    l_jJ[toms::field::g_pszName] = l_oD.m_jName; l_jJ[toms::field::g_pszDesc] = l_oD.m_jDesc;
    l_jJ[toms::field::g_pszEffect] = { {toms::field::g_pszAtk, l_oD.m_iStatAtk}, {toms::field::g_pszDef, l_oD.m_iStatDef} };
    return l_jJ;
}

SkillDefinition f_SkillFromJson(const nlohmann::json& l_jJ) {
    SkillDefinition l_oD;
    if (!l_jJ.is_object()) return l_oD;
    l_oD.m_strId = l_jJ.value(toms::field::g_pszId, std::string());
    l_oD.m_strLineage = l_jJ.value(toms::field::g_pszLineage, std::string());
    l_oD.m_iTier = l_jJ.value(toms::field::g_pszTier, 0);
    l_oD.m_iCost = l_jJ.value(toms::field::g_pszCost, 0);
    if (l_jJ.contains(toms::field::g_pszRequires) && l_jJ[toms::field::g_pszRequires].is_array())
        for (auto& l_jR : l_jJ[toms::field::g_pszRequires]) if (l_jR.is_string()) l_oD.m_vRequires.push_back(l_jR.get<std::string>());
    l_oD.m_jName = l_jJ.contains(toms::field::g_pszName) ? l_jJ[toms::field::g_pszName] : nlohmann::json(l_oD.m_strId);
    l_oD.m_jDesc = l_jJ.contains(toms::field::g_pszDesc) ? l_jJ[toms::field::g_pszDesc] : nlohmann::json("");
    if (l_jJ.contains(toms::field::g_pszEffect) && l_jJ[toms::field::g_pszEffect].is_object()) {
        l_oD.m_iStatAtk = l_jJ[toms::field::g_pszEffect].value(toms::field::g_pszAtk, 0);
        l_oD.m_iStatDef = l_jJ[toms::field::g_pszEffect].value(toms::field::g_pszDef, 0);
    }
    return l_oD;
}

std::map<std::string, SkillDefinition> f_LoadSkillDefs(const std::string& l_strPath) {
    std::map<std::string, SkillDefinition> l_mapOut;
    nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
    if (!l_jJ.is_object()) return l_mapOut;
    for (auto& [l_strId, l_jV] : l_jJ.items()) {
        SkillDefinition l_oD = f_SkillFromJson(l_jV);
        if (l_oD.m_strId.empty()) l_oD.m_strId = l_strId;   // tolerate a file that relies on the key, not a repeated "id"
        l_mapOut[l_strId] = l_oD;
    }
    return l_mapOut;
}

bool f_CanUnlockSkill(const std::map<std::string, SkillDefinition>& l_mapDefs,
                     const std::vector<std::string>& l_vOwned, int l_iSkillPoints,
                     const std::string& l_strSkillId) {
    auto l_itIt = l_mapDefs.find(l_strSkillId);
    if (l_itIt == l_mapDefs.end()) return false;
    // A tier-0 node is a lineage's root -- it arrives ONLY as a chapter grant
    // (Game::applyChapterGrants calls RunStoryState::unlockSkill directly, bypassing this
    // predicate entirely, exactly so a grant is never subject to it). Its cost is 0 because a
    // grant is free, not because it's meant to be purchasable the moment any points exist --
    // confirmed as a real bug during S4's own verification: with this check absent, opening the
    // skill screen with 1 point let the player instantly buy ch_02/ch_03's not-yet-granted roots.
    if (l_itIt->second.m_iTier <= 0) return false;
    if (std::find(l_vOwned.begin(), l_vOwned.end(), l_strSkillId) != l_vOwned.end()) return false;   // already owned
    if (l_iSkillPoints < l_itIt->second.m_iCost) return false;
    for (auto& l_strReq : l_itIt->second.m_vRequires)
        if (std::find(l_vOwned.begin(), l_vOwned.end(), l_strReq) == l_vOwned.end()) return false;
    return true;
}

void f_ApplySkillEffects(const std::map<std::string, SkillDefinition>& l_mapDefs,
                        const std::vector<std::string>& l_vOwned, int& l_iAtk, int& l_iDef,
                        float l_fRebirthScale) {
    for (auto& l_strId : l_vOwned) {
        auto l_itIt = l_mapDefs.find(l_strId);
        if (l_itIt == l_mapDefs.end()) continue;
        float l_fScale = (l_itIt->second.m_iTier <= 0) ? 1.0f : l_fRebirthScale;
        l_iAtk += (int)std::floor(l_itIt->second.m_iStatAtk * l_fScale);
        l_iDef += (int)std::floor(l_itIt->second.m_iStatDef * l_fScale);
    }
}

} // namespace toms
