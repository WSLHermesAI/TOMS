#include "active_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

namespace toms {

nlohmann::json toJson(const ActiveSkillDefinition& d) {
    nlohmann::json j;
    j["id"] = d.id; j["name"] = d.name; j["desc"] = d.desc;
    j["effect"] = { {"kind", d.effectKind}, {"damageMult", d.damageMult} };
    return j;
}

ActiveSkillDefinition activeSkillFromJson(const nlohmann::json& j) {
    ActiveSkillDefinition d;
    if (!j.is_object()) return d;
    d.id = j.value("id", std::string());
    d.name = j.contains("name") ? j["name"] : nlohmann::json(d.id);
    d.desc = j.contains("desc") ? j["desc"] : nlohmann::json("");
    if (j.contains("effect") && j["effect"].is_object()) {
        const auto& e = j["effect"];
        d.effectKind = e.value("kind", std::string());
        d.damageMult = e.value("damageMult", 1.0f);
    }
    return d;
}

std::map<std::string, ActiveSkillDefinition> loadActiveSkills(const std::string& path) {
    std::map<std::string, ActiveSkillDefinition> out;
    nlohmann::json j = readJsonFileSafe(path);
    if (!j.is_object()) return out;
    for (auto& [id, v] : j.items()) {
        ActiveSkillDefinition d = activeSkillFromJson(v);
        if (d.id.empty()) d.id = id;   // tolerate a file that relies on the key, not a repeated "id"
        out[id] = d;
    }
    return out;
}

} // namespace toms
