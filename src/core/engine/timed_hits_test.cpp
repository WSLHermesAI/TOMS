// timed_hits_test.cpp -- toms::TimedTrack (timed_hits.h). Exits 0 on success.
#include "timed_hits.h"

#include <cmath>
#include <cstdio>

using namespace toms;
using G = TimedTrack::Grade;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

int main() {
    // 1. Grades by distance from the due time, both sides.
    {
        TimedTrack t(40, 110);
        t.add(0, 500); t.add(0, 1000); t.add(0, 1500);
        t.update(480);
        auto a = t.tap();
        CHECK(a.grade == G::Perfect && a.index == 0 && a.offsetMs == -20, "20 ms early is perfect");
        t.update(1080 - 480);
        auto b = t.tap();
        CHECK(b.grade == G::Good && b.index == 1 && b.offsetMs == 80, "80 ms late is good");
        t.update(1700 - 1080);
        auto missed = t.takeMissed();
        CHECK(missed.size() == 1 && missed[0] == 2, "a point past its window becomes missed");
        CHECK(t.finished() && t.hits() == 2 && t.perfects() == 1, "the score");
    }
    // 2. An ordered track (sword, staff): a tap far from the next point counts against it.
    {
        TimedTrack t(40, 110, /*ordered=*/true);
        t.add(0, 500); t.add(0, 600);
        t.update(200);
        auto m = t.tap();
        CHECK(m.grade == G::Miss && m.index == 0, "ordered: mashing early misses the next point");
        t.update(400);
        auto h = t.tap();
        CHECK(h.grade == G::Perfect && h.index == 1, "ordered: then the next point can still be hit");
    }
    // 3. A free track (unarmed): any open point, the nearest; a wasted tap costs nothing.
    {
        TimedTrack t(40, 110, /*ordered=*/false);
        t.add(0, 500); t.add(0, 900);
        t.update(200);
        auto w = t.tap();
        CHECK(w.grade == G::Miss && w.index == -1, "free: a tap with nothing near wastes nothing");
        t.update(700);
        auto h = t.tap();
        CHECK(h.grade == G::Perfect && h.index == 1, "free: the second point first is fine");
        t.update(0);
        CHECK(t.points()[0].result == G::Miss, "the skipped point is missed by now (past its window)");
    }
    // 4. A point not yet appeared cannot be hit on a free track; growth runs 0 -> 1 from appear to due.
    {
        TimedTrack t(40, 110, false);
        t.add(400, 800);
        t.update(300);
        CHECK(t.tap().index == -1, "not appeared yet: no hit");
        CHECK(t.growth(0) == 0.0f, "growth 0 before it appears");
        t.update(300);   // now 600: halfway from 400 to 800
        CHECK(std::fabs(t.growth(0) - 0.5f) < 0.01f, "growth 0.5 halfway");
        t.update(200);
        CHECK(std::fabs(t.growth(0) - 1.0f) < 0.01f, "growth 1 at its due time");
    }
    // 5. Wider windows (higher DEX) turn a good into a perfect.
    {
        TimedTrack narrow(40, 110), wide(90, 160);
        narrow.add(0, 500); wide.add(0, 500);
        narrow.update(570); wide.update(570);
        CHECK(narrow.tap().grade == G::Good && wide.tap().grade == G::Perfect, "a wider window grades kinder");
    }

    if (g_fail) { std::printf("timed_hits_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("timed_hits_test: all passed\n");
    return 0;
}
