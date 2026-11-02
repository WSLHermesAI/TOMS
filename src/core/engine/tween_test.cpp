// tween_test.cpp -- toms::Tween and the easing curves (tween.h). Exits 0 on success.
#include "tween.h"

#include <cmath>
#include <cstdio>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)
static bool f_Near(float l_fA, float l_fB, float l_fEps = 0.01f) { return std::fabs(l_fA - l_fB) < l_fEps; }

int main() {
    // 1. Every curve starts at 0 and ends at 1.
    const Ease l_arrAll[] = {Ease::Linear, Ease::InQuad, Ease::OutQuad, Ease::InOutQuad, Ease::InCubic, Ease::OutCubic,
                        Ease::InOutCubic, Ease::OutBack, Ease::OutBounce};
    for (Ease l_eE : l_arrAll) {
        CHECK(f_Near(f_Ease(l_eE, 0.0f), 0.0f), "every ease starts at 0");
        CHECK(f_Near(f_Ease(l_eE, 1.0f), 1.0f), "every ease ends at 1");
    }
    CHECK(f_Near(f_Ease(Ease::Linear, 0.25f), 0.25f), "linear is linear");
    CHECK(f_Ease(Ease::InQuad, 0.5f) < 0.5f && f_Ease(Ease::OutQuad, 0.5f) > 0.5f, "in is slow first, out fast first");
    CHECK(f_Ease(Ease::OutBack, 0.8f) > 1.0f, "outBack overshoots");
    CHECK(f_EaseFromName("outBack") == Ease::OutBack && f_EaseFromName("nonsense") == Ease::Linear, "names");

    // 2. Once: from -> to, then stays.
    {
        Tween l_oT(10.0f, 20.0f, 100);
        CHECK(f_Near(l_oT.f_Value(), 10.0f), "once: starts at from");
        l_oT.f_Update(50);
        CHECK(f_Near(l_oT.f_Value(), 15.0f), "once: halfway (linear)");
        l_oT.f_Update(500);
        CHECK(f_Near(l_oT.f_Value(), 20.0f) && l_oT.f_Done(), "once: ends at to and is done");
    }
    // 3. Repeat wraps; PingPong comes back, even across a long frame.
    {
        Tween l_oR(0.0f, 1.0f, 100, Ease::Linear, Tween::Loop::Repeat);
        l_oR.f_Update(130);
        CHECK(f_Near(l_oR.f_Value(), 0.3f) && !l_oR.f_Done(), "repeat wraps");
        Tween l_oP(0.0f, 100.0f, 100, Ease::Linear, Tween::Loop::PingPong);
        l_oP.f_Update(130);
        CHECK(f_Near(l_oP.f_Value(), 70.0f) && !l_oP.f_Forward(), "ping-pong turns back at the end");
        l_oP.f_Update(100);
        CHECK(f_Near(l_oP.f_Value(), 30.0f, 0.5f) && l_oP.f_Forward(), "and turns again at the start");
        l_oP.f_Update(250);   // 2 passes + 50: a long frame crosses both ends
        CHECK(f_Near(l_oP.f_Value(), 80.0f, 0.5f) && l_oP.f_Forward(), "a long frame crossing both ends keeps the direction right");
    }
    // 4. setDuration keeps the position (a marker speeding up as power charges).
    {
        Tween l_oT(0.0f, 100.0f, 1000);
        l_oT.f_Update(500);
        l_oT.f_SetDuration(200);
        CHECK(f_Near(l_oT.f_Value(), 50.0f, 0.6f), "a new speed keeps the place");
        l_oT.f_Update(100);
        CHECK(f_Near(l_oT.f_Value(), 100.0f), "and goes on at the new speed");
    }

    if (g_iFail) { std::printf("tween_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("tween_test: all passed\n");
    return 0;
}
