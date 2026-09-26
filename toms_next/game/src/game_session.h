// game_session.h -- runs the legacy `Game` on bgfx, independent of the windowing toolkit.
//
// Both hosts use it: the SDL3 game (main_sdl.cpp) and the Qt editor viewport. The host owns the
// window and bgfx (bgfx_host.h); the session owns the Game, its BgfxRenderer and ImGui, and turns
// platform-neutral input into Game calls. The input rules are a line-by-line port of the loop in
// src/game/core/main.cpp, so the game plays exactly as before.
#pragma once
#include "imgui_bgfx.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

class Game;

namespace toms::next {

class BgfxRenderer;

enum class Key : uint8_t {
    Up, Down, Left, Right, W, A, S, D,
    Enter, Space, Escape, Tab, F1, F2, F3,
    F, G, H, I, B,
    Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Count
};

struct InputState {
    std::array<bool, (size_t)Key::Count> down{};   // level state of every key this frame
    float mouseX = 0, mouseY = 0;                  // backbuffer (device) pixels
    bool  mouseLeft = false, mouseRight = false, mouseMiddle = false;
    float wheel = 0;                               // wheel steps since last frame
    bool  hasMouse = false;
    bool  isDown(Key k) const { return down[(size_t)k]; }
};

struct SessionOptions {
    std::string assetDir;              // empty = auto (see defaultAssetDir)
    std::string startStage = "stage01";
    bool        enableDebugUi = true;  // ImGui windows (F1 / F2 / Tab stage select / toasts)
    // Mobile: grow the UI objects, keep the 1024x768 game resolution (Game::setUiScale / setPadScale,
    // the owner's rule). 1.0 = unchanged. The web host sets these on small screens.
    float       uiScale  = 1.0f;
    float       padScale = 1.0f;
};

class GameSession {
public:
    GameSession();
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // bgfx must already be initialized. On failure `error` is a user-facing explanation.
    bool start(const SessionOptions& opts, std::string& error);
    void stop();                       // frees the Game and every GPU handle it owns
    bool running() const { return game_ != nullptr; }

    // Advances and draws one frame into the bgfx views (does NOT call bgfx::frame()).
    // Returns false when the game asks to quit (Escape on the title menu).
    bool frame(int dtMs, const InputState& in, uint32_t deviceW, uint32_t deviceH);

    Game* game() { return game_.get(); }
    BgfxRenderer* renderer() { return renderer_; }
    const std::string& assetDir() const { return assetDir_; }

    bool showDebugOverlay = false;     // F1
    bool showStylingSpike = false;     // F2

    // Asset folder resolution: env ASSET_DIR, else the legacy TOMS/assets baked in at build time.
    static std::string defaultAssetDir();
    // If TOMS/assets/wqy-zenhei.ttc is missing, points TOMS_FONT at a Windows CJK font.
    // Returns a note for the user, or "" when nothing had to change.
    static std::string applyFontFallback(const std::string& assetDir);

private:
    bool keyPressed(const InputState& in, Key k);

    std::unique_ptr<Game> game_;
    BgfxRenderer* renderer_ = nullptr;   // created by Game::loadAssets; owned (and freed) by us
    ImGuiBgfx imgui_;
    bool debugUi_ = true;
    std::string assetDir_;
    std::array<bool, (size_t)Key::Count> keyWas_{};
    bool mouseWasDown_ = false, mouseHasDesign_ = false;
    float mouseDesignX_ = 0, mouseDesignY_ = 0;
    int  mouseHeldMs_ = 0, mouseRepeatMs_ = 0;   // hold-to-repeat taps (the on-screen pad)
};

}  // namespace toms::next
