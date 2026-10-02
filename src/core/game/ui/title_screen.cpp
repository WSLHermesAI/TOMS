#include "title_screen.h"
#include <algorithm>

namespace toms {

// ---------------------------------------------------------------- state machine

void TitleScreen::open() {
    open_ = true;
    page_ = TitlePage::Menu;
    menuSel_ = 0;
    slotSel_ = 0;
    setSel_ = 0;
    pendingSlot_ = 0;
    closeConfirm();
    closeLanguageConfirm();
    closeStyleConfirm();
}

void TitleScreen::setPage(TitlePage p) {
    page_ = p;
    closeConfirm();          // never carry a dialog across pages
    closeLanguageConfirm();
    closeStyleConfirm();
    if (p == TitlePage::Continue) {
        int first = firstPlayableSlot();
        slotSel_ = (first > 0) ? first - 1 : 0;   // park on something the player can actually load
        if (slotSel_ >= slotCount_) slotSel_ = std::max(0, slotCount_ - 1);
    } else if (p == TitlePage::Settings) {
        setSel_ = langIdx_;
        if (setSel_ >= langCount_) setSel_ = std::max(0, langCount_ - 1);
    }
}

int TitleScreen::selection() const {
    switch (page_) {
        case TitlePage::Menu:     return menuSel_;
        case TitlePage::Continue: return slotSel_;
        case TitlePage::Settings: return setSel_;
    }
    return 0;
}

void TitleScreen::setSlotCount(int n) {
    slotCount_ = std::max(1, std::min(n, kMaxSlots));
    if (slotSel_ >= slotCount_) slotSel_ = slotCount_ - 1;
}

void TitleScreen::setLanguageCount(int n) {
    langCount_ = std::max(1, std::min(n, kMaxLanguages));
    if (langIdx_ >= langCount_) langIdx_ = langCount_ - 1;
    if (setSel_ >= langCount_) setSel_ = langCount_ - 1;
}

void TitleScreen::setLanguageIndex(int i) {
    if (i < 0 || i >= langCount_) return;
    langIdx_ = i;
    setSel_ = i;
}

void TitleScreen::setStyleCount(int n) {
    styleCount_ = n > 1 ? n : 0;              // one style = just the original: no rows to pick from
    if (styleIdx_ >= std::max(1, styleCount_)) styleIdx_ = 0;
    if (setSel_ >= settingsRowCount()) setSel_ = std::max(0, settingsRowCount() - 1);
}

void TitleScreen::setStyleIndex(int i) {
    if (i < 0 || i >= std::max(1, styleCount_)) return;
    styleIdx_ = i;
}

int TitleScreen::selectedSlotNumber() const { return slotSel_ + 1; }

int TitleScreen::firstPlayableSlot() const {
    for (size_t i = 0; i < slots_.size(); i++)
        if (slots_[i].exists) return (int)i + 1;
    return -1;
}

bool TitleScreen::selectedSlotExists() const {
    int i = slotSel_;
    return i >= 0 && i < (int)slots_.size() && slots_[i].exists;
}

void TitleScreen::setRunInProgress(bool v) { runInProgress_ = v; }

TitleAction TitleScreen::moveVertical(int delta) {
    if (delta == 0) return TitleAction::None;
    // Confirm dialogs are 2-button prompts: any direction toggles which answer is armed.
    if (confirmNewGame_) { confirmYes_ = !confirmYes_; return TitleAction::None; }
    if (confirmLanguage_) { confirmLangYes_ = !confirmLangYes_; return TitleAction::None; }
    if (confirmStyle_) { confirmStyleYes_ = !confirmStyleYes_; return TitleAction::None; }
    switch (page_) {
        case TitlePage::Menu: {
            int n = kMenuItemCount;
            menuSel_ = ((menuSel_ + delta) % n + n) % n;   // menus wrap
            return TitleAction::None;
        }
        case TitlePage::Continue: {
            int n = std::max(1, slotCount_);
            slotSel_ = std::max(0, std::min(slotSel_ + delta, n - 1));   // lists clamp
            return TitleAction::None;
        }
        case TitlePage::Settings: {
            // Just moves the highlight now -- activate()/tap opens the confirm dialog below,
            // which is what actually applies the language (see AskLanguageChange).
            int n = std::max(1, settingsRowCount());
            setSel_ = ((setSel_ + delta) % n + n) % n;
            return TitleAction::None;
        }
    }
    return TitleAction::None;
}

TitleAction TitleScreen::moveHorizontal(int delta) {
    if (delta == 0) return TitleAction::None;
    if (confirmNewGame_) { confirmYes_ = !confirmYes_; return TitleAction::None; }
    if (confirmLanguage_) { confirmLangYes_ = !confirmLangYes_; return TitleAction::None; }
    if (confirmStyle_) { confirmStyleYes_ = !confirmStyleYes_; return TitleAction::None; }
    // Only the Settings page uses left/right: it is a 2-4 option picker, not a list.
    if (page_ != TitlePage::Settings) return TitleAction::None;
    return moveVertical(delta);
}

TitleAction TitleScreen::activate() {
    // Confirm dialogs first: they are the topmost thing on screen.
    if (confirmNewGame_) {
        const bool yes = confirmYes_;
        const int slot = confirmSlot_;
        closeConfirm();
        if (yes) { pendingSlot_ = slot; return TitleAction::StartNewGameInSlot; }
        return TitleAction::DismissNewGameConfirm;
    }
    if (confirmLanguage_) {
        const bool yes = confirmLangYes_;
        const int idx = confirmLangIdx_;
        closeLanguageConfirm();
        if (yes) { langIdx_ = idx; return TitleAction::SetLanguage; }
        return TitleAction::DismissLanguageConfirm;
    }
    if (confirmStyle_) {
        const bool yes = confirmStyleYes_;
        const int idx = confirmStyleIdx_;
        closeStyleConfirm();
        if (yes) { styleIdx_ = idx; return TitleAction::SetArtStyle; }
        return TitleAction::DismissStyleConfirm;
    }
    switch (page_) {
        case TitlePage::Menu:
            switch (menuSel_) {
                case 0: return TitleAction::NewGame;
                case 1: setPage(TitlePage::Continue); return TitleAction::OpenContinue;
                case 2: setPage(TitlePage::Settings); return TitleAction::OpenSettings;
                default: return TitleAction::None;
            }
        case TitlePage::Continue: {
            if (slotSel_ < 0 || slotSel_ >= slotCount_) return TitleAction::None;
            const int slot = slotSel_ + 1;
            if (!selectedSlotExists()) {
                // Empty slot: ask before starting a new game here (owner's report: this used to
                // do nothing at all, which read as a broken button).
                confirmNewGame_ = true;
                confirmSlot_ = slot;
                confirmYes_ = true;      // "Yes, start a new game" is the default action
                pendingSlot_ = slot;
                return TitleAction::AskNewGameInSlot;
            }
            pendingSlot_ = slot;
            return TitleAction::LoadSlot;
        }
        case TitlePage::Settings: {
            if (setSel_ < 0 || setSel_ >= settingsRowCount()) return TitleAction::None;
            if (setSel_ >= langCount_) {   // an art style row
                confirmStyle_ = true;
                confirmStyleIdx_ = setSel_ - langCount_;
                confirmStyleYes_ = true;
                return TitleAction::AskStyleChange;
            }
            confirmLanguage_ = true;
            confirmLangIdx_ = setSel_;
            confirmLangYes_ = true;      // "Yes, switch" is the default action
            return TitleAction::AskLanguageChange;
        }
    }
    return TitleAction::None;
}

TitleAction TitleScreen::cancel() {
    if (confirmNewGame_) { closeConfirm(); return TitleAction::DismissNewGameConfirm; }
    if (confirmLanguage_) { closeLanguageConfirm(); return TitleAction::DismissLanguageConfirm; }
    if (confirmStyle_) { closeStyleConfirm(); return TitleAction::DismissStyleConfirm; }
    if (page_ == TitlePage::Continue || page_ == TitlePage::Settings) {
        page_ = TitlePage::Menu;
        return TitleAction::Back;
    }
    return TitleAction::None;   // caller decides what Esc means on the Menu page
}

// A tap on row `row` of the current page (-1 = the Back button). The UI draws the rows in the
// same order as rows()/the keyboard cursor, so a row index is all a click needs.
TitleAction TitleScreen::clickRow(int row) {
    if (anyConfirmOpen()) return TitleAction::None;   // a dialog is up: answer it
    if (row < 0) return page_ == TitlePage::Menu ? TitleAction::None : cancel();
    switch (page_) {
        case TitlePage::Menu:
            if (row >= kMenuItemCount) return TitleAction::None;
            menuSel_ = row;
            return activate();
        case TitlePage::Continue:
            if (row >= slotCount_) return TitleAction::None;
            slotSel_ = row;
            return activate();
        case TitlePage::Settings:
            if (row >= settingsRowCount()) return TitleAction::None;
            setSel_ = row;
            return activate();
    }
    return TitleAction::None;
}

void TitleScreen::hoverRow(int row) {
    if (anyConfirmOpen() || row < 0) return;
    switch (page_) {
        case TitlePage::Menu:     if (row < kMenuItemCount) menuSel_ = row; break;
        case TitlePage::Continue: if (row < slotCount_) slotSel_ = row; break;
        case TitlePage::Settings: if (row < settingsRowCount()) setSel_ = row; break;
    }
}

TitleAction TitleScreen::answerConfirm(bool yes) {
    if (confirmNewGame_) { confirmYes_ = yes; return activate(); }
    if (confirmLanguage_) { confirmLangYes_ = yes; return activate(); }
    if (confirmStyle_) { confirmStyleYes_ = yes; return activate(); }
    return TitleAction::None;
}

} // namespace toms
