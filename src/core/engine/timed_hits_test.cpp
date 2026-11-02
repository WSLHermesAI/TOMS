// timed_hits_test.cpp -- toms::TimedTrack (timed_hits.h). Exits 0 on success.
#include "timed_hits.h"

#include <cmath>
#include <cstdio>

using namespace toms;
using G = TimedTrack::Grade;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

int main() {
    // 1. Grades by distance from the due time, both sides.
    {
        TimedTrack l_oT(40, 110);
        l_oT.f_Add(0, 500); l_oT.f_Add(0, 1000); l_oT.f_Add(0, 1500);
        l_oT.f_Update(480);
        auto l_oA = l_oT.f_Tap();
        CHECK(l_oA.m_eGrade == G::Perfect && l_oA.m_iIndex == 0 && l_oA.m_iOffsetMs == -20, "20 ms early is perfect");
        l_oT.f_Update(1080 - 480);
        auto l_oB = l_oT.f_Tap();
        CHECK(l_oB.m_eGrade == G::Good && l_oB.m_iIndex == 1 && l_oB.m_iOffsetMs == 80, "80 ms late is good");
        l_oT.f_Update(1700 - 1080);
        auto l_vMissed = l_oT.f_TakeMissed();
        CHECK(l_vMissed.size() == 1 && l_vMissed[0] == 2, "a point past its window becomes missed");
        CHECK(l_oT.f_Finished() && l_oT.f_Hits() == 2 && l_oT.f_Perfects() == 1, "the score");
    }
    // 2. An ordered track (sword, staff): a tap far from the next point counts against it.
    {
        TimedTrack l_oT(40, 110, /*ordered=*/true);
        l_oT.f_Add(0, 500); l_oT.f_Add(0, 600);
        l_oT.f_Update(200);
        auto l_oM = l_oT.f_Tap();
        CHECK(l_oM.m_eGrade == G::Miss && l_oM.m_iIndex == 0, "ordered: mashing early misses the next point");
        l_oT.f_Update(400);
        auto l_oH = l_oT.f_Tap();
        CHECK(l_oH.m_eGrade == G::Perfect && l_oH.m_iIndex == 1, "ordered: then the next point can still be hit");
    }
    // 3. A free track (unarmed): any open point, the nearest; a wasted tap costs nothing.
    {
        TimedTrack l_oT(40, 110, /*ordered=*/false);
        l_oT.f_Add(0, 500); l_oT.f_Add(0, 900);
        l_oT.f_Update(200);
        auto l_oW = l_oT.f_Tap();
        CHECK(l_oW.m_eGrade == G::Miss && l_oW.m_iIndex == -1, "free: a tap with nothing near wastes nothing");
        l_oT.f_Update(700);
        auto l_oH = l_oT.f_Tap();
        CHECK(l_oH.m_eGrade == G::Perfect && l_oH.m_iIndex == 1, "free: the second point first is fine");
        l_oT.f_Update(0);
        CHECK(l_oT.f_Points()[0].m_eResult == G::Miss, "the skipped point is missed by now (past its window)");
    }
    // 4. A point not yet appeared cannot be hit on a free track; growth runs 0 -> 1 from appear to due.
    {
        TimedTrack l_oT(40, 110, false);
        l_oT.f_Add(400, 800);
        l_oT.f_Update(300);
        CHECK(l_oT.f_Tap().m_iIndex == -1, "not appeared yet: no hit");
        CHECK(l_oT.f_Growth(0) == 0.0f, "growth 0 before it appears");
        l_oT.f_Update(300);   // now 600: halfway from 400 to 800
        CHECK(std::fabs(l_oT.f_Growth(0) - 0.5f) < 0.01f, "growth 0.5 halfway");
        l_oT.f_Update(200);
        CHECK(std::fabs(l_oT.f_Growth(0) - 1.0f) < 0.01f, "growth 1 at its due time");
    }
    // 5. Wider windows (higher DEX) turn a good into a perfect.
    {
        TimedTrack l_oNarrow(40, 110), l_oWide(90, 160);
        l_oNarrow.f_Add(0, 500); l_oWide.f_Add(0, 500);
        l_oNarrow.f_Update(570); l_oWide.f_Update(570);
        CHECK(l_oNarrow.f_Tap().m_eGrade == G::Good && l_oWide.f_Tap().m_eGrade == G::Perfect, "a wider window grades kinder");
    }

    if (g_iFail) { std::printf("timed_hits_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("timed_hits_test: all passed\n");
    return 0;
}
