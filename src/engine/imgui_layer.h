// imgui_layer.h — Dear ImGui integration for the Vulkan+GLFW desktop renderer.
// See docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §2/§3 and
// docs/IMPLEMENTATION_ROADMAP.md Milestone 2 ("UI framework bring-up", dev-overlay only).
//
// Scope: Vulkan+GLFW desktop backend only, wired this milestone. The WebGL2/WebGPU
// (Emscripten) backends are deferred until the toolchain is available to build+test them
// (see docs/PROGRESS_REPORT.md) — this file is never compiled into the toms_web target.
//
// Owns a dedicated VkDescriptorPool so it never competes with the sprite/text renderer's own
// `dsPool` (src/engine/renderer.h) for capacity — purely additive, isolated from the existing
// render path except for the one hook point in Renderer::end() (renderer.h's `uiOverlayHook`).
#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

namespace toms {

class ImGuiLayer {
public:
    // Call once, after the Renderer (renderer.h) has finished Vulkan init (device, render pass,
    // command pool all valid — i.e. after Game::loadAssets()). Returns false (and logs to
    // stderr) on any failure; the caller should treat that as "no debug overlay this run",
    // not a fatal error.
    bool init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physical, VkDevice device,
              uint32_t queueFamily, VkQueue queue, VkRenderPass renderPass, uint32_t imageCount);

    // Call once, before the owning Renderer/VulkanContext is torn down (before Renderer::destroy()).
    void shutdown();

    // Per-frame sequence (see main.cpp):
    //   newFrame() -> build ImGui:: window content -> endFrame() -> Renderer::end() (which,
    //   inside its active render pass, invokes renderDrawData() via Renderer::uiOverlayHook).
    void newFrame();
    void endFrame();                            // finalizes ImGui's draw data (ImGui::Render())
    void renderDrawData(VkCommandBuffer cmd);    // records into an ALREADY-ACTIVE render pass

    bool initialized() const { return initialized_; }

private:
    bool initialized_ = false;
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
};

} // namespace toms
