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

        // Settings page: language rows; moving returns SetLanguage so the caller persists it.
        t.setPage(TitlePage::Settings);
        CHECK(t.page() == TitlePage::Settings, "setPage switches to Settings");
        t.setLanguageCount(2);
        t.setLanguageIndex(0);
        a = t.moveVertical(1);
        CHECK(a == TitleAction::SetLanguage, "changing the language row asks to apply it");
        CHECK(t.languageIndex() == 1, "the language index follows the selection");
        a = t.moveHorizontal(1);
        CHECK(a == TitleAction::SetLanguage, "left/right also switches the language");
        CHECK(t.languageIndex() == 0, "language selection wraps with left/right");

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

        // Settings tap selects a language row.
        t.setPage(TitlePage::Settings);
        t.setLanguageCount(2);
        a = t.click(L.langRow[1].x + 5, L.langRow[1].y + 5, L);
        CHECK(a == TitleAction::SetLanguage && t.languageIndex() == 1,
              "tapping a language row selects it");

        // Slot count is clamped to what the layout can show (8 rows).
        t.setSlotCount(99);
        CHECK(t.slotCount() == TitleLayout::kMaxSlotRows, "slotCount clamps to the layout max");
    }

    if (g_fail == 0) { printf("title_screen_test: ALL PASS\n"); return 0; }
    printf("title_screen_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
