#include "equipment_actives.h"

#include <algorithm>

#include "vfs.h"

#include "json.hpp"

namespace toms {

std::map<std::string, ActiveDefinition> f_LoadActiveDefinitions(const std::string& l_strPath) {
    std::map<std::string, ActiveDefinition> l_mapOut;
    std::string l_strText;
    if (!f_VfsReadAll(l_strPath, l_strText)) return l_mapOut;       // absent table = no actives, not a crash
    try {
        nlohmann::json l_jJ = nlohmann::json::parse(l_strText);
        if (!l_jJ.is_object()) return l_mapOut;
        for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
            const nlohmann::json& l_jV = l_oIt.value();
            ActiveDefinition l_oD;
            l_oD.m_strId = l_jV.value("id", l_oIt.key());
            l_oD.m_strKind = l_jV.value("kind", std::string());
            l_oD.m_fDamageMultiplier = l_jV.value("damageMultiplier", 1.0f);
            l_oD.m_iUsesPerBattle = l_jV.value("usesPerBattle", 1);
            l_oD.m_fCooldownSeconds = l_jV.value("cooldownSeconds", 0.0f);
            l_oD.m_iVfxFrames = l_jV.value("vfxFrames", 0);
            l_mapOut[l_oD.m_strId] = l_oD;
        }
    } catch (const std::exception&) {
        return {};                                  // malformed table behaves like an absent one
    }
    return l_mapOut;
}

std::map<std::string, std::vector<std::string>> f_LoadEquipmentActives(const std::string& l_strPath) {
    std::map<std::string, std::vector<std::string>> l_mapOut;
    std::string l_strText;
    if (!f_VfsReadAll(l_strPath, l_strText)) return l_mapOut;
    try {
        nlohmann::json l_jJ = nlohmann::json::parse(l_strText);
        if (!l_jJ.is_object()) return l_mapOut;
        for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
            std::vector<std::string> l_vIds;
            if (l_oIt.value().contains("actives") && l_oIt.value()["actives"].is_array())
                for (const auto& l_jA : l_oIt.value()["actives"])
                    if (l_jA.is_string()) l_vIds.push_back(l_jA.get<std::string>());
            l_mapOut[l_oIt.key()] = l_vIds;
        }
    } catch (const std::exception&) {
        return {};
    }
    return l_mapOut;
}

std::vector<std::string> f_ActivesForSet(const std::string& l_strWeaponId,
                                      const std::string& l_strArmorId,
                                      const std::string& l_strTalentId,
                                      const std::map<std::string, std::vector<std::string>>& l_mapByItem) {
    std::vector<std::string> l_vOut;
    const std::string l_arrSlots[3] = { l_strWeaponId, l_strArmorId, l_strTalentId };
    for (const std::string& l_strItemId : l_arrSlots) {
        if (l_strItemId.empty()) continue;
        auto l_itIt = l_mapByItem.find(l_strItemId);
        if (l_itIt == l_mapByItem.end()) continue;
        for (const std::string& l_strActiveId : l_itIt->second)
            if (std::find(l_vOut.begin(), l_vOut.end(), l_strActiveId) == l_vOut.end()) l_vOut.push_back(l_strActiveId);
    }
    return l_vOut;
}

std::map<std::string, ActiveRuntime> f_MakeBattleRuntime(const std::map<std::string, ActiveDefinition>& l_mapDefs) {
    std::map<std::string, ActiveRuntime> l_mapRt;
    for (const auto& l_pairKv : l_mapDefs) {
        ActiveRuntime l_oR;
        l_oR.m_iUsesLeft = l_pairKv.second.m_iUsesPerBattle;
        l_oR.m_fCooldownLeft = 0.0f;
        l_mapRt[l_pairKv.first] = l_oR;
    }
    return l_mapRt;
}

int f_TickActives(std::map<std::string, ActiveRuntime>& l_mapRt, float l_fDt) {
    if (l_fDt <= 0.0f) return 0;
    int l_iReady = 0;
    for (auto& l_pairKv : l_mapRt) {
        ActiveRuntime& l_oR = l_pairKv.second;
        if (l_oR.m_fCooldownLeft <= 0.0f) continue;
        l_oR.m_fCooldownLeft -= l_fDt;
        if (l_oR.m_fCooldownLeft <= 0.0f) { l_oR.m_fCooldownLeft = 0.0f; ++l_iReady; }
    }
    return l_iReady;
}

bool f_CanUseActive(const std::string& l_strId,
                  const std::map<std::string, ActiveDefinition>& l_mapDefs,
                  const std::map<std::string, ActiveRuntime>& l_mapRt,
                  const ActiveStatusEffects& l_oSt) {
    if (l_oSt.m_bSilence) return false;                       // st_silence blocks every active
    if (l_mapDefs.find(l_strId) == l_mapDefs.end()) return false;      // not a real active
    auto l_itIt = l_mapRt.find(l_strId);
    if (l_itIt == l_mapRt.end()) return false;                   // not granted this battle
    if (l_itIt->second.m_iUsesLeft <= 0) return false;         // once-per-battle spent
    if (l_itIt->second.m_fCooldownLeft > 0.0f) return false;   // still cooling
    return true;
}

ActiveEffect f_UseActive(const std::string& l_strId,
                       const std::map<std::string, ActiveDefinition>& l_mapDefs,
                       std::map<std::string, ActiveRuntime>& l_mapRt,
                       ActiveStatusEffects& l_oSt) {
    ActiveEffect l_oEff;
    if (!f_CanUseActive(l_strId, l_mapDefs, l_mapRt, l_oSt)) return l_oEff;

    const ActiveDefinition& l_oD = l_mapDefs.at(l_strId);
    ActiveRuntime& l_oR = l_mapRt.at(l_strId);
    l_oR.m_iUsesLeft -= 1;
    // st_echo: "your next active does NOT go on cooldown" -- consumed by this use.
    if (l_oSt.m_bEchoQueued) l_oSt.m_bEchoQueued = false;
    else if (l_oD.m_fCooldownSeconds > 0.0f) l_oR.m_fCooldownLeft = l_oD.m_fCooldownSeconds;

    l_oEff.m_bFired = true;
    l_oEff.m_strId = l_strId;
    l_oEff.m_fDamageMultiplier = l_oD.m_fDamageMultiplier;
    l_oEff.m_bGuaranteedCrit = (l_oD.m_strKind == "guaranteedCrit");
    l_oEff.m_bSurviveLethal = (l_oD.m_strKind == "surviveLethal");
    return l_oEff;
}

}  // namespace toms
