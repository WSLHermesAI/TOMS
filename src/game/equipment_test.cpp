// equipment_test.cpp — headless verification of the Equipment System (Milestone 6).
// Exits 0 on success, 1 on any CHECK failure. Run: ./equipment_test
#include "equipment_system.h"
#include <cstdio>
#include <cmath>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

static bool near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) < eps; }

int main() {
    // 1. EquipmentSlot toString/fromString round-trip + fail-safe default.
    for (auto s : {EquipmentSlot::Weapon, EquipmentSlot::Armor, EquipmentSlot::Talent})
        CHECK(equipmentSlotFromString(toString(s)) == s, "EquipmentSlot round-trips toString(s)");
    CHECK(equipmentSlotFromString("bogus") == EquipmentSlot::Weapon, "unrecognized slot defaults to Weapon");

    // 2. EquipmentDefinition JSON round-trip, using War Hammer's real numbers
    //    (MAIN_BATTLE_SCENE_DESIGN.md §4.3: +8 ATK, 10/140/2.6s, 25/40/75, maxMult 2.6).
    {
        EquipmentDefinition warHammer;
        warHammer.id = "war_hammer"; warHammer.name = "War Hammer"; warHammer.sprite = "wpn_hammer.png";
        warHammer.desc = "Power build."; warHammer.slot = EquipmentSlot::Weapon;
        warHammer.statAtk = 8; warHammer.statDef = 0;
        warHammer.bar = { 10.0f, 140.0f, 2.6f, 25.0f, 40.0f, 75.0f };
        warHammer.maxMult = 2.6f;

        auto j = toJson(warHammer);
        EquipmentDefinition d2 = equipmentFromJson(j);
        CHECK(d2.id == "war_hammer" && d2.name == "War Hammer", "round-trip: id/name");
        CHECK(d2.slot == EquipmentSlot::Weapon, "round-trip: slot");
        CHECK(d2.statAtk == 8 && d2.statDef == 0, "round-trip: stat bonuses");
        CHECK(near(d2.bar.v0, 10.0f) && near(d2.bar.vmax, 140.0f) && near(d2.bar.rampTime, 2.6f),
              "round-trip: bar speed params");
        CHECK(near(d2.bar.greenHalf, 25.0f) && near(d2.bar.blueOuter, 40.0f) && near(d2.bar.redOuter, 75.0f),
              "round-trip: bar zone radii");
        CHECK(near(d2.maxMult, 2.6f), "round-trip: maxMult");
    }

    // 3. A talent-slot item round-trips its talent id (no bar/stat fields needed).
    {
        EquipmentDefinition guardianRing;
        guardianRing.id = "guardian_ring"; guardianRing.name = "Guardian Ring";
        guardianRing.slot = EquipmentSlot::Talent; guardianRing.talent = "guardian";
        auto j = toJson(guardianRing);
        EquipmentDefinition d2 = equipmentFromJson(j);
        CHECK(d2.slot == EquipmentSlot::Talent && d2.talent == "guardian", "talent item round-trips slot+talent");
    }

    // Build a small equipment registry matching MAIN_BATTLE_SCENE_DESIGN.md §4.3's example set.
    std::map<std::string, EquipmentDefinition> defs;
    {
        EquipmentDefinition wand; wand.id = "wand"; wand.statAtk = 0; wand.bar = {}; wand.maxMult = 2.0f;
        defs["wand"] = wand;

        EquipmentDefinition daggers; daggers.id = "daggers"; daggers.statAtk = -2;
        daggers.bar = { 20.0f, 260.0f, 1.0f, 8.0f, 20.0f, 45.0f }; daggers.maxMult = 2.2f;
        defs["daggers"] = daggers;

        EquipmentDefinition hammer; hammer.id = "hammer"; hammer.statAtk = 8;
        hammer.bar = { 10.0f, 140.0f, 2.6f, 25.0f, 40.0f, 75.0f }; hammer.maxMult = 2.6f;
        defs["hammer"] = hammer;

        EquipmentDefinition plate; plate.id = "plate"; plate.statDef = 5;
        plate.bar = { 12.0f, 150.0f, 2.2f, 24.0f, 38.0f, 68.0f };
        defs["plate"] = plate;

        EquipmentDefinition focus; focus.id = "focus"; focus.slot = EquipmentSlot::Talent; focus.talent = "focus";
        defs["focus"] = focus;
        EquipmentDefinition berserker; berserker.id = "berserker"; berserker.slot = EquipmentSlot::Talent; berserker.talent = "berserker";
        defs["berserker"] = berserker;
        EquipmentDefinition guardian; guardian.id = "guardian"; guardian.slot = EquipmentSlot::Talent; guardian.talent = "guardian";
        defs["guardian"] = guardian;
    }

    // 4. applyEquipmentStats: nothing equipped contributes 0; equipping hammer+plate adds
    //    exactly their stat bonuses on top of whatever base stats the caller already computed.
    {
        EquippedSet none;
        int atk = 12, def = 4;
        applyEquipmentStats(none, defs, atk, def);
        CHECK(atk == 12 && def == 4, "no equipment equipped contributes 0 to stats");

        EquippedSet hammerAndPlate; hammerAndPlate.weaponId = "hammer"; hammerAndPlate.armorId = "plate";
        int atk2 = 12, def2 = 4;
        applyEquipmentStats(hammerAndPlate, defs, atk2, def2);
        CHECK(atk2 == 20 && def2 == 9, "War Hammer (+8 atk) + Guardian Plate (+5 def) matches the worked example in MAIN_BATTLE_SCENE_DESIGN.md §5");
    }

    // 5. An unknown/unregistered equipped id is silently ignored (fails closed, doesn't crash).
    {
        EquippedSet bogus; bogus.weaponId = "does_not_exist";
        int atk = 12, def = 4;
        applyEquipmentStats(bogus, defs, atk, def);
        CHECK(atk == 12 && def == 4, "an unregistered equipped id contributes nothing, doesn't crash");
    }

    // 6. effectiveAttackBar/effectiveDefenseBar: baseline geometry with nothing equipped,
    //    the weapon/armor's own geometry when equipped, and the Focus talent's rampTime stretch.
    {
        EquippedSet none;
        PowerBarParams bar = effectiveAttackBar(none, defs);
        CHECK(near(bar.rampTime, 1.8f) && near(bar.redOuter, 60.0f), "no weapon equipped -> baseline Attack Bar geometry");

        EquippedSet withDaggers; withDaggers.weaponId = "daggers";
        PowerBarParams daggerBar = effectiveAttackBar(withDaggers, defs);
        CHECK(near(daggerBar.rampTime, 1.0f) && near(daggerBar.redOuter, 45.0f) && near(daggerBar.greenHalf, 8.0f),
              "Twin Daggers equipped -> its own fast/narrow Attack Bar geometry");

        EquippedSet withDaggersAndFocus; withDaggersAndFocus.weaponId = "daggers"; withDaggersAndFocus.talentId = "focus";
        PowerBarParams focusedBar = effectiveAttackBar(withDaggersAndFocus, defs);
        CHECK(near(focusedBar.rampTime, 1.25f), "Focus talent stretches rampTime by 25% on top of the weapon's own value (1.0 * 1.25)");
        CHECK(near(focusedBar.redOuter, 45.0f), "Focus talent does not affect zone geometry, only rampTime");

        EquippedSet withPlate; withPlate.armorId = "plate";
        PowerBarParams plateBar = effectiveDefenseBar(withPlate, defs);
        CHECK(near(plateBar.rampTime, 2.2f) && near(plateBar.greenHalf, 24.0f), "Guardian Plate equipped -> its own Defense Bar geometry");
    }

    // 7. effectiveMaxMult: baseline 2.0 with nothing equipped, the weapon's own ceiling when
    //    equipped, and Berserker's +15% ceiling bonus stacking on top.
    {
        EquippedSet none;
        CHECK(near(effectiveMaxMult(none, defs), 2.0f), "no weapon equipped -> baseline 2.0x ceiling");

        EquippedSet withHammer; withHammer.weaponId = "hammer";
        CHECK(near(effectiveMaxMult(withHammer, defs), 2.6f), "War Hammer equipped -> its own 2.6x ceiling");

        EquippedSet withDaggersAndBerserker; withDaggersAndBerserker.weaponId = "daggers"; withDaggersAndBerserker.talentId = "berserker";
        CHECK(near(effectiveMaxMult(withDaggersAndBerserker, defs), 2.35f), "Berserker's +15% stacks on top of Twin Daggers' own 2.2x ceiling");
    }

    // 8. Talent predicate functions: recognized values, and a safe no-op for unrecognized/absent.
    CHECK(near(talentRampTimeMultiplier("focus"), 1.25f), "talentRampTimeMultiplier(focus) == 1.25");
    CHECK(near(talentRampTimeMultiplier("berserker"), 1.0f), "talentRampTimeMultiplier is a no-op for a talent that doesn't affect it");
    CHECK(near(talentRampTimeMultiplier(""), 1.0f), "talentRampTimeMultiplier(\"\") (no talent equipped) is a no-op");
    CHECK(near(talentMaxMultBonus("berserker"), 0.15f), "talentMaxMultBonus(berserker) == 0.15");
    CHECK(near(talentMaxMultBonus("focus"), 0.0f), "talentMaxMultBonus is 0 for a talent that doesn't affect it");
    CHECK(talentZerosRedZoneAttacks("berserker"), "Berserker zeros Red-zone attacks");
    CHECK(!talentZerosRedZoneAttacks("guardian"), "Guardian does not zero Red-zone attacks");
    CHECK(near(talentDefenseMitigationFloor("guardian"), 0.10f), "Guardian's mitigation floor is 0.10 (10%)");
    CHECK(near(talentDefenseMitigationFloor(""), 0.0f), "no talent equipped -> no mitigation floor");

    if (g_fail == 0) { printf("equipment_test: ALL PASS (30 checks)\n"); return 0; }
    printf("equipment_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
