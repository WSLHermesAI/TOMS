// equipment_test.cpp — headless verification of the Equipment System (Milestone 6).
// Exits 0 on success, 1 on any CHECK failure. Run: ./equipment_test
#include "equipment_system.h"
#include <cstdio>
#include <cmath>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

static bool f_Near(float l_fA, float l_fB, float l_fEps = 0.001f) { return std::fabs(l_fA - l_fB) < l_fEps; }

int main() {
    // 1. EquipmentSlot toString/fromString round-trip + fail-safe default.
    for (auto l_eS : {EquipmentSlot::Weapon, EquipmentSlot::Armor, EquipmentSlot::Talent})
        CHECK(f_EquipmentSlotFromString(f_ToString(l_eS)) == l_eS, "EquipmentSlot round-trips toString(s)");
    CHECK(f_EquipmentSlotFromString("bogus") == EquipmentSlot::Weapon, "unrecognized slot defaults to Weapon");

    // 2. EquipmentDefinition JSON round-trip, using War Hammer's real numbers
    //    (docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.3: +8 ATK, 10/140/2.6s, 25/40/75, maxMult 2.6).
    {
        EquipmentDefinition l_oWarHammer;
        l_oWarHammer.m_strId = "war_hammer"; l_oWarHammer.m_jName = "War Hammer"; l_oWarHammer.m_strSprite = "wpn_hammer.png";
        l_oWarHammer.m_jDesc = "Power build."; l_oWarHammer.m_eSlot = EquipmentSlot::Weapon;
        l_oWarHammer.m_iStatAtk = 8; l_oWarHammer.m_iStatDef = 0;
        l_oWarHammer.m_oBar = { 10.0f, 140.0f, 2.6f, 25.0f, 40.0f, 75.0f };
        l_oWarHammer.m_fMaxMult = 2.6f;

        auto l_jJ = f_ToJson(l_oWarHammer);
        EquipmentDefinition l_oD2 = f_EquipmentFromJson(l_jJ);
        CHECK(l_oD2.m_strId == "war_hammer" && l_oD2.m_jName == "War Hammer", "round-trip: id/name");
        CHECK(l_oD2.m_eSlot == EquipmentSlot::Weapon, "round-trip: slot");
        CHECK(l_oD2.m_iStatAtk == 8 && l_oD2.m_iStatDef == 0, "round-trip: stat bonuses");
        CHECK(f_Near(l_oD2.m_oBar.m_fV0, 10.0f) && f_Near(l_oD2.m_oBar.m_fVmax, 140.0f) && f_Near(l_oD2.m_oBar.m_fRampTime, 2.6f),
              "round-trip: bar speed params");
        CHECK(f_Near(l_oD2.m_oBar.m_fGreenHalf, 25.0f) && f_Near(l_oD2.m_oBar.m_fBlueOuter, 40.0f) && f_Near(l_oD2.m_oBar.m_fRedOuter, 75.0f),
              "round-trip: bar zone radii");
        CHECK(f_Near(l_oD2.m_fMaxMult, 2.6f), "round-trip: maxMult");
    }

    // 3. A talent-slot item round-trips its talent id (no bar/stat fields needed).
    {
        EquipmentDefinition l_oGuardianRing;
        l_oGuardianRing.m_strId = "guardian_ring"; l_oGuardianRing.m_jName = "Guardian Ring";
        l_oGuardianRing.m_eSlot = EquipmentSlot::Talent; l_oGuardianRing.m_strTalent = "guardian";
        auto l_jJ = f_ToJson(l_oGuardianRing);
        EquipmentDefinition l_oD2 = f_EquipmentFromJson(l_jJ);
        CHECK(l_oD2.m_eSlot == EquipmentSlot::Talent && l_oD2.m_strTalent == "guardian", "talent item round-trips slot+talent");
    }

    // Build a small equipment registry matching docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.3's example set.
    std::map<std::string, EquipmentDefinition> l_mapDefs;
    {
        EquipmentDefinition l_oWand; l_oWand.m_strId = "wand"; l_oWand.m_iStatAtk = 0; l_oWand.m_oBar = {}; l_oWand.m_fMaxMult = 2.0f;
        l_mapDefs["wand"] = l_oWand;

        EquipmentDefinition l_oDaggers; l_oDaggers.m_strId = "daggers"; l_oDaggers.m_iStatAtk = -2;
        l_oDaggers.m_oBar = { 20.0f, 260.0f, 1.0f, 8.0f, 20.0f, 45.0f }; l_oDaggers.m_fMaxMult = 2.2f;
        l_mapDefs["daggers"] = l_oDaggers;

        EquipmentDefinition l_oHammer; l_oHammer.m_strId = "hammer"; l_oHammer.m_iStatAtk = 8;
        l_oHammer.m_oBar = { 10.0f, 140.0f, 2.6f, 25.0f, 40.0f, 75.0f }; l_oHammer.m_fMaxMult = 2.6f;
        l_mapDefs["hammer"] = l_oHammer;

        EquipmentDefinition l_oPlate; l_oPlate.m_strId = "plate"; l_oPlate.m_iStatDef = 5;
        l_oPlate.m_oBar = { 12.0f, 150.0f, 2.2f, 24.0f, 38.0f, 68.0f };
        l_mapDefs["plate"] = l_oPlate;

        EquipmentDefinition l_oFocus; l_oFocus.m_strId = "focus"; l_oFocus.m_eSlot = EquipmentSlot::Talent; l_oFocus.m_strTalent = "focus";
        l_mapDefs["focus"] = l_oFocus;
        EquipmentDefinition l_oBerserker; l_oBerserker.m_strId = "berserker"; l_oBerserker.m_eSlot = EquipmentSlot::Talent; l_oBerserker.m_strTalent = "berserker";
        l_mapDefs["berserker"] = l_oBerserker;
        EquipmentDefinition l_oGuardian; l_oGuardian.m_strId = "guardian"; l_oGuardian.m_eSlot = EquipmentSlot::Talent; l_oGuardian.m_strTalent = "guardian";
        l_mapDefs["guardian"] = l_oGuardian;
    }

    // 4. applyEquipmentStats: nothing equipped contributes 0; equipping hammer+plate adds
    //    exactly their stat bonuses on top of whatever base stats the caller already computed.
    {
        EquippedSet l_oNone;
        int l_iAtk = 12, l_iDef = 4;
        f_ApplyEquipmentStats(l_oNone, l_mapDefs, l_iAtk, l_iDef);
        CHECK(l_iAtk == 12 && l_iDef == 4, "no equipment equipped contributes 0 to stats");

        EquippedSet l_oHammerAndPlate; l_oHammerAndPlate.m_strWeaponId = "hammer"; l_oHammerAndPlate.m_strArmorId = "plate";
        int l_iAtk2 = 12, l_iDef2 = 4;
        f_ApplyEquipmentStats(l_oHammerAndPlate, l_mapDefs, l_iAtk2, l_iDef2);
        CHECK(l_iAtk2 == 20 && l_iDef2 == 9, "War Hammer (+8 atk) + Guardian Plate (+5 def) matches the worked example in docs/design/MAIN_BATTLE_SCENE_DESIGN.md §5");
    }

    // 5. An unknown/unregistered equipped id is silently ignored (fails closed, doesn't crash).
    {
        EquippedSet l_oBogus; l_oBogus.m_strWeaponId = "does_not_exist";
        int l_iAtk = 12, l_iDef = 4;
        f_ApplyEquipmentStats(l_oBogus, l_mapDefs, l_iAtk, l_iDef);
        CHECK(l_iAtk == 12 && l_iDef == 4, "an unregistered equipped id contributes nothing, doesn't crash");
    }

    // 6. effectiveAttackBar/effectiveDefenseBar: baseline geometry with nothing equipped,
    //    the weapon/armor's own geometry when equipped, and the Focus talent's rampTime stretch.
    {
        EquippedSet l_oNone;
        PowerBarParams l_oBar = f_EffectiveAttackBar(l_oNone, l_mapDefs);
        CHECK(f_Near(l_oBar.m_fRampTime, 1.8f) && f_Near(l_oBar.m_fRedOuter, 60.0f), "no weapon equipped -> baseline Attack Bar geometry");

        EquippedSet l_oWithDaggers; l_oWithDaggers.m_strWeaponId = "daggers";
        PowerBarParams l_oDaggerBar = f_EffectiveAttackBar(l_oWithDaggers, l_mapDefs);
        CHECK(f_Near(l_oDaggerBar.m_fRampTime, 1.0f) && f_Near(l_oDaggerBar.m_fRedOuter, 45.0f) && f_Near(l_oDaggerBar.m_fGreenHalf, 8.0f),
              "Twin Daggers equipped -> its own fast/narrow Attack Bar geometry");

        EquippedSet l_oWithDaggersAndFocus; l_oWithDaggersAndFocus.m_strWeaponId = "daggers"; l_oWithDaggersAndFocus.m_strTalentId = "focus";
        PowerBarParams l_oFocusedBar = f_EffectiveAttackBar(l_oWithDaggersAndFocus, l_mapDefs);
        CHECK(f_Near(l_oFocusedBar.m_fRampTime, 1.25f), "Focus talent stretches rampTime by 25% on top of the weapon's own value (1.0 * 1.25)");
        CHECK(f_Near(l_oFocusedBar.m_fRedOuter, 45.0f), "Focus talent does not affect zone geometry, only rampTime");

        EquippedSet l_oWithPlate; l_oWithPlate.m_strArmorId = "plate";
        PowerBarParams l_oPlateBar = f_EffectiveDefenseBar(l_oWithPlate, l_mapDefs);
        CHECK(f_Near(l_oPlateBar.m_fRampTime, 2.2f) && f_Near(l_oPlateBar.m_fGreenHalf, 24.0f), "Guardian Plate equipped -> its own Defense Bar geometry");
    }

    // 7. effectiveMaxMult: baseline 2.0 with nothing equipped, the weapon's own ceiling when
    //    equipped, and Berserker's +15% ceiling bonus stacking on top.
    {
        EquippedSet l_oNone;
        CHECK(f_Near(f_EffectiveMaxMult(l_oNone, l_mapDefs), 2.0f), "no weapon equipped -> baseline 2.0x ceiling");

        EquippedSet l_oWithHammer; l_oWithHammer.m_strWeaponId = "hammer";
        CHECK(f_Near(f_EffectiveMaxMult(l_oWithHammer, l_mapDefs), 2.6f), "War Hammer equipped -> its own 2.6x ceiling");

        EquippedSet l_oWithDaggersAndBerserker; l_oWithDaggersAndBerserker.m_strWeaponId = "daggers"; l_oWithDaggersAndBerserker.m_strTalentId = "berserker";
        CHECK(f_Near(f_EffectiveMaxMult(l_oWithDaggersAndBerserker, l_mapDefs), 2.35f), "Berserker's +15% stacks on top of Twin Daggers' own 2.2x ceiling");
    }

    // 8. Talent predicate functions: recognized values, and a safe no-op for unrecognized/absent.
    CHECK(f_Near(f_TalentRampTimeMultiplier("focus"), 1.25f), "talentRampTimeMultiplier(focus) == 1.25");
    CHECK(f_Near(f_TalentRampTimeMultiplier("berserker"), 1.0f), "talentRampTimeMultiplier is a no-op for a talent that doesn't affect it");
    CHECK(f_Near(f_TalentRampTimeMultiplier(""), 1.0f), "talentRampTimeMultiplier(\"\") (no talent equipped) is a no-op");
    CHECK(f_Near(f_TalentMaxMultBonus("berserker"), 0.15f), "talentMaxMultBonus(berserker) == 0.15");
    CHECK(f_Near(f_TalentMaxMultBonus("focus"), 0.0f), "talentMaxMultBonus is 0 for a talent that doesn't affect it");
    CHECK(f_TalentZerosRedZoneAttacks("berserker"), "Berserker zeros Red-zone attacks");
    CHECK(!f_TalentZerosRedZoneAttacks("guardian"), "Guardian does not zero Red-zone attacks");
    CHECK(f_Near(f_TalentDefenseMitigationFloor("guardian"), 0.10f), "Guardian's mitigation floor is 0.10 (10%)");
    CHECK(f_Near(f_TalentDefenseMitigationFloor(""), 0.0f), "no talent equipped -> no mitigation floor");

    // 9. S7: equippedActives() -- unions whatever the currently equipped weapon/armor/talent grant.
    {
        EquipmentDefinition l_oDaggersWithActive = l_mapDefs["daggers"];
        l_oDaggersWithActive.m_vActives = {"a_twin_flash"};
        std::map<std::string, EquipmentDefinition> l_mapDefsWithActive = l_mapDefs;
        l_mapDefsWithActive["daggers"] = l_oDaggersWithActive;

        EquippedSet l_oNone;
        CHECK(f_EquippedActives(l_oNone, l_mapDefsWithActive).empty(), "nothing equipped -> no actives");

        EquippedSet l_oWithDaggers; l_oWithDaggers.m_strWeaponId = "daggers";
        auto l_vActs = f_EquippedActives(l_oWithDaggers, l_mapDefsWithActive);
        CHECK(l_vActs.size() == 1 && l_vActs[0] == "a_twin_flash", "equipping the active-bearing weapon grants it");

        EquippedSet l_oWithPlainWand; l_oWithPlainWand.m_strWeaponId = "wand";
        CHECK(f_EquippedActives(l_oWithPlainWand, l_mapDefsWithActive).empty(), "a weapon with no actives field grants none");

        CHECK(f_EquippedActives(l_oWithDaggers, l_mapDefs).empty(), "the SAME weapon id in the original (no-actives) registry grants none -- actives live on the definition, not the id");
    }

    if (g_iFail == 0) { printf("equipment_test: ALL PASS (34 checks)\n"); return 0; }
    printf("equipment_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}
