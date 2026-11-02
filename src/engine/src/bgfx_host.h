// bgfx_host.h -- one place that starts and stops bgfx for a native window. Used by both hosts:
// the SDL3 game (game/src/main_sdl.cpp) and the Qt editor viewport (editor/src/bgfx_viewport.cpp).
//
// This header does not include bgfx: bx requires C++20, while the core game code (and so the
// hosts that include its headers) stays C++17. Only toms_bgfx itself compiles bgfx code.
//
// bgfx is a process-wide singleton: one init per process. Extra windows (more editor viewports)
// get their own swap chain through bgfx::createFrameBuffer(nativeWindowHandle, ...).
#pragma once
#include <cstdint>
#include <string>

namespace toms::next {

struct BgfxHostConfig {
    void*       m_pNativeWindow  = nullptr;   // HWND on Windows
    void*       m_pNativeDisplay = nullptr;   // X11 Display* / wl_display* on Linux, else null
    // Vulkan needs a SURFACE, not just a window: the Android path creates it with
    // SDL_Vulkan_CreateSurface and stores it here (VkSurfaceKHR), with the instance in nativeInstance
    // (VkInstance). Left null everywhere else -- bgfx uses nwh/ndt on desktop, and a null surface is
    // simply "no Vulkan surface", so nothing changes there.
    void*       m_pNativeSurface = nullptr;   // VkSurfaceKHR (Android Vulkan)
    void*       m_pNativeInstance = nullptr;  // VkInstance   (Android Vulkan)
    uint32_t    m_uWidth  = 1280;
    uint32_t    m_uHeight = 720;
    std::string m_strRenderer = "auto";         // auto | d3d11 | d3d12 | vulkan | opengl | gles
    bool        m_bVsync = true;
    bool        m_bDebugText = false;         // bgfx's on-screen stats overlay
    int         m_iMsaa = 0;                  // 0, 2, 4, 8, 16: multisampled backbuffer (3D viewers; the 2D game draws with none)
    // Present each frame as soon as it is rendered. bgfx otherwise presents a frame at the start of the
    // next bgfx::frame(): fine for a loop that draws all the time (the game), but a view that only redraws
    // when something changes (the editors' canvases) would show the frame before the last one.
    bool        m_bFlipAfterRender = false;
};

// Initializes bgfx in single-threaded mode (bgfx::renderFrame() before bgfx::init), which is what
// both the SDL loop and the Qt timer expect. On failure, `error` says why and what to try.
bool f_BgfxHostInit(const BgfxHostConfig& l_oCfg, std::string& l_strError);
void f_BgfxHostReset(uint32_t l_uWidth, uint32_t l_uHeight);
// The window's surface was recreated (Android, back from the background): draw on `nativeWindow` now.
void f_BgfxHostSetWindow(void* l_pNativeWindow, uint32_t l_uWidth, uint32_t l_uHeight);
uint32_t f_BgfxHostFrame();                  // bgfx::frame(): submit everything queued this frame; its frame number
void f_BgfxHostShutdown();
bool f_BgfxHostReady();
double f_BgfxHostGpuMs();                    // the GPU's time for the last frame bgfx measured (ms; 0 = unknown)
std::string f_BgfxHostRendererName();       // e.g. "Direct3D 11"; "(not started)" before init

void f_BgfxHostSetDebugText(bool l_bOn);
// The one-line performance HUD (F3 / --fps): bgfx debug text, top-left. Set it every frame.
void f_BgfxHostSetHud(bool l_bOn);
void f_BgfxHostHudText(const char* l_pszText);   // several lines: a newline between them
bool f_BgfxHostSupportsCompute();          // BGFX_CAPS_COMPUTE on the active backend

// Number of screenshots written by the bgfx callback (tests wait for this to change).
int f_BgfxHostScreenshotsWritten();

}  // namespace toms::next
