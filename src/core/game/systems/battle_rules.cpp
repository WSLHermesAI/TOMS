// battle_rules.cpp -- see battle_rules.h.
#include "battle_rules.h"

#include <algorithm>
#include <cmath>

namespace toms {

// ---- looks ----

const LookState* LooksTable::f_Find(const std::string& l_strLook, const std::string& l_strState, const std::string& l_strFallback) const {
    std::string l_strId = m_mapLooks.count(l_strLook) ? l_strLook : l_strFallback;
    for (int l_iStep = 0; l_iStep < 8 && !l_strId.empty(); l_iStep++) {
        auto l_itIt = m_mapLooks.find(l_strId);
        if (l_itIt == m_mapLooks.end()) return nullptr;
        auto l_itS = l_itIt->second.m_mapStates.find(l_strState);
        if (l_itS != l_itIt->second.m_mapStates.end()) return &l_itS->second;
        l_strId = l_itIt->second.m_strBase;
    }
    return nullptr;
}

LooksTable f_LooksFromJson(const nlohmann::json& l_jJ) {
    LooksTable l_oT;
    if (!l_jJ.is_object()) return l_oT;
    for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
        if (l_oIt.key().empty() || l_oIt.key()[0] == '_' || !l_oIt.value().is_object()) continue;
        Look l_oL;
        l_oL.m_strBase = l_oIt.value().value("base", std::string());
        l_oL.m_strRender = l_oIt.value().value("render", std::string());
        l_oL.m_strCharacter = l_oIt.value().value("character", std::string());
        l_oL.m_strModel = l_oIt.value().value("model", std::string());
        const nlohmann::json l_jSt = l_oIt.value().value("states", nlohmann::json::object());
        for (auto l_oS = l_jSt.begin(); l_oS != l_jSt.end(); ++l_oS) {
            if (!l_oS.value().is_object()) continue;
            LookState l_oLs;
            l_oLs.m_strClip = l_oS.value().value("clip", std::string());
            l_oLs.m_strSound = l_oS.value().value("sound", std::string());
            l_oLs.m_strFx = l_oS.value().value("fx", std::string());
            l_oL.m_mapStates[l_oS.key()] = l_oLs;
        }
        l_oT.m_mapLooks[l_oIt.key()] = l_oL;
    }
    return l_oT;
}

// ---- monster events ----

std::vector<MonsterEvent> f_MonsterEventsFromJson(const nlohmann::json& l_jJ) {
    std::vector<MonsterEvent> l_vOut;
    if (!l_jJ.is_array()) return l_vOut;
    for (const auto& l_jE : l_jJ) {
        if (!l_jE.is_object()) continue;
        MonsterEvent l_oM;
        l_oM.m_strOn = l_jE.value("on", std::string());
        l_oM.m_fValue = l_jE.value("value", 0.0f);
        l_oM.m_bOnce = l_jE.value("once", l_oM.m_strOn != "everyMs" && l_oM.m_strOn != "perfectHit");
        if (l_jE.contains("do") && l_jE["do"].is_array()) l_oM.m_jActions = l_jE["do"];
        if (!l_oM.m_strOn.empty()) l_vOut.push_back(l_oM);
    }
    return l_vOut;
}

std::vector<int> f_FireMonsterEvents(std::vector<MonsterEvent>& l_vEvents, const EventCheck& l_oC) {
    std::vector<int> l_vOut;
    for (size_t l_nI = 0; l_nI < l_vEvents.size(); l_nI++) {
        MonsterEvent& l_oE = l_vEvents[l_nI];
        if (l_oE.m_strOn != l_oC.m_strOn || (l_oE.m_bOnce && l_oE.m_iFired > 0)) continue;
        bool l_bFire = false;
        if (l_oC.m_strOn == "hpBelow" || l_oC.m_strOn == "playerHpBelow")   // crossing the line, not every hit below it
            l_bFire = l_oC.m_fHpPctBefore >= l_oE.m_fValue && l_oC.m_fHpPctAfter < l_oE.m_fValue;
        else if (l_oC.m_strOn == "attacks") {
            const int l_iN = std::max(1, (int)std::lround(l_oE.m_fValue));
            l_bFire = l_oE.m_bOnce ? l_oC.m_iAttacks == l_iN : l_oC.m_iAttacks > 0 && l_oC.m_iAttacks % l_iN == 0;
        } else if (l_oC.m_strOn == "everyMs") {
            const int l_iN = std::max(100, (int)std::lround(l_oE.m_fValue));
            l_bFire = l_oC.m_iMsAfter / l_iN > l_oC.m_iMsBefore / l_iN;
        } else
            l_bFire = true;   // beforeBattle, battleStart, perfectHit, falls
        if (!l_bFire) continue;
        l_oE.m_iFired++;
        l_vOut.push_back((int)l_nI);
    }
    return l_vOut;
}

// ---- moves ----

const MoveDef& MovesTable::f_Get(const std::string& l_strId) const {
    static const MoveDef l_oStrike = [] { MoveDef l_oM; l_oM.m_strId = "strike"; l_oM.m_strText = "battle.intent.strike"; return l_oM; }();
    auto l_itIt = m_mapMoves.find(l_strId);
    return l_itIt != m_mapMoves.end() ? l_itIt->second : l_oStrike;
}

MoveKind f_MoveKindFromString(const std::string& l_strS) {
    if (l_strS == "heavy") return MoveKind::Heavy;
    if (l_strS == "jabs") return MoveKind::Jabs;
    if (l_strS == "spell") return MoveKind::Spell;
    if (l_strS == "guard") return MoveKind::Guard;
    return MoveKind::Strike;
}

const char* f_MoveKindName(MoveKind l_eK) {
    switch (l_eK) {
    case MoveKind::Heavy: return "heavy";
    case MoveKind::Jabs: return "jabs";
    case MoveKind::Spell: return "spell";
    case MoveKind::Guard: return "guard";
    default: return "strike";
    }
}

MovesTable f_MovesFromJson(const nlohmann::json& l_jJ) {
    MovesTable l_oT;
    if (!l_jJ.is_object()) return l_oT;
    for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
        if (l_oIt.key().empty() || l_oIt.key()[0] == '_' || !l_oIt.value().is_object()) continue;
        const nlohmann::json& l_jV = l_oIt.value();
        MoveDef l_oM;
        l_oM.m_strId = l_oIt.key();
        l_oM.m_eKind = f_MoveKindFromString(l_jV.value("kind", l_oIt.key()));
        l_oM.m_fDamage = std::max(0.0f, l_jV.value("damage", 1.0f));
        l_oM.m_iHits = std::max(1, l_jV.value("hits", 1));
        l_oM.m_fTime = std::max(0.1f, l_jV.value("time", 1.0f));
        l_oM.m_iBreakHits = std::max(1, l_jV.value("breakHits", 2));
        l_oM.m_fReduce = std::clamp(l_jV.value("reduce", 0.3f), 0.0f, 1.0f);
        l_oM.m_strText = l_jV.value("text", "battle.intent." + std::string(f_MoveKindName(l_oM.m_eKind)));
        l_oM.m_strIcon = l_jV.value("icon", std::string());
        l_oM.m_iChargeMs = std::max(0, l_jV.value("chargeMs", 0));
        if (l_jV.contains("gapsMs") && l_jV["gapsMs"].is_array()) {
            for (const auto& l_jG : l_jV["gapsMs"])
                if (l_jG.is_number()) l_oM.m_vGapsMs.push_back(std::max(50, l_jG.get<int>()));
        } else if (l_jV.contains("gapsMs") && l_jV["gapsMs"].is_number()) {
            l_oM.m_vGapsMs.push_back(std::max(50, l_jV["gapsMs"].get<int>()));
        }
        l_oM.m_iWarnMs = l_jV.value("warnMs", -1);
        l_oM.m_strShape = l_jV.value("shape", std::string());
        l_oM.m_strGuardPlay = l_jV.value("guardPlay", std::string());
        l_oT.m_mapMoves[l_oM.m_strId] = l_oM;
    }
    return l_oT;
}

std::vector<MoveChoice> f_MoveChoicesFromJson(const nlohmann::json& l_jJ) {
    std::vector<MoveChoice> l_vOut;
    if (!l_jJ.is_array()) return l_vOut;
    for (const auto& l_jM : l_jJ) {
        MoveChoice l_oC;
        if (l_jM.is_string()) l_oC.m_strId = l_jM.get<std::string>();
        else if (l_jM.is_object()) { l_oC.m_strId = l_jM.value("id", std::string()); l_oC.m_iWeight = std::max(0, l_jM.value("weight", 1)); }
        if (!l_oC.m_strId.empty() && l_oC.m_iWeight > 0) l_vOut.push_back(l_oC);
    }
    return l_vOut;
}

std::string f_PickMove(const std::vector<MoveChoice>& l_vChoices, float l_fRoll) {
    int l_iTotal = 0;
    for (const MoveChoice& l_oC : l_vChoices) l_iTotal += l_oC.m_iWeight;
    if (l_iTotal <= 0) return "strike";
    int l_iAt = std::clamp((int)std::floor(l_fRoll * l_iTotal), 0, l_iTotal - 1);
    for (const MoveChoice& l_oC : l_vChoices) {
        if (l_iAt < l_oC.m_iWeight) return l_oC.m_strId;
        l_iAt -= l_oC.m_iWeight;
    }
    return l_vChoices.back().m_strId;
}

// ---- the pace ----

Pace f_PaceFromString(const std::string& l_strS, Pace l_eFallback) {
    if (l_strS == "stamina") return Pace::Stamina;
    if (l_strS == "cooldown") return Pace::Cooldown;
    return l_eFallback;
}

const char* f_PaceName(Pace l_eP) { return l_eP == Pace::Stamina ? "stamina" : "cooldown"; }

PaceSettings f_PaceFromJson(const nlohmann::json& l_jB) {
    PaceSettings l_oP;
    if (!l_jB.is_object()) return l_oP;
    l_oP.m_ePace = f_PaceFromString(l_jB.value("pace", std::string()), Pace::Cooldown);
    const nlohmann::json l_jS = l_jB.value("stamina", nlohmann::json::object());
    l_oP.m_iStaminaMax = std::max(1, l_jS.value("max", l_oP.m_iStaminaMax));
    l_oP.m_fStaminaRegenPerS = std::max(0.0f, l_jS.value("regenPerS", l_oP.m_fStaminaRegenPerS));
    l_oP.m_iAttackCost = std::max(0, l_jS.value("attack", l_oP.m_iAttackCost));
    l_oP.m_iGuardCost = std::max(0, l_jS.value("guard", l_oP.m_iGuardCost));
    return l_oP;
}

// ---- break ----

BreakSettings f_BreakFromJson(const nlohmann::json& l_jB) {
    BreakSettings l_oS;
    if (!l_jB.is_object()) return l_oS;
    const nlohmann::json l_jJ = l_jB.value("break", nlohmann::json::object());
    l_oS.m_iPoints = std::max(1, l_jJ.value("points", l_oS.m_iPoints));
    l_oS.m_iBrokenMs = std::max(0, l_jJ.value("brokenMs", l_oS.m_iBrokenMs));
    l_oS.m_fBrokenDamage = std::max(1.0f, l_jJ.value("brokenDamage", l_oS.m_fBrokenDamage));
    l_oS.m_iParryMs = std::max(0, l_jJ.value("parryMs", l_oS.m_iParryMs));
    l_oS.m_fParryDamage = std::max(1.0f, l_jJ.value("parryDamage", l_oS.m_fParryDamage));
    l_oS.m_iParryPower = std::clamp(l_jJ.value("parryPower", l_oS.m_iParryPower), 0, 100);
    l_oS.m_iParryBreak = std::max(0, l_jJ.value("parryBreak", l_oS.m_iParryBreak));
    l_oS.m_iGuardBreak = std::max(0, l_jJ.value("guardBreak", l_oS.m_iGuardBreak));
    return l_oS;
}

bool f_AddBreak(float& l_fGauge, float l_fAdd, int l_iPoints) {
    l_fGauge += std::max(0.0f, l_fAdd);
    if (l_fGauge + 1e-4f < (float)std::max(1, l_iPoints)) return false;
    l_fGauge = 0.0f;
    return true;
}

// ---- the revamp ----

int FeelSettings::f_Stop(const std::string& l_strM, bool l_bReduced) const {
    auto l_itIt = m_mapStopMs.find(l_strM);
    if (l_itIt == m_mapStopMs.end()) return 0;
    return std::max(0, (int)std::lround(l_itIt->second * m_fStrength * (l_bReduced ? m_fReducedScale : 1.0f)));
}

float FeelSettings::f_Shake(const std::string& l_strM, bool l_bReduced) const {
    auto l_itIt = m_mapShakePx.find(l_strM);
    if (l_itIt == m_mapShakePx.end()) return 0.0f;
    return std::max(0.0f, l_itIt->second * m_fStrength * (l_bReduced ? m_fReducedScale : 1.0f));
}

std::string FeelSettings::f_Sound(const std::string& l_strM) const {
    auto l_itIt = m_mapSounds.find(l_strM);
    return l_itIt != m_mapSounds.end() ? l_itIt->second : std::string();
}

FeelSettings f_FeelFromJson(const nlohmann::json& l_jB) {
    FeelSettings l_oF;
    l_oF.m_mapStopMs = {{"hit", 25}, {"great", 50}, {"perfect", 80}, {"parry", 90}, {"break", 120}, {"hurt", 30},
                {"heavyHurt", 60}, {"ultimate", 120}};
    l_oF.m_mapShakePx = {{"hit", 2}, {"great", 4}, {"perfect", 7}, {"parry", 4}, {"break", 9}, {"hurt", 4},
                 {"heavyHurt", 8}, {"ultimate", 10}, {"phase", 6}};
    if (!l_jB.is_object() || !l_jB.contains("feel") || !l_jB["feel"].is_object()) return l_oF;
    const nlohmann::json& l_jJ = l_jB["feel"];
    l_oF.m_fStrength = std::max(0.0f, l_jJ.value("strength", l_oF.m_fStrength));
    for (const char* l_pszK : {"stopMs", "shakePx", "sounds"})
        if (l_jJ.contains(l_pszK) && l_jJ[l_pszK].is_object())
            for (auto l_oIt = l_jJ[l_pszK].begin(); l_oIt != l_jJ[l_pszK].end(); ++l_oIt) {
                if (std::string(l_pszK) == "stopMs" && l_oIt.value().is_number()) l_oF.m_mapStopMs[l_oIt.key()] = l_oIt.value().get<int>();
                if (std::string(l_pszK) == "shakePx" && l_oIt.value().is_number()) l_oF.m_mapShakePx[l_oIt.key()] = l_oIt.value().get<float>();
                if (std::string(l_pszK) == "sounds" && l_oIt.value().is_string()) l_oF.m_mapSounds[l_oIt.key()] = l_oIt.value().get<std::string>();
            }
    l_oF.m_iShakeMs = std::max(0, l_jJ.value("shakeMs", l_oF.m_iShakeMs));
    l_oF.m_iSlowMoMs = std::max(0, l_jJ.value("slowMoMs", l_oF.m_iSlowMoMs));
    l_oF.m_fSlowMoScale = std::clamp(l_jJ.value("slowMoScale", l_oF.m_fSlowMoScale), 0.05f, 1.0f);
    l_oF.m_strWinClip = l_jJ.value("winClip", l_oF.m_strWinClip);
    l_oF.m_strWinBigClip = l_jJ.value("winBigClip", l_oF.m_strWinBigClip);
    l_oF.m_strWinParClip = l_jJ.value("winParClip", l_oF.m_strWinParClip);
    l_oF.m_strLoseClip = l_jJ.value("loseClip", l_oF.m_strLoseClip);
    l_oF.m_fWinParHp = std::clamp(l_jJ.value("winParHp", l_oF.m_fWinParHp), 0.0f, 1.0f);
    l_oF.m_iEndMaxMs = std::max(0, l_jJ.value("endMaxMs", l_oF.m_iEndMaxMs));
    l_oF.m_fReducedScale = std::clamp(l_jJ.value("reducedScale", l_oF.m_fReducedScale), 0.0f, 1.0f);
    l_oF.m_fChainStep = std::max(0.0f, l_jJ.value("chainStep", l_oF.m_fChainStep));
    l_oF.m_iChainMax = std::max(1, l_jJ.value("chainMax", l_oF.m_iChainMax));
    return l_oF;
}

GradeSettings f_GradeFromJson(const nlohmann::json& l_jB) {
    GradeSettings l_oG;
    if (!l_jB.is_object() || !l_jB.contains("grade") || !l_jB["grade"].is_object()) return l_oG;
    const nlohmann::json& l_jJ = l_jB["grade"];
    l_oG.m_fParSeconds = l_jJ.value("parSeconds", l_oG.m_fParSeconds);
    l_oG.m_fTimePenalty = l_jJ.value("timePenalty", l_oG.m_fTimePenalty);
    l_oG.m_fDamagePenalty = l_jJ.value("damagePenalty", l_oG.m_fDamagePenalty);
    l_oG.m_fPerfectBonus = l_jJ.value("perfectBonus", l_oG.m_fPerfectBonus);
    l_oG.m_fChainBonus = l_jJ.value("chainBonus", l_oG.m_fChainBonus);
    l_oG.m_fS = l_jJ.value("s", l_oG.m_fS);
    l_oG.m_fA = l_jJ.value("a", l_oG.m_fA);
    l_oG.m_fB = l_jJ.value("b", l_oG.m_fB);
    l_oG.m_fSGold = std::max(0.0f, l_jJ.value("sGold", l_oG.m_fSGold));
    l_oG.m_fSCard = std::clamp(l_jJ.value("sCard", l_oG.m_fSCard), 0.0f, 1.0f);
    return l_oG;
}

std::string f_BattleGrade(const GradeInput& l_oIn, const GradeSettings& l_oG, int* l_pScore) {
    const float l_fSc = 100.0f - std::max(0.0f, l_oIn.m_fSeconds - l_oG.m_fParSeconds) * l_oG.m_fTimePenalty -
                     std::clamp(l_oIn.m_fLostShare, 0.0f, 1.0f) * l_oG.m_fDamagePenalty +
                     std::clamp(l_oIn.m_fPerfectRate, 0.0f, 1.0f) * l_oG.m_fPerfectBonus + (float)std::max(0, l_oIn.m_iBestChain) * l_oG.m_fChainBonus;
    if (l_pScore) *l_pScore = (int)std::lround(l_fSc);
    return l_fSc >= l_oG.m_fS ? "S" : l_fSc >= l_oG.m_fA ? "A" : l_fSc >= l_oG.m_fB ? "B" : "C";
}

GuardSettings f_GuardFromJson(const nlohmann::json& l_jB) {
    GuardSettings l_oG;
    if (!l_jB.is_object() || !l_jB.contains("guard") || !l_jB["guard"].is_object()) return l_oG;
    const nlohmann::json& l_jJ = l_jB["guard"];
    const std::string l_strMode = l_jJ.value("mode", l_oG.m_strMode);
    l_oG.m_strMode = l_strMode == "bar" || l_strMode == "warn" ? l_strMode : "timing";
    l_oG.m_iWarnMs = std::max(100, l_jJ.value("warnMs", l_oG.m_iWarnMs));
    l_oG.m_fParryShare = std::clamp(l_jJ.value("parryShare", l_oG.m_fParryShare), 0.0f, 1.0f);
    l_oG.m_iMissLockMs = std::max(0, l_jJ.value("missLockMs", l_oG.m_iMissLockMs));
    l_oG.m_iParryMs = std::max(0, l_jJ.value("parryMs", l_oG.m_iParryMs));
    l_oG.m_iBlockMs = std::max(l_oG.m_iParryMs, l_jJ.value("blockMs", l_oG.m_iBlockMs));
    l_oG.m_iEarlyMs = std::max(l_oG.m_iBlockMs, l_jJ.value("earlyMs", l_oG.m_iEarlyMs));
    l_oG.m_fBlockPower = std::clamp(l_jJ.value("blockPower", l_oG.m_fBlockPower), 0.0f, 100.0f);
    l_oG.m_iCooldownMs = std::max(0, l_jJ.value("cooldownMs", l_oG.m_iCooldownMs));
    return l_oG;
}

GuardTiming f_GuardWarn(int l_iSinceMs, int l_iWarnMs, const GuardSettings& l_oG) {
    if (l_iSinceMs < 0 || l_iSinceMs > l_iWarnMs) return GuardTiming::None;
    return (float)l_iSinceMs >= (float)l_iWarnMs * (1.0f - l_oG.m_fParryShare) ? GuardTiming::Parry : GuardTiming::Block;
}

int MoveDef::f_GapBefore(int l_iHit) const {
    if (l_iHit <= 0 || m_vGapsMs.empty()) return 0;
    return m_vGapsMs[(size_t)std::min(l_iHit - 1, (int)m_vGapsMs.size() - 1)];
}

GuardTiming f_GuardTiming(int l_iSinceMs, const GuardSettings& l_oG) {
    if (l_iSinceMs < 0) return GuardTiming::None;
    if (l_iSinceMs <= l_oG.m_iParryMs) return GuardTiming::Parry;
    if (l_iSinceMs <= l_oG.m_iBlockMs) return GuardTiming::Block;
    if (l_iSinceMs <= l_oG.m_iEarlyMs) return GuardTiming::Early;
    return GuardTiming::None;
}

BattleTutorial f_TutorialFromJson(const nlohmann::json& l_jBattle) {
    BattleTutorial l_oT;
    const nlohmann::json l_jT = l_jBattle.value("tutorial", nlohmann::json::object());
    l_oT.m_strEnemy = l_jT.value("enemy", std::string());
    l_oT.m_jOverrides = l_jT.value("overrides", nlohmann::json::object());
    for (const nlohmann::json& l_jS : l_jT.value("steps", nlohmann::json::array())) {
        TutorialStep l_oS;
        l_oS.m_strText = l_jS.value("text", std::string());
        l_oS.m_strMark = l_jS.value("mark", std::string());
        l_oS.m_strDoneOn = l_jS.value("doneOn", std::string());
        l_oS.m_strMove = l_jS.value("move", std::string());
        l_oS.m_iCount = std::max(1, l_jS.value("count", 1));
        l_oS.m_bHold = l_jS.value("hold", false);
        l_oS.m_bOnce = l_jS.value("once", false);
        l_oT.m_vSteps.push_back(l_oS);
    }
    return l_oT;
}

} // namespace toms
