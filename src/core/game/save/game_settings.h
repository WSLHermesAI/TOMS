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
// 3: Traditional Chinese became the default (the owner, 2026-11-02); an older file's language is reset to it.
constexpr int g_iGameSettingsSchemaVersion = 3;

// Particle emitters with more than this many particles (maxParticles) are simulated on the GPU,
// where the renderer has compute shaders (docs/engine/07_PARTICLES.md); 0 = always on the CPU. Phones get
// a lower default: their CPUs are slower, so the GPU pays off sooner.
#if defined(__ANDROID__) || defined(TOMS_IOS)
constexpr int g_iDefaultParticleGpuThreshold = 3000;
#else
constexpr int g_iDefaultParticleGpuThreshold = 5000;
#endif

struct GameSettings {
    int m_iSchemaVersion = g_iGameSettingsSchemaVersion;
    // Language code into data/text.json's "languages" list. Traditional Chinese is the default (the owner,
    // 2026-11-02); only it and English are offered for now (text.json "offered"), and a saved language that is not
    // offered plays in English. The environment variable TOMS_LANGUAGE overrides it without saving (the tests: "en").
    std::string m_strLanguage = "zh_TW";
    // How many save files the Continue page offers.
    int m_iSlotCount = 3;
    // ImGui font scale for the desktop debug/overlay UI (kept here so it finally persists
    // instead of resetting every launch — see uiFontScale_ in game.h).
    float m_fUiFontScale = 1.5f;
    // Maze camera: 0 = Follow (viewport pans to keep the player centered), 1 = Rooms (the
    // stage is divided into fixed viewport-sized sections; the camera slides between them as
    // the player crosses a boundary). See Game::cameraMode_ in game.h.
    int m_iCameraMode = 0;
    // How many tile columns the maze camera shows across the screen -- tile size and visible
    // row count are both derived from this (see Game::cameraViewportTiles() in game.cpp).
    // Adjustable live from the F1 debug overlay for testing what fits on a smaller screen.
    int m_iViewCols = 13;
    // Art style: the folder name under assets/media/styles/ whose sprites replace the original
    // ones ("" = the original art). Read once at startup (Game::loadAssets), so changing it in
    // Settings takes effect after a restart -- the Settings dialog says so.
    std::string m_strArtStyle;
    // The player's look: a sprite name in the atlas ("player_mage" ...; docs/engine/14_AI_SPRITES.md). "" or a sprite the atlas
    // lacks = the original "player".
    std::string m_strPlayerLook;
    // See kDefaultParticleGpuThreshold. Read whenever a particle effect starts.
    int m_iParticleGpuThreshold = g_iDefaultParticleGpuThreshold;
    // Settings > "Reduce shaking and flashes" (docs/gameplay/15 §3.8): the battle's hit stops and shakes a quarter as
    // strong, no flashes, no slow motion.
    bool m_bReduceEffects = false;
    float m_fBattleSpeed = 1.0f;       // Settings > Battle speed (docs/gameplay/17 I10): the battle's own time
    bool m_bBowTutorialDone = false;   // the bow's first-pull tutorial was shown and followed (docs/gameplay/16 §3.2)
};

nlohmann::json f_ToJson(const GameSettings& l_oS);
GameSettings f_GameSettingsFromJson(const nlohmann::json& l_jJ);
// Reads `path`; missing/unreadable file yields defaults and sets *found=false (not an error).
GameSettings f_LoadGameSettings(const std::string& l_strPath, bool* l_pFound = nullptr);
bool f_SaveGameSettings(const std::string& l_strPath, const GameSettings& l_oS);

} // namespace toms
