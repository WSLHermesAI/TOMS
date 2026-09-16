// game.cpp — implementation of Game.
#include "game.h"
#include <cstdio>    // std::snprintf (title screen's play-time column)
#include "node.h"   // 2D scene-graph Node (parent/child + local/world transform)
#include "scene.h"  // render binding: GameObject / SpriteNode / TextNode / FullScreenSplash

// Shared file-local helpers (C4 / trParam / readJsonFile / cellSprite / entSprite / GP table /
// g_textGame) now live in game_helpers.h as inline definitions, since the 2026-09-13 refactor split
// the rest of this file into game_*.cpp units that need the same helpers.
#include "game_helpers.h"
#include "game_condition.h"   // GameConditionContext (condition.h adapter)

using namespace toms::game_detail;

// TextNode draws through Game's font; g_textGame (bound in loadAssets) now lives in
// game_helpers.h so the split translation units share one instance.
void toms_TextNodeDraw(const std::string& s, float x, float y, float sz, const float* t) {
    if (g_textGame) g_textGame->drawTextPublic(s, x, y, sz, t);
}
namespace toms { TextNode::DrawFn TextNode::Draw = ::toms_TextNodeDraw; }
#ifdef __EMSCRIPTEN__
  #ifdef WEBGPU
    #include "renderer_webgpu.h"   // WebGPU backend (browser build only)
  #else
    #include "renderer_webgl.h"   // WebGL2 backend (browser build only)
  #endif
#else
#include "renderer.h"         // Vulkan backend (desktop build only)
#endif
#ifndef __EMSCRIPTEN__
#include "vk_util.h"   // Vulkan helpers — desktop build only
#endif
#include "event_bus.h"   // toms::EventBus — see Game::resolveCombatRound / Game::movePlayer
#include "condition.h"   // toms::evaluate / toms::ConditionContext — see GameConditionContext below
#include "story_controller.h" // toms::advanceStoryBeat / setStoryFlag / hasStoryFlag
#include "encounter.h"   // toms::resolveEncounterKind / toms::EncounterKind — see Game::movePlayer
#ifndef __EMSCRIPTEN__
#include "imgui.h"       // core ImGui API only -- backend plumbing lives in imgui_layer.h/.cpp
#endif

#include <json.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <ctime>


Quad Game::spriteQuad(float x, float y, float w, float h, int layer, const float tint[4]) {
    Quad q; q.rect[0]=x; q.rect[1]=y; q.rect[2]=w; q.rect[3]=h;
    spriteUV(layer, q.uv);
    q.tint[0]=tint[0]; q.tint[1]=tint[1]; q.tint[2]=tint[2]; q.tint[3]=tint[3];
    return q;
}


// ---------- maze camera ----------
// The camera's own math lives in src/game/camera.cpp (toms::Camera). What remains here is glue:
// hand it the pixel size of the drawing area, the grid size and the focus tile, then ease it.

// Adapter over toms::Camera::viewportTiles() so nothing else has to know the renderer exists.
void Game::cameraViewportTiles(float& ts, int& cols, int& rows) const {
    const float W = ren ? (float)ren->width()  : 1024.0f;
    const float H = ren ? (float)ren->height() : 768.0f;
    cam_.viewportTiles(W, H, ts, cols, rows);
}

void Game::setCameraModeIndex(int m) {
    cam_.setMode(toms::Camera::modeFromIndex(m));
    settings_.cameraMode = cam_.modeIndex();
    toms::ensureSaveDir(toms::defaultSaveDir());
    toms::saveGameSettings(toms::defaultSaveDir() + "/settings.json", settings_);
    // Re-target immediately so the effect is visible right away instead of waiting for the
    // player to take a step; ease into it rather than snapping (consistent with "Rooms" mode's
    // own slide, and it's a nicer transition than a hard cut for "Follow" too).
    cam_.retarget(pl.x, pl.y, st.width, st.height,
                  ren ? (float)ren->width() : 1024.0f, ren ? (float)ren->height() : 768.0f);
}


std::vector<int> Game::storeTabIndices() const {
    std::vector<int> out;
    for (int i = 0; i < (int)storeItems_.size(); i++) {
        const StoreItemDef& d = storeItems_[i];
        int tab;
        if (d.equipmentId.empty()) {
            tab = 0;   // potions / plain consumables
        } else {
            auto it = equipmentDefs_.find(d.equipmentId);
            toms::EquipmentSlot slot = (it != equipmentDefs_.end()) ? it->second.slot : toms::EquipmentSlot::Weapon;
            tab = (slot == toms::EquipmentSlot::Weapon) ? 1 : (slot == toms::EquipmentSlot::Armor) ? 2 : 3;
        }
        if (tab == storeTab_) out.push_back(i);
    }
    return out;
}


// Milestone 9: opens the Yes/No confirm instead of transitioning immediately -- see
// stairsConfirmOpen_'s declaration in game.h for why.
void Game::requestStageTransition(const std::string& target, bool isUp) {
    stairsConfirmOpen_ = true;
    stairsConfirmIsUp_ = isUp;
    stairsConfirmTarget_ = target;
    audio.play("confirm_click");
}

void Game::confirmStageTransition() {
    if (!stairsConfirmOpen_) return;
    std::string target = stairsConfirmTarget_;
    bool isUp = stairsConfirmIsUp_;
    stairsConfirmOpen_ = false;
    stairsConfirmTarget_.clear();
    // M9 (stair alignment): climbing up lands on the new floor's OWN stairs_down (the matching
    // physical stairwell coming from below); going down lands on its stairs_up (arriving from
    // above) -- see docs/story/STAIR_ALIGNMENT.md.
    loadStage(target, isUp ? StageArrival::FromBelow : StageArrival::FromAbove);
    // A floor change is the run's natural checkpoint: write/refresh the slot immediately
    // (rather than waiting for the throttled autosave) so the Continue list is always current.
    saveCurrentRun();
}

void Game::cancelStageTransition() {
    stairsConfirmOpen_ = false;
    stairsConfirmTarget_.clear();
    audio.play("close_ui");
}




#ifndef __EMSCRIPTEN__


#endif


void Game::newGame(int slot) {
    // A new game is a genuinely fresh run: default stats and wiped story/entity/meta progress.
    pl = Player();
    pl.maxhp = kStartingHp; pl.hp = kStartingHp; pl.atk = kStartingAtk; pl.def = kStartingDef; pl.gold = 0; pl.exp = 0; pl.lv = 1;
    pl.inv = {"potion_red", "potion_blue", "exp_up"};
    pl.x = 1; pl.y = 1;
    entityStatus_.clear();
    run_.reset();   // S3: a new game is a fresh run -- choices/counters/side stories/floors/deaths
    missionTrackers_.clear();
    meta_ = toms::MetaSaveData{};
    notifications_.clear();
    cs = CombatState{};
    inDialogue = false; dlgChoices.clear(); dlgNode = "root";
    invOpen = false; storeOpen = false; storeUnlockDlg = false;
    stageSelectOpen_ = false; stairsConfirmOpen_ = false;
    playTimeSec_ = 0;
    if (slot > 0) {
        // Explicit slot (the Continue page's "start a new game in this empty slot?" answer).
        activeSlot_ = slot;
    } else {
        // Prefer a free slot; when every slot is taken, reuse slot 1 rather than refusing to
        // start (the Continue page still lists all of them, so nothing is silently destroyed).
        int free = toms::firstEmptySlot(toms::defaultSaveDir(), title_.slotCount());
        activeSlot_ = (free > 0) ? free : 1;
    }
    // S3.5: with the tower data present the run starts on its first floor (F01), so progression,
    // the counter and the save all speak floor ids; otherwise the historical starting stage.
    loadStage(floorMode() ? floors_.at(1).id : std::string("stage01"));
    saveCurrentRun();
    title_.close();
    title_.setRunInProgress(true);
}


void Game::update(int dtMs) {
    // S3.5 (c): the act card fades out on its own; everything it shows is also on screen elsewhere
    // (the HUD carries the floor), so a player who ignores it loses nothing.
    if (chapterCardMs_ > 0.0f) chapterCardMs_ = std::max(0.0f, chapterCardMs_ - (float)dtMs);
    // Title-screen animation clock: advances in every state (the title is drawn long before any
    // gameplay exists) and wraps so a float never drifts into precision loss on a long session.
    titleAnimMs_ += (float)dtMs;
    if (titleAnimMs_ > 3600000.0f) titleAnimMs_ -= 3600000.0f;
    // Title phase: the run's clock only advances while actually playing, and a changed run is
    // flushed to its slot on a throttle (see kAutosaveIntervalMs) rather than on every event --
    // one atomic write per few seconds instead of one per pickup.
    if (title_.isOpen()) {
        if (saveDirty_ && !title_.runInProgress()) saveDirty_ = false;   // nothing to save yet
    } else {
        playTimeSec_ += dtMs / 1000;
        if (saveDirty_) {
            saveFlushMs_ += dtMs;
            if (saveFlushMs_ >= kAutosaveIntervalMs) { saveFlushMs_ = 0; saveCurrentRun(); }
        } else {
            saveFlushMs_ = 0;
        }
    }
    // Battle System v2: both bars auto-move continuously and independently, and the enemy fires
    // on its own fixed real-time clock -- none of this waits on player input, so it all just ticks
    // every frame combat is active. See CombatState's comment in game.h for the full model.
    if (cs.active) {
        auto stepBar = [&](CombatState::AutoBar& bar, const toms::PowerBarParams& z) {
            if (bar.cooling) {
                bar.cooldownMs -= dtMs;
                if (bar.cooldownMs <= 0) { bar.cooling = false; bar.pos = 0.0f; bar.dir = 1; bar.legMs = 0.0f; }
                return;
            }
            bar.legMs += (float)dtMs;
            float t = z.rampTime > 0.0f ? std::min(1.0f, bar.legMs / 1000.0f / z.rampTime) : 1.0f;
            float speed = z.v0 + (z.vmax - z.v0) * (t * t * t);   // cubic ease-in, same curve as a manual charge
            float span = 2.0f * z.redOuter;
            bar.pos += (float)bar.dir * speed * (dtMs / 1000.0f);
            if (bar.pos > span) { bar.pos = span; bar.dir = -1; bar.legMs = 0.0f; }
            if (bar.pos < 0.0f)  { bar.pos = 0.0f;  bar.dir = 1;  bar.legMs = 0.0f; }
        };
        stepBar(cs.atkBar, toms::effectiveAttackBar(equipped_, equipmentDefs_));
        stepBar(cs.defBar, toms::effectiveDefenseBar(equipped_, equipmentDefs_));

        cs.enemyClockMs += dtMs;
        int interval = std::max(500, cs.enemy.atkIntervalMs);
        if (cs.enemyClockMs >= interval) {
            cs.enemyClockMs = 0;
            resolveEnemyClockFire();
        }
    }
    // The result-pause countdown must run regardless of cs.active: finishCombatLose() sets
    // active=false in the same call that starts this pause, so gating on cs.active would freeze
    // it forever and leave a stale "you fell" message stuck in cs.log (which -- see showBattle's
    // condition -- would keep the battle overlay stuck open permanently after a loss).
    if (cs.resultPauseMs > 0) {
        cs.resultPauseMs -= dtMs;
        if (cs.resultPauseMs <= 0) {
            cs.resultPauseMs = 0;
            // A win keeps its victory text showing until the player dismisses it (cs.won,
            // cleared elsewhere on tap/interact); a loss clears its message now, once the brief
            // pause has had a chance to show it, so showBattle's condition stops holding the
            // battle overlay open.
            if (!cs.active && !cs.won) cs.log = "";
        }
    }
    // store UI timers (toast / shake) tick down regardless of combat
    if (toastTimer_ > 0) { toastTimer_ -= dtMs; if (toastTimer_ < 0) toastTimer_ = 0; }
    if (shakeTimer_ > 0) { shakeTimer_ -= dtMs; if (shakeTimer_ < 0) shakeTimer_ = 0; }
    // Milestone 5: notification toasts tick down and expire.
    for (auto& n : notifications_) n.second -= dtMs;
    notifications_.erase(
        std::remove_if(notifications_.begin(), notifications_.end(),
                        [](const std::pair<std::string,int>& n) { return n.second <= 0; }),
        notifications_.end());
    // Milestone 9 polish: "keep moving while held" -- the actual repeat timer, driven from
    // here regardless of which input (keyboard or the virtual/touch d-pad) is holding a
    // direction; see setMoveHeldX/Y's declaration in game.h. Each axis repeats independently
    // so a held diagonal (e.g. up+right) keeps moving diagonally. movePlayer() is already a
    // safe no-op while modalActive(), so ticking this even during a dialogue/battle/etc. that
    // started mid-hold is harmless.
    auto tickMoveAxis = [&](MoveHoldAxis& a, int dx, int dy) {
        if (a.dir == 0) return;
        a.holdMs += dtMs;
        int threshold = a.repeating ? kMoveRepeatMs : kMoveInitialDelayMs;
        if (a.holdMs >= threshold) {
            movePlayer(dx, dy);
            a.holdMs -= threshold;
            a.repeating = true;
        }
    };
    tickMoveAxis(moveHoldX_, moveHoldX_.dir, 0);
    tickMoveAxis(moveHoldY_, 0, moveHoldY_.dir);

    // Maze camera: retarget on the player's tile, then ease toward it (both modes slide --
    // Rooms' target only actually MOVES when the player crosses into a different section, so it
    // reads as "smoothly snap to the new room" for that mode and as continuous following for
    // Follow mode). All of that lives in toms::Camera (src/game/camera.cpp); the easing is
    // frame-rate-independent exponential smoothing, not a fixed-duration tween.
    if (!title_.isOpen()) {
        cam_.retarget(pl.x, pl.y, st.width, st.height,
                      ren ? (float)ren->width() : 1024.0f, ren ? (float)ren->height() : 768.0f);
        cam_.update((float)dtMs / 1000.0f);
    }
}

void Game::saveFrame(const std::string& path) {
    ren->savePNG(path);
}
