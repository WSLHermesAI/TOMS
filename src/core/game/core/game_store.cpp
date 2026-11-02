// game_store.cpp — the shop and the pause menu: store data/UI/purchase, stage select,
// stairs confirm, and the in-game menu (save / settings / back to title). Split out of game.cpp 2026-09-13.
#include "game_internal.h"
#include <algorithm>

using namespace toms::game_detail;

// ---------- store system ----------
void Game::f_LoadStore() {
    // What the store sells: ids into data/items.json, which holds every item's name, icon, stats and
    // price (loadAssets reads items.json first). An entry naming no known item is skipped.
    nlohmann::json l_jJ = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataStore));
    if (l_jJ.is_null() || l_jJ.empty()) { return; }
    if (l_jJ.contains("store_title")) m_jStoreTitle = l_jJ["store_title"];
    if (l_jJ.contains("unlockstage")) m_iStoreUnlockStage = l_jJ["unlockstage"].get<int>();
    for (auto& l_jIt : l_jJ["items"]) {
        StoreItemDef l_oD;
        l_oD.m_strId = l_jIt.value("item", std::string());
        if (!f_ItemDef(l_oD.m_strId)) { fprintf(stderr, "[store] '%s' is not in data/items.json, skipped\n", l_oD.m_strId.c_str()); continue; }
        l_oD.m_iPrice = std::max(0, f_ItemPrice(l_oD.m_strId));
        l_oD.m_iGrowth = std::max(1, l_jIt.value("growth", 2));
        l_oD.m_iPurchases = 0;
        m_vStoreItems.push_back(l_oD);
    }
}

void Game::f_OpenStore() {
    if (!m_bStoreUnlocked) return;
    m_bStoreOpen = true;
    m_bNewsStore = false;
    m_iStoreSel = 0;
    m_oAudio.f_Play("confirm_click");
}

void Game::f_CloseStore() {
    m_bStoreOpen = false;
    m_oAudio.f_Play("close_ui");
    if (m_bStoreFromMenu) { m_bStoreFromMenu = false; f_OpenPlayerMenu((int)MenuTab::System); }   // back where it was opened
}

// Milestone 5: scans data/stages/ once for every stage's id/name/index, so the Stage Select hub
// can list all of them (locked or not) without re-parsing JSON every frame.
void Game::f_EnsureStageListLoaded() {
    if (m_bStageListLoaded) return;
    m_bStageListLoaded = true;
    // S3.5 (b): when the tower data is present the hub lists the 70 floors (seq order, names from
    // the floor specs' i18n keys) instead of the eleven hand-authored files. Locking still reads
    // meta_.unlockedStages, and StageInfo::id is now a floor id, so loadStage() routes through the
    // floor table. The hand-authored stages stay on disk: they ARE the ten boss floors' maps.
    if (f_FloorMode()) {
        for (const toms::FloorInfo& l_oF : m_oFloors.f_All()) {
            StageInfo l_oInfo;
            l_oInfo.m_strId = l_oF.m_strId;
            l_oInfo.m_iIndex = l_oF.m_iSeq;
            l_oInfo.m_strFileStem = l_oF.m_strId;
            std::string l_strResolved = l_oF.m_strNameKey;
            if (l_strResolved.rfind("story.", 0) == 0) {
                std::string l_strTr = m_oLocale.f_Tr(l_strResolved);
                l_strResolved = (!l_strTr.empty() && l_strTr != l_strResolved) ? l_strTr : l_oF.m_strId;
            }
            l_oInfo.m_strName = l_strResolved.empty() ? l_oF.m_strId : l_strResolved;
            // The act title is already authored (S3: story.chNN.title), so a hub row can say which
            // act it belongs to without inventing another key; falls back to the act id.
            std::string l_strActTitle = m_oLocale.f_Tr("story." + l_oF.m_strActKey + ".title");
            l_oInfo.m_strPreview = (l_strActTitle.empty() || l_strActTitle.rfind("story.", 0) == 0) ? l_oF.m_strAct : l_strActTitle;
            m_vStageList.push_back(l_oInfo);
        }
        return;
    }
    std::string l_strDir = f_PathOf(toms::key::g_pszFolderStages) + "/";
    for (const std::string& l_strName : toms::f_VfsListDir(l_strDir)) {   // vfs: APK folders on Android
        if (l_strName.size() < 5 || l_strName.compare(l_strName.size() - 5, 5, ".json") != 0) continue;
        try {
            nlohmann::json l_jJ = f_ReadJsonFile(l_strDir + l_strName);
            if (l_jJ.is_null() || !l_jJ.contains("id")) continue;
            StageInfo l_oInfo;
            l_oInfo.m_strId = l_jJ.value("id", std::string());
            l_oInfo.m_strName = l_jJ.contains("name") ? m_oLocale.f_Field(l_jJ["name"]) : l_oInfo.m_strId;
            l_oInfo.m_iIndex = l_jJ.value("index", 0);
            l_oInfo.m_strFileStem = l_strName.substr(0, l_strName.size() - 5);
            // Milestone 8: recommended-stats blurb, authored per stage JSON's optional top-level
            // "preview" string -- empty for a file that doesn't set one (none did before M8).
            l_oInfo.m_strPreview = l_jJ.contains("preview") ? m_oLocale.f_Field(l_jJ["preview"]) : std::string();
            if (!l_oInfo.m_strId.empty()) m_vStageList.push_back(l_oInfo);
        } catch (...) {}
    }
    std::sort(m_vStageList.begin(), m_vStageList.end(),
              [](const StageInfo& l_oA, const StageInfo& l_oB) { return l_oA.m_iIndex < l_oB.m_iIndex; });
}

void Game::f_OpenStageSelect() {
    f_EnsureStageListLoaded();
    m_bStageSelectOpen = true;
    m_oAudio.f_Play("confirm_click");
}

void Game::f_CloseStageSelect() {
    m_bStageSelectOpen = false;
    m_oAudio.f_Play("close_ui");
}

// ---------- in-game menu (walking-phase HUD gear icon) ----------
// Save / Settings (language) / Back to Title. A lighter-weight sibling of the title's own
// Menu/Settings flow, built directly as Game state to match every other in-game modal here.

void Game::f_OpenInGameMenu() {
    f_OpenPlayerMenu((int)MenuTab::System);
}

void Game::f_CloseInGameMenu() {
    f_ClosePlayerMenu();
}

void Game::f_InGameMenuMove(int l_iDelta) {
    if (l_iDelta == 0 || !m_bInGameMenuOpen) return;
    if (m_bInGameLangConfirmOpen) { m_bInGameLangConfirmYes = !m_bInGameLangConfirmYes; return; }
    if (m_eInGameMenuPage == InGameMenuPage::Main) {
        int l_iN = std::max(1, (int)f_MainMenuOrder().size());
        m_iInGameMenuSel = ((m_iInGameMenuSel + l_iDelta) % l_iN + l_iN) % l_iN;
    } else if (m_eInGameMenuPage == InGameMenuPage::Skills) {
        int l_iN = std::max(1, (int)f_SkillMenuOrder().size());
        m_iInGameMenuSel = ((m_iInGameMenuSel + l_iDelta) % l_iN + l_iN) % l_iN;
    } else if (m_eInGameMenuPage == InGameMenuPage::Forge) {
        int l_iN = std::max(1, (int)f_ForgeMenuOrder().size());
        m_iInGameMenuSel = ((m_iInGameMenuSel + l_iDelta) % l_iN + l_iN) % l_iN;
    } else if (m_eInGameMenuPage == InGameMenuPage::Hub) {
        int l_iN = std::max(1, (int)f_HubMenuOrder().size());
        m_iInGameMenuSel = ((m_iInGameMenuSel + l_iDelta) % l_iN + l_iN) % l_iN;
    } else {
        // languages, the art style rows (none when only the original exists), the camera row, the reduce-effects row
        int l_iN = std::max(1, m_oLocale.f_LanguageCount() + f_SettingsStyleRowCount() + 2);
        m_iInGameMenuSel = ((m_iInGameMenuSel + l_iDelta) % l_iN + l_iN) % l_iN;
    }
}

void Game::f_InGameMenuActivate() {
    if (!m_bInGameMenuOpen) return;
    if (m_bInGameLangConfirmOpen) {
        const bool l_bYes = m_bInGameLangConfirmYes;
        const int l_iIdx = m_iInGameLangConfirmIdx;
        m_bInGameLangConfirmOpen = false;
        if (l_bYes && m_bInGameConfirmIsStyle) f_ApplyArtStyle(l_iIdx);
        else if (l_bYes) { m_oTitle.f_SetLanguageIndex(l_iIdx); f_ApplyLanguage(); }
        m_oAudio.f_Play("confirm_click");
        return;
    }
    if (m_eInGameMenuPage == InGameMenuPage::Main) {
        // S5/S6: row index -> page is resolved through mainMenuOrder() (see its declaration in
        // game.h), never a hardcoded case number -- Forge/Village are each independently
        // conditional, so which literal index means what shifts depending on which are unlocked.
        auto l_vOrder = f_MainMenuOrder();
        if (m_iInGameMenuSel < 0 || m_iInGameMenuSel >= (int)l_vOrder.size()) return;
        switch (l_vOrder[m_iInGameMenuSel]) {
            case MainMenuRow::Store:
                if (!m_bStoreUnlocked) { m_oAudio.f_Play("deny"); break; }
                f_ClosePlayerMenu();
                m_bStoreFromMenu = true;
                f_OpenStore();
                break;
            case MainMenuRow::Fullscreen:
                m_bFullscreenRequest = true;   // the host switches (takeFullscreenRequest)
                m_oAudio.f_Play("confirm_click");
                break;
            case MainMenuRow::Save:
                f_OpenSaveLoad(0);   // Save / Load: the player picks the slot, and confirms
                break;
            case MainMenuRow::Settings:
                m_eInGameMenuPage = InGameMenuPage::Settings;
                m_iInGameMenuSel = m_oLocale.f_LanguageIndex();
                m_oAudio.f_Play("confirm_click");
                break;
            case MainMenuRow::Skills:
                m_eInGameMenuPage = InGameMenuPage::Skills;
                m_iInGameMenuSel = 0;
                m_oAudio.f_Play("confirm_click");
                break;
            case MainMenuRow::Village:   // S6
                m_eInGameMenuPage = InGameMenuPage::Hub;
                m_iInGameMenuSel = 0;
                m_oAudio.f_Play("confirm_click");
                break;
            case MainMenuRow::Forge:     // S5
                m_eInGameMenuPage = InGameMenuPage::Forge;
                m_iInGameMenuSel = 0;
                m_oAudio.f_Play("confirm_click");
                break;
            case MainMenuRow::BackToTitle:
                f_ReturnToTitle();
                break;
        }
    } else if (m_eInGameMenuPage == InGameMenuPage::Skills) {
        // Activating a row tries to spend a point on it -- tryUnlockSkill re-validates everything
        // (owned/cost/prerequisites) itself, so an already-owned or still-locked row is a silent,
        // safe no-op rather than something this handler needs to pre-check.
        auto l_vOrder = f_SkillMenuOrder();
        if (m_iInGameMenuSel >= 0 && m_iInGameMenuSel < (int)l_vOrder.size())
            f_TryUnlockSkill(l_vOrder[m_iInGameMenuSel]);
    } else if (m_eInGameMenuPage == InGameMenuPage::Forge) {
        // Same reasoning as Skills above: tryCraft re-validates canCraft itself, so activating an
        // unaffordable/unknown row is a silent no-op.
        auto l_vOrder = f_ForgeMenuOrder();
        if (m_iInGameMenuSel >= 0 && m_iInGameMenuSel < (int)l_vOrder.size())
            f_TryCraft(l_vOrder[m_iInGameMenuSel]);
    } else if (m_eInGameMenuPage == InGameMenuPage::Hub) {
        // Same reasoning again: activateHubLocation re-checks hubLocationUnlocked itself. On
        // success it closes the whole in-game menu itself (see its own comment) since it opens a
        // dialogue, which was never designed to render underneath this menu.
        auto l_vOrder = f_HubMenuOrder();
        if (m_iInGameMenuSel >= 0 && m_iInGameMenuSel < (int)l_vOrder.size())
            f_ActivateHubLocation(l_vOrder[m_iInGameMenuSel]);
    } else if (m_iInGameMenuSel >= 0 && m_iInGameMenuSel < m_oLocale.f_LanguageCount()) {
        // Settings page: activating a language row opens the "switch to XXX?" dialog,
        // mirroring the title screen's own confirm dialog (see AskLanguageChange).
        m_bInGameLangConfirmOpen = true;
        m_bInGameConfirmIsStyle = false;
        m_iInGameLangConfirmIdx = m_iInGameMenuSel;
        m_bInGameLangConfirmYes = true;
        m_oAudio.f_Play("confirm_click");
    } else if (m_iInGameMenuSel < m_oLocale.f_LanguageCount() + f_SettingsStyleRowCount()) {
        // An art style row: same confirm dialog, which also says the new style applies on the way
        // back to the title (a run never changes look under the player; see refreshArtStyle).
        m_bInGameLangConfirmOpen = true;
        m_bInGameConfirmIsStyle = true;
        m_iInGameLangConfirmIdx = m_iInGameMenuSel - m_oLocale.f_LanguageCount();
        m_bInGameLangConfirmYes = true;
        m_oAudio.f_Play("confirm_click");
    } else if (m_iInGameMenuSel == m_oLocale.f_LanguageCount() + f_SettingsStyleRowCount()) {
        // The camera row (after the art styles): cycles instantly, no confirm dialog needed -- unlike
        // language, this is a low-stakes preference whose effect the player already sees behind
        // this very menu (the maze keeps scrolling/paging under the scrim).
        f_SetCameraModeIndex(1 - f_CameraModeIndex());
        m_oAudio.f_Play("confirm_click");
    } else if (m_iInGameMenuSel == m_oLocale.f_LanguageCount() + f_SettingsStyleRowCount() + 1) {
        f_SetReduceEffects(!m_oSettings.m_bReduceEffects);   // "Reduce shaking and flashes" (docs/gameplay/15 §3.8)
        m_oAudio.f_Play("confirm_click");
    }
}

void Game::f_InGameMenuBack() {
    if (!m_bInGameMenuOpen) return;
    if (m_bInGameLangConfirmOpen) { m_bInGameLangConfirmOpen = false; m_oAudio.f_Play("close_ui"); return; }
    if (m_eInGameMenuPage != InGameMenuPage::Main) {
        // Back to the list, on the row that opened the page (found by name: Forge and Village are
        // only there once unlocked, so the rows move).
        MainMenuRow l_eFrom = MainMenuRow::Settings;
        if (m_eInGameMenuPage == InGameMenuPage::Skills) l_eFrom = MainMenuRow::Skills;
        if (m_eInGameMenuPage == InGameMenuPage::Forge)  l_eFrom = MainMenuRow::Forge;
        if (m_eInGameMenuPage == InGameMenuPage::Hub)    l_eFrom = MainMenuRow::Village;
        m_iInGameMenuSel = f_MainMenuRowIndex(l_eFrom);
        m_eInGameMenuPage = InGameMenuPage::Main;
        m_oAudio.f_Play("close_ui");
        return;
    }
    f_ClosePlayerMenu();
}

// Saves, tears down transient modal/run-in-progress state, and reopens the title -- the
// inverse of newGame()/applyLoadedRun() (see their resets for what this mirrors). Unlike
// newGame(), this deliberately leaves pl/meta_/entityStatus_/activeSlot_/curStage alone: the
// run is only SUSPENDED, not discarded, so Continue can pick it back up.
void Game::f_ReturnToTitle() {
    f_SaveCurrentRun();   // flush before leaving so nothing is lost
    m_oCs = CombatState{};
    m_bInDialogue = false; m_vDlgChoices.clear(); m_strDlgNode = "root";
    m_bStoreOpen = false; m_bStoreUnlockDlg = false; m_bStoreFromMenu = false;
    m_bStageSelectOpen = false; m_bStairsConfirmOpen = false;
    if (m_bInGameMenuOpen) f_CloseInGameMenu();
    m_mapMissionTrackers.clear();
    m_vNotifications.clear();
    m_oTitle.f_SetRunInProgress(false);
    m_oTitle.f_Open();
    f_RefreshArtStyle();   // a style chosen during the run applies now (nothing of the run is on screen)
    f_RefreshSlots();
}

void Game::f_StoreKey(int l_iKey) {
    // unlock dialog: Enter/Esc/Space confirms
    if (m_bStoreUnlockDlg) {
        if (l_iKey == 13 || l_iKey == 27 || l_iKey == 32) { m_bStoreUnlockDlg = false; m_oAudio.f_Play("confirm_click"); }
        return;
    }
    if (!m_bStoreOpen) return;
    // Milestone 8: storeSel_ is an index into the current tab's filtered list, not storeItems_
    // directly (see storeTabIndices()) -- map through it before buying.
    std::vector<int> l_vIdxList = f_StoreTabIndices();
    int l_iN = (int)l_vIdxList.size();
    if (l_iKey == 27) { f_CloseStore(); return; }                       // Esc closes
    if (l_iKey == 13 || l_iKey == 32) {                                  // Enter/Space buys
        if (m_iStoreSel >= 0 && m_iStoreSel < l_iN) f_BuyStoreItem(l_vIdxList[m_iStoreSel]);
        return;
    }
    if (l_iKey == 262 || l_iKey == 263) {                                // right/left arrow
        m_iStoreSel += (l_iKey == 262) ? 1 : -1;
        if (m_iStoreSel < 0) m_iStoreSel = l_iN-1; if (m_iStoreSel >= l_iN) m_iStoreSel = 0;
        return;
    }
    if (l_iKey >= '1' && l_iKey <= '9') {                                // number keys select
        int l_iSel = l_iKey - '1';
        if (l_iSel < l_iN) m_iStoreSel = l_iSel;
    }
}

void Game::f_BuyStoreItem(int l_iIdx) {
    if (l_iIdx < 0 || l_iIdx >= (int)m_vStoreItems.size()) return;
    StoreItemDef& l_oD = m_vStoreItems[l_iIdx];
    // Gear the player already owns is not bought twice: tapping it wears it.
    if (f_IsGearType(f_ItemType(l_oD.m_strId)) && std::find(m_vGearOwned.begin(), m_vGearOwned.end(), l_oD.m_strId) != m_vGearOwned.end()) {
        f_EquipGear(l_oD.m_strId);
        m_strToastMsg = f_TrParam(m_oLocale.f_Tr("store.toast_purchase_success"), "name", f_ItemName(l_oD.m_strId));
        m_iToastTimer = 1200;
        m_oAudio.f_Play("confirm_click");
        f_MarkProgressDirty();
        return;
    }
    int l_iCost = l_oD.f_LiveCost();
    if (m_oPl.m_iGold < l_iCost) {
        // not enough gold -> toast + shake (simple effect), no purchase
        m_strToastMsg = f_TrParam(m_oLocale.f_Tr("store.toast_insufficient_gold"), "cost", std::to_string(l_iCost));
        m_iToastTimer = 1600;
        m_iShakeTimer = 350;
        m_oAudio.f_Play("deny");
        return;
    }
    m_oPl.m_iGold -= l_iCost;
    // A weapon / armor / talent joins the owned gear and is worn at once (the Equipment tab swaps it
    // later); a consumable is used right away, as the store always did.
    if (f_IsGearType(f_ItemType(l_oD.m_strId))) f_OwnGear(l_oD.m_strId, true);
    else f_ApplyStats(l_oD.m_strId, f_ItemStats(l_oD.m_strId));
    l_oD.m_iPurchases++;
    m_strToastMsg = f_TrParam(m_oLocale.f_Tr("store.toast_purchase_success"), "name", f_ItemName(l_oD.m_strId));
    m_iToastTimer = 1400;
    m_oAudio.f_Play("get_item");
    f_MarkProgressDirty();   // a purchase changes gold/stats/equipment: autosave will flush it
}
