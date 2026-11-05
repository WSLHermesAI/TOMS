// weapon_play.cpp -- see weapon_play.h.
#include "weapon_play.h"
#include "save_system.h"   // readJsonFileSafe

#include <algorithm>
#include <cmath>

namespace toms {

const ComboTier* WeaponPlay::f_ComboTierAt(int l_iCount) const {
    const ComboTier* l_pBest = nullptr;
    for (const ComboTier& l_oT : m_vComboTiers) if (l_oT.m_iFrom <= l_iCount) l_pBest = &l_oT;
    return l_pBest;
}

std::vector<std::string> WeaponPlay::f_SwipeCuesAt(int l_iLv) const {
    std::vector<std::string> l_vOut;
    for (const SwipeCues& l_oC : m_vSwipeCues) if (l_oC.m_iLv <= l_iLv) l_vOut = l_oC.m_vCues;
    return l_vOut;
}

WeaponPlay f_WeaponPlayFromJson(const std::string& l_strId, const nlohmann::json& l_jJ) {
    WeaponPlay l_oW;
    l_oW.m_strId = l_strId;
    if (!l_jJ.is_object()) return l_oW;
    l_oW.m_iMissWaitMs = std::max(0, l_jJ.value("missWaitMs", 0));
    if (l_jJ.contains("missBack") && l_jJ["missBack"].is_array()) {
        for (const auto& l_jR : l_jJ["missBack"]) {
            if (!l_jR.is_object()) continue;
            MissBackRule l_oRule;
            l_oRule.m_iLv = std::max(1, l_jR.value("lv", 1));
            const auto& l_jB = l_jR.contains("back") ? l_jR["back"] : nlohmann::json("start");
            l_oRule.m_iBack = l_jB.is_number_integer() ? std::max(0, l_jB.get<int>()) : -1;   // "start" (or anything else): -1
            l_oW.m_vMissBack.push_back(l_oRule);
        }
        std::stable_sort(l_oW.m_vMissBack.begin(), l_oW.m_vMissBack.end(),
                         [](const MissBackRule& l_oA, const MissBackRule& l_oB) { return l_oA.m_iLv < l_oB.m_iLv; });
    }
    if (l_jJ.contains("levels") && l_jJ["levels"].is_array()) {
        for (const auto& l_jL : l_jJ["levels"]) {
            if (!l_jL.is_object()) continue;
            WeaponLevel l_oLv;
            l_oLv.m_iLv = std::max(1, l_jL.value("lv", 1));
            l_oLv.m_iPoints = std::max(1, l_jL.value("points", 3));
            if (l_jL.contains("stats") && l_jL["stats"].is_object())
                for (auto l_oS = l_jL["stats"].begin(); l_oS != l_jL["stats"].end(); ++l_oS)
                    if (l_oS.value().is_number_integer()) l_oLv.m_mapStats[l_oS.key()] = l_oS.value().get<int>();
            l_oW.m_vLevels.push_back(l_oLv);
        }
        std::stable_sort(l_oW.m_vLevels.begin(), l_oW.m_vLevels.end(), [](const WeaponLevel& l_oA, const WeaponLevel& l_oB) { return l_oA.m_iLv < l_oB.m_iLv; });
    }
    if (l_jJ.contains("levelExp") && l_jJ["levelExp"].is_object()) {
        l_oW.m_iLevelExpFirst = std::max(1, l_jJ["levelExp"].value("first", l_oW.m_iLevelExpFirst));
        l_oW.m_fLevelExpScale = std::max(1.0f, l_jJ["levelExp"].value("scale", l_oW.m_fLevelExpScale));
    }
    l_oW.m_bTurnBack = l_jJ.value("turnBack", l_oW.m_bTurnBack);
    l_oW.m_iSweepMs = std::max(200, l_jJ.value("sweepMs", l_oW.m_iSweepMs));
    l_oW.m_iMaxSweeps = std::max(1, l_jJ.value("maxSweeps", l_oW.m_iMaxSweeps));
    l_oW.m_iChargeMs = std::max(1, l_jJ.value("chargeMs", l_oW.m_iChargeMs));
    l_oW.m_fChargeSpeedUp = std::max(0.0f, l_jJ.value("chargeSpeedUp", l_oW.m_fChargeSpeedUp));
    l_oW.m_fChargeDamage = std::max(0.0f, l_jJ.value("chargeDamage", l_oW.m_fChargeDamage));
    l_oW.m_fHitWindow = std::clamp(l_jJ.value("hitWindow", l_oW.m_fHitWindow), 0.01f, 0.25f);
    l_oW.m_iHitWindowMs = std::clamp(l_jJ.value("hitWindowMs", l_oW.m_iHitWindowMs), 0, 1000);
    l_oW.m_iLateMs = std::clamp(l_jJ.value("lateMs", l_oW.m_iLateMs), 0, 500);
    if (l_jJ.contains("fx") && l_jJ["fx"].is_array()) {
        for (const auto& l_jF : l_jJ["fx"]) {
            if (!l_jF.is_object()) continue;
            WeaponFx l_oFx;
            l_oFx.m_iLv = std::max(1, l_jF.value("lv", 1));
            l_oFx.m_strRelease = l_jF.value("release", std::string());
            l_oFx.m_strHit = l_jF.value("hit", std::string());
            l_oW.m_vFx.push_back(l_oFx);
        }
        std::stable_sort(l_oW.m_vFx.begin(), l_oW.m_vFx.end(), [](const WeaponFx& l_oA, const WeaponFx& l_oB) { return l_oA.m_iLv < l_oB.m_iLv; });
    }
    if (l_jJ.contains("results") && l_jJ["results"].is_array()) {
        for (const auto& l_jR : l_jJ["results"]) {
            if (!l_jR.is_object()) continue;
            WeaponResult l_oX;
            const nlohmann::json l_jH = l_jR.value("hits", nlohmann::json(0));
            l_oX.m_iHits = l_jH.is_string() ? (l_jH.get<std::string>() == "all" ? -1 : 0) : std::max(0, l_jH.get<int>());
            l_oX.m_strName = l_jR.value("name", std::string());
            l_oX.m_fDamage = std::max(0.0f, l_jR.value("damage", 1.0f));
            l_oX.m_strFx = l_jR.value("fx", std::string());
            l_oX.m_strAnim = l_jR.value("anim", std::string());
            l_oX.m_strSound = l_jR.value("sound", std::string());
            l_oX.m_strMove = l_jR.value("move", std::string());
            l_oX.m_iStunMs = std::max(0, l_jR.value("stunMs", 0));
            l_oX.m_iPushMs = std::max(0, l_jR.value("pushMs", 0));
            l_oX.m_fBreakAdd = std::max(0.0f, l_jR.value("breakAdd", 0.0f));
            l_oW.m_vResults.push_back(l_oX);
        }
    }
    l_oW.m_strPace = l_jJ.value("pace", std::string());
    l_oW.m_strPlay = l_jJ.value("play", std::string("track"));
    l_oW.m_strUltimate = l_jJ.value("ultimate", std::string());
    l_oW.m_bComboKeep = l_jJ.value("comboKeep", false);
    if (l_jJ.contains("comboTiers") && l_jJ["comboTiers"].is_array()) {
        for (const auto& l_jE : l_jJ["comboTiers"]) {
            if (!l_jE.is_object()) continue;
            ComboTier l_oT;
            l_oT.m_iFrom = std::max(1, l_jE.value("from", 1));
            l_oT.m_strClip = l_jE.value("clip", std::string());
            l_oT.m_strFx = l_jE.value("fx", std::string());
            l_oT.m_strFeel = l_jE.value("feel", std::string());
            l_oW.m_vComboTiers.push_back(l_oT);
        }
        std::sort(l_oW.m_vComboTiers.begin(), l_oW.m_vComboTiers.end(), [](const ComboTier& l_oA, const ComboTier& l_oB) { return l_oA.m_iFrom < l_oB.m_iFrom; });
    }
    if (l_jJ.contains("swipeCues") && l_jJ["swipeCues"].is_array()) {
        for (const auto& l_jE : l_jJ["swipeCues"]) {
            if (!l_jE.is_object()) continue;
            WeaponPlay::SwipeCues l_oC;
            l_oC.m_iLv = std::max(1, l_jE.value("lv", 1));
            if (l_jE.contains("cues") && l_jE["cues"].is_array())
                for (const auto& l_jS : l_jE["cues"]) if (l_jS.is_string()) l_oC.m_vCues.push_back(l_jS.get<std::string>());
            l_oW.m_vSwipeCues.push_back(l_oC);
        }
        std::sort(l_oW.m_vSwipeCues.begin(), l_oW.m_vSwipeCues.end(), [](const auto& l_oA, const auto& l_oB) { return l_oA.m_iLv < l_oB.m_iLv; });
    }
    return l_oW;
}

const WeaponResult* f_WeaponResultFor(const WeaponPlay& l_oW, int l_iHits, int l_iPoints, bool l_bFailed) {
    if (l_bFailed) l_iHits = 0;
    const WeaponResult* l_pBest = nullptr;
    int l_iBestNeed = -1;
    for (const WeaponResult& l_oR : l_oW.m_vResults) {
        const int l_iNeed = l_oR.m_iHits < 0 ? std::max(1, l_iPoints) : l_oR.m_iHits;   // "all": every point
        if (l_iHits >= l_iNeed && l_iNeed > l_iBestNeed) { l_pBest = &l_oR; l_iBestNeed = l_iNeed; }
    }
    return l_pBest;
}

const WeaponFx* f_WeaponFxEntry(const WeaponPlay& l_oW, int l_iWeaponLevel) {
    const WeaponFx* l_pBest = nullptr;
    for (const WeaponFx& l_oF : l_oW.m_vFx)
        if (l_oF.m_iLv <= l_iWeaponLevel) l_pBest = &l_oF;
    return l_pBest ? l_pBest : (l_oW.m_vFx.empty() ? nullptr : &l_oW.m_vFx.front());
}

const WeaponLevel& f_WeaponLevelEntry(const WeaponPlay& l_oW, int l_iWeaponLevel) {
    static const WeaponLevel l_oBuiltIn;
    const WeaponLevel* l_pBest = nullptr;
    for (const WeaponLevel& l_oL : l_oW.m_vLevels)
        if (l_oL.m_iLv <= l_iWeaponLevel) l_pBest = &l_oL;
    return l_pBest ? *l_pBest : (l_oW.m_vLevels.empty() ? l_oBuiltIn : l_oW.m_vLevels.front());
}

int f_WeaponMaxLevel(const WeaponPlay& l_oW) { return l_oW.m_vLevels.empty() ? 1 : std::max(1, l_oW.m_vLevels.back().m_iLv); }

int f_ExpToNextLevel(const WeaponPlay& l_oW, int l_iWeaponLevel) {
    if (l_iWeaponLevel < 1 || l_iWeaponLevel >= f_WeaponMaxLevel(l_oW)) return 0;
    return std::max(1, (int)std::lround((float)l_oW.m_iLevelExpFirst * std::pow(l_oW.m_fLevelExpScale, (float)(l_iWeaponLevel - 1))));
}

int f_WeaponLevelFromExp(const WeaponPlay& l_oW, int l_iExp) {
    int l_iLv = 1;
    for (int l_iNeed = f_ExpToNextLevel(l_oW, l_iLv); l_iNeed > 0 && l_iExp >= l_iNeed; l_iNeed = f_ExpToNextLevel(l_oW, l_iLv)) {
        l_iExp -= l_iNeed;
        l_iLv++;
    }
    return l_iLv;
}

int f_WeaponExpForLevel(const WeaponPlay& l_oW, int l_iWeaponLevel) {
    int l_iAll = 0;
    for (int l_iLv = 1; l_iLv < l_iWeaponLevel; l_iLv++) l_iAll += f_ExpToNextLevel(l_oW, l_iLv);
    return l_iAll;
}

WeaponPlayTable f_LoadWeaponPlay(const std::string& l_strPath) {
    WeaponPlayTable l_oT;
    const nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
    if (!l_jJ.is_object()) return l_oT;
    for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
        if (l_oIt.key().empty() || l_oIt.key()[0] == '_' || !l_oIt.value().is_object()) continue;   // "_comment"
        l_oT.m_mapWeapons[l_oIt.key()] = f_WeaponPlayFromJson(l_oIt.key(), l_oIt.value());
    }
    return l_oT;
}

const WeaponPlay& WeaponPlayTable::f_Get(const std::string& l_strWeaponId) const {
    static const WeaponPlay l_oBuiltIn = [] { WeaponPlay l_oW; l_oW.m_strId = "builtin"; return l_oW; }();
    auto l_itIt = m_mapWeapons.find(l_strWeaponId);
    if (l_itIt != m_mapWeapons.end()) return l_itIt->second;
    l_itIt = m_mapWeapons.find("default");
    return l_itIt != m_mapWeapons.end() ? l_itIt->second : l_oBuiltIn;
}

int f_MissBackPoints(const WeaponPlay& l_oW, int l_iWeaponLevel) {
    int l_iBack = -1;   // no rule reaches this level: back to the start
    for (const MissBackRule& l_oR : l_oW.m_vMissBack)
        if (l_oR.m_iLv <= l_iWeaponLevel) l_iBack = l_oR.m_iBack;
    return l_iBack;
}

int f_StepAfterMiss(const WeaponPlay& l_oW, int l_iWeaponLevel, int l_iStep) {
    const int l_iBack = f_MissBackPoints(l_oW, l_iWeaponLevel);
    if (l_iBack < 0) return 0;
    return std::max(0, l_iStep - l_iBack);
}

} // namespace toms
