// game_session.cpp -- see game_session.h. The input block was ported from the old GLFW main
// (src/game/core/main.cpp, removed 2026-09-27; GLFW keys -> toms::next::Key).
#include "game_session.h"
#include "title_scene.h"

#include "bgfx_host.h"       // the F3 HUD: debug text, backend name, compute caps
#include "bgfx_renderer.h"
#include "game.h"
#include "game_ui.h"
#include "log.h"
#include "rml_ui.h"
#include "job_system.h"
#include "save_slots.h"
#include "vfs.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>   // f_NextTitleBackground: localStorage
#endif

namespace fs = std::filesystem;

namespace toms::next {

namespace {

// The title's background, in turns: no value yet = 0; else the value + 1, kept for the next start. Even: the sea, odd: the
// model. The web keeps it in the browser's localStorage ("background"), the desktop in a file beside the saves.
int f_NextTitleBackground() {
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({
        var l_iNext = 0;
        try {
            var l_s = localStorage.getItem('background');
            if (l_s !== null && /^[0-9]+$/.test(l_s)) l_iNext = parseInt(l_s) + 1;
            localStorage.setItem('background', String(l_iNext));
        } catch (e) {}
        return l_iNext;
    });
#else
    const std::string l_strPath = toms::f_DefaultSaveDir() + "/title_background.txt";
    int l_iNext = 0, l_iValue = 0;
    {
        std::ifstream l_oIn(l_strPath);
        if (l_oIn >> l_iValue && l_iValue >= 0) l_iNext = l_iValue + 1;
    }
    std::error_code l_oEc;
    fs::create_directories(toms::f_DefaultSaveDir(), l_oEc);
    std::ofstream l_oOut(l_strPath);
    l_oOut << l_iNext << "\n";
    return l_iNext;
#endif
}

}  // namespace

GameSession::GameSession() = default;
GameSession::~GameSession() { f_Stop(); }

std::string GameSession::f_DefaultAssetDir() {
    if (const char* l_pszEnv = std::getenv("ASSET_DIR")) return l_pszEnv;
#ifdef TOMS_DEFAULT_ASSET_DIR
    return TOMS_DEFAULT_ASSET_DIR;
#else
    return "assets";
#endif
}

bool GameSession::f_Start(const SessionOptions& l_oOpts, std::string& l_strError) {
    f_Stop();
    m_bDebugUi = l_oOpts.m_bEnableDebugUi;
    m_strAssetDir = l_oOpts.m_strAssetDir.empty() ? f_DefaultAssetDir() : l_oOpts.m_strAssetDir;
    m_uPinW = l_oOpts.m_uDesignW;
    m_uPinH = l_oOpts.m_uDesignH;
    m_oCanvas = m_uPinW && m_uPinH ? toms::Canvas{m_uPinW, m_uPinH} : toms::Canvas{};   // the screen's own on the first frame
    BgfxRenderer::f_SetDesignSizeExact(m_oCanvas.m_uW, m_oCanvas.m_uH);

    // vfs, not std::filesystem: on Android these are entries inside the APK (folders via vfsListDir).
    if (toms::f_VfsListDir(m_strAssetDir + "/atlas").empty()) {
        l_strError = "Game assets were not found at:\n  " + m_strAssetDir +
                "\n\nExpected the game's media folder (assets/media, with atlas/ and fonts/). "
                "Set the environment variable ASSET_DIR, or restore the content with: git checkout -- assets";
        return false;
    }
    if (toms::f_VfsListDir(m_strAssetDir + "/../data/stages").empty()) {
        l_strError = "Game data was not found next to the assets folder:\n  " +
                (fs::path(m_strAssetDir) / ".." / "data").lexically_normal().string();
        return false;
    }
#ifdef __EMSCRIPTEN__
    const bool l_bUiFiles = toms::f_VfsExists(m_strAssetDir + "/ui/hud.rml");   // the browser draws the text
#else
    const bool l_bUiFiles = toms::f_VfsExists(f_UiFontPath(m_strAssetDir)) && toms::f_VfsExists(m_strAssetDir + "/ui/hud.rml");
#endif
    if (!l_bUiFiles) {
        l_strError = "The UI files were not found in\n  " + m_strAssetDir +
                "\n\nExpected fonts/NotoSansCJKtc-TOMS.otf and ui/*.rml (restore them with: git checkout -- assets)";
        return false;
    }

    if (m_bDebugUi && !m_oImgui.f_Init()) {
        l_strError = "ImGui could not be initialized (shader program missing for this renderer).";
        return false;
    }

    toms::JobSystem::f_Start();   // worker threads for parallel work (once; none in the single-threaded web build)
    toms::Logger::f_Instance().f_SetFile("toms.log");
    toms::Logger::f_Instance().f_SetLevel(toms::LogLevel::Info);
    TOMS_LOG_INFO("TOMS start (bgfx host, C++{})", __cplusplus / 100);

    m_upGame = std::make_unique<Game>();
    if (l_oOpts.m_fUiScale != 1.0f) m_upGame->f_SetUiScale(l_oOpts.m_fUiScale);
    if (!m_upGame->f_LoadAssets(m_strAssetDir)) {   // creates the renderer: `new Renderer()` = BgfxRenderer
        l_strError = "Game::loadAssets failed for\n  " + m_strAssetDir + "\nSee the console / toms.log for the file that failed.";
        m_pRenderer = dynamic_cast<BgfxRenderer*>(m_upGame->f_Renderer());
        f_Stop();
        return false;
    }
    m_pRenderer = dynamic_cast<BgfxRenderer*>(m_upGame->f_Renderer());
    m_upGame->f_SetHotReload(l_oOpts.m_bHotReload);
    m_bWatchUi = l_oOpts.m_bHotReload;
    if (!m_pRenderer) {
        l_strError = "internal: Game did not create a BgfxRenderer (is game/compat first on the include path?)";
        f_Stop();
        return false;
    }
    if (const char* l_pszHm = std::getenv("TOMS_HIDE")) m_upGame->m_iHideMask = std::atoi(l_pszHm);
    if (const char* l_pszSn = std::getenv("TOMS_SPLIT_NODE")) m_pRenderer->f_SetNodeFilter((uint8_t)std::atoi(l_pszSn));
    m_bShowHud = l_oOpts.m_bShowFps;
    if (l_oOpts.m_iFxGpuThreshold >= 0) m_upGame->f_OverrideParticleGpuThreshold(l_oOpts.m_iFxGpuThreshold);
    if (l_oOpts.m_bDefaultBattleTuning) m_upGame->f_UseDefaultBattleTuning();
    if (l_oOpts.m_bBattleLog) m_upGame->f_SetBattleLogToStderr(true);
    m_upGame->f_SetShowVersion(l_oOpts.m_bShowVersion);
    // The 3D scene behind the title: the sea, or a model; without either the title is plain.
    bool l_bOcean = l_oOpts.m_strTitleScene == "ocean";
    if (l_oOpts.m_bTitleSceneInTurns && l_oOpts.m_strTitleScene.empty()) l_bOcean = f_NextTitleBackground() % 2 == 0;
    if (l_bOcean) {
        m_upTitleScene = std::make_unique<TitleScene>();
        std::string l_strWhy;
        if (!m_upTitleScene->f_LoadOcean(l_strWhy)) {
            std::fprintf(stderr, "[title] no ocean: %s\n", l_strWhy.c_str());
            m_upTitleScene->f_Shutdown();
            m_upTitleScene.reset();
        }
    }
    if (!m_upTitleScene && l_oOpts.m_strTitleScene != "none") {   // (the sea could not start: the model after all)
        const bool l_bDefault = l_oOpts.m_strTitleScene.empty() || l_oOpts.m_strTitleScene == "ocean";
        const std::string l_strPath = l_bDefault ? m_strAssetDir + "/models/VirtualCity.glb" : l_oOpts.m_strTitleScene;
        if (!l_bDefault || toms::f_VfsExists(l_strPath)) {
            m_upTitleScene = std::make_unique<TitleScene>();
            std::string l_strWhy;
            if (!m_upTitleScene->f_Load(l_strPath, l_strWhy)) {
                std::fprintf(stderr, "[title] no 3D scene: %s\n", l_strWhy.c_str());
                m_upTitleScene->f_Shutdown();
                m_upTitleScene.reset();
            }
        }
    }
    f_BgfxHostSetHud(m_bShowHud);
    if (l_oOpts.m_strSpritePath != "auto") {   // --sprite-path / --no-instancing
        using P = BgfxRenderer::SpritePath;
        const P l_eP = l_oOpts.m_strSpritePath == "compute" ? P::Compute : l_oOpts.m_strSpritePath == "instancing" ? P::Instancing
                  : l_oOpts.m_strSpritePath == "cpu"     ? P::Cpu     : P::Auto;
        m_pRenderer->f_SetSpritePath(l_eP);
        std::fprintf(stderr, "[toms] sprite batch: %s (asked for %s)\n", BgfxRenderer::f_SpritePathName(m_pRenderer->f_SpritePath()),
                     l_oOpts.m_strSpritePath.c_str());
    }
    m_upGame->f_LoadStage(l_oOpts.m_strStartStage);
    m_arrKeyWas.fill(false);
    m_upGame->f_SetPreviewCharacter(l_oOpts.m_strPreviewCharacter);   // --character (before the clip starts: it is fitted then)
    m_upGame->f_SetPreviewHeld(l_oOpts.m_strPreviewHeld);             // --held (bound when the clip starts)
    m_upGame->f_SetPreviewCrowd(l_oOpts.m_iPreviewCrowd);           // --anim-crowd
    if (!l_oOpts.m_strPreviewAnim.empty()) {   // --anim=<file>#<clip> (a developer/test option)
        const size_t l_nHash = l_oOpts.m_strPreviewAnim.rfind('#');
        std::string l_strAnimError;
        if (l_nHash == std::string::npos ||
            !m_upGame->f_PlayPreviewAnim(l_oOpts.m_strPreviewAnim.substr(0, l_nHash), l_oOpts.m_strPreviewAnim.substr(l_nHash + 1), l_strAnimError))
            std::fprintf(stderr, "[anim] --anim=%s: %s\n", l_oOpts.m_strPreviewAnim.c_str(),
                         l_nHash == std::string::npos ? "expected <file>#<clip>" : l_strAnimError.c_str());
    }
    if (!l_oOpts.m_strPreviewFx.empty()) {     // --fx=<file>#<effect> (a developer/test option)
        const size_t l_nHash = l_oOpts.m_strPreviewFx.rfind('#');
        std::string l_strFxError;
        if (l_nHash == std::string::npos ||
            !m_upGame->f_PlayPreviewFx(l_oOpts.m_strPreviewFx.substr(0, l_nHash), l_oOpts.m_strPreviewFx.substr(l_nHash + 1), l_strFxError))
            std::fprintf(stderr, "[fx] --fx=%s: %s\n", l_oOpts.m_strPreviewFx.c_str(),
                         l_nHash == std::string::npos ? "expected <file>#<effect>" : l_strFxError.c_str());
    }
    if (!l_oOpts.m_strPreviewMesh.empty()) {   // --mesh-test=<sprite> (a developer/test option)
        std::string l_strMeshError;
        if (!m_upGame->f_PlayPreviewMesh(l_oOpts.m_strPreviewMesh, l_strMeshError))
            std::fprintf(stderr, "[mesh] --mesh-test=%s: %s\n", l_oOpts.m_strPreviewMesh.c_str(), l_strMeshError.c_str());
    }

    // The UI: RmlUi on bgfx (view kViewUi), every screen bound to the game (game_ui.h).
    m_upRml = std::make_unique<RmlUi>();
    m_upUi = std::make_unique<GameUi>();
    if (!m_upRml->f_Init((int)BgfxRenderer::m_uDesignW, (int)BgfxRenderer::m_uDesignH, f_UiFontPath(m_strAssetDir), l_strError) ||
        !m_upUi->f_Init(m_upRml->f_Context(), m_upGame.get(), (fs::path(m_strAssetDir) / "ui").string(), l_strError)) {
        l_strError = "The game UI could not start:\n" + l_strError;
        f_Stop();
        return false;
    }
    m_strUiLanguage.clear();
    f_ApplyLanguageFont();
    return true;
}

void GameSession::f_ReloadUi() {
    if (m_upUi) m_upUi->f_Reload();
}

// The UI font follows the game's language (text.json: "font" on desktop, "web_font" on the web).
void GameSession::f_ApplyLanguageFont() {
    const toms::Locale& l_oL = m_upGame->f_Locale();
    if (l_oL.f_LanguageCode() == m_strUiLanguage) return;
    m_strUiLanguage = l_oL.f_LanguageCode();
    const toms::LanguageInfo* l_pInfo = nullptr;
    for (const auto& l_oLi : l_oL.f_Languages()) if (l_oLi.m_strCode == m_strUiLanguage) l_pInfo = &l_oLi;
    std::string l_strFile;
    if (l_pInfo && !l_pInfo->m_strFont.empty()) {
        fs::path l_oP(l_pInfo->m_strFont);
        l_strFile = (l_oP.is_absolute() ? l_oP : fs::path(m_strAssetDir) / l_oP).generic_string();
    }
    m_upUi->f_SetFontFamily(m_upRml->f_LanguageFont(m_strUiLanguage, l_strFile, l_pInfo ? l_pInfo->m_strWebFont : std::string()));
}

std::string GameSession::f_UiFontPath(const std::string& l_strAssetDir) {
    return (fs::path(l_strAssetDir) / "fonts" / "NotoSansCJKtc-TOMS.otf").generic_string();
}

void GameSession::f_Stop() {
    // Game never deletes its renderer (the old main called Renderer::destroy() by hand), so the
    // session frees it. Order: the UI (it points at Game and owns bgfx handles), Game, then the
    // renderer's GPU handles.
    if (m_upTitleScene) m_upTitleScene->f_Shutdown();   // its GPU buffers, while bgfx is up
    m_upTitleScene.reset();
    if (m_upUi) m_upUi->f_Shutdown();
    if (m_upRml) m_upRml->f_Shutdown();
    m_upUi.reset();
    m_upRml.reset();
    m_upGame.reset();
    if (m_pRenderer) {
        m_pRenderer->f_Destroy();
        delete m_pRenderer;
        m_pRenderer = nullptr;
    }
    if (m_oImgui.f_Ready()) m_oImgui.f_Shutdown();
}

bool GameSession::f_KeyPressed(const InputState& l_oIn, Key l_eK) {
    const bool l_bNow = l_oIn.f_IsDown(l_eK);
    const bool l_bFired = l_bNow && !m_arrKeyWas[(size_t)l_eK];
    m_arrKeyWas[(size_t)l_eK] = l_bNow;
    return l_bFired;
}

// The design canvas for the screen (canvas.h): when the screen turns or the window changes shape, the renderer, the UI
// (its documents lay out again and match their @media rules) and the game (the camera) move to the new one at once.
void GameSession::f_FitCanvas(uint32_t l_uDeviceW, uint32_t l_uDeviceH) {
    const toms::Canvas l_oWant = m_uPinW && m_uPinH ? toms::Canvas{m_uPinW, m_uPinH} : toms::f_CanvasFor(l_uDeviceW, l_uDeviceH);
    if (l_oWant == m_oCanvas) return;
    m_oCanvas = l_oWant;
    BgfxRenderer::f_SetDesignSizeExact(l_oWant.m_uW, l_oWant.m_uH);
    if (m_upRml) m_upRml->f_SetDesignSize((int)l_oWant.m_uW, (int)l_oWant.m_uH);
    if (m_upGame) m_upGame->f_CanvasChanged();
    std::fprintf(stderr, "[toms] canvas %ux%u (%s)\n", l_oWant.m_uW, l_oWant.m_uH, l_oWant.f_Portrait() ? "portrait" : "landscape");
}

bool GameSession::f_Frame(int l_iDtMs, const InputState& l_oIn, uint32_t l_uDeviceW, uint32_t l_uDeviceH) {
    if (!m_upGame) return true;
    Game& l_oG = *m_upGame;
    m_pRenderer->f_SetDeviceSize(l_uDeviceW, l_uDeviceH);
    f_FitCanvas(l_uDeviceW, l_uDeviceH);
    if (l_iDtMs > 0) l_oG.f_Update(l_iDtMs);

    bool l_bKeepRunning = true;
    // Query each key exactly once per frame (keyPressed is an edge trigger with state).
    const bool l_bUpPressed    = f_KeyPressed(l_oIn, Key::Up)    || f_KeyPressed(l_oIn, Key::W);
    const bool l_bDownPressed  = f_KeyPressed(l_oIn, Key::Down)  || f_KeyPressed(l_oIn, Key::S);
    const bool l_bLeftPressed  = f_KeyPressed(l_oIn, Key::Left)  || f_KeyPressed(l_oIn, Key::A);
    const bool l_bRightPressed = f_KeyPressed(l_oIn, Key::Right) || f_KeyPressed(l_oIn, Key::D);
    const bool l_bEnterPressed = f_KeyPressed(l_oIn, Key::Enter);
    const bool l_bSpacePressed = f_KeyPressed(l_oIn, Key::Space);
    const bool l_bEscPressed   = f_KeyPressed(l_oIn, Key::Escape);

    if (f_KeyPressed(l_oIn, Key::F5)) m_upUi->f_Reload();              // re-read assets/media/ui/*.rml/.rcss
    if (m_bWatchUi && (m_iUiWatchMs += std::max(l_iDtMs, 1)) >= 500) {   // hot reload: a saved .rml / .rcss shows at once
        m_iUiWatchMs = 0;
        const std::string l_strChanged = m_upUi->f_ReloadIfChanged();
        if (!l_strChanged.empty()) l_oG.f_ShowNotice("UI reloaded: " + l_strChanged);
    }
    if (f_KeyPressed(l_oIn, Key::F8)) m_upRml->f_ToggleDebugger();     // RmlUi's element/style inspector
    if (l_oG.f_TitleOpen()) {
        if (l_bUpPressed)    l_oG.f_TitleMove(0, -1);
        if (l_bDownPressed)  l_oG.f_TitleMove(0,  1);
        if (l_bLeftPressed)  l_oG.f_TitleMove(-1, 0);
        if (l_bRightPressed) l_oG.f_TitleMove( 1, 0);
        if (l_bEnterPressed || l_bSpacePressed) l_oG.f_TitleConfirm();
        if (l_bEscPressed) {
            if (l_oG.f_Title().f_Page() == toms::TitlePage::Menu) l_bKeepRunning = false;
            else l_oG.f_TitleCancel();
        }
    }
    // A screen opened by this frame's key (Enter on the System tab's Store row) does not also take
    // that key: the store only reads keys when it was already open at the start of the frame.
    const bool l_bStoreWasOpen = l_oG.f_StoreModal();
    if (!l_oG.f_TitleOpen()) {
        if (f_KeyPressed(l_oIn, Key::F1)) m_bShowDebugOverlay = !m_bShowDebugOverlay;
        if (f_KeyPressed(l_oIn, Key::F2)) m_bShowStylingSpike = !m_bShowStylingSpike;
        if (l_bEscPressed) {
            if (l_oG.f_MiniGameOpen()) l_oG.f_CloseMiniGame();
            else if (l_oG.f_EndingActive()) l_oG.f_DismissEndingScreen();
            else if (l_oG.f_NoticeOpen()) l_oG.f_DismissNotice();
            else if (l_oG.f_StoreModal()) l_oG.f_StoreKey(27);
            else if (l_oG.f_StairsConfirmOpen()) l_oG.f_CancelStageTransition();
            else if (l_oG.f_StageSelectOpen()) l_oG.f_CloseStageSelect();
            else if (l_oG.f_PlayerMenuOpen()) l_oG.f_PlayerMenuBack();
            else if (!l_oG.f_ModalActive()) { l_oG.f_CancelWalk(); l_oG.f_OpenPlayerMenu(); }
        }
        if (f_KeyPressed(l_oIn, Key::Tab)) {
            if (l_oG.f_PlayerMenuOpen()) l_oG.f_PlayerMenuTab(1);
            else if (l_oG.f_StageSelectOpen()) l_oG.f_CloseStageSelect();
            else if (!l_oG.f_ModalActive()) l_oG.f_OpenStageSelect();
        }
        if (l_oG.f_PlayerMenuOpen() && f_KeyPressed(l_oIn, Key::Q)) l_oG.f_PlayerMenuTab(-1);
        if (l_oG.f_PlayerMenuOpen() && f_KeyPressed(l_oIn, Key::E)) l_oG.f_PlayerMenuTab(1);
        if (f_KeyPressed(l_oIn, Key::C)) {   // the Status tab (closes the menu when it is already there)
            using Tab = ::Game::MenuTab;
            if (l_oG.f_PlayerMenuOpen() && l_oG.f_PlayerMenuTab() == Tab::Status) l_oG.f_ClosePlayerMenu();
            else if (l_oG.f_PlayerMenuOpen() || !l_oG.f_ModalActive()) { l_oG.f_CancelWalk(); l_oG.f_OpenPlayerMenu((int)Tab::Status); }
        }
        {   // held movement repeats (level state, not edge)
            int l_iYdir = 0;
            if (l_oIn.f_IsDown(Key::Up) || l_oIn.f_IsDown(Key::W)) l_iYdir = -1;
            else if (l_oIn.f_IsDown(Key::Down) || l_oIn.f_IsDown(Key::S)) l_iYdir = 1;
            l_oG.f_SetMoveHeldY(l_iYdir);
            int l_iXdir = 0;
            if (l_oIn.f_IsDown(Key::Left) || l_oIn.f_IsDown(Key::A)) l_iXdir = -1;
            else if (l_oIn.f_IsDown(Key::Right) || l_oIn.f_IsDown(Key::D)) l_iXdir = 1;
            l_oG.f_SetMoveHeldX(l_iXdir);
        }
        if (l_oG.f_InDialogueFlag()) {
            if (l_bUpPressed)   l_oG.f_DlgMoveSel(-1);
            if (l_bDownPressed) l_oG.f_DlgMoveSel(1);
        }
        if (l_bEnterPressed || l_bSpacePressed) {
            if (l_oG.f_EndingActive()) { if (!l_oG.f_Rebirth()) l_oG.f_DismissEndingScreen(); }
            else if (l_oG.f_NoticeOpen()) l_oG.f_DismissNotice();
            else if (l_oG.f_PlayerMenuOpen()) l_oG.f_PlayerMenuActivate();
            else if (l_oG.f_StairsConfirmOpen()) l_oG.f_ConfirmStageTransition();
            else if (l_oG.f_MiniGameOpen()) l_oG.f_MiniGameTap();
            else if (l_oG.f_CombatWon()) l_oG.f_DismissVictory();
            else if (l_oG.f_CombatActive()) l_oG.f_BattleTapAttack();
            else if (l_oG.f_InDialogueFlag()) l_oG.f_ChooseDialogue(l_oG.f_DialogueSel());
            else l_oG.f_Interact();
        }
        if (f_KeyPressed(l_oIn, Key::F) && l_oG.f_CombatActive()) l_oG.f_BattleTapDefense();
        if (l_oG.f_CombatActive() && (l_bLeftPressed || l_bRightPressed)) l_oG.f_BattleCycleTarget(l_bLeftPressed ? -1 : 1);   // the target
        if (f_KeyPressed(l_oIn, Key::G) && l_oG.f_CombatActive()) l_oG.f_BattleTapSuper();
        if (f_KeyPressed(l_oIn, Key::H) && l_oG.f_CombatActive()) l_oG.f_BattleTapActive();
        if (f_KeyPressed(l_oIn, Key::I)) l_oG.f_ToggleInventory();
        if (f_KeyPressed(l_oIn, Key::B) && !l_oG.f_ModalActive()) l_oG.f_OpenStore();
        if (l_oG.f_PlayerMenuOpen()) {
            if (l_bUpPressed)    l_oG.f_PlayerMenuMove(0, -1);
            if (l_bDownPressed)  l_oG.f_PlayerMenuMove(0,  1);
            if (l_bLeftPressed)  l_oG.f_PlayerMenuMove(-1, 0);
            if (l_bRightPressed) l_oG.f_PlayerMenuMove( 1, 0);
        }
        if (l_oG.f_StoreModal() && l_bStoreWasOpen) {
            for (int l_iK = 0; l_iK < 9; ++l_iK)
                if (f_KeyPressed(l_oIn, (Key)((int)Key::Num1 + l_iK))) l_oG.f_StoreKey((char)('1' + l_iK));
            if (l_bEnterPressed) l_oG.f_StoreKey(13);
            if (l_bLeftPressed)  l_oG.f_StoreKey(263);
            if (l_bRightPressed) l_oG.f_StoreKey(262);
        }
    }

    // Mouse: the UI first (design space). A press that lands on no UI element -- the HUD is
    // pointer-events: none except its buttons -- goes on to the map (Game::handleTouch).
    {
        Rml::Context* l_pCtx = m_upRml->f_Context();
        float l_fBx = 0, l_fBy = 0;
        const bool l_bOnGame = l_oIn.m_bHasMouse && m_pRenderer->f_DeviceToDesign(l_oIn.m_fMouseX, l_oIn.m_fMouseY, l_fBx, l_fBy);
        if (l_bOnGame) l_pCtx->ProcessMouseMove((int)l_fBx, (int)l_fBy, 0);
        if (l_bOnGame) {   // the battle's own plays read the pointer themselves (touch_play.h), but not a press on a button
            int l_iPhase = l_oIn.m_bMouseLeft ? (m_bMouseWasDown ? 1 : 0) : (m_bMouseWasDown ? 2 : -1);
            if (l_iPhase == 0)
                for (Rml::Element* l_pE = l_pCtx->GetHoverElement(); l_pE; l_pE = l_pE->GetParentNode())
                    if (l_pE->IsClassSet("btn")) { l_iPhase = -1; break; }
            if (l_iPhase >= 0) l_oG.f_BattlePointer(l_iPhase, l_fBx, l_fBy);
        }
        if (l_oIn.m_bMouseLeft && !m_bMouseWasDown) {
            const bool l_bFree = l_pCtx->ProcessMouseButtonDown(0, 0);
            m_bMousePressFree = l_bFree && l_bOnGame;
            if (m_bMousePressFree) l_oG.f_HandleTouch(l_fBx, l_fBy, 0);
        } else if (!l_oIn.m_bMouseLeft && m_bMouseWasDown) {
            l_pCtx->ProcessMouseButtonUp(0, 0);
            if (m_bMousePressFree) l_oG.f_HandleTouch(l_fBx, l_fBy, 2);
            m_bMousePressFree = false;
        }
        if (l_oIn.m_fWheel != 0) l_pCtx->ProcessMouseWheel(-l_oIn.m_fWheel, 0);
        m_bMouseWasDown = l_oIn.m_bMouseLeft;
    }

    // Same order as the old main: ImGui frame, dev windows, ImGui render, then the game draw.
    if (m_bDebugUi) {
        ImGuiBgfx::Input l_oMi;
        l_oMi.m_fMouseX = l_oIn.m_fMouseX; l_oMi.m_fMouseY = l_oIn.m_fMouseY; l_oMi.m_bHasMouse = l_oIn.m_bHasMouse;
        l_oMi.m_arrMouseDown[0] = l_oIn.m_bMouseLeft; l_oMi.m_arrMouseDown[1] = l_oIn.m_bMouseRight; l_oMi.m_arrMouseDown[2] = l_oIn.m_bMouseMiddle;
        l_oMi.m_fWheel = l_oIn.m_fWheel;
        m_oImgui.f_NewFrame(l_uDeviceW, l_uDeviceH, l_iDtMs / 1000.0f, l_oMi);
        l_oG.f_ApplyUiSettings();
        if (m_bShowDebugOverlay) l_oG.f_DrawDebugOverlay();
        l_oG.f_SetStylingSpikeVisible(false);
        if (m_bShowStylingSpike) l_oG.f_DrawStylingSpike();
    }
    // The title's 3D scene: its own views, run between the screen clear and the sprites.
    const bool l_bScene = m_upTitleScene && l_oG.f_TitleOpen();
    if (l_bScene) m_upTitleScene->f_Draw(l_iDtMs / 1000.0, l_uDeviceW, l_uDeviceH);
    m_pRenderer->f_SetSceneViews(TitleScene::m_u16FirstView, l_bScene ? m_upTitleScene->f_ViewCount() : 0);
    if (m_upUi) m_upUi->f_SetTitleScene(l_bScene);
    l_oG.f_Draw();   // the world -> BgfxRenderer::end(): views kViewClear + kViewGame
    {           // the UI over it, under ImGui
        const auto l_oVp = BgfxRenderer::f_ComputeAspectFitViewport(l_uDeviceW, l_uDeviceH, BgfxRenderer::m_uDesignW, BgfxRenderer::m_uDesignH);
        m_upRml->f_SetViewport(l_oVp.m_fX, l_oVp.m_fY, l_oVp.m_fWidth, l_oVp.m_fHeight);
        f_ApplyLanguageFont();
        m_upUi->f_Sync();
        m_dUiTime += l_iDtMs / 1000.0;
        m_upRml->f_Update(m_dUiTime);
        m_upRml->f_Render(BgfxRenderer::m_u16ViewUi);
    }
    if (m_bDebugUi) m_oImgui.f_Render(BgfxRenderer::m_u16ViewOverlay);
    if (f_KeyPressed(l_oIn, Key::F3)) {   // anywhere, title included
        m_bShowHud = !m_bShowHud;
        f_BgfxHostSetHud(m_bShowHud);
    }
    f_DrawHud();
    return l_bKeepRunning;
}

// FPS 60.0 (16.7 ms) | Direct3D 11 | sprites: GPU instancing | compute: yes | 1234 quads, 3 draws
void GameSession::f_DrawHud() {
    const double l_dNow = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (m_dHudClock < 0) m_dHudClock = l_dNow;
    m_iHudFrames++;
    if (l_dNow - m_dHudClock >= 0.5 || (m_dHudFps == 0 && m_iHudFrames >= 10 && l_dNow > m_dHudClock)) {   // a first value soon
        m_dHudFps = m_iHudFrames / (l_dNow - m_dHudClock);
        m_dHudMs = (l_dNow - m_dHudClock) * 1000.0 / m_iHudFrames;
        m_dHudClock = l_dNow;
        m_iHudFrames = 0;
    }
    if (!m_bShowHud || !m_pRenderer) return;
    char l_pszLine[256];
    // "quads drawn/asked": a frame that could not draw everything shows it.
    std::snprintf(l_pszLine, sizeof l_pszLine,
                  "FPS %5.1f (%5.2f ms) | %s | sprites: %s | compute: %s | %zu/%zu quads, %u draws | fx %d alive, GPU %d/%d emitters (>%d)",
                  m_dHudFps, m_dHudMs, f_BgfxHostRendererName().c_str(),
                  BgfxRenderer::f_SpritePathName(m_pRenderer->f_SpritePath()),
                  f_BgfxHostSupportsCompute() ? "yes" : "no", m_pRenderer->f_LastDrawnQuads(), m_pRenderer->f_LastQuadCount(),
                  m_pRenderer->f_LastDrawCalls(), m_upGame ? m_upGame->f_PreviewFxLive() : 0, m_upGame ? m_upGame->f_PreviewFxGpuEmitters() : 0,
                  m_upGame ? m_upGame->f_PreviewFxEmitters() : 0, m_upGame ? m_upGame->f_ParticleGpuThreshold() : 0);
    f_BgfxHostHudText(l_pszLine);
}

}  // namespace toms::next
