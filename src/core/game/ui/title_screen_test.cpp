// title_screen_test.cpp — headless verification of the title phase (New Game / Continue /
// Settings): the page state machine, save-slot I/O, persisted settings, and the localization
// table. No renderer/Vulkan/GLFW involvement — this is pure game logic plus file I/O, so it
// runs anywhere. Run: ./title_screen_test  (exit 0 = all checks pass)
#include "title_screen.h"
#include "game_settings.h"
#include "localization.h"
#include "art_styles.h"
#include <cstdio>
#include <fstream>
#include <filesystem>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

int main() {
    // ---------------------------------------------------------------- localization
    {
        Locale l_oLoc;
        CHECK(l_oLoc.f_LanguageCount() == 2, "Locale starts with 2 built-in languages");
        CHECK(l_oLoc.f_LanguageCode() == "en", "Locale defaults to English");
        CHECK(l_oLoc.f_Tr("menu.new_game") == "New Game", "built-in fallback: menu.new_game in English");
        CHECK(l_oLoc.f_SetLanguage("zh_TW") && l_oLoc.f_Tr("menu.new_game") == "新遊戲", "built-in fallback: menu.new_game in zh_TW");

        // Load the real shipped table when it is reachable from the test's CWD.
        const char* l_arrCandidates[] = { "data/text.json", "../data/text.json", "../../data/text.json" };
        bool l_bLoaded = false;
        for (const char* l_pszP : l_arrCandidates) if (l_oLoc.f_LoadFromFile(l_pszP)) { l_bLoaded = true; break; }
        CHECK(l_bLoaded, "data/text.json loads from one of the candidate paths");
        if (l_bLoaded) {
            CHECK(l_oLoc.f_StringCount() > 20, "loaded table defines the expected number of keys");
            CHECK(l_oLoc.f_LanguageCode() == "zh_TW", "text.json's default: Traditional Chinese");
            CHECK(l_oLoc.f_LanguageCount() == 2, "only the offered languages (zh_TW, en) can be picked");
            CHECK(l_oLoc.f_Has("settings.language"), "loaded table has settings.language");
            CHECK(l_oLoc.f_SetLanguage("en"), "setLanguage(\"en\") succeeds");
            CHECK(l_oLoc.f_Tr("menu.new_game") == "New Game", "menu.new_game switches to English");
            CHECK(l_oLoc.f_LanguageIndex() == 1, "language index tracks the selection");
            CHECK(l_oLoc.f_Tr("no.such.key") == "no.such.key", "unknown key returns the key itself");
        }
        CHECK(!l_oLoc.f_SetLanguage("fr"), "an unknown language code is rejected");
    }

    // ---------------------------------------------------------------- settings persistence
    {
        GameSettings l_oS;
        l_oS.m_strLanguage = "en";
        l_oS.m_iSlotCount = 4;
        l_oS.m_fUiFontScale = 1.75f;
        l_oS.m_iCameraMode = 1;
        auto l_jJ = f_ToJson(l_oS);
        GameSettings l_oS2 = f_GameSettingsFromJson(l_jJ);
        CHECK(l_oS2.m_strLanguage == "en" && l_oS2.m_iSlotCount == 4, "settings JSON round-trip");
        CHECK(l_oS2.m_iCameraMode == 1, "cameraMode round-trips");

        // Out-of-range values are clamped, not accepted blindly.
        nlohmann::json l_jBad;
        l_jBad["slotCount"] = 99;
        l_jBad["uiFontScale"] = 100.0f;
        l_jBad["cameraMode"] = 7;
        GameSettings l_oS3 = f_GameSettingsFromJson(l_jBad);
        CHECK(l_oS3.m_iSlotCount <= 8, "slotCount is clamped to the layout maximum");
        CHECK(l_oS3.m_fUiFontScale <= 4.0f, "uiFontScale is clamped to a sane range");
        CHECK(l_oS3.m_iCameraMode == 0, "an unrecognized cameraMode falls back to Follow (0)");

        // File round-trip.
        std::string l_strPath = "title_test_settings.json";
        std::error_code l_oEc;
        std::filesystem::remove(l_strPath, l_oEc);
        bool l_bFound = true;
        GameSettings l_oMissing = f_LoadGameSettings(l_strPath, &l_bFound);
        CHECK(!l_bFound, "a missing settings file reports found=false");
        CHECK(l_oMissing.m_strLanguage == "zh_TW", "a missing settings file falls back to defaults (Traditional Chinese)");
        nlohmann::json l_jV1;
        l_jV1["schemaVersion"] = 2;
        l_jV1["language"] = "en";
        CHECK(f_GameSettingsFromJson(l_jV1).m_strLanguage == "zh_TW", "a version-2 settings file (English default) moves to Chinese");
        l_jV1["schemaVersion"] = 3;
        CHECK(f_GameSettingsFromJson(l_jV1).m_strLanguage == "en", "a version-3 file keeps the language the player chose");

        CHECK(f_SaveGameSettings(l_strPath, l_oS), "saveGameSettings writes the file");
        bool l_bFound2 = false;
        GameSettings l_oS4 = f_LoadGameSettings(l_strPath, &l_bFound2);
        CHECK(l_bFound2, "saved settings file is found on reload");
        CHECK(l_oS4.m_strLanguage == "en" && l_oS4.m_iSlotCount == 4, "settings survive a file round-trip");
        std::filesystem::remove(l_strPath, l_oEc);
    }

    // ---------------------------------------------------------------- save slots
    {
        std::string l_strDir = "title_test_saves";
        std::error_code l_oEc;
        std::filesystem::remove_all(l_strDir, l_oEc);
        CHECK(f_EnsureSaveDir(l_strDir), "ensureSaveDir creates the slot directory");
        CHECK(f_FirstEmptySlot(l_strDir, 3) == 1, "with no files, the first empty slot is 1");
        CHECK(f_NewestSlot(l_strDir, 3) == -1, "with no files, there is no newest slot");
        CHECK(!f_SlotExists(l_strDir, 2), "slot 2 reports as absent before it is written");

        MetaSaveData l_oMeta;
        l_oMeta.m_iCurrentBeat = 3;
        l_oMeta.m_setStoryFlags = {"beat_02_tutorial_done"};
        l_oMeta.m_vUnlockedStages = {"stage_01", "stage_02"};
        RunSaveData l_oRun;
        l_oRun.m_strCurrentStageId = "stage_02";
        l_oRun.m_oPlayer.m_iHp = 77; l_oRun.m_oPlayer.m_iMaxhp = 130; l_oRun.m_oPlayer.m_iGold = 55; l_oRun.m_oPlayer.m_iLv = 3;
        l_oRun.m_oPlayer.m_vInv = {"potion_red", "gem_atk"};
        l_oRun.m_mapEntityStatus["stage_02|4,3"] = "Defeated";

        CHECK(f_WriteSlotSave(l_strDir, 1, l_oMeta, l_oRun, 3725, "2026-09-10 12:00:00", "廢墟迷宮"),
              "writeSlotSave writes slot 1");
        CHECK(f_SlotExists(l_strDir, 1), "slot 1 exists after writing");
        CHECK(f_FirstEmptySlot(l_strDir, 3) == 2, "the next empty slot advances to 2");

        SlotSummary l_oSum = f_SummarizeSlot(l_strDir, 1);
        CHECK(l_oSum.m_bExists, "summarizeSlot reports the slot as occupied");
        CHECK(l_oSum.m_iLv == 3 && l_oSum.m_iGold == 55 && l_oSum.m_iHp == 77,
              "summary carries the player's level/gold/hp for the Continue row");
        CHECK(l_oSum.m_strStageId == "stage_02", "summary carries the stage id");
        CHECK(l_oSum.m_strStageName == "廢墟迷宮", "summary carries the stage display name");
        CHECK(l_oSum.m_iPlayTimeSec == 3725, "summary carries play time");
        CHECK(l_oSum.m_strSavedAt == "2026-09-10 12:00:00", "summary carries the saved-at stamp");
        CHECK(f_SummarizeSlot(l_strDir, 2).m_bExists == false, "an unwritten slot summarizes as empty");

        MetaSaveData l_oMeta2; RunSaveData l_oRun2; int l_iPlaySec = 0; std::string l_strStamp;
        CHECK(f_ReadSlotSave(l_strDir, 1, l_oMeta2, l_oRun2, &l_iPlaySec, &l_strStamp), "readSlotSave succeeds");
        CHECK(l_oMeta2.m_vUnlockedStages.size() == 2 && l_oMeta2.m_iCurrentBeat == 3,
              "meta progress (story flags/stages) survives the slot round-trip");
        CHECK(l_oRun2.m_strCurrentStageId == "stage_02" && l_oRun2.m_oPlayer.m_iHp == 77,
              "run state survives the slot round-trip");
        CHECK(l_oRun2.m_mapEntityStatus.at("stage_02|4,3") == "Defeated",
              "per-tile entity status (cleared monsters) survives the slot round-trip");
        CHECK(l_iPlaySec == 3725, "play time survives the slot round-trip");

        CHECK(f_NewestSlot(l_strDir, 3) == 1, "newestSlot returns the only written slot");
        f_WriteSlotSave(l_strDir, 2, l_oMeta, l_oRun, 10, "2026-09-10 13:00:00", "廢墟迷宮");
        CHECK(f_NewestSlot(l_strDir, 3) == 2, "newestSlot follows the newest saved-at stamp");
        CHECK(f_FirstEmptySlot(l_strDir, 2) == -1, "firstEmptySlot returns -1 when every slot is taken");

        CHECK(f_DeleteSlot(l_strDir, 2), "deleteSlot removes a slot");
        CHECK(!f_SlotExists(l_strDir, 2), "a deleted slot reads as absent");
        std::filesystem::remove_all(l_strDir, l_oEc);
    }

    // ---------------------------------------------------------------- title state machine
    {
        TitleScreen l_oT;
        l_oT.f_Open();
        CHECK(l_oT.f_IsOpen(), "open() shows the title");
        CHECK(l_oT.f_Page() == TitlePage::Menu, "the title opens on the Menu page");
        CHECK(l_oT.f_MenuSelection() == 0, "the Menu starts on New Game");

        // Menu navigation wraps (3 items: New Game / Continue / Settings).
        l_oT.f_MoveVertical(1);
        CHECK(l_oT.f_MenuSelection() == 1, "down moves to Continue");
        l_oT.f_MoveVertical(1);
        l_oT.f_MoveVertical(1);
        CHECK(l_oT.f_MenuSelection() == 0, "menu selection wraps past the last item");

        // Continue page: parks the cursor on the first slot that actually has a save.
        std::vector<SlotSummary> l_vSums(3);
        for (int l_iI = 0; l_iI < 3; l_iI++) { l_vSums[l_iI].m_iSlot = l_iI + 1; l_vSums[l_iI].m_bExists = false; }
        l_vSums[1].m_bExists = true;      // slot 2 exists
        l_oT.f_SetSummaries(l_vSums);
        l_oT.f_SetSlotCount(3);
        TitleAction l_eA = l_oT.f_Activate();          // menuSel == 0 -> New Game
        CHECK(l_eA == TitleAction::NewGame, "activating the first Menu row asks for New Game");

        l_oT.f_SetPage(TitlePage::Continue);
        CHECK(l_oT.f_Page() == TitlePage::Continue, "setPage switches to Continue");
        CHECK(l_oT.f_SlotSelection() == 1, "Continue parks on the first slot that has a save (slot 2)");
        CHECK(l_oT.f_SelectedSlotNumber() == 2, "selectedSlotNumber is 1-based");
        CHECK(l_oT.f_SelectedSlotExists(), "the parked slot is reported as playable");
        l_eA = l_oT.f_Activate();
        CHECK(l_eA == TitleAction::LoadSlot, "activating a Continue row asks to load that slot");
        CHECK(l_oT.f_PendingSlot() == 2, "pendingSlot carries the highlighted slot number");

        // Vertical navigation clamps inside the list (no wrap).
        l_oT.f_MoveVertical(1);
        CHECK(l_oT.f_SlotSelection() == 2, "down moves one slot row");
        l_oT.f_MoveVertical(1);
        CHECK(l_oT.f_SlotSelection() == 2, "slot selection clamps at the last slot");
        l_oT.f_MoveVertical(-5);
        CHECK(l_oT.f_SlotSelection() == 0, "slot selection clamps at the first slot");

        // Nothing to load: the Game is expected to keep the title open (empty slot).
        l_oT.f_SetSummaries(std::vector<SlotSummary>(3));
        CHECK(l_oT.f_FirstPlayableSlot() == -1, "with no saves there is no playable slot");
        CHECK(!l_oT.f_SelectedSlotExists(), "an empty slot is not reported as playable");

        // Settings page: language rows. Moving only changes the highlight -- activate()/tap is
        // needed to open the "switch to XXX?" dialog, and only confirming it actually applies
        // the language (see the dedicated confirm-dialog test block below for the full flow).
        l_oT.f_SetPage(TitlePage::Settings);
        CHECK(l_oT.f_Page() == TitlePage::Settings, "setPage switches to Settings");
        l_oT.f_SetLanguageCount(2);
        l_oT.f_SetLanguageIndex(0);
        l_eA = l_oT.f_MoveVertical(1);
        CHECK(l_eA == TitleAction::None, "moving the highlight alone does not apply the language");
        CHECK(l_oT.f_SettingsSelection() == 1, "the highlighted row follows the move");
        CHECK(l_oT.f_LanguageIndex() == 0, "the active language is unchanged until confirmed");
        l_eA = l_oT.f_MoveHorizontal(1);
        CHECK(l_eA == TitleAction::None, "left/right also just moves the highlight");
        CHECK(l_oT.f_SettingsSelection() == 0, "the highlight wraps with left/right");

        // Esc goes Back to the Menu from a sub-page.
        l_eA = l_oT.f_Cancel();
        CHECK(l_eA == TitleAction::Back && l_oT.f_Page() == TitlePage::Menu, "cancel returns to the Menu");
        l_eA = l_oT.f_Cancel();
        CHECK(l_eA == TitleAction::None, "cancel on the Menu itself is left to the caller");

        // Clicks by row (the UI lists rows in keyboard-cursor order; -1 = Back).
        CHECK(l_oT.f_ClickRow(7) == TitleAction::None, "a row past the menu does nothing");
        l_oT.f_HoverRow(2);
        CHECK(l_oT.f_MenuSelection() == 2, "hovering moves the highlight only");
        l_eA = l_oT.f_ClickRow(1);
        CHECK(l_eA == TitleAction::OpenContinue, "tapping Continue opens the Continue page");
        CHECK(l_oT.f_Page() == TitlePage::Continue, "the tap actually changed the page");
        l_eA = l_oT.f_ClickRow(-1);
        CHECK(l_eA == TitleAction::Back, "tapping Back returns to the Menu");

        // Settings tap opens the "switch to XXX?" dialog rather than applying instantly.
        l_oT.f_SetPage(TitlePage::Settings);
        l_oT.f_SetLanguageCount(2);
        l_eA = l_oT.f_ClickRow(1);
        CHECK(l_eA == TitleAction::AskLanguageChange && l_oT.f_LanguageConfirmOpen(),
              "tapping a language row opens the confirm dialog instead of applying it");
        CHECK(l_oT.f_LanguageIndex() == 0, "the active language has not changed yet");
        CHECK(l_oT.f_LanguageConfirmIndex() == 1, "the dialog names the tapped row");
        l_oT.f_Cancel();   // leave it closed for the next block

        // Slot count is clamped to what the Continue page lists (8 rows).
        l_oT.f_SetSlotCount(99);
        CHECK(l_oT.f_SlotCount() == TitleScreen::m_iMaxSlots, "slotCount clamps to the page max");
    }

    // ------------------------------------------------- empty-slot "start a new game?" prompt
    // (owner's report: activating an empty Continue slot used to do nothing at all)
    {
        TitleScreen l_oT;
        l_oT.f_Open();
        l_oT.f_SetSlotCount(3);
        std::vector<SlotSummary> l_vSums(3);
        for (int l_iI = 0; l_iI < 3; l_iI++) { l_vSums[l_iI].m_iSlot = l_iI + 1; l_vSums[l_iI].m_bExists = false; }
        l_vSums[0].m_bExists = true;                  // slot 1 filled, slots 2-3 empty
        l_oT.f_SetSummaries(l_vSums);
        l_oT.f_SetPage(TitlePage::Continue);
        CHECK(l_oT.f_SlotSelection() == 0, "Continue parks on the filled slot 1");

        TitleAction l_eA = l_oT.f_Activate();
        CHECK(l_eA == TitleAction::LoadSlot, "a FILLED slot still loads directly");
        CHECK(!l_oT.f_NewGameConfirmOpen(), "no prompt for a filled slot");

        l_oT.f_MoveVertical(1);                      // -> slot 2 (empty)
        CHECK(l_oT.f_SelectedSlotNumber() == 2, "cursor moved to empty slot 2");
        l_eA = l_oT.f_Activate();
        CHECK(l_eA == TitleAction::AskNewGameInSlot, "activating an EMPTY slot asks for confirmation");
        CHECK(l_oT.f_NewGameConfirmOpen(), "the confirm prompt is open");
        CHECK(l_oT.f_NewGameConfirmSlot() == 2, "the prompt names the slot the player picked");
        CHECK(l_oT.f_NewGameConfirmYesSelected(), "Yes is armed by default");
        CHECK(l_oT.f_PendingSlot() == 2, "pendingSlot points at the chosen slot");

        l_eA = l_oT.f_Activate();
        CHECK(l_eA == TitleAction::StartNewGameInSlot, "Yes asks to start a new game in that slot");
        CHECK(!l_oT.f_NewGameConfirmOpen(), "answering closes the prompt");
        CHECK(l_oT.f_PendingSlot() == 2, "pendingSlot survives the answer");

        l_eA = l_oT.f_Activate();
        CHECK(l_eA == TitleAction::AskNewGameInSlot, "the prompt re-opens for the next attempt");
        l_oT.f_SetNewGameConfirmYesSelected(false);
        l_eA = l_oT.f_Activate();
        CHECK(l_eA == TitleAction::DismissNewGameConfirm, "No dismisses the prompt");
        CHECK(!l_oT.f_NewGameConfirmOpen(), "No closes the prompt");

        l_oT.f_Activate();                           // re-open, then leave with Esc
        l_eA = l_oT.f_Cancel();
        CHECK(l_eA == TitleAction::DismissNewGameConfirm, "Esc dismisses the prompt");
        CHECK(!l_oT.f_NewGameConfirmOpen(), "Esc closes the prompt");
        CHECK(l_oT.f_Page() == TitlePage::Continue, "Esc on the prompt does not leave the Continue page");

        l_oT.f_Activate();
        const bool l_bArmed = l_oT.f_NewGameConfirmYesSelected();
        l_oT.f_MoveHorizontal(1);
        CHECK(l_oT.f_NewGameConfirmYesSelected() != l_bArmed, "left/right toggles the armed answer");
        l_oT.f_MoveVertical(1);
        CHECK(l_oT.f_NewGameConfirmYesSelected() == l_bArmed, "up/down toggles it back");

        // Taps: only the two answers are live while the prompt is up.
        l_oT.f_SetNewGameConfirmYesSelected(true);
        l_eA = l_oT.f_AnswerConfirm(false);
        CHECK(l_eA == TitleAction::DismissNewGameConfirm, "tapping No dismisses the prompt");
        l_oT.f_Activate();                           // re-open
        l_eA = l_oT.f_AnswerConfirm(true);
        CHECK(l_eA == TitleAction::StartNewGameInSlot, "tapping Yes starts the new game");
        l_oT.f_Activate();                           // re-open
        l_eA = l_oT.f_ClickRow(-1);
        CHECK(l_eA == TitleAction::None, "taps outside the prompt are ignored while it is open");
        CHECK(l_oT.f_NewGameConfirmOpen(), "the prompt survives a stray tap");
        CHECK(l_oT.f_ClickRow(0) == TitleAction::None && l_oT.f_NewGameConfirmOpen(),
              "a tap on a row behind the prompt does not start a game");
    }

    // ------------------------------------------------- "switch to XXX language?" prompt
    // (a single tap/Enter used to apply a language instantly -- one wrong tap away from
    // stranding a touch/mobile player with no keyboard/Esc to undo it)
    {
        TitleScreen l_oT;
        l_oT.f_Open();
        l_oT.f_SetLanguageCount(3);
        l_oT.f_SetLanguageIndex(0);
        l_oT.f_SetPage(TitlePage::Settings);

        // Keyboard: move the highlight, then Enter opens the dialog (does not apply yet).
        l_oT.f_MoveVertical(1);
        CHECK(l_oT.f_SettingsSelection() == 1, "cursor moved to row 1");
        TitleAction l_eA = l_oT.f_Activate();
        CHECK(l_eA == TitleAction::AskLanguageChange, "Enter on a language row asks for confirmation");
        CHECK(l_oT.f_LanguageConfirmOpen(), "the confirm prompt is open");
        CHECK(l_oT.f_LanguageConfirmIndex() == 1, "the prompt names the row the player picked");
        CHECK(l_oT.f_LanguageConfirmYesSelected(), "Yes is armed by default");
        CHECK(l_oT.f_LanguageIndex() == 0, "the active language has not changed yet");

        l_eA = l_oT.f_Activate();
        CHECK(l_eA == TitleAction::SetLanguage, "Yes asks the caller to apply + persist the language");
        CHECK(!l_oT.f_LanguageConfirmOpen(), "answering closes the prompt");
        CHECK(l_oT.f_LanguageIndex() == 1, "the language index now follows the confirmed row");

        // No leaves the active language untouched.
        l_oT.f_MoveVertical(1);                      // -> row 2
        l_eA = l_oT.f_Activate();
        CHECK(l_eA == TitleAction::AskLanguageChange, "the prompt re-opens for the next row");
        l_oT.f_SetLanguageConfirmYesSelected(false);
        l_eA = l_oT.f_Activate();
        CHECK(l_eA == TitleAction::DismissLanguageConfirm, "No dismisses the prompt");
        CHECK(!l_oT.f_LanguageConfirmOpen(), "No closes the prompt");
        CHECK(l_oT.f_LanguageIndex() == 1, "declining leaves the previously-applied language alone");

        // Esc (no keyboard on web/touch, but still must work on desktop) also dismisses it.
        l_oT.f_Activate();                            // re-open (row 2, Yes armed again by default)
        l_eA = l_oT.f_Cancel();
        CHECK(l_eA == TitleAction::DismissLanguageConfirm, "Esc dismisses the prompt");
        CHECK(!l_oT.f_LanguageConfirmOpen(), "Esc closes the prompt");
        CHECK(l_oT.f_Page() == TitlePage::Settings, "Esc on the prompt does not leave the Settings page");
        CHECK(l_oT.f_LanguageIndex() == 1, "Esc does not apply the pending language");

        // Left/right and up/down both toggle the armed answer while the prompt is open.
        l_oT.f_Activate();
        const bool l_bArmed = l_oT.f_LanguageConfirmYesSelected();
        l_oT.f_MoveHorizontal(1);
        CHECK(l_oT.f_LanguageConfirmYesSelected() != l_bArmed, "left/right toggles the armed answer");
        l_oT.f_MoveVertical(1);
        CHECK(l_oT.f_LanguageConfirmYesSelected() == l_bArmed, "up/down toggles it back");

        // Taps: THIS is the mobile-critical path -- the dialog must expose a real, always-hit-
        // testable "No" button so a touch player (no keyboard, no Esc) is never stuck on it.
        l_oT.f_SetLanguageConfirmYesSelected(true);
        l_eA = l_oT.f_AnswerConfirm(false);
        CHECK(l_eA == TitleAction::DismissLanguageConfirm, "tapping No dismisses the prompt");
        CHECK(!l_oT.f_LanguageConfirmOpen(), "tapping No actually closed it (mobile is not stuck)");
        CHECK(l_oT.f_LanguageIndex() == 1, "tapping No did not change the language");

        l_oT.f_MoveVertical(1); l_oT.f_Activate();         // re-open on a different row
        l_eA = l_oT.f_AnswerConfirm(true);
        CHECK(l_eA == TitleAction::SetLanguage, "tapping Yes applies the language");
        CHECK(l_oT.f_LanguageIndex() == l_oT.f_SettingsSelection(), "the confirmed row is now active");

        // Settings itself also has a touch-reachable way out (title.rml's Back button).
        l_eA = l_oT.f_ClickRow(-1);
        CHECK(l_eA == TitleAction::Back && l_oT.f_Page() == TitlePage::Menu,
              "tapping Back on Settings returns to the Menu");
    }

    // ------------------------------------------------- art styles: list + Settings rows + prompt
    // (Settings > art style; the choice is persisted and applies after a restart)
    {
        // styles.json parsing: the original is always style 0; bad and duplicate ids are skipped.
        auto l_vStyles = f_ArtStylesFromJson(nlohmann::json::parse(R"({"styles":[
            {"id":"dark16","name":{"en":"Dark"}}, {"id":"dark16"}, {"id":"../evil"}, {"id":""}, {"id":"cute"}]})"));
        CHECK(l_vStyles.size() == 3, "original + 2 valid styles (dup / path / empty ids skipped)");
        CHECK(l_vStyles[0].m_strId.empty() && l_vStyles[1].m_strId == "dark16" && l_vStyles[2].m_strId == "cute", "style order is kept");
        CHECK(f_ArtStyleIndex(l_vStyles, "cute") == 2, "artStyleIndex finds a style");
        CHECK(f_ArtStyleIndex(l_vStyles, "gone") == 0, "an unknown (removed) style falls back to the original");
        CHECK(f_ArtStylesFromJson(nlohmann::json()).size() == 1, "no styles.json = only the original");

        // The shipped list, when present (the test runs in assets/): every listed style has its cooked atlas (the raw
        // PNGs are in art_raw/, not shipped).
        std::ifstream l_oShipped("media/styles/styles.json");
        if (l_oShipped) {
            auto l_vList = f_ArtStylesFromJson(nlohmann::json::parse(l_oShipped, nullptr, false));
            for (size_t l_nI = 1; l_nI < l_vList.size(); l_nI++)
                CHECK(std::filesystem::is_regular_file("media/styles/" + l_vList[l_nI].m_strId + "/atlas/game.atlas"),
                      "every style in media/styles/styles.json has its atlas");
        }

        // Settings persistence round trip.
        GameSettings l_oS;
        l_oS.m_strArtStyle = "dark16";
        CHECK(f_GameSettingsFromJson(f_ToJson(l_oS)).m_strArtStyle == "dark16", "artStyle survives settings.json");
        CHECK(f_GameSettingsFromJson(nlohmann::json::object()).m_strArtStyle.empty(), "a missing artStyle = original");

        TitleScreen l_oT;
        l_oT.f_SetLanguageCount(2);
        l_oT.f_SetStyleCount(1);
        CHECK(l_oT.f_SettingsRowCount() == 2, "only the original style: no style rows on Settings");
        l_oT.f_SetStyleCount(3);
        CHECK(l_oT.f_SettingsRowCount() == 5, "3 styles: 2 language rows + 3 style rows");
        l_oT.f_Open();
        l_oT.f_SetPage(TitlePage::Settings);

        // A style row (row 2+ = style index row-2) asks first, with no change yet.
        TitleAction l_eA = l_oT.f_ClickRow(3);
        CHECK(l_eA == TitleAction::AskStyleChange && l_oT.f_StyleConfirmOpen() && l_oT.f_StyleConfirmIndex() == 1,
              "tapping a style row opens the restart prompt for that style");
        CHECK(l_oT.f_StyleIndex() == 0 && !l_oT.f_LanguageConfirmOpen(), "nothing changes while the prompt is up");
        CHECK(l_oT.f_ClickRow(0) == TitleAction::None, "rows ignore taps while the prompt is up");
        l_eA = l_oT.f_AnswerConfirm(false);
        CHECK(l_eA == TitleAction::DismissStyleConfirm && l_oT.f_StyleIndex() == 0, "No keeps the current style");

        l_oT.f_ClickRow(4);
        l_eA = l_oT.f_Cancel();
        CHECK(l_eA == TitleAction::DismissStyleConfirm && !l_oT.f_StyleConfirmOpen() && l_oT.f_Page() == TitlePage::Settings,
              "Esc closes the prompt and stays on Settings");

        l_oT.f_ClickRow(4);
        l_eA = l_oT.f_AnswerConfirm(true);
        CHECK(l_eA == TitleAction::SetArtStyle && l_oT.f_StyleIndex() == 2, "Yes chooses the style (caller persists it)");

        // A language row still opens the language prompt, not the style one.
        l_eA = l_oT.f_ClickRow(1);
        CHECK(l_eA == TitleAction::AskLanguageChange && !l_oT.f_StyleConfirmOpen(), "language rows are unaffected");
        l_oT.f_Cancel();

        // Keyboard: the cursor walks languages then styles, and wraps.
        l_oT.f_SetPage(TitlePage::Settings);
        for (int l_iI = 0; l_iI < 4; l_iI++) l_oT.f_MoveVertical(1);
        CHECK(l_oT.f_SettingsSelection() == (l_oT.f_LanguageIndex() + 4) % 5, "the cursor reaches the style rows");
        l_oT.f_SetStyleIndex(7);
        CHECK(l_oT.f_StyleIndex() == 2, "an out-of-range style index is ignored");
    }

    if (g_iFail == 0) { printf("title_screen_test: ALL PASS\n"); return 0; }
    printf("title_screen_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}
