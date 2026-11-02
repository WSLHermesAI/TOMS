// battle_party_test.cpp -- toms::nextStanding / allDown / battleEventLine (battle_party.h). Exits 0 on success.
#include "battle_party.h"

#include <cstdio>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

int main() {
    // 1. The target: stays while standing, moves to the next standing one, wraps, skips the fallen.
    CHECK(f_NextStanding({10, 5, 7}, 0, 0) == 0, "stay on a standing target");
    CHECK(f_NextStanding({0, 5, 7}, 0, 0) == 1, "a fallen target: the next standing one");
    CHECK(f_NextStanding({10, 5, 7}, 2, +1) == 0, "next wraps to the first");
    CHECK(f_NextStanding({10, 5, 7}, 0, -1) == 2, "previous wraps to the last");
    CHECK(f_NextStanding({10, 0, 7}, 0, +1) == 2, "next skips a fallen one");
    CHECK(f_NextStanding({0, 0, 7}, 2, +1) == 2, "the only one standing: itself");
    CHECK(f_NextStanding({0, 0, 0}, 1, 0) == -1 && f_NextStanding({}, 0, 1) == -1, "nobody standing: -1");
    CHECK(f_NextStanding({5}, 9, 0) == 0, "an index out of range starts at the first");
    // 2. All down.
    CHECK(f_AllDown({0, -3, 0}) && !f_AllDown({0, 1}) && f_AllDown({}), "allDown");
    // 3. The log line.
    BattleEvent l_oHit;
    l_oHit.m_iMs = 2400; l_oHit.m_strType = "hit"; l_oHit.m_iWho = -1; l_oHit.m_iWhom = 1; l_oHit.m_iValue = 12;
    const std::string l_strLine = f_BattleEventLine(l_oHit, {"slime", "bat"});
    CHECK(l_strLine == "  2.40  player -> foe 1 (bat): hit 12", l_strLine.c_str());
    BattleEvent l_oDown;
    l_oDown.m_iMs = 3000; l_oDown.m_strType = "down"; l_oDown.m_iWho = 0;
    CHECK(f_BattleEventLine(l_oDown, {"slime"}) == "  3.00  foe 0 (slime): down", f_BattleEventLine(l_oDown, {"slime"}).c_str());
    if (g_iFail) { std::printf("battle_party_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("battle_party_test: all passed\n");
    return 0;
}
