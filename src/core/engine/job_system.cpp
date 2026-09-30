// job_system.cpp -- see job_system.h.
#include "job_system.h"

#include <algorithm>
#include <cstdio>

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define TOMS_NO_THREADS 1        // the single-threaded web build: every job runs inline
#endif

#ifndef TOMS_NO_THREADS
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#endif

#ifndef TOMS_WEB_THREAD_POOL
#define TOMS_WEB_THREAD_POOL 4   // must match -sPTHREAD_POOL_SIZE (src/game/CMakeLists.txt)
#endif

namespace toms {

#ifdef TOMS_NO_THREADS

void JobSystem::start(int) {}
void JobSystem::stop() {}
int JobSystem::workers() { return 0; }
bool JobSystem::threadsAvailable() { return false; }
void JobSystem::parallelFor(int count, const std::function<void(int)>& body) {
    for (int i = 0; i < count; ++i) body(i);
}

#else

namespace {

struct Job {
    const std::function<void(int)>* body = nullptr;
    int count = 0;
    std::atomic<int> next{0};    // next index to take
    std::atomic<int> done{0};    // indices finished
};

struct Pool {
    std::vector<std::thread> threads;
    std::mutex m;                // guards job, generation, active, stopping
    std::condition_variable wake;      // workers: a new job (or stop)
    std::condition_variable finished;  // caller: the last worker left the job
    Job job;
    unsigned generation = 0;
    int active = 0;              // workers currently inside the job
    bool stopping = false;
    std::mutex callMutex;        // one parallelFor at a time
    // Still-running std::threads abort the program when destroyed, so a pool left running until
    // exit is stopped here.
    ~Pool() {
        {
            std::lock_guard<std::mutex> lk(m);
            stopping = true;
        }
        wake.notify_all();
        for (std::thread& t : threads) if (t.joinable()) t.join();
    }
};

Pool& pool() { static Pool p; return p; }
thread_local bool t_inJob = false;   // nested parallelFor runs inline

void runIndices(Job& job) {
    for (int i; (i = job.next.fetch_add(1)) < job.count;) {
        (*job.body)(i);
        job.done.fetch_add(1);
    }
}

void workerMain(unsigned seen) {
    Pool& p = pool();
    std::unique_lock<std::mutex> lk(p.m);
    for (;;) {
        p.wake.wait(lk, [&] { return p.stopping || p.generation != seen; });
        if (p.stopping) return;
        seen = p.generation;
        ++p.active;
        lk.unlock();
        t_inJob = true;
        runIndices(p.job);
        t_inJob = false;
        lk.lock();
        if (--p.active == 0) p.finished.notify_all();
    }
}

}  // namespace

void JobSystem::start(int workers) {
    Pool& p = pool();
    std::lock_guard<std::mutex> call(p.callMutex);
    if (!p.threads.empty()) return;
    if (workers < 0) {
        const int hw = (int)std::thread::hardware_concurrency();
        workers = std::max(0, std::min(hw - 1, 8));
    }
#ifdef __EMSCRIPTEN__
    // Only the pre-started pool: a new Web Worker would need the main thread to return to the
    // browser first, and the main thread waits for jobs -- it would never arrive.
    workers = std::min(workers, TOMS_WEB_THREAD_POOL);
#endif
    {
        std::lock_guard<std::mutex> lk(p.m);
        p.stopping = false;
    }
    for (int i = 0; i < workers; ++i) p.threads.emplace_back(workerMain, p.generation);
    std::fprintf(stderr, "[jobs] %d worker thread(s)\n", workers);
}

void JobSystem::stop() {
    Pool& p = pool();
    std::lock_guard<std::mutex> call(p.callMutex);
    {
        std::lock_guard<std::mutex> lk(p.m);
        p.stopping = true;
    }
    p.wake.notify_all();
    for (std::thread& t : p.threads) t.join();
    p.threads.clear();
}

int JobSystem::workers() { return (int)pool().threads.size(); }
bool JobSystem::threadsAvailable() { return true; }

void JobSystem::parallelFor(int count, const std::function<void(int)>& body) {
    if (count <= 0) return;
    Pool& p = pool();
    if (t_inJob || count == 1) {           // nested, or nothing to share
        for (int i = 0; i < count; ++i) body(i);
        return;
    }
    std::lock_guard<std::mutex> call(p.callMutex);
    if (p.threads.empty()) {
        t_inJob = true;                    // a nested call must not take callMutex again
        for (int i = 0; i < count; ++i) body(i);
        t_inJob = false;
        return;
    }
    {
        // A worker that woke late for the previous job may still be inside it (finding nothing
        // left to take): let it leave before the job is reused.
        std::unique_lock<std::mutex> lk(p.m);
        p.finished.wait(lk, [&] { return p.active == 0; });
        p.job.body = &body;
        p.job.count = count;
        p.job.next.store(0);
        p.job.done.store(0);
        ++p.generation;
    }
    p.wake.notify_all();
    t_inJob = true;
    runIndices(p.job);                     // the caller works too
    t_inJob = false;
    std::unique_lock<std::mutex> lk(p.m);
    p.finished.wait(lk, [&] { return p.job.done.load() == count && p.active == 0; });
}

#endif

}  // namespace toms
