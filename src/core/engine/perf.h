// perf.h -- the performance checker (docs/engine/25_PERFORMANCE.md): how long each major update and render takes every
// frame, what the GPU drew (draw calls, triangles, its time per view), counters (particles alive, ...), the last frames'
// history; shown on screen (F4) and written to a file when the game runs slower than 30 fps for a whole second.
//
//   { toms::perf::Scope l_oScope(toms::perf::section::g_pszRenderParticles); ... }   time a block into its section
//   toms::perf::f_Count(toms::perf::counter::g_pszParticlesLive, l_iN);             add to a counter this frame
//   toms::perf::Profiler::f_Get().f_EndFrame(l_oGpu);                                the host, after bgfx::frame()
//
// Header only (the particle and anim tools build particle_fx.cpp / anim_player.cpp without the game): one registry per
// program, made on first use. Sections nest (render.particles is inside render.game): each is timed on its own.
// Main thread only.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace toms::perf {

// The sections: "update." the work of moving the game on, "render." the work of drawing it (CPU time; the GPU's own
// time is per view, below). A new one: a line here, then a Scope where it runs.
namespace section {
inline constexpr const char g_pszUpdateGame[]          = "update.game";            // Game::f_Update, all of it
inline constexpr const char g_pszUpdateMapObjects[]    = "update.map_objects";     // the floor's objects (doors, ...)
inline constexpr const char g_pszUpdateBattleRig[]     = "update.battle_rig";      // the hero's clips in a battle
inline constexpr const char g_pszUpdateMonsterRig[]    = "update.monster_rig";     // the monsters' clips in a battle
inline constexpr const char g_pszUpdateBattleFx[]      = "update.battle_fx";       // the battle's effects (their particles inside)
inline constexpr const char g_pszUpdateBackdrop[]      = "update.backdrop";        // the battle's painted backdrop
inline constexpr const char g_pszUpdateBattleLogic[]   = "update.battle_logic";    // the fight's clocks, moves, plays
inline constexpr const char g_pszUpdateParticles[]     = "update.particles";       // every particle effect (EffectInstance)
inline constexpr const char g_pszUpdateAnim[]          = "update.anim";            // every clip player (AnimPlayer)
inline constexpr const char g_pszUpdateStage3D[]       = "update.stage3d";         // the 3D stages' loading and requests
inline constexpr const char g_pszUpdateUi[]            = "update.ui";              // the UI's state and RmlUi's update, all of it
inline constexpr const char g_pszUpdateUiBuild[]       = "update.ui_build";        // the game makes the UI's state (Game::f_BuildUiState)
inline constexpr const char g_pszUpdateUiSync[]        = "update.ui_sync";         // it into the data model, the battle's boxes read back
inline constexpr const char g_pszUpdateUiRml[]         = "update.ui_rml";          // RmlUi: the bindings that changed, styles, layout
inline constexpr const char g_pszRenderGame[]          = "render.game";            // Game::f_Draw, all of it
inline constexpr const char g_pszRenderWalk3D[]        = "render.walk3d";          // the floor's objects on the 3D map
inline constexpr const char g_pszRenderBattle[]        = "render.battle";          // the fighters and the battle's effects
inline constexpr const char g_pszRenderParticles[]     = "render.particles";       // every particle effect (EffectInstance)
inline constexpr const char g_pszRenderSpriteSubmit[]  = "render.sprite_submit";   // the sprite batches to bgfx (renderer end)
inline constexpr const char g_pszRenderStageBattle[]   = "render.stage3d_battle";  // the battle's 3D stage
inline constexpr const char g_pszRenderStageWalk[]     = "render.stage3d_walk";    // the floor's 3D model
inline constexpr const char g_pszRenderStageSprites[]  = "render.stage3d_sprites"; // the sprites standing in the 3D scene
inline constexpr const char g_pszRenderTitleScene[]    = "render.title_scene";     // the title's 3D scene
inline constexpr const char g_pszRenderUi[]            = "render.ui";              // RmlUi's documents
inline constexpr const char g_pszRenderImgui[]         = "render.imgui";           // the debug windows
inline constexpr const char g_pszFramePresent[]        = "frame.present";          // bgfx::frame(): waiting for the GPU and the screen
}  // namespace section

// The counters: summed over a frame.
namespace counter {
inline constexpr const char g_pszParticlesLive[]   = "particles.live";     // particles alive in the effects drawn
inline constexpr const char g_pszParticleEffects[] = "particles.effects";  // effects drawn
inline constexpr const char g_pszParticlesGpu[]    = "particles.gpu_emitters";   // emitters run by a compute shader
inline constexpr const char g_pszAnimPlayers[]     = "anim.players";       // clip players updated
inline constexpr const char g_pszSpriteQuads[]     = "sprites.quads";      // sprites drawn by the 2D renderer
}  // namespace counter

// The GPU's view names (bgfx::setViewName), shown with each view's time.
namespace view {
inline constexpr const char g_pszClear[]    = "clear";
inline constexpr const char g_pszGame[]     = "game";
inline constexpr const char g_pszUi[]       = "ui";
inline constexpr const char g_pszTop[]      = "top";
inline constexpr const char g_pszImgui[]    = "imgui";
inline constexpr const char g_pszShadow[]   = "3d shadow";
inline constexpr const char g_pszOpaque[]   = "3d opaque";
inline constexpr const char g_pszBlended[]  = "3d blended";
inline constexpr const char g_pszCapture[]  = "capture";
inline constexpr const char g_pszLayer[]    = "layer";
}  // namespace view

// What the GPU did in a frame (bgfx::getStats; the host fills it).
struct GpuView { std::string m_strName; uint16_t m_u16Id = 0; double m_dGpuMs = 0, m_dCpuMs = 0; };
struct GpuFrame {
    double m_dGpuMs = 0;            // the whole frame on the GPU (0: not measured)
    uint32_t m_uDrawCalls = 0;      // every submit of the frame
    uint64_t m_u64Triangles = 0;    // triangle lists and strips
    uint32_t m_uComputeCalls = 0;
    std::vector<GpuView> m_vViews;  // per view (only while the GPU profiler is on: the F4 window, or a dump armed)
};

struct SectionTime { const char* m_pszName = nullptr; double m_dMs = 0; int m_iCalls = 0; };
struct CounterValue { const char* m_pszName = nullptr; int64_t m_i64Value = 0; };

// One frame, as recorded.
struct FrameRecord {
    double m_dEndS = 0;                  // when it ended (seconds since the checker started)
    double m_dFrameMs = 0;               // since the previous frame's end: what the player waited
    GpuFrame m_oGpu;
    std::vector<SectionTime> m_vSections;
    std::vector<CounterValue> m_vCounters;
};

class Profiler {
public:
    static constexpr int m_iSlowFps = 30;           // slower than this over a whole m_dSlowWindowS: a dump (the owner, 2026-11-26)
    static constexpr double m_dSlowWindowS = 1.0;
    static constexpr double m_dWarmUpS = 3.0;       // the first seconds (loading) are not judged
    static constexpr double m_dCooldownS = 30.0;    // after a dump, none for this long
    static constexpr int m_iCauseSections = 2;      // a slowdown's cause: its costliest sections (the same cause: no second dump)
    static constexpr size_t m_nHistory = 300;       // frames kept

    static Profiler& f_Get() { static Profiler l_oP; return l_oP; }

    // ---- recording (Scope, f_Count call these) ----
    void f_AddTime(const char* l_pszName, double l_dMs) {
        SectionTime& l_oS = f_Slot(m_vNow, l_pszName);
        l_oS.m_dMs += l_dMs;
        l_oS.m_iCalls++;
    }
    void f_AddCount(const char* l_pszName, int64_t l_i64N) {
        for (CounterValue& l_oC : m_vCountNow) if (l_oC.m_pszName == l_pszName) { l_oC.m_i64Value += l_i64N; return; }
        m_vCountNow.push_back({l_pszName, l_i64N});
    }

    // ---- the host, once a frame after bgfx::frame() ----
    void f_EndFrame(const GpuFrame& l_oGpu) {
        const double l_dNow = f_NowS();
        FrameRecord l_oR;
        l_oR.m_dEndS = l_dNow;
        l_oR.m_dFrameMs = m_vHistory.empty() ? 0.0 : (l_dNow - m_vHistory.back().m_dEndS) * 1000.0;
        l_oR.m_oGpu = l_oGpu;
        l_oR.m_vSections = m_vNow;
        l_oR.m_vCounters = m_vCountNow;
        for (SectionTime& l_oS : m_vNow) { l_oS.m_dMs = 0; l_oS.m_iCalls = 0; }
        m_vCountNow.clear();
        m_vHistory.push_back(std::move(l_oR));
        if (m_vHistory.size() > m_nHistory) m_vHistory.pop_front();
        f_CheckSlow(l_dNow);
    }

    // ---- settings ----
    void f_SetAutoDump(bool l_bOn) { m_bAutoDump = l_bOn; }
    bool f_AutoDump() const { return m_bAutoDump; }
    void f_SetDumpDir(const std::string& l_strDir) { m_strDumpDir = l_strDir; }
    void f_SetDumpAtExit(bool l_bOn) { m_bDumpAtExit = l_bOn; }   // --perf-dump=exit: the last 2 s when the run ends
    bool f_DumpAtExit() const { return m_bDumpAtExit; }
    void f_SetContext(const std::string& l_strText) { m_strContext = l_strText; }   // "battle on F30", the renderer, ...
    bool f_WantGpuViews() const { return m_bShown || m_bAutoDump; }   // the host turns bgfx's profiler on for these
    void f_SetShown(bool l_bOn) { m_bShown = l_bOn; }
    bool f_Shown() const { return m_bShown; }

    // ---- reading ----
    const std::deque<FrameRecord>& f_History() const { return m_vHistory; }
    const std::string& f_LastDumpPath() const { return m_strLastDump; }
    int f_DumpCount() const { return m_iDumps; }

    // The frames of the last l_dSeconds (newest last).
    std::vector<const FrameRecord*> f_Recent(double l_dSeconds) const {
        std::vector<const FrameRecord*> l_vOut;
        if (m_vHistory.empty()) return l_vOut;
        const double l_dFrom = m_vHistory.back().m_dEndS - l_dSeconds;
        for (const FrameRecord& l_oR : m_vHistory) if (l_oR.m_dEndS > l_dFrom) l_vOut.push_back(&l_oR);
        return l_vOut;
    }

    // Each section's average and worst over some frames, the costliest first.
    struct SectionSummary { const char* m_pszName = nullptr; double m_dAvgMs = 0, m_dMaxMs = 0, m_dCallsPerFrame = 0; };
    static std::vector<SectionSummary> f_Summarize(const std::vector<const FrameRecord*>& l_vFrames) {
        std::vector<SectionSummary> l_vOut;
        if (l_vFrames.empty()) return l_vOut;
        for (const FrameRecord* l_pR : l_vFrames)
            for (const SectionTime& l_oS : l_pR->m_vSections) {
                if (l_oS.m_iCalls == 0) continue;
                auto l_itIt = std::find_if(l_vOut.begin(), l_vOut.end(), [&](const SectionSummary& l_oX) { return l_oX.m_pszName == l_oS.m_pszName; });
                if (l_itIt == l_vOut.end()) { l_vOut.push_back({l_oS.m_pszName}); l_itIt = l_vOut.end() - 1; }
                l_itIt->m_dAvgMs += l_oS.m_dMs;
                l_itIt->m_dMaxMs = std::max(l_itIt->m_dMaxMs, l_oS.m_dMs);
                l_itIt->m_dCallsPerFrame += l_oS.m_iCalls;
            }
        for (SectionSummary& l_oS : l_vOut) { l_oS.m_dAvgMs /= (double)l_vFrames.size(); l_oS.m_dCallsPerFrame /= (double)l_vFrames.size(); }
        std::sort(l_vOut.begin(), l_vOut.end(), [](const SectionSummary& l_oA, const SectionSummary& l_oB) { return l_oA.m_dAvgMs > l_oB.m_dAvgMs; });
        return l_vOut;
    }
    struct CounterSummary { const char* m_pszName = nullptr; double m_dAvg = 0; int64_t m_i64Max = 0; };
    static std::vector<CounterSummary> f_SummarizeCounters(const std::vector<const FrameRecord*>& l_vFrames) {
        std::vector<CounterSummary> l_vOut;
        if (l_vFrames.empty()) return l_vOut;
        for (const FrameRecord* l_pR : l_vFrames)
            for (const CounterValue& l_oC : l_pR->m_vCounters) {
                auto l_itIt = std::find_if(l_vOut.begin(), l_vOut.end(), [&](const CounterSummary& l_oX) { return l_oX.m_pszName == l_oC.m_pszName; });
                if (l_itIt == l_vOut.end()) { l_vOut.push_back({l_oC.m_pszName}); l_itIt = l_vOut.end() - 1; }
                l_itIt->m_dAvg += (double)l_oC.m_i64Value;
                l_itIt->m_i64Max = std::max(l_itIt->m_i64Max, l_oC.m_i64Value);
            }
        for (CounterSummary& l_oC : l_vOut) l_oC.m_dAvg /= (double)l_vFrames.size();
        return l_vOut;
    }

    // Writes the last l_dSeconds to a file in the dump folder; its path ("" when it could not be written).
    std::string f_Dump(const char* l_pszWhy, double l_dSeconds = 2.0) {
        const std::vector<const FrameRecord*> l_vFrames = f_Recent(l_dSeconds);
        std::error_code l_oEc;
        std::filesystem::create_directories(m_strDumpDir, l_oEc);
        const std::time_t l_tNow = std::time(nullptr);
        std::tm l_oTm{};
#if defined(_WIN32)
        localtime_s(&l_oTm, &l_tNow);
#else
        localtime_r(&l_tNow, &l_oTm);
#endif
        char l_pszStamp[64];
        std::strftime(l_pszStamp, sizeof l_pszStamp, "%Y%m%d_%H%M%S", &l_oTm);
        const std::string l_strPath = m_strDumpDir + "/perf_" + l_pszStamp + "_" + std::to_string(++m_iDumps) + ".txt";
        std::FILE* l_pF = std::fopen(l_strPath.c_str(), "w");
        if (!l_pF) return std::string();
        std::fprintf(l_pF, "TOMS performance dump (docs/engine/25_PERFORMANCE.md)\nwhy: %s\n", l_pszWhy);
        if (!m_strContext.empty()) std::fprintf(l_pF, "where: %s\n", m_strContext.c_str());
        double l_dSum = 0, l_dWorst = 0;
        for (const FrameRecord* l_pR : l_vFrames) { l_dSum += l_pR->m_dFrameMs; l_dWorst = std::max(l_dWorst, l_pR->m_dFrameMs); }
        if (!l_vFrames.empty())
            std::fprintf(l_pF, "the last %.1f s: %zu frames, %.1f fps, %.1f ms average, worst %.1f ms\n\n", l_dSeconds, l_vFrames.size(),
                         l_dSum > 0 ? 1000.0 * (double)l_vFrames.size() / l_dSum : 0.0, l_dSum / (double)l_vFrames.size(), l_dWorst);
        std::fprintf(l_pF, "== sections (CPU ms a frame; they nest: render.particles is inside render.game) ==\n");
        std::fprintf(l_pF, "%-26s %9s %9s %8s\n", "section", "avg ms", "max ms", "calls");
        for (const SectionSummary& l_oS : f_Summarize(l_vFrames))
            std::fprintf(l_pF, "%-26s %9.3f %9.3f %8.1f\n", l_oS.m_pszName, l_oS.m_dAvgMs, l_oS.m_dMaxMs, l_oS.m_dCallsPerFrame);
        std::fprintf(l_pF, "\n== counters (a frame) ==\n%-26s %9s %9s\n", "counter", "avg", "max");
        for (const CounterSummary& l_oC : f_SummarizeCounters(l_vFrames))
            std::fprintf(l_pF, "%-26s %9.1f %9lld\n", l_oC.m_pszName, l_oC.m_dAvg, (long long)l_oC.m_i64Max);
        if (!l_vFrames.empty() && !l_vFrames.back()->m_oGpu.m_vViews.empty()) {
            std::fprintf(l_pF, "\n== GPU views (the last frame) ==\n%-4s %-16s %9s %9s\n", "id", "view", "gpu ms", "cpu ms");
            for (const GpuView& l_oV : l_vFrames.back()->m_oGpu.m_vViews)
                std::fprintf(l_pF, "%-4u %-16s %9.3f %9.3f\n", (unsigned)l_oV.m_u16Id, l_oV.m_strName.c_str(), l_oV.m_dGpuMs, l_oV.m_dCpuMs);
        }
        std::fprintf(l_pF, "\n== frames (oldest first) ==\n%9s %9s %8s %11s  %s\n", "frame ms", "gpu ms", "draws", "triangles", "the three costliest sections");
        for (const FrameRecord* l_pR : l_vFrames) {
            std::vector<SectionTime> l_vTop = l_pR->m_vSections;
            std::sort(l_vTop.begin(), l_vTop.end(), [](const SectionTime& l_oA, const SectionTime& l_oB) { return l_oA.m_dMs > l_oB.m_dMs; });
            std::string l_strTop;
            for (size_t l_nI = 0; l_nI < l_vTop.size() && l_nI < 3; l_nI++) {
                if (l_vTop[l_nI].m_iCalls == 0) break;
                char l_pszOne[80];
                std::snprintf(l_pszOne, sizeof l_pszOne, "%s%s %.2f", l_strTop.empty() ? "" : ", ", l_vTop[l_nI].m_pszName, l_vTop[l_nI].m_dMs);
                l_strTop += l_pszOne;
            }
            std::fprintf(l_pF, "%9.2f %9.2f %8u %11llu  %s\n", l_pR->m_dFrameMs, l_pR->m_oGpu.m_dGpuMs, l_pR->m_oGpu.m_uDrawCalls,
                         (unsigned long long)l_pR->m_oGpu.m_u64Triangles, l_strTop.c_str());
        }
        std::fclose(l_pF);
        m_strLastDump = l_strPath;
        return l_strPath;
    }

    static double f_NowS() {
        static const auto l_oStart = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - l_oStart).count();
    }

private:
    Profiler() = default;

    static SectionTime& f_Slot(std::vector<SectionTime>& l_vList, const char* l_pszName) {
        for (SectionTime& l_oS : l_vList) if (l_oS.m_pszName == l_pszName) return l_oS;
        l_vList.push_back({l_pszName, 0.0, 0});
        return l_vList.back();
    }

    // Slower than m_iSlowFps over a whole second (a single frame of more than a second counts too): one dump, then
    // none for m_dCooldownS, and none until it has run at speed again. A slowdown with the same cause as one already
    // dumped (its m_iCauseSections costliest sections; frame.present, the wait for the screen, left out) is not dumped
    // again: it would say nothing new.
    void f_CheckSlow(double l_dNow) {
        if (!m_bAutoDump || l_dNow < m_dWarmUpS || m_vHistory.empty() || m_vHistory.front().m_dEndS > l_dNow - m_dSlowWindowS) return;
        int l_iFrames = 0;
        for (auto l_itIt = m_vHistory.rbegin(); l_itIt != m_vHistory.rend() && l_itIt->m_dEndS > l_dNow - m_dSlowWindowS; ++l_itIt) l_iFrames++;
        const bool l_bSlow = l_iFrames < m_iSlowFps || (m_vHistory.back().m_dFrameMs > m_dSlowWindowS * 1000.0);
        if (!l_bSlow) { m_bArmed = true; return; }
        if (!m_bArmed || l_dNow - m_dLastDumpS < m_dCooldownS) return;
        const std::string l_strCause = f_Cause(f_Recent(m_dSlowWindowS));
        m_bArmed = false;   // this slowdown is judged: the next one starts once it runs at speed again
        if (std::find(m_vCauses.begin(), m_vCauses.end(), l_strCause) != m_vCauses.end()) {
            std::fprintf(stderr, "[perf] slower than %d fps again (%s): the same cause as an earlier dump, none written\n", m_iSlowFps, l_strCause.c_str());
            return;
        }
        m_vCauses.push_back(l_strCause);
        m_dLastDumpS = l_dNow;
        char l_pszWhy[256];
        std::snprintf(l_pszWhy, sizeof l_pszWhy, "slower than %d fps: %d frame(s) in the last %.1f s; the costliest: %s", m_iSlowFps, l_iFrames,
                      m_dSlowWindowS, l_strCause.c_str());
        const std::string l_strPath = f_Dump(l_pszWhy, 2.0);
        std::fprintf(stderr, "[perf] %s: %s\n", l_pszWhy, l_strPath.empty() ? "(the dump could not be written)" : l_strPath.c_str());
    }

    // A slowdown's cause: its costliest sections by name, "render.walk3d + update.ui".
    static std::string f_Cause(const std::vector<const FrameRecord*>& l_vFrames) {
        std::string l_strOut;
        int l_iN = 0;
        for (const SectionSummary& l_oS : f_Summarize(l_vFrames)) {
            if (std::strcmp(l_oS.m_pszName, section::g_pszFramePresent) == 0) continue;
            l_strOut += (l_strOut.empty() ? "" : " + ") + std::string(l_oS.m_pszName);
            if (++l_iN == m_iCauseSections) break;
        }
        return l_strOut.empty() ? std::string(section::g_pszFramePresent) : l_strOut;
    }

    std::vector<SectionTime> m_vNow;      // this frame so far
    std::vector<CounterValue> m_vCountNow;
    std::deque<FrameRecord> m_vHistory;
    bool m_bAutoDump = true, m_bArmed = true, m_bShown = false, m_bDumpAtExit = false;
    double m_dLastDumpS = -1e9;
    std::vector<std::string> m_vCauses;   // the causes already dumped this run
    int m_iDumps = 0;
    std::string m_strDumpDir = "perf_dumps", m_strContext, m_strLastDump;
};

// Times the block it lives in into its section.
class Scope {
public:
    explicit Scope(const char* l_pszSection) : m_pszSection(l_pszSection), m_oT0(std::chrono::steady_clock::now()) {}
    ~Scope() {
        Profiler::f_Get().f_AddTime(m_pszSection, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_oT0).count());
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    const char* m_pszSection;
    std::chrono::steady_clock::time_point m_oT0;
};

inline void f_Count(const char* l_pszCounter, int64_t l_i64N) { Profiler::f_Get().f_AddCount(l_pszCounter, l_i64N); }

}  // namespace toms::perf
