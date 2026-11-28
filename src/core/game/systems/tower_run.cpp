// tower_run.cpp -- see tower_run.h (docs/gameplay/27 §3).
#include "tower_run.h"
#include "field_keys.h"
#include <algorithm>
#include <cmath>

namespace toms {

bool StaminaClock::f_Catch(const StaminaRules& l_oR, long long l_llNow) {
    const int l_iEvery = std::max(1, l_oR.m_iEverySec);
    bool l_bChanged = false;
    if (m_iNow > l_oR.m_iMax) { m_iNow = l_oR.m_iMax; l_bChanged = true; }
    if (m_iNow >= l_oR.m_iMax) {   // full: no timer
        if (m_llNextAt != 0) { m_llNextAt = 0; l_bChanged = true; }
        return l_bChanged;
    }
    if (m_llNextAt <= 0) { m_llNextAt = l_llNow + l_iEvery; return true; }   // (a file from before the timer: start it)
    while (m_iNow < l_oR.m_iMax && m_llNextAt <= l_llNow) {   // each time come: a point, and the next timer after it
        m_iNow++;
        m_llNextAt += l_iEvery;
        l_bChanged = true;
    }
    if (m_iNow >= l_oR.m_iMax) m_llNextAt = 0;
    return l_bChanged;
}

bool StaminaClock::f_Spend(const StaminaRules& l_oR, long long l_llNow, int l_iCost) {
    f_Catch(l_oR, l_llNow);
    if (l_iCost <= 0) return true;
    if (m_iNow < l_iCost) return false;
    const bool l_bWasFull = m_iNow >= l_oR.m_iMax;
    m_iNow -= l_iCost;
    if (l_bWasFull || m_llNextAt <= 0) m_llNextAt = l_llNow + std::max(1, l_oR.m_iEverySec);   // not full now: the timer starts
    return true;
}

nlohmann::json f_StaminaToJson(const StaminaClock& l_oC) {
    return nlohmann::json{{field::g_pszStamina, l_oC.m_iNow}, {field::g_pszNextAt, l_oC.m_llNextAt}};
}

StaminaClock f_StaminaFromJson(const nlohmann::json& l_jJ, const StaminaRules& l_oR) {
    StaminaClock l_oC;
    l_oC.m_iNow = l_oR.m_iMax;
    if (!l_jJ.is_object()) return l_oC;
    l_oC.m_iNow = std::clamp(l_jJ.value(field::g_pszStamina, l_oR.m_iMax), 0, std::max(0, l_oR.m_iMax));
    l_oC.m_llNextAt = std::max(0LL, l_jJ.value(field::g_pszNextAt, 0LL));
    return l_oC;
}

TowerRunRules f_TowerRunFromJson(const nlohmann::json& l_jJ) {
    TowerRunRules l_oR;
    if (!l_jJ.is_object()) return l_oR;
    l_oR.m_bOn = l_jJ.value(field::g_pszOn, true);
    const nlohmann::json l_jS = l_jJ.value(field::g_pszStamina, nlohmann::json::object());
    l_oR.m_oStamina.m_iMax = std::max(1, l_jS.value(field::g_pszMax, 60));
    l_oR.m_oStamina.m_iEverySec = std::max(1, l_jS.value(field::g_pszEverySec, 300));
    l_oR.m_oStamina.m_iPerFight = std::max(0, l_jS.value(field::g_pszPerFight, 1));
    const nlohmann::json l_jD = l_jJ.value(field::g_pszDeath, nlohmann::json::object());
    l_oR.m_fKeepGoldOnDeath = std::clamp(l_jD.value(field::g_pszKeepGold, 0.5f), 0.0f, 1.0f);
    l_oR.m_fKeepCrystalsOnDeath = std::clamp(l_jD.value(field::g_pszKeepCrystals, 0.5f), 0.0f, 1.0f);
    const nlohmann::json l_jSc = l_jJ.value(field::g_pszScore, nlohmann::json::object());
    l_oR.m_iPerFloor = l_jSc.value(field::g_pszFloor, 100);
    l_oR.m_iPerMonster = l_jSc.value(field::g_pszMonster, 10);
    l_oR.m_iPerCombo = l_jSc.value(field::g_pszCombo, 5);
    l_oR.m_iPerPerfect = l_jSc.value(field::g_pszPerfect, 20);
    l_oR.m_fPerDamage = l_jSc.value(field::g_pszDamage, 0.1f);
    l_oR.m_fPerSecond = l_jSc.value(field::g_pszSecond, 0.05f);
    for (const auto& l_jRk : l_jJ.value(field::g_pszRanks, nlohmann::json::array())) {
        if (!l_jRk.is_object()) continue;
        RankRule l_oRk;
        l_oRk.m_strRank = l_jRk.value(field::g_pszRank, std::string());
        l_oRk.m_iMinScore = l_jRk.value(field::g_pszMin, 0);
        l_oRk.m_iCrystals = std::max(0, l_jRk.value(field::g_pszCrystals, 0));
        l_oR.m_vRanks.push_back(l_oRk);
    }
    std::sort(l_oR.m_vRanks.begin(), l_oR.m_vRanks.end(), [](const RankRule& l_oA, const RankRule& l_oB) { return l_oA.m_iMinScore > l_oB.m_iMinScore; });
    for (const auto& l_jSt : l_jJ.value(field::g_pszStories, nlohmann::json::array())) {
        if (!l_jSt.is_object()) continue;
        TowerStory l_oSt;
        l_oSt.m_strId = l_jSt.value(field::g_pszId, std::string());
        l_oSt.m_strTitle = l_jSt.value(field::g_pszTitle, std::string());
        l_oSt.m_bChallenge = l_jSt.value(field::g_pszChallenge, false);
        std::string l_strPrev;
        for (const auto& l_jC : l_jSt.value(field::g_pszChapters, nlohmann::json::array())) {
            TowerChapter l_oC;
            if (l_jC.is_string()) l_oC.m_strId = l_jC.get<std::string>();   // a story chapter by its id: after the one before it
            else if (l_jC.is_object()) {
                l_oC.m_strId = l_jC.value(field::g_pszId, std::string());
                l_oC.m_strFloorsOf = l_jC.value(field::g_pszFloors, std::string());
                l_oC.m_strAfter = l_jC.value(field::g_pszAfter, l_strPrev);
                l_oC.m_strTitle = l_jC.value(field::g_pszTitle, std::string());
                l_oC.m_fHpX = std::max(0.1f, l_jC.value(field::g_pszHpX, 1.0f));
                l_oC.m_fAtkX = std::max(0.1f, l_jC.value(field::g_pszAtkX, 1.0f));
                l_oC.m_fGoldX = std::max(0.0f, l_jC.value(field::g_pszGoldX, 1.0f));
                l_oC.m_iCrystals = std::max(0, l_jC.value(field::g_pszCrystals, 0));
            } else continue;
            if (l_oC.m_strId.empty()) continue;
            if (l_jC.is_string()) l_oC.m_strAfter = l_strPrev;
            if (l_oC.m_strFloorsOf.empty()) l_oC.m_strFloorsOf = l_oC.m_strId;
            l_strPrev = l_oC.m_strId;
            l_oSt.m_vChapters.push_back(l_oC);
        }
        if (!l_oSt.m_strId.empty()) l_oR.m_vStories.push_back(l_oSt);
    }
    return l_oR;
}

int f_ClimbScore(const ClimbStats& l_oS, const TowerRunRules& l_oR) {
    const float l_fScore = (float)(l_oS.m_iFloors * l_oR.m_iPerFloor + l_oS.m_iMonsters * l_oR.m_iPerMonster + l_oS.m_iBestCombo * l_oR.m_iPerCombo +
                                   l_oS.m_iPerfects * l_oR.m_iPerPerfect) -
                           (float)l_oS.m_iDamageTaken * l_oR.m_fPerDamage - (float)l_oS.m_iSeconds * l_oR.m_fPerSecond;
    return std::max(0, (int)std::lround(l_fScore));
}

const RankRule* f_RankFor(int l_iScore, const TowerRunRules& l_oR) {
    for (const RankRule& l_oRk : l_oR.m_vRanks) if (l_iScore >= l_oRk.m_iMinScore) return &l_oRk;
    return l_oR.m_vRanks.empty() ? nullptr : &l_oR.m_vRanks.back();
}

const TowerChapter* f_FindChapter(const TowerRunRules& l_oR, const std::string& l_strId, const TowerStory** l_ppStory) {
    for (const TowerStory& l_oSt : l_oR.m_vStories)
        for (const TowerChapter& l_oC : l_oSt.m_vChapters)
            if (l_oC.m_strId == l_strId) { if (l_ppStory) *l_ppStory = &l_oSt; return &l_oC; }
    return nullptr;
}

}  // namespace toms
