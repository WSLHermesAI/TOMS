#include "game_settings.h"
#include "save_system.h"

namespace toms {

nlohmann::json toJson(const GameSettings& s) {
    nlohmann::json j;
    j["schemaVersion"] = s.schemaVersion;
    j["language"] = s.language;
    j["slotCount"] = s.slotCount;
    j["uiFontScale"] = s.uiFontScale;
    return j;
}

GameSettings gameSettingsFromJson(const nlohmann::json& j) {
    GameSettings s;
    if (!j.is_object()) return s;
    s.schemaVersion = j.value("schemaVersion", kGameSettingsSchemaVersion);
    std::string lang = j.value("language", std::string("zh_TW"));
    if (!lang.empty()) s.language = lang;
    int slots = j.value("slotCount", 3);
    if (slots < 1) slots = 1;
    if (slots > 8) slots = 8;      // the Continue page lays out at most 8 rows
    s.slotCount = slots;
    float fs = j.value("uiFontScale", 1.5f);
    if (fs < 0.5f || fs > 4.0f) fs = 1.5f;
    s.uiFontScale = fs;
    return s;
}

GameSettings loadGameSettings(const std::string& path, bool* found) {
    if (found) *found = false;
    nlohmann::json j = readJsonFileSafe(path);
    if (j.is_null()) return GameSettings{};
    if (found) *found = true;
    return gameSettingsFromJson(j);
}

bool saveGameSettings(const std::string& path, const GameSettings& s) {
    GameSettings copy = s;
    copy.schemaVersion = kGameSettingsSchemaVersion;
    return writeJsonAtomic(path, toJson(copy));
}

} // namespace toms
