#include "forge_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

#include <algorithm>

namespace toms {

nlohmann::json toJson(const ForgeRecipeDefinition& d) {
    nlohmann::json j;
    j["id"] = d.id; j["resultEquipmentId"] = d.resultEquipmentId;
    j["name"] = d.name; j["desc"] = d.desc;
    j["cost"] = { {"gold", d.costGold}, {"materials", d.materials} };
    return j;
}

ForgeRecipeDefinition forgeRecipeFromJson(const nlohmann::json& j) {
    ForgeRecipeDefinition d;
    if (!j.is_object()) return d;
    d.id = j.value("id", std::string());
    d.resultEquipmentId = j.value("resultEquipmentId", std::string());
    d.name = j.contains("name") ? j["name"] : nlohmann::json(d.id);
    d.desc = j.contains("desc") ? j["desc"] : nlohmann::json("");
    if (j.contains("cost") && j["cost"].is_object()) {
        const auto& c = j["cost"];
        d.costGold = c.value("gold", 0);
        if (c.contains("materials") && c["materials"].is_object())
            for (auto& [itemId, count] : c["materials"].items())
                if (count.is_number_integer()) d.materials[itemId] = count.get<int>();
    }
    return d;
}

std::map<std::string, ForgeRecipeDefinition> loadForgeRecipes(const std::string& path) {
    std::map<std::string, ForgeRecipeDefinition> out;
    nlohmann::json j = readJsonFileSafe(path);
    if (!j.is_object()) return out;
    for (auto& [id, v] : j.items()) {
        ForgeRecipeDefinition d = forgeRecipeFromJson(v);
        if (d.id.empty()) d.id = id;   // tolerate a file that relies on the key, not a repeated "id"
        out[id] = d;
    }
    return out;
}

bool canCraft(const std::map<std::string, ForgeRecipeDefinition>& defs,
              const std::vector<std::string>& known, const std::vector<std::string>& inv,
              int gold, const std::string& recipeId) {
    auto it = defs.find(recipeId);
    if (it == defs.end()) return false;
    if (std::find(known.begin(), known.end(), recipeId) == known.end()) return false;   // not learned
    if (gold < it->second.costGold) return false;
    for (auto& [itemId, need] : it->second.materials) {
        int have = (int)std::count(inv.begin(), inv.end(), itemId);
        if (have < need) return false;
    }
    return true;
}

} // namespace toms
