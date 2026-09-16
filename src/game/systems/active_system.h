// active_system.h — S7 (first slice): equipment actives (data/actives.json), the fourth of
// docs/story/STORY_DATA_SCHEMA.md's four systems (技能樹／鍛造／裝備主動技／村莊樞紐), per
// ART_AND_ABILITY_DESIGN.md's named actives (a_qingxiao_edge, a_sanctuary_echo, a_soul_echo) and
// the status-effect system's "使用主動技"/cooldown wording (st_echo/st_silence) -- a real,
// player-pressed battle action, not a passive auto-trigger.
//
// Same shape as skill_system.h/forge_system.h on purpose: pure content, no Game dependency. An
// active is granted by whatever equipped gear lists its id in EquipmentDefinition::actives (see
// equipment_system.h's equippedActives()) -- there is no separate "known" list yet (unlike
// forgeRecipesKnown), because nothing in this slice's content grants one independently of the
// equipment that carries it. See docs/progress_report's log for why that's a deliberate, named
// deferral rather than an oversight.
#pragma once
#include <json.hpp>
#include <map>
#include <string>

namespace toms {

struct ActiveSkillDefinition {
    std::string id;
    // What pressing this active does. Only "guaranteed_crit_next_attack" is implemented so far
    // (arms the next Attack-bar tap to resolve as a perfect release, then multiplies its damage
    // by `damageMult`) -- a string, not an enum, so a future kind is a content + dispatcher-case
    // addition, not a schema change (mirrors hub_system.h's actionKind).
    std::string effectKind;
    float damageMult = 1.0f;   // effectKind-specific parameter
    // Raw json (plain string or {code:text} multi-language map) -- resolved via Locale::field()
    // at display time, matching every other content file's name/desc convention.
    nlohmann::json name, desc;
};

nlohmann::json toJson(const ActiveSkillDefinition& d);
ActiveSkillDefinition activeSkillFromJson(const nlohmann::json& j);

// Loads data/actives.json's {id: ActiveSkillDefinition} map. Missing/malformed file -> empty map
// (never throws), matching readJsonFileSafe's fail-soft convention used everywhere else.
std::map<std::string, ActiveSkillDefinition> loadActiveSkills(const std::string& path);

} // namespace toms
