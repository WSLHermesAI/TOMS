// safe_timer_test.cpp -- toms::TimerQueue / TimerOwner (safe_timer.h). Exits 0 on success.
#include "safe_timer.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

int main() {
    // 1. A call runs once, when its time comes -- not before.
    {
        TimerQueue l_oQ; TimerOwner l_oO; int l_iN = 0;
        l_oQ.f_After(l_oO, 100, [&] { l_iN++; });
        l_oQ.f_Update(99);
        CHECK(l_iN == 0, "not before its time");
        l_oQ.f_Update(1);
        CHECK(l_iN == 1, "runs at its time");
        l_oQ.f_Update(500);
        CHECK(l_iN == 1, "runs once");
        CHECK(l_oQ.f_Pending() == 0, "nothing left");
    }
    // 2. The owner gone: its calls never run (the point of the queue).
    {
        TimerQueue l_oQ; int l_iN = 0;
        {
            TimerOwner l_oO;
            l_oQ.f_After(l_oO, 10, [&] { l_iN++; });
            CHECK(l_oQ.f_Pending() == 1, "one pending while the owner lives");
        }
        l_oQ.f_Update(100);
        CHECK(l_iN == 0, "a destroyed owner's call never runs");
        CHECK(l_oQ.f_Pending() == 0, "and is not counted");
    }
    // 3. reset() cancels what was scheduled before it, not what comes after.
    {
        TimerQueue l_oQ; TimerOwner l_oO; std::string l_strLog;
        l_oQ.f_After(l_oO, 10, [&] { l_strLog += "old"; });
        l_oO.f_Reset();
        l_oQ.f_After(l_oO, 10, [&] { l_strLog += "new"; });
        l_oQ.f_Update(20);
        CHECK(l_strLog == "new", "reset cancels only the earlier calls");
    }
    // 4. Time order; equal times in scheduling order; one long frame runs everything due.
    {
        TimerQueue l_oQ; TimerOwner l_oO; std::string l_strLog;
        l_oQ.f_After(l_oO, 30, [&] { l_strLog += "c"; });
        l_oQ.f_After(l_oO, 10, [&] { l_strLog += "a"; });
        l_oQ.f_After(l_oO, 10, [&] { l_strLog += "b"; });
        l_oQ.f_Update(100);
        CHECK(l_strLog == "abc", "time order, ties by scheduling order");
    }
    // 5. cancel(id).
    {
        TimerQueue l_oQ; TimerOwner l_oO; int l_iN = 0;
        auto l_uId = l_oQ.f_After(l_oO, 10, [&] { l_iN++; });
        l_oQ.f_Cancel(l_uId);
        l_oQ.f_Update(50);
        CHECK(l_iN == 0, "a cancelled call does not run");
    }
    // 6. A call that schedules another: a 0 ms one runs on the NEXT update, not inside this one.
    {
        TimerQueue l_oQ; TimerOwner l_oO; std::string l_strLog;
        l_oQ.f_After(l_oO, 10, [&] { l_strLog += "1"; l_oQ.f_After(l_oO, 0, [&] { l_strLog += "2"; }); });
        l_oQ.f_Update(10);
        CHECK(l_strLog == "1", "a call scheduled inside an update waits for the next");
        l_oQ.f_Update(0);
        CHECK(l_strLog == "12", "and runs then");
    }
    // 7. every(): repeats, at most once per update, until its owner goes.
    {
        TimerQueue l_oQ; int l_iN = 0;
        auto l_upO = std::make_unique<TimerOwner>();
        l_oQ.f_Every(*l_upO, 50, [&] { l_iN++; });
        l_oQ.f_Update(49);  CHECK(l_iN == 0, "every: not before the first period");
        l_oQ.f_Update(1);   CHECK(l_iN == 1, "every: first call");
        l_oQ.f_Update(50);  CHECK(l_iN == 2, "every: second call");
        l_oQ.f_Update(500); CHECK(l_iN == 3, "every: once per update even after a long frame");
        l_upO.reset();
        l_oQ.f_Update(500); CHECK(l_iN == 3, "every: stops when the owner is gone");
    }
    // 8. A copied owner owns nothing of the original's (copying a battle state must not keep its calls alive).
    {
        TimerQueue l_oQ; int l_iN = 0;
        auto l_upA = std::make_unique<TimerOwner>();
        l_oQ.f_After(*l_upA, 10, [&] { l_iN++; });
        TimerOwner l_oB = *l_upA;
        l_upA.reset();
        l_oQ.f_Update(20);
        CHECK(l_iN == 0, "a copy does not keep the original's calls");
        (void)l_oB;
    }

    if (g_iFail) { std::printf("safe_timer_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("safe_timer_test: all passed\n");
    return 0;
}
