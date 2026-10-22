// mini_game_test.cpp -- reusable reward events (mini_game.h) and the shipped data/minigames.json. Exits 0 on success.
// Runs in assets/ (tests/CMakeLists.txt), so the data file is "data/minigames.json".
#include "mini_game.h"

#include <cstdio>

using namespace toms;

namespace {
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)
}  // namespace

int main() {
    // 1. Parsing: the play's settings, the currency (only gold or crystals), no charge and nothing lost on a miss.
    {
        const MiniGameDef g = miniGameFromJson("x", nlohmann::json::parse(R"({"currency": "crystals", "perHit": 2, "bonusAll": 5,
            "points": 9, "sweepMs": 1200, "limit": 1, "title": {"zh_TW": "測試"}})"));
        CHECK(g.currency == "crystals" && g.perHit == 2 && g.bonusAll == 5 && g.limit == 1, "fields read");
        CHECK(g.play.points == kMaxComboPoints && g.play.sweepMs == 1200, "points capped at five; the sweep");
        CHECK(g.play.speedUp == 0 && g.play.missBack == 0, "no charge; a miss keeps the hits");
        CHECK(miniGameFromJson("y", nlohmann::json::parse(R"({"currency": "diamonds"})")).currency == "gold", "unknown currency: gold");
    }
    // 2. The reward: per hit, the bonus for all, nothing for a fail.
    {
        MiniGameDef g;
        g.perHit = 10;
        g.bonusAll = 25;
        ComboResult r;
        r.points = 4;
        r.hits = 3;
        CHECK(miniGameReward(g, r) == 30, "3 of 4: 3 x 10");
        r.hits = 4;
        CHECK(miniGameReward(g, r) == 65, "all 4: 40 + 25");
        r.failed = true;
        CHECK(miniGameReward(g, r) == 0, "a fail: nothing");
    }
    // 3. A whole play through ComboPlay: hit every point.
    {
        MiniGameDef g = miniGameFromJson("z", nlohmann::json::parse(R"({"points": 2, "sweepMs": 1000, "perHit": 1, "bonusAll": 1})"));
        ComboPlay c;
        c.start(g.play, 0.0f);
        for (float p : c.pointsAt()) {
            while (c.running() && c.marker() < p - 0.005f) c.update(1);
            c.tap();
        }
        CHECK(c.done() && miniGameReward(g, c.result()) == 3, "both hit: 2 + 1");
    }
    // 4. The shipped events: the gold chest and the crystal shrine (its text Traditional Chinese only, the owner's test).
    {
        const MiniGameTable t = loadMiniGames("data/minigames.json");
        const MiniGameDef* chest = t.find("chest_lockpick");
        const MiniGameDef* shrine = t.find("crystal_shrine");
        CHECK(chest && chest->currency == "gold" && chest->perHit > 0, "chest_lockpick: gold");
        CHECK(shrine && shrine->currency == "crystals" && shrine->perHit > 0, "crystal_shrine: crystals");
        CHECK(shrine && shrine->text.contains("zh_TW") && !shrine->text.contains("en"), "crystal_shrine: zh_TW text only");
    }
    if (g_fail) { std::printf("mini_game_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("mini_game_test: OK\n");
    return 0;
}
