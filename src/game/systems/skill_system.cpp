#include "skill_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

#include <algorithm>

namespace toms {

nlohmann::json toJson(const SkillDefinition& d) {
    nlohmann::json j;
    j["id"] = d.id; j["lineage"] = d.lineage; j["tier"] = d.tier; j["cost"] = d.cost;
    j["requires"] = d.requires_;
    j["name"] = d.name; j["desc"] = d.desc;
    j["effect"] = { {"atk", d.statAtk}, {"def", d.statDef} };
    return j;
}

SkillDefinition skillFromJson(const nlohmann::json& j) {
    SkillDefinition d;
    if (!j.is_object()) return d;
    d.id = j.value("id", std::string());
    d.lineage = j.value("lineage", std::string());
    d.tier = j.value("tier", 0);
    d.cost = j.value("cost", 0);
    if (j.contains("requires") && j["requires"].is_array())
        for (auto& r : j["requires"]) if (r.is_string()) d.requires_.push_back(r.get<std::string>());
    d.name = j.contains("name") ? j["name"] : nlohmann::json(d.id);
    d.desc = j.contains("desc") ? j["desc"] : nlohmann::json("");
    if (j.contains("effect") && j["effect"].is_object()) {
        d.statAtk = j["effect"].value("atk", 0);
        d.statDef = j["effect"].value("def", 0);
    }
    return d;
}

std::map<std::string, SkillDefinition> loadSkillDefs(const std::string& path) {
    std::map<std::string, SkillDefinition> out;
    nlohmann::json j = readJsonFileSafe(path);
    if (!j.is_object()) return out;
    for (auto& [id, v] : j.items()) {
        SkillDefinition d = skillFromJson(v);
        if (d.id.empty()) d.id = id;   // tolerate a file that relies on the key, not a repeated "id"
        out[id] = d;
    }
    return out;
}

bool canUnlockSkill(const std::map<std::string, SkillDefinition>& defs,
                     const std::vector<std::string>& owned, int skillPoints,
                     const std::string& skillId) {
    auto it = defs.find(skillId);
    if (it == defs.end()) return false;
    // A tier-0 node is a lineage's root -- it arrives ONLY as a chapter grant
    // (Game::applyChapterGrants calls RunStoryState::unlockSkill directly, bypassing this
    // predicate entirely, exactly so a grant is never subject to it). Its cost is 0 because a
    // grant is free, not because it's meant to be purchasable the moment any points exist --
    // confirmed as a real bug during S4's own verification: with this check absent, opening the
    // skill screen with 1 point let the player instantly buy ch_02/ch_03's not-yet-granted roots.
    if (it->second.tier <= 0) return false;
    if (std::find(owned.begin(), owned.end(), skillId) != owned.end()) return false;   // already owned
    if (skillPoints < it->second.cost) return false;
    for (auto& req : it->second.requires_)
        if (std::find(owned.begin(), owned.end(), req) == owned.end()) return false;
    return true;
}

void applySkillEffects(const std::map<std::string, SkillDefinition>& defs,
                        const std::vector<std::string>& owned, int& atk, int& def) {
    for (auto& id : owned) {
        auto it = defs.find(id);
        if (it == defs.end()) continue;
        atk += it->second.statAtk;
        def += it->second.statDef;
    }
}

} // namespace toms
