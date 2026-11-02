// equipment_system.h — Weapon/Armor/Talent definitions and the effective stat/bar-parameter
// stack they contribute, per docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4 and
// docs/architecture/IMPLEMENTATION_ROADMAP.md Milestone 6.
//
// Deliberately content-light: the schema and the modifier-stack math are fully built and
// tested here; a real data/equipment.json (per docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.4's example set)
// is Milestone 8's content-authoring job, same "systems now, content later" split already used
// for missions (mission_system.h) in Milestone 4.
#pragma once
#include <json.hpp>
#include <map>
#include <string>
#include <vector>
#include "power_bar.h"

namespace toms {

enum class EquipmentSlot { Weapon, Armor, Talent };
const char* f_ToString(EquipmentSlot l_eS);
EquipmentSlot f_EquipmentSlotFromString(const std::string& l_strS);   // unrecognized -> Weapon (fail-safe default)

struct EquipmentDefinition {
    std::string m_strId, m_strSprite;
    // Raw json (plain string or {code:text} multi-language map) -- resolve via
    // Locale::field() at display time, not here (equipmentFromJson has no Locale access,
    // and is called before the title phase sets the player's language anyway).
    nlohmann::json m_jName, m_jDesc;
    EquipmentSlot m_eSlot = EquipmentSlot::Weapon;
    int m_iStatAtk = 0, m_iStatDef = 0;
    // Every other bonus the piece gives while worn, by id -- the attributes of data/stats.json
    // ("str", "dex", ...). Read for the player menu's Status tab; no battle rule uses them yet.
    std::map<std::string, int> m_mapStats;
    PowerBarParams m_oBar;              // baseline geometry by default (PowerBarParams{})
    float m_fMaxMult = 2.0f;            // attack power ceiling -- meaningful for weapons only
    // Talent id (talent slot only) -- a small string switched on by the talent*() functions
    // below, not a scripting hook, matching this project's `action`/`kind` enum convention.
    std::string m_strTalent;
    // S7 (equipment actives, docs/story/STORY_DATA_SCHEMA.md §1.2/§6.2): ids into
    // equipment_actives.h's ActiveDefinition map. Almost always empty -- only a handful of
    // named pieces (per docs/design/ART_AND_ABILITY_DESIGN.md) ever grant one.
    std::vector<std::string> m_vActives;
    // The battle picture (docs/engine/05_ANIMATION.md "Characters"): items.json "battleAnim": {"character": .anim, "held":
    // .anim}, paths under assets/media. The character clip (body / hand nodes) is played by the player's character and
    // the held clip (the weapon) is drawn at its hand; the battle's status picks the clip of both (idle, charge,
    // attack, miss). Empty: the battle shows the player's picture.
    std::string m_strBattleCharacterAnim, m_strBattleHeldAnim;
    // "battleAnim": {"render": "2d" | "3d", "model": ".glb"}: how the fighter is drawn (docs/gameplay/18_FIGHTER_VIEWS.md,
    // FighterView); "2d" (default) plays the .anim files above, "3d" the model's animations by the same clip names.
    std::string m_strBattleRender = "2d", m_strBattleModel;
    // "battleAnim": {"clips": {"idle": "ready", "charge": "raise", "attack": "slash", "miss": "fumble"}} (docs/gameplay/07 phase 2):
    // the clip each battle status plays, in both files. A status not listed plays the clip of its own name.
    std::map<std::string, std::string> m_mapBattleClips;
    std::string f_BattleClip(const std::string& l_strStatus) const {
        auto l_itIt = m_mapBattleClips.find(l_strStatus);
        return l_itIt == m_mapBattleClips.end() ? l_strStatus : l_itIt->second;
    }
};

nlohmann::json f_ToJson(const EquipmentDefinition& l_oD);
EquipmentDefinition f_EquipmentFromJson(const nlohmann::json& l_jJ);

// What's currently equipped in each slot -- an empty id means nothing equipped there.
struct EquippedSet {
    std::string m_strWeaponId, m_strArmorId, m_strTalentId;
};

// Adds the equipped weapon/armor's flat stat bonuses on top of already-computed base atk/def
// (level-up + gem/potion bonuses are a separate, earlier layer in the stack -- see
// docs/design/MAIN_BATTLE_SCENE_DESIGN.md §6.2). An empty/unknown slot contributes 0, never throws.
void f_ApplyEquipmentStats(const EquippedSet& l_oEq, const std::map<std::string, EquipmentDefinition>& l_mapDefs,
                          int& l_iAtk, int& l_iDef);

// S7: every active id granted by whatever is CURRENTLY equipped (weapon, then armor, then
// talent) -- today's content has at most one across all three slots, so "first across slots" is
// not yet a real ordering decision. An empty/unknown slot contributes nothing.
std::vector<std::string> f_EquippedActives(const EquippedSet& l_oEq, const std::map<std::string, EquipmentDefinition>& l_mapDefs);

// The equipped weapon's Attack Bar params (or PowerBarParams{}'s baseline geometry if none
// equipped), with the equipped talent's rampTime modifier applied if it has one (e.g. Focus
// stretches rampTime by 25% on both bars -- see talentRampTimeMultiplier()).
PowerBarParams f_EffectiveAttackBar(const EquippedSet& l_oEq, const std::map<std::string, EquipmentDefinition>& l_mapDefs);
// Same, for the equipped armor's Defense Bar params.
PowerBarParams f_EffectiveDefenseBar(const EquippedSet& l_oEq, const std::map<std::string, EquipmentDefinition>& l_mapDefs);
// The equipped weapon's maxMult (attack power ceiling), or 2.0 (baseline) if none equipped,
// with the equipped talent's ceiling bonus applied if it has one (e.g. Berserker +15%).
float f_EffectiveMaxMult(const EquippedSet& l_oEq, const std::map<std::string, EquipmentDefinition>& l_mapDefs);

// Talent effects (docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.2/§4.3) -- small, explicit functions rather
// than a scripting hook. Each returns a neutral/no-op value for any talentId it doesn't
// recognize (including "" / no talent equipped), so callers never need to special-case absence.
float f_TalentRampTimeMultiplier(const std::string& l_strTalentId);      // Focus: 1.25x; else 1.0x
float f_TalentMaxMultBonus(const std::string& l_strTalentId);             // Berserker: +0.15 (15%); else 0
bool  f_TalentZerosRedZoneAttacks(const std::string& l_strTalentId);      // Berserker: true; else false
float f_TalentDefenseMitigationFloor(const std::string& l_strTalentId);   // Guardian: 0.10 (10%); else 0

} // namespace toms
