#include "title_screen.h"
#include <algorithm>

namespace toms {

// ---------------------------------------------------------------- layout

int TitleLayout::hitMenuRow(float x, float y) const {
    for (int i = 0; i < menuRowCount; i++) if (menuRow[i].contains(x, y)) return i;
    return -1;
}
int TitleLayout::hitSlotRow(float x, float y) const {
    for (int i = 0; i < slotRowCount; i++) if (slotRow[i].contains(x, y)) return i;
    return -1;
}
int TitleLayout::hitLangRow(float x, float y) const {
    for (int i = 0; i < langRowCount; i++) if (langRow[i].contains(x, y)) return i;
    return -1;
}
bool TitleLayout::hitBack(float x, float y) const { return backButton.contains(x, y); }

int TitleLayout::hitConfirmButton(float x, float y) const {
    if (confirmYes.contains(x, y)) return 0;
    if (confirmNo.contains(x, y)) return 1;
    return -1;
}

TitleLayout computeTitleLayout(int designW, int designH, int slotCount, int langCount) {
    TitleLayout L;
    const float W = (float)designW, H = (float)designH;

    // Menu: three stacked buttons, centered.
    {
        const float w = 360.0f, h = 64.0f, gap = 78.0f;
        const float x = (W - w) * 0.5f, y0 = H * 0.43f;
        for (int i = 0; i < TitleLayout::kMaxMenuRows; i++)
            L.menuRow[i] = TitleRow{ x, y0 + i * gap, w, h };
        L.menuRowCount = TitleLayout::kMaxMenuRows;
    }

    // Continue: one row per slot, fitted into the space above the Back button so any slot
    // count (1..8) stays inside the screen instead of running off the bottom.
    {
        const float w = 720.0f, x = (W - w) * 0.5f;
        const float y0 = 250.0f, yLim = H - 140.0f;
        int n = std::max(1, std::min(slotCount, TitleLayout::kMaxSlotRows));
        float pitch = (yLim - y0) / (float)n;
        float rh = std::min(64.0f, pitch - 10.0f);
        if (rh < 24.0f) rh = 24.0f;
        for (int i = 0; i < n; i++) L.slotRow[i] = TitleRow{ x, y0 + i * pitch, w, rh };
        L.slotRowCount = n;
    }

    // Settings: one row per language (highlight doubles as the current selection).
    {
        const float w = 520.0f, x = (W - w) * 0.5f;
        const float y0 = 330.0f, yLim = H - 140.0f;
        int n = std::max(1, std::min(langCount, TitleLayout::kMaxLangRows));
        float pitch = (yLim - y0) / (float)n;
        float rh = std::min(64.0f, pitch - 10.0f);
        if (rh < 24.0f) rh = 24.0f;
        for (int i = 0; i < n; i++) L.langRow[i] = TitleRow{ x, y0 + i * pitch, w, rh };
        L.langRowCount = n;
    }

    // Back button (Continue / Settings pages).
    {
        const float w = 220.0f, h = 52.0f;
        L.backButton = TitleRow{ (W - w) * 0.5f, H - 100.0f, w, h };
    }

    // "Start a new game in this slot?" dialog: centered panel with a Yes/No pair.
    {
        const float w = 560.0f, h = 210.0f;
        const float bx = (W - w) * 0.5f, by = (H - h) * 0.5f;
        L.confirmBox = TitleRow{ bx, by, w, h };
        const float bw = 160.0f, bh = 56.0f, gap = 40.0f;
        const float groupW = bw * 2 + gap;
        const float bx0 = bx + (w - groupW) * 0.5f;
        const float byy = by + h - bh - 26.0f;
        L.confirmYes = TitleRow{ bx0, byy, bw, bh };
        L.confirmNo  = TitleRow{ bx0 + bw + gap, byy, bw, bh };
    }
    return L;
}

// ---------------------------------------------------------------- state machine

void TitleScreen::open() {
    open_ = true;
    page_ = TitlePage::Menu;
    menuSel_ = 0;
    slotSel_ = 0;
    setSel_ = 0;
    pendingSlot_ = 0;
    closeConfirm();
}

void TitleScreen::setPage(TitlePage p) {
    page_ = p;
    closeConfirm();   // never carry the dialog across pages
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
    slotCount_ = std::max(1, std::min(n, TitleLayout::kMaxSlotRows));
    if (slotSel_ >= slotCount_) slotSel_ = slotCount_ - 1;
}

void TitleScreen::setLanguageCount(int n) {
    langCount_ = std::max(1, std::min(n, TitleLayout::kMaxLangRows));
    if (langIdx_ >= langCount_) langIdx_ = langCount_ - 1;
    if (setSel_ >= langCount_) setSel_ = langCount_ - 1;
}

void TitleScreen::setLanguageIndex(int i) {
    if (i < 0 || i >= langCount_) return;
    langIdx_ = i;
    setSel_ = i;
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
    // The confirm dialog is a 2-button prompt: any direction toggles which answer is armed.
    if (confirmNewGame_) { confirmYes_ = !confirmYes_; return TitleAction::None; }
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
            int n = std::max(1, langCount_);
            setSel_ = ((setSel_ + delta) % n + n) % n;
            langIdx_ = setSel_;
            return TitleAction::SetLanguage;
        }
    }
    return TitleAction::None;
}

TitleAction TitleScreen::moveHorizontal(int delta) {
    if (delta == 0) return TitleAction::None;
    if (confirmNewGame_) { confirmYes_ = !confirmYes_; return TitleAction::None; }
    // Only the Settings page uses left/right: it is a 2-4 option picker, not a list.
    if (page_ != TitlePage::Settings) return TitleAction::None;
    return moveVertical(delta);
}

TitleAction TitleScreen::activate() {
    // Confirm dialog first: it is the topmost thing on screen.
    if (confirmNewGame_) {
        const bool yes = confirmYes_;
        const int slot = confirmSlot_;
        closeConfirm();
        if (yes) { pendingSlot_ = slot; return TitleAction::StartNewGameInSlot; }
        return TitleAction::DismissNewGameConfirm;
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
        case TitlePage::Settings:
            return TitleAction::None;   // language already applied by moveHorizontal/Vertical
    }
    return TitleAction::None;
}

TitleAction TitleScreen::cancel() {
    if (confirmNewGame_) { closeConfirm(); return TitleAction::DismissNewGameConfirm; }
    if (page_ == TitlePage::Continue || page_ == TitlePage::Settings) {
        page_ = TitlePage::Menu;
        return TitleAction::Back;
    }
    return TitleAction::None;   // caller decides what Esc means on the Menu page
}

TitleAction TitleScreen::click(float px, float py, const TitleLayout& layout) {
    // The dialog swallows taps: only its two answers are live.
    if (confirmNewGame_) {
        const int b = layout.hitConfirmButton(px, py);
        if (b < 0) return TitleAction::None;
        confirmYes_ = (b == 0);
        return activate();
    }
    switch (page_) {
        case TitlePage::Menu: {
            int r = layout.hitMenuRow(px, py);
            if (r < 0 || r >= kMenuItemCount) return TitleAction::None;
            menuSel_ = r;
            return activate();
        }
        case TitlePage::Continue: {
            int r = layout.hitSlotRow(px, py);
            if (r >= 0 && r < slotCount_) { slotSel_ = r; return activate(); }
            if (layout.hitBack(px, py)) return cancel();
            return TitleAction::None;
        }
        case TitlePage::Settings: {
            int r = layout.hitLangRow(px, py);
            if (r >= 0 && r < langCount_) { setSel_ = r; langIdx_ = r; return TitleAction::SetLanguage; }
            if (layout.hitBack(px, py)) return cancel();
            return TitleAction::None;
        }
    }
    return TitleAction::None;
}

} // namespace toms
