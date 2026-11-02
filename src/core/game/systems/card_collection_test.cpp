// card_collection_test.cpp -- monster cards (card_collection.h) and their place in the meta save. Exits 0 on success.
// Runs in assets/ (tests/CMakeLists.txt), so the shipped data is "data/...".
#include "card_collection.h"
#include "save_system.h"

#include <cstdio>
#include <fstream>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

int main() {
    // 1. The owner's rule (2026-10-20): the first card 100 %, another copy 50 % -- the defaults.
    {
        const CardChance l_oC = f_CardChanceFromJson(nlohmann::json::parse(R"({"hp": 10})"));
        CHECK(l_oC.m_fFirst == 1.0f && l_oC.m_fRepeat == 0.5f, "defaults: first 1.0, repeat 0.5");
        const CardChance l_oD = f_CardChanceFromJson(nlohmann::json::parse(R"({"card": {"chance": 0.3, "repeatChance": 2}})"));
        CHECK(l_oD.m_fFirst == 0.3f && l_oD.m_fRepeat == 1.0f, "a monster's own chances, clamped to 0..1");
    }
    // 2. Wins: counted every time; the first card always, the next by the repeat chance.
    {
        CardBook l_mapB;
        const CardChance l_oC;   // 1.0 / 0.5
        CHECK(f_RecordCardWin(l_mapB, "slime", l_oC, 0.99f), "the first win always gives the card (roll 0.99 < 1.0)");
        CHECK(!f_RecordCardWin(l_mapB, "slime", l_oC, 0.50f), "a second copy needs a roll under 0.5 (0.50: no)");
        CHECK(f_RecordCardWin(l_mapB, "slime", l_oC, 0.49f), "a second copy with a roll under 0.5");
        CHECK(l_mapB["slime"].m_iDefeated == 3 && l_mapB["slime"].m_iCopies == 2, "3 wins, 2 copies");
        CardChance l_oNever; l_oNever.m_fFirst = 0.0f;
        CHECK(!f_RecordCardWin(l_mapB, "golem", l_oNever, 0.0f), "chance 0: never");
        CHECK(l_mapB["golem"].m_iDefeated == 1 && l_mapB["golem"].m_iCopies == 0, "still counted as defeated");
    }
    // 3. Losses.
    {
        CardBook l_mapB;
        f_RecordCardLoss(l_mapB, "bat"); f_RecordCardLoss(l_mapB, "bat");
        CHECK(l_mapB["bat"].m_iDefeatedBy == 2 && l_mapB["bat"].m_iCopies == 0 && l_mapB["bat"].m_iDefeated == 0, "losses counted on their own");
        f_RecordCardLoss(l_mapB, "");
        CHECK(l_mapB.count("") == 0, "no record for an empty id");
    }
    // 4. JSON and the meta save: written and read back; an old save (no "cards") loads with none.
    {
        CardBook l_mapB;
        l_mapB["slime"] = {2, 5, 1};
        const CardBook l_mapBack = f_CardBookFromJson(f_CardBookToJson(l_mapB));
        CHECK(l_mapBack.size() == 1 && l_mapBack.at("slime").m_iCopies == 2 && l_mapBack.at("slime").m_iDefeated == 5 && l_mapBack.at("slime").m_iDefeatedBy == 1,
              "the book round-trips");
        CHECK(f_CardBookFromJson(nlohmann::json::parse(R"({"x": {"copies": -3}, "y": 7})")).at("x").m_iCopies == 0,
              "negative numbers read as 0, a broken entry is skipped");
        MetaSaveData l_oM;
        l_oM.m_mapCards = l_mapB;
        const MetaSaveData l_oM2 = f_MetaFromJson(f_ToJson(l_oM));
        CHECK(l_oM2.m_mapCards.size() == 1 && l_oM2.m_mapCards.at("slime").m_iCopies == 2, "the meta save keeps the cards");
        const MetaSaveData l_oOld = f_MetaFromJson(nlohmann::json::parse(R"({"schemaVersion": 3, "cycleIndex": 2})"));
        CHECK(l_oOld.m_mapCards.empty() && l_oOld.m_iCycleIndex == 2, "an older save without cards loads");
    }
    // 5. The shipped enemies.json: every "card" setting is readable and in range.
    {
        std::ifstream l_oF("data/enemies.json");
        const nlohmann::json l_jE = nlohmann::json::parse(l_oF, nullptr, false);
        CHECK(l_jE.is_object() && !l_jE.empty(), "data/enemies.json loads");
        if (l_jE.is_object())
            for (auto l_oIt = l_jE.begin(); l_oIt != l_jE.end(); ++l_oIt) {
                if (!l_oIt.value().is_object() || !l_oIt.value().contains("card")) continue;
                const auto& l_jC = l_oIt.value()["card"];
                const bool l_bOk = l_jC.is_object() && (!l_jC.contains("chance") || (l_jC["chance"].is_number() && l_jC["chance"].get<double>() >= 0 && l_jC["chance"].get<double>() <= 1))
                                && (!l_jC.contains("repeatChance") || (l_jC["repeatChance"].is_number() && l_jC["repeatChance"].get<double>() >= 0 && l_jC["repeatChance"].get<double>() <= 1));
                if (!l_bOk) { std::printf("FAIL: enemies.json %s: \"card\" needs chance / repeatChance between 0 and 1\n", l_oIt.key().c_str()); g_iFail++; }
            }
    }

    if (g_iFail) { std::printf("card_collection_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("card_collection_test: all passed\n");
    return 0;
}
