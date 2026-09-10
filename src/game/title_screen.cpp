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
}

void TitleScreen::setPage(TitlePage p) {
    page_ = p;
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
    // Only the Settings page uses left/right: it is a 2-4 option picker, not a list.
    if (page_ != TitlePage::Settings || delta == 0) return TitleAction::None;
    return moveVertical(delta);
}

TitleAction TitleScreen::activate() {
    switch (page_) {
        case TitlePage::Menu:
            switch (menuSel_) {
                case 0: return TitleAction::NewGame;
                case 1: setPage(TitlePage::Continue); return TitleAction::OpenContinue;
                case 2: setPage(TitlePage::Settings); return TitleAction::OpenSettings;
                default: return TitleAction::None;
            }
        case TitlePage::Continue:
            if (slotSel_ < 0 || slotSel_ >= slotCount_) return TitleAction::None;
            pendingSlot_ = slotSel_ + 1;
            return TitleAction::LoadSlot;
        case TitlePage::Settings:
            return TitleAction::None;   // language already applied by moveHorizontal/Vertical
    }
    return TitleAction::None;
}

TitleAction TitleScreen::cancel() {
    if (page_ == TitlePage::Continue || page_ == TitlePage::Settings) {
        page_ = TitlePage::Menu;
        return TitleAction::Back;
    }
    return TitleAction::None;   // caller decides what Esc means on the Menu page
}

TitleAction TitleScreen::click(float px, float py, const TitleLayout& layout) {
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
