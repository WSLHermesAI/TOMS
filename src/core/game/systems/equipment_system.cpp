#include "field_keys.h"    // toms::field: every data field by its constant
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
    l_jJ[toms::field::g_pszId] = l_oD.m_strId; l_jJ[toms::field::g_pszName] = l_oD.m_jName; l_jJ[toms::field::g_pszSprite] = l_oD.m_strSprite; l_jJ[toms::field::g_pszDesc] = l_oD.m_jDesc;
    l_jJ[toms::field::g_pszSlot] = f_ToString(l_oD.m_eSlot);
    l_jJ[toms::field::g_pszStat] = { {toms::field::g_pszAtk, l_oD.m_iStatAtk}, {toms::field::g_pszDef, l_oD.m_iStatDef} };
    l_jJ[toms::field::g_pszBar] = {
        {toms::field::g_pszV0, l_oD.m_oBar.m_fV0}, {toms::field::g_pszVmax, l_oD.m_oBar.m_fVmax}, {toms::field::g_pszRampTime, l_oD.m_oBar.m_fRampTime},
        {toms::field::g_pszGreenHalf, l_oD.m_oBar.m_fGreenHalf}, {toms::field::g_pszBlueOuter, l_oD.m_oBar.m_fBlueOuter}, {toms::field::g_pszRedOuter, l_oD.m_oBar.m_fRedOuter},
        {toms::field::g_pszMaxMult, l_oD.m_fMaxMult}
    };
    l_jJ[toms::field::g_pszTalent] = l_oD.m_strTalent;
    l_jJ[toms::field::g_pszActives] = l_oD.m_vActives;
    if (!l_oD.m_strBattleCharacterAnim.empty() || !l_oD.m_strBattleHeldAnim.empty()) {
        l_jJ[toms::field::g_pszBattleAnim] = {{toms::field::g_pszCharacter, l_oD.m_strBattleCharacterAnim}, {toms::field::g_pszHeld, l_oD.m_strBattleHeldAnim}};
        if (!l_oD.m_mapBattleClips.empty()) l_jJ[toms::field::g_pszBattleAnim][toms::field::g_pszClips] = l_oD.m_mapBattleClips;
        if (l_oD.m_strBattleRender != "2d") l_jJ[toms::field::g_pszBattleAnim][toms::field::g_pszRender] = l_oD.m_strBattleRender;
        if (!l_oD.m_strBattleModel.empty()) l_jJ[toms::field::g_pszBattleAnim][toms::field::g_pszModel] = l_oD.m_strBattleModel;
    }
    return l_jJ;
}

EquipmentDefinition f_EquipmentFromJson(const nlohmann::json& l_jJ) {
    EquipmentDefinition l_oD;
    if (!l_jJ.is_object()) return l_oD;
    l_oD.m_strId = l_jJ.value(toms::field::g_pszId, std::string());
    l_oD.m_jName = l_jJ.contains(toms::field::g_pszName) ? l_jJ[toms::field::g_pszName] : nlohmann::json(l_oD.m_strId);
    l_oD.m_strSprite = l_jJ.value(toms::field::g_pszSprite, std::string());
    l_oD.m_jDesc = l_jJ.contains(toms::field::g_pszDesc) ? l_jJ[toms::field::g_pszDesc] : nlohmann::json("");
    // data/items.json (the all-item file) names the slot in "type" and the bonuses in "stats"; the
    // older equipment.json shape ("slot", "stat") still reads the same.
    l_oD.m_eSlot = f_EquipmentSlotFromString(l_jJ.value(toms::field::g_pszSlot, l_jJ.value(toms::field::g_pszType, std::string("weapon"))));
    const char* l_pszStatKey = l_jJ.contains(toms::field::g_pszStats) ? toms::field::g_pszStats : toms::field::g_pszStat;
    if (l_jJ.contains(l_pszStatKey) && l_jJ[l_pszStatKey].is_object()) {
        l_oD.m_iStatAtk = l_jJ[l_pszStatKey].value(toms::field::g_pszAtk, 0);
        l_oD.m_iStatDef = l_jJ[l_pszStatKey].value(toms::field::g_pszDef, 0);
        for (auto l_oIt = l_jJ[l_pszStatKey].begin(); l_oIt != l_jJ[l_pszStatKey].end(); ++l_oIt)
            if (l_oIt.value().is_number() && l_oIt.key() != "atk" && l_oIt.key() != "def") l_oD.m_mapStats[l_oIt.key()] = l_oIt.value().get<int>();
    }
    if (l_jJ.contains(toms::field::g_pszBar) && l_jJ[toms::field::g_pszBar].is_object()) {
        auto& l_jB = l_jJ[toms::field::g_pszBar];
        l_oD.m_oBar.m_fV0 = l_jB.value(toms::field::g_pszV0, l_oD.m_oBar.m_fV0);
        l_oD.m_oBar.m_fVmax = l_jB.value(toms::field::g_pszVmax, l_oD.m_oBar.m_fVmax);
        l_oD.m_oBar.m_fRampTime = l_jB.value(toms::field::g_pszRampTime, l_oD.m_oBar.m_fRampTime);
        l_oD.m_oBar.m_fGreenHalf = l_jB.value(toms::field::g_pszGreenHalf, l_oD.m_oBar.m_fGreenHalf);
        l_oD.m_oBar.m_fBlueOuter = l_jB.value(toms::field::g_pszBlueOuter, l_oD.m_oBar.m_fBlueOuter);
        l_oD.m_oBar.m_fRedOuter = l_jB.value(toms::field::g_pszRedOuter, l_oD.m_oBar.m_fRedOuter);
        l_oD.m_fMaxMult = l_jB.value(toms::field::g_pszMaxMult, l_oD.m_fMaxMult);
    }
    l_oD.m_strTalent = l_jJ.value(toms::field::g_pszTalent, std::string());
    l_oD.m_vActives = l_jJ.value(toms::field::g_pszActives, std::vector<std::string>{});
    if (l_jJ.contains(toms::field::g_pszBattleAnim) && l_jJ[toms::field::g_pszBattleAnim].is_object()) {
        l_oD.m_strBattleCharacterAnim = l_jJ[toms::field::g_pszBattleAnim].value(toms::field::g_pszCharacter, std::string());
        l_oD.m_strBattleHeldAnim = l_jJ[toms::field::g_pszBattleAnim].value(toms::field::g_pszHeld, std::string());
        l_oD.m_strBattleRender = l_jJ[toms::field::g_pszBattleAnim].value(toms::field::g_pszRender, std::string("2d"));
        l_oD.m_strBattleModel = l_jJ[toms::field::g_pszBattleAnim].value(toms::field::g_pszModel, std::string());
        const auto& l_jClips = l_jJ[toms::field::g_pszBattleAnim].value(toms::field::g_pszClips, nlohmann::json::object());
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
