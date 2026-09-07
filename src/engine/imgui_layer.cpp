#include "imgui_layer.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <cstdio>

namespace toms {

bool ImGuiLayer::init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                       uint32_t queueFamily, VkQueue queue,
                       VkRenderPass renderPass, uint32_t imageCount) {
    if (initialized_) return true;
    device_ = device;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        fprintf(stderr, "[imgui] ImGui_ImplGlfw_InitForVulkan failed\n");
        ImGui::DestroyContext();
        return false;
    }

    // A dedicated, modestly-sized descriptor pool: ImGui allocates one combined-image-sampler
    // descriptor per font/user texture. 100 sets per type is comfortably oversized for a
    // debug overlay (a handful of windows), versus the 1000-per-type pool ImGui's own examples
    // use for a full application UI.
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 },
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 100u * (uint32_t)(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.poolSizeCount = (uint32_t)(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descPool_) != VK_SUCCESS) {
        fprintf(stderr, "[imgui] vkCreateDescriptorPool failed\n");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physical;
    initInfo.Device = device;
    initInfo.QueueFamily = queueFamily;
    initInfo.Queue = queue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = descPool_;
    initInfo.RenderPass = renderPass;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = imageCount < 2 ? 2 : imageCount;
    initInfo.ImageCount = imageCount < 2 ? 2 : imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = [](VkResult r) {
        if (r != VK_SUCCESS) fprintf(stderr, "[imgui] Vulkan backend error: %d\n", (int)r);
    };

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        fprintf(stderr, "[imgui] ImGui_ImplVulkan_Init failed\n");
        vkDestroyDescriptorPool(device, descPool_, nullptr); descPool_ = VK_NULL_HANDLE;
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    // This backend version creates/uploads the font atlas texture using its own internal
    // command pool + immediate submit (no caller-provided command buffer needed).
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        fprintf(stderr, "[imgui] ImGui_ImplVulkan_CreateFontsTexture failed\n");
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(device, descPool_, nullptr); descPool_ = VK_NULL_HANDLE;
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    initialized_ = true;
    return true;
}

void ImGuiLayer::shutdown() {
    if (!initialized_) return;
    vkDeviceWaitIdle(device_);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (descPool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descPool_, nullptr); descPool_ = VK_NULL_HANDLE; }
    initialized_ = false;
}

void ImGuiLayer::newFrame() {
    if (!initialized_) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    if (!initialized_) return;
    ImGui::Render();
}

void ImGuiLayer::renderDrawData(VkCommandBuffer cmd) {
    if (!initialized_) return;
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd) ImGui_ImplVulkan_RenderDrawData(dd, cmd);
}

} // namespace toms
