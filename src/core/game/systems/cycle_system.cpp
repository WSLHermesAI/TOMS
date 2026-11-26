#include "field_keys.h"    // toms::field: every data field by its constant
#include "cycle_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

namespace toms {

CyclesConfig f_LoadCyclesConfig(const std::string& l_strPath) {
    CyclesConfig l_oC;   // documented defaults if the file is missing/malformed
    nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
    if (!l_jJ.is_object()) return l_oC;
    l_oC.m_iMaxCycles = l_jJ.value(toms::field::g_pszMaxCycles, l_oC.m_iMaxCycles);
    if (l_jJ.contains(toms::field::g_pszCarry) && l_jJ[toms::field::g_pszCarry].is_object()) {
        const auto& l_jCarry = l_jJ[toms::field::g_pszCarry];
        if (l_jCarry.contains(toms::field::g_pszSkills) && l_jCarry[toms::field::g_pszSkills].is_object())
            l_oC.m_fSkillEffectScale = l_jCarry[toms::field::g_pszSkills].value(toms::field::g_pszEffectScale, l_oC.m_fSkillEffectScale);
        if (l_jCarry.contains(toms::field::g_pszAtkDefBonus) && l_jCarry[toms::field::g_pszAtkDefBonus].is_object())
            l_oC.m_fStatBonusScale = l_jCarry[toms::field::g_pszAtkDefBonus].value(toms::field::g_pszValue, l_oC.m_fStatBonusScale);
    }
    return l_oC;
}

} // namespace toms
