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
//   --frames=<n>          quit after n frames (automated tests; desktop)
//   --screenshot=<png>    save the last frame to a PNG (with --frames; desktop)
//   --keys=<k@f,...>      press key k at frame f, e.g. enter@30,enter@60 (automated tests)
#include "bgfx_host.h"
#include "bgfx_renderer.h"
#include "game_session.h"
#include "game.h"
#include "object.h"

#define SDL_MAIN_HANDLED        // keep our own main(); SDL_SetMainReady() below tells SDL
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace toms::next;

namespace {

struct Args {
    std::string renderer = "auto", assets, stage = "stage01", screenshot;
    int frames = 0;
    bool vsync = true, stats = false;
    struct Press { Key key; int frame; };
    std::vector<Press> presses;
};

bool keyFromName(const std::string& n, Key& out) {
    static const struct { const char* name; Key key; } table[] = {
        {"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right},
        {"enter", Key::Enter}, {"space", Key::Space}, {"esc", Key::Escape}, {"tab", Key::Tab},
        {"f1", Key::F1}, {"f2", Key::F2}, {"i", Key::I}, {"b", Key::B},
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
        else if (s == "--no-vsync") a.vsync = false;
        else if (s == "--stats") a.stats = true;
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

void readKeyboard(InputState& in) {
    const bool* ks = SDL_GetKeyboardState(nullptr);
    auto set = [&](Key k, SDL_Scancode sc) { in.down[(size_t)k] = ks[sc]; };
    set(Key::Up, SDL_SCANCODE_UP);       set(Key::Down, SDL_SCANCODE_DOWN);
    set(Key::Left, SDL_SCANCODE_LEFT);   set(Key::Right, SDL_SCANCODE_RIGHT);
    set(Key::W, SDL_SCANCODE_W); set(Key::A, SDL_SCANCODE_A); set(Key::S, SDL_SCANCODE_S); set(Key::D, SDL_SCANCODE_D);
    in.down[(size_t)Key::Enter] = ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER];
    set(Key::Space, SDL_SCANCODE_SPACE); set(Key::Escape, SDL_SCANCODE_ESCAPE); set(Key::Tab, SDL_SCANCODE_TAB);
    set(Key::F1, SDL_SCANCODE_F1); set(Key::F2, SDL_SCANCODE_F2); set(Key::F3, SDL_SCANCODE_F3);
    set(Key::F, SDL_SCANCODE_F); set(Key::G, SDL_SCANCODE_G); set(Key::H, SDL_SCANCODE_H);
    set(Key::I, SDL_SCANCODE_I); set(Key::B, SDL_SCANCODE_B);
    const SDL_Scancode nums[9] = {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
                                  SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9};
    for (int i = 0; i < 9; ++i) in.down[(size_t)Key::Num1 + i] = ks[nums[i]];
}

// Everything one running game needs. Heap-allocated: on the web, main() returns while the
// browser keeps calling appFrame(), so nothing may live on main()'s stack.
struct App {
    Args args;
    SDL_Window* window = nullptr;
    GameSession session;
    InputState in;
    uint64_t lastTicks = 0;
    int frameNo = 0;
    int backW = 0, backH = 0;   // size bgfx was last initialized/reset with
};

App* g_app = nullptr;

bool appInit(App& app) {
    const Args& args = app.args;
    SDL_SetMainReady();
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
#endif
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

    SessionOptions opts;
    opts.assetDir = args.assets.empty() ? GameSession::defaultAssetDir() : args.assets;
    opts.startStage = args.stage;
#ifdef __EMSCRIPTEN__
    opts.enableDebugUi = true;
    // Small screens (phones): grow the UI objects, keep the game resolution -- same thresholds and
    // factors as the old web entry (src/engine/emscripten_main.cpp).
    const int cssW = EM_ASM_INT({ return window.innerWidth; });
    const int cssH = EM_ASM_INT({ return window.innerHeight; });
    if (cssW < 900 || cssH < 560) {
        opts.padScale = 1.20f;
        opts.uiScale = 1.5f;
        std::fprintf(stderr, "[web] small screen (%dx%d css): pad x1.20, UI x1.50\n", cssW, cssH);
    }
#else
    const std::string fontNote = GameSession::applyFontFallback(opts.assetDir);
    if (!fontNote.empty()) std::fprintf(stderr, "[toms] %s\n", fontNote.c_str());
#endif
    if (!app.session.start(opts, err)) {
        fatalBox(app.window, "TOMS: the game could not start", err);
        return false;
    }
    std::printf("TOMS on bgfx (%s). Arrows/WASD move, Enter interacts, I inventory, B store, "
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
        else if (e.type == SDL_EVENT_MOUSE_WHEEL) in.wheel += e.wheel.y;
    }
    readKeyboard(in);
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
    in.mouseLeft   = (buttons & SDL_BUTTON_LMASK) != 0;
    in.mouseRight  = (buttons & SDL_BUTTON_RMASK) != 0;
    in.mouseMiddle = (buttons & SDL_BUTTON_MMASK) != 0;
#ifdef __EMSCRIPTEN__
    in.hasMouse = true;   // touch arrives as mouse events; there is no "mouse focus" on a phone
#else
    in.hasMouse = (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
#endif

    const uint64_t now = SDL_GetTicks();
    const int dtMs = (int)(now - app.lastTicks);
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
// The page's backpack button.
EMSCRIPTEN_KEEPALIVE void jsInventory() {
    if (g_app && g_app->session.game()) g_app->session.game()->toggleInventory();
}
// For page tests: frames drawn so far and whether the title screen is up.
EMSCRIPTEN_KEEPALIVE int jsFrameCount() { return g_app ? g_app->frameNo : 0; }
EMSCRIPTEN_KEEPALIVE int jsTitleOpen() {
    return (g_app && g_app->session.game() && g_app->session.game()->titleOpen()) ? 1 : 0;
}
}
#endif

int main(int argc, char** argv) {
    std::vector<std::string> argList(argv + 1, argv + argc);
#ifdef __EMSCRIPTEN__
    for (auto& s : urlArgs()) argList.push_back(s);

    // Saves that survive a page reload: the legacy save code writes /save/*.json and calls
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
