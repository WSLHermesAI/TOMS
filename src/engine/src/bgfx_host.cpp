// bgfx_host.cpp -- see bgfx_host.h.
#include "bgfx_host.h"
#include "color_space.h"   // TOMS_GAMMA_SPACE: web and Android blend in gamma space

#include <cstring>
#include <bgfx/bgfx.h>
#include <bx/allocator.h>
#include <bx/debug.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <vector>

// The one stb_image_write implementation (it used to live in the old Vulkan renderer.cpp).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace toms::next {
namespace {

bool g_bReady = false;
bool g_bVsync = true;
int g_iMsaa = 0;   // BgfxHostConfig::msaa
bool g_bFlipAfterRender = false;   // BgfxHostConfig::flipAfterRender
uint32_t g_uDebug = BGFX_DEBUG_NONE;
bool g_bStats = false, g_bHud = false;   // --stats overlay, F3 HUD: both make g_debug
std::atomic<int> g_oScreenshots{0};

// bgfx reports fatal errors, traces and screenshots through this callback.
struct HostCallback : public bgfx::CallbackI {
    void fatal(const char* l_pszFilePath, uint16_t l_u16Line, bgfx::Fatal::Enum l_eCode, const char* l_pszStr) override {
        std::fprintf(stderr, "[bgfx] FATAL %s(%u) code %d: %s\n", l_pszFilePath, l_u16Line, (int)l_eCode, l_pszStr);
        std::fflush(stderr);
        if (l_eCode != bgfx::Fatal::DebugCheck) std::abort();
    }
    void traceVargs(const char* l_pszFilePath, uint16_t l_u16Line, const char* l_pszFormat, va_list l_pszArgs) override {
#ifndef NDEBUG
        char l_pszBuf[2048];
        int l_iN = std::snprintf(l_pszBuf, sizeof(l_pszBuf), "[bgfx] %s(%u): ", l_pszFilePath, l_u16Line);
        std::vsnprintf(l_pszBuf + l_iN, sizeof(l_pszBuf) - l_iN, l_pszFormat, l_pszArgs);
        std::fputs(l_pszBuf, stderr);
#else
        (void)l_pszFilePath; (void)l_u16Line; (void)l_pszFormat; (void)l_pszArgs;
#endif
    }
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void screenShot(const char* l_pszFilePath, uint32_t l_uWidth, uint32_t l_uHeight, uint32_t l_uPitch,
                    bgfx::TextureFormat::Enum l_eFormat, const void* l_pData, uint32_t, bool l_bYflip) override {
        // bgfx hands over BGRA8 rows; stb wants RGBA8.
        std::vector<uint8_t> l_vRgba((size_t)l_uWidth * l_uHeight * 4);
        const auto* l_pSrc = (const uint8_t*)l_pData;
        const bool l_bBgra = l_eFormat != bgfx::TextureFormat::RGBA8;
        for (uint32_t l_uY = 0; l_uY < l_uHeight; ++l_uY) {
            const uint8_t* l_pRow = l_pSrc + (size_t)(l_bYflip ? l_uHeight - 1 - l_uY : l_uY) * l_uPitch;
            uint8_t* l_pDst = l_vRgba.data() + (size_t)l_uY * l_uWidth * 4;
            for (uint32_t l_uX = 0; l_uX < l_uWidth; ++l_uX) {
                const uint8_t* l_pP = l_pRow + l_uX * 4;
                l_pDst[l_uX * 4 + 0] = l_bBgra ? l_pP[2] : l_pP[0];
                l_pDst[l_uX * 4 + 1] = l_pP[1];
                l_pDst[l_uX * 4 + 2] = l_bBgra ? l_pP[0] : l_pP[2];
                l_pDst[l_uX * 4 + 3] = 255;
            }
        }
        if (stbi_write_png(l_pszFilePath, (int)l_uWidth, (int)l_uHeight, 4, l_vRgba.data(), (int)l_uWidth * 4))
            std::fprintf(stderr, "[toms] screenshot written: %s (%ux%u)\n", l_pszFilePath, l_uWidth, l_uHeight);
        else
            std::fprintf(stderr, "[toms] screenshot FAILED: %s\n", l_pszFilePath);
        ++g_oScreenshots;
    }
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

HostCallback g_oCallback;

}  // namespace

// "d3d11", "d3d12", "vulkan", "opengl", "gles", "auto" -> bgfx::RendererType (Count = auto).
bgfx::RendererType::Enum f_ParseRendererName(const std::string& l_strIn) {
    std::string l_strN = l_strIn;
    std::transform(l_strN.begin(), l_strN.end(), l_strN.begin(), [](unsigned char l_u8C) { return (char)std::tolower(l_u8C); });
    if (l_strN == "d3d11" || l_strN == "dx11")    return bgfx::RendererType::Direct3D11;
    if (l_strN == "d3d12" || l_strN == "dx12")    return bgfx::RendererType::Direct3D12;
    if (l_strN == "vulkan" || l_strN == "vk")     return bgfx::RendererType::Vulkan;
    if (l_strN == "opengl" || l_strN == "gl")     return bgfx::RendererType::OpenGL;
    if (l_strN == "gles" || l_strN == "opengles") return bgfx::RendererType::OpenGLES;
    return bgfx::RendererType::Count;
}

// Device/frame settings go in the reset flags; per-surface settings (size, sRGB) in the SwapChain.
uint32_t f_BgfxHostResetFlags() {
    uint32_t l_uF = g_bVsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    if (g_bFlipAfterRender) l_uF |= BGFX_RESET_FLIP_AFTER_RENDER;
    switch (g_iMsaa) {
    case 2: l_uF |= BGFX_RESET_MSAA_X2; break;
    case 4: l_uF |= BGFX_RESET_MSAA_X4; break;
    case 8: l_uF |= BGFX_RESET_MSAA_X8; break;
    case 16: l_uF |= BGFX_RESET_MSAA_X16; break;
    default: break;
    }
    return l_uF;
}

// sRGB backbuffer, like the Vulkan swapchain (VK_FORMAT_B8G8R8A8_SRGB) the old renderer used. The web and Android
// render in gamma space (color_space.h): WebGL has no sRGB backbuffer, and Android's GL ES may not give one.
#if TOMS_GAMMA_SPACE
constexpr uint32_t g_uSwapChainFlags = BGFX_SWAP_CHAIN_NONE;
#else
constexpr uint32_t g_uSwapChainFlags = BGFX_SWAP_CHAIN_SRGB_BACKBUFFER;
#endif

bool f_BgfxHostInit(const BgfxHostConfig& l_oCfg, std::string& l_strError) {
    if (g_bReady) return true;
    if (!l_oCfg.m_pNativeWindow) {
        l_strError = "no native window handle";
        return false;
    }
    g_bVsync = l_oCfg.m_bVsync;
    g_iMsaa = l_oCfg.m_iMsaa;
    g_bFlipAfterRender = l_oCfg.m_bFlipAfterRender;

    // Calling renderFrame() before init() makes a multi-threaded bgfx render on this thread (no
    // render thread). A single-threaded bgfx (Emscripten) already does, and its debug build asserts
    // "only makes sense if used with multi-threaded renderer" if this is called.
#if BGFX_CONFIG_MULTITHREADED
    bgfx::renderFrame();
#endif

    bgfx::Init l_oInit;
    const bgfx::RendererType::Enum l_eType = f_ParseRendererName(l_oCfg.m_strRenderer);
    l_oInit.type = l_eType;
    l_oInit.swapChain.nwh    = l_oCfg.m_pNativeWindow;
    l_oInit.swapChain.ndt    = l_oCfg.m_pNativeDisplay;
    l_oInit.swapChain.width  = std::max<uint32_t>(l_oCfg.m_uWidth, 1);
    l_oInit.swapChain.height = std::max<uint32_t>(l_oCfg.m_uHeight, 1);
    l_oInit.swapChain.flags  = g_uSwapChainFlags;
    l_oInit.reset = f_BgfxHostResetFlags();
    l_oInit.callback = &g_oCallback;
    if (!bgfx::init(l_oInit)) {
        l_strError = std::string("bgfx::init failed for renderer '") +
                (l_eType == bgfx::RendererType::Count ? "auto" : bgfx::getRendererName(l_eType)) +
                "'. Update the GPU driver, or start with --renderer=d3d11 (or opengl).";
        return false;
    }
    g_bReady = true;
    f_BgfxHostSetDebugText(l_oCfg.m_bDebugText);
    std::fprintf(stderr, "[toms] bgfx %d ready: renderer=%s\n", BGFX_API_VERSION,
                 bgfx::getRendererName(bgfx::getRendererType()));
    return true;
}

void f_BgfxHostReset(uint32_t l_uWidth, uint32_t l_uHeight) {
    if (!g_bReady) return;
    bgfx::SwapChain l_oSc;
    l_oSc.width  = std::max<uint32_t>(l_uWidth, 1);
    l_oSc.height = std::max<uint32_t>(l_uHeight, 1);
    l_oSc.flags  = g_uSwapChainFlags;
    bgfx::reset(f_BgfxHostResetFlags(), &l_oSc);
}

void f_BgfxHostSetWindow(void* l_pNativeWindow, uint32_t l_uWidth, uint32_t l_uHeight) {
    if (!g_bReady || !l_pNativeWindow) return;
    bgfx::SwapChain l_oSc;
    l_oSc.nwh    = l_pNativeWindow;   // GlContext::resize() makes the new EGL surface on it (see TomsDependencies.cmake)
    l_oSc.width  = std::max<uint32_t>(l_uWidth, 1);
    l_oSc.height = std::max<uint32_t>(l_uHeight, 1);
    l_oSc.flags  = g_uSwapChainFlags;
    bgfx::reset(f_BgfxHostResetFlags(), &l_oSc);
}

void f_BgfxHostShutdown() {
    if (!g_bReady) return;
    bgfx::shutdown();
    g_bReady = false;
}

bool f_BgfxHostReady() { return g_bReady; }

uint32_t f_BgfxHostFrame() {
    return g_bReady ? bgfx::frame() : 0;
}

double f_BgfxHostGpuMs() {
    if (!g_bReady) return 0;
    const bgfx::Stats* l_pS = bgfx::getStats();
    return l_pS && l_pS->gpuTimerFreq > 0 ? double(l_pS->gpuTimeEnd - l_pS->gpuTimeBegin) * 1000.0 / double(l_pS->gpuTimerFreq) : 0;
}

std::string f_BgfxHostRendererName() {
    return g_bReady ? bgfx::getRendererName(bgfx::getRendererType()) : "(not started)";
}

namespace {
void f_ApplyDebug() {
    g_uDebug = (g_bStats ? BGFX_DEBUG_STATS : 0u) | (g_bHud ? BGFX_DEBUG_TEXT : 0u);
    if (g_bReady) bgfx::setDebug(g_uDebug);
}
}  // namespace

void f_BgfxHostSetDebugText(bool l_bOn) {
    g_bStats = l_bOn;
    f_ApplyDebug();
}

void f_BgfxHostSetHud(bool l_bOn) {
    g_bHud = l_bOn;
    f_ApplyDebug();
}

void f_BgfxHostHudText(const char* l_pszText) {
    if (!g_bReady || !g_bHud) return;
    bgfx::dbgTextClear();
    uint16_t l_u16Row = 1;   // one line per newline
    for (const char* l_pszP = l_pszText; *l_pszP && l_u16Row < 64; l_u16Row++) {
        const char* l_pszE = std::strchr(l_pszP, '\n');
        const int l_iN = l_pszE ? int(l_pszE - l_pszP) : int(std::strlen(l_pszP));
        bgfx::dbgTextPrintf(1, l_u16Row, 0x0f, "%.*s", l_iN, l_pszP);
        if (!l_pszE) break;
        l_pszP = l_pszE + 1;
    }
}

bool f_BgfxHostSupportsCompute() {
    return g_bReady && (bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE) != 0;
}

int f_BgfxHostScreenshotsWritten() { return g_oScreenshots.load(); }

}  // namespace toms::next
