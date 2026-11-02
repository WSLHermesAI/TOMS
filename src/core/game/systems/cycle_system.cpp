#include "cycle_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

namespace toms {

CyclesConfig f_LoadCyclesConfig(const std::string& l_strPath) {
    CyclesConfig l_oC;   // documented defaults if the file is missing/malformed
    nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
    if (!l_jJ.is_object()) return l_oC;
    l_oC.m_iMaxCycles = l_jJ.value("maxCycles", l_oC.m_iMaxCycles);
    if (l_jJ.contains("carry") && l_jJ["carry"].is_object()) {
        const auto& l_jCarry = l_jJ["carry"];
        if (l_jCarry.contains("skills") && l_jCarry["skills"].is_object())
            l_oC.m_fSkillEffectScale = l_jCarry["skills"].value("effectScale", l_oC.m_fSkillEffectScale);
        if (l_jCarry.contains("atkDefBonus") && l_jCarry["atkDefBonus"].is_object())
            l_oC.m_fStatBonusScale = l_jCarry["atkDefBonus"].value("value", l_oC.m_fStatBonusScale);
    }
    return l_oC;
}

} // namespace toms
