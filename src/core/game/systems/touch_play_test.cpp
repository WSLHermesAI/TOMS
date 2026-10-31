// touch_play_test.cpp -- the weapons' own plays (touch_play.h): bow, slice, circles. Exits 0 on success.
#include "touch_play.h"

#include <cmath>
#include <cstdio>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

int main() {
    const glm::vec4 area(500, 100, 300, 200);
    // 1. Bow: drag back from below the target, let go: the crosshair goes the other way.
    {
        TouchPlay p;
        p.start(PlayKind::Bow, 2, false, area, 7);
        CHECK(p.running() && !p.started() && p.timeLeft() == 1.0f, "the bow waits for the first touch");
        p.update(5000);
        CHECK(p.index() == 0, "no time runs before the first touch");
        const auto t = p.target();
        CHECK(t.x >= area.x && t.x <= area.x + area.z && t.y >= area.y && t.y <= area.y + area.w, "the target is in the area");
        // the crosshair = from - (to - from) * 1.8: pull 50 px straight down from 90 px under the target
        p.pointer(0, t.x, t.y + 90);
        p.pointer(1, t.x, t.y + 140);
        CHECK(p.aiming() && std::fabs(p.crosshair().y - t.y) < 0.01f && std::fabs(p.crosshair().x - t.x) < 0.01f,
              "the crosshair sits on the target while aiming");
        p.pointer(2, t.x, t.y + 140);
        CHECK(p.index() == 1 && p.result().hits == 1, "a shot on the target hits");
        const auto t2 = p.target();
        p.pointer(0, t2.x, t2.y);
        p.pointer(2, t2.x + 5, t2.y);
        CHECK(p.index() == 1, "a pull shorter than 25 px is not a shot");
        p.pointer(0, t2.x, t2.y);
        p.pointer(2, t2.x + 100, t2.y);   // the crosshair 180 px left: a miss
        CHECK(p.done() && p.result().hits == 1 && p.result().points == 2 && !p.result().failed, "1 of 2");
    }
    {   // an arrow not shot in time is a miss
        TouchPlay p;
        p.start(PlayKind::Bow, 1, false, area, 3);
        p.pointer(0, 600, 300); p.pointer(1, 600, 302);   // started, still aiming
        p.update(3100);
        CHECK(p.done() && p.result().hits == 0 && p.result().failed, "the arrow's time ran out: a miss (a fail)");
    }
    // 2. Slice: swipe the arrows' ways.
    {
        TouchPlay p;
        p.start(PlayKind::Slice, 3, false, area, 11);
        for (int k = 0; k < 3; k++) {
            const float a = p.arrows()[(size_t)k].dir * 3.14159265f / 4.0f;
            const float len = k == 2 ? 50.0f : 160.0f;   // the third too short: not a cut yet
            p.pointer(0, 650, 200);
            p.pointer(2, 650 + std::cos(a) * len, 200 + std::sin(a) * len);
        }
        CHECK(p.index() == 2 && p.result().hits == 2, "two long cuts the right way; a short swipe is not a cut");
        const float wrong = (p.arrows()[2].dir + 4) * 3.14159265f / 4.0f;   // the opposite way
        p.pointer(0, 650, 200);
        p.pointer(2, 650 + std::cos(wrong) * 160, 200 + std::sin(wrong) * 160);
        CHECK(p.done() && p.result().hits == 2 && p.arrows()[2].state == 2, "the wrong way: a miss");
    }
    {   // the ultimate's memory chain: shown, then hidden; a wrong way ends it
        TouchPlay p;
        p.start(PlayKind::Slice, 4, true, area, 5);
        CHECK(p.started() && p.arrowsShown(), "the ultimate starts at once, its arrows shown");
        p.update(1500);
        CHECK(!p.arrowsShown(), "then hidden (a memory chain)");
        const float wrong = (p.arrows()[0].dir + 4) * 3.14159265f / 4.0f;
        p.pointer(0, 650, 200);
        p.pointer(2, 650 + std::cos(wrong) * 160, 200 + std::sin(wrong) * 160);
        CHECK(p.done() && p.result().hits == 0 && p.result().failed, "a wrong way ends the chain");
    }
    // 3. Circles: the first tap starts the beat; then each circle at its time.
    {
        TouchPlay p;
        p.start(PlayKind::Circles, 3, false, area, 9);
        const auto c = p.circles();
        CHECK(c.size() == 3 && c[0].atMs == 900 && c[2].atMs == 900 + 2 * 550, "three circles, one every 550 ms after 900");
        for (const auto& k : c) CHECK(k.x >= area.x && k.x <= area.x + area.z && k.y >= area.y && k.y <= area.y + area.w, "in the area");
        p.pointer(0, 0, 0);   // the first tap, anywhere: the beat starts
        p.update(890);
        p.pointer(0, c[0].x, c[0].y);   // 10 ms early: perfect
        CHECK(p.circles()[0].state == 2, "on time: perfect");
        p.update(450);                  // 1340 ms: circle 2 closes at 1450 (110 ms early)
        p.pointer(0, c[1].x, c[1].y);
        CHECK(p.circles()[1].state == 1, "110 ms early: a hit");
        p.update(600);                  // 1940 ms: circle 3 (at 2000) never tapped...
        p.update(300);                  // ...2240 ms: past its window
        CHECK(p.done() && p.result().hits == 2 && p.circles()[2].state == 3, "a circle let pass: a miss");
    }
    CHECK(playKindFromString("bow") == PlayKind::Bow && playKindFromString("anything") == PlayKind::Track &&
              std::string(playKindName(PlayKind::Circles)) == "circles", "names");
    if (g_fail) { std::printf("touch_play_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("touch_play_test: all passed\n");
    return 0;
}
