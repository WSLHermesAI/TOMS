// game_settings.h — persisted, game-wide player settings (title phase's Settings page).
//
// Kept separate from save_system.h's Meta/Run saves: those are *progress*, this is
// *preferences* (they survive New Game, wiping a slot, and a schema migration of either save).
//
// Stored at save/settings.json (desktop) or /save/settings.json (browser, IDBFS).
#pragma once
#include <json.hpp>
#include <string>

namespace toms {

constexpr int kGameSettingsSchemaVersion = 1;

struct GameSettings {
    int schemaVersion = kGameSettingsSchemaVersion;
    // Language code into data/text.json's "languages" list ("zh_TW" default — this project's
    // UI text is authored in Traditional Chinese).
    std::string language = "zh_TW";
    // How many save files the Continue page offers.
    int slotCount = 3;
    // ImGui font scale for the desktop debug/overlay UI (kept here so it finally persists
    // instead of resetting every launch — see uiFontScale_ in game.h).
    float uiFontScale = 1.5f;
    // Maze camera: 0 = Follow (viewport pans to keep the player centered), 1 = Rooms (the
    // stage is divided into fixed viewport-sized sections; the camera slides between them as
    // the player crosses a boundary). See Game::cameraMode_ in game.h.
    int cameraMode = 0;
    // How many tile columns the maze camera shows across the screen -- tile size and visible
    // row count are both derived from this (see Game::cameraViewportTiles() in game.cpp).
    // Adjustable live from the F1 debug overlay for testing what fits on a smaller screen.
    int viewCols = 13;
};

nlohmann::json toJson(const GameSettings& s);
GameSettings gameSettingsFromJson(const nlohmann::json& j);
// Reads `path`; missing/unreadable file yields defaults and sets *found=false (not an error).
GameSettings loadGameSettings(const std::string& path, bool* found = nullptr);
bool saveGameSettings(const std::string& path, const GameSettings& s);

} // namespace toms
