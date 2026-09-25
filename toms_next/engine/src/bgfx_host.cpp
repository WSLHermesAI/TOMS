// bgfx_host.cpp -- see bgfx_host.h.
#include "bgfx_host.h"

#include <bgfx/bgfx.h>
#include <bx/allocator.h>
#include <bx/debug.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <vector>

// The legacy renderer.cpp owned the stb_image_write implementation; it is not compiled here.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace toms::next {
namespace {

bool g_ready = false;
bool g_vsync = true;
uint32_t g_debug = BGFX_DEBUG_NONE;
std::atomic<int> g_screenshots{0};

// bgfx reports fatal errors, traces and screenshots through this callback.
struct HostCallback : public bgfx::CallbackI {
    void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) override {
        std::fprintf(stderr, "[bgfx] FATAL %s(%u) code %d: %s\n", filePath, line, (int)code, str);
        std::fflush(stderr);
        if (code != bgfx::Fatal::DebugCheck) std::abort();
    }
    void traceVargs(const char* filePath, uint16_t line, const char* format, va_list args) override {
#ifndef NDEBUG
        char buf[2048];
        int n = std::snprintf(buf, sizeof(buf), "[bgfx] %s(%u): ", filePath, line);
        std::vsnprintf(buf + n, sizeof(buf) - n, format, args);
        std::fputs(buf, stderr);
#else
        (void)filePath; (void)line; (void)format; (void)args;
#endif
    }
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void screenShot(const char* filePath, uint32_t width, uint32_t height, uint32_t pitch,
                    bgfx::TextureFormat::Enum format, const void* data, uint32_t, bool yflip) override {
        // bgfx hands over BGRA8 rows; stb wants RGBA8.
        std::vector<uint8_t> rgba((size_t)width * height * 4);
        const auto* src = (const uint8_t*)data;
        const bool bgra = format != bgfx::TextureFormat::RGBA8;
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* row = src + (size_t)(yflip ? height - 1 - y : y) * pitch;
            uint8_t* dst = rgba.data() + (size_t)y * width * 4;
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t* p = row + x * 4;
                dst[x * 4 + 0] = bgra ? p[2] : p[0];
                dst[x * 4 + 1] = p[1];
                dst[x * 4 + 2] = bgra ? p[0] : p[2];
                dst[x * 4 + 3] = 255;
            }
        }
        if (stbi_write_png(filePath, (int)width, (int)height, 4, rgba.data(), (int)width * 4))
            std::fprintf(stderr, "[toms] screenshot written: %s (%ux%u)\n", filePath, width, height);
        else
            std::fprintf(stderr, "[toms] screenshot FAILED: %s\n", filePath);
        ++g_screenshots;
    }
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

HostCallback g_callback;

}  // namespace

// "d3d11", "d3d12", "vulkan", "opengl", "gles", "auto" -> bgfx::RendererType (Count = auto).
bgfx::RendererType::Enum parseRendererName(const std::string& in) {
    std::string n = in;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (n == "d3d11" || n == "dx11")    return bgfx::RendererType::Direct3D11;
    if (n == "d3d12" || n == "dx12")    return bgfx::RendererType::Direct3D12;
    if (n == "vulkan" || n == "vk")     return bgfx::RendererType::Vulkan;
    if (n == "opengl" || n == "gl")     return bgfx::RendererType::OpenGL;
    if (n == "gles" || n == "opengles") return bgfx::RendererType::OpenGLES;
    return bgfx::RendererType::Count;
}

// Device/frame settings go in the reset flags; per-surface settings (size, sRGB) in the SwapChain.
uint32_t bgfxHostResetFlags() { return g_vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE; }

// sRGB backbuffer, like the Vulkan swapchain (VK_FORMAT_B8G8R8A8_SRGB) the old renderer used.
constexpr uint32_t kSwapChainFlags = BGFX_SWAP_CHAIN_SRGB_BACKBUFFER;

bool bgfxHostInit(const BgfxHostConfig& cfg, std::string& error) {
    if (g_ready) return true;
    if (!cfg.nativeWindow) {
        error = "no native window handle";
        return false;
    }
    g_vsync = cfg.vsync;

    // Calling renderFrame() before init() makes bgfx render on this thread (no render thread).
    bgfx::renderFrame();

    bgfx::Init init;
    const bgfx::RendererType::Enum type = parseRendererName(cfg.renderer);
    init.type = type;
    init.swapChain.nwh    = cfg.nativeWindow;
    init.swapChain.ndt    = cfg.nativeDisplay;
    init.swapChain.width  = std::max<uint32_t>(cfg.width, 1);
    init.swapChain.height = std::max<uint32_t>(cfg.height, 1);
    init.swapChain.flags  = kSwapChainFlags;
    init.reset = bgfxHostResetFlags();
    init.callback = &g_callback;
    if (!bgfx::init(init)) {
        error = std::string("bgfx::init failed for renderer '") +
                (type == bgfx::RendererType::Count ? "auto" : bgfx::getRendererName(type)) +
                "'. Update the GPU driver, or start with --renderer=d3d11 (or opengl).";
        return false;
    }
    g_ready = true;
    bgfxHostSetDebugText(cfg.debugText);
    std::fprintf(stderr, "[toms] bgfx %d ready: renderer=%s\n", BGFX_API_VERSION,
                 bgfx::getRendererName(bgfx::getRendererType()));
    return true;
}

void bgfxHostReset(uint32_t width, uint32_t height) {
    if (!g_ready) return;
    bgfx::SwapChain sc;
    sc.width  = std::max<uint32_t>(width, 1);
    sc.height = std::max<uint32_t>(height, 1);
    sc.flags  = kSwapChainFlags;
    bgfx::reset(bgfxHostResetFlags(), &sc);
}

void bgfxHostShutdown() {
    if (!g_ready) return;
    bgfx::shutdown();
    g_ready = false;
}

bool bgfxHostReady() { return g_ready; }

void bgfxHostFrame() {
    if (g_ready) bgfx::frame();
}

std::string bgfxHostRendererName() {
    return g_ready ? bgfx::getRendererName(bgfx::getRendererType()) : "(not started)";
}

void bgfxHostSetDebugText(bool on) {
    g_debug = on ? BGFX_DEBUG_STATS : BGFX_DEBUG_NONE;
    if (g_ready) bgfx::setDebug(g_debug);
}

int bgfxHostScreenshotsWritten() { return g_screenshots.load(); }

}  // namespace toms::next
