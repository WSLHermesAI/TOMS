#pragma once
// job_system.h -- run work on worker threads, the same code on every platform (docs/10_THREADS.md).
//
// A fixed pool of worker threads plus the calling thread share each job. With zero workers --
// the single-threaded web build always, or JobSystem::start(0) -- a job simply runs inline on the
// caller, so code written against this API never needs an #ifdef for "are there threads".
//
//   toms::JobSystem::start();                        // once at startup: hardware threads - 1
//   toms::JobSystem::parallelFor(n, [&](int i) {     // blocks until all n calls have returned;
//       results[i] = work(inputs[i]);                //   each i runs exactly once, on any thread
//   });
//
// Rules: the body must be safe to run on several threads at once (write only to its own slot i,
// no shared containers). One parallelFor at a time: calls from several threads queue up; calling
// parallelFor from inside a body runs that inner loop inline. The web's threaded build caps the
// pool at the pre-started worker count (TOMS_WEB_THREAD_POOL); the browser's main thread must
// never wait for a thread that does not exist yet.
#include <functional>

namespace toms {

class JobSystem {
public:
    // workers < 0: hardware threads - 1 (at most 8). Calling start() again changes nothing
    // unless stop() came first.
    static void start(int workers = -1);
    static void stop();                   // joins the workers; later jobs run inline
    static int workers();                 // 0 = everything runs on the calling thread
    static bool threadsAvailable();       // false in the single-threaded web build

    static void parallelFor(int count, const std::function<void(int)>& body);
};

}  // namespace toms
