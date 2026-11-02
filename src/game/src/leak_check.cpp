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
HANDLE f_PrivateHeap() {
    static HANDLE l_pH = HeapCreate(0, 0, 0);
    return l_pH;
}
template <class T> struct MallocAlloc {
    using value_type = T;
    MallocAlloc() = default;
    template <class U> MallocAlloc(const MallocAlloc<U>&) {}
    T* allocate(size_t l_nN) {
        if (void* l_pP = HeapAlloc(f_PrivateHeap(), 0, l_nN * sizeof(T))) return static_cast<T*>(l_pP);
        throw std::bad_alloc();
    }
    void deallocate(T* l_pP, size_t) { HeapFree(f_PrivateHeap(), 0, l_pP); }
    template <class U> bool operator==(const MallocAlloc<U>&) const { return true; }
    template <class U> bool operator!=(const MallocAlloc<U>&) const { return false; }
};

constexpr int g_iStackFrames = 14;
struct Alloc {
    size_t m_nSize = 0;
    uint64_t m_u64Seq = 0;              // the order it was made in (0 = before main began)
    USHORT m_u16Frames = 0;
    void* m_arrStack[g_iStackFrames] = {};
};
using AllocMap = std::unordered_map<void*, Alloc, std::hash<void*>, std::equal_to<void*>,
                                    MallocAlloc<std::pair<void* const, Alloc>>>;

struct Tracker {
    SRWLOCK m_oLock = SRWLOCK_INIT;
    AllocMap m_mapLive;                 // only what is still allocated: a free takes its entry out
    uint64_t m_u64Seq = 0;              // allocations so far
    uint64_t m_u64Baseline = 0;         // seq when main began: the report shows only later allocations
    int m_iOn = -1;                  // -1 not decided yet (TOMS_LEAK_TRACK), 0 off, 1 on
};

// Never destroyed: deletes keep coming while the program's statics are torn down after main.
Tracker& f_Tracker() {
    alignas(Tracker) static unsigned char l_arrStorage[sizeof(Tracker)];
    static Tracker* l_pT = new (l_arrStorage) Tracker();   // placement new: not the replaceable operator new
    return *l_pT;
}

thread_local bool g_bTInside = false;   // this thread is inside the tracker (or writing the report): do not track
void f_InstallHook();                   // below, with the debug heap's hook

bool f_TrackingOn(Tracker& l_oT) {
    if (l_oT.m_iOn < 0) {
        char l_pszV[8] = {};
        l_oT.m_iOn = (GetEnvironmentVariableA("TOMS_LEAK_TRACK", l_pszV, sizeof l_pszV) > 0 && l_pszV[0] == '0') ? 0 : 1;
        if (l_oT.m_iOn == 1) f_InstallHook();   // frees from every module come back through it
    }
    return l_oT.m_iOn == 1;
}

void f_Track(void* l_pP, size_t l_nSize) {
    if (!l_pP || g_bTInside) return;
    Tracker& l_oT = f_Tracker();
    if (!f_TrackingOn(l_oT)) return;
    g_bTInside = true;
    Alloc l_oA;
    l_oA.m_nSize = l_nSize;
    l_oA.m_u16Frames = CaptureStackBackTrace(2, g_iStackFrames, l_oA.m_arrStack, nullptr);   // skip track() and operator new
    AcquireSRWLockExclusive(&l_oT.m_oLock);
    l_oA.m_u64Seq = ++l_oT.m_u64Seq;
    l_oT.m_mapLive[l_pP] = l_oA;
    ReleaseSRWLockExclusive(&l_oT.m_oLock);
    g_bTInside = false;
}

// A delete (our operator delete) or a free (the debug heap's hook, which sees every free of every module -- Qt deleting
// a widget our code made with `new` included): the allocation is released, so it leaves the map and the report.
void f_Untrack(void* l_pP, bool /*fromDelete*/) {
    if (!l_pP || g_bTInside) return;
    Tracker& l_oT = f_Tracker();
    if (l_oT.m_iOn != 1) return;
    g_bTInside = true;
    AcquireSRWLockExclusive(&l_oT.m_oLock);
    l_oT.m_mapLive.erase(l_pP);
    ReleaseSRWLockExclusive(&l_oT.m_oLock);
    g_bTInside = false;
}

void* f_Allocate(size_t l_nSize) {
    void* l_pP = std::malloc(l_nSize ? l_nSize : 1);
    if (l_pP) f_Track(l_pP, l_nSize);
    return l_pP;
}
void* f_AllocateAligned(size_t l_nSize, std::align_val_t l_eAl) {
    void* l_pP = _aligned_malloc(l_nSize ? l_nSize : 1, (size_t)l_eAl);
    if (l_pP) f_Track(l_pP, l_nSize);
    return l_pP;
}

// ---------------------------------------------------------------- the debug heap's report
_CrtMemState g_oStart;
bool g_bStarted = false;
void f_InstallHook();

// --leak-trace: the call stacks of chosen debug-heap allocation numbers, captured as they happen. Fixed arrays: the
// allocation hook must not allocate (it would call itself).
constexpr int g_iMaxTraced = 32, g_iMaxFrames = 24;
struct Trace { long m_iRequest = 0; USHORT m_u16Frames = 0; void* m_arrStack[g_iMaxFrames] = {}; size_t m_nSize = 0; };
Trace g_arrTraces[g_iMaxTraced];
int g_iTraceCount = 0;
_CRT_ALLOC_HOOK g_fnPreviousHook = nullptr;

int __cdecl f_AllocHook(int l_iAllocType, void* l_pUserData, size_t l_nSize, int l_iBlockType, long l_iRequest,
                      const unsigned char* l_pFile, int l_iLine) {
    if (l_iAllocType == _HOOK_FREE) f_Untrack(l_pUserData, false);   // the block is going: out of the new / delete map
    if (l_iAllocType == _HOOK_ALLOC || l_iAllocType == _HOOK_REALLOC)
        for (int l_iI = 0; l_iI < g_iTraceCount; ++l_iI)
            if (g_arrTraces[l_iI].m_iRequest == l_iRequest) {
                g_arrTraces[l_iI].m_nSize = l_nSize;
                g_arrTraces[l_iI].m_u16Frames = CaptureStackBackTrace(2, g_iMaxFrames, g_arrTraces[l_iI].m_arrStack, nullptr);   // skip the CRT
                break;
            }
    return g_fnPreviousHook ? g_fnPreviousHook(l_iAllocType, l_pUserData, l_nSize, l_iBlockType, l_iRequest, l_pFile, l_iLine) : TRUE;
}

bool g_bHookInstalled = false;
void f_InstallHook() {
    if (g_bHookInstalled) return;
    g_bHookInstalled = true;
    g_fnPreviousHook = _CrtSetAllocHook(f_AllocHook);
}

// ---------------------------------------------------------------- writing
void f_WriteLine(HANDLE l_pFile, const char* l_pszText) {
    DWORD l_uWritten = 0;
    WriteFile(l_pFile, l_pszText, (DWORD)std::strlen(l_pszText), &l_uWritten, nullptr);
}

// A frame we skip in the report: the C runtime, the STL's allocation plumbing, Windows' thread start.
bool f_Plumbing(const char* l_pszName) {
    static const char* l_arrSkip[] = {"_dbg", "malloc", "operator new", "std::_Allocate", "std::allocator<", "std::_Default_allocate",
                                 "std::_Container_proxy", "RtlUserThreadStart", "BaseThreadInitThunk", "invoke_main",
                                 "__scrt_", "mainCRTStartup", "std::_Allocate_at_least", "std::_Alloc_construct"};
    for (const char* l_pszS : l_arrSkip) if (std::strstr(l_pszName, l_pszS)) return true;
    return false;
}

// One call stack, a frame a line ("function  file:line"); plumbing frames left out. maxLines: our code first.
void f_WriteStack(HANDLE l_pFile, HANDLE l_pProcess, bool l_bSymbols, void* const* l_pStack, USHORT l_u16Frames, int l_iMaxLines) {
    char l_pszLine[1200];
    int l_iWritten = 0;
    for (USHORT l_u16F = 0; l_u16F < l_u16Frames && l_iWritten < l_iMaxLines; ++l_u16F) {
        const DWORD64 l_u64Addr = (DWORD64)l_pStack[l_u16F];
        alignas(SYMBOL_INFO) char l_pszBuf[sizeof(SYMBOL_INFO) + 256] = {};
        auto* l_pSym = reinterpret_cast<SYMBOL_INFO*>(l_pszBuf);
        l_pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
        l_pSym->MaxNameLen = 255;
        DWORD64 l_u64Off = 0;
        IMAGEHLP_LINE64 l_oSrc = {};
        l_oSrc.SizeOfStruct = sizeof l_oSrc;
        DWORD l_uLineOff = 0;
        const bool l_bNamed = l_bSymbols && SymFromAddr(l_pProcess, l_u64Addr, &l_u64Off, l_pSym);
        if (l_bNamed && f_Plumbing(l_pSym->Name)) continue;
        const bool l_bHasLine = l_bSymbols && SymGetLineFromAddr64(l_pProcess, l_u64Addr, &l_uLineOff, &l_oSrc);
        std::snprintf(l_pszLine, sizeof l_pszLine, "    %s  %s:%lu\r\n", l_bNamed ? l_pSym->Name : "?",
                      l_bHasLine ? l_oSrc.FileName : "?", l_bHasLine ? (unsigned long)l_oSrc.LineNumber : 0ul);
        f_WriteLine(l_pFile, l_pszLine);
        l_iWritten++;
    }
}

// Each --leak-trace allocation's call stack.
void f_WriteTraces(HANDLE l_pFile, HANDLE l_pProcess, bool l_bSymbols) {
    if (g_iTraceCount == 0) return;
    char l_pszLine[256];
    f_WriteLine(l_pFile, "\r\n==== Call stacks of the traced debug-heap allocations (--leak-trace) ====\r\n");
    for (int l_iI = 0; l_iI < g_iTraceCount; ++l_iI) {
        const Trace& l_oT = g_arrTraces[l_iI];
        std::snprintf(l_pszLine, sizeof l_pszLine, "\r\n{%ld}%s\r\n", l_oT.m_iRequest,
                      l_oT.m_u16Frames ? "" : "  not made in this run (the run differed, or it was made before main)");
        f_WriteLine(l_pFile, l_pszLine);
        f_WriteStack(l_pFile, l_pProcess, l_bSymbols, l_oT.m_arrStack, l_oT.m_u16Frames, g_iMaxFrames);
    }
}

// The new / delete map: what is left, made after main began, grouped by its call stack; the biggest groups first.
// Returns how many allocations are left.
uint64_t f_WriteNewDeleteReport(HANDLE l_pFile, HANDLE l_pProcess, bool l_bSymbols) {
    Tracker& l_oT = f_Tracker();
    if (l_oT.m_iOn != 1) {
        f_WriteLine(l_pFile, "==== new / delete map: off (TOMS_LEAK_TRACK=0) ====\r\n\r\n");
        return 0;
    }
    struct Group { uint64_t m_u64Count = 0, m_u64Bytes = 0, m_u64FirstSeq = 0; const Alloc* m_pSample = nullptr; };
    struct Key { USHORT m_u16N; void* m_arrF[g_iStackFrames]; };
    struct KeyHash { size_t operator()(const Key& l_oK) const { size_t l_nH = l_oK.m_u16N; for (USHORT l_u16I = 0; l_u16I < l_oK.m_u16N; ++l_u16I) l_nH = l_nH * 1315423911u ^ (size_t)l_oK.m_arrF[l_u16I]; return l_nH; } };
    struct KeyEq { bool operator()(const Key& l_oA, const Key& l_oB) const { return l_oA.m_u16N == l_oB.m_u16N && std::memcmp(l_oA.m_arrF, l_oB.m_arrF, l_oA.m_u16N * sizeof(void*)) == 0; } };
    std::unordered_map<Key, Group, KeyHash, KeyEq, MallocAlloc<std::pair<const Key, Group>>> l_mapGroups;
    uint64_t l_u64Left = 0, l_u64Bytes = 0;
    AcquireSRWLockExclusive(&l_oT.m_oLock);
    for (const auto& [l_pP, l_oA] : l_oT.m_mapLive) {
        if (l_oA.m_u64Seq <= l_oT.m_u64Baseline) continue;   // made before main began: statics
        Key l_oK{};
        l_oK.m_u16N = l_oA.m_u16Frames;
        std::memcpy(l_oK.m_arrF, l_oA.m_arrStack, l_oA.m_u16Frames * sizeof(void*));
        Group& l_oG = l_mapGroups[l_oK];
        if (!l_oG.m_pSample || l_oA.m_u64Seq < l_oG.m_u64FirstSeq) { l_oG.m_pSample = &l_oA; l_oG.m_u64FirstSeq = l_oA.m_u64Seq; }
        l_oG.m_u64Count++;
        l_oG.m_u64Bytes += l_oA.m_nSize;
        l_u64Left++;
        l_u64Bytes += l_oA.m_nSize;
    }
    std::vector<std::pair<Key, Group>, MallocAlloc<std::pair<Key, Group>>> l_vSorted(l_mapGroups.begin(), l_mapGroups.end());
    std::sort(l_vSorted.begin(), l_vSorted.end(), [](const auto& l_oA, const auto& l_oB) { return l_oA.second.m_u64Bytes > l_oB.second.m_u64Bytes; });
    char l_pszLine[512];
    std::snprintf(l_pszLine, sizeof l_pszLine,
                  "==== new / delete map (this executable's operator new / delete) ====\r\n"
                  "Only what was never released: %llu allocation(s), %llu bytes made after main() began and never deleted,\r\n"
                  "from %zu place(s) in the code (everything deleted properly is left out). Written after the singletons were\r\n"
                  "released and the statics destroyed: everything listed here is a leak.\r\n"
                  "Each place: count, bytes, the number of its first allocation, and where it was made (our code first).\r\n",
                  (unsigned long long)l_u64Left, (unsigned long long)l_u64Bytes, l_vSorted.size());
    f_WriteLine(l_pFile, l_pszLine);
    for (size_t l_nI = 0; l_nI < l_vSorted.size(); ++l_nI) {   // every place, the biggest first
        const Group& l_oG = l_vSorted[l_nI].second;
        std::snprintf(l_pszLine, sizeof l_pszLine, "\r\n[%zu] %llu allocation(s), %llu bytes, first #%llu\r\n", l_nI + 1,
                      (unsigned long long)l_oG.m_u64Count, (unsigned long long)l_oG.m_u64Bytes, (unsigned long long)l_oG.m_u64FirstSeq);
        f_WriteLine(l_pFile, l_pszLine);
        f_WriteStack(l_pFile, l_pProcess, l_bSymbols, l_oG.m_pSample->m_arrStack, l_oG.m_pSample->m_u16Frames, 10);
    }
    ReleaseSRWLockExclusive(&l_oT.m_oLock);
    f_WriteLine(l_pFile, "\r\n");
    return l_u64Left;
}

} // namespace

// ---------------------------------------------------------------- the replaced operators (this executable only)
void* operator new(size_t l_nSize) {
    if (void* l_pP = f_Allocate(l_nSize)) return l_pP;
    throw std::bad_alloc();
}
void* operator new[](size_t l_nSize) {
    if (void* l_pP = f_Allocate(l_nSize)) return l_pP;
    throw std::bad_alloc();
}
void* operator new(size_t l_nSize, const std::nothrow_t&) noexcept { return f_Allocate(l_nSize); }
void* operator new[](size_t l_nSize, const std::nothrow_t&) noexcept { return f_Allocate(l_nSize); }
void* operator new(size_t l_nSize, std::align_val_t l_eAl) {
    if (void* l_pP = f_AllocateAligned(l_nSize, l_eAl)) return l_pP;
    throw std::bad_alloc();
}
void* operator new[](size_t l_nSize, std::align_val_t l_eAl) {
    if (void* l_pP = f_AllocateAligned(l_nSize, l_eAl)) return l_pP;
    throw std::bad_alloc();
}
void* operator new(size_t l_nSize, std::align_val_t l_eAl, const std::nothrow_t&) noexcept { return f_AllocateAligned(l_nSize, l_eAl); }
void* operator new[](size_t l_nSize, std::align_val_t l_eAl, const std::nothrow_t&) noexcept { return f_AllocateAligned(l_nSize, l_eAl); }

void operator delete(void* l_pP) noexcept { f_Untrack(l_pP, true); std::free(l_pP); }
void operator delete[](void* l_pP) noexcept { f_Untrack(l_pP, true); std::free(l_pP); }
void operator delete(void* l_pP, size_t) noexcept { f_Untrack(l_pP, true); std::free(l_pP); }
void operator delete[](void* l_pP, size_t) noexcept { f_Untrack(l_pP, true); std::free(l_pP); }
void operator delete(void* l_pP, const std::nothrow_t&) noexcept { f_Untrack(l_pP, true); std::free(l_pP); }
void operator delete[](void* l_pP, const std::nothrow_t&) noexcept { f_Untrack(l_pP, true); std::free(l_pP); }
void operator delete(void* l_pP, std::align_val_t) noexcept { f_Untrack(l_pP, true); _aligned_free(l_pP); }
void operator delete[](void* l_pP, std::align_val_t) noexcept { f_Untrack(l_pP, true); _aligned_free(l_pP); }
void operator delete(void* l_pP, size_t, std::align_val_t) noexcept { f_Untrack(l_pP, true); _aligned_free(l_pP); }
void operator delete[](void* l_pP, size_t, std::align_val_t) noexcept { f_Untrack(l_pP, true); _aligned_free(l_pP); }
void operator delete(void* l_pP, std::align_val_t, const std::nothrow_t&) noexcept { f_Untrack(l_pP, true); _aligned_free(l_pP); }
void operator delete[](void* l_pP, std::align_val_t, const std::nothrow_t&) noexcept { f_Untrack(l_pP, true); _aligned_free(l_pP); }

namespace toms {

static long f_WriteReport(const char* l_pszReportPath);

namespace {
char g_pszReportPath[MAX_PATH] = {};
bool g_bReportRequested = false;
// Run by the C runtime after main() returned and after every function-local static made since leakCheckBegin was
// destroyed (atexit runs newest first, and this was registered first) -- so statics are gone from the report, and
// what is left is what nobody released.
void f_ReportAtExit() {
    if (g_bReportRequested) f_WriteReport(g_pszReportPath);
}
} // namespace

void f_LeakCheckBegin(long l_iBreakAlloc, const char* l_pszTraceList) {
    // _CRTDBG_ALLOC_MEM_DF: track every block. No _CRTDBG_LEAK_CHECK_DF: the CRT's own end-of-program dump would list
    // every static too, after our report; ours compares with the start instead.
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_ALLOC_MEM_DF);
    if (l_iBreakAlloc > 0) _CrtSetBreakAlloc(l_iBreakAlloc);
    for (const char* l_pszP = l_pszTraceList; l_pszP && *l_pszP && g_iTraceCount < g_iMaxTraced;) {   // "4558,348307"
        const long l_iN = std::strtol(l_pszP, nullptr, 10);
        if (l_iN > 0) g_arrTraces[g_iTraceCount++].m_iRequest = l_iN;
        l_pszP = std::strchr(l_pszP, ',');
        if (l_pszP) ++l_pszP;
    }
    f_InstallHook();   // --leak-trace and the frees share it
    {   // the new / delete map: from here on is what the report counts
        Tracker& l_oT = f_Tracker();
        f_TrackingOn(l_oT);
        AcquireSRWLockExclusive(&l_oT.m_oLock);
        l_oT.m_u64Baseline = l_oT.m_u64Seq;
        ReleaseSRWLockExclusive(&l_oT.m_oLock);
    }
    _CrtMemCheckpoint(&g_oStart);
    g_bStarted = true;
    std::atexit(f_ReportAtExit);   // before any static made from here on: it runs after they are all destroyed
}

long f_LeakCheckEnd(const char* l_pszReportPath) {
    // The report is written at exit, after the statics are destroyed (reportAtExit); here only where it goes. The path
    // is made absolute now: the working folder could change before then.
    if (!g_bStarted) return 0;
    if (!GetFullPathNameA(l_pszReportPath, MAX_PATH, g_pszReportPath, nullptr)) std::snprintf(g_pszReportPath, MAX_PATH, "%s", l_pszReportPath);
    g_bReportRequested = true;
    return 0;
}

static long f_WriteReport(const char* l_pszReportPath) {
    if (!g_bStarted) return 0;
    g_bTInside = true;   // the report's own allocations are not tracked
    _CrtMemState l_oNow, l_oDiff;
    _CrtMemCheckpoint(&l_oNow);
    const bool l_bHeapLeft = _CrtMemDifference(&l_oDiff, &g_oStart, &l_oNow) != 0;
    // Signed: written after the statics are destroyed, the heap can hold less than when main() began (statics made
    // before main are freed too), and the difference is then negative.
    const long long l_i64Blocks = l_bHeapLeft ? (long long)(l_oDiff.lCounts[_NORMAL_BLOCK] + l_oDiff.lCounts[_CLIENT_BLOCK]) : 0;
    const long long l_i64Bytes = l_bHeapLeft ? (long long)(l_oDiff.lSizes[_NORMAL_BLOCK] + l_oDiff.lSizes[_CLIENT_BLOCK]) : 0;
    HANDLE l_pFile = CreateFileA(l_pszReportPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    uint64_t l_u64NewLeft = 0;
    if (l_pFile != INVALID_HANDLE_VALUE) {
        f_WriteLine(l_pFile,
            "TOMS leak report (Windows Debug). docs/engine/15_LEAK_CHECK.md\r\n"
            "The new / delete map (where each allocation of this program was made), then a short debug-heap summary.\r\n"
            "Written at the very end: after main() returned, toms::releaseSingletons() ran and the statics were destroyed.\r\n\r\n");
        HANDLE l_pProcess = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        const bool l_bSymbols = SymInitialize(l_pProcess, nullptr, TRUE) != FALSE;
        l_u64NewLeft = f_WriteNewDeleteReport(l_pFile, l_pProcess, l_bSymbols);
        if (l_bHeapLeft) {
            // Only a summary: the debug heap cannot tell a leak from a static and lists every module's blocks (Qt's
            // caches, the statics, this checker's own map), without names -- the map above is the report. The
            // block-by-block dump (and the {N} numbers --leak-trace / --leak-break use) only with TOMS_LEAK_HEAP_DUMP=1.
            char l_pszV[8] = {};
            const bool l_bDump = GetEnvironmentVariableA("TOMS_LEAK_HEAP_DUMP", l_pszV, sizeof l_pszV) > 0 && l_pszV[0] == '1';
            char l_pszLine[512];
            std::snprintf(l_pszLine, sizeof l_pszLine,
                "==== Debug heap summary (every module: Qt, statics, this checker's own map -- not a list of leaks) ====\r\n"
                "Change since main() began: %+lld block(s), %+lld bytes (0 or less: nothing more than at the start; less:\r\n"
                "statics made before main() were freed too). Peak in use: %zu bytes.\r\n"
                "Allocated over the whole run (freed included): %zu bytes.\r\n%s",
                l_i64Blocks, l_i64Bytes, (size_t)l_oDiff.lHighWaterCount, (size_t)l_oDiff.lTotalCount,
                l_bDump ? "\r\nTOMS_LEAK_HEAP_DUMP=1: every block follows. {N} is its allocation number: --leak-trace=N[,M,...]\r\n"
                       "adds its call stack at the end, --leak-break=N stops in the debugger where it is made.\r\n\r\n"
                     : "(The block-by-block list: set TOMS_LEAK_HEAP_DUMP=1.)\r\n");
            f_WriteLine(l_pFile, l_pszLine);
            if (l_bDump) {
                _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
                _CrtSetReportFile(_CRT_WARN, l_pFile);
                _CrtMemDumpAllObjectsSince(&g_oStart);
                _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
                _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
            }
        } else {
            f_WriteLine(l_pFile, "==== Debug heap: nothing still allocated since main() began ====\r\n");
        }
        f_WriteTraces(l_pFile, l_pProcess, l_bSymbols);
        if (l_bSymbols) SymCleanup(l_pProcess);
        CloseHandle(l_pFile);
    }
    std::fprintf(stderr, "[leaks] %s: %llu allocation(s) never deleted (debug heap since start: %+lld block(s), %+lld bytes) -- see %s\n",
                 l_u64NewLeft == 0 ? "no leaks" : "LEAKS", (unsigned long long)l_u64NewLeft, l_i64Blocks, l_i64Bytes, l_pszReportPath);
    // From here (the program's statics going away) deletes are not checked any more.
    f_Tracker().m_iOn = 0;
    g_bTInside = false;
    return (long)l_u64NewLeft;
}

} // namespace toms

#else   // not a Windows Debug build: nothing to do

namespace toms {
void f_LeakCheckBegin(long, const char*) {}
long f_LeakCheckEnd(const char*) { return 0; }
} // namespace toms

#endif
