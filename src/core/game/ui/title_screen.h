// title_screen.h — the title phase (Boot screen): New Game / Continue / Settings.
//
// Owns only the *state machine* of the title phase — which page is showing, what is
// highlighted, and what the player asked for. Everything with side effects (resetting the
// player, loading a stage, loading/writing a slot, switching language) is done by Game, which
// receives a TitleAction from activate()/cancel()/click() and acts on it. That split is what
// lets title_screen_test exercise the whole flow headlessly, with no renderer or Vulkan.
//
// Pages:
//   Menu     -> New Game | Continue | Settings
//   Continue -> one row per save slot (data/save/slotN.json) + Back
//   Settings -> language rows + Back
//
// game_state.h already names these screens (GameState::MainMenu / GameState::Settings) and
// already allows MainMenu -> {StageSelect, Settings} and Settings -> MainMenu, so the title
// phase plugs into the existing state machine rather than inventing a parallel one — see
// Game::currentState().
#pragma once
#include "save_slots.h"
#include <string>
#include <vector>

namespace toms {

enum class TitlePage { Menu, Continue, Settings };

inline const char* toString(TitlePage p) {
    switch (p) {
        case TitlePage::Menu:     return "Menu";
        case TitlePage::Continue: return "Continue";
        case TitlePage::Settings: return "Settings";
    }
    return "?";
}

enum class TitleAction {
    None,
    NewGame,        // start a fresh run (caller resets state + loads stage01)
    LoadSlot,       // resume from pendingSlot()
    OpenContinue,   // page switched to Continue
    OpenSettings,   // page switched to Settings
    SetLanguage,    // Settings: a language change was CONFIRMED, caller should apply + persist
    Back,           // left Continue/Settings, now on Menu
    // An EMPTY slot was activated: show the "start a new game in this slot?" confirm dialog
    // (the title draws it itself -- the caller has nothing to do).
    AskNewGameInSlot,
    // The dialog was answered Yes: start a fresh run in pendingSlot().
    StartNewGameInSlot,
    // The dialog was answered No (or Esc): stay on the Continue page.
    DismissNewGameConfirm,
    // A language row was activated/tapped: show the "switch to XXX?" confirm dialog (the title
    // draws it itself -- the caller has nothing to do). Mirrors AskNewGameInSlot -- selecting a
    // language used to apply instantly on a single tap, which is one wrong tap away from
    // stranding a touch/mobile player with no keyboard to undo it.
    AskLanguageChange,
    // The dialog was answered No (or Esc): stay on Settings, nothing changes.
    DismissLanguageConfirm,
};

// One clickable row, in design-resolution pixels (1024x768).
struct TitleRow { float x = 0, y = 0, w = 0, h = 0; bool contains(float px, float py) const {
    return px >= x && px <= x + w && py >= y && py <= y + h; } };

// Pure layout math, shared by the draw pass and click hit-testing so a tap always lands on the
// row that was actually drawn (same numbers, one source of truth — no renderer needed, so this
// is unit-testable).
struct TitleLayout {
    static constexpr int kMaxMenuRows = 3;
    static constexpr int kMaxSlotRows = 8;
    static constexpr int kMaxLangRows = 4;

    TitleRow menuRow[kMaxMenuRows];
    int menuRowCount = 0;
    TitleRow slotRow[kMaxSlotRows];
    int slotRowCount = 0;
    TitleRow langRow[kMaxLangRows];
    int langRowCount = 0;
    TitleRow backButton;
    // "Start a new game in this slot?" dialog: panel + its two answer buttons.
    TitleRow confirmBox;
    TitleRow confirmYes;
    TitleRow confirmNo;

    // Row index under the point, or -1. hitBack() is separate because Back is not a list row.
    int hitMenuRow(float x, float y) const;
    int hitSlotRow(float x, float y) const;
    int hitLangRow(float x, float y) const;
    bool hitBack(float x, float y) const;
    // 0 = Yes, 1 = No, -1 = neither.
    int hitConfirmButton(float x, float y) const;
};

TitleLayout computeTitleLayout(int designW, int designH, int slotCount, int langCount);

class TitleScreen {
public:
    static constexpr int kMenuItemCount = 3;   // New Game / Continue / Settings

    void open();                       // show the title, reset to the Menu page
    void close() { open_ = false; closeConfirm(); closeLanguageConfirm(); }
    bool isOpen() const { return open_; }

    TitlePage page() const { return page_; }
    void setPage(TitlePage p);
    int selection() const;
    int menuSelection() const { return menuSel_; }
    int slotSelection() const { return slotSel_; }
    int settingsSelection() const { return setSel_; }

    int slotCount() const { return slotCount_; }
    void setSlotCount(int n);
    int languageCount() const { return langCount_; }
    void setLanguageCount(int n);
    void setLanguageIndex(int i);
    int languageIndex() const { return langIdx_; }

    void setSummaries(std::vector<SlotSummary> s) { slots_ = std::move(s); }
    const std::vector<SlotSummary>& summaries() const { return slots_; }
    // 1-based slot number of the highlighted Continue row (even when that slot is empty).
    int selectedSlotNumber() const;
    // First slot that actually has a save, or -1. Used to park the cursor on something playable.
    int firstPlayableSlot() const;
    bool selectedSlotExists() const;

    bool runInProgress() const { return runInProgress_; }
    void setRunInProgress(bool v);

    // Navigation, clamped per page. Return an action when the move itself was an action
    // (Settings' left/right changes the language), else None.
    TitleAction moveVertical(int delta);
    TitleAction moveHorizontal(int delta);
    // Enter/Space: performs the highlighted row. May change pages.
    TitleAction activate();
    // Esc: Continue/Settings -> Menu; on Menu it is not handled here (caller decides: desktop
    // quits, the browser build ignores it).
    TitleAction cancel();
    // Tap/click in design space. Uses the layout the draw pass produced.
    TitleAction click(float px, float py, const TitleLayout& layout);

    int pendingSlot() const { return pendingSlot_; }   // valid for LoadSlot / StartNewGameInSlot

    // ---- "Start a new game in this slot?" confirm dialog ----
    // Shown when the player activates an EMPTY slot on the Continue page (the owner's ask:
    // picking an empty slot used to do nothing visible). Yes starts a fresh run in that slot
    // instead of silently doing nothing; No/Esc returns to the list.
    bool newGameConfirmOpen() const { return confirmNewGame_; }
    int newGameConfirmSlot() const { return confirmSlot_; }
    bool newGameConfirmYesSelected() const { return confirmYes_; }
    void setNewGameConfirmYesSelected(bool yes) { confirmYes_ = yes; }

    // ---- "Switch to XXX language?" confirm dialog ----
    // Shown when the player activates/taps a language row on the Settings page, instead of
    // applying it instantly -- a single mis-tap on a touch device (no keyboard, no Esc) used to
    // have no way back. No/Esc leaves the active language untouched.
    bool languageConfirmOpen() const { return confirmLanguage_; }
    int languageConfirmIndex() const { return confirmLangIdx_; }
    bool languageConfirmYesSelected() const { return confirmLangYes_; }
    void setLanguageConfirmYesSelected(bool yes) { confirmLangYes_ = yes; }

private:
    bool open_ = false;
    TitlePage page_ = TitlePage::Menu;
    int menuSel_ = 0;
    int slotSel_ = 0;
    int setSel_ = 0;
    int slotCount_ = 3;
    int langCount_ = 2;
    int langIdx_ = 0;
    int pendingSlot_ = 0;
    bool runInProgress_ = false;
    std::vector<SlotSummary> slots_;
    bool confirmNewGame_ = false;   // dialog visible
    int confirmSlot_ = 0;           // slot the dialog is about (1-based)
    bool confirmYes_ = true;        // which answer is highlighted (Yes is the default)
    void closeConfirm() { confirmNewGame_ = false; confirmSlot_ = 0; confirmYes_ = true; }

    bool confirmLanguage_ = false;  // dialog visible
    int confirmLangIdx_ = 0;        // language row the dialog is about
    bool confirmLangYes_ = true;    // which answer is highlighted (Yes is the default)
    void closeLanguageConfirm() { confirmLanguage_ = false; confirmLangIdx_ = 0; confirmLangYes_ = true; }
};

} // namespace toms
