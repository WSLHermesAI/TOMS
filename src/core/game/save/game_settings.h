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

// 2: English became the default language (2026-10-12); a version-1 file's language is reset to it.
constexpr int kGameSettingsSchemaVersion = 2;

// Particle emitters with more than this many particles (maxParticles) are simulated on the GPU,
// where the renderer has compute shaders (docs/engine/07_PARTICLES.md); 0 = always on the CPU. Phones get
// a lower default: their CPUs are slower, so the GPU pays off sooner.
#if defined(__ANDROID__) || defined(TOMS_IOS)
constexpr int kDefaultParticleGpuThreshold = 3000;
#else
constexpr int kDefaultParticleGpuThreshold = 5000;
#endif

struct GameSettings {
    int schemaVersion = kGameSettingsSchemaVersion;
    // Language code into data/text.json's "languages" list. English is the default (the owner,
    // 2026-10-12: English only until translations are asked for; every text is still looked up by its
    // key, so another language is a matter of filling text.json).
    std::string language = "en";
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
    // Art style: the folder name under assets/media/styles/ whose sprites replace the original
    // ones ("" = the original art). Read once at startup (Game::loadAssets), so changing it in
    // Settings takes effect after a restart -- the Settings dialog says so.
    std::string artStyle;
    // The player's look: a sprite name in the atlas ("player_mage" ...; docs/engine/14_AI_SPRITES.md). "" or a sprite the atlas
    // lacks = the original "player".
    std::string playerLook;
    // See kDefaultParticleGpuThreshold. Read whenever a particle effect starts.
    int particleGpuThreshold = kDefaultParticleGpuThreshold;
    // Settings > "Reduce shaking and flashes" (docs/gameplay/15 §3.8): the battle's hit stops and shakes a quarter as
    // strong, no flashes, no slow motion.
    bool reduceEffects = false;
};

nlohmann::json toJson(const GameSettings& s);
GameSettings gameSettingsFromJson(const nlohmann::json& j);
// Reads `path`; missing/unreadable file yields defaults and sets *found=false (not an error).
GameSettings loadGameSettings(const std::string& path, bool* found = nullptr);
bool saveGameSettings(const std::string& path, const GameSettings& s);

} // namespace toms
