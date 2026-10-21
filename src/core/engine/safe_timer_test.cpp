// safe_timer_test.cpp -- toms::TimerQueue / TimerOwner (safe_timer.h). Exits 0 on success.
#include "safe_timer.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

int main() {
    // 1. A call runs once, when its time comes -- not before.
    {
        TimerQueue q; TimerOwner o; int n = 0;
        q.after(o, 100, [&] { n++; });
        q.update(99);
        CHECK(n == 0, "not before its time");
        q.update(1);
        CHECK(n == 1, "runs at its time");
        q.update(500);
        CHECK(n == 1, "runs once");
        CHECK(q.pending() == 0, "nothing left");
    }
    // 2. The owner gone: its calls never run (the point of the queue).
    {
        TimerQueue q; int n = 0;
        {
            TimerOwner o;
            q.after(o, 10, [&] { n++; });
            CHECK(q.pending() == 1, "one pending while the owner lives");
        }
        q.update(100);
        CHECK(n == 0, "a destroyed owner's call never runs");
        CHECK(q.pending() == 0, "and is not counted");
    }
    // 3. reset() cancels what was scheduled before it, not what comes after.
    {
        TimerQueue q; TimerOwner o; std::string log;
        q.after(o, 10, [&] { log += "old"; });
        o.reset();
        q.after(o, 10, [&] { log += "new"; });
        q.update(20);
        CHECK(log == "new", "reset cancels only the earlier calls");
    }
    // 4. Time order; equal times in scheduling order; one long frame runs everything due.
    {
        TimerQueue q; TimerOwner o; std::string log;
        q.after(o, 30, [&] { log += "c"; });
        q.after(o, 10, [&] { log += "a"; });
        q.after(o, 10, [&] { log += "b"; });
        q.update(100);
        CHECK(log == "abc", "time order, ties by scheduling order");
    }
    // 5. cancel(id).
    {
        TimerQueue q; TimerOwner o; int n = 0;
        auto id = q.after(o, 10, [&] { n++; });
        q.cancel(id);
        q.update(50);
        CHECK(n == 0, "a cancelled call does not run");
    }
    // 6. A call that schedules another: a 0 ms one runs on the NEXT update, not inside this one.
    {
        TimerQueue q; TimerOwner o; std::string log;
        q.after(o, 10, [&] { log += "1"; q.after(o, 0, [&] { log += "2"; }); });
        q.update(10);
        CHECK(log == "1", "a call scheduled inside an update waits for the next");
        q.update(0);
        CHECK(log == "12", "and runs then");
    }
    // 7. every(): repeats, at most once per update, until its owner goes.
    {
        TimerQueue q; int n = 0;
        auto o = std::make_unique<TimerOwner>();
        q.every(*o, 50, [&] { n++; });
        q.update(49);  CHECK(n == 0, "every: not before the first period");
        q.update(1);   CHECK(n == 1, "every: first call");
        q.update(50);  CHECK(n == 2, "every: second call");
        q.update(500); CHECK(n == 3, "every: once per update even after a long frame");
        o.reset();
        q.update(500); CHECK(n == 3, "every: stops when the owner is gone");
    }
    // 8. A copied owner owns nothing of the original's (copying a battle state must not keep its calls alive).
    {
        TimerQueue q; int n = 0;
        auto a = std::make_unique<TimerOwner>();
        q.after(*a, 10, [&] { n++; });
        TimerOwner b = *a;
        a.reset();
        q.update(20);
        CHECK(n == 0, "a copy does not keep the original's calls");
        (void)b;
    }

    if (g_fail) { std::printf("safe_timer_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("safe_timer_test: all passed\n");
    return 0;
}
