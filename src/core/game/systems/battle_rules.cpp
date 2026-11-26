// battle_rules.cpp -- see battle_rules.h.
#include "text_keys.h"     // toms::text: every line of text by its key
#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_names.h"   // toms::state, toms::battle_event, ... (no hard-coded name)
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
        l_oL.m_strBase = l_oIt.value().value(toms::field::g_pszBase, std::string());
        l_oL.m_strRender = l_oIt.value().value(toms::field::g_pszRender, std::string());
        l_oL.m_strCharacter = l_oIt.value().value(toms::field::g_pszCharacter, std::string());
        l_oL.m_strModel = l_oIt.value().value(toms::field::g_pszModel, std::string());
        const nlohmann::json l_jSt = l_oIt.value().value(toms::field::g_pszStates, nlohmann::json::object());
        for (auto l_oS = l_jSt.begin(); l_oS != l_jSt.end(); ++l_oS) {
            if (!l_oS.value().is_object()) continue;
            LookState l_oLs;
            l_oLs.m_strClip = l_oS.value().value(toms::field::g_pszClip, std::string());
            l_oLs.m_strSound = l_oS.value().value(toms::field::g_pszSound, std::string());
            l_oLs.m_strFx = l_oS.value().value(toms::field::g_pszFx, std::string());
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
        l_oM.m_strOn = l_jE.value(toms::field::g_pszOn, std::string());
        l_oM.m_fValue = l_jE.value(toms::field::g_pszValue, 0.0f);
        l_oM.m_bOnce = l_jE.value(toms::field::g_pszOnce, l_oM.m_strOn != "everyMs" && l_oM.m_strOn != "perfectHit");
        if (l_jE.contains(toms::field::g_pszDo) && l_jE[toms::field::g_pszDo].is_array()) l_oM.m_jActions = l_jE[toms::field::g_pszDo];
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
    static const MoveDef l_oStrike = [] { MoveDef l_oM; l_oM.m_strId = toms::move_kind::g_pszStrike; l_oM.m_strText = toms::text::g_pszBattleIntentStrike; return l_oM; }();
    auto l_itIt = m_mapMoves.find(l_strId);
    return l_itIt != m_mapMoves.end() ? l_itIt->second : l_oStrike;
}

MoveKind f_MoveKindFromString(const std::string& l_strS) {
    if (l_strS == toms::move_kind::g_pszHeavy) return MoveKind::Heavy;
    if (l_strS == toms::move_kind::g_pszJabs) return MoveKind::Jabs;
    if (l_strS == toms::move_kind::g_pszSpell) return MoveKind::Spell;
    if (l_strS == toms::move_kind::g_pszGuard) return MoveKind::Guard;
    return MoveKind::Strike;
}

const char* f_MoveKindName(MoveKind l_eK) {
    switch (l_eK) {
    case MoveKind::Heavy: return toms::move_kind::g_pszHeavy;
    case MoveKind::Jabs: return toms::move_kind::g_pszJabs;
    case MoveKind::Spell: return toms::move_kind::g_pszSpell;
    case MoveKind::Guard: return toms::move_kind::g_pszGuard;
    default: return toms::move_kind::g_pszStrike;
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
        l_oM.m_eKind = f_MoveKindFromString(l_jV.value(toms::field::g_pszKind, l_oIt.key()));
        l_oM.m_fDamage = std::max(0.0f, l_jV.value(toms::field::g_pszDamage, 1.0f));
        l_oM.m_iHits = std::max(1, l_jV.value(toms::field::g_pszHits, 1));
        l_oM.m_fTime = std::max(0.1f, l_jV.value(toms::field::g_pszTime, 1.0f));
        l_oM.m_iBreakHits = std::max(1, l_jV.value(toms::field::g_pszBreakHits, 2));
        l_oM.m_fReduce = std::clamp(l_jV.value(toms::field::g_pszReduce, 0.3f), 0.0f, 1.0f);
        l_oM.m_strText = l_jV.value(toms::field::g_pszText, "battle.intent." + std::string(f_MoveKindName(l_oM.m_eKind)));
        l_oM.m_strIcon = l_jV.value(toms::field::g_pszIcon, std::string());
        l_oM.m_iChargeMs = std::max(0, l_jV.value(toms::field::g_pszChargeMs, 0));
        if (l_jV.contains(toms::field::g_pszGapsMs) && l_jV[toms::field::g_pszGapsMs].is_array()) {
            for (const auto& l_jG : l_jV[toms::field::g_pszGapsMs])
                if (l_jG.is_number()) l_oM.m_vGapsMs.push_back(std::max(50, l_jG.get<int>()));
        } else if (l_jV.contains(toms::field::g_pszGapsMs) && l_jV[toms::field::g_pszGapsMs].is_number()) {
            l_oM.m_vGapsMs.push_back(std::max(50, l_jV[toms::field::g_pszGapsMs].get<int>()));
        }
        l_oM.m_iWarnMs = l_jV.value(toms::field::g_pszWarnMs, -1);
        l_oM.m_strShape = l_jV.value(toms::field::g_pszShape, std::string());
        l_oM.m_strGuardPlay = l_jV.value(toms::field::g_pszGuardPlay, std::string());
        l_oM.m_iMp = l_jV.contains(toms::field::g_pszMp) ? std::max(0, l_jV.value(toms::field::g_pszMp, 0)) : -1;
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
        else if (l_jM.is_object()) {
            l_oC.m_strId = l_jM.value(toms::field::g_pszId, std::string());
            l_oC.m_iWeight = std::max(0, l_jM.contains(toms::field::g_pszChance) ? l_jM.value(toms::field::g_pszChance, 1) : l_jM.value(toms::field::g_pszWeight, 1));
        }
        if (!l_oC.m_strId.empty() && l_oC.m_iWeight > 0) l_vOut.push_back(l_oC);
    }
    return l_vOut;
}

std::string f_PickMove(const std::vector<MoveChoice>& l_vChoices, float l_fRoll, const std::vector<bool>* l_pAllowed) {
    const auto l_fnIn = [&](size_t l_nI) { return !l_pAllowed || l_nI >= l_pAllowed->size() || (*l_pAllowed)[l_nI]; };
    int l_iTotal = 0;
    for (size_t l_nI = 0; l_nI < l_vChoices.size(); l_nI++) if (l_fnIn(l_nI)) l_iTotal += l_vChoices[l_nI].m_iWeight;
    if (l_iTotal <= 0) return toms::move_kind::g_pszStrike;
    int l_iAt = std::clamp((int)std::floor(l_fRoll * l_iTotal), 0, l_iTotal - 1);
    std::string l_strLast = toms::move_kind::g_pszStrike;
    for (size_t l_nI = 0; l_nI < l_vChoices.size(); l_nI++) {
        if (!l_fnIn(l_nI)) continue;
        if (l_iAt < l_vChoices[l_nI].m_iWeight) return l_vChoices[l_nI].m_strId;
        l_iAt -= l_vChoices[l_nI].m_iWeight;
        l_strLast = l_vChoices[l_nI].m_strId;
    }
    return l_strLast;
}

MonsterMpRules f_MonsterMpFor(const MonsterMpRules& l_oDefaults, const nlohmann::json& l_jMp) {
    MonsterMpRules l_oR = l_oDefaults;
    if (l_jMp.is_number()) {
        l_oR.m_iMax = std::max(0, l_jMp.get<int>());
        l_oR.m_iStart = std::min(l_oR.m_iStart, l_oR.m_iMax);
    } else if (l_jMp.is_object()) {
        l_oR.m_iMax = std::max(0, l_jMp.value(toms::field::g_pszMax, l_oR.m_iMax));
        l_oR.m_iStart = std::clamp(l_jMp.value(toms::field::g_pszStart, std::min(l_oR.m_iStart, l_oR.m_iMax)), 0, l_oR.m_iMax);
        l_oR.m_fRegen = std::max(0.0f, l_jMp.value(toms::field::g_pszRegen, l_oR.m_fRegen));
        l_oR.m_iSpecialCost = std::max(0, l_jMp.value(toms::field::g_pszSpecialCost, l_oR.m_iSpecialCost));
    }
    return l_oR;
}

MonsterMpRules f_MonsterMpFromJson(const nlohmann::json& l_jBattle) {
    if (!l_jBattle.is_object() || !l_jBattle.contains(toms::field::g_pszMonsterMp)) return MonsterMpRules{};
    return f_MonsterMpFor(MonsterMpRules{}, l_jBattle[toms::field::g_pszMonsterMp]);
}

// ---- the pace ----

Pace f_PaceFromString(const std::string& l_strS, Pace l_eFallback) {
    if (l_strS == toms::pace::g_pszStamina) return Pace::Stamina;
    if (l_strS == toms::pace::g_pszCooldown) return Pace::Cooldown;
    return l_eFallback;
}

const char* f_PaceName(Pace l_eP) { return l_eP == Pace::Stamina ? toms::pace::g_pszStamina : toms::pace::g_pszCooldown; }

PaceSettings f_PaceFromJson(const nlohmann::json& l_jB) {
    PaceSettings l_oP;
    if (!l_jB.is_object()) return l_oP;
    l_oP.m_ePace = f_PaceFromString(l_jB.value(toms::field::g_pszPace, std::string()), Pace::Cooldown);
    const nlohmann::json l_jS = l_jB.value(toms::field::g_pszStamina, nlohmann::json::object());
    l_oP.m_iStaminaMax = std::max(1, l_jS.value(toms::field::g_pszMax, l_oP.m_iStaminaMax));
    l_oP.m_fStaminaRegenPerS = std::max(0.0f, l_jS.value(toms::field::g_pszRegenPerS, l_oP.m_fStaminaRegenPerS));
    l_oP.m_iAttackCost = std::max(0, l_jS.value(toms::field::g_pszAttack, l_oP.m_iAttackCost));
    l_oP.m_iGuardCost = std::max(0, l_jS.value(toms::field::g_pszGuard, l_oP.m_iGuardCost));
    return l_oP;
}

// ---- break ----

BreakSettings f_BreakFromJson(const nlohmann::json& l_jB) {
    BreakSettings l_oS;
    if (!l_jB.is_object()) return l_oS;
    const nlohmann::json l_jJ = l_jB.value(toms::field::g_pszBreak, nlohmann::json::object());
    l_oS.m_iPoints = std::max(1, l_jJ.value(toms::field::g_pszPoints, l_oS.m_iPoints));
    l_oS.m_iBrokenMs = std::max(0, l_jJ.value(toms::field::g_pszBrokenMs, l_oS.m_iBrokenMs));
    l_oS.m_fBrokenDamage = std::max(1.0f, l_jJ.value(toms::field::g_pszBrokenDamage, l_oS.m_fBrokenDamage));
    l_oS.m_iParryMs = std::max(0, l_jJ.value(toms::field::g_pszParryMs, l_oS.m_iParryMs));
    l_oS.m_fParryDamage = std::max(1.0f, l_jJ.value(toms::field::g_pszParryDamage, l_oS.m_fParryDamage));
    l_oS.m_iParryPower = std::clamp(l_jJ.value(toms::field::g_pszParryPower, l_oS.m_iParryPower), 0, 100);
    l_oS.m_iParryBreak = std::max(0, l_jJ.value(toms::field::g_pszParryBreak, l_oS.m_iParryBreak));
    l_oS.m_iGuardBreak = std::max(0, l_jJ.value(toms::field::g_pszGuardBreak, l_oS.m_iGuardBreak));
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
    l_oF.m_mapStopMs = {{toms::moment::g_pszHit, 25}, {toms::moment::g_pszGreat, 50}, {toms::moment::g_pszPerfect, 80}, {toms::moment::g_pszParry, 90}, {toms::moment::g_pszBreak, 120}, {toms::moment::g_pszHurt, 30},
                {toms::moment::g_pszHeavyHurt, 60}, {toms::moment::g_pszUltimate, 120}};
    l_oF.m_mapShakePx = {{toms::moment::g_pszHit, 2}, {toms::moment::g_pszGreat, 4}, {toms::moment::g_pszPerfect, 7}, {toms::moment::g_pszParry, 4}, {toms::moment::g_pszBreak, 9}, {toms::moment::g_pszHurt, 4},
                 {toms::moment::g_pszHeavyHurt, 8}, {toms::moment::g_pszUltimate, 10}, {toms::moment::g_pszPhase, 6}};
    if (!l_jB.is_object() || !l_jB.contains(toms::field::g_pszFeel) || !l_jB[toms::field::g_pszFeel].is_object()) return l_oF;
    const nlohmann::json& l_jJ = l_jB[toms::field::g_pszFeel];
    l_oF.m_fStrength = std::max(0.0f, l_jJ.value(toms::field::g_pszStrength, l_oF.m_fStrength));
    for (const char* l_pszK : {toms::field::g_pszStopMs, toms::field::g_pszShakePx, toms::field::g_pszSounds})
        if (l_jJ.contains(l_pszK) && l_jJ[l_pszK].is_object())
            for (auto l_oIt = l_jJ[l_pszK].begin(); l_oIt != l_jJ[l_pszK].end(); ++l_oIt) {
                if (std::string(l_pszK) == "stopMs" && l_oIt.value().is_number()) l_oF.m_mapStopMs[l_oIt.key()] = l_oIt.value().get<int>();
                if (std::string(l_pszK) == "shakePx" && l_oIt.value().is_number()) l_oF.m_mapShakePx[l_oIt.key()] = l_oIt.value().get<float>();
                if (std::string(l_pszK) == "sounds" && l_oIt.value().is_string()) l_oF.m_mapSounds[l_oIt.key()] = l_oIt.value().get<std::string>();
            }
    l_oF.m_iShakeMs = std::max(0, l_jJ.value(toms::field::g_pszShakeMs, l_oF.m_iShakeMs));
    l_oF.m_iSlowMoMs = std::max(0, l_jJ.value(toms::field::g_pszSlowMoMs, l_oF.m_iSlowMoMs));
    l_oF.m_fSlowMoScale = std::clamp(l_jJ.value(toms::field::g_pszSlowMoScale, l_oF.m_fSlowMoScale), 0.05f, 1.0f);
    l_oF.m_strWinClip = l_jJ.value(toms::field::g_pszWinClip, l_oF.m_strWinClip);
    l_oF.m_strWinBigClip = l_jJ.value(toms::field::g_pszWinBigClip, l_oF.m_strWinBigClip);
    l_oF.m_strWinParClip = l_jJ.value(toms::field::g_pszWinParClip, l_oF.m_strWinParClip);
    l_oF.m_strLoseClip = l_jJ.value(toms::field::g_pszLoseClip, l_oF.m_strLoseClip);
    l_oF.m_fWinParHp = std::clamp(l_jJ.value(toms::field::g_pszWinParHp, l_oF.m_fWinParHp), 0.0f, 1.0f);
    l_oF.m_iEndMaxMs = std::max(0, l_jJ.value(toms::field::g_pszEndMaxMs, l_oF.m_iEndMaxMs));
    l_oF.m_fReducedScale = std::clamp(l_jJ.value(toms::field::g_pszReducedScale, l_oF.m_fReducedScale), 0.0f, 1.0f);
    l_oF.m_fChainStep = std::max(0.0f, l_jJ.value(toms::field::g_pszChainStep, l_oF.m_fChainStep));
    l_oF.m_iChainMax = std::max(1, l_jJ.value(toms::field::g_pszChainMax, l_oF.m_iChainMax));
    return l_oF;
}

GradeSettings f_GradeFromJson(const nlohmann::json& l_jB) {
    GradeSettings l_oG;
    if (!l_jB.is_object() || !l_jB.contains(toms::field::g_pszGrade) || !l_jB[toms::field::g_pszGrade].is_object()) return l_oG;
    const nlohmann::json& l_jJ = l_jB[toms::field::g_pszGrade];
    l_oG.m_fParSeconds = l_jJ.value(toms::field::g_pszParSeconds, l_oG.m_fParSeconds);
    l_oG.m_fTimePenalty = l_jJ.value(toms::field::g_pszTimePenalty, l_oG.m_fTimePenalty);
    l_oG.m_fDamagePenalty = l_jJ.value(toms::field::g_pszDamagePenalty, l_oG.m_fDamagePenalty);
    l_oG.m_fPerfectBonus = l_jJ.value(toms::field::g_pszPerfectBonus, l_oG.m_fPerfectBonus);
    l_oG.m_fChainBonus = l_jJ.value(toms::field::g_pszChainBonus, l_oG.m_fChainBonus);
    l_oG.m_fS = l_jJ.value(toms::field::g_pszS, l_oG.m_fS);
    l_oG.m_fA = l_jJ.value(toms::field::g_pszA, l_oG.m_fA);
    l_oG.m_fB = l_jJ.value(toms::field::g_pszB, l_oG.m_fB);
    l_oG.m_fSGold = std::max(0.0f, l_jJ.value(toms::field::g_pszSGold, l_oG.m_fSGold));
    l_oG.m_fSCard = std::clamp(l_jJ.value(toms::field::g_pszSCard, l_oG.m_fSCard), 0.0f, 1.0f);
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
    if (!l_jB.is_object() || !l_jB.contains(toms::field::g_pszGuard) || !l_jB[toms::field::g_pszGuard].is_object()) return l_oG;
    const nlohmann::json& l_jJ = l_jB[toms::field::g_pszGuard];
    const std::string l_strMode = l_jJ.value(toms::field::g_pszMode, l_oG.m_strMode);
    l_oG.m_strMode = l_strMode == toms::guard_mode::g_pszBar || l_strMode == toms::guard_mode::g_pszWarn ? l_strMode : toms::guard_mode::g_pszTiming;
    l_oG.m_iWarnMs = std::max(100, l_jJ.value(toms::field::g_pszWarnMs, l_oG.m_iWarnMs));
    l_oG.m_fParryShare = std::clamp(l_jJ.value(toms::field::g_pszParryShare, l_oG.m_fParryShare), 0.0f, 1.0f);
    l_oG.m_iMissLockMs = std::max(0, l_jJ.value(toms::field::g_pszMissLockMs, l_oG.m_iMissLockMs));
    l_oG.m_iParryMs = std::max(0, l_jJ.value(toms::field::g_pszParryMs, l_oG.m_iParryMs));
    l_oG.m_iBlockMs = std::max(l_oG.m_iParryMs, l_jJ.value(toms::field::g_pszBlockMs, l_oG.m_iBlockMs));
    l_oG.m_iEarlyMs = std::max(l_oG.m_iBlockMs, l_jJ.value(toms::field::g_pszEarlyMs, l_oG.m_iEarlyMs));
    l_oG.m_fBlockPower = std::clamp(l_jJ.value(toms::field::g_pszBlockPower, l_oG.m_fBlockPower), 0.0f, 100.0f);
    l_oG.m_iCooldownMs = std::max(0, l_jJ.value(toms::field::g_pszCooldownMs, l_oG.m_iCooldownMs));
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
    const nlohmann::json l_jT = l_jBattle.value(toms::field::g_pszTutorial, nlohmann::json::object());
    l_oT.m_strEnemy = l_jT.value(toms::field::g_pszEnemy, std::string());
    l_oT.m_jOverrides = l_jT.value(toms::field::g_pszOverrides, nlohmann::json::object());
    for (const nlohmann::json& l_jS : l_jT.value(toms::field::g_pszSteps, nlohmann::json::array())) {
        TutorialStep l_oS;
        l_oS.m_strText = l_jS.value(toms::field::g_pszText, std::string());
        l_oS.m_strMark = l_jS.value(toms::field::g_pszMark, std::string());
        l_oS.m_strDoneOn = l_jS.value(toms::field::g_pszDoneOn, std::string());
        l_oS.m_strMove = l_jS.value(toms::field::g_pszMove, std::string());
        l_oS.m_iCount = std::max(1, l_jS.value(toms::field::g_pszCount, 1));
        l_oS.m_bHold = l_jS.value(toms::field::g_pszHold, false);
        l_oS.m_bOnce = l_jS.value(toms::field::g_pszOnce, false);
        l_oS.m_iHideMs = std::max(0, l_jS.value(toms::field::g_pszHideMs, 0));
        l_oT.m_vSteps.push_back(l_oS);
    }
    return l_oT;
}

} // namespace toms
