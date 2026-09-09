// renderer.h — windowed 2D sprite/text renderer (Vulkan + GLFW).
// Implements IRenderer. Desktop-only; not compiled under Emscripten.
#pragma once
// GLFW_INCLUDE_NONE => don't pull GL/gl.h (avoids needing OpenGL dev headers on
// Windows/Linux). We include <vulkan/vulkan.h> explicitly ourselves.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
// glfwCreateWindowSurface is declared by GLFW only when GLFW_INCLUDE_VULKAN is set;
// GLFW_INCLUDE_VULKAN also forces GL/gl.h (OpenGL dev headers) which we don't have.
// Since we use GLFW_INCLUDE_NONE, declare the stable surface helper ourselves.
extern "C" GLFWAPI VkResult glfwCreateWindowSurface(VkInstance instance, GLFWwindow* window,
                                                    const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include "vk_util.h"
#include "render_iface.h"
#include "texture.h"     // VkTextureRefs (Texture shares the renderer's Vulkan handles)

struct VulkanContext {
    // Core
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue gfxQueue = VK_NULL_HANDLE;
    uint32_t gfxFamily = 0;

    // Window + surface
    GLFWwindow* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    // Swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_B8G8R8A8_SRGB;
    std::vector<VkImage>     swapImages;
    std::vector<VkImageView> swapViews;
    uint32_t swapImageCount = 0;

    // Per-frame sync
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    VkSemaphore imageAvailable[MAX_FRAMES_IN_FLIGHT] = {};
    VkSemaphore renderFinished[MAX_FRAMES_IN_FLIGHT] = {};
    VkFence     inFlight[MAX_FRAMES_IN_FLIGHT] = {};
    int currentFrame = 0;

    // Current acquired swapchain image index (filled by acquireNext)
    uint32_t currentImageIndex = 0;

    // Set by the GLFW framebuffer-size callback (window edge dragged) and by
    // acquireNext()/present() when the swapchain reports itself out of date.
    bool framebufferResized = false;

    void init(uint32_t w, uint32_t h);
    void destroy();
    bool shouldClose() const { return window && glfwWindowShouldClose(window); }
    void pollEvents() { glfwPollEvents(); }
    // Acquire next swapchain image; returns false if the swapchain needs recreating.
    bool acquireNext();
    // Present the rendered image; call after end().
    void present();
    // Tear down and recreate the swapchain + image views at the given size
    // (instance/device/surface are kept). Called after a window resize.
    void recreateSwapchainImages(uint32_t w, uint32_t h);
};

struct Atlas { VkImage img = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
               VkDescriptorSet set = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE;
               uint32_t w = 0, h = 0; };

class Renderer : public IRenderer {
public:
    uint32_t W=0, H=0;
    VulkanContext vk;
    VkCommandPool cmdPool=VK_NULL_HANDLE;
    VkRenderPass renderPass=VK_NULL_HANDLE;
    // Swapchain framebuffers (one per swap image)
    std::vector<VkFramebuffer> swapFBs;
    // Command buffers (one per swap image)
    std::vector<VkCommandBuffer> cmdBufs;
    VkDescriptorSetLayout dsLayout=VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;
    VkDescriptorPool dsPool=VK_NULL_HANDLE;
    VkDescriptorSet spriteSet=VK_NULL_HANDLE, fontSet=VK_NULL_HANDLE;
    VkBuffer vbuf=VK_NULL_HANDLE; VkDeviceMemory vbufMem=VK_NULL_HANDLE; size_t vbufCap=0;
    VkBuffer ibuf=VK_NULL_HANDLE; VkDeviceMemory ibufMem=VK_NULL_HANDLE; size_t ibufCap=0;
    uint32_t lastDrawCalls=0;
    size_t   lastQuadCount=0;
    // Milestone 2: optional hook invoked with the active command buffer, right before
    // vkCmdEndRenderPass in the normal (non-split) path — lets an owned ImGuiLayer
    // (imgui_layer.h) record its draw data into the same render-pass instance without this
    // class needing to know ImGui exists. No-op (std::function empty) unless someone sets it.
    std::function<void(VkCommandBuffer)> uiOverlayHook;
    // Bugfix: a window resize can recreate the swapchain with a *different* image count
    // (surface capabilities aren't guaranteed stable across a resize/monitor change), and
    // Dear ImGui's Vulkan backend bakes in whatever the image count was at ImGuiLayer::init()
    // time -- nothing ever told it the count changed later. Optional hook, invoked from
    // recreateSwapchain() whenever swapImageCount actually changes, so main.cpp can forward
    // it to ImGuiLayer::setMinImageCount() (the officially documented fix for exactly this).
    // No-op (std::function empty) unless someone sets it.
    std::function<void(uint32_t)> onSwapchainImageCountChanged;
    Atlas spriteAtlas_, fontAtlas_;
    VkImage   solidImg_   = VK_NULL_HANDLE;
    VkImageView solidView_ = VK_NULL_HANDLE;
    VkDeviceMemory solidMem_ = VK_NULL_HANDLE;
    VkDescriptorSet solidSet_ = VK_NULL_HANDLE;
    std::vector<Quad> sprites, texts;

    void init(uint32_t w, uint32_t h) override;
    void destroy();   // free Vulkan resources (atlases, pools, layout, sampler, pipeline)
    // Acquire the next swapchain image, transparently recreating the swapchain
    // (and dependent framebuffers) if the window was resized or the swapchain
    // reports itself out of date. Returns false if the frame should be skipped
    // (a recreation just happened, or the window is minimized).
    bool beginFrame();
    // Rebuild the swapchain + framebuffers to match the window's current size.
    void recreateSwapchain();
    void loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) override;
    void loadFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) override;
    // Re-upload the font atlas (after the Font grew / gained glyphs at runtime).
    // Frees the previous font image/view/set/mem first so it does not leak.
    void updateFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) override;
    void begin() override;
    void drawSprite(const Quad& q) override;
    void drawText(const Quad& q) override;
    void setNode(uint8_t n) override;   // diagnostic: tag subsequent quads (4-way split render)
    void setNodeFilter(uint8_t n) override;   // diagnostic: emit only quads of this node
    void end() override;
    void savePNG(const std::string& path) override;
    // Game logic (game.cpp) lays everything out against this FIXED design resolution, not the
    // live window size -- see kDesignW/kDesignH below. `W`/`H` (the actual device/swapchain
    // pixel size) are still used internally for the real framebuffer/swapchain, and for scaling
    // the viewport to fit the window (see computeAspectFitViewport / deviceToDesign).
    uint32_t width()  const override { return kDesignW; }
    uint32_t height() const override { return kDesignH; }

    // Fixed logical/design resolution every game-space pixel coordinate (tile size, HUD
    // positions, dialogue boxes, ...) is authored against -- matches the 1024x768 "buffer
    // space" convention already established for touch input (see stage.h's handleTouch doc
    // comment and the web build's toBP()). The actual window can be resized to any size/aspect
    // ratio; only the viewport (see computeAspectFitViewport) scales to fit it, letterboxed.
    static constexpr uint32_t kDesignW = 1024, kDesignH = 768;

    struct ViewportRect { float x, y, width, height; };
    // Computes a centered, aspect-correct viewport rect (in device/window pixels) that fits
    // kDesignW x kDesignH into a deviceW x deviceH window without stretching -- the same
    // "letterbox/pillarbox" technique as the reference cOpenGLRender::
    // SetAcceptRationWithGameresolution this was ported from (see PROGRESS_REPORT.md).
    static ViewportRect computeAspectFitViewport(uint32_t deviceW, uint32_t deviceH,
                                                  uint32_t targetW, uint32_t targetH);
    // Maps a device/window pixel coordinate (e.g. a GLFW cursor position, already converted to
    // framebuffer pixels) into this renderer's fixed kDesignW x kDesignH logical space,
    // accounting for the letterboxed viewport. Returns false (outputs left unchanged) if the
    // point falls in a letterbox bar, outside the actual rendered content.
    bool deviceToDesign(double deviceX, double deviceY, float& outX, float& outY) const;

    void uploadAtlas(const std::vector<uint8_t>& px, uint32_t w, uint32_t h,
                    VkImage& img, VkImageView& view, VkDescriptorSet& set, VkDeviceMemory& mem);
    void transitionImage(VkImage img, VkImageLayout from, VkImageLayout to, VkImageAspectFlags asp);
    void ensureVertexBuffer(size_t needBytes);
    void ensureIndexBuffer(size_t needBytes);
    // Bundle the Vulkan handles a Texture needs to create/upload its image + descriptor.
    VkTextureRefs textureRefs() const;
};
