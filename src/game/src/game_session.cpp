// game_session.cpp -- see game_session.h. The input block was ported from the old GLFW main
// (src/game/core/main.cpp, removed 2026-09-27; GLFW keys -> toms::next::Key).
#include "game_session.h"

#include "bgfx_renderer.h"
#include "game.h"
#include "game_ui.h"
#include "log.h"
#include "rml_ui.h"
#include "job_system.h"
#include "vfs.h"

#include <cstdlib>
#include <filesystem>
#include <imgui.h>
#include <vector>

namespace fs = std::filesystem;

namespace toms::next {

GameSession::GameSession() = default;
GameSession::~GameSession() { stop(); }

std::string GameSession::defaultAssetDir() {
    if (const char* env = std::getenv("ASSET_DIR")) return env;
#ifdef TOMS_DEFAULT_ASSET_DIR
    return TOMS_DEFAULT_ASSET_DIR;
#else
    return "assets";
#endif
}

bool GameSession::start(const SessionOptions& opts, std::string& error) {
    stop();
    debugUi_ = opts.enableDebugUi;
    assetDir_ = opts.assetDir.empty() ? defaultAssetDir() : opts.assetDir;

    // vfs, not std::filesystem: on Android these are entries inside the APK (folders via vfsListDir).
    if (toms::vfsListDir(assetDir_ + "/sprites").empty()) {
        error = "Game assets were not found at:\n  " + assetDir_ +
                "\n\nExpected the game's media folder (assets/media, with sprites/ and fonts/). "
                "Set the environment variable ASSET_DIR, or restore the content with: git checkout -- assets";
        return false;
    }
    if (toms::vfsListDir(assetDir_ + "/../data/stages").empty()) {
        error = "Game data was not found next to the assets folder:\n  " +
                (fs::path(assetDir_) / ".." / "data").lexically_normal().string();
        return false;
    }
#ifdef __EMSCRIPTEN__
    const bool uiFiles = toms::vfsExists(assetDir_ + "/ui/hud.rml");   // the browser draws the text
#else
    const bool uiFiles = toms::vfsExists(uiFontPath(assetDir_)) && toms::vfsExists(assetDir_ + "/ui/hud.rml");
#endif
    if (!uiFiles) {
        error = "The UI files were not found in\n  " + assetDir_ +
                "\n\nExpected fonts/NotoSansCJKtc-TOMS.otf and ui/*.rml (restore them with: git checkout -- assets)";
        return false;
    }

    if (debugUi_ && !imgui_.init()) {
        error = "ImGui could not be initialized (shader program missing for this renderer).";
        return false;
    }

    toms::JobSystem::start();   // worker threads for parallel work (once; none in the single-threaded web build)
    toms::Logger::instance().setFile("toms.log");
    toms::Logger::instance().setLevel(toms::LogLevel::Info);
    TOMS_LOG_INFO("TOMS start (bgfx host, C++{})", __cplusplus / 100);

    game_ = std::make_unique<Game>();
    if (opts.uiScale != 1.0f) game_->setUiScale(opts.uiScale);
    if (!game_->loadAssets(assetDir_)) {   // creates the renderer: `new Renderer()` = BgfxRenderer
        error = "Game::loadAssets failed for\n  " + assetDir_ + "\nSee the console / toms.log for the file that failed.";
        renderer_ = dynamic_cast<BgfxRenderer*>(game_->renderer());
        stop();
        return false;
    }
    renderer_ = dynamic_cast<BgfxRenderer*>(game_->renderer());
    if (!renderer_) {
        error = "internal: Game did not create a BgfxRenderer (is game/compat first on the include path?)";
        stop();
        return false;
    }
    if (const char* hm = std::getenv("TOMS_HIDE")) game_->hideMask = std::atoi(hm);
    if (const char* sn = std::getenv("TOMS_SPLIT_NODE")) renderer_->setNodeFilter((uint8_t)std::atoi(sn));
    game_->loadStage(opts.startStage);
    keyWas_.fill(false);

    // The UI: RmlUi on bgfx (view kViewUi), every screen bound to the game (game_ui.h).
    rml_ = std::make_unique<RmlUi>();
    ui_ = std::make_unique<GameUi>();
    if (!rml_->init((int)BgfxRenderer::kDesignW, (int)BgfxRenderer::kDesignH, uiFontPath(assetDir_), error) ||
        !ui_->init(rml_->context(), game_.get(), (fs::path(assetDir_) / "ui").string(), error)) {
        error = "The game UI could not start:\n" + error;
        stop();
        return false;
    }
    uiLanguage_.clear();
    applyLanguageFont();
    return true;
}

// The UI font follows the game's language (text.json: "font" on desktop, "web_font" on the web).
void GameSession::applyLanguageFont() {
    const toms::Locale& L = game_->locale();
    if (L.languageCode() == uiLanguage_) return;
    uiLanguage_ = L.languageCode();
    const toms::LanguageInfo* info = nullptr;
    for (const auto& li : L.languages()) if (li.code == uiLanguage_) info = &li;
    std::string file;
    if (info && !info->font.empty()) {
        fs::path p(info->font);
        file = (p.is_absolute() ? p : fs::path(assetDir_) / p).generic_string();
    }
    ui_->setFontFamily(rml_->languageFont(uiLanguage_, file, info ? info->webFont : std::string()));
}

std::string GameSession::uiFontPath(const std::string& assetDir) {
    return (fs::path(assetDir) / "fonts" / "NotoSansCJKtc-TOMS.otf").generic_string();
}

void GameSession::stop() {
    // Game never deletes its renderer (the old main called Renderer::destroy() by hand), so the
    // session frees it. Order: the UI (it points at Game and owns bgfx handles), Game, then the
    // renderer's GPU handles.
    if (ui_) ui_->shutdown();
    if (rml_) rml_->shutdown();
    ui_.reset();
    rml_.reset();
    game_.reset();
    if (renderer_) {
        renderer_->destroy();
        delete renderer_;
        renderer_ = nullptr;
    }
    if (imgui_.ready()) imgui_.shutdown();
}

bool GameSession::keyPressed(const InputState& in, Key k) {
    const bool now = in.isDown(k);
    const bool fired = now && !keyWas_[(size_t)k];
    keyWas_[(size_t)k] = now;
    return fired;
}

bool GameSession::frame(int dtMs, const InputState& in, uint32_t deviceW, uint32_t deviceH) {
    if (!game_) return true;
    Game& g = *game_;
    renderer_->setDeviceSize(deviceW, deviceH);
    if (dtMs > 0) g.update(dtMs);

    bool keepRunning = true;
    // Query each key exactly once per frame (keyPressed is an edge trigger with state).
    const bool upPressed    = keyPressed(in, Key::Up)    || keyPressed(in, Key::W);
    const bool downPressed  = keyPressed(in, Key::Down)  || keyPressed(in, Key::S);
    const bool leftPressed  = keyPressed(in, Key::Left)  || keyPressed(in, Key::A);
    const bool rightPressed = keyPressed(in, Key::Right) || keyPressed(in, Key::D);
    const bool enterPressed = keyPressed(in, Key::Enter);
    const bool spacePressed = keyPressed(in, Key::Space);
    const bool escPressed   = keyPressed(in, Key::Escape);

    if (keyPressed(in, Key::F5)) ui_->reload();              // re-read assets/media/ui/*.rml/.rcss
    if (keyPressed(in, Key::F8)) rml_->toggleDebugger();     // RmlUi's element/style inspector
    if (g.titleOpen()) {
        if (upPressed)    g.titleMove(0, -1);
        if (downPressed)  g.titleMove(0,  1);
        if (leftPressed)  g.titleMove(-1, 0);
        if (rightPressed) g.titleMove( 1, 0);
        if (enterPressed || spacePressed) g.titleConfirm();
        if (escPressed) {
            if (g.title().page() == toms::TitlePage::Menu) keepRunning = false;
            else g.titleCancel();
        }
    }
    if (!g.titleOpen()) {
        if (keyPressed(in, Key::F1)) showDebugOverlay = !showDebugOverlay;
        if (keyPressed(in, Key::F2)) showStylingSpike = !showStylingSpike;
        if (escPressed) {
            if (g.endingActive()) g.dismissEndingScreen();
            else if (g.storeModal()) g.storeKey(27);
            else if (g.stairsConfirmOpen()) g.cancelStageTransition();
            else if (g.stageSelectOpen()) g.closeStageSelect();
            else if (g.inventoryOpen()) g.toggleInventory();
            else if (g.inGameMenuOpen()) g.inGameMenuBack();
            else if (!g.modalActive()) g.openInGameMenu();
        }
        if (keyPressed(in, Key::Tab)) {
            if (g.stageSelectOpen()) g.closeStageSelect();
            else if (!g.modalActive()) g.openStageSelect();
        }
        {   // held movement repeats (level state, not edge)
            int ydir = 0;
            if (in.isDown(Key::Up) || in.isDown(Key::W)) ydir = -1;
            else if (in.isDown(Key::Down) || in.isDown(Key::S)) ydir = 1;
            g.setMoveHeldY(ydir);
            int xdir = 0;
            if (in.isDown(Key::Left) || in.isDown(Key::A)) xdir = -1;
            else if (in.isDown(Key::Right) || in.isDown(Key::D)) xdir = 1;
            g.setMoveHeldX(xdir);
        }
        if (g.inDialogueFlag()) {
            if (upPressed)   g.dlgMoveSel(-1);
            if (downPressed) g.dlgMoveSel(1);
        }
        if (enterPressed || spacePressed) {
            if (g.endingActive()) { if (!g.rebirth()) g.dismissEndingScreen(); }
            else if (g.inGameMenuOpen()) g.inGameMenuActivate();
            else if (g.stairsConfirmOpen()) g.confirmStageTransition();
            else if (g.combatWon()) g.dismissVictory();
            else if (g.combatActive()) g.battleTapAttack();
            else if (g.inDialogueFlag()) g.chooseDialogue(g.dialogueSel());
            else g.interact();
        }
        if (keyPressed(in, Key::F) && g.combatActive()) g.battleTapDefense();
        if (keyPressed(in, Key::G) && g.combatActive()) g.battleTapSuper();
        if (keyPressed(in, Key::H) && g.combatActive()) g.battleTapActive();
        if (keyPressed(in, Key::I)) g.toggleInventory();
        if (keyPressed(in, Key::B) && !g.modalActive()) g.openStore();
        if (g.inGameMenuOpen()) {
            if (upPressed || leftPressed)    g.inGameMenuMove(-1);
            if (downPressed || rightPressed) g.inGameMenuMove(1);
        }
        if (g.inventoryOpen()) {
            if (upPressed)    g.invMoveSel(0, -1);
            if (downPressed)  g.invMoveSel(0,  1);
            if (leftPressed)  g.invMoveSel(-1, 0);
            if (rightPressed) g.invMoveSel( 1, 0);
            if (enterPressed) g.invUseSelected();
        }
        if (g.storeModal()) {
            for (int k = 0; k < 9; ++k)
                if (keyPressed(in, (Key)((int)Key::Num1 + k))) g.storeKey((char)('1' + k));
            if (enterPressed) g.storeKey(13);
            if (leftPressed)  g.storeKey(263);
            if (rightPressed) g.storeKey(262);
        }
    }

    // Mouse: the UI first (design space). A press that lands on no UI element -- the HUD is
    // pointer-events: none except its buttons -- goes on to the map (Game::handleTouch).
    {
        Rml::Context* ctx = rml_->context();
        float bx = 0, by = 0;
        const bool onGame = in.hasMouse && renderer_->deviceToDesign(in.mouseX, in.mouseY, bx, by);
        if (onGame) ctx->ProcessMouseMove((int)bx, (int)by, 0);
        if (in.mouseLeft && !mouseWasDown_) {
            const bool free = ctx->ProcessMouseButtonDown(0, 0);
            mousePressFree_ = free && onGame;
            if (mousePressFree_) g.handleTouch(bx, by, 0);
        } else if (!in.mouseLeft && mouseWasDown_) {
            ctx->ProcessMouseButtonUp(0, 0);
            if (mousePressFree_) g.handleTouch(bx, by, 2);
            mousePressFree_ = false;
        }
        if (in.wheel != 0) ctx->ProcessMouseWheel(-in.wheel, 0);
        mouseWasDown_ = in.mouseLeft;
    }

    // Same order as the old main: ImGui frame, dev windows, ImGui render, then the game draw.
    if (debugUi_) {
        ImGuiBgfx::Input mi;
        mi.mouseX = in.mouseX; mi.mouseY = in.mouseY; mi.hasMouse = in.hasMouse;
        mi.mouseDown[0] = in.mouseLeft; mi.mouseDown[1] = in.mouseRight; mi.mouseDown[2] = in.mouseMiddle;
        mi.wheel = in.wheel;
        imgui_.newFrame(deviceW, deviceH, dtMs / 1000.0f, mi);
        g.applyUiSettings();
        if (showDebugOverlay) g.drawDebugOverlay();
        g.setStylingSpikeVisible(false);
        if (showStylingSpike) g.drawStylingSpike();
    }
    g.draw();   // the world -> BgfxRenderer::end(): views kViewClear + kViewGame
    {           // the UI over it, under ImGui
        const auto vp = BgfxRenderer::computeAspectFitViewport(deviceW, deviceH, BgfxRenderer::kDesignW, BgfxRenderer::kDesignH);
        rml_->setViewport(vp.x, vp.y, vp.width, vp.height);
        applyLanguageFont();
        ui_->sync();
        uiTime_ += dtMs / 1000.0;
        rml_->update(uiTime_);
        rml_->render(BgfxRenderer::kViewUi);
    }
    if (debugUi_) imgui_.render(BgfxRenderer::kViewOverlay);
    return keepRunning;
}

}  // namespace toms::next
