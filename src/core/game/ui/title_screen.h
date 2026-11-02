// title_screen.h — the title phase (Boot screen): New Game / Continue / Settings.
//
// Owns only the *state machine* of the title phase — which page is showing, what is
// highlighted, and what the player asked for. Everything with side effects (resetting the
// player, loading a stage, loading/writing a slot, switching language) is done by Game, which
// receives a TitleAction from activate()/cancel()/clickRow() and acts on it. That split is what
// lets title_screen_test exercise the whole flow headlessly, with no renderer or UI.
//
// Pages:
//   Menu     -> New Game | Continue | Settings
//   Continue -> one row per save slot (data/save/slotN.json) + Back
//   Settings -> language rows, then art style rows (only when there is more than one style) + Back
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

inline const char* f_ToString(TitlePage l_eP) {
    switch (l_eP) {
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
    // An art style row was activated: show "switch the art style to XXX? (needs a restart)".
    AskStyleChange,
    // That dialog was answered Yes: caller persists styleIndex() (it shows after a restart).
    SetArtStyle,
    // That dialog was answered No (or Esc): nothing changes.
    DismissStyleConfirm,
};

class TitleScreen {
public:
    static constexpr int m_iMenuItemCount = 3;   // New Game / Continue / Settings
    static constexpr int m_iMaxSlots = 8;        // save slots listed on the Continue page
    static constexpr int m_iMaxLanguages = 16;

    void f_Open();                       // show the title, reset to the Menu page
    void f_Close() { m_bOpen = false; f_CloseConfirm(); f_CloseLanguageConfirm(); f_CloseStyleConfirm(); }
    bool f_IsOpen() const { return m_bOpen; }

    TitlePage f_Page() const { return m_ePage; }
    void f_SetPage(TitlePage l_eP);
    int f_Selection() const;
    int f_MenuSelection() const { return m_iMenuSel; }
    int f_SlotSelection() const { return m_iSlotSel; }
    int f_SettingsSelection() const { return m_iSetSel; }

    int f_SlotCount() const { return m_iSlotCount; }
    void f_SetSlotCount(int l_iN);
    int f_LanguageCount() const { return m_iLangCount; }
    void f_SetLanguageCount(int l_iN);
    void f_SetLanguageIndex(int l_iI);
    int f_LanguageIndex() const { return m_iLangIdx; }
    // Art styles: rows after the languages on the Settings page. 0 = no style rows (only the
    // original art exists); otherwise the style count including the original.
    int f_StyleCount() const { return m_iStyleCount; }
    void f_SetStyleCount(int l_iN);
    void f_SetStyleIndex(int l_iI);
    int f_StyleIndex() const { return m_iStyleIdx; }        // the chosen style (persisted by Game)
    int f_SettingsRowCount() const { return m_iLangCount + m_iStyleCount; }

    void f_SetSummaries(std::vector<SlotSummary> l_vS) { m_vSlots = std::move(l_vS); }
    const std::vector<SlotSummary>& f_Summaries() const { return m_vSlots; }
    // 1-based slot number of the highlighted Continue row (even when that slot is empty).
    int f_SelectedSlotNumber() const;
    // First slot that actually has a save, or -1. Used to park the cursor on something playable.
    int f_FirstPlayableSlot() const;
    bool f_SelectedSlotExists() const;

    bool f_RunInProgress() const { return m_bRunInProgress; }
    void f_SetRunInProgress(bool l_bV);

    // Navigation, clamped per page. Return an action when the move itself was an action
    // (Settings' left/right changes the language), else None.
    TitleAction f_MoveVertical(int l_iDelta);
    TitleAction f_MoveHorizontal(int l_iDelta);
    // Enter/Space: performs the highlighted row. May change pages.
    TitleAction f_Activate();
    // Esc: Continue/Settings -> Menu; on Menu it is not handled here (caller decides: desktop
    // quits, the browser build ignores it).
    TitleAction f_Cancel();
    // Mouse/touch, by row: the UI (assets/media/ui/title.rml) lists the current page's rows in
    // the same order as the keyboard cursor. clickRow(-1) = the Back button. hoverRow() only moves
    // the highlight. answerConfirm() answers whichever confirm dialog is open.
    TitleAction f_ClickRow(int l_iRow);
    void f_HoverRow(int l_iRow);
    TitleAction f_AnswerConfirm(bool l_bYes);

    int f_PendingSlot() const { return m_iPendingSlot; }   // valid for LoadSlot / StartNewGameInSlot

    // ---- "Start a new game in this slot?" confirm dialog ----
    // Shown when the player activates an EMPTY slot on the Continue page (the owner's ask:
    // picking an empty slot used to do nothing visible). Yes starts a fresh run in that slot
    // instead of silently doing nothing; No/Esc returns to the list.
    bool f_NewGameConfirmOpen() const { return m_bConfirmNewGame; }
    int f_NewGameConfirmSlot() const { return m_iConfirmSlot; }
    bool f_NewGameConfirmYesSelected() const { return m_bConfirmYes; }
    void f_SetNewGameConfirmYesSelected(bool l_bYes) { m_bConfirmYes = l_bYes; }

    // ---- "Switch to XXX language?" confirm dialog ----
    // Shown when the player activates/taps a language row on the Settings page, instead of
    // applying it instantly -- a single mis-tap on a touch device (no keyboard, no Esc) used to
    // have no way back. No/Esc leaves the active language untouched.
    bool f_LanguageConfirmOpen() const { return m_bConfirmLanguage; }
    int f_LanguageConfirmIndex() const { return m_iConfirmLangIdx; }
    bool f_LanguageConfirmYesSelected() const { return m_bConfirmLangYes; }
    void f_SetLanguageConfirmYesSelected(bool l_bYes) { m_bConfirmLangYes = l_bYes; }

    // ---- "Switch the art style to XXX? It applies after a restart." confirm dialog ----
    bool f_StyleConfirmOpen() const { return m_bConfirmStyle; }
    int f_StyleConfirmIndex() const { return m_iConfirmStyleIdx; }
    bool f_StyleConfirmYesSelected() const { return m_bConfirmStyleYes; }
    bool f_AnyConfirmOpen() const { return m_bConfirmNewGame || m_bConfirmLanguage || m_bConfirmStyle; }

private:
    bool m_bOpen = false;
    TitlePage m_ePage = TitlePage::Menu;
    int m_iMenuSel = 0;
    int m_iSlotSel = 0;
    int m_iSetSel = 0;
    int m_iSlotCount = 3;
    int m_iLangCount = 2;
    int m_iLangIdx = 0;
    int m_iPendingSlot = 0;
    bool m_bRunInProgress = false;
    std::vector<SlotSummary> m_vSlots;
    bool m_bConfirmNewGame = false;   // dialog visible
    int m_iConfirmSlot = 0;           // slot the dialog is about (1-based)
    bool m_bConfirmYes = true;        // which answer is highlighted (Yes is the default)
    void f_CloseConfirm() { m_bConfirmNewGame = false; m_iConfirmSlot = 0; m_bConfirmYes = true; }

    bool m_bConfirmLanguage = false;  // dialog visible
    int m_iConfirmLangIdx = 0;        // language row the dialog is about
    bool m_bConfirmLangYes = true;    // which answer is highlighted (Yes is the default)
    void f_CloseLanguageConfirm() { m_bConfirmLanguage = false; m_iConfirmLangIdx = 0; m_bConfirmLangYes = true; }

    int m_iStyleCount = 0;
    int m_iStyleIdx = 0;
    bool m_bConfirmStyle = false;     // dialog visible
    int m_iConfirmStyleIdx = 0;       // style the dialog is about
    bool m_bConfirmStyleYes = true;
    void f_CloseStyleConfirm() { m_bConfirmStyle = false; m_iConfirmStyleIdx = 0; m_bConfirmStyleYes = true; }
};

} // namespace toms
