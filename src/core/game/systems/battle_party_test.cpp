// battle_party_test.cpp -- toms::nextStanding / allDown / battleEventLine (battle_party.h). Exits 0 on success.
#include "battle_party.h"

#include <cstdio>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

int main() {
    // 1. The target: stays while standing, moves to the next standing one, wraps, skips the fallen.
    CHECK(nextStanding({10, 5, 7}, 0, 0) == 0, "stay on a standing target");
    CHECK(nextStanding({0, 5, 7}, 0, 0) == 1, "a fallen target: the next standing one");
    CHECK(nextStanding({10, 5, 7}, 2, +1) == 0, "next wraps to the first");
    CHECK(nextStanding({10, 5, 7}, 0, -1) == 2, "previous wraps to the last");
    CHECK(nextStanding({10, 0, 7}, 0, +1) == 2, "next skips a fallen one");
    CHECK(nextStanding({0, 0, 7}, 2, +1) == 2, "the only one standing: itself");
    CHECK(nextStanding({0, 0, 0}, 1, 0) == -1 && nextStanding({}, 0, 1) == -1, "nobody standing: -1");
    CHECK(nextStanding({5}, 9, 0) == 0, "an index out of range starts at the first");
    // 2. All down.
    CHECK(allDown({0, -3, 0}) && !allDown({0, 1}) && allDown({}), "allDown");
    // 3. The log line.
    BattleEvent hit;
    hit.ms = 2400; hit.type = "hit"; hit.who = -1; hit.whom = 1; hit.value = 12;
    const std::string line = battleEventLine(hit, {"slime", "bat"});
    CHECK(line == "  2.40  player -> foe 1 (bat): hit 12", line.c_str());
    BattleEvent down;
    down.ms = 3000; down.type = "down"; down.who = 0;
    CHECK(battleEventLine(down, {"slime"}) == "  3.00  foe 0 (slime): down", battleEventLine(down, {"slime"}).c_str());
    if (g_fail) { std::printf("battle_party_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("battle_party_test: all passed\n");
    return 0;
}
