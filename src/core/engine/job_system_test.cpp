// job_system_test.cpp -- JobSystem: every index runs exactly once, with and without workers,
// under many back-to-back jobs (the case that exposes races between one job and the next).
#include "job_system.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

static int g_iFail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); ++g_iFail; } } while (0)

static void f_RunChecks(const char* l_pszMode) {
    // 1. each index exactly once
    std::vector<std::atomic<int>> l_vHits(1000);
    toms::JobSystem::f_ParallelFor(1000, [&](int l_iI) { l_vHits[l_iI].fetch_add(1); });
    int l_iWrong = 0;
    for (auto& l_oH : l_vHits) l_iWrong += l_oH.load() != 1;
    CHECK(l_iWrong == 0, "%s: %d of 1000 indices did not run exactly once", l_pszMode, l_iWrong);

    // 2. many small jobs back to back: sums stay exact
    long long l_i64Total = 0;
    for (int l_iRound = 0; l_iRound < 2000; ++l_iRound) {
        std::atomic<long long> l_oSum{0};
        toms::JobSystem::f_ParallelFor(64, [&](int l_iI) { l_oSum.fetch_add(l_iI + 1); });
        l_i64Total += l_oSum.load();
    }
    CHECK(l_i64Total == 2000LL * (64 * 65 / 2), "%s: back-to-back jobs lost work (total %lld)", l_pszMode, l_i64Total);

    // 3. a nested parallelFor runs inline instead of deadlocking
    std::atomic<int> l_oInner{0};
    toms::JobSystem::f_ParallelFor(8, [&](int) { toms::JobSystem::f_ParallelFor(10, [&](int) { l_oInner.fetch_add(1); }); });
    CHECK(l_oInner.load() == 80, "%s: nested jobs ran %d of 80 times", l_pszMode, l_oInner.load());

    // 4. empty and single jobs
    int l_iRan = 0;
    toms::JobSystem::f_ParallelFor(0, [&](int) { ++l_iRan; });
    toms::JobSystem::f_ParallelFor(1, [&](int) { ++l_iRan; });
    CHECK(l_iRan == 1, "%s: count 0/1 ran %d times", l_pszMode, l_iRan);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not swallow what was printed
    // Inline (no workers): the single-threaded web build's behaviour.
    f_RunChecks("0 workers");
    CHECK(toms::JobSystem::f_Workers() == 0, "workers() should be 0 before start()");

    toms::JobSystem::f_Start(3);
    CHECK(toms::JobSystem::f_Workers() == 3, "start(3) gave %d workers", toms::JobSystem::f_Workers());
    f_RunChecks("3 workers");

    // Work really spreads over several threads (needs a machine with >1 core).
    std::mutex l_oM;
    std::set<std::thread::id> l_setIds;
    toms::JobSystem::f_ParallelFor(400, [&](int) {
        volatile double l_dX = 0; for (int l_iK = 0; l_iK < 20000; ++l_iK) l_dX += l_iK * 0.5;   // enough work to share
        std::lock_guard<std::mutex> l_oLk(l_oM); l_setIds.insert(std::this_thread::get_id());
    });
    if (std::thread::hardware_concurrency() > 1)
        CHECK(l_setIds.size() > 1, "400 jobs all ran on one thread");

    toms::JobSystem::f_Stop();
    CHECK(toms::JobSystem::f_Workers() == 0, "stop() left %d workers", toms::JobSystem::f_Workers());
    f_RunChecks("after stop");

    if (g_iFail == 0) { std::printf("job_system_test: ALL PASS\n"); return 0; }
    std::printf("job_system_test: %d FAILED\n", g_iFail);
    return 1;
}
