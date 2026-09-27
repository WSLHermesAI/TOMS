// game_title_glue.cpp — title phase + save slots glue: title menu state, slot summaries,
// new game / continue, autosave, language switching, and the title pages' drawing.
// Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

// =====================================================================================
// Title phase — New Game / Continue / Settings (see src/game/title_screen.h)
// =====================================================================================

std::string Game::stageDisplayName(const std::string& id) const {
    // Match either identifier: the game loads stages by FILE STEM ("stage01"), while the id
    // inside the JSON is "stage_01" -- so a pure id comparison silently missed and the Continue
    // list showed the raw stem instead of the stage's real name.
    for (const auto& s : stageList_)
        if (s.id == id || s.fileStem == id) return s.name.empty() ? id : s.name;
    return id;
}

// Snapshots the live game state into the Run-save schema (save_system.h) for writing to a slot.
toms::RunSaveData Game::runSaveFromState() const {
    toms::RunSaveData r;
    r.currentStageId = curStage;
    r.player.hp = pl.hp; r.player.maxhp = pl.maxhp;
    r.player.atk = pl.atk; r.player.def = pl.def;
    r.player.gold = pl.gold; r.player.exp = pl.exp; r.player.lv = pl.lv;
    r.player.key_yellow = pl.key_yellow; r.player.key_blue = pl.key_blue; r.player.key_red = pl.key_red;
    r.player.inv = pl.inv;
    r.entityStatus = entityStatus_;
    // S3: the run's story state travels with the run save (schemaVersion 3) -- choices, counters,
    // side-story states, run flags, shards, floor progress and deaths.
    run_.writeInto(r);
    return r;
}

void Game::saveCurrentRun() {
    if (activeSlot_ <= 0) return;          // no slot chosen yet (title still up at boot)
    ensureStageListLoaded();
    const std::string dir = toms::defaultSaveDir();
    if (toms::writeSlotSave(dir, activeSlot_, meta_, runSaveFromState(), playTimeSec_,
                            toms::nowStamp(), stageDisplayName(curStage))) {
        saveDirty_ = false;
    }
    if (title_.isOpen()) refreshSlots();
}

void Game::refreshSlots() {
    std::vector<toms::SlotSummary> v;
    const std::string dir = toms::defaultSaveDir();
    for (int i = 1; i <= title_.slotCount(); i++) v.push_back(toms::summarizeSlot(dir, i));
    title_.setSummaries(std::move(v));
}

void Game::applyLanguage() {
    // The Settings page owns the selection -- mirror it into the persisted preference. Reading
    // it back from title_ (rather than re-applying the old settings_.language) is what makes
    // the choice actually stick: without this the freshly-picked language was overwritten by
    // the stale preference on the very same call, so the UI snapped back.
    const int idx = title_.languageIndex();
    if (idx >= 0 && idx < locale_.languageCount())
        settings_.language = locale_.languages()[idx].code;
    locale_.setLanguage(settings_.language);
    settings_.language = locale_.languageCode();   // normalize (e.g. an unknown code -> default)
    title_.setLanguageIndex(locale_.languageIndex());
    toms::ensureSaveDir(toms::defaultSaveDir());
    toms::saveGameSettings(toms::defaultSaveDir() + "/settings.json", settings_);
}

void Game::applyLoadedRun(const toms::MetaSaveData& m, const toms::RunSaveData& r,
                          int slot, int playSec) {
    meta_ = m;
    entityStatus_ = r.entityStatus;
    run_.readFrom(r);   // S3: restore the run's story state (defaults for a pre-v3 file)
    // Anything that belongs to the *previous* run must not leak into the loaded one.
    missionTrackers_.clear();
    notifications_.clear();
    cs = CombatState{};
    inDialogue = false; dlgChoices.clear(); dlgNode = "root";
    invOpen = false; storeOpen = false; storeUnlockDlg = false;
    stageSelectOpen_ = false; stairsConfirmOpen_ = false;
    pl.hp = r.player.hp; pl.maxhp = r.player.maxhp;
    pl.atk = r.player.atk; pl.def = r.player.def;
    pl.gold = r.player.gold; pl.exp = r.player.exp; pl.lv = r.player.lv;
    pl.key_yellow = r.player.key_yellow; pl.key_blue = r.player.key_blue; pl.key_red = r.player.key_red;
    pl.inv = r.player.inv;
    activeSlot_ = slot;
    playTimeSec_ = playSec;
    loadStage(r.currentStageId.empty() ? std::string("stage01") : r.currentStageId);
    title_.close();
    title_.setRunInProgress(true);
}

bool Game::continueFromSlot(int slot) {
    if (slot <= 0) return false;
    toms::MetaSaveData m;
    toms::RunSaveData r;
    int playSec = 0;
    if (!toms::readSlotSave(toms::defaultSaveDir(), slot, m, r, &playSec, nullptr)) {
        // Deliberately silent: the Continue row already reads "[空]/[Empty]" and the page shows
        // "no saves yet -- start a new game first", and the toast path draws with ImGui's
        // default font, which has no CJK glyphs (it would render as a "?" box). The title stays
        // open, so there is nothing to resume from.
        refreshSlots();
        return false;
    }
    applyLoadedRun(m, r, slot, playSec);
    return true;
}

void Game::newGame() { newGame(0); }

void Game::handleTitleAction(toms::TitleAction a) {
    switch (a) {
        case toms::TitleAction::NewGame:             newGame(); break;
        case toms::TitleAction::StartNewGameInSlot:  newGame(title_.pendingSlot()); break;
        case toms::TitleAction::LoadSlot:            continueFromSlot(title_.pendingSlot()); break;
        case toms::TitleAction::OpenContinue:        refreshSlots(); break;   // always show current files
        case toms::TitleAction::SetLanguage:         applyLanguage(); break;
        // The confirm dialogs are drawn and answered by the title itself; these are informational.
        case toms::TitleAction::AskNewGameInSlot:
        case toms::TitleAction::DismissNewGameConfirm:
        case toms::TitleAction::AskLanguageChange:
        case toms::TitleAction::DismissLanguageConfirm:
        case toms::TitleAction::OpenSettings:
        case toms::TitleAction::Back:
        case toms::TitleAction::None:                break;
    }
}

void Game::titleMove(int dx, int dy) {
    if (!title_.isOpen()) return;
    toms::TitleAction a = toms::TitleAction::None;
    if (dy != 0) a = title_.moveVertical(dy);
    if (dx != 0) {
        toms::TitleAction h = title_.moveHorizontal(dx);   // Settings: left/right switches language
        if (a == toms::TitleAction::None) a = h;
    }
    handleTitleAction(a);
}

void Game::titleConfirm() {
    if (title_.isOpen()) handleTitleAction(title_.activate());
}

void Game::titleCancel() {
    if (title_.isOpen()) handleTitleAction(title_.cancel());
}
