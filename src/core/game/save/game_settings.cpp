#include "game_settings.h"
#include "save_system.h"

namespace toms {

nlohmann::json toJson(const GameSettings& s) {
    nlohmann::json j;
    j["schemaVersion"] = s.schemaVersion;
    j["language"] = s.language;
    j["slotCount"] = s.slotCount;
    j["uiFontScale"] = s.uiFontScale;
    j["cameraMode"] = s.cameraMode;
    j["viewCols"] = s.viewCols;
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
    int cam = j.value("cameraMode", 0);
    s.cameraMode = (cam == 1) ? 1 : 0;   // unrecognized/missing -> Follow
    int vc = j.value("viewCols", 13);
    if (vc < 4) vc = 4;      // cameraViewportTiles() floors at 4 anyway; keep the setting sane
    if (vc > 60) vc = 60;    // past this a tile is a couple px, not a testing scenario
    s.viewCols = vc;
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
