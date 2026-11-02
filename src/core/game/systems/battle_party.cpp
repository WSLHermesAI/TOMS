// battle_party.cpp -- see battle_party.h.
#include "battle_party.h"

#include <cstdio>

namespace toms {

int f_NextStanding(const std::vector<int>& l_vHp, int l_iFrom, int l_iDir) {
    const int l_iN = (int)l_vHp.size();
    if (l_iN == 0) return -1;
    if (l_iFrom < 0 || l_iFrom >= l_iN) l_iFrom = 0;
    if (l_iDir == 0 && l_vHp[(size_t)l_iFrom] > 0) return l_iFrom;
    const int l_iStep = l_iDir < 0 ? -1 : 1;
    for (int l_iK = 1; l_iK <= l_iN; l_iK++) {
        const int l_iI = ((l_iFrom + l_iStep * l_iK) % l_iN + l_iN) % l_iN;
        if (l_vHp[(size_t)l_iI] > 0) return l_iI;
    }
    return -1;
}

bool f_AllDown(const std::vector<int>& l_vHp) {
    for (int l_iH : l_vHp)
        if (l_iH > 0) return false;
    return true;
}

std::string f_BattleEventLine(const BattleEvent& l_oE, const std::vector<std::string>& l_vFoeIds) {
    auto l_fnName = [&](int l_iI) {
        if (l_iI < 0) return std::string("player");
        return "foe " + std::to_string(l_iI) + (l_iI < (int)l_vFoeIds.size() ? " (" + l_vFoeIds[(size_t)l_iI] + ")" : std::string());
    };
    char l_pszT[16];
    std::snprintf(l_pszT, sizeof l_pszT, "%6.2f", l_oE.m_iMs / 1000.0);
    std::string l_strS = std::string(l_pszT) + "  " + l_fnName(l_oE.m_iWho);
    if (l_oE.m_iWhom != l_oE.m_iWho && (l_oE.m_strType == "hit" || l_oE.m_strType == "combo" || l_oE.m_strType == "super" || l_oE.m_strType == "target" ||
                             l_oE.m_strType == "parry" || l_oE.m_strType == "guardBreak" || l_oE.m_strType == "spellBreak" || l_oE.m_strType == "summon"))
        l_strS += " -> " + l_fnName(l_oE.m_iWhom);
    l_strS += ": " + l_oE.m_strType;
    if (l_oE.m_strType == "hit" || l_oE.m_strType == "guard" || l_oE.m_strType == "combo" || l_oE.m_strType == "super") l_strS += " " + std::to_string(l_oE.m_iValue);
    if (!l_oE.m_strNote.empty()) l_strS += " (" + l_oE.m_strNote + ")";
    return l_strS;
}

} // namespace toms
