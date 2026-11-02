// job_system.cpp -- see job_system.h.
#include "job_system.h"
#include "singletons.h"   // released at the end of main (docs/engine/15_LEAK_CHECK.md)

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

void JobSystem::f_Start(int) {}
void JobSystem::f_Stop() {}
int JobSystem::f_Workers() { return 0; }
bool JobSystem::f_ThreadsAvailable() { return false; }
void JobSystem::f_ParallelFor(int l_iCount, const std::function<void(int)>& l_fnBody) {
    for (int l_iI = 0; l_iI < l_iCount; ++l_iI) l_fnBody(l_iI);
}

#else

namespace {

struct Job {
    const std::function<void(int)>* m_pBody = nullptr;
    int m_iCount = 0;
    std::atomic<int> m_oNext{0};    // next index to take
    std::atomic<int> m_oDone{0};    // indices finished
};

struct Pool {
    std::vector<std::thread> m_vThreads;
    std::mutex m_oM;                // guards job, generation, active, stopping
    std::condition_variable m_oWake;      // workers: a new job (or stop)
    std::condition_variable m_oFinished;  // caller: the last worker left the job
    Job m_oJob;
    unsigned m_uGeneration = 0;
    int m_iActive = 0;              // workers currently inside the job
    bool m_bStopping = false;
    std::mutex m_oCallMutex;        // one parallelFor at a time
    // Still-running std::threads abort the program when destroyed, so a pool left running until
    // exit is stopped here.
    ~Pool() {
        {
            std::lock_guard<std::mutex> l_oLk(m_oM);
            m_bStopping = true;
        }
        m_oWake.notify_all();
        for (std::thread& l_oT : m_vThreads) if (l_oT.joinable()) l_oT.join();
    }
};

Pool& f_Pool() { static Pool l_oP; return l_oP; }
thread_local bool g_bTInJob = false;   // nested parallelFor runs inline

void f_RunIndices(Job& l_oJob) {
    for (int l_iI; (l_iI = l_oJob.m_oNext.fetch_add(1)) < l_oJob.m_iCount;) {
        (*l_oJob.m_pBody)(l_iI);
        l_oJob.m_oDone.fetch_add(1);
    }
}

void f_WorkerMain(unsigned l_uSeen) {
    Pool& l_oP = f_Pool();
    std::unique_lock<std::mutex> l_oLk(l_oP.m_oM);
    for (;;) {
        l_oP.m_oWake.wait(l_oLk, [&] { return l_oP.m_bStopping || l_oP.m_uGeneration != l_uSeen; });
        if (l_oP.m_bStopping) return;
        l_uSeen = l_oP.m_uGeneration;
        ++l_oP.m_iActive;
        l_oLk.unlock();
        g_bTInJob = true;
        f_RunIndices(l_oP.m_oJob);
        g_bTInJob = false;
        l_oLk.lock();
        if (--l_oP.m_iActive == 0) l_oP.m_oFinished.notify_all();
    }
}

}  // namespace

void JobSystem::f_Start(int l_iWorkers) {
    Pool& l_oP = f_Pool();
    std::lock_guard<std::mutex> l_oCall(l_oP.m_oCallMutex);
    if (!l_oP.m_vThreads.empty()) return;
    if (l_iWorkers < 0) {
        const int l_iHw = (int)std::thread::hardware_concurrency();
        l_iWorkers = std::max(0, std::min(l_iHw - 1, 8));
    }
#ifdef __EMSCRIPTEN__
    // Only the pre-started pool: a new Web Worker would need the main thread to return to the
    // browser first, and the main thread waits for jobs -- it would never arrive.
    l_iWorkers = std::min(l_iWorkers, TOMS_WEB_THREAD_POOL);
#endif
    {
        std::lock_guard<std::mutex> l_oLk(l_oP.m_oM);
        l_oP.m_bStopping = false;
    }
    for (int l_iI = 0; l_iI < l_iWorkers; ++l_iI) l_oP.m_vThreads.emplace_back(f_WorkerMain, l_oP.m_uGeneration);
    std::fprintf(stderr, "[jobs] %d worker thread(s)\n", l_iWorkers);
    // A singleton with threads: stopped (and its thread list freed) by releaseSingletons() at the end of main()
    if (l_iWorkers > 0) f_OnReleaseSingletons("job system", [] {
        JobSystem::f_Stop();
        std::vector<std::thread>().swap(f_Pool().m_vThreads);
    });
}

void JobSystem::f_Stop() {
    Pool& l_oP = f_Pool();
    std::lock_guard<std::mutex> l_oCall(l_oP.m_oCallMutex);
    {
        std::lock_guard<std::mutex> l_oLk(l_oP.m_oM);
        l_oP.m_bStopping = true;
    }
    l_oP.m_oWake.notify_all();
    for (std::thread& l_oT : l_oP.m_vThreads) l_oT.join();
    l_oP.m_vThreads.clear();
}

int JobSystem::f_Workers() { return (int)f_Pool().m_vThreads.size(); }
bool JobSystem::f_ThreadsAvailable() { return true; }

void JobSystem::f_ParallelFor(int l_iCount, const std::function<void(int)>& l_fnBody) {
    if (l_iCount <= 0) return;
    Pool& l_oP = f_Pool();
    if (g_bTInJob || l_iCount == 1) {           // nested, or nothing to share
        for (int l_iI = 0; l_iI < l_iCount; ++l_iI) l_fnBody(l_iI);
        return;
    }
    std::lock_guard<std::mutex> l_oCall(l_oP.m_oCallMutex);
    if (l_oP.m_vThreads.empty()) {
        g_bTInJob = true;                    // a nested call must not take callMutex again
        for (int l_iI = 0; l_iI < l_iCount; ++l_iI) l_fnBody(l_iI);
        g_bTInJob = false;
        return;
    }
    {
        // A worker that woke late for the previous job may still be inside it (finding nothing
        // left to take): let it leave before the job is reused.
        std::unique_lock<std::mutex> l_oLk(l_oP.m_oM);
        l_oP.m_oFinished.wait(l_oLk, [&] { return l_oP.m_iActive == 0; });
        l_oP.m_oJob.m_pBody = &l_fnBody;
        l_oP.m_oJob.m_iCount = l_iCount;
        l_oP.m_oJob.m_oNext.store(0);
        l_oP.m_oJob.m_oDone.store(0);
        ++l_oP.m_uGeneration;
    }
    l_oP.m_oWake.notify_all();
    g_bTInJob = true;
    f_RunIndices(l_oP.m_oJob);                     // the caller works too
    g_bTInJob = false;
    std::unique_lock<std::mutex> l_oLk(l_oP.m_oM);
    l_oP.m_oFinished.wait(l_oLk, [&] { return l_oP.m_oJob.m_oDone.load() == l_iCount && l_oP.m_iActive == 0; });
}

#endif

}  // namespace toms
