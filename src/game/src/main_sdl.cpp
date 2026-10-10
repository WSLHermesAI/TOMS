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
//                         assets/media/models/VirtualCity.glb when it is there; docs/18_GLTF.md)
//   --frames=<n>          quit after n frames (automated tests; desktop)
//   --screenshot=<png>    save the last frame to a PNG (with --frames; desktop)
//   --keys=<k@f,...>      press key k at frame f, e.g. enter@30,enter@60 (automated tests)
//   --clicks=<x:y@f,...>  left-click at design point (x,y) (1024x768 space) at frame f (tests)
//   --fixed-dt=<ms>       every frame advances exactly this long, so a scripted run is identical
//                         every time (screenshot smoke tests, tests/CMakeLists.txt)
//   --anim=<file>#<clip>  play one clip of an .anim file over the screen (anim_clip.h; tests)
//   --ui-scale=<x>        the small-screen UI scale (the web build uses 1.5 on phones), to test it on desktop
//   --fx=<file>#<effect>  play one particle effect of a .particle file over the screen (particle_fx.h)
//   --give=<id,...>       when the title first closes, hand the player these items (data/items.json ids;
//                         gear is owned, not worn) and gold:<n> -- tests of the player menu (tests/CMakeLists.txt)
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
    std::string renderer = "auto", assets, stage = "stage01", screenshot, anim, fx;
    int frames = 0;
    bool vsync = true, stats = false, fps = false;
    std::string spritePath = "auto";
    int fxGpuThreshold = -1;
    std::string titleScene;            // --title-scene=<file|none>
    float uiScale = 0.0f;              // --ui-scale=<x>: the phone UI scale on desktop (testing the web build's small-screen layout)
    int fixedDtMs = 0;       // > 0: deterministic frame time (tests)
    struct Press { Key key; int frame; };
    std::vector<Press> presses;
    struct Click { float x, y; int frame; };
    std::vector<Click> clicks;
    std::vector<std::string> give;     // --give=<id,...>
};

bool keyFromName(const std::string& n, Key& out) {
    static const struct { const char* name; Key key; } table[] = {
        {"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right},
        {"enter", Key::Enter}, {"space", Key::Space}, {"esc", Key::Escape}, {"tab", Key::Tab},
        {"f1", Key::F1}, {"f2", Key::F2}, {"f5", Key::F5}, {"f8", Key::F8},
        {"i", Key::I}, {"b", Key::B}, {"c", Key::C}, {"q", Key::Q}, {"e", Key::E},
        {"1", Key::Num1}, {"2", Key::Num2}, {"3", Key::Num3},
    };
    for (auto& e : table) if (n == e.name) { out = e.key; return true; }
    return false;
}

void parseKeys(const std::string& list, Args& a) {
    size_t pos = 0;
    while (pos < list.size()) {
        size_t comma = list.find(',', pos);
        std::string item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        size_t at = item.find('@');
        Key k;
        if (at != std::string::npos && keyFromName(item.substr(0, at), k))
            a.presses.push_back({k, std::atoi(item.c_str() + at + 1)});
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
}

void parseClicks(const std::string& list, Args& a) {
    size_t pos = 0;
    while (pos < list.size()) {
        size_t comma = list.find(',', pos);
        std::string item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        float x = 0, y = 0; int f = 0;
        if (std::sscanf(item.c_str(), "%f:%f@%d", &x, &y, &f) == 3) a.clicks.push_back({x, y, f});
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
}

Args parseArgs(const std::vector<std::string>& argv) {
    Args a;
    auto value = [](const std::string& arg, const char* prefix) -> const char* {
        size_t n = std::strlen(prefix);
        return arg.compare(0, n, prefix) == 0 ? arg.c_str() + n : nullptr;
    };
    for (const std::string& s : argv) {
        if (const char* v = value(s, "--renderer="))   a.renderer = v;
        else if (const char* v = value(s, "--assets=")) a.assets = v;
        else if (const char* v = value(s, "--stage="))  a.stage = v;
        else if (const char* v = value(s, "--frames=")) a.frames = std::atoi(v);
        else if (const char* v = value(s, "--screenshot=")) a.screenshot = v;
        else if (const char* v = value(s, "--keys=")) parseKeys(v, a);
        else if (const char* v = value(s, "--clicks=")) parseClicks(v, a);
        else if (const char* v = value(s, "--fixed-dt=")) a.fixedDtMs = std::atoi(v);
        else if (const char* v = value(s, "--anim=")) a.anim = v;
        else if (const char* v = value(s, "--fx=")) a.fx = v;
        else if (s == "--no-vsync") a.vsync = false;
        else if (s == "--stats") a.stats = true;
        else if (s == "--no-instancing") a.spritePath = "cpu";
        else if (const char* v = value(s, "--sprite-path=")) a.spritePath = v;
        else if (s == "--fps") a.fps = true;
        else if (const char* v = value(s, "--fx-gpu-threshold=")) a.fxGpuThreshold = std::max(0, std::atoi(v));
        else if (const char* v = value(s, "--title-scene=")) a.titleScene = v;
        else if (const char* v = value(s, "--ui-scale=")) a.uiScale = (float)std::atof(v);
        else if (const char* v = value(s, "--give=")) {
            std::string list = v;
            for (size_t pos = 0; pos <= list.size();) {
                const size_t comma = std::min(list.find(',', pos), list.size());
                if (comma > pos) a.give.push_back(list.substr(pos, comma - pos));
                pos = comma + 1;
            }
        }
    }
    return a;
}

#ifdef __EMSCRIPTEN__
// ?stage=stage03&keys=enter@60&stats -> {"--stage=stage03", "--keys=enter@60", "--stats"}
std::vector<std::string> urlArgs() {
    char* q = (char*)EM_ASM_PTR({
        // No regex here: EM_ASM turns this into a C string, so backslash escapes would be lost.
        var s = (typeof location !== 'undefined' && location.search) ? location.search.substring(1) : '';
        return stringToNewUTF8(s);
    });
    std::string query = q ? q : "";
    free(q);
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string kv = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (!kv.empty()) out.push_back("--" + kv);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return out;
}
#endif

void fatalBox(SDL_Window* window, const std::string& title, const std::string& text) {
    std::fprintf(stderr, "[toms] %s\n%s\n", title.c_str(), text.c_str());
#ifdef __EMSCRIPTEN__
    (void)window;
    EM_ASM({ if (typeof tomsShowError === 'function') tomsShowError(UTF8ToString($0), UTF8ToString($1)); },
           title.c_str(), text.c_str());
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), window);
#endif
}

// `pressed`: keys that went down during this frame's events. A key pressed AND released between two
// frames (a quick tap, adb input, some on-screen keyboards) is not down in SDL_GetKeyboardState any
// more, but still counts as down for one frame.
void readKeyboard(InputState& in, const bool* pressed) {
    const bool* sdlKeys = SDL_GetKeyboardState(nullptr);
    bool ks[SDL_SCANCODE_COUNT];
    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) ks[i] = sdlKeys[i] || pressed[i];
    ks[SDL_SCANCODE_ESCAPE] = ks[SDL_SCANCODE_ESCAPE] || ks[SDL_SCANCODE_AC_BACK];   // Android's Back
    auto set = [&](Key k, SDL_Scancode sc) { in.down[(size_t)k] = ks[sc]; };
    set(Key::Up, SDL_SCANCODE_UP);       set(Key::Down, SDL_SCANCODE_DOWN);
    set(Key::Left, SDL_SCANCODE_LEFT);   set(Key::Right, SDL_SCANCODE_RIGHT);
    set(Key::W, SDL_SCANCODE_W); set(Key::A, SDL_SCANCODE_A); set(Key::S, SDL_SCANCODE_S); set(Key::D, SDL_SCANCODE_D);
    in.down[(size_t)Key::Enter] = ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER];
    set(Key::Space, SDL_SCANCODE_SPACE); set(Key::Escape, SDL_SCANCODE_ESCAPE); set(Key::Tab, SDL_SCANCODE_TAB);
    set(Key::F1, SDL_SCANCODE_F1); set(Key::F2, SDL_SCANCODE_F2); set(Key::F3, SDL_SCANCODE_F3);
    set(Key::F5, SDL_SCANCODE_F5); set(Key::F8, SDL_SCANCODE_F8);
    set(Key::F, SDL_SCANCODE_F); set(Key::G, SDL_SCANCODE_G); set(Key::H, SDL_SCANCODE_H);
    set(Key::I, SDL_SCANCODE_I); set(Key::B, SDL_SCANCODE_B);
    set(Key::C, SDL_SCANCODE_C); set(Key::Q, SDL_SCANCODE_Q); set(Key::E, SDL_SCANCODE_E);
    const SDL_Scancode nums[9] = {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
                                  SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9};
    for (int i = 0; i < 9; ++i) in.down[(size_t)Key::Num1 + i] = ks[nums[i]];
}

// Everything one running game needs. Heap-allocated: on the web, main() returns while the
// browser keeps calling appFrame(), so nothing may live on main()'s stack.
struct App {
    bool suspended = false;     // Android: set while backgrounded, so we do not render a suspended app
    bool resumed = false;       // Android: back in the foreground -- the surface is a new one
    void* nativeWindow = nullptr;   // the window handle bgfx draws on
    Args args;
    SDL_Window* window = nullptr;
    GameSession session;
    InputState in;
    uint64_t lastTicks = 0;
    int frameNo = 0;
    bool gave = false;          // --give applied
    int backW = 0, backH = 0;   // size bgfx was last initialized/reset with
    bool pressedKeys[SDL_SCANCODE_COUNT] = {};   // went down during this frame's events (readKeyboard)
    bool pressedLeft = false;                    // the same for the left button / a touch
    bool pressQueued = false;                    // a touch: hovered this frame, pressed the next
    float lastMx = -1, lastMy = -1;
};

App* g_app = nullptr;

bool appInit(App& app) {
    const Args& args = app.args;
    SDL_SetMainReady();
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");   // Back arrives as a key (= Esc) instead of closing the app
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fatalBox(nullptr, "TOMS: SDL could not start", SDL_GetError());
        return false;
    }
    // Web: no SDL_WINDOW_FILL_DOCUMENT. shell.html sizes the canvas with CSS (full viewport) and SDL
    // follows that; fill-document mode depended on a probe that fails at fractional display scaling
    // (the canvas stayed 1x1) and it also hid the page's own buttons.
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    app.window = SDL_CreateWindow("Tower of the Sorcerer (bgfx)", 1280, 720, flags);
    if (!app.window) {
        fatalBox(nullptr, "TOMS: no window", SDL_GetError());
        return false;
    }

    BgfxHostConfig cfg;
#if defined(__EMSCRIPTEN__)
    // bgfx's WebGL backend takes the canvas CSS selector as its "window handle".
    static std::string canvas;
    const char* id = SDL_GetStringProperty(SDL_GetWindowProperties(app.window),
                                           SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, "#canvas");
    canvas = (id && id[0] == '#') ? id : std::string("#") + (id ? id : "canvas");
    cfg.nativeWindow = (void*)canvas.c_str();
#elif defined(_WIN32)
    cfg.nativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(app.window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__ANDROID__)
    // The ANativeWindow of SDLActivity's surface; bgfx makes its EGL (or Vulkan) surface on it.
    cfg.nativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(app.window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#endif
    app.nativeWindow = cfg.nativeWindow;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(app.window, &pw, &ph);
    cfg.width = (uint32_t)pw; cfg.height = (uint32_t)ph;
    app.backW = pw; app.backH = ph;
    cfg.renderer = args.renderer;
    cfg.vsync = args.vsync;
    cfg.debugText = args.stats;
    std::string err;
    if (!bgfxHostInit(cfg, err)) {
        fatalBox(app.window, "TOMS: graphics could not start", err);
        return false;
    }

#if defined(__ANDROID__)
    // Everything the game ships with lives INSIDE the APK, so vfs must have the AAssetManager before the
    // first file is read -- without this every read silently falls back to stdio and finds nothing
    // (a black screen with no assets). SDL3 exposes the JNI environment and the activity, and
    // AAssetManager_fromJava is the NDK's documented way to get the manager from the activity.
    {
        JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
        jobject activity = env ? (jobject)SDL_GetAndroidActivity() : nullptr;
        if (env && activity) {
            jclass cls = env->GetObjectClass(activity);
            jmethodID getAssets = cls ? env->GetMethodID(cls, "getAssets", "()Landroid/content/res/AssetManager;") : nullptr;
            jobject assets = getAssets ? env->CallObjectMethod(activity, getAssets) : nullptr;
            // A failed lookup leaves a Java exception pending, and the NEXT JNI call (SDL's own) would
            // then abort the app; clear it so a failure shows up as the log line below instead.
            if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
            if (assets) toms::vfsInit(AAssetManager_fromJava(env, assets));
        }
        fprintf(stderr, "[android] vfs assets: %s\n",
                toms::g_vfsAssetManager ? "ready" : "MISSING -- assets will not load");
        // Saves cannot live beside the binary on Android: app-private storage, via SDL, and tell the
        // save layer through the override defaultSaveDir() reads. Done here, before any save is touched.
        if (char* pref = SDL_GetPrefPath("WSLHermesAI", "TOMS")) {
            setenv("TOMS_SAVE_DIR", pref, 1);
            fprintf(stderr, "[android] saves: %s\n", pref);
            SDL_free(pref);
        }
    }
#endif
    SessionOptions opts;
    opts.assetDir = args.assets.empty() ? GameSession::defaultAssetDir() : args.assets;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // A packaged game (build_windows.bat) carries its content next to the exe: <exe dir>/assets/media
    // (+ assets/data). That wins over the path baked in at build time, unless --assets / ASSET_DIR
    // say otherwise.
    if (args.assets.empty() && !std::getenv("ASSET_DIR")) {
        if (const char* base = SDL_GetBasePath()) {
            const std::string packaged = std::string(base) + "assets/media";
            if (std::filesystem::exists(packaged + "/sprites")) opts.assetDir = packaged;
        }
    }
#endif
    opts.startStage = args.stage;
    opts.previewAnim = args.anim;
    opts.previewFx = args.fx;
    opts.spritePath = args.spritePath;
    opts.showFps = args.fps;
    opts.fxGpuThreshold = args.fxGpuThreshold;
    opts.titleScene = args.titleScene;
    if (args.uiScale > 0.0f) opts.uiScale = args.uiScale;
#ifdef __EMSCRIPTEN__
    opts.enableDebugUi = true;
    // Small screens (phones): grow the UI objects, keep the game resolution -- same thresholds and
    // factors as the old web entry (emscripten_main.cpp, removed 2026-09-27).
    const int cssW = EM_ASM_INT({ return window.innerWidth; });
    const int cssH = EM_ASM_INT({ return window.innerHeight; });
    if (cssW < 900 || cssH < 560) {
        opts.uiScale = 1.5f;
        std::fprintf(stderr, "[web] small screen (%dx%d css): pad x1.20, UI x1.50\n", cssW, cssH);
    }
#endif
    if (!app.session.start(opts, err)) {
        fatalBox(app.window, "TOMS: the game could not start", err);
        return false;
    }
    std::printf("TOMS on bgfx (%s). Arrows/WASD or click a tile to move, Enter interacts, I inventory, B store, "
                "Tab stage select, F1 debug, Esc menu.\n", bgfxHostRendererName().c_str());
    app.lastTicks = SDL_GetTicks();
    return true;
}

// One frame. Returns false when the game should end (desktop only).
bool appFrame(App& app) {
    const Args& args = app.args;
    InputState& in = app.in;
    bool quit = false;
    in.wheel = 0;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) quit = true;
        // Android (and any mobile OS) suspends the app: stop drawing while backgrounded, and resync the
        // drawable size on return -- a suspended app can come back at a different size, and its GL context
        // may have been lost. These events never fire on desktop, so this is inert there.
        else if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND) app.suspended = true;
        else if (e.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
            app.suspended = false;      // the per-frame drawable-size compare handles the rest
            app.resumed = true;
        }
        else if (e.type == SDL_EVENT_MOUSE_WHEEL) in.wheel += e.wheel.y;
        else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode < SDL_SCANCODE_COUNT) app.pressedKeys[e.key.scancode] = true;
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) app.pressedLeft = true;
    }
#if defined(__ANDROID__)
    // In the background there is no surface to draw on: skip the frame (SDL blocks the loop while the
    // activity is paused anyway). Back in the foreground the surface is NEW, and bgfx must make its
    // EGL surface on it, or every frame fails with EGL_BAD_SURFACE.
    if (app.suspended) { SDL_Delay(16); return !quit; }
    if (void* nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(app.window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr)) {
        if (nwh != app.nativeWindow || app.resumed) {
            int sw = 1, sh = 1;
            SDL_GetWindowSizeInPixels(app.window, &sw, &sh);
            bgfxHostSetWindow(nwh, (uint32_t)sw, (uint32_t)sh);
            app.nativeWindow = nwh;
            app.backW = sw; app.backH = sh;
            app.resumed = false;
            app.lastTicks = SDL_GetTicks();   // no giant frame time for the time spent away
        }
    }
#endif
    readKeyboard(in, app.pressedKeys);
    std::fill(std::begin(app.pressedKeys), std::end(app.pressedKeys), false);
    for (const auto& p : args.presses)                       // scripted presses (tests)
        if (app.frameNo >= p.frame && app.frameNo < p.frame + 2) in.down[(size_t)p.key] = true;

    float mx = 0, my = 0;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
    int ww = 1, wh = 1, pw = 1, ph = 1;
    SDL_GetWindowSize(app.window, &ww, &wh);
    SDL_GetWindowSizeInPixels(app.window, &pw, &ph);
    // Keep bgfx's backbuffer equal to the real drawable size even if a resize event was missed.
    if (pw > 0 && ph > 0 && (pw != app.backW || ph != app.backH)) {
        bgfxHostReset((uint32_t)pw, (uint32_t)ph);
        app.backW = pw; app.backH = ph;
    }
    in.mouseX = mx * (float)pw / (float)(ww > 0 ? ww : 1);   // window points -> pixels (HiDPI)
    in.mouseY = my * (float)ph / (float)(wh > 0 ? wh : 1);
    // A press that went down during the events counts even if it is already up again (a quick tap).
    // A touch moves the pointer and presses at once, but the UI (like a mouse) expects the pointer to
    // hover before it presses: then this frame only hovers, and the press follows next frame.
    bool left = (buttons & SDL_BUTTON_LMASK) != 0 || app.pressedLeft;
    if (app.pressQueued) { left = true; app.pressQueued = false; }
    else if (app.pressedLeft && (mx != app.lastMx || my != app.lastMy)) { left = false; app.pressQueued = true; }
    app.pressedLeft = false;
    app.lastMx = mx; app.lastMy = my;
    in.mouseLeft   = left;
    in.mouseRight  = (buttons & SDL_BUTTON_RMASK) != 0;
    in.mouseMiddle = (buttons & SDL_BUTTON_MMASK) != 0;
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    in.hasMouse = true;   // touch arrives as mouse events; there is no "mouse focus" on a phone
#else
    in.hasMouse = (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
#endif
    if (args.fixedDtMs > 0) {   // a scripted test run: only the scripted keys and clicks count
        in = InputState{};
        for (const auto& p : args.presses)
            if (app.frameNo >= p.frame && app.frameNo < p.frame + 2) in.down[(size_t)p.key] = true;
    }
    for (const auto& c : args.clicks) {                      // scripted clicks (tests)
        const int d = app.frameNo - c.frame;                 // hover 2 frames, press 2, release, stay
        if (d < -2 || d > 4) continue;
        const auto vp = BgfxRenderer::computeAspectFitViewport((uint32_t)pw, (uint32_t)ph,
                                                               BgfxRenderer::kDesignW, BgfxRenderer::kDesignH);
        in.mouseX = vp.x + c.x * vp.width / (float)BgfxRenderer::kDesignW;
        in.mouseY = vp.y + c.y * vp.height / (float)BgfxRenderer::kDesignH;
        in.mouseLeft = (d == 0 || d == 1);
        in.hasMouse = true;
    }

    if (!app.gave && !args.give.empty() && app.session.game() && !app.session.game()->titleOpen()) {
        for (const std::string& id : args.give) app.session.game()->debugGive(id);
        app.gave = true;
    }
    // Fullscreen is the host's: the player menu's System tab shows the row and asks; this switches.
    // Web: the page does it (shell.html's tomsToggleFullscreen), inside the click's user activation.
    // Android: always full screen, so the row is hidden.
    if (::Game* game = app.session.game()) {
#if defined(__EMSCRIPTEN__)
        const bool fsOn = EM_ASM_INT({ return document.fullscreenElement || document.webkitFullscreenElement ? 1 : 0; }) != 0;
        game->setFullscreenState(true, fsOn);
        if (game->takeFullscreenRequest()) EM_ASM({ if (window.tomsToggleFullscreen) window.tomsToggleFullscreen(); });
#elif defined(__ANDROID__)
        game->setFullscreenState(false, true);
        game->takeFullscreenRequest();
#else
        const bool fsOn = (SDL_GetWindowFlags(app.window) & SDL_WINDOW_FULLSCREEN) != 0;
        game->setFullscreenState(true, fsOn);
        if (game->takeFullscreenRequest()) SDL_SetWindowFullscreen(app.window, !fsOn);
#endif
    }

    const uint64_t now = SDL_GetTicks();
    const int dtMs = args.fixedDtMs > 0 ? args.fixedDtMs : (int)(now - app.lastTicks);
    app.lastTicks = now;
    if (!app.session.frame(dtMs, in, (uint32_t)pw, (uint32_t)ph)) {
#ifndef __EMSCRIPTEN__
        quit = true;   // Esc on the title menu; a web page just stays on the title
#endif
    }

    if (args.frames > 0 && !args.screenshot.empty() && app.frameNo == args.frames)
        app.session.renderer()->savePNG(args.screenshot);
    bgfxHostFrame();
    ++app.frameNo;
    if (args.frames > 0 && app.frameNo > args.frames + (args.screenshot.empty() ? 0 : 3)) quit = true;
    return !quit;
}

void appShutdown(App& app) {
    app.session.stop();
    toms::JobSystem::stop();   // join the worker threads before the process exits
    bgfxHostShutdown();
    if (app.window) SDL_DestroyWindow(app.window);
    SDL_Quit();
}

#ifdef __EMSCRIPTEN__
void webFrame(void*) {
    if (g_app) appFrame(*g_app);
}
#endif

}  // namespace

#ifdef __EMSCRIPTEN__
// ---- JavaScript hooks (called from shell.html with Module.ccall) ----
extern "C" {
// IDBFS finished loading /save: re-read the save slots so the title's Continue list is right.
EMSCRIPTEN_KEEPALIVE void jsRefreshSlots() {
    if (g_app && g_app->session.game()) g_app->session.game()->refreshSlots();
}
// Opens (or closes) the player menu's Items tab -- for page tests; the page itself has no buttons.
EMSCRIPTEN_KEEPALIVE void jsInventory() {
    if (g_app && g_app->session.game()) g_app->session.game()->toggleInventory();
}
// For page tests: frames drawn so far and whether the title screen is up.
EMSCRIPTEN_KEEPALIVE int jsFrameCount() { return g_app ? g_app->frameNo : 0; }
// Job-system worker threads (0 in the single-threaded build, see docs/10_THREADS.md).
EMSCRIPTEN_KEEPALIVE int jsWorkerCount() { return toms::JobSystem::workers(); }
EMSCRIPTEN_KEEPALIVE int jsTitleOpen() {
    return (g_app && g_app->session.game() && g_app->session.game()->titleOpen()) ? 1 : 0;
}
}
#endif

#if defined(__ANDROID__)
// On Android stderr goes nowhere, but much of the game reports with fprintf(stderr) ("[jobs] ...",
// "[rmlui] ..."): pipe it into logcat, one entry per line, tag "toms". (The logger's own lines reach
// logcat directly, log.h; they go to stdout, which is left alone so nothing is logged twice.)
static void pipeStderrToLogcat() {
    static int fds[2];
    if (pipe(fds) != 0) return;
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(fds[1], STDERR_FILENO);
    std::thread([] {
        char buf[512];
        std::string line;
        ssize_t n;
        while ((n = read(fds[0], buf, sizeof buf)) > 0) {
            for (ssize_t i = 0; i < n; i++) {
                if (buf[i] != '\n') { line += buf[i]; continue; }
                __android_log_write(ANDROID_LOG_INFO, "toms", line.c_str());
                line.clear();
            }
        }
    }).detach();
}
#endif

int main(int argc, char** argv) {
#if defined(__ANDROID__)
    pipeStderrToLogcat();
#endif
    std::vector<std::string> argList(argv + 1, argv + argc);
#ifdef __EMSCRIPTEN__
    for (auto& s : urlArgs()) argList.push_back(s);

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
        } catch (e) { console.warn('[TOMS] IndexedDB unavailable; saves last for this session only', e); }
    });

    g_app = new App();
    g_app->args = parseArgs(argList);
    if (!appInit(*g_app)) return 1;
    emscripten_set_main_loop_arg(webFrame, nullptr, 0, false);   // 0 = the browser's display rate
    return 0;
#else
    int rc = 0;
    {
        App app;
        g_app = &app;
        app.args = parseArgs(argList);
        if (appInit(app)) {
            while (appFrame(app)) {}
        } else {
            rc = 1;
        }
        appShutdown(app);
        g_app = nullptr;
    }
    Object::DumpLeaks();   // same leak report as the old executable
    return rc;
#endif
}
