// main_sdl.cpp -- toms_game: the shipped game (SDL3 window + bgfx). No Qt here.
// One source for both targets:
//   desktop  a normal while-loop around appFrame()
//   web      (Emscripten) the browser drives appFrame() through requestAnimationFrame; bgfx runs on
//            WebGL2; saves persist in IndexedDB (IDBFS at /save); page options come from the URL.
//
// Options (desktop: command line; web: URL query, e.g. toms_game.html?stage=stage03&keys=enter@60):
//   --renderer=auto|d3d11|d3d12|vulkan|opengl   pick the bgfx backend (desktop; default: auto)
//   --assets=<dir>        the TOMS assets folder (default: env ASSET_DIR, else ../assets of the repo)
//   --stage=<id>          first stage to load (default stage01)
//   --no-vsync            uncapped frame rate (desktop)
//   --stats               bgfx's on-screen stats overlay
//   --sprite-path=<p>     how the sprite batch makes its vertices: auto (default: instancing, else compute,
//                         else cpu) | instancing | compute | cpu (bgfx_renderer.h); --no-instancing = cpu
//   --fps                 start with the F3 performance line on (FPS, backend, GPU / CPU sprite path)
//   --fx-gpu-threshold=N  particle emitters above N particles run on the GPU (0 = never); this run only,
//                         the saved setting (particleGpuThreshold: desktop 5000, phones 3000) is unchanged
//   --title-scene=<f|none> the 3D scene behind the title: a .glb / .gltf, or none (default:
//                         assets/media/models/VirtualCity.glb when it is there; docs/engine/08_GLTF.md)
//   --frames=<n>          quit after n frames (automated tests; desktop)
//   --screenshot=<png>    save the last frame to a PNG (with --frames; desktop). The window stays hidden.
//   --record=<folder>     save frames as <folder>/frame_00000.png, ... (desktop; the window stays hidden): every frame
//                         from --record-from=<n> (default 0), or every --record-step=<n>th; with --frames and
//                         --fixed-dt, a clip or a battle as pictures, merged by tools/frames_to_gif.py
//   --keys=<k@f,...>      press key k at frame f, e.g. enter@30,enter@60 (automated tests)
//   --clicks=<x:y@f,...>  left-click at design point (x,y) (1024x768 space) at frame f (tests)
//   --fixed-dt=<ms>       every frame advances exactly this long, so a scripted run is identical
//                         every time (screenshot smoke tests, tests/CMakeLists.txt)
//   --anim=<file>#<clip>  play one clip of an .anim file over the screen (anim_clip.h; tests)
//   --ui-scale=<x>        the small-screen UI scale (the web build uses 1.5 on phones), to test it on desktop
//   --battle-tuning=default  the battle bars' built-in values instead of data/battle.json (tests: smoke.battle)
//   --battle-log          each battle event (hits, falls, the target, the win) as a line on stderr
//   --window=<W>x<H>      the window's size (default 1280x720; e.g. 540x1200 to see the portrait layout on desktop)
//   --resize=<frame>:<W>x<H>  resize the window at that frame (tests: the layout switches while the game runs)
//   --design=<W>x<H>      a fixed design canvas, letterboxed (canvas.h; the screenshot tests pin 1024x768). Without it
//                         the canvas follows the window's shape, landscape or portrait
//   --fx=<file>#<effect>  play one particle effect of a .particle file over the screen (particle_fx.h)
//   --mesh-test=<sprite>  draw that game-atlas sprite as a waving triangle mesh beside the plain sprite (IRenderer::drawMesh)
//   --anim-crowd=<N>      with --anim: the clip N times in a grid, each its own AnimPlayer (a benchmark)
//   --bench               every 120 frames, the average frame CPU time, the clips' draw time and the draw calls (stderr)
//   --character=<sprite>  with --anim: play a character clip (body / hand nodes) as this character (fitCharacter)
//   --held=<file.anim>    with --anim: draw that file's clip of the same name at the character clip's hand (CharacterRig)
//   --hot-reload          read changed art, clips and effects again while playing (on by default in debug builds)
//   --skip-start-story    New Game goes straight to stage 1, skipping the weapon master (docs/gameplay/07 phase 3; the screenshot tests)
//   --give=<id,...>       when the title first closes, hand the player these items (data/items.json ids;
//                         gear is owned, not worn), gold:<n>, exp:<n>, points:<n> -- tests of the player menu (tests/CMakeLists.txt)
//   --stair-lock=<floor>:<event>  lock that floor's first stair up until the event is finished (locked stairs, Q7)
//   --events-done=<id,...>        when the title first closes, finish these events as the player would (tests)
//   --goto=<floor>                when the title first closes, go to that floor (e.g. F04; tests)
//   --scenario=<folder|file>      start straight in one scene from a scenario file (docs/gameplay/08_TEST_SCENARIOS.md;
//                                 a folder means its scenario.json). Nothing is saved.
//   --arena=<enemy,...>[:<weapon>]  the weapon test arena: fight these enemies in turn, again and again, with this
//                                 weapon (an items.json id; none = unarmed). No rewards, nothing saved (docs/gameplay/08)
//   --leak-trace=<n,m,...>        Windows Debug builds: add the call stacks of heap allocations n, m (the {n} of
//                                 toms_leaks.log entries) to the report (docs/engine/15_LEAK_CHECK.md)
//   --leak-break=<n>              Windows Debug builds: stop in the debugger at heap allocation n
//   --scenario-test               also run the scenario's "test" frames and keys, then exit 3 when the scene it
//                                 expects is not showing (ctest scenario.*)
#include "bgfx_host.h"
#include "bgfx_renderer.h"
#include "../../core/engine/vfs.h"   // toms::vfsInit: APK entries need the AAssetManager
#if defined(__ANDROID__)
#  include <jni.h>
#  include <android/asset_manager_jni.h>
#  include <SDL3/SDL_system.h>      // SDL_GetAndroidJNIEnv / SDL_GetAndroidActivity
#  include <SDL3/SDL_filesystem.h>  // SDL_GetPrefPath (app-private save dir)
#  include <android/log.h>
#  include <unistd.h>
#  include <thread>
#endif
#include "game_session.h"
#include "leak_check.h"
#include "singletons.h"    // releaseSingletons() at the end of main    // Windows Debug builds: what is still allocated at exit (docs/engine/15)
#include "game.h"
#include "job_system.h"
#include "object.h"

#if !defined(__ANDROID__)
#define SDL_MAIN_HANDLED        // keep our own main(); SDL_SetMainReady() below tells SDL
#endif                          // Android: SDL_main.h renames main() to SDL_main, which SDLActivity calls
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

using namespace toms::next;

namespace {

struct Args {
    std::string m_strRenderer = "auto", m_strAssets, m_strStage = "stage01", m_strScreenshot, m_strAnim, m_strFx, m_strMeshTest, m_strCharacter, m_strHeld;
    int m_iFrames = 0;
    std::string m_strRecord;                // --record=<folder>
    int m_iRecordFrom = 0, m_iRecordStep = 1;   // --record-from=<n>, --record-step=<n>
    int m_iAnimCrowd = 0;                 // --anim-crowd=N
    bool m_bBench = false;                // --bench
    bool m_bVsync = true, m_bStats = false, m_bFps = false;
    bool m_bHotReload = false;   // --hot-reload
    bool m_bSkipStartStory = false;   // --skip-start-story: New Game goes straight to stage 1 (the screenshot tests)
    std::string m_strSpritePath = "auto";
    int m_iFxGpuThreshold = -1;
    bool m_bDefaultBattleTuning = false;  // --battle-tuning=default
    bool m_bBattleLog = false;            // --battle-log
    std::string m_strTitleScene;            // --title-scene=<file|none>
    float m_fUiScale = 0.0f;              // --ui-scale=<x>: the phone UI scale on desktop (testing the web build's small-screen layout)
    int m_iWindowW = 1280, m_iWindowH = 720;   // --window=WxH
    int m_iResizeFrame = -1, m_iResizeW = 0, m_iResizeH = 0;   // --resize=frame:WxH
    uint32_t m_uDesignW = 0, m_uDesignH = 0;   // --design=WxH (0: follow the window)
    int m_iFixedDtMs = 0;       // > 0: deterministic frame time (tests)
    struct Press { Key m_eKey; int m_iFrame; };
    std::vector<Press> m_vPresses;
    struct Click { float m_fX, m_fY; int m_iFrame; };
    std::vector<Click> m_vClicks;
    std::vector<std::string> m_vGive;     // --give=<id,...>
    std::string m_strStairLock;             // --stair-lock=<floor>:<event>
    std::vector<std::string> m_vEventsDone;   // --events-done=<id,...>
    std::string m_strGotoFloor;             // --goto=<floor>: after the title closes, go to that floor (tests)
    std::string m_strScenario;              // --scenario=<folder|file>
    std::string m_strArena;                 // --arena=<enemy,...>[:<weapon>]
    bool m_bScenarioTest = false;         // --scenario-test
};

bool f_KeyFromName(const std::string& l_strN, Key& l_eOut) {
    static const struct { const char* m_pszName; Key m_eKey; } l_arrTable[] = {
        {"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right},
        {"enter", Key::Enter}, {"space", Key::Space}, {"esc", Key::Escape}, {"tab", Key::Tab},
        {"f1", Key::F1}, {"f2", Key::F2}, {"f5", Key::F5}, {"f8", Key::F8},
        {"i", Key::I}, {"b", Key::B}, {"c", Key::C}, {"q", Key::Q}, {"e", Key::E},
        {"f", Key::F}, {"g", Key::G}, {"h", Key::H},   // the battle: guard, super, the active
        {"1", Key::Num1}, {"2", Key::Num2}, {"3", Key::Num3},
    };
    for (auto& l_oE : l_arrTable) if (l_strN == l_oE.m_pszName) { l_eOut = l_oE.m_eKey; return true; }
    return false;
}

void f_ParseKeys(const std::string& l_strList, Args& l_oA) {
    size_t l_nPos = 0;
    while (l_nPos < l_strList.size()) {
        size_t l_nComma = l_strList.find(',', l_nPos);
        std::string l_strItem = l_strList.substr(l_nPos, l_nComma == std::string::npos ? std::string::npos : l_nComma - l_nPos);
        size_t l_nAt = l_strItem.find('@');
        Key l_eK;
        if (l_nAt != std::string::npos && f_KeyFromName(l_strItem.substr(0, l_nAt), l_eK))
            l_oA.m_vPresses.push_back({l_eK, std::atoi(l_strItem.c_str() + l_nAt + 1)});
        if (l_nComma == std::string::npos) break;
        l_nPos = l_nComma + 1;
    }
}

void f_ParseClicks(const std::string& l_strList, Args& l_oA) {
    size_t l_nPos = 0;
    while (l_nPos < l_strList.size()) {
        size_t l_nComma = l_strList.find(',', l_nPos);
        std::string l_strItem = l_strList.substr(l_nPos, l_nComma == std::string::npos ? std::string::npos : l_nComma - l_nPos);
        float l_fX = 0, l_fY = 0; int l_iF = 0;
        if (std::sscanf(l_strItem.c_str(), "%f:%f@%d", &l_fX, &l_fY, &l_iF) == 3) l_oA.m_vClicks.push_back({l_fX, l_fY, l_iF});
        if (l_nComma == std::string::npos) break;
        l_nPos = l_nComma + 1;
    }
}

Args f_ParseArgs(const std::vector<std::string>& l_vArgv) {
    Args l_oA;
    auto l_fnValue = [](const std::string& l_strArg, const char* l_pszPrefix) -> const char* {
        size_t l_nN = std::strlen(l_pszPrefix);
        return l_strArg.compare(0, l_nN, l_pszPrefix) == 0 ? l_strArg.c_str() + l_nN : nullptr;
    };
    for (const std::string& l_strS : l_vArgv) {
        if (const char* l_pszV = l_fnValue(l_strS, "--renderer="))   l_oA.m_strRenderer = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--assets=")) l_oA.m_strAssets = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--stage="))  l_oA.m_strStage = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--frames=")) l_oA.m_iFrames = std::atoi(l_pszV);
        else if (const char* l_pszV = l_fnValue(l_strS, "--screenshot=")) l_oA.m_strScreenshot = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--record=")) l_oA.m_strRecord = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--record-from=")) l_oA.m_iRecordFrom = std::max(0, std::atoi(l_pszV));
        else if (const char* l_pszV = l_fnValue(l_strS, "--record-step=")) l_oA.m_iRecordStep = std::max(1, std::atoi(l_pszV));
        else if (const char* l_pszV = l_fnValue(l_strS, "--keys=")) f_ParseKeys(l_pszV, l_oA);
        else if (const char* l_pszV = l_fnValue(l_strS, "--clicks=")) f_ParseClicks(l_pszV, l_oA);
        else if (const char* l_pszV = l_fnValue(l_strS, "--fixed-dt=")) l_oA.m_iFixedDtMs = std::atoi(l_pszV);
        else if (const char* l_pszV = l_fnValue(l_strS, "--anim=")) l_oA.m_strAnim = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--anim-crowd=")) l_oA.m_iAnimCrowd = std::atoi(l_pszV);
        else if (l_strS == "--bench") l_oA.m_bBench = true;
        else if (const char* l_pszV = l_fnValue(l_strS, "--fx=")) l_oA.m_strFx = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--mesh-test=")) l_oA.m_strMeshTest = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--character=")) l_oA.m_strCharacter = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--held=")) l_oA.m_strHeld = l_pszV;
        else if (l_strS == "--no-vsync") l_oA.m_bVsync = false;
        else if (l_strS == "--stats") l_oA.m_bStats = true;
        else if (l_strS == "--no-instancing") l_oA.m_strSpritePath = "cpu";
        else if (const char* l_pszV = l_fnValue(l_strS, "--sprite-path=")) l_oA.m_strSpritePath = l_pszV;
        else if (l_strS == "--fps") l_oA.m_bFps = true;
        else if (l_strS == "--hot-reload") l_oA.m_bHotReload = true;
        else if (l_strS == "--skip-start-story") l_oA.m_bSkipStartStory = true;
        else if (const char* l_pszV = l_fnValue(l_strS, "--fx-gpu-threshold=")) l_oA.m_iFxGpuThreshold = std::max(0, std::atoi(l_pszV));
        else if (const char* l_pszV = l_fnValue(l_strS, "--title-scene=")) l_oA.m_strTitleScene = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--ui-scale=")) l_oA.m_fUiScale = (float)std::atof(l_pszV);
        else if (const char* l_pszV = l_fnValue(l_strS, "--resize=")) { int l_iF = 0, l_iW = 0, l_iH = 0; if (std::sscanf(l_pszV, "%d:%dx%d", &l_iF, &l_iW, &l_iH) == 3 && l_iW >= 200 && l_iH >= 200) { l_oA.m_iResizeFrame = l_iF; l_oA.m_iResizeW = l_iW; l_oA.m_iResizeH = l_iH; } }
        else if (const char* l_pszV = l_fnValue(l_strS, "--window=")) { int l_iW = 0, l_iH = 0; if (std::sscanf(l_pszV, "%dx%d", &l_iW, &l_iH) == 2 && l_iW >= 200 && l_iH >= 200) { l_oA.m_iWindowW = l_iW; l_oA.m_iWindowH = l_iH; } }
        else if (const char* l_pszV = l_fnValue(l_strS, "--design=")) { unsigned l_uW = 0, l_uH = 0; if (std::sscanf(l_pszV, "%ux%u", &l_uW, &l_uH) == 2 && l_uW >= 320 && l_uH >= 320) { l_oA.m_uDesignW = l_uW; l_oA.m_uDesignH = l_uH; } }
        else if (const char* l_pszV = l_fnValue(l_strS, "--battle-tuning=")) l_oA.m_bDefaultBattleTuning = std::string(l_pszV) == "default";
        else if (l_strS == "--battle-log") l_oA.m_bBattleLog = true;
        else if (const char* l_pszV = l_fnValue(l_strS, "--stair-lock=")) l_oA.m_strStairLock = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--goto=")) l_oA.m_strGotoFloor = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--scenario=")) l_oA.m_strScenario = l_pszV;
        else if (const char* l_pszV = l_fnValue(l_strS, "--arena=")) l_oA.m_strArena = l_pszV;
        else if (l_strS == "--scenario-test") l_oA.m_bScenarioTest = true;
        else if (const char* l_pszV = l_fnValue(l_strS, "--events-done=")) {
            std::string l_strList = l_pszV;
            for (size_t l_nPos = 0; l_nPos <= l_strList.size();) {
                const size_t l_nComma = std::min(l_strList.find(',', l_nPos), l_strList.size());
                if (l_nComma > l_nPos) l_oA.m_vEventsDone.push_back(l_strList.substr(l_nPos, l_nComma - l_nPos));
                l_nPos = l_nComma + 1;
            }
        }
        else if (const char* l_pszV = l_fnValue(l_strS, "--give=")) {
            std::string l_strList = l_pszV;
            for (size_t l_nPos = 0; l_nPos <= l_strList.size();) {
                const size_t l_nComma = std::min(l_strList.find(',', l_nPos), l_strList.size());
                if (l_nComma > l_nPos) l_oA.m_vGive.push_back(l_strList.substr(l_nPos, l_nComma - l_nPos));
                l_nPos = l_nComma + 1;
            }
        }
    }
    return l_oA;
}

#ifdef __EMSCRIPTEN__
// ?stage=stage03&keys=enter@60&stats -> {"--stage=stage03", "--keys=enter@60", "--stats"}
std::vector<std::string> f_UrlArgs() {
    char* l_pszQ = (char*)EM_ASM_PTR({
        // No regex here: EM_ASM turns this into a C string, so backslash escapes would be lost.
        var s = (typeof location !== 'undefined' && location.search) ? location.search.substring(1) : '';
        return stringToNewUTF8(s);
    });
    std::string l_strQuery = l_pszQ ? l_pszQ : "";
    free(l_pszQ);
    std::vector<std::string> l_vOut;
    size_t l_nPos = 0;
    while (l_nPos < l_strQuery.size()) {
        size_t l_nAmp = l_strQuery.find('&', l_nPos);
        std::string l_strKv = l_strQuery.substr(l_nPos, l_nAmp == std::string::npos ? std::string::npos : l_nAmp - l_nPos);
        if (!l_strKv.empty()) l_vOut.push_back("--" + l_strKv);
        if (l_nAmp == std::string::npos) break;
        l_nPos = l_nAmp + 1;
    }
    return l_vOut;
}
#endif

void f_FatalBox(SDL_Window* l_pWindow, const std::string& l_strTitle, const std::string& l_strText) {
    std::fprintf(stderr, "[toms] %s\n%s\n", l_strTitle.c_str(), l_strText.c_str());
#ifdef __EMSCRIPTEN__
    (void)l_pWindow;
    EM_ASM({ if (typeof tomsShowError === 'function') tomsShowError(UTF8ToString($0), UTF8ToString($1)); },
           l_strTitle.c_str(), l_strText.c_str());
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, l_strTitle.c_str(), l_strText.c_str(), l_pWindow);
#endif
}

// `pressed`: keys that went down during this frame's events. A key pressed AND released between two
// frames (a quick tap, adb input, some on-screen keyboards) is not down in SDL_GetKeyboardState any
// more, but still counts as down for one frame.
void f_ReadKeyboard(InputState& l_oIn, const bool* l_pPressed) {
    const bool* l_pSdlKeys = SDL_GetKeyboardState(nullptr);
    bool l_arrKs[SDL_SCANCODE_COUNT];
    for (int l_iI = 0; l_iI < SDL_SCANCODE_COUNT; ++l_iI) l_arrKs[l_iI] = l_pSdlKeys[l_iI] || l_pPressed[l_iI];
    l_arrKs[SDL_SCANCODE_ESCAPE] = l_arrKs[SDL_SCANCODE_ESCAPE] || l_arrKs[SDL_SCANCODE_AC_BACK];   // Android's Back
    auto l_fnSet = [&](Key l_eK, SDL_Scancode l_eSc) { l_oIn.m_arrDown[(size_t)l_eK] = l_arrKs[l_eSc]; };
    l_fnSet(Key::Up, SDL_SCANCODE_UP);       l_fnSet(Key::Down, SDL_SCANCODE_DOWN);
    l_fnSet(Key::Left, SDL_SCANCODE_LEFT);   l_fnSet(Key::Right, SDL_SCANCODE_RIGHT);
    l_fnSet(Key::W, SDL_SCANCODE_W); l_fnSet(Key::A, SDL_SCANCODE_A); l_fnSet(Key::S, SDL_SCANCODE_S); l_fnSet(Key::D, SDL_SCANCODE_D);
    l_oIn.m_arrDown[(size_t)Key::Enter] = l_arrKs[SDL_SCANCODE_RETURN] || l_arrKs[SDL_SCANCODE_KP_ENTER];
    l_fnSet(Key::Space, SDL_SCANCODE_SPACE); l_fnSet(Key::Escape, SDL_SCANCODE_ESCAPE); l_fnSet(Key::Tab, SDL_SCANCODE_TAB);
    l_fnSet(Key::F1, SDL_SCANCODE_F1); l_fnSet(Key::F2, SDL_SCANCODE_F2); l_fnSet(Key::F3, SDL_SCANCODE_F3);
    l_fnSet(Key::F5, SDL_SCANCODE_F5); l_fnSet(Key::F8, SDL_SCANCODE_F8);
    l_fnSet(Key::F, SDL_SCANCODE_F); l_fnSet(Key::G, SDL_SCANCODE_G); l_fnSet(Key::H, SDL_SCANCODE_H);
    l_fnSet(Key::I, SDL_SCANCODE_I); l_fnSet(Key::B, SDL_SCANCODE_B);
    l_fnSet(Key::C, SDL_SCANCODE_C); l_fnSet(Key::Q, SDL_SCANCODE_Q); l_fnSet(Key::E, SDL_SCANCODE_E);
    const SDL_Scancode l_arrNums[9] = {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
                                  SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9};
    for (int l_iI = 0; l_iI < 9; ++l_iI) l_oIn.m_arrDown[(size_t)Key::Num1 + l_iI] = l_arrKs[l_arrNums[l_iI]];
}

// Everything one running game needs. Heap-allocated: on the web, main() returns while the
// browser keeps calling appFrame(), so nothing may live on main()'s stack.
struct App {
    bool m_bSuspended = false;     // Android: set while backgrounded, so we do not render a suspended app
    bool m_bResumed = false;       // Android: back in the foreground -- the surface is a new one
    void* m_pNativeWindow = nullptr;   // the window handle bgfx draws on
    Args m_oArgs;
    SDL_Window* m_pWindow = nullptr;
    GameSession m_oSession;
    InputState m_oIn;
    uint64_t m_u64LastTicks = 0;
    int m_iFrameNo = 0;
    bool m_bGave = false;          // --give applied
    bool m_bScenarioStarted = false;   // --scenario applied
    int m_iExitCode = 0;           // 3: --scenario-test found the scene it expected not showing
    int m_iBackW = 0, m_iBackH = 0;   // size bgfx was last initialized/reset with
    bool m_arrPressedKeys[SDL_SCANCODE_COUNT] = {};   // went down during this frame's events (readKeyboard)
    bool m_bPressedLeft = false;                    // the same for the left button / a touch
    bool m_bPressQueued = false;                    // a touch: hovered this frame, pressed the next
    float m_fLastMx = -1, m_fLastMy = -1;
};

App* g_pApp = nullptr;

bool f_AppInit(App& l_oApp) {
    const Args& l_oArgs = l_oApp.m_oArgs;
    SDL_SetMainReady();
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");   // Back arrives as a key (= Esc) instead of closing the app
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        f_FatalBox(nullptr, "TOMS: SDL could not start", SDL_GetError());
        return false;
    }
    // Web: no SDL_WINDOW_FILL_DOCUMENT. shell.html sizes the canvas with CSS (full viewport) and SDL
    // follows that; fill-document mode depended on a probe that fails at fractional display scaling
    // (the canvas stayed 1x1) and it also hid the page's own buttons.
    SDL_WindowFlags l_u64Flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // A run that only takes pictures draws into a hidden window: no window flashing open and shut on the desktop.
    if (!l_oArgs.m_strScreenshot.empty() || !l_oArgs.m_strRecord.empty()) l_u64Flags |= SDL_WINDOW_HIDDEN;
#endif
    l_oApp.m_pWindow = SDL_CreateWindow("Tower of the Sorcerer (bgfx)", l_oArgs.m_iWindowW, l_oArgs.m_iWindowH, l_u64Flags);
    if (!l_oApp.m_pWindow) {
        f_FatalBox(nullptr, "TOMS: no window", SDL_GetError());
        return false;
    }

    BgfxHostConfig l_oCfg;
#if defined(__EMSCRIPTEN__)
    // bgfx's WebGL backend takes the canvas CSS selector as its "window handle".
    static std::string l_strCanvas;
    const char* l_pszId = SDL_GetStringProperty(SDL_GetWindowProperties(l_oApp.m_pWindow),
                                           SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, "#canvas");
    l_strCanvas = (l_pszId && l_pszId[0] == '#') ? l_pszId : std::string("#") + (l_pszId ? l_pszId : "canvas");
    l_oCfg.m_pNativeWindow = (void*)l_strCanvas.c_str();
#elif defined(_WIN32)
    l_oCfg.m_pNativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(l_oApp.m_pWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__ANDROID__)
    // The ANativeWindow of SDLActivity's surface; bgfx makes its EGL (or Vulkan) surface on it.
    l_oCfg.m_pNativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(l_oApp.m_pWindow), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#endif
    l_oApp.m_pNativeWindow = l_oCfg.m_pNativeWindow;
    int l_iPw = 0, l_iPh = 0;
    SDL_GetWindowSizeInPixels(l_oApp.m_pWindow, &l_iPw, &l_iPh);
    l_oCfg.m_uWidth = (uint32_t)l_iPw; l_oCfg.m_uHeight = (uint32_t)l_iPh;
    l_oApp.m_iBackW = l_iPw; l_oApp.m_iBackH = l_iPh;
    l_oCfg.m_strRenderer = l_oArgs.m_strRenderer;
    l_oCfg.m_bVsync = l_oArgs.m_bVsync;
    l_oCfg.m_bDebugText = l_oArgs.m_bStats;
    std::string l_strErr;
    if (!f_BgfxHostInit(l_oCfg, l_strErr)) {
        f_FatalBox(l_oApp.m_pWindow, "TOMS: graphics could not start", l_strErr);
        return false;
    }

#if defined(__ANDROID__)
    // Everything the game ships with lives INSIDE the APK, so vfs must have the AAssetManager before the
    // first file is read -- without this every read silently falls back to stdio and finds nothing
    // (a black screen with no assets). SDL3 exposes the JNI environment and the activity, and
    // AAssetManager_fromJava is the NDK's documented way to get the manager from the activity.
    {
        JNIEnv* l_pEnv = (JNIEnv*)SDL_GetAndroidJNIEnv();
        jobject l_pActivity = l_pEnv ? (jobject)SDL_GetAndroidActivity() : nullptr;
        if (l_pEnv && l_pActivity) {
            jclass l_pCls = l_pEnv->GetObjectClass(l_pActivity);
            jmethodID l_pGetAssets = l_pCls ? l_pEnv->GetMethodID(l_pCls, "getAssets", "()Landroid/content/res/AssetManager;") : nullptr;
            jobject l_pAssets = l_pGetAssets ? l_pEnv->CallObjectMethod(l_pActivity, l_pGetAssets) : nullptr;
            // A failed lookup leaves a Java exception pending, and the NEXT JNI call (SDL's own) would
            // then abort the app; clear it so a failure shows up as the log line below instead.
            if (l_pEnv->ExceptionCheck()) { l_pEnv->ExceptionDescribe(); l_pEnv->ExceptionClear(); }
            if (l_pAssets) toms::f_VfsInit(AAssetManager_fromJava(l_pEnv, l_pAssets));
        }
        fprintf(stderr, "[android] vfs assets: %s\n",
                toms::g_pVfsAssetManager ? "ready" : "MISSING -- assets will not load");
        // Saves cannot live beside the binary on Android: app-private storage, via SDL, and tell the
        // save layer through the override defaultSaveDir() reads. Done here, before any save is touched.
        if (char* l_pszPref = SDL_GetPrefPath("WSLHermesAI", "TOMS")) {
            setenv("TOMS_SAVE_DIR", l_pszPref, 1);
            fprintf(stderr, "[android] saves: %s\n", l_pszPref);
            SDL_free(l_pszPref);
        }
    }
#endif
    SessionOptions l_oOpts;
    l_oOpts.m_strAssetDir = l_oArgs.m_strAssets.empty() ? GameSession::f_DefaultAssetDir() : l_oArgs.m_strAssets;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // A packaged game (tools\release_windows.cmd) carries its content next to the exe: <exe dir>/assets/media
    // (+ assets/data). That wins over the path baked in at build time, unless --assets / ASSET_DIR
    // say otherwise.
    if (l_oArgs.m_strAssets.empty() && !std::getenv("ASSET_DIR")) {
        if (const char* l_pszBase = SDL_GetBasePath()) {
            const std::string l_strPackaged = std::string(l_pszBase) + "assets/media";
            if (std::filesystem::exists(l_strPackaged + "/sprites")) l_oOpts.m_strAssetDir = l_strPackaged;
        }
    }
#endif
    l_oOpts.m_strStartStage = l_oArgs.m_strStage;
    l_oOpts.m_strPreviewAnim = l_oArgs.m_strAnim;
    l_oOpts.m_strPreviewFx = l_oArgs.m_strFx;
    l_oOpts.m_strPreviewMesh = l_oArgs.m_strMeshTest;
    l_oOpts.m_strPreviewCharacter = l_oArgs.m_strCharacter;
    l_oOpts.m_strPreviewHeld = l_oArgs.m_strHeld;
    l_oOpts.m_iPreviewCrowd = l_oArgs.m_iAnimCrowd;
    l_oOpts.m_strSpritePath = l_oArgs.m_strSpritePath;
    l_oOpts.m_bShowFps = l_oArgs.m_bFps;
#if !defined(NDEBUG) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    l_oOpts.m_bHotReload = true;   // a debug build on a desktop: art edited while playing shows up
#else
    l_oOpts.m_bHotReload = l_oArgs.m_bHotReload;
#endif
    l_oOpts.m_iFxGpuThreshold = l_oArgs.m_iFxGpuThreshold;
    l_oOpts.m_bDefaultBattleTuning = l_oArgs.m_bDefaultBattleTuning;
    l_oOpts.m_bBattleLog = l_oArgs.m_bBattleLog;
    l_oOpts.m_strTitleScene = l_oArgs.m_strTitleScene;
    if (l_oArgs.m_fUiScale > 0.0f) l_oOpts.m_fUiScale = l_oArgs.m_fUiScale;
    l_oOpts.m_uDesignW = l_oArgs.m_uDesignW;
    l_oOpts.m_uDesignH = l_oArgs.m_uDesignH;
#ifdef __EMSCRIPTEN__
    l_oOpts.m_bEnableDebugUi = true;
    // Small screens (phones): grow the UI objects, keep the game resolution -- same thresholds and
    // factors as the old web entry (emscripten_main.cpp, removed 2026-09-27).
    const int l_iCssW = EM_ASM_INT({ return window.innerWidth; });
    const int l_iCssH = EM_ASM_INT({ return window.innerHeight; });
    if (l_iCssW < 900 || l_iCssH < 560) {
        l_oOpts.m_fUiScale = 1.5f;
        std::fprintf(stderr, "[web] small screen (%dx%d css): pad x1.20, UI x1.50\n", l_iCssW, l_iCssH);
    }
#endif
    if (!l_oApp.m_oSession.f_Start(l_oOpts, l_strErr)) {
        f_FatalBox(l_oApp.m_pWindow, "TOMS: the game could not start", l_strErr);
        return false;
    }
    std::printf("TOMS on bgfx (%s). Arrows/WASD or click a tile to move, Enter interacts, I inventory, B store, "
                "Tab stage select, F1 debug, Esc menu.\n", f_BgfxHostRendererName().c_str());
    l_oApp.m_u64LastTicks = SDL_GetTicks();
    return true;
}

// One frame. Returns false when the game should end (desktop only).
bool f_AppFrame(App& l_oApp) {
    const Args& l_oArgs = l_oApp.m_oArgs;
    InputState& l_oIn = l_oApp.m_oIn;
    bool l_bQuit = false;
    l_oIn.m_fWheel = 0;
    SDL_Event l_oE;
    while (SDL_PollEvent(&l_oE)) {
        if (l_oE.type == SDL_EVENT_QUIT) {
            l_bQuit = true;
            if (::Game* l_pG = l_oApp.m_oSession.f_Game()) l_pG->f_FlushSave();   // the autosave's last seconds (docs/gameplay/05)
        }
        // Android (and any mobile OS) suspends the app: stop drawing while backgrounded, and resync the
        // drawable size on return -- a suspended app can come back at a different size, and its GL context
        // may have been lost. These events never fire on desktop, so this is inert there.
        else if (l_oE.type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
            l_oApp.m_bSuspended = true;
            if (::Game* l_pG = l_oApp.m_oSession.f_Game()) l_pG->f_FlushSave();   // Android may end a backgrounded app without notice
        }
        else if (l_oE.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
            l_oApp.m_bSuspended = false;      // the per-frame drawable-size compare handles the rest
            l_oApp.m_bResumed = true;
        }
        else if (l_oE.type == SDL_EVENT_MOUSE_WHEEL) l_oIn.m_fWheel += l_oE.wheel.y;
        else if (l_oE.type == SDL_EVENT_KEY_DOWN && l_oE.key.scancode < SDL_SCANCODE_COUNT) l_oApp.m_arrPressedKeys[l_oE.key.scancode] = true;
        else if (l_oE.type == SDL_EVENT_MOUSE_BUTTON_DOWN && l_oE.button.button == SDL_BUTTON_LEFT) l_oApp.m_bPressedLeft = true;
    }
#if defined(__ANDROID__)
    // In the background there is no surface to draw on: skip the frame (SDL blocks the loop while the
    // activity is paused anyway). Back in the foreground the surface is NEW, and bgfx must make its
    // EGL surface on it, or every frame fails with EGL_BAD_SURFACE.
    if (l_oApp.m_bSuspended) { SDL_Delay(16); return !l_bQuit; }
    if (void* l_pNwh = SDL_GetPointerProperty(SDL_GetWindowProperties(l_oApp.m_pWindow), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr)) {
        if (l_pNwh != l_oApp.m_pNativeWindow || l_oApp.m_bResumed) {
            int l_iSw = 1, l_iSh = 1;
            SDL_GetWindowSizeInPixels(l_oApp.m_pWindow, &l_iSw, &l_iSh);
            f_BgfxHostSetWindow(l_pNwh, (uint32_t)l_iSw, (uint32_t)l_iSh);
            l_oApp.m_pNativeWindow = l_pNwh;
            l_oApp.m_iBackW = l_iSw; l_oApp.m_iBackH = l_iSh;
            l_oApp.m_bResumed = false;
            l_oApp.m_u64LastTicks = SDL_GetTicks();   // no giant frame time for the time spent away
        }
    }
#endif
    f_ReadKeyboard(l_oIn, l_oApp.m_arrPressedKeys);
    std::fill(std::begin(l_oApp.m_arrPressedKeys), std::end(l_oApp.m_arrPressedKeys), false);
    for (const auto& l_oP : l_oArgs.m_vPresses)                       // scripted presses (tests)
        if (l_oApp.m_iFrameNo >= l_oP.m_iFrame && l_oApp.m_iFrameNo < l_oP.m_iFrame + 2) l_oIn.m_arrDown[(size_t)l_oP.m_eKey] = true;

    float l_fMx = 0, l_fMy = 0;
    const SDL_MouseButtonFlags l_uButtons = SDL_GetMouseState(&l_fMx, &l_fMy);
    if (l_oApp.m_iFrameNo == l_oArgs.m_iResizeFrame) SDL_SetWindowSize(l_oApp.m_pWindow, l_oArgs.m_iResizeW, l_oArgs.m_iResizeH);   // --resize (tests)
    int l_iWw = 1, l_iWh = 1, l_iPw = 1, l_iPh = 1;
    SDL_GetWindowSize(l_oApp.m_pWindow, &l_iWw, &l_iWh);
    SDL_GetWindowSizeInPixels(l_oApp.m_pWindow, &l_iPw, &l_iPh);
    // Keep bgfx's backbuffer equal to the real drawable size even if a resize event was missed.
    if (l_iPw > 0 && l_iPh > 0 && (l_iPw != l_oApp.m_iBackW || l_iPh != l_oApp.m_iBackH)) {
        f_BgfxHostReset((uint32_t)l_iPw, (uint32_t)l_iPh);
        l_oApp.m_iBackW = l_iPw; l_oApp.m_iBackH = l_iPh;
    }
    l_oIn.m_fMouseX = l_fMx * (float)l_iPw / (float)(l_iWw > 0 ? l_iWw : 1);   // window points -> pixels (HiDPI)
    l_oIn.m_fMouseY = l_fMy * (float)l_iPh / (float)(l_iWh > 0 ? l_iWh : 1);
    // A press that went down during the events counts even if it is already up again (a quick tap).
    // A touch moves the pointer and presses at once, but the UI (like a mouse) expects the pointer to
    // hover before it presses: then this frame only hovers, and the press follows next frame.
    bool l_bLeft = (l_uButtons & SDL_BUTTON_LMASK) != 0 || l_oApp.m_bPressedLeft;
    if (l_oApp.m_bPressQueued) { l_bLeft = true; l_oApp.m_bPressQueued = false; }
    else if (l_oApp.m_bPressedLeft && (l_fMx != l_oApp.m_fLastMx || l_fMy != l_oApp.m_fLastMy)) { l_bLeft = false; l_oApp.m_bPressQueued = true; }
    l_oApp.m_bPressedLeft = false;
    l_oApp.m_fLastMx = l_fMx; l_oApp.m_fLastMy = l_fMy;
    l_oIn.m_bMouseLeft   = l_bLeft;
    l_oIn.m_bMouseRight  = (l_uButtons & SDL_BUTTON_RMASK) != 0;
    l_oIn.m_bMouseMiddle = (l_uButtons & SDL_BUTTON_MMASK) != 0;
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    l_oIn.m_bHasMouse = true;   // touch arrives as mouse events; there is no "mouse focus" on a phone
#else
    l_oIn.m_bHasMouse = (SDL_GetWindowFlags(l_oApp.m_pWindow) & SDL_WINDOW_MOUSE_FOCUS) != 0;
#endif
    if (l_oArgs.m_iFixedDtMs > 0) {   // a scripted test run: only the scripted keys and clicks count
        l_oIn = InputState{};
        for (const auto& l_oP : l_oArgs.m_vPresses)
            if (l_oApp.m_iFrameNo >= l_oP.m_iFrame && l_oApp.m_iFrameNo < l_oP.m_iFrame + 2) l_oIn.m_arrDown[(size_t)l_oP.m_eKey] = true;
    }
    for (const auto& l_oC : l_oArgs.m_vClicks) {                      // scripted clicks (tests)
        const int l_iD = l_oApp.m_iFrameNo - l_oC.m_iFrame;                 // hover 2 frames, press 2, release, stay
        if (l_iD < -2 || l_iD > 4) continue;
        const auto l_oVp = BgfxRenderer::f_ComputeAspectFitViewport((uint32_t)l_iPw, (uint32_t)l_iPh,
                                                               BgfxRenderer::m_uDesignW, BgfxRenderer::m_uDesignH);
        l_oIn.m_fMouseX = l_oVp.m_fX + l_oC.m_fX * l_oVp.m_fWidth / (float)BgfxRenderer::m_uDesignW;
        l_oIn.m_fMouseY = l_oVp.m_fY + l_oC.m_fY * l_oVp.m_fHeight / (float)BgfxRenderer::m_uDesignH;
        l_oIn.m_bMouseLeft = (l_iD == 0 || l_iD == 1);
        l_oIn.m_bHasMouse = true;
    }

    if (l_oArgs.m_bSkipStartStory)
        if (::Game* l_pG = l_oApp.m_oSession.f_Game()) l_pG->f_SetSkipStartStory(true);
    // --scenario: once the game is up, straight into the scene (the title closes, unless the scenario is the title).
    if ((!l_oArgs.m_strScenario.empty() || !l_oArgs.m_strArena.empty()) && !l_oApp.m_bScenarioStarted && l_oApp.m_oSession.f_Game()) {
        l_oApp.m_bScenarioStarted = true;
        std::string l_strErr;
        ::Game* l_pG = l_oApp.m_oSession.f_Game();
        bool l_bStarted = false;
        if (!l_oArgs.m_strScenario.empty()) {
            l_bStarted = l_pG->f_StartScenario(l_oArgs.m_strScenario, l_strErr);
        } else {   // --arena=slime,bat:wand -> the same as a scenario with scene "arena"
            const size_t l_nColon = l_oArgs.m_strArena.find(':');
            const std::string l_strList = l_oArgs.m_strArena.substr(0, l_nColon);
            const std::string l_strWeapon = l_nColon == std::string::npos ? std::string() : l_oArgs.m_strArena.substr(l_nColon + 1);
            nlohmann::json l_jSc = {{"scene", "arena"}, {"battle", {{"enemies", nlohmann::json::array()}}}};
            for (size_t l_nPos = 0; l_nPos <= l_strList.size();) {
                const size_t l_nComma = std::min(l_strList.find(',', l_nPos), l_strList.size());
                if (l_nComma > l_nPos) l_jSc["battle"]["enemies"].push_back(l_strList.substr(l_nPos, l_nComma - l_nPos));
                l_nPos = l_nComma + 1;
            }
            if (!l_strWeapon.empty() && l_strWeapon != "unarmed") l_jSc["player"] = {{"equip", {l_strWeapon}}};
            else l_jSc["player"] = {{"unarmed", true}};   // no weapon named: unarmed (a new game would wear the bow)
            l_bStarted = l_pG->f_StartScenarioJson(l_jSc, "arena", l_strErr);
        }
        if (!l_bStarted) {
            std::fprintf(stderr, "[scenario] %s\n", l_strErr.c_str());
            l_oApp.m_iExitCode = 3;
            l_bQuit = true;
        } else if (l_oArgs.m_bScenarioTest) {   // the scenario's own frames and keys, counted from now
            if (l_oApp.m_oArgs.m_iFrames == 0) l_oApp.m_oArgs.m_iFrames = l_oApp.m_iFrameNo + std::max(1, l_pG->f_ScenarioTestFrames());
            const size_t l_nBefore = l_oApp.m_oArgs.m_vPresses.size();
            f_ParseKeys(l_pG->f_ScenarioTestKeys(), l_oApp.m_oArgs);
            for (size_t l_nI = l_nBefore; l_nI < l_oApp.m_oArgs.m_vPresses.size(); ++l_nI) l_oApp.m_oArgs.m_vPresses[l_nI].m_iFrame += l_oApp.m_iFrameNo;
            const size_t l_nClicksBefore = l_oApp.m_oArgs.m_vClicks.size();   // and its clicks ("test": { "clicks": "x:y@f,..." })
            f_ParseClicks(l_pG->f_ScenarioTestClicks(), l_oApp.m_oArgs);
            for (size_t l_nI = l_nClicksBefore; l_nI < l_oApp.m_oArgs.m_vClicks.size(); ++l_nI) l_oApp.m_oArgs.m_vClicks[l_nI].m_iFrame += l_oApp.m_iFrameNo;
        }
    }
    if (!l_oApp.m_bGave && (!l_oArgs.m_vGive.empty() || !l_oArgs.m_strStairLock.empty() || !l_oArgs.m_vEventsDone.empty() || !l_oArgs.m_strGotoFloor.empty()) &&
        l_oApp.m_oSession.f_Game() &&
        !l_oApp.m_oSession.f_Game()->f_TitleOpen()) {
        ::Game* l_pG = l_oApp.m_oSession.f_Game();
        for (const std::string& l_strId : l_oArgs.m_vGive) l_pG->f_DebugGive(l_strId);
        if (const size_t l_nColon = l_oArgs.m_strStairLock.find(':'); l_nColon != std::string::npos)
            l_pG->f_DebugLockStairs(l_oArgs.m_strStairLock.substr(0, l_nColon), l_oArgs.m_strStairLock.substr(l_nColon + 1));
        for (const std::string& l_strId : l_oArgs.m_vEventsDone) l_pG->f_DebugFinishEvent(l_strId);
        if (!l_oArgs.m_strGotoFloor.empty()) l_pG->f_LoadStage(l_oArgs.m_strGotoFloor);
        l_oApp.m_bGave = true;
    }
    // Fullscreen is the host's: the player menu's System tab shows the row and asks; this switches.
    // Web: the page does it (shell.html's tomsToggleFullscreen), inside the click's user activation.
    // Android: always full screen, so the row is hidden.
    if (::Game* l_pGame = l_oApp.m_oSession.f_Game()) {
#if defined(__EMSCRIPTEN__)
        const bool l_bFsOn = EM_ASM_INT({ return document.fullscreenElement || document.webkitFullscreenElement ? 1 : 0; }) != 0;
        l_pGame->f_SetFullscreenState(true, l_bFsOn);
        if (l_pGame->f_TakeFullscreenRequest()) EM_ASM({ if (window.tomsToggleFullscreen) window.tomsToggleFullscreen(); });
#elif defined(__ANDROID__)
        l_pGame->f_SetFullscreenState(false, true);
        l_pGame->f_TakeFullscreenRequest();
#else
        const bool l_bFsOn = (SDL_GetWindowFlags(l_oApp.m_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
        l_pGame->f_SetFullscreenState(true, l_bFsOn);
        if (l_pGame->f_TakeFullscreenRequest()) SDL_SetWindowFullscreen(l_oApp.m_pWindow, !l_bFsOn);
#endif
    }

    const uint64_t l_u64Now = SDL_GetTicks();
    const int l_iDtMs = l_oArgs.m_iFixedDtMs > 0 ? l_oArgs.m_iFixedDtMs : (int)(l_u64Now - l_oApp.m_u64LastTicks);
    l_oApp.m_u64LastTicks = l_u64Now;
    const uint64_t l_u64BenchT0 = SDL_GetPerformanceCounter();
    const bool l_bMore = l_oApp.m_oSession.f_Frame(l_iDtMs, l_oIn, (uint32_t)l_iPw, (uint32_t)l_iPh);
    if (!l_bMore) {
#ifndef __EMSCRIPTEN__
        l_bQuit = true;   // Esc on the title menu; a web page just stays on the title
#endif
    }

    if (l_oArgs.m_iFrames > 0 && !l_oArgs.m_strScreenshot.empty() && l_oApp.m_iFrameNo == l_oArgs.m_iFrames)
        l_oApp.m_oSession.f_Renderer()->f_SavePNG(l_oArgs.m_strScreenshot);
    if (!l_oArgs.m_strRecord.empty() && l_oApp.m_iFrameNo >= l_oArgs.m_iRecordFrom && (l_oApp.m_iFrameNo - l_oArgs.m_iRecordFrom) % l_oArgs.m_iRecordStep == 0 &&
        (l_oArgs.m_iFrames <= 0 || l_oApp.m_iFrameNo <= l_oArgs.m_iFrames)) {
        static int l_iRecorded = 0;
        if (l_iRecorded == 0) std::filesystem::create_directories(l_oArgs.m_strRecord);
        char l_pszName[32];
        std::snprintf(l_pszName, sizeof l_pszName, "/frame_%05d.png", l_iRecorded++);
        l_oApp.m_oSession.f_Renderer()->f_SavePNG(l_oArgs.m_strRecord + l_pszName);
    }
    f_BgfxHostFrame();
    if (l_oArgs.m_bBench && l_oApp.m_oSession.f_Renderer()) {   // every 120 frames: the averages (stderr)
        static double l_dSumFrame = 0, l_dSumAnim = 0, l_dSumCalls = 0, l_dSumGpu = 0;
        static int l_iCount = 0;
        l_dSumFrame += double(SDL_GetPerformanceCounter() - l_u64BenchT0) * 1000.0 / double(SDL_GetPerformanceFrequency());
        l_dSumAnim += l_oApp.m_oSession.f_Game() ? l_oApp.m_oSession.f_Game()->f_PreviewDrawMs() : 0.0;
        l_dSumCalls += l_oApp.m_oSession.f_Renderer()->f_LastDrawCalls();
        l_dSumGpu += f_BgfxHostGpuMs();
        if (++l_iCount == 120) {
            std::fprintf(stderr, "[bench] frame (update + draw + bgfx::frame) %.2f ms, clips' draw %.2f ms, draw calls %.0f, GPU %.2f ms\n",
                         l_dSumFrame / l_iCount, l_dSumAnim / l_iCount, l_dSumCalls / l_iCount, l_dSumGpu / l_iCount);
            l_dSumFrame = l_dSumAnim = l_dSumCalls = l_dSumGpu = 0;
            l_iCount = 0;
        }
    }
    ++l_oApp.m_iFrameNo;
    if (l_oArgs.m_iFrames > 0 && l_oApp.m_iFrameNo > l_oArgs.m_iFrames + (l_oArgs.m_strScreenshot.empty() && l_oArgs.m_strRecord.empty() ? 0 : 3)) l_bQuit = true;
    if (l_bQuit && l_oArgs.m_bScenarioTest && l_oApp.m_iExitCode == 0 && l_oApp.m_bScenarioStarted && l_oApp.m_oSession.f_Game()) {
        std::string l_strReport;
        if (!l_oApp.m_oSession.f_Game()->f_ScenarioCheck(l_strReport)) l_oApp.m_iExitCode = 3;
        std::fprintf(stderr, "%s\n", l_strReport.c_str());
    }
    return !l_bQuit;
}

void f_AppShutdown(App& l_oApp) {
    l_oApp.m_oSession.f_Stop();
    toms::JobSystem::f_Stop();   // join the worker threads before the process exits
    f_BgfxHostShutdown();
    if (l_oApp.m_pWindow) SDL_DestroyWindow(l_oApp.m_pWindow);
    SDL_Quit();
}

#ifdef __EMSCRIPTEN__
void f_WebFrame(void*) {
    if (g_pApp) f_AppFrame(*g_pApp);
}
#endif

}  // namespace

#ifdef __EMSCRIPTEN__
// ---- JavaScript hooks (called from shell.html with Module.ccall) ----
extern "C" {
// IDBFS finished loading /save: re-read the save slots so the title's Continue list is right.
// The page is being hidden or closed (pagehide / visibilitychange below): write the run now; the save code then
// starts its IndexedDB flush.
EMSCRIPTEN_KEEPALIVE void jsFlushSave() {
    if (g_pApp)
        if (::Game* l_pG = g_pApp->m_oSession.f_Game()) l_pG->f_FlushSave();
}

EMSCRIPTEN_KEEPALIVE void jsRefreshSlots() {
    if (g_pApp && g_pApp->m_oSession.f_Game()) g_pApp->m_oSession.f_Game()->f_RefreshSlots();
}
// Opens (or closes) the player menu's Items tab -- for page tests; the page itself has no buttons.
EMSCRIPTEN_KEEPALIVE void jsInventory() {
    if (g_pApp && g_pApp->m_oSession.f_Game()) g_pApp->m_oSession.f_Game()->f_ToggleInventory();
}
// For page tests: frames drawn so far and whether the title screen is up.
EMSCRIPTEN_KEEPALIVE int jsFrameCount() { return g_pApp ? g_pApp->m_iFrameNo : 0; }
// Job-system worker threads (0 in the single-threaded build, see docs/engine/02_THREADS.md).
EMSCRIPTEN_KEEPALIVE int jsWorkerCount() { return toms::JobSystem::f_Workers(); }
EMSCRIPTEN_KEEPALIVE int jsTitleOpen() {
    return (g_pApp && g_pApp->m_oSession.f_Game() && g_pApp->m_oSession.f_Game()->f_TitleOpen()) ? 1 : 0;
}
}
#endif

#if defined(__ANDROID__)
// On Android stderr goes nowhere, but much of the game reports with fprintf(stderr) ("[jobs] ...",
// "[rmlui] ..."): pipe it into logcat, one entry per line, tag "toms". (The logger's own lines reach
// logcat directly, log.h; they go to stdout, which is left alone so nothing is logged twice.)
static void f_PipeStderrToLogcat() {
    static int l_arrFds[2];
    if (pipe(l_arrFds) != 0) return;
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(l_arrFds[1], STDERR_FILENO);
    std::thread([] {
        char l_pszBuf[512];
        std::string l_strLine;
        ssize_t l_iN;
        while ((l_iN = read(l_arrFds[0], l_pszBuf, sizeof l_pszBuf)) > 0) {
            for (ssize_t l_iI = 0; l_iI < l_iN; l_iI++) {
                if (l_pszBuf[l_iI] != '\n') { l_strLine += l_pszBuf[l_iI]; continue; }
                __android_log_write(ANDROID_LOG_INFO, "toms", l_strLine.c_str());
                l_strLine.clear();
            }
        }
    }).detach();
}
#endif

int main(int l_iArgc, char** l_pArgv) {
#if defined(__ANDROID__)
    f_PipeStderrToLogcat();
#endif
    std::vector<std::string> l_vArgList(l_pArgv + 1, l_pArgv + l_iArgc);
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Windows Debug builds: track every heap block from here; the report is written when the game exits.
    long l_iLeakBreak = 0;
    std::string l_strLeakTrace;
    for (const std::string& l_strA : l_vArgList) {
        if (l_strA.rfind("--leak-break=", 0) == 0) l_iLeakBreak = std::atol(l_strA.c_str() + 13);
        if (l_strA.rfind("--leak-trace=", 0) == 0) l_strLeakTrace = l_strA.substr(13);
    }
    toms::f_LeakCheckBegin(l_iLeakBreak, l_strLeakTrace.c_str());
#endif
#ifdef __EMSCRIPTEN__
    for (auto& l_strS : f_UrlArgs()) l_vArgList.push_back(l_strS);

    // Saves that survive a page reload: the core save code writes /save/*.json and calls
    // Module.__tomsSyncfs(false) after each save (src/game/save/save_slots.cpp). Mount IndexedDB
    // there. Without IndexedDB (private browsing) the mount fails and saves last for the session.
    EM_ASM({
        try {
            try { FS.mkdir('/save'); } catch (e) {}
            FS.mount(IDBFS, {}, '/save');
            Module.__tomsSyncfs = function(load) {
                try {
                    FS.syncfs(!!load, function(err) {
                        if (err) { console.warn('[TOMS] syncfs', err); return; }
                        if (load) { try { Module.ccall('jsRefreshSlots', null, [], []); } catch (e) {} }
                    });
                } catch (e) { console.warn('[TOMS] syncfs failed', e); }
            };
            Module.__tomsSyncfs(true);
            var flush = function() { try { Module.ccall('jsFlushSave', null, [], []); } catch (e) {} };
            window.addEventListener('pagehide', flush);
            document.addEventListener('visibilitychange', function() { if (document.visibilityState === 'hidden') flush(); });
        } catch (e) { console.warn('[TOMS] IndexedDB unavailable; saves last for this session only', e); }
    });

    g_pApp = new App();
    g_pApp->m_oArgs = f_ParseArgs(l_vArgList);
    if (!f_AppInit(*g_pApp)) return 1;
    emscripten_set_main_loop_arg(f_WebFrame, nullptr, 0, false);   // 0 = the browser's display rate
    return 0;
#else
    int l_iRc = 0;
    {
        App l_oApp;
        g_pApp = &l_oApp;
        l_oApp.m_oArgs = f_ParseArgs(l_vArgList);
        if (f_AppInit(l_oApp)) {
            while (f_AppFrame(l_oApp)) {}
        } else {
            l_iRc = 1;
        }
        if (l_iRc == 0) l_iRc = l_oApp.m_iExitCode;
        f_AppShutdown(l_oApp);
        g_pApp = nullptr;
    }
    toms::f_ReleaseSingletons();   // the job system and other singletons, now that the game is gone
    Object::f_DumpLeaks();   // same leak report as the old executable
    toms::f_LeakCheckEnd();  // Windows Debug: the leak report, written once the statics are destroyed (docs/engine/15)
    return l_iRc;
#endif
}
