#include "ending_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

namespace toms {

nlohmann::json toJson(const EndingDefinition& d) {
    nlohmann::json j;
    j["id"] = d.id; j["name"] = d.nameKey; j["text"] = d.textKey; j["tone"] = d.tone;
    j["unlocksHint"] = d.unlocksHint; j["allowsRebirth"] = d.allowsRebirth;
    j["requires"] = d.requires_;
    return j;
}

EndingDefinition endingFromJson(const nlohmann::json& j) {
    EndingDefinition d;
    if (!j.is_object()) return d;
    d.id = j.value("id", std::string());
    d.nameKey = j.value("name", std::string());
    d.textKey = j.value("text", std::string());
    d.tone = j.value("tone", std::string());
    d.unlocksHint = j.value("unlocksHint", std::string());
    d.allowsRebirth = j.value("allowsRebirth", false);
    d.requires_ = j.contains("requires") ? j["requires"] : nlohmann::json();
    return d;
}

EndingsTable loadEndings(const std::string& path) {
    EndingsTable t;
    nlohmann::json j = readJsonFileSafe(path);
    if (!j.is_object()) return t;
    t.fallback = j.value("fallback", std::string());
    if (j.contains("endings") && j["endings"].is_array())
        for (auto& e : j["endings"]) {
            EndingDefinition d = endingFromJson(e);
            if (!d.id.empty()) t.endings.push_back(d);
        }
    return t;
}

static const EndingDefinition* findEnding(const EndingsTable& table, const std::string& id) {
    for (auto& e : table.endings) if (e.id == id) return &e;
    return nullptr;
}

const EndingDefinition* judgeEnding(const EndingsTable& table, const ConditionContext& ctx) {
    for (auto& e : table.endings)
        if (evaluate(e.requires_, ctx)) return &e;
    return findEnding(table, table.fallback);
}

const EndingDefinition* checkNamedEndings(const EndingsTable& table, const std::vector<std::string>& order,
                                           const ConditionContext& ctx) {
    for (auto& id : order) {
        const EndingDefinition* e = findEnding(table, id);
        if (e && evaluate(e->requires_, ctx)) return e;
    }
    return nullptr;
}

} // namespace toms
