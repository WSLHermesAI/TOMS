// job_system_test.cpp -- JobSystem: every index runs exactly once, with and without workers,
// under many back-to-back jobs (the case that exposes races between one job and the next).
#include "job_system.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

static void runChecks(const char* mode) {
    // 1. each index exactly once
    std::vector<std::atomic<int>> hits(1000);
    toms::JobSystem::parallelFor(1000, [&](int i) { hits[i].fetch_add(1); });
    int wrong = 0;
    for (auto& h : hits) wrong += h.load() != 1;
    CHECK(wrong == 0, "%s: %d of 1000 indices did not run exactly once", mode, wrong);

    // 2. many small jobs back to back: sums stay exact
    long long total = 0;
    for (int round = 0; round < 2000; ++round) {
        std::atomic<long long> sum{0};
        toms::JobSystem::parallelFor(64, [&](int i) { sum.fetch_add(i + 1); });
        total += sum.load();
    }
    CHECK(total == 2000LL * (64 * 65 / 2), "%s: back-to-back jobs lost work (total %lld)", mode, total);

    // 3. a nested parallelFor runs inline instead of deadlocking
    std::atomic<int> inner{0};
    toms::JobSystem::parallelFor(8, [&](int) { toms::JobSystem::parallelFor(10, [&](int) { inner.fetch_add(1); }); });
    CHECK(inner.load() == 80, "%s: nested jobs ran %d of 80 times", mode, inner.load());

    // 4. empty and single jobs
    int ran = 0;
    toms::JobSystem::parallelFor(0, [&](int) { ++ran; });
    toms::JobSystem::parallelFor(1, [&](int) { ++ran; });
    CHECK(ran == 1, "%s: count 0/1 ran %d times", mode, ran);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not swallow what was printed
    // Inline (no workers): the single-threaded web build's behaviour.
    runChecks("0 workers");
    CHECK(toms::JobSystem::workers() == 0, "workers() should be 0 before start()");

    toms::JobSystem::start(3);
    CHECK(toms::JobSystem::workers() == 3, "start(3) gave %d workers", toms::JobSystem::workers());
    runChecks("3 workers");

    // Work really spreads over several threads (needs a machine with >1 core).
    std::mutex m;
    std::set<std::thread::id> ids;
    toms::JobSystem::parallelFor(400, [&](int) {
        volatile double x = 0; for (int k = 0; k < 20000; ++k) x += k * 0.5;   // enough work to share
        std::lock_guard<std::mutex> lk(m); ids.insert(std::this_thread::get_id());
    });
    if (std::thread::hardware_concurrency() > 1)
        CHECK(ids.size() > 1, "400 jobs all ran on one thread");

    toms::JobSystem::stop();
    CHECK(toms::JobSystem::workers() == 0, "stop() left %d workers", toms::JobSystem::workers());
    runChecks("after stop");

    if (g_fail == 0) { std::printf("job_system_test: ALL PASS\n"); return 0; }
    std::printf("job_system_test: %d FAILED\n", g_fail);
    return 1;
}
