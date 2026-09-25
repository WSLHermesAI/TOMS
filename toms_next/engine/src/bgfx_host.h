// bgfx_host.h -- one place that starts and stops bgfx for a native window. Used by both hosts:
// the SDL3 game (game/src/main_sdl.cpp) and the Qt editor viewport (editor/src/bgfx_viewport.cpp).
//
// This header does not include bgfx: bx requires C++20, while the legacy game code (and so the
// hosts that include its headers) stays C++17. Only toms_bgfx itself compiles bgfx code.
//
// bgfx is a process-wide singleton: one init per process. Extra windows (more editor viewports)
// get their own swap chain through bgfx::createFrameBuffer(nativeWindowHandle, ...).
#pragma once
#include <cstdint>
#include <string>

namespace toms::next {

struct BgfxHostConfig {
    void*       nativeWindow  = nullptr;   // HWND on Windows
    void*       nativeDisplay = nullptr;   // X11 Display* / wl_display* on Linux, else null
    uint32_t    width  = 1280;
    uint32_t    height = 720;
    std::string renderer = "auto";         // auto | d3d11 | d3d12 | vulkan | opengl | gles
    bool        vsync = true;
    bool        debugText = false;         // bgfx's on-screen stats overlay
};

// Initializes bgfx in single-threaded mode (bgfx::renderFrame() before bgfx::init), which is what
// both the SDL loop and the Qt timer expect. On failure, `error` says why and what to try.
bool bgfxHostInit(const BgfxHostConfig& cfg, std::string& error);
void bgfxHostReset(uint32_t width, uint32_t height);
void bgfxHostFrame();                      // bgfx::frame(): submit everything queued this frame
void bgfxHostShutdown();
bool bgfxHostReady();
std::string bgfxHostRendererName();       // e.g. "Direct3D 11"; "(not started)" before init

void bgfxHostSetDebugText(bool on);

// Number of screenshots written by the bgfx callback (tests wait for this to change).
int bgfxHostScreenshotsWritten();

}  // namespace toms::next
