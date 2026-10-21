// card_collection_test.cpp -- monster cards (card_collection.h) and their place in the meta save. Exits 0 on success.
// Runs in assets/ (tests/CMakeLists.txt), so the shipped data is "data/...".
#include "card_collection.h"
#include "save_system.h"

#include <cstdio>
#include <fstream>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

int main() {
    // 1. The owner's rule (2026-10-20): the first card 100 %, another copy 50 % -- the defaults.
    {
        const CardChance c = cardChanceFromJson(nlohmann::json::parse(R"({"hp": 10})"));
        CHECK(c.first == 1.0f && c.repeat == 0.5f, "defaults: first 1.0, repeat 0.5");
        const CardChance d = cardChanceFromJson(nlohmann::json::parse(R"({"card": {"chance": 0.3, "repeatChance": 2}})"));
        CHECK(d.first == 0.3f && d.repeat == 1.0f, "a monster's own chances, clamped to 0..1");
    }
    // 2. Wins: counted every time; the first card always, the next by the repeat chance.
    {
        CardBook b;
        const CardChance c;   // 1.0 / 0.5
        CHECK(recordCardWin(b, "slime", c, 0.99f), "the first win always gives the card (roll 0.99 < 1.0)");
        CHECK(!recordCardWin(b, "slime", c, 0.50f), "a second copy needs a roll under 0.5 (0.50: no)");
        CHECK(recordCardWin(b, "slime", c, 0.49f), "a second copy with a roll under 0.5");
        CHECK(b["slime"].defeated == 3 && b["slime"].copies == 2, "3 wins, 2 copies");
        CardChance never; never.first = 0.0f;
        CHECK(!recordCardWin(b, "golem", never, 0.0f), "chance 0: never");
        CHECK(b["golem"].defeated == 1 && b["golem"].copies == 0, "still counted as defeated");
    }
    // 3. Losses.
    {
        CardBook b;
        recordCardLoss(b, "bat"); recordCardLoss(b, "bat");
        CHECK(b["bat"].defeatedBy == 2 && b["bat"].copies == 0 && b["bat"].defeated == 0, "losses counted on their own");
        recordCardLoss(b, "");
        CHECK(b.count("") == 0, "no record for an empty id");
    }
    // 4. JSON and the meta save: written and read back; an old save (no "cards") loads with none.
    {
        CardBook b;
        b["slime"] = {2, 5, 1};
        const CardBook back = cardBookFromJson(cardBookToJson(b));
        CHECK(back.size() == 1 && back.at("slime").copies == 2 && back.at("slime").defeated == 5 && back.at("slime").defeatedBy == 1,
              "the book round-trips");
        CHECK(cardBookFromJson(nlohmann::json::parse(R"({"x": {"copies": -3}, "y": 7})")).at("x").copies == 0,
              "negative numbers read as 0, a broken entry is skipped");
        MetaSaveData m;
        m.cards = b;
        const MetaSaveData m2 = metaFromJson(toJson(m));
        CHECK(m2.cards.size() == 1 && m2.cards.at("slime").copies == 2, "the meta save keeps the cards");
        const MetaSaveData old = metaFromJson(nlohmann::json::parse(R"({"schemaVersion": 3, "cycleIndex": 2})"));
        CHECK(old.cards.empty() && old.cycleIndex == 2, "an older save without cards loads");
    }
    // 5. The shipped enemies.json: every "card" setting is readable and in range.
    {
        std::ifstream f("data/enemies.json");
        const nlohmann::json e = nlohmann::json::parse(f, nullptr, false);
        CHECK(e.is_object() && !e.empty(), "data/enemies.json loads");
        if (e.is_object())
            for (auto it = e.begin(); it != e.end(); ++it) {
                if (!it.value().is_object() || !it.value().contains("card")) continue;
                const auto& c = it.value()["card"];
                const bool ok = c.is_object() && (!c.contains("chance") || (c["chance"].is_number() && c["chance"].get<double>() >= 0 && c["chance"].get<double>() <= 1))
                                && (!c.contains("repeatChance") || (c["repeatChance"].is_number() && c["repeatChance"].get<double>() >= 0 && c["repeatChance"].get<double>() <= 1));
                if (!ok) { std::printf("FAIL: enemies.json %s: \"card\" needs chance / repeatChance between 0 and 1\n", it.key().c_str()); g_fail++; }
            }
    }

    if (g_fail) { std::printf("card_collection_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("card_collection_test: all passed\n");
    return 0;
}
