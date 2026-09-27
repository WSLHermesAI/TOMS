// skill_system.h — the 功法三系 (three cultivation lineages) skill tree's definitions and the
// effective stat bonus owned skills contribute, per docs/story/STORY_BIBLE.md §3 (the tree itself opens at
// ch_04/F25) and docs/story/STORY_DATA_SCHEMA.md §1.2/§13's M4.
//
// A skill's own "owned" state is NOT stored here -- it lives on RunStoryState (skillsOwned()/
// unlockSkill()), the same split game.h already uses for equipment (EquippedSet is Game/run
// state, EquipmentDefinition is content). This file is pure content + pure math: no Game/Player/
// Renderer dependency, so it is unit-testable (skill_system_test.cpp), and the same shape as
// equipment_system.h on purpose -- one more instance of this project's "systems now, content
// later, content is data" pattern, not a new one.
#pragma once
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

namespace toms {

struct SkillDefinition {
    std::string id;
    std::string lineage;          // "yinqi" | "yuqi" | "faqi" (data-driven, not an enum -- a
                                   // fourth lineage should never need an engine change)
    int tier = 0;                 // 0 = the chapter-granted root of its lineage (see below)
    int cost = 0;                 // skill points required to unlock (root nodes cost 0: they
                                   // arrive as a chapter grant, not a purchase)
    std::vector<std::string> requires_;   // skill ids that must already be owned
    // Raw json (plain string or {code:text} multi-language map) -- resolved via Locale::field()
    // at display time, matching every other content file's name/desc convention.
    nlohmann::json name, desc;
    // Flat stat bonuses this skill contributes once owned -- deliberately the same two stats
    // equipment already grants (see applyEquipmentStats), so a skill and a weapon/armor stack in
    // the exact same combat formula rather than needing a parallel one.
    int statAtk = 0, statDef = 0;
};

nlohmann::json toJson(const SkillDefinition& d);
SkillDefinition skillFromJson(const nlohmann::json& j);

// Loads data/skills.json's {id: SkillDefinition} map. Missing/malformed file -> empty map (never
// throws), matching readJsonFileSafe's fail-soft convention used everywhere else in this project.
std::map<std::string, SkillDefinition> loadSkillDefs(const std::string& path);

// True if `skillId` names a real skill in `defs`, is not already owned, every id in its
// `requires_` IS owned, and `skillPoints` covers its cost. Pure predicate -- callers (the UI, and
// RunStoryState::unlockSkill itself) share this one rule instead of re-deriving it.
bool canUnlockSkill(const std::map<std::string, SkillDefinition>& defs,
                     const std::vector<std::string>& owned, int skillPoints,
                     const std::string& skillId);

// Adds every owned skill's flat stat bonus on top of already-computed atk/def -- called
// alongside applyEquipmentStats (same two out-parameters), so skills and gear are one stack, not
// two the caller has to remember to add separately. Unknown ids in `owned` contribute 0, never
// throw (a save from before a skill was renamed/removed must not crash the game).
//
// `rebirthScale` (M8, docs/story/STORY_BIBLE.md §8): "已學技能：保留，但每個技能的數值效果 ×0.5
// （取整到最小單位；純解鎖型技能如「引氣」保留全效）" -- after at least one rebirth, every TIER>=1
// node's bonus is scaled (floored); a tier<=0 root (a pure-unlock grant, no purchase involved) is
// always exempt regardless of `rebirthScale`, matching the bible's own carve-out. Defaults to 1.0
// so every pre-M8 call site keeps compiling and behaving exactly as before.
void applySkillEffects(const std::map<std::string, SkillDefinition>& defs,
                        const std::vector<std::string>& owned, int& atk, int& def,
                        float rebirthScale = 1.0f);

} // namespace toms
