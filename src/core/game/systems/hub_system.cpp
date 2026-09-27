#include "hub_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

namespace toms {

nlohmann::json toJson(const HubLocationDefinition& d) {
    nlohmann::json j;
    j["id"] = d.id; j["unlockFlag"] = d.unlockFlag;
    j["name"] = d.name; j["desc"] = d.desc;
    j["action"] = { {"kind", d.actionKind}, {"npc", d.actionNpc} };
    return j;
}

HubLocationDefinition hubLocationFromJson(const nlohmann::json& j) {
    HubLocationDefinition d;
    if (!j.is_object()) return d;
    d.id = j.value("id", std::string());
    d.unlockFlag = j.value("unlockFlag", std::string());
    d.name = j.contains("name") ? j["name"] : nlohmann::json(d.id);
    d.desc = j.contains("desc") ? j["desc"] : nlohmann::json("");
    if (j.contains("action") && j["action"].is_object()) {
        const auto& a = j["action"];
        d.actionKind = a.value("kind", std::string());
        d.actionNpc = a.value("npc", std::string());
    }
    return d;
}

std::map<std::string, HubLocationDefinition> loadHubLocations(const std::string& path) {
    std::map<std::string, HubLocationDefinition> out;
    nlohmann::json j = readJsonFileSafe(path);
    if (!j.is_object()) return out;
    for (auto& [id, v] : j.items()) {
        HubLocationDefinition d = hubLocationFromJson(v);
        if (d.id.empty()) d.id = id;   // tolerate a file that relies on the key, not a repeated "id"
        out[id] = d;
    }
    return out;
}

bool hubLocationUnlocked(const std::map<std::string, HubLocationDefinition>& defs,
                          const std::function<bool(const std::string&)>& hasFlag,
                          const std::string& locationId) {
    auto it = defs.find(locationId);
    if (it == defs.end()) return false;
    if (it->second.unlockFlag.empty()) return true;   // no gate declared -> always reachable
    return hasFlag(it->second.unlockFlag);
}

} // namespace toms
