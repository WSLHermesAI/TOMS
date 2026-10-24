// singletons.h -- releasing the program-wide singletons before the program ends (docs/engine/15_LEAK_CHECK.md).
//
// A singleton that owns memory or threads (the job system's pool, a cache kept for the whole run) registers how to
// release it when it is created; main() calls releaseSingletons() once the game and its assets are gone, just before
// the leak check reports. Released newest first (the reverse of creation, like destructors), each once.
//
//   toms::onReleaseSingletons("job system", [] { JobSystem::stop(); });
//   ...
//   toms::releaseSingletons();   // end of main()
//
// Plain function-local statics need nothing: C++ destroys them after main(), and the leak report is written after that
// (leak_check.h). This is for what C++ would not free on its own (a `new` kept in a pointer) or what must go in order.
// After releaseSingletons() a singleton may be used again (it is made again, and registers again).
#pragma once

#include <functional>

namespace toms {

void onReleaseSingletons(const char* name, std::function<void()> release);
void releaseSingletons();

} // namespace toms
