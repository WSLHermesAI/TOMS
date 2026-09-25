// game_session.cpp -- see game_session.h. The input block is ported from src/game/core/main.cpp
// (GLFW keys -> toms::next::Key); keep the two in step until the old main is retired.
#include "game_session.h"

#include "bgfx_renderer.h"
#include "game.h"
#include "log.h"

#include <cstdlib>
#include <filesystem>
#include <imgui.h>

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

std::string GameSession::applyFontFallback(const std::string& assetDir) {
    if (std::getenv("TOMS_FONT")) return "";
    if (fs::exists(fs::path(assetDir) / "wqy-zenhei.ttc")) return "";
#ifdef _WIN32
    const char* windir = std::getenv("WINDIR");
    fs::path fallback = fs::path(windir ? windir : "C:\\Windows") / "Fonts" / "msjh.ttc";
    if (fs::exists(fallback)) {
        _putenv_s("TOMS_FONT", fallback.string().c_str());
        return "assets/wqy-zenhei.ttc is missing, so Microsoft JhengHei (msjh.ttc) is used instead.\n"
               "For the shipped look, download WenQuanYi Zen Hei and copy wqy-zenhei.ttc into TOMS/assets/:\n"
               "https://sourceforge.net/projects/wqy/files/wqy-zenhei/";
    }
#endif
    return "";
}

bool GameSession::start(const SessionOptions& opts, std::string& error) {
    stop();
    debugUi_ = opts.enableDebugUi;
    assetDir_ = opts.assetDir.empty() ? defaultAssetDir() : opts.assetDir;

    if (!fs::exists(fs::path(assetDir_) / "sprites")) {
        error = "Game assets were not found at:\n  " + assetDir_ +
                "\n\nExpected the TOMS 'assets' folder (with sprites/ and fonts/). "
                "Set the environment variable ASSET_DIR, or keep toms_next inside the TOMS checkout.";
        return false;
    }
    if (!fs::exists(fs::path(assetDir_) / ".." / "data" / "stages")) {
        error = "Game data was not found next to the assets folder:\n  " +
                (fs::path(assetDir_) / ".." / "data").lexically_normal().string();
        return false;
    }
    const bool hasFont = std::getenv("TOMS_FONT") || fs::exists(fs::path(assetDir_) / "wqy-zenhei.ttc");
    if (!hasFont) {
        error = "No CJK font is available.\n\nDownload WenQuanYi Zen Hei and copy wqy-zenhei.ttc into\n  " +
                assetDir_ + "\n\nhttps://sourceforge.net/projects/wqy/files/wqy-zenhei/\n"
                "(or set TOMS_FONT to any .ttf/.ttc with Traditional Chinese glyphs)";
        return false;
    }

    if (debugUi_ && !imgui_.init()) {
        error = "ImGui could not be initialized (shader program missing for this renderer).";
        return false;
    }

    toms::Logger::instance().setFile("toms.log");
    toms::Logger::instance().setLevel(toms::LogLevel::Info);
    TOMS_LOG_INFO("TOMS start (bgfx host, C++{})", __cplusplus / 100);

    game_ = std::make_unique<Game>();
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
    return true;
}

void GameSession::stop() {
    // Game never deletes its renderer (the old main called Renderer::destroy() by hand), so the
    // session frees it. Order: Game first, then the renderer's GPU handles.
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
        }
    }

    // Mouse -> handleTouch in design space (down on press, up at the press position on release).
    if (in.hasMouse && in.mouseLeft) {
        if (!mouseWasDown_) {
            float bx, by;
            if (renderer_->deviceToDesign(in.mouseX, in.mouseY, bx, by)) {
                g.handleTouch(bx, by, 0);
                mouseDesignX_ = bx; mouseDesignY_ = by; mouseHasDesign_ = true;
            }
        }
        mouseWasDown_ = true;
    } else {
        if (mouseWasDown_ && mouseHasDesign_) g.handleTouch(mouseDesignX_, mouseDesignY_, 2);
        mouseWasDown_ = false;
        mouseHasDesign_ = false;
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
        if (!g.titleOpen()) g.drawNotifications();
        if (g.stageSelectOpen()) g.drawStageSelect();
    }
    g.draw();   // -> BgfxRenderer::end(): views kViewClear + kViewGame
    if (debugUi_) imgui_.render(BgfxRenderer::kViewOverlay);
    return keepRunning;
}

}  // namespace toms::next
