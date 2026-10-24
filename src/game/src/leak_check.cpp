// leak_check.cpp -- see leak_check.h.
//
// Windows Debug builds only. Two records of what is allocated:
//   1. our own operator new / delete (replaced below for this executable): every `new` of the game's code -- objects,
//      shared_ptr / make_shared control blocks, every STL container -- is put in a map with its size, its number and
//      the call stack that made it; every `delete` takes it out (a delete of a pointer the map does not have is
//      counted: a double delete, or memory from elsewhere). At exit what is left, made after main() began, is the
//      report, grouped by where it was made, with function names and lines. Qt's DLLs keep their own new / delete,
//      so their internals are not in it -- only the code of this executable (the game, the engine, RmlUi, bgfx).
//   2. the MSVC debug heap: every heap block, from any module, still allocated (the CRT's own dump).
// TOMS_LEAK_TRACK=0 turns 1 off (it captures a call stack per `new`, which slows a debug run down a little).

#include "leak_check.h"

#if defined(_WIN32) && defined(_DEBUG)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <crtdbg.h>
#include <dbghelp.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>
#include <unordered_map>
#include <vector>
#pragma comment(lib, "dbghelp.lib")

namespace {

// ---------------------------------------------------------------- the new / delete map
// The map's own memory comes from a private Windows heap -- never operator new (which would call back into the map),
// and never the C runtime heap either: the free hook runs inside the C runtime's heap lock and then takes the map's
// lock, so a thread holding the map's lock must never wait for the heap lock (two threads, each holding one, would wait
// for each other forever -- the exit hang of 2026-10-21).
HANDLE privateHeap() {
    static HANDLE h = HeapCreate(0, 0, 0);
    return h;
}
template <class T> struct MallocAlloc {
    using value_type = T;
    MallocAlloc() = default;
    template <class U> MallocAlloc(const MallocAlloc<U>&) {}
    T* allocate(size_t n) {
        if (void* p = HeapAlloc(privateHeap(), 0, n * sizeof(T))) return static_cast<T*>(p);
        throw std::bad_alloc();
    }
    void deallocate(T* p, size_t) { HeapFree(privateHeap(), 0, p); }
    template <class U> bool operator==(const MallocAlloc<U>&) const { return true; }
    template <class U> bool operator!=(const MallocAlloc<U>&) const { return false; }
};

constexpr int kStackFrames = 14;
struct Alloc {
    size_t size = 0;
    uint64_t seq = 0;              // the order it was made in (0 = before main began)
    USHORT frames = 0;
    void* stack[kStackFrames] = {};
};
using AllocMap = std::unordered_map<void*, Alloc, std::hash<void*>, std::equal_to<void*>,
                                    MallocAlloc<std::pair<void* const, Alloc>>>;

struct Tracker {
    SRWLOCK lock = SRWLOCK_INIT;
    AllocMap live;                 // only what is still allocated: a free takes its entry out
    uint64_t seq = 0;              // allocations so far
    uint64_t baseline = 0;         // seq when main began: the report shows only later allocations
    int on = -1;                  // -1 not decided yet (TOMS_LEAK_TRACK), 0 off, 1 on
};

// Never destroyed: deletes keep coming while the program's statics are torn down after main.
Tracker& tracker() {
    alignas(Tracker) static unsigned char storage[sizeof(Tracker)];
    static Tracker* t = new (storage) Tracker();   // placement new: not the replaceable operator new
    return *t;
}

thread_local bool t_inside = false;   // this thread is inside the tracker (or writing the report): do not track
void installHook();                   // below, with the debug heap's hook

bool trackingOn(Tracker& t) {
    if (t.on < 0) {
        char v[8] = {};
        t.on = (GetEnvironmentVariableA("TOMS_LEAK_TRACK", v, sizeof v) > 0 && v[0] == '0') ? 0 : 1;
        if (t.on == 1) installHook();   // frees from every module come back through it
    }
    return t.on == 1;
}

void track(void* p, size_t size) {
    if (!p || t_inside) return;
    Tracker& t = tracker();
    if (!trackingOn(t)) return;
    t_inside = true;
    Alloc a;
    a.size = size;
    a.frames = CaptureStackBackTrace(2, kStackFrames, a.stack, nullptr);   // skip track() and operator new
    AcquireSRWLockExclusive(&t.lock);
    a.seq = ++t.seq;
    t.live[p] = a;
    ReleaseSRWLockExclusive(&t.lock);
    t_inside = false;
}

// A delete (our operator delete) or a free (the debug heap's hook, which sees every free of every module -- Qt deleting
// a widget our code made with `new` included): the allocation is released, so it leaves the map and the report.
void untrack(void* p, bool /*fromDelete*/) {
    if (!p || t_inside) return;
    Tracker& t = tracker();
    if (t.on != 1) return;
    t_inside = true;
    AcquireSRWLockExclusive(&t.lock);
    t.live.erase(p);
    ReleaseSRWLockExclusive(&t.lock);
    t_inside = false;
}

void* allocate(size_t size) {
    void* p = std::malloc(size ? size : 1);
    if (p) track(p, size);
    return p;
}
void* allocateAligned(size_t size, std::align_val_t al) {
    void* p = _aligned_malloc(size ? size : 1, (size_t)al);
    if (p) track(p, size);
    return p;
}

// ---------------------------------------------------------------- the debug heap's report
_CrtMemState g_start;
bool g_started = false;
void installHook();

// --leak-trace: the call stacks of chosen debug-heap allocation numbers, captured as they happen. Fixed arrays: the
// allocation hook must not allocate (it would call itself).
constexpr int kMaxTraced = 32, kMaxFrames = 24;
struct Trace { long request = 0; USHORT frames = 0; void* stack[kMaxFrames] = {}; size_t size = 0; };
Trace g_traces[kMaxTraced];
int g_traceCount = 0;
_CRT_ALLOC_HOOK g_previousHook = nullptr;

int __cdecl allocHook(int allocType, void* userData, size_t size, int blockType, long request,
                      const unsigned char* file, int line) {
    if (allocType == _HOOK_FREE) untrack(userData, false);   // the block is going: out of the new / delete map
    if (allocType == _HOOK_ALLOC || allocType == _HOOK_REALLOC)
        for (int i = 0; i < g_traceCount; ++i)
            if (g_traces[i].request == request) {
                g_traces[i].size = size;
                g_traces[i].frames = CaptureStackBackTrace(2, kMaxFrames, g_traces[i].stack, nullptr);   // skip the CRT
                break;
            }
    return g_previousHook ? g_previousHook(allocType, userData, size, blockType, request, file, line) : TRUE;
}

bool g_hookInstalled = false;
void installHook() {
    if (g_hookInstalled) return;
    g_hookInstalled = true;
    g_previousHook = _CrtSetAllocHook(allocHook);
}

// ---------------------------------------------------------------- writing
void writeLine(HANDLE file, const char* text) {
    DWORD written = 0;
    WriteFile(file, text, (DWORD)std::strlen(text), &written, nullptr);
}

// A frame we skip in the report: the C runtime, the STL's allocation plumbing, Windows' thread start.
bool plumbing(const char* name) {
    static const char* skip[] = {"_dbg", "malloc", "operator new", "std::_Allocate", "std::allocator<", "std::_Default_allocate",
                                 "std::_Container_proxy", "RtlUserThreadStart", "BaseThreadInitThunk", "invoke_main",
                                 "__scrt_", "mainCRTStartup", "std::_Allocate_at_least", "std::_Alloc_construct"};
    for (const char* s : skip) if (std::strstr(name, s)) return true;
    return false;
}

// One call stack, a frame a line ("function  file:line"); plumbing frames left out. maxLines: our code first.
void writeStack(HANDLE file, HANDLE process, bool symbols, void* const* stack, USHORT frames, int maxLines) {
    char line[1200];
    int written = 0;
    for (USHORT f = 0; f < frames && written < maxLines; ++f) {
        const DWORD64 addr = (DWORD64)stack[f];
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256] = {};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 off = 0;
        IMAGEHLP_LINE64 src = {};
        src.SizeOfStruct = sizeof src;
        DWORD lineOff = 0;
        const bool named = symbols && SymFromAddr(process, addr, &off, sym);
        if (named && plumbing(sym->Name)) continue;
        const bool hasLine = symbols && SymGetLineFromAddr64(process, addr, &lineOff, &src);
        std::snprintf(line, sizeof line, "    %s  %s:%lu\r\n", named ? sym->Name : "?",
                      hasLine ? src.FileName : "?", hasLine ? (unsigned long)src.LineNumber : 0ul);
        writeLine(file, line);
        written++;
    }
}

// Each --leak-trace allocation's call stack.
void writeTraces(HANDLE file, HANDLE process, bool symbols) {
    if (g_traceCount == 0) return;
    char line[256];
    writeLine(file, "\r\n==== Call stacks of the traced debug-heap allocations (--leak-trace) ====\r\n");
    for (int i = 0; i < g_traceCount; ++i) {
        const Trace& t = g_traces[i];
        std::snprintf(line, sizeof line, "\r\n{%ld}%s\r\n", t.request,
                      t.frames ? "" : "  not made in this run (the run differed, or it was made before main)");
        writeLine(file, line);
        writeStack(file, process, symbols, t.stack, t.frames, kMaxFrames);
    }
}

// The new / delete map: what is left, made after main began, grouped by its call stack; the biggest groups first.
// Returns how many allocations are left.
uint64_t writeNewDeleteReport(HANDLE file, HANDLE process, bool symbols) {
    Tracker& t = tracker();
    if (t.on != 1) {
        writeLine(file, "==== new / delete map: off (TOMS_LEAK_TRACK=0) ====\r\n\r\n");
        return 0;
    }
    struct Group { uint64_t count = 0, bytes = 0, firstSeq = 0; const Alloc* sample = nullptr; };
    struct Key { USHORT n; void* f[kStackFrames]; };
    struct KeyHash { size_t operator()(const Key& k) const { size_t h = k.n; for (USHORT i = 0; i < k.n; ++i) h = h * 1315423911u ^ (size_t)k.f[i]; return h; } };
    struct KeyEq { bool operator()(const Key& a, const Key& b) const { return a.n == b.n && std::memcmp(a.f, b.f, a.n * sizeof(void*)) == 0; } };
    std::unordered_map<Key, Group, KeyHash, KeyEq, MallocAlloc<std::pair<const Key, Group>>> groups;
    uint64_t left = 0, bytes = 0;
    AcquireSRWLockExclusive(&t.lock);
    for (const auto& [p, a] : t.live) {
        if (a.seq <= t.baseline) continue;   // made before main began: statics
        Key k{};
        k.n = a.frames;
        std::memcpy(k.f, a.stack, a.frames * sizeof(void*));
        Group& g = groups[k];
        if (!g.sample || a.seq < g.firstSeq) { g.sample = &a; g.firstSeq = a.seq; }
        g.count++;
        g.bytes += a.size;
        left++;
        bytes += a.size;
    }
    std::vector<std::pair<Key, Group>, MallocAlloc<std::pair<Key, Group>>> sorted(groups.begin(), groups.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.bytes > b.second.bytes; });
    char line[512];
    std::snprintf(line, sizeof line,
                  "==== new / delete map (this executable's operator new / delete) ====\r\n"
                  "Only what was never released: %llu allocation(s), %llu bytes made after main() began and never deleted,\r\n"
                  "from %zu place(s) in the code (everything deleted properly is left out). Written after the singletons were\r\n"
                  "released and the statics destroyed: everything listed here is a leak.\r\n"
                  "Each place: count, bytes, the number of its first allocation, and where it was made (our code first).\r\n",
                  (unsigned long long)left, (unsigned long long)bytes, sorted.size());
    writeLine(file, line);
    for (size_t i = 0; i < sorted.size(); ++i) {   // every place, the biggest first
        const Group& g = sorted[i].second;
        std::snprintf(line, sizeof line, "\r\n[%zu] %llu allocation(s), %llu bytes, first #%llu\r\n", i + 1,
                      (unsigned long long)g.count, (unsigned long long)g.bytes, (unsigned long long)g.firstSeq);
        writeLine(file, line);
        writeStack(file, process, symbols, g.sample->stack, g.sample->frames, 10);
    }
    ReleaseSRWLockExclusive(&t.lock);
    writeLine(file, "\r\n");
    return left;
}

} // namespace

// ---------------------------------------------------------------- the replaced operators (this executable only)
void* operator new(size_t size) {
    if (void* p = allocate(size)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size) {
    if (void* p = allocate(size)) return p;
    throw std::bad_alloc();
}
void* operator new(size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new(size_t size, std::align_val_t al) {
    if (void* p = allocateAligned(size, al)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size, std::align_val_t al) {
    if (void* p = allocateAligned(size, al)) return p;
    throw std::bad_alloc();
}
void* operator new(size_t size, std::align_val_t al, const std::nothrow_t&) noexcept { return allocateAligned(size, al); }
void* operator new[](size_t size, std::align_val_t al, const std::nothrow_t&) noexcept { return allocateAligned(size, al); }

void operator delete(void* p) noexcept { untrack(p, true); std::free(p); }
void operator delete[](void* p) noexcept { untrack(p, true); std::free(p); }
void operator delete(void* p, size_t) noexcept { untrack(p, true); std::free(p); }
void operator delete[](void* p, size_t) noexcept { untrack(p, true); std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { untrack(p, true); std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { untrack(p, true); std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { untrack(p, true); _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { untrack(p, true); _aligned_free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { untrack(p, true); _aligned_free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { untrack(p, true); _aligned_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { untrack(p, true); _aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { untrack(p, true); _aligned_free(p); }

namespace toms {

static long writeReport(const char* reportPath);

namespace {
char g_reportPath[MAX_PATH] = {};
bool g_reportRequested = false;
// Run by the C runtime after main() returned and after every function-local static made since leakCheckBegin was
// destroyed (atexit runs newest first, and this was registered first) -- so statics are gone from the report, and
// what is left is what nobody released.
void reportAtExit() {
    if (g_reportRequested) writeReport(g_reportPath);
}
} // namespace

void leakCheckBegin(long breakAlloc, const char* traceList) {
    // _CRTDBG_ALLOC_MEM_DF: track every block. No _CRTDBG_LEAK_CHECK_DF: the CRT's own end-of-program dump would list
    // every static too, after our report; ours compares with the start instead.
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_ALLOC_MEM_DF);
    if (breakAlloc > 0) _CrtSetBreakAlloc(breakAlloc);
    for (const char* p = traceList; p && *p && g_traceCount < kMaxTraced;) {   // "4558,348307"
        const long n = std::strtol(p, nullptr, 10);
        if (n > 0) g_traces[g_traceCount++].request = n;
        p = std::strchr(p, ',');
        if (p) ++p;
    }
    installHook();   // --leak-trace and the frees share it
    {   // the new / delete map: from here on is what the report counts
        Tracker& t = tracker();
        trackingOn(t);
        AcquireSRWLockExclusive(&t.lock);
        t.baseline = t.seq;
        ReleaseSRWLockExclusive(&t.lock);
    }
    _CrtMemCheckpoint(&g_start);
    g_started = true;
    std::atexit(reportAtExit);   // before any static made from here on: it runs after they are all destroyed
}

long leakCheckEnd(const char* reportPath) {
    // The report is written at exit, after the statics are destroyed (reportAtExit); here only where it goes. The path
    // is made absolute now: the working folder could change before then.
    if (!g_started) return 0;
    if (!GetFullPathNameA(reportPath, MAX_PATH, g_reportPath, nullptr)) std::snprintf(g_reportPath, MAX_PATH, "%s", reportPath);
    g_reportRequested = true;
    return 0;
}

static long writeReport(const char* reportPath) {
    if (!g_started) return 0;
    t_inside = true;   // the report's own allocations are not tracked
    _CrtMemState now, diff;
    _CrtMemCheckpoint(&now);
    const bool heapLeft = _CrtMemDifference(&diff, &g_start, &now) != 0;
    // Signed: written after the statics are destroyed, the heap can hold less than when main() began (statics made
    // before main are freed too), and the difference is then negative.
    const long long blocks = heapLeft ? (long long)(diff.lCounts[_NORMAL_BLOCK] + diff.lCounts[_CLIENT_BLOCK]) : 0;
    const long long bytes = heapLeft ? (long long)(diff.lSizes[_NORMAL_BLOCK] + diff.lSizes[_CLIENT_BLOCK]) : 0;
    HANDLE file = CreateFileA(reportPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    uint64_t newLeft = 0;
    if (file != INVALID_HANDLE_VALUE) {
        writeLine(file,
            "TOMS leak report (Windows Debug). docs/engine/15_LEAK_CHECK.md\r\n"
            "The new / delete map (where each allocation of this program was made), then a short debug-heap summary.\r\n"
            "Written at the very end: after main() returned, toms::releaseSingletons() ran and the statics were destroyed.\r\n\r\n");
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        const bool symbols = SymInitialize(process, nullptr, TRUE) != FALSE;
        newLeft = writeNewDeleteReport(file, process, symbols);
        if (heapLeft) {
            // Only a summary: the debug heap cannot tell a leak from a static and lists every module's blocks (Qt's
            // caches, the statics, this checker's own map), without names -- the map above is the report. The
            // block-by-block dump (and the {N} numbers --leak-trace / --leak-break use) only with TOMS_LEAK_HEAP_DUMP=1.
            char v[8] = {};
            const bool dump = GetEnvironmentVariableA("TOMS_LEAK_HEAP_DUMP", v, sizeof v) > 0 && v[0] == '1';
            char line[512];
            std::snprintf(line, sizeof line,
                "==== Debug heap summary (every module: Qt, statics, this checker's own map -- not a list of leaks) ====\r\n"
                "Change since main() began: %+lld block(s), %+lld bytes (0 or less: nothing more than at the start; less:\r\n"
                "statics made before main() were freed too). Peak in use: %zu bytes.\r\n"
                "Allocated over the whole run (freed included): %zu bytes.\r\n%s",
                blocks, bytes, (size_t)diff.lHighWaterCount, (size_t)diff.lTotalCount,
                dump ? "\r\nTOMS_LEAK_HEAP_DUMP=1: every block follows. {N} is its allocation number: --leak-trace=N[,M,...]\r\n"
                       "adds its call stack at the end, --leak-break=N stops in the debugger where it is made.\r\n\r\n"
                     : "(The block-by-block list: set TOMS_LEAK_HEAP_DUMP=1.)\r\n");
            writeLine(file, line);
            if (dump) {
                _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
                _CrtSetReportFile(_CRT_WARN, file);
                _CrtMemDumpAllObjectsSince(&g_start);
                _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
                _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
            }
        } else {
            writeLine(file, "==== Debug heap: nothing still allocated since main() began ====\r\n");
        }
        writeTraces(file, process, symbols);
        if (symbols) SymCleanup(process);
        CloseHandle(file);
    }
    std::fprintf(stderr, "[leaks] %s: %llu allocation(s) never deleted (debug heap since start: %+lld block(s), %+lld bytes) -- see %s\n",
                 newLeft == 0 ? "no leaks" : "LEAKS", (unsigned long long)newLeft, blocks, bytes, reportPath);
    // From here (the program's statics going away) deletes are not checked any more.
    tracker().on = 0;
    t_inside = false;
    return (long)newLeft;
}

} // namespace toms

#else   // not a Windows Debug build: nothing to do

namespace toms {
void leakCheckBegin(long, const char*) {}
long leakCheckEnd(const char*) { return 0; }
} // namespace toms

#endif
