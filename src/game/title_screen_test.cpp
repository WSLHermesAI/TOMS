// title_screen_test.cpp — headless verification of the title phase (New Game / Continue /
// Settings): the page state machine, save-slot I/O, persisted settings, and the localization
// table. No renderer/Vulkan/GLFW involvement — this is pure game logic plus file I/O, so it
// runs anywhere. Run: ./title_screen_test  (exit 0 = all checks pass)
#include "title_screen.h"
#include "game_settings.h"
#include "localization.h"
#include <cstdio>
#include <filesystem>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

int main() {
    // ---------------------------------------------------------------- localization
    {
        Locale loc;
        CHECK(loc.languageCount() == 2, "Locale starts with 2 built-in languages");
        CHECK(loc.languageCode() == "zh_TW", "Locale defaults to zh_TW");
        CHECK(loc.tr("menu.new_game") == "新遊戲", "built-in fallback: menu.new_game in zh_TW");

        // Load the real shipped table when it is reachable from the test's CWD.
        const char* candidates[] = { "data/text.json", "../data/text.json", "../../data/text.json" };
        bool loaded = false;
        for (const char* p : candidates) if (loc.loadFromFile(p)) { loaded = true; break; }
        CHECK(loaded, "data/text.json loads from one of the candidate paths");
        if (loaded) {
            CHECK(loc.stringCount() > 20, "loaded table defines the expected number of keys");
            CHECK(loc.has("settings.language"), "loaded table has settings.language");
            CHECK(loc.setLanguage("en"), "setLanguage(\"en\") succeeds");
            CHECK(loc.tr("menu.new_game") == "New Game", "menu.new_game switches to English");
            CHECK(loc.languageIndex() == 1, "language index tracks the selection");
            CHECK(loc.tr("no.such.key") == "no.such.key", "unknown key returns the key itself");
        }
        CHECK(!loc.setLanguage("fr"), "an unknown language code is rejected");
    }

    // ---------------------------------------------------------------- settings persistence
    {
        GameSettings s;
        s.language = "en";
        s.slotCount = 4;
        s.uiFontScale = 1.75f;
        auto j = toJson(s);
        GameSettings s2 = gameSettingsFromJson(j);
        CHECK(s2.language == "en" && s2.slotCount == 4, "settings JSON round-trip");

        // Out-of-range values are clamped, not accepted blindly.
        nlohmann::json bad;
        bad["slotCount"] = 99;
        bad["uiFontScale"] = 100.0f;
        GameSettings s3 = gameSettingsFromJson(bad);
        CHECK(s3.slotCount <= 8, "slotCount is clamped to the layout maximum");
        CHECK(s3.uiFontScale <= 4.0f, "uiFontScale is clamped to a sane range");

        // File round-trip.
        std::string path = "title_test_settings.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        bool found = true;
        GameSettings missing = loadGameSettings(path, &found);
        CHECK(!found, "a missing settings file reports found=false");
        CHECK(missing.language == "zh_TW", "a missing settings file falls back to defaults");

        CHECK(saveGameSettings(path, s), "saveGameSettings writes the file");
        bool found2 = false;
        GameSettings s4 = loadGameSettings(path, &found2);
        CHECK(found2, "saved settings file is found on reload");
        CHECK(s4.language == "en" && s4.slotCount == 4, "settings survive a file round-trip");
        std::filesystem::remove(path, ec);
    }

    // ---------------------------------------------------------------- save slots
    {
        std::string dir = "title_test_saves";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        CHECK(ensureSaveDir(dir), "ensureSaveDir creates the slot directory");
        CHECK(firstEmptySlot(dir, 3) == 1, "with no files, the first empty slot is 1");
        CHECK(newestSlot(dir, 3) == -1, "with no files, there is no newest slot");
        CHECK(!slotExists(dir, 2), "slot 2 reports as absent before it is written");

        MetaSaveData meta;
        meta.currentBeat = 3;
        meta.storyFlags = {"beat_02_tutorial_done"};
        meta.unlockedStages = {"stage_01", "stage_02"};
        RunSaveData run;
        run.currentStageId = "stage_02";
        run.player.hp = 77; run.player.maxhp = 130; run.player.gold = 55; run.player.lv = 3;
        run.player.inv = {"potion_red", "gem_atk"};
        run.entityStatus["stage_02|4,3"] = "Defeated";

        CHECK(writeSlotSave(dir, 1, meta, run, 3725, "2026-09-10 12:00:00", "廢墟迷宮"),
              "writeSlotSave writes slot 1");
        CHECK(slotExists(dir, 1), "slot 1 exists after writing");
        CHECK(firstEmptySlot(dir, 3) == 2, "the next empty slot advances to 2");

        SlotSummary sum = summarizeSlot(dir, 1);
        CHECK(sum.exists, "summarizeSlot reports the slot as occupied");
        CHECK(sum.lv == 3 && sum.gold == 55 && sum.hp == 77,
              "summary carries the player's level/gold/hp for the Continue row");
        CHECK(sum.stageId == "stage_02", "summary carries the stage id");
        CHECK(sum.stageName == "廢墟迷宮", "summary carries the stage display name");
        CHECK(sum.playTimeSec == 3725, "summary carries play time");
        CHECK(sum.savedAt == "2026-09-10 12:00:00", "summary carries the saved-at stamp");
        CHECK(summarizeSlot(dir, 2).exists == false, "an unwritten slot summarizes as empty");

        MetaSaveData meta2; RunSaveData run2; int playSec = 0; std::string stamp;
        CHECK(readSlotSave(dir, 1, meta2, run2, &playSec, &stamp), "readSlotSave succeeds");
        CHECK(meta2.unlockedStages.size() == 2 && meta2.currentBeat == 3,
              "meta progress (story flags/stages) survives the slot round-trip");
        CHECK(run2.currentStageId == "stage_02" && run2.player.hp == 77,
              "run state survives the slot round-trip");
        CHECK(run2.entityStatus.at("stage_02|4,3") == "Defeated",
              "per-tile entity status (cleared monsters) survives the slot round-trip");
        CHECK(playSec == 3725, "play time survives the slot round-trip");

        CHECK(newestSlot(dir, 3) == 1, "newestSlot returns the only written slot");
        writeSlotSave(dir, 2, meta, run, 10, "2026-09-10 13:00:00", "廢墟迷宮");
        CHECK(newestSlot(dir, 3) == 2, "newestSlot follows the newest saved-at stamp");
        CHECK(firstEmptySlot(dir, 2) == -1, "firstEmptySlot returns -1 when every slot is taken");

        CHECK(deleteSlot(dir, 2), "deleteSlot removes a slot");
        CHECK(!slotExists(dir, 2), "a deleted slot reads as absent");
        std::filesystem::remove_all(dir, ec);
    }

    // ---------------------------------------------------------------- title state machine
    {
        TitleScreen t;
        t.open();
        CHECK(t.isOpen(), "open() shows the title");
        CHECK(t.page() == TitlePage::Menu, "the title opens on the Menu page");
        CHECK(t.menuSelection() == 0, "the Menu starts on New Game");

        // Menu navigation wraps (3 items: New Game / Continue / Settings).
        t.moveVertical(1);
        CHECK(t.menuSelection() == 1, "down moves to Continue");
        t.moveVertical(1);
        t.moveVertical(1);
        CHECK(t.menuSelection() == 0, "menu selection wraps past the last item");

        // Continue page: parks the cursor on the first slot that actually has a save.
        std::vector<SlotSummary> sums(3);
        for (int i = 0; i < 3; i++) { sums[i].slot = i + 1; sums[i].exists = false; }
        sums[1].exists = true;      // slot 2 exists
        t.setSummaries(sums);
        t.setSlotCount(3);
        TitleAction a = t.activate();          // menuSel == 0 -> New Game
        CHECK(a == TitleAction::NewGame, "activating the first Menu row asks for New Game");

        t.setPage(TitlePage::Continue);
        CHECK(t.page() == TitlePage::Continue, "setPage switches to Continue");
        CHECK(t.slotSelection() == 1, "Continue parks on the first slot that has a save (slot 2)");
        CHECK(t.selectedSlotNumber() == 2, "selectedSlotNumber is 1-based");
        CHECK(t.selectedSlotExists(), "the parked slot is reported as playable");
        a = t.activate();
        CHECK(a == TitleAction::LoadSlot, "activating a Continue row asks to load that slot");
        CHECK(t.pendingSlot() == 2, "pendingSlot carries the highlighted slot number");

        // Vertical navigation clamps inside the list (no wrap).
        t.moveVertical(1);
        CHECK(t.slotSelection() == 2, "down moves one slot row");
        t.moveVertical(1);
        CHECK(t.slotSelection() == 2, "slot selection clamps at the last slot");
        t.moveVertical(-5);
        CHECK(t.slotSelection() == 0, "slot selection clamps at the first slot");

        // Nothing to load: the Game is expected to keep the title open (empty slot).
        t.setSummaries(std::vector<SlotSummary>(3));
        CHECK(t.firstPlayableSlot() == -1, "with no saves there is no playable slot");
        CHECK(!t.selectedSlotExists(), "an empty slot is not reported as playable");

        // Settings page: language rows. Moving only changes the highlight -- activate()/tap is
        // needed to open the "switch to XXX?" dialog, and only confirming it actually applies
        // the language (see the dedicated confirm-dialog test block below for the full flow).
        t.setPage(TitlePage::Settings);
        CHECK(t.page() == TitlePage::Settings, "setPage switches to Settings");
        t.setLanguageCount(2);
        t.setLanguageIndex(0);
        a = t.moveVertical(1);
        CHECK(a == TitleAction::None, "moving the highlight alone does not apply the language");
        CHECK(t.settingsSelection() == 1, "the highlighted row follows the move");
        CHECK(t.languageIndex() == 0, "the active language is unchanged until confirmed");
        a = t.moveHorizontal(1);
        CHECK(a == TitleAction::None, "left/right also just moves the highlight");
        CHECK(t.settingsSelection() == 0, "the highlight wraps with left/right");

        // Esc goes Back to the Menu from a sub-page.
        a = t.cancel();
        CHECK(a == TitleAction::Back && t.page() == TitlePage::Menu, "cancel returns to the Menu");
        a = t.cancel();
        CHECK(a == TitleAction::None, "cancel on the Menu itself is left to the caller");

        // Click hit-testing agrees with the layout the draw pass uses.
        TitleLayout L = computeTitleLayout(1024, 768, 3, 2);
        CHECK(L.menuRowCount == 3, "the layout has three menu rows");
        CHECK(L.hitMenuRow(512, L.menuRow[1].y + 5) == 1, "a tap in the middle row hits row 1");
        CHECK(L.hitMenuRow(512, 5) == -1, "a tap in the title area hits no menu row");
        a = t.click(512, L.menuRow[1].y + 5, L);
        CHECK(a == TitleAction::OpenContinue, "tapping Continue opens the Continue page");
        CHECK(t.page() == TitlePage::Continue, "the tap actually changed the page");
        a = t.click(L.backButton.x + 5, L.backButton.y + 5, L);
        CHECK(a == TitleAction::Back, "tapping Back returns to the Menu");

        // Settings tap opens the "switch to XXX?" dialog rather than applying instantly.
        t.setPage(TitlePage::Settings);
        t.setLanguageCount(2);
        a = t.click(L.langRow[1].x + 5, L.langRow[1].y + 5, L);
        CHECK(a == TitleAction::AskLanguageChange && t.languageConfirmOpen(),
              "tapping a language row opens the confirm dialog instead of applying it");
        CHECK(t.languageIndex() == 0, "the active language has not changed yet");
        CHECK(t.languageConfirmIndex() == 1, "the dialog names the tapped row");
        t.cancel();   // leave it closed for the next block

        // Slot count is clamped to what the layout can show (8 rows).
        t.setSlotCount(99);
        CHECK(t.slotCount() == TitleLayout::kMaxSlotRows, "slotCount clamps to the layout max");
    }

    // ------------------------------------------------- empty-slot "start a new game?" prompt
    // (owner's report: activating an empty Continue slot used to do nothing at all)
    {
        TitleScreen t;
        t.open();
        t.setSlotCount(3);
        std::vector<SlotSummary> sums(3);
        for (int i = 0; i < 3; i++) { sums[i].slot = i + 1; sums[i].exists = false; }
        sums[0].exists = true;                  // slot 1 filled, slots 2-3 empty
        t.setSummaries(sums);
        t.setPage(TitlePage::Continue);
        CHECK(t.slotSelection() == 0, "Continue parks on the filled slot 1");

        TitleAction a = t.activate();
        CHECK(a == TitleAction::LoadSlot, "a FILLED slot still loads directly");
        CHECK(!t.newGameConfirmOpen(), "no prompt for a filled slot");

        t.moveVertical(1);                      // -> slot 2 (empty)
        CHECK(t.selectedSlotNumber() == 2, "cursor moved to empty slot 2");
        a = t.activate();
        CHECK(a == TitleAction::AskNewGameInSlot, "activating an EMPTY slot asks for confirmation");
        CHECK(t.newGameConfirmOpen(), "the confirm prompt is open");
        CHECK(t.newGameConfirmSlot() == 2, "the prompt names the slot the player picked");
        CHECK(t.newGameConfirmYesSelected(), "Yes is armed by default");
        CHECK(t.pendingSlot() == 2, "pendingSlot points at the chosen slot");

        a = t.activate();
        CHECK(a == TitleAction::StartNewGameInSlot, "Yes asks to start a new game in that slot");
        CHECK(!t.newGameConfirmOpen(), "answering closes the prompt");
        CHECK(t.pendingSlot() == 2, "pendingSlot survives the answer");

        a = t.activate();
        CHECK(a == TitleAction::AskNewGameInSlot, "the prompt re-opens for the next attempt");
        t.setNewGameConfirmYesSelected(false);
        a = t.activate();
        CHECK(a == TitleAction::DismissNewGameConfirm, "No dismisses the prompt");
        CHECK(!t.newGameConfirmOpen(), "No closes the prompt");

        t.activate();                           // re-open, then leave with Esc
        a = t.cancel();
        CHECK(a == TitleAction::DismissNewGameConfirm, "Esc dismisses the prompt");
        CHECK(!t.newGameConfirmOpen(), "Esc closes the prompt");
        CHECK(t.page() == TitlePage::Continue, "Esc on the prompt does not leave the Continue page");

        t.activate();
        const bool armed = t.newGameConfirmYesSelected();
        t.moveHorizontal(1);
        CHECK(t.newGameConfirmYesSelected() != armed, "left/right toggles the armed answer");
        t.moveVertical(1);
        CHECK(t.newGameConfirmYesSelected() == armed, "up/down toggles it back");

        // Taps: only the two answers are live while the prompt is up.
        TitleLayout L = computeTitleLayout(1024, 768, 3, 2);
        t.setNewGameConfirmYesSelected(true);
        a = t.click(L.confirmNo.x + 5, L.confirmNo.y + 5, L);
        CHECK(a == TitleAction::DismissNewGameConfirm, "tapping No dismisses the prompt");
        t.activate();                           // re-open
        a = t.click(L.confirmYes.x + 5, L.confirmYes.y + 5, L);
        CHECK(a == TitleAction::StartNewGameInSlot, "tapping Yes starts the new game");
        t.activate();                           // re-open
        a = t.click(L.backButton.x + 5, L.backButton.y + 5, L);
        CHECK(a == TitleAction::None, "taps outside the prompt are ignored while it is open");
        CHECK(t.newGameConfirmOpen(), "the prompt survives a stray tap");
        CHECK(t.click(500, 400, L) == TitleAction::None || !t.newGameConfirmOpen(),
              "a tap in the scrim does not start a game");
    }

    // ------------------------------------------------- "switch to XXX language?" prompt
    // (a single tap/Enter used to apply a language instantly -- one wrong tap away from
    // stranding a touch/mobile player with no keyboard/Esc to undo it)
    {
        TitleScreen t;
        t.open();
        t.setLanguageCount(3);
        t.setLanguageIndex(0);
        t.setPage(TitlePage::Settings);

        // Keyboard: move the highlight, then Enter opens the dialog (does not apply yet).
        t.moveVertical(1);
        CHECK(t.settingsSelection() == 1, "cursor moved to row 1");
        TitleAction a = t.activate();
        CHECK(a == TitleAction::AskLanguageChange, "Enter on a language row asks for confirmation");
        CHECK(t.languageConfirmOpen(), "the confirm prompt is open");
        CHECK(t.languageConfirmIndex() == 1, "the prompt names the row the player picked");
        CHECK(t.languageConfirmYesSelected(), "Yes is armed by default");
        CHECK(t.languageIndex() == 0, "the active language has not changed yet");

        a = t.activate();
        CHECK(a == TitleAction::SetLanguage, "Yes asks the caller to apply + persist the language");
        CHECK(!t.languageConfirmOpen(), "answering closes the prompt");
        CHECK(t.languageIndex() == 1, "the language index now follows the confirmed row");

        // No leaves the active language untouched.
        t.moveVertical(1);                      // -> row 2
        a = t.activate();
        CHECK(a == TitleAction::AskLanguageChange, "the prompt re-opens for the next row");
        t.setLanguageConfirmYesSelected(false);
        a = t.activate();
        CHECK(a == TitleAction::DismissLanguageConfirm, "No dismisses the prompt");
        CHECK(!t.languageConfirmOpen(), "No closes the prompt");
        CHECK(t.languageIndex() == 1, "declining leaves the previously-applied language alone");

        // Esc (no keyboard on web/touch, but still must work on desktop) also dismisses it.
        t.activate();                            // re-open (row 2, Yes armed again by default)
        a = t.cancel();
        CHECK(a == TitleAction::DismissLanguageConfirm, "Esc dismisses the prompt");
        CHECK(!t.languageConfirmOpen(), "Esc closes the prompt");
        CHECK(t.page() == TitlePage::Settings, "Esc on the prompt does not leave the Settings page");
        CHECK(t.languageIndex() == 1, "Esc does not apply the pending language");

        // Left/right and up/down both toggle the armed answer while the prompt is open.
        t.activate();
        const bool armed = t.languageConfirmYesSelected();
        t.moveHorizontal(1);
        CHECK(t.languageConfirmYesSelected() != armed, "left/right toggles the armed answer");
        t.moveVertical(1);
        CHECK(t.languageConfirmYesSelected() == armed, "up/down toggles it back");

        // Taps: THIS is the mobile-critical path -- the dialog must expose a real, always-hit-
        // testable "No" button so a touch player (no keyboard, no Esc) is never stuck on it.
        TitleLayout L = computeTitleLayout(1024, 768, 3, 3);
        t.setLanguageConfirmYesSelected(true);
        a = t.click(L.confirmNo.x + 5, L.confirmNo.y + 5, L);
        CHECK(a == TitleAction::DismissLanguageConfirm, "tapping No dismisses the prompt");
        CHECK(!t.languageConfirmOpen(), "tapping No actually closed it (mobile is not stuck)");
        CHECK(t.languageIndex() == 1, "tapping No did not change the language");

        t.moveVertical(1); t.activate();         // re-open on a different row
        a = t.click(L.confirmYes.x + 5, L.confirmYes.y + 5, L);
        CHECK(a == TitleAction::SetLanguage, "tapping Yes applies the language");
        CHECK(t.languageIndex() == t.settingsSelection(), "the confirmed row is now active");

        // Settings itself also has a touch-reachable way out (a Back button is now drawn there
        // by Game::draw(), matching Continue's -- this just proves the hit-test still agrees).
        a = t.click(L.backButton.x + 5, L.backButton.y + 5, L);
        CHECK(a == TitleAction::Back && t.page() == TitlePage::Menu,
              "tapping Back on Settings returns to the Menu");
    }

    if (g_fail == 0) { printf("title_screen_test: ALL PASS\n"); return 0; }
    printf("title_screen_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
