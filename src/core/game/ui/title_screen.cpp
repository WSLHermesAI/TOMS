#include "title_screen.h"
#include <algorithm>

namespace toms {

// ---------------------------------------------------------------- state machine

void TitleScreen::f_Open() {
    m_bOpen = true;
    m_ePage = TitlePage::Menu;
    m_iMenuSel = 0;
    m_iSlotSel = 0;
    m_iSetSel = 0;
    m_iPendingSlot = 0;
    f_CloseConfirm();
    f_CloseLanguageConfirm();
    f_CloseStyleConfirm();
}

void TitleScreen::f_SetPage(TitlePage l_eP) {
    m_ePage = l_eP;
    f_CloseConfirm();          // never carry a dialog across pages
    f_CloseLanguageConfirm();
    f_CloseStyleConfirm();
    if (l_eP == TitlePage::Continue) {
        int l_iFirst = f_FirstPlayableSlot();
        m_iSlotSel = (l_iFirst > 0) ? l_iFirst - 1 : 0;   // park on something the player can actually load
        if (m_iSlotSel >= m_iSlotCount) m_iSlotSel = std::max(0, m_iSlotCount - 1);
    } else if (l_eP == TitlePage::Settings) {
        m_iSetSel = m_iLangIdx;
        if (m_iSetSel >= m_iLangCount) m_iSetSel = std::max(0, m_iLangCount - 1);
    }
}

int TitleScreen::f_Selection() const {
    switch (m_ePage) {
        case TitlePage::Menu:     return m_iMenuSel;
        case TitlePage::Continue: return m_iSlotSel;
        case TitlePage::Settings: return m_iSetSel;
    }
    return 0;
}

void TitleScreen::f_SetSlotCount(int l_iN) {
    m_iSlotCount = std::max(1, std::min(l_iN, m_iMaxSlots));
    if (m_iSlotSel >= m_iSlotCount) m_iSlotSel = m_iSlotCount - 1;
}

void TitleScreen::f_SetLanguageCount(int l_iN) {
    m_iLangCount = std::max(1, std::min(l_iN, m_iMaxLanguages));
    if (m_iLangIdx >= m_iLangCount) m_iLangIdx = m_iLangCount - 1;
    if (m_iSetSel >= m_iLangCount) m_iSetSel = m_iLangCount - 1;
}

void TitleScreen::f_SetLanguageIndex(int l_iI) {
    if (l_iI < 0 || l_iI >= m_iLangCount) return;
    m_iLangIdx = l_iI;
    m_iSetSel = l_iI;
}

void TitleScreen::f_SetStyleCount(int l_iN) {
    m_iStyleCount = l_iN > 1 ? l_iN : 0;              // one style = just the original: no rows to pick from
    if (m_iStyleIdx >= std::max(1, m_iStyleCount)) m_iStyleIdx = 0;
    if (m_iSetSel >= f_SettingsRowCount()) m_iSetSel = std::max(0, f_SettingsRowCount() - 1);
}

void TitleScreen::f_SetStyleIndex(int l_iI) {
    if (l_iI < 0 || l_iI >= std::max(1, m_iStyleCount)) return;
    m_iStyleIdx = l_iI;
}

int TitleScreen::f_SelectedSlotNumber() const { return m_iSlotSel + 1; }

int TitleScreen::f_FirstPlayableSlot() const {
    for (size_t l_nI = 0; l_nI < m_vSlots.size(); l_nI++)
        if (m_vSlots[l_nI].m_bExists) return (int)l_nI + 1;
    return -1;
}

bool TitleScreen::f_SelectedSlotExists() const {
    int l_iI = m_iSlotSel;
    return l_iI >= 0 && l_iI < (int)m_vSlots.size() && m_vSlots[l_iI].m_bExists;
}

void TitleScreen::f_SetRunInProgress(bool l_bV) { m_bRunInProgress = l_bV; }

TitleAction TitleScreen::f_MoveVertical(int l_iDelta) {
    if (l_iDelta == 0) return TitleAction::None;
    // Confirm dialogs are 2-button prompts: any direction toggles which answer is armed.
    if (m_bConfirmNewGame) { m_bConfirmYes = !m_bConfirmYes; return TitleAction::None; }
    if (m_bConfirmLanguage) { m_bConfirmLangYes = !m_bConfirmLangYes; return TitleAction::None; }
    if (m_bConfirmStyle) { m_bConfirmStyleYes = !m_bConfirmStyleYes; return TitleAction::None; }
    switch (m_ePage) {
        case TitlePage::Menu: {
            int l_iN = m_iMenuItemCount;
            m_iMenuSel = ((m_iMenuSel + l_iDelta) % l_iN + l_iN) % l_iN;   // menus wrap
            return TitleAction::None;
        }
        case TitlePage::Continue: {
            int l_iN = std::max(1, m_iSlotCount);
            m_iSlotSel = std::max(0, std::min(m_iSlotSel + l_iDelta, l_iN - 1));   // lists clamp
            return TitleAction::None;
        }
        case TitlePage::Settings: {
            // Just moves the highlight now -- activate()/tap opens the confirm dialog below,
            // which is what actually applies the language (see AskLanguageChange).
            int l_iN = std::max(1, f_SettingsRowCount());
            m_iSetSel = ((m_iSetSel + l_iDelta) % l_iN + l_iN) % l_iN;
            return TitleAction::None;
        }
    }
    return TitleAction::None;
}

TitleAction TitleScreen::f_MoveHorizontal(int l_iDelta) {
    if (l_iDelta == 0) return TitleAction::None;
    if (m_bConfirmNewGame) { m_bConfirmYes = !m_bConfirmYes; return TitleAction::None; }
    if (m_bConfirmLanguage) { m_bConfirmLangYes = !m_bConfirmLangYes; return TitleAction::None; }
    if (m_bConfirmStyle) { m_bConfirmStyleYes = !m_bConfirmStyleYes; return TitleAction::None; }
    // Only the Settings page uses left/right: it is a 2-4 option picker, not a list.
    if (m_ePage != TitlePage::Settings) return TitleAction::None;
    return f_MoveVertical(l_iDelta);
}

TitleAction TitleScreen::f_Activate() {
    // Confirm dialogs first: they are the topmost thing on screen.
    if (m_bConfirmNewGame) {
        const bool l_bYes = m_bConfirmYes;
        const int l_iSlot = m_iConfirmSlot;
        f_CloseConfirm();
        if (l_bYes) { m_iPendingSlot = l_iSlot; return TitleAction::StartNewGameInSlot; }
        return TitleAction::DismissNewGameConfirm;
    }
    if (m_bConfirmLanguage) {
        const bool l_bYes = m_bConfirmLangYes;
        const int l_iIdx = m_iConfirmLangIdx;
        f_CloseLanguageConfirm();
        if (l_bYes) { m_iLangIdx = l_iIdx; return TitleAction::SetLanguage; }
        return TitleAction::DismissLanguageConfirm;
    }
    if (m_bConfirmStyle) {
        const bool l_bYes = m_bConfirmStyleYes;
        const int l_iIdx = m_iConfirmStyleIdx;
        f_CloseStyleConfirm();
        if (l_bYes) { m_iStyleIdx = l_iIdx; return TitleAction::SetArtStyle; }
        return TitleAction::DismissStyleConfirm;
    }
    switch (m_ePage) {
        case TitlePage::Menu:
            switch (m_iMenuSel) {
                case 0: return TitleAction::NewGame;
                case 1: f_SetPage(TitlePage::Continue); return TitleAction::OpenContinue;
                case 2: f_SetPage(TitlePage::Settings); return TitleAction::OpenSettings;
                default: return TitleAction::None;
            }
        case TitlePage::Continue: {
            if (m_iSlotSel < 0 || m_iSlotSel >= m_iSlotCount) return TitleAction::None;
            const int l_iSlot = m_iSlotSel + 1;
            // damaged: nothing loads, and a new game must not write over what may still be recovered from the file
            if (m_iSlotSel < (int)m_vSlots.size() && m_vSlots[m_iSlotSel].m_bDamaged) return TitleAction::None;
            if (!f_SelectedSlotExists()) {
                // Empty slot: ask before starting a new game here (owner's report: this used to
                // do nothing at all, which read as a broken button).
                m_bConfirmNewGame = true;
                m_iConfirmSlot = l_iSlot;
                m_bConfirmYes = true;      // "Yes, start a new game" is the default action
                m_iPendingSlot = l_iSlot;
                return TitleAction::AskNewGameInSlot;
            }
            m_iPendingSlot = l_iSlot;
            return TitleAction::LoadSlot;
        }
        case TitlePage::Settings: {
            if (m_iSetSel < 0 || m_iSetSel >= f_SettingsRowCount()) return TitleAction::None;
            if (m_iSetSel >= m_iLangCount) {   // an art style row
                m_bConfirmStyle = true;
                m_iConfirmStyleIdx = m_iSetSel - m_iLangCount;
                m_bConfirmStyleYes = true;
                return TitleAction::AskStyleChange;
            }
            m_bConfirmLanguage = true;
            m_iConfirmLangIdx = m_iSetSel;
            m_bConfirmLangYes = true;      // "Yes, switch" is the default action
            return TitleAction::AskLanguageChange;
        }
    }
    return TitleAction::None;
}

TitleAction TitleScreen::f_Cancel() {
    if (m_bConfirmNewGame) { f_CloseConfirm(); return TitleAction::DismissNewGameConfirm; }
    if (m_bConfirmLanguage) { f_CloseLanguageConfirm(); return TitleAction::DismissLanguageConfirm; }
    if (m_bConfirmStyle) { f_CloseStyleConfirm(); return TitleAction::DismissStyleConfirm; }
    if (m_ePage == TitlePage::Continue || m_ePage == TitlePage::Settings) {
        m_ePage = TitlePage::Menu;
        return TitleAction::Back;
    }
    return TitleAction::None;   // caller decides what Esc means on the Menu page
}

// A tap on row `row` of the current page (-1 = the Back button). The UI draws the rows in the
// same order as rows()/the keyboard cursor, so a row index is all a click needs.
TitleAction TitleScreen::f_ClickRow(int l_iRow) {
    if (f_AnyConfirmOpen()) return TitleAction::None;   // a dialog is up: answer it
    if (l_iRow < 0) return m_ePage == TitlePage::Menu ? TitleAction::None : f_Cancel();
    switch (m_ePage) {
        case TitlePage::Menu:
            if (l_iRow >= m_iMenuItemCount) return TitleAction::None;
            m_iMenuSel = l_iRow;
            return f_Activate();
        case TitlePage::Continue:
            if (l_iRow >= m_iSlotCount) return TitleAction::None;
            m_iSlotSel = l_iRow;
            return f_Activate();
        case TitlePage::Settings:
            if (l_iRow >= f_SettingsRowCount()) return TitleAction::None;
            m_iSetSel = l_iRow;
            return f_Activate();
    }
    return TitleAction::None;
}

void TitleScreen::f_HoverRow(int l_iRow) {
    if (f_AnyConfirmOpen() || l_iRow < 0) return;
    switch (m_ePage) {
        case TitlePage::Menu:     if (l_iRow < m_iMenuItemCount) m_iMenuSel = l_iRow; break;
        case TitlePage::Continue: if (l_iRow < m_iSlotCount) m_iSlotSel = l_iRow; break;
        case TitlePage::Settings: if (l_iRow < f_SettingsRowCount()) m_iSetSel = l_iRow; break;
    }
}

TitleAction TitleScreen::f_AnswerConfirm(bool l_bYes) {
    if (m_bConfirmNewGame) { m_bConfirmYes = l_bYes; return f_Activate(); }
    if (m_bConfirmLanguage) { m_bConfirmLangYes = l_bYes; return f_Activate(); }
    if (m_bConfirmStyle) { m_bConfirmStyleYes = l_bYes; return f_Activate(); }
    return TitleAction::None;
}

} // namespace toms
