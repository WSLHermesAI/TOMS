// equipment_system.h — Weapon/Armor/Talent definitions and the effective stat/bar-parameter
// stack they contribute, per MAIN_BATTLE_SCENE_DESIGN.md §4 and
// docs/IMPLEMENTATION_ROADMAP.md Milestone 6.
//
// Deliberately content-light: the schema and the modifier-stack math are fully built and
// tested here; a real data/equipment.json (per MAIN_BATTLE_SCENE_DESIGN.md §4.4's example set)
// is Milestone 8's content-authoring job, same "systems now, content later" split already used
// for missions (mission_system.h) in Milestone 4.
#pragma once
#include <json.hpp>
#include <map>
#include <string>
#include "power_bar.h"

namespace toms {

enum class EquipmentSlot { Weapon, Armor, Talent };
const char* toString(EquipmentSlot s);
EquipmentSlot equipmentSlotFromString(const std::string& s);   // unrecognized -> Weapon (fail-safe default)

struct EquipmentDefinition {
    std::string id, sprite;
    // Raw json (plain string or {code:text} multi-language map) -- resolve via
    // Locale::field() at display time, not here (equipmentFromJson has no Locale access,
    // and is called before the title phase sets the player's language anyway).
    nlohmann::json name, desc;
    EquipmentSlot slot = EquipmentSlot::Weapon;
    int statAtk = 0, statDef = 0;
    PowerBarParams bar;              // baseline geometry by default (PowerBarParams{})
    float maxMult = 2.0f;            // attack power ceiling -- meaningful for weapons only
    // Talent id (talent slot only) -- a small string switched on by the talent*() functions
    // below, not a scripting hook, matching this project's `action`/`kind` enum convention.
    std::string talent;
};

nlohmann::json toJson(const EquipmentDefinition& d);
EquipmentDefinition equipmentFromJson(const nlohmann::json& j);

// What's currently equipped in each slot -- an empty id means nothing equipped there.
struct EquippedSet {
    std::string weaponId, armorId, talentId;
};

// Adds the equipped weapon/armor's flat stat bonuses on top of already-computed base atk/def
// (level-up + gem/potion bonuses are a separate, earlier layer in the stack -- see
// MAIN_BATTLE_SCENE_DESIGN.md §6.2). An empty/unknown slot contributes 0, never throws.
void applyEquipmentStats(const EquippedSet& eq, const std::map<std::string, EquipmentDefinition>& defs,
                          int& atk, int& def);

// The equipped weapon's Attack Bar params (or PowerBarParams{}'s baseline geometry if none
// equipped), with the equipped talent's rampTime modifier applied if it has one (e.g. Focus
// stretches rampTime by 25% on both bars -- see talentRampTimeMultiplier()).
PowerBarParams effectiveAttackBar(const EquippedSet& eq, const std::map<std::string, EquipmentDefinition>& defs);
// Same, for the equipped armor's Defense Bar params.
PowerBarParams effectiveDefenseBar(const EquippedSet& eq, const std::map<std::string, EquipmentDefinition>& defs);
// The equipped weapon's maxMult (attack power ceiling), or 2.0 (baseline) if none equipped,
// with the equipped talent's ceiling bonus applied if it has one (e.g. Berserker +15%).
float effectiveMaxMult(const EquippedSet& eq, const std::map<std::string, EquipmentDefinition>& defs);

// Talent effects (MAIN_BATTLE_SCENE_DESIGN.md §4.2/§4.3) -- small, explicit functions rather
// than a scripting hook. Each returns a neutral/no-op value for any talentId it doesn't
// recognize (including "" / no talent equipped), so callers never need to special-case absence.
float talentRampTimeMultiplier(const std::string& talentId);      // Focus: 1.25x; else 1.0x
float talentMaxMultBonus(const std::string& talentId);             // Berserker: +0.15 (15%); else 0
bool  talentZerosRedZoneAttacks(const std::string& talentId);      // Berserker: true; else false
float talentDefenseMitigationFloor(const std::string& talentId);   // Guardian: 0.10 (10%); else 0

} // namespace toms
