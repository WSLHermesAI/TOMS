// game_session.h -- runs the core `Game` on bgfx, independent of the windowing toolkit.
//
// Both hosts use it: the SDL3 game (main_sdl.cpp) and the Qt editor viewport. The host owns the
// window and bgfx (bgfx_host.h); the session owns the Game, its BgfxRenderer and ImGui, and turns
// platform-neutral input into Game calls. The input rules are a line-by-line port of the loop in
// the old GLFW main (removed 2026-09-27), so the game plays exactly as before.
#pragma once
#include "canvas.h"   // toms::Canvas -- the design canvas for the screen
#include "imgui_bgfx.h"

#include <chrono>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

class Game;

namespace toms::next {

class BgfxRenderer;
class RmlUi;
class GameUi;

enum class Key : uint8_t {
    Up, Down, Left, Right, W, A, S, D,
    Enter, Space, Escape, Tab, F1, F2, F3,
    F5, F8,                                  // reload the UI files, RmlUi debugger
    F, G, H, I, B,
    C, Q, E,                                 // the player menu: Status, previous / next tab
    Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Count
};

struct InputState {
    std::array<bool, (size_t)Key::Count> m_arrDown{};   // level state of every key this frame
    float m_fMouseX = 0, m_fMouseY = 0;                  // backbuffer (device) pixels
    bool  m_bMouseLeft = false, m_bMouseRight = false, m_bMouseMiddle = false;
    float m_fWheel = 0;                               // wheel steps since last frame
    bool  m_bHasMouse = false;
    bool  f_IsDown(Key l_eK) const { return m_arrDown[(size_t)l_eK]; }
};

struct SessionOptions {
    std::string m_strAssetDir;              // empty = auto (see defaultAssetDir)
    std::string m_strStartStage = "stage01";
    std::string m_strPreviewAnim;           // "<file>#<clip>": play this .anim clip over the screen (--anim)
    std::string m_strSpritePath = "auto";   // auto | compute | instancing | cpu (--sprite-path, bgfx_renderer.h)
    bool m_bShowFps = false;              // the F3 HUD from the start (--fps)
    int m_iFxGpuThreshold = -1;           // --fx-gpu-threshold (-1: GameSettings::particleGpuThreshold)
    bool m_bDefaultBattleTuning = false;  // --battle-tuning=default: ignore data/battle.json (tests)
    bool m_bBattleLog = false;            // --battle-log: each battle event as a line on stderr (docs/gameplay/13 §4.5)
    std::string m_strPreviewFx;             // "<file>#<effect>": play this particle effect over the screen (--fx)
    std::string m_strPreviewMesh;           // a game-atlas sprite drawn as a waving triangle mesh (--mesh-test)
    std::string m_strPreviewCharacter;      // --character: the character a --anim character clip is played by
    std::string m_strPreviewHeld;           // --held: a held thing's .anim, its clip of the same name at the hand
    int m_iPreviewCrowd = 0;              // --anim-crowd=N: the --anim clip N times in a grid (AnimPlayer each; a benchmark)
    bool        m_bEnableDebugUi = true;  // ImGui windows (F1 / F2 / Tab stage select / toasts)
    // Mobile: grow the UI objects (Game::setUiScale). 1.0 = unchanged. The web host sets these on small screens.
    float       m_fUiScale  = 1.0f;
    // The design canvas (canvas.h): 0 x 0 = follow the screen's shape (landscape or portrait, live; the owner
    // 2026-10-25); a size = always that one, letterboxed (--design=WxH; the screenshot tests pin 1024 x 768).
    uint32_t    m_uDesignW = 0, m_uDesignH = 0;
    // The 3D scene behind the title (title_scene.h): a .glb / .gltf path, "" = the default
    // (<assets>/models/VirtualCity.glb, when it exists), "none" = the plain title, "ocean" = the sea (--title-scene).
    std::string m_strTitleScene;
    // With "": the sea and the default model in turns, one each start (game_session.cpp f_NextTitleBackground). Normal play only: a
    // test must draw the same picture every time.
    bool m_bTitleSceneInTurns = false;
    bool m_bShowVersion = true;   // the published version in the title's corner (off in tests: their reference pictures)
    // The battle on a 3D stage (battle.json "stages3d", docs/gameplay/21_BATTLE_3D.md), where the floor has one: on in
    // normal play on the desktop; off in scripted runs (their reference pictures) unless --battle-3d=on.
    bool m_bBattle3D = false;
    bool m_bBattle3DWait = false;   // load the stage before going on (scripted runs: their pictures never wait on the disk)
    // Read changed art, clips and effects again while running (Game::setHotReload, docs/engine/12_RESOURCE_MANAGER.md §4.6).
    bool m_bHotReload = false;
};

class TitleScene;
class BattleStage;

class GameSession {
public:
    GameSession();
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // bgfx must already be initialized. On failure `error` is a user-facing explanation.
    bool f_Start(const SessionOptions& l_oOpts, std::string& l_strError);
    void f_Stop();                       // frees the Game and every GPU handle it owns
    bool f_Running() const { return m_upGame != nullptr; }

    // Advances and draws one frame into the bgfx views (does NOT call bgfx::frame()).
    // Returns false when the game asks to quit (Escape on the title menu).
    bool f_Frame(int l_iDtMs, const InputState& l_oIn, uint32_t l_uDeviceW, uint32_t l_uDeviceH);

    Game* f_Game() { return m_upGame.get(); }
    RmlUi* f_Rml() { return m_upRml.get(); }   // the UI layer (the UI editor: unsaved files, the element under the mouse)
    void f_ReloadUi();                      // every .rml / .rcss again (F5; the UI editor after each edit)
    BgfxRenderer* f_Renderer() { return m_pRenderer; }
    const std::string& f_AssetDir() const { return m_strAssetDir; }

    bool m_bShowDebugOverlay = false;     // F1
    bool m_bShowStylingSpike = false;     // F2
    bool m_bShowHud = false;              // F3: FPS, backend, sprite batch path (--fps starts with it on)

    // Asset folder resolution: env ASSET_DIR, else assets/media (baked in at build time).
    static std::string f_DefaultAssetDir();
    // The default UI font (assets/media/fonts/NotoSansCJKtc-TOMS.otf, tools/make_ui_font.py);
    // desktop only -- the web build lets the browser draw text.
    static std::string f_UiFontPath(const std::string& l_strAssetDir);

private:
    bool f_KeyPressed(const InputState& l_oIn, Key l_eK);
    void f_ApplyLanguageFont();          // the UI font for the current language (on change)

    std::unique_ptr<Game> m_upGame;
    BgfxRenderer* m_pRenderer = nullptr;   // created by Game::loadAssets; owned (and freed) by us
    ImGuiBgfx m_oImgui;
    bool m_bDebugUi = true;
    // The HUD's frame rate: real time (not the game's dt, which --fixed-dt fakes), over half a second.
    double m_dHudClock = -1, m_dHudFps = 0, m_dHudMs = 0;
    int m_iHudFrames = 0;
    void f_DrawHud();
    std::string m_strAssetDir;
    bool m_bWatchUi = false;   // hot reload: the UI files are watched and reloaded when saved (GameUi::reloadIfChanged)
    int m_iUiWatchMs = 0;
    std::array<bool, (size_t)Key::Count> m_arrKeyWas{};
    bool m_bMouseWasDown = false;
    bool m_bAnyButtonWasDown = false;              // any mouse button, last frame (a won battle goes on with any)
    bool m_bSwallowPress = false;                  // that press went on from a won battle: the UI never sees it
    bool m_bMousePressFree = false;                // the current press hit no UI element (map click)
    std::unique_ptr<RmlUi> m_upRml;                 // RmlUi on bgfx (rml_ui.h)
    std::unique_ptr<GameUi> m_upUi;                 // the game's documents + data model (game_ui.h)
    std::unique_ptr<TitleScene> m_upTitleScene;     // the 3D scene behind the title (title_scene.h)
    std::unique_ptr<BattleStage> m_upBattleStage;   // the battle's 3D stage (battle_stage.h), kept between battles
    bool m_bBattle3DWait = false;                   // SessionOptions::m_bBattle3DWait
    double m_dUiTime = 0;
    std::string m_strUiLanguage;                     // language the UI font was last chosen for
    uint32_t m_uPinW = 0, m_uPinH = 0;               // SessionOptions::designW / H
    toms::Canvas m_oCanvas;                        // the canvas now (BgfxRenderer::kDesignW / H, the UI's size)
    void f_FitCanvas(uint32_t l_uDeviceW, uint32_t l_uDeviceH);   // the canvas for the screen; on a change, everyone is told
};

}  // namespace toms::next
