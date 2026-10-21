// leak_check.h -- Windows: which memory is still allocated when the game exits (docs/33_LEAK_CHECK.md).
//
// Two reports at exit:
//   * every build: Object::DumpLeaks() -- the game objects (Trackable) still alive, by type and name;
//   * Windows Debug builds (the MSVC debug heap, _DEBUG): EVERY heap block allocated after leakCheckBegin() and not
//     freed by leakCheckEnd() -- its allocation number, size, and first bytes -- written to toms_leaks.log (and the
//     debugger's Output window). Release builds and other platforms: these two calls do nothing.
//
// Finding where a leaked block came from (the {N} in the report is its allocation number; the numbers repeat between
// runs when the run repeats: --fixed-dt, the same keys):
//   --leak-trace=N[,M,...]  run again: the call stacks of those allocations (function, file:line) are added to the
//                           report -- no debugger needed;
//   --leak-break=N          in the debugger: it stops inside allocation N.
//
// The report is written at the very end: after main() returned, after toms::releaseSingletons() (singletons.h, called
// at the end of main) and after the function-local statics were destroyed -- so whatever it lists, nobody released.
// An empty report means no leak.
#pragma once

namespace toms {

// At the top of main: turn the debug heap's tracking on, remember what is allocated now. breakAlloc > 0: stop in the
// debugger when that allocation number is made.
void leakCheckBegin(long breakAlloc = 0, const char* traceList = nullptr);   // traceList: "4558,348307"
// At the end of main, after releaseSingletons(): where the report goes. It is written once the statics are destroyed
// (an atexit handler leakCheckBegin registered). Returns 0.
long leakCheckEnd(const char* reportPath = "toms_leaks.log");

} // namespace toms
