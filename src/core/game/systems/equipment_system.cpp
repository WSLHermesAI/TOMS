#include "equipment_system.h"

namespace toms {

const char* f_ToString(EquipmentSlot l_eS) {
    switch (l_eS) {
        case EquipmentSlot::Weapon: return "weapon";
        case EquipmentSlot::Armor:  return "armor";
        case EquipmentSlot::Talent: return "talent";
    }
    return "weapon";
}

EquipmentSlot f_EquipmentSlotFromString(const std::string& l_strS) {
    if (l_strS == "armor")  return EquipmentSlot::Armor;
    if (l_strS == "talent") return EquipmentSlot::Talent;
    return EquipmentSlot::Weapon;
}

nlohmann::json f_ToJson(const EquipmentDefinition& l_oD) {
    nlohmann::json l_jJ;
    l_jJ["id"] = l_oD.m_strId; l_jJ["name"] = l_oD.m_jName; l_jJ["sprite"] = l_oD.m_strSprite; l_jJ["desc"] = l_oD.m_jDesc;
    l_jJ["slot"] = f_ToString(l_oD.m_eSlot);
    l_jJ["stat"] = { {"atk", l_oD.m_iStatAtk}, {"def", l_oD.m_iStatDef} };
    l_jJ["bar"] = {
        {"v0", l_oD.m_oBar.m_fV0}, {"vmax", l_oD.m_oBar.m_fVmax}, {"rampTime", l_oD.m_oBar.m_fRampTime},
        {"greenHalf", l_oD.m_oBar.m_fGreenHalf}, {"blueOuter", l_oD.m_oBar.m_fBlueOuter}, {"redOuter", l_oD.m_oBar.m_fRedOuter},
        {"maxMult", l_oD.m_fMaxMult}
    };
    l_jJ["talent"] = l_oD.m_strTalent;
    l_jJ["actives"] = l_oD.m_vActives;
    if (!l_oD.m_strBattleCharacterAnim.empty() || !l_oD.m_strBattleHeldAnim.empty()) {
        l_jJ["battleAnim"] = {{"character", l_oD.m_strBattleCharacterAnim}, {"held", l_oD.m_strBattleHeldAnim}};
        if (!l_oD.m_mapBattleClips.empty()) l_jJ["battleAnim"]["clips"] = l_oD.m_mapBattleClips;
        if (l_oD.m_strBattleRender != "2d") l_jJ["battleAnim"]["render"] = l_oD.m_strBattleRender;
        if (!l_oD.m_strBattleModel.empty()) l_jJ["battleAnim"]["model"] = l_oD.m_strBattleModel;
    }
    return l_jJ;
}

EquipmentDefinition f_EquipmentFromJson(const nlohmann::json& l_jJ) {
    EquipmentDefinition l_oD;
    if (!l_jJ.is_object()) return l_oD;
    l_oD.m_strId = l_jJ.value("id", std::string());
    l_oD.m_jName = l_jJ.contains("name") ? l_jJ["name"] : nlohmann::json(l_oD.m_strId);
    l_oD.m_strSprite = l_jJ.value("sprite", std::string());
    l_oD.m_jDesc = l_jJ.contains("desc") ? l_jJ["desc"] : nlohmann::json("");
    // data/items.json (the all-item file) names the slot in "type" and the bonuses in "stats"; the
    // older equipment.json shape ("slot", "stat") still reads the same.
    l_oD.m_eSlot = f_EquipmentSlotFromString(l_jJ.value("slot", l_jJ.value("type", std::string("weapon"))));
    const char* l_pszStatKey = l_jJ.contains("stats") ? "stats" : "stat";
    if (l_jJ.contains(l_pszStatKey) && l_jJ[l_pszStatKey].is_object()) {
        l_oD.m_iStatAtk = l_jJ[l_pszStatKey].value("atk", 0);
        l_oD.m_iStatDef = l_jJ[l_pszStatKey].value("def", 0);
        for (auto l_oIt = l_jJ[l_pszStatKey].begin(); l_oIt != l_jJ[l_pszStatKey].end(); ++l_oIt)
            if (l_oIt.value().is_number() && l_oIt.key() != "atk" && l_oIt.key() != "def") l_oD.m_mapStats[l_oIt.key()] = l_oIt.value().get<int>();
    }
    if (l_jJ.contains("bar") && l_jJ["bar"].is_object()) {
        auto& l_jB = l_jJ["bar"];
        l_oD.m_oBar.m_fV0 = l_jB.value("v0", l_oD.m_oBar.m_fV0);
        l_oD.m_oBar.m_fVmax = l_jB.value("vmax", l_oD.m_oBar.m_fVmax);
        l_oD.m_oBar.m_fRampTime = l_jB.value("rampTime", l_oD.m_oBar.m_fRampTime);
        l_oD.m_oBar.m_fGreenHalf = l_jB.value("greenHalf", l_oD.m_oBar.m_fGreenHalf);
        l_oD.m_oBar.m_fBlueOuter = l_jB.value("blueOuter", l_oD.m_oBar.m_fBlueOuter);
        l_oD.m_oBar.m_fRedOuter = l_jB.value("redOuter", l_oD.m_oBar.m_fRedOuter);
        l_oD.m_fMaxMult = l_jB.value("maxMult", l_oD.m_fMaxMult);
    }
    l_oD.m_strTalent = l_jJ.value("talent", std::string());
    l_oD.m_vActives = l_jJ.value("actives", std::vector<std::string>{});
    if (l_jJ.contains("battleAnim") && l_jJ["battleAnim"].is_object()) {
        l_oD.m_strBattleCharacterAnim = l_jJ["battleAnim"].value("character", std::string());
        l_oD.m_strBattleHeldAnim = l_jJ["battleAnim"].value("held", std::string());
        l_oD.m_strBattleRender = l_jJ["battleAnim"].value("render", std::string("2d"));
        l_oD.m_strBattleModel = l_jJ["battleAnim"].value("model", std::string());
        const auto& l_jClips = l_jJ["battleAnim"].value("clips", nlohmann::json::object());
        if (l_jClips.is_object())
            for (auto l_oIt = l_jClips.begin(); l_oIt != l_jClips.end(); ++l_oIt)
                if (l_oIt.value().is_string()) l_oD.m_mapBattleClips[l_oIt.key()] = l_oIt.value().get<std::string>();
    }
    return l_oD;
}

static const EquipmentDefinition* f_FindEquipped(const std::string& l_strId,
                                                const std::map<std::string, EquipmentDefinition>& l_mapDefs) {
    if (l_strId.empty()) return nullptr;
    auto l_itIt = l_mapDefs.find(l_strId);
    return l_itIt == l_mapDefs.end() ? nullptr : &l_itIt->second;
}

void f_ApplyEquipmentStats(const EquippedSet& l_oEq, const std::map<std::string, EquipmentDefinition>& l_mapDefs,
                          int& l_iAtk, int& l_iDef) {
    if (const auto* l_pW = f_FindEquipped(l_oEq.m_strWeaponId, l_mapDefs)) l_iAtk += l_pW->m_iStatAtk;
    if (const auto* l_pA = f_FindEquipped(l_oEq.m_strArmorId, l_mapDefs))  l_iDef += l_pA->m_iStatDef;
}

std::vector<std::string> f_EquippedActives(const EquippedSet& l_oEq, const std::map<std::string, EquipmentDefinition>& l_mapDefs) {
    std::vector<std::string> l_vOut;
    for (const std::string& l_strId : {l_oEq.m_strWeaponId, l_oEq.m_strArmorId, l_oEq.m_strTalentId}) {
        if (const auto* l_pD = f_FindEquipped(l_strId, l_mapDefs))
            l_vOut.insert(l_vOut.end(), l_pD->m_vActives.begin(), l_pD->m_vActives.end());
    }
    return l_vOut;
}

PowerBarParams f_EffectiveAttackBar(const EquippedSet& l_oEq, const std::map<std::string, EquipmentDefinition>& l_mapDefs) {
    PowerBarParams l_oBar;   // baseline geometry if nothing equipped
    if (const auto* l_pW = f_FindEquipped(l_oEq.m_strWeaponId, l_mapDefs)) l_oBar = l_pW->m_oBar;
    l_oBar.m_fRampTime *= f_TalentRampTimeMultiplier(l_oEq.m_strTalentId);
    return l_oBar;
}

PowerBarParams f_EffectiveDefenseBar(const EquippedSet& l_oEq, const std::map<std::string, EquipmentDefinition>& l_mapDefs) {
    PowerBarParams l_oBar;
    if (const auto* l_pA = f_FindEquipped(l_oEq.m_strArmorId, l_mapDefs)) l_oBar = l_pA->m_oBar;
    l_oBar.m_fRampTime *= f_TalentRampTimeMultiplier(l_oEq.m_strTalentId);
    return l_oBar;
}

float f_EffectiveMaxMult(const EquippedSet& l_oEq, const std::map<std::string, EquipmentDefinition>& l_mapDefs) {
    float l_fMaxMult = 2.0f;
    if (const auto* l_pW = f_FindEquipped(l_oEq.m_strWeaponId, l_mapDefs)) l_fMaxMult = l_pW->m_fMaxMult;
    return l_fMaxMult + f_TalentMaxMultBonus(l_oEq.m_strTalentId);
}

float f_TalentRampTimeMultiplier(const std::string& l_strTalentId) {
    if (l_strTalentId == "focus") return 1.25f;
    return 1.0f;
}

float f_TalentMaxMultBonus(const std::string& l_strTalentId) {
    if (l_strTalentId == "berserker") return 0.15f;
    return 0.0f;
}

bool f_TalentZerosRedZoneAttacks(const std::string& l_strTalentId) {
    return l_strTalentId == "berserker";
}

float f_TalentDefenseMitigationFloor(const std::string& l_strTalentId) {
    if (l_strTalentId == "guardian") return 0.10f;
    return 0.0f;
}

} // namespace toms
