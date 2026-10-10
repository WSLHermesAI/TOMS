// game_store.cpp — the shop and the pause menu: store data/UI/purchase, stage select,
// stairs confirm, and the in-game menu (save / settings / back to title). Split out of game.cpp 2026-09-13.
#include "game_internal.h"
#include <algorithm>

using namespace toms::game_detail;

// ---------- store system ----------
void Game::loadStore(const std::string& assetDir) {
    // What the store sells: ids into data/items.json, which holds every item's name, icon, stats and
    // price (loadAssets reads items.json first). An entry naming no known item is skipped.
    nlohmann::json j = readJsonFile(assetDir + "/../data/store.json");
    if (j.is_null() || j.empty()) { return; }
    if (j.contains("store_title")) storeTitle_ = j["store_title"];
    if (j.contains("unlockstage")) storeUnlockStage_ = j["unlockstage"].get<int>();
    for (auto& it : j["items"]) {
        StoreItemDef d;
        d.id = it.value("item", std::string());
        if (!itemDef(d.id)) { fprintf(stderr, "[store] '%s' is not in data/items.json, skipped\n", d.id.c_str()); continue; }
        d.price = std::max(0, itemPrice(d.id));
        d.growth = std::max(1, it.value("growth", 2));
        d.purchases = 0;
        storeItems_.push_back(d);
    }
}

void Game::openStore() {
    if (!storeUnlocked_) return;
    storeOpen = true;
    newsStore_ = false;
    storeSel_ = 0;
    audio.play("confirm_click");
}

void Game::closeStore() {
    storeOpen = false;
    audio.play("close_ui");
    if (storeFromMenu_) { storeFromMenu_ = false; openPlayerMenu((int)MenuTab::System); }   // back where it was opened
}

// Milestone 5: scans data/stages/ once for every stage's id/name/index, so the Stage Select hub
// can list all of them (locked or not) without re-parsing JSON every frame.
void Game::ensureStageListLoaded() {
    if (stageListLoaded_) return;
    stageListLoaded_ = true;
    // S3.5 (b): when the tower data is present the hub lists the 70 floors (seq order, names from
    // the floor specs' i18n keys) instead of the eleven hand-authored files. Locking still reads
    // meta_.unlockedStages, and StageInfo::id is now a floor id, so loadStage() routes through the
    // floor table. The hand-authored stages stay on disk: they ARE the ten boss floors' maps.
    if (floorMode()) {
        for (const toms::FloorInfo& f : floors_.all()) {
            StageInfo info;
            info.id = f.id;
            info.index = f.seq;
            info.fileStem = f.id;
            std::string resolved = f.nameKey;
            if (resolved.rfind("story.", 0) == 0) {
                std::string tr = locale_.tr(resolved);
                resolved = (!tr.empty() && tr != resolved) ? tr : f.id;
            }
            info.name = resolved.empty() ? f.id : resolved;
            // The act title is already authored (S3: story.chNN.title), so a hub row can say which
            // act it belongs to without inventing another key; falls back to the act id.
            std::string actTitle = locale_.tr("story." + f.actKey + ".title");
            info.preview = (actTitle.empty() || actTitle.rfind("story.", 0) == 0) ? f.act : actTitle;
            stageList_.push_back(info);
        }
        return;
    }
    std::string dir = dataDir + "/../data/stages/";
    for (const std::string& name : toms::vfsListDir(dir)) {   // vfs: APK folders on Android
        if (name.size() < 5 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
        try {
            nlohmann::json j = readJsonFile(dir + name);
            if (j.is_null() || !j.contains("id")) continue;
            StageInfo info;
            info.id = j.value("id", std::string());
            info.name = j.contains("name") ? locale_.field(j["name"]) : info.id;
            info.index = j.value("index", 0);
            info.fileStem = name.substr(0, name.size() - 5);
            // Milestone 8: recommended-stats blurb, authored per stage JSON's optional top-level
            // "preview" string -- empty for a file that doesn't set one (none did before M8).
            info.preview = j.contains("preview") ? locale_.field(j["preview"]) : std::string();
            if (!info.id.empty()) stageList_.push_back(info);
        } catch (...) {}
    }
    std::sort(stageList_.begin(), stageList_.end(),
              [](const StageInfo& a, const StageInfo& b) { return a.index < b.index; });
}

void Game::openStageSelect() {
    ensureStageListLoaded();
    stageSelectOpen_ = true;
    audio.play("confirm_click");
}

void Game::closeStageSelect() {
    stageSelectOpen_ = false;
    audio.play("close_ui");
}

// ---------- in-game menu (walking-phase HUD gear icon) ----------
// Save / Settings (language) / Back to Title. A lighter-weight sibling of the title's own
// Menu/Settings flow, built directly as Game state to match every other in-game modal here.

void Game::openInGameMenu() {
    openPlayerMenu((int)MenuTab::System);
}

void Game::closeInGameMenu() {
    closePlayerMenu();
}

void Game::inGameMenuMove(int delta) {
    if (delta == 0 || !inGameMenuOpen_) return;
    if (inGameLangConfirmOpen_) { inGameLangConfirmYes_ = !inGameLangConfirmYes_; return; }
    if (inGameMenuPage_ == InGameMenuPage::Main) {
        int n = std::max(1, (int)mainMenuOrder().size());
        inGameMenuSel_ = ((inGameMenuSel_ + delta) % n + n) % n;
    } else if (inGameMenuPage_ == InGameMenuPage::Skills) {
        int n = std::max(1, (int)skillMenuOrder().size());
        inGameMenuSel_ = ((inGameMenuSel_ + delta) % n + n) % n;
    } else if (inGameMenuPage_ == InGameMenuPage::Forge) {
        int n = std::max(1, (int)forgeMenuOrder().size());
        inGameMenuSel_ = ((inGameMenuSel_ + delta) % n + n) % n;
    } else if (inGameMenuPage_ == InGameMenuPage::Hub) {
        int n = std::max(1, (int)hubMenuOrder().size());
        inGameMenuSel_ = ((inGameMenuSel_ + delta) % n + n) % n;
    } else {
        // languages, the art style rows (none when only the original exists), the camera toggle row
        int n = std::max(1, locale_.languageCount() + settingsStyleRowCount() + 1);
        inGameMenuSel_ = ((inGameMenuSel_ + delta) % n + n) % n;
    }
}

void Game::inGameMenuActivate() {
    if (!inGameMenuOpen_) return;
    if (inGameLangConfirmOpen_) {
        const bool yes = inGameLangConfirmYes_;
        const int idx = inGameLangConfirmIdx_;
        inGameLangConfirmOpen_ = false;
        if (yes && inGameConfirmIsStyle_) applyArtStyle(idx);
        else if (yes) { title_.setLanguageIndex(idx); applyLanguage(); }
        audio.play("confirm_click");
        return;
    }
    if (inGameMenuPage_ == InGameMenuPage::Main) {
        // S5/S6: row index -> page is resolved through mainMenuOrder() (see its declaration in
        // game.h), never a hardcoded case number -- Forge/Village are each independently
        // conditional, so which literal index means what shifts depending on which are unlocked.
        auto order = mainMenuOrder();
        if (inGameMenuSel_ < 0 || inGameMenuSel_ >= (int)order.size()) return;
        switch (order[inGameMenuSel_]) {
            case MainMenuRow::Store:
                if (!storeUnlocked_) { audio.play("deny"); break; }
                closePlayerMenu();
                storeFromMenu_ = true;
                openStore();
                break;
            case MainMenuRow::Fullscreen:
                fullscreenRequest_ = true;   // the host switches (takeFullscreenRequest)
                audio.play("confirm_click");
                break;
            case MainMenuRow::Save:
                openSaveLoad(0);   // Save / Load: the player picks the slot, and confirms
                break;
            case MainMenuRow::Settings:
                inGameMenuPage_ = InGameMenuPage::Settings;
                inGameMenuSel_ = locale_.languageIndex();
                audio.play("confirm_click");
                break;
            case MainMenuRow::Skills:
                inGameMenuPage_ = InGameMenuPage::Skills;
                inGameMenuSel_ = 0;
                audio.play("confirm_click");
                break;
            case MainMenuRow::Village:   // S6
                inGameMenuPage_ = InGameMenuPage::Hub;
                inGameMenuSel_ = 0;
                audio.play("confirm_click");
                break;
            case MainMenuRow::Forge:     // S5
                inGameMenuPage_ = InGameMenuPage::Forge;
                inGameMenuSel_ = 0;
                audio.play("confirm_click");
                break;
            case MainMenuRow::BackToTitle:
                returnToTitle();
                break;
        }
    } else if (inGameMenuPage_ == InGameMenuPage::Skills) {
        // Activating a row tries to spend a point on it -- tryUnlockSkill re-validates everything
        // (owned/cost/prerequisites) itself, so an already-owned or still-locked row is a silent,
        // safe no-op rather than something this handler needs to pre-check.
        auto order = skillMenuOrder();
        if (inGameMenuSel_ >= 0 && inGameMenuSel_ < (int)order.size())
            tryUnlockSkill(order[inGameMenuSel_]);
    } else if (inGameMenuPage_ == InGameMenuPage::Forge) {
        // Same reasoning as Skills above: tryCraft re-validates canCraft itself, so activating an
        // unaffordable/unknown row is a silent no-op.
        auto order = forgeMenuOrder();
        if (inGameMenuSel_ >= 0 && inGameMenuSel_ < (int)order.size())
            tryCraft(order[inGameMenuSel_]);
    } else if (inGameMenuPage_ == InGameMenuPage::Hub) {
        // Same reasoning again: activateHubLocation re-checks hubLocationUnlocked itself. On
        // success it closes the whole in-game menu itself (see its own comment) since it opens a
        // dialogue, which was never designed to render underneath this menu.
        auto order = hubMenuOrder();
        if (inGameMenuSel_ >= 0 && inGameMenuSel_ < (int)order.size())
            activateHubLocation(order[inGameMenuSel_]);
    } else if (inGameMenuSel_ >= 0 && inGameMenuSel_ < locale_.languageCount()) {
        // Settings page: activating a language row opens the "switch to XXX?" dialog,
        // mirroring the title screen's own confirm dialog (see AskLanguageChange).
        inGameLangConfirmOpen_ = true;
        inGameConfirmIsStyle_ = false;
        inGameLangConfirmIdx_ = inGameMenuSel_;
        inGameLangConfirmYes_ = true;
        audio.play("confirm_click");
    } else if (inGameMenuSel_ < locale_.languageCount() + settingsStyleRowCount()) {
        // An art style row: same confirm dialog, which also says the new style applies on the way
        // back to the title (a run never changes look under the player; see refreshArtStyle).
        inGameLangConfirmOpen_ = true;
        inGameConfirmIsStyle_ = true;
        inGameLangConfirmIdx_ = inGameMenuSel_ - locale_.languageCount();
        inGameLangConfirmYes_ = true;
        audio.play("confirm_click");
    } else if (inGameMenuSel_ == locale_.languageCount() + settingsStyleRowCount()) {
        // The camera row (always last): cycles instantly, no confirm dialog needed -- unlike
        // language, this is a low-stakes preference whose effect the player already sees behind
        // this very menu (the maze keeps scrolling/paging under the scrim).
        setCameraModeIndex(1 - cameraModeIndex());
        audio.play("confirm_click");
    }
}

void Game::inGameMenuBack() {
    if (!inGameMenuOpen_) return;
    if (inGameLangConfirmOpen_) { inGameLangConfirmOpen_ = false; audio.play("close_ui"); return; }
    if (inGameMenuPage_ != InGameMenuPage::Main) {
        // Back to the list, on the row that opened the page (found by name: Forge and Village are
        // only there once unlocked, so the rows move).
        MainMenuRow from = MainMenuRow::Settings;
        if (inGameMenuPage_ == InGameMenuPage::Skills) from = MainMenuRow::Skills;
        if (inGameMenuPage_ == InGameMenuPage::Forge)  from = MainMenuRow::Forge;
        if (inGameMenuPage_ == InGameMenuPage::Hub)    from = MainMenuRow::Village;
        inGameMenuSel_ = mainMenuRowIndex(from);
        inGameMenuPage_ = InGameMenuPage::Main;
        audio.play("close_ui");
        return;
    }
    closePlayerMenu();
}

// Saves, tears down transient modal/run-in-progress state, and reopens the title -- the
// inverse of newGame()/applyLoadedRun() (see their resets for what this mirrors). Unlike
// newGame(), this deliberately leaves pl/meta_/entityStatus_/activeSlot_/curStage alone: the
// run is only SUSPENDED, not discarded, so Continue can pick it back up.
void Game::returnToTitle() {
    saveCurrentRun();   // flush before leaving so nothing is lost
    cs = CombatState{};
    inDialogue = false; dlgChoices.clear(); dlgNode = "root";
    storeOpen = false; storeUnlockDlg = false; storeFromMenu_ = false;
    stageSelectOpen_ = false; stairsConfirmOpen_ = false;
    if (inGameMenuOpen_) closeInGameMenu();
    missionTrackers_.clear();
    notifications_.clear();
    title_.setRunInProgress(false);
    title_.open();
    refreshArtStyle();   // a style chosen during the run applies now (nothing of the run is on screen)
    refreshSlots();
}

void Game::storeKey(int key) {
    // unlock dialog: Enter/Esc/Space confirms
    if (storeUnlockDlg) {
        if (key == 13 || key == 27 || key == 32) { storeUnlockDlg = false; audio.play("confirm_click"); }
        return;
    }
    if (!storeOpen) return;
    // Milestone 8: storeSel_ is an index into the current tab's filtered list, not storeItems_
    // directly (see storeTabIndices()) -- map through it before buying.
    std::vector<int> idxList = storeTabIndices();
    int n = (int)idxList.size();
    if (key == 27) { closeStore(); return; }                       // Esc closes
    if (key == 13 || key == 32) {                                  // Enter/Space buys
        if (storeSel_ >= 0 && storeSel_ < n) buyStoreItem(idxList[storeSel_]);
        return;
    }
    if (key == 262 || key == 263) {                                // right/left arrow
        storeSel_ += (key == 262) ? 1 : -1;
        if (storeSel_ < 0) storeSel_ = n-1; if (storeSel_ >= n) storeSel_ = 0;
        return;
    }
    if (key >= '1' && key <= '9') {                                // number keys select
        int sel = key - '1';
        if (sel < n) storeSel_ = sel;
    }
}

void Game::buyStoreItem(int idx) {
    if (idx < 0 || idx >= (int)storeItems_.size()) return;
    StoreItemDef& d = storeItems_[idx];
    // Gear the player already owns is not bought twice: tapping it wears it.
    if (isGearType(itemType(d.id)) && std::find(gearOwned_.begin(), gearOwned_.end(), d.id) != gearOwned_.end()) {
        equipGear(d.id);
        toastMsg_ = trParam(locale_.tr("store.toast_purchase_success"), "name", itemName(d.id));
        toastTimer_ = 1200;
        audio.play("confirm_click");
        markProgressDirty();
        return;
    }
    int cost = d.liveCost();
    if (pl.gold < cost) {
        // not enough gold -> toast + shake (simple effect), no purchase
        toastMsg_ = trParam(locale_.tr("store.toast_insufficient_gold"), "cost", std::to_string(cost));
        toastTimer_ = 1600;
        shakeTimer_ = 350;
        audio.play("deny");
        return;
    }
    pl.gold -= cost;
    // A weapon / armor / talent joins the owned gear and is worn at once (the Equipment tab swaps it
    // later); a consumable is used right away, as the store always did.
    if (isGearType(itemType(d.id))) ownGear(d.id, true);
    else applyStats(d.id, itemStats(d.id));
    d.purchases++;
    toastMsg_ = trParam(locale_.tr("store.toast_purchase_success"), "name", itemName(d.id));
    toastTimer_ = 1400;
    audio.play("get_item");
    markProgressDirty();   // a purchase changes gold/stats/equipment: autosave will flush it
}
