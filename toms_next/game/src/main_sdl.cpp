// main_sdl.cpp -- toms_game: the shipped game executable (SDL3 window + bgfx). No Qt here.
//
// Command line (all optional):
//   --renderer=auto|d3d11|d3d12|vulkan|opengl   pick the bgfx backend (default: auto)
//   --assets=<dir>        the TOMS assets folder (default: env ASSET_DIR, else ../assets of the repo)
//   --stage=<id>          first stage to load (default stage01)
//   --no-vsync            uncapped frame rate
//   --stats               bgfx's on-screen stats overlay
//   --frames=<n>          quit after n frames (automated tests)
//   --screenshot=<png>    save the last frame to a PNG (with --frames)
//   --keys=<k@f,...>      press key k at frame f, e.g. enter@30,enter@60 (automated tests)
#include "bgfx_host.h"
#include "bgfx_renderer.h"
#include "game_session.h"
#include "object.h"

#define SDL_MAIN_HANDLED        // keep our own main(); SDL_SetMainReady() below tells SDL
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

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

Args parseArgs(int argc, char** argv) {
    Args a;
    auto value = [](const char* arg, const char* prefix) -> const char* {
        size_t n = std::strlen(prefix);
        return std::strncmp(arg, prefix, n) == 0 ? arg + n : nullptr;
    };
    for (int i = 1; i < argc; ++i) {
        const char* s = argv[i];
        if (const char* v = value(s, "--renderer="))   a.renderer = v;
        else if (const char* v = value(s, "--assets=")) a.assets = v;
        else if (const char* v = value(s, "--stage="))  a.stage = v;
        else if (const char* v = value(s, "--frames=")) a.frames = std::atoi(v);
        else if (const char* v = value(s, "--screenshot=")) a.screenshot = v;
        else if (const char* v = value(s, "--keys=")) {
            std::string list = v;
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
        else if (std::strcmp(s, "--no-vsync") == 0) a.vsync = false;
        else if (std::strcmp(s, "--stats") == 0) a.stats = true;
    }
    return a;
}

void fatalBox(SDL_Window* window, const std::string& title, const std::string& text) {
    std::fprintf(stderr, "[toms] %s\n%s\n", title.c_str(), text.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), window);
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

}  // namespace

int main(int argc, char** argv) {
    const Args args = parseArgs(argc, argv);
    int exitCode = 0;
    {
        SDL_SetMainReady();
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            fatalBox(nullptr, "TOMS: SDL could not start", SDL_GetError());
            return 1;
        }
        SDL_Window* window = SDL_CreateWindow("Tower of the Sorcerer (bgfx)", 1280, 720,
                                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) {
            fatalBox(nullptr, "TOMS: no window", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        BgfxHostConfig cfg;
#if defined(_WIN32)
        cfg.nativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#endif
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        cfg.width = (uint32_t)pw; cfg.height = (uint32_t)ph;
        cfg.renderer = args.renderer;
        cfg.vsync = args.vsync;
        cfg.debugText = args.stats;
        std::string err;
        if (!bgfxHostInit(cfg, err)) {
            fatalBox(window, "TOMS: graphics could not start", err);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        GameSession session;
        SessionOptions opts;
        opts.assetDir = args.assets.empty() ? GameSession::defaultAssetDir() : args.assets;
        opts.startStage = args.stage;
        const std::string fontNote = GameSession::applyFontFallback(opts.assetDir);
        if (!fontNote.empty()) std::fprintf(stderr, "[toms] %s\n", fontNote.c_str());
        if (!session.start(opts, err)) {
            fatalBox(window, "TOMS: the game could not start", err);
            bgfxHostShutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        std::printf("TOMS on bgfx (%s). Arrows/WASD move, Enter interacts, I inventory, B store, "
                    "Tab stage select, F1 debug, Esc menu.\n", bgfxHostRendererName().c_str());

        InputState in;
        uint64_t last = SDL_GetTicks();
        int frameNo = 0;
        bool quit = false;
        while (!quit) {
            in.wheel = 0;
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) quit = true;
                else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) bgfxHostReset((uint32_t)e.window.data1, (uint32_t)e.window.data2);
                else if (e.type == SDL_EVENT_MOUSE_WHEEL) in.wheel += e.wheel.y;
            }
            readKeyboard(in);
            for (const auto& p : args.presses)                       // scripted presses (tests)
                if (frameNo >= p.frame && frameNo < p.frame + 2) in.down[(size_t)p.key] = true;

            float mx = 0, my = 0;
            const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
            int ww = 1, wh = 1;
            SDL_GetWindowSize(window, &ww, &wh);
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            in.mouseX = mx * (float)pw / (float)(ww > 0 ? ww : 1);   // window points -> pixels (HiDPI)
            in.mouseY = my * (float)ph / (float)(wh > 0 ? wh : 1);
            in.mouseLeft   = (buttons & SDL_BUTTON_LMASK) != 0;
            in.mouseRight  = (buttons & SDL_BUTTON_RMASK) != 0;
            in.mouseMiddle = (buttons & SDL_BUTTON_MMASK) != 0;
            in.hasMouse = (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS) != 0;

            const uint64_t now = SDL_GetTicks();
            const int dtMs = (int)(now - last);
            last = now;
            if (!session.frame(dtMs, in, (uint32_t)pw, (uint32_t)ph)) quit = true;

            if (args.frames > 0 && !args.screenshot.empty() && frameNo == args.frames)
                session.renderer()->savePNG(args.screenshot);
            bgfxHostFrame();
            ++frameNo;
            if (args.frames > 0 && frameNo > args.frames + (args.screenshot.empty() ? 0 : 3)) quit = true;
        }

        session.stop();
        bgfxHostShutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    Object::DumpLeaks();   // same leak report as the old executable
    return exitCode;
}
