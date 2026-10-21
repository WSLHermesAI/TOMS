// tween_test.cpp -- toms::Tween and the easing curves (tween.h). Exits 0 on success.
#include "tween.h"

#include <cmath>
#include <cstdio>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)
static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

int main() {
    // 1. Every curve starts at 0 and ends at 1.
    const Ease all[] = {Ease::Linear, Ease::InQuad, Ease::OutQuad, Ease::InOutQuad, Ease::InCubic, Ease::OutCubic,
                        Ease::InOutCubic, Ease::OutBack, Ease::OutBounce};
    for (Ease e : all) {
        CHECK(near(ease(e, 0.0f), 0.0f), "every ease starts at 0");
        CHECK(near(ease(e, 1.0f), 1.0f), "every ease ends at 1");
    }
    CHECK(near(ease(Ease::Linear, 0.25f), 0.25f), "linear is linear");
    CHECK(ease(Ease::InQuad, 0.5f) < 0.5f && ease(Ease::OutQuad, 0.5f) > 0.5f, "in is slow first, out fast first");
    CHECK(ease(Ease::OutBack, 0.8f) > 1.0f, "outBack overshoots");
    CHECK(easeFromName("outBack") == Ease::OutBack && easeFromName("nonsense") == Ease::Linear, "names");

    // 2. Once: from -> to, then stays.
    {
        Tween t(10.0f, 20.0f, 100);
        CHECK(near(t.value(), 10.0f), "once: starts at from");
        t.update(50);
        CHECK(near(t.value(), 15.0f), "once: halfway (linear)");
        t.update(500);
        CHECK(near(t.value(), 20.0f) && t.done(), "once: ends at to and is done");
    }
    // 3. Repeat wraps; PingPong comes back, even across a long frame.
    {
        Tween r(0.0f, 1.0f, 100, Ease::Linear, Tween::Loop::Repeat);
        r.update(130);
        CHECK(near(r.value(), 0.3f) && !r.done(), "repeat wraps");
        Tween p(0.0f, 100.0f, 100, Ease::Linear, Tween::Loop::PingPong);
        p.update(130);
        CHECK(near(p.value(), 70.0f) && !p.forward(), "ping-pong turns back at the end");
        p.update(100);
        CHECK(near(p.value(), 30.0f, 0.5f) && p.forward(), "and turns again at the start");
        p.update(250);   // 2 passes + 50: a long frame crosses both ends
        CHECK(near(p.value(), 80.0f, 0.5f) && p.forward(), "a long frame crossing both ends keeps the direction right");
    }
    // 4. setDuration keeps the position (a marker speeding up as power charges).
    {
        Tween t(0.0f, 100.0f, 1000);
        t.update(500);
        t.setDuration(200);
        CHECK(near(t.value(), 50.0f, 0.6f), "a new speed keeps the place");
        t.update(100);
        CHECK(near(t.value(), 100.0f), "and goes on at the new speed");
    }

    if (g_fail) { std::printf("tween_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("tween_test: all passed\n");
    return 0;
}
