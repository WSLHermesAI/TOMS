// vk_util.h — minimal Vulkan helpers for the offscreen 2D game renderer.
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static inline void vk_check(VkResult r, const char* msg) {
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan error %d: %s\n", (int)r, msg);
        std::abort();
    }
}

// Load a precompiled SPIR-V module. Tries several candidate locations so the
// binary works from the project root (Linux) and from a nested Visual Studio
// output dir (Windows) without extra setup.
static inline VkShaderModule loadSpv(VkDevice dev, const char* name) {
    const char* candidates[] = {"./",
        "shaders/", "src/shaders/", "src/engine/shaders/",
        "../src/shaders/", "../../src/shaders/",
        "../shaders/", "../../shaders/"
    };
    for (const char* base : candidates) {
        std::string p = std::string(base) + name;
        FILE* f = std::fopen(p.c_str(), "rb");
        if (f) {
            std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
            std::vector<uint32_t> code(sz / 4 + 1);
            std::fread(code.data(), 1, sz, f); std::fclose(f);
            VkShaderModuleCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            ci.codeSize = sz; ci.pCode = code.data();
            VkShaderModule m; vk_check(vkCreateShaderModule(dev, &ci, nullptr, &m), "shader module");
            return m;
        }
    }
    std::fprintf(stderr, "cannot open spv %s (tried multiple paths)\n", name);
    std::abort();
}

// One-shot command submit helper.
static inline VkCommandBuffer beginOnce(VkDevice dev, VkCommandPool pool) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &ai, &cb);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &bi); return cb;
}
static inline void endSubmit(VkDevice dev, VkQueue q, VkCommandPool pool, VkCommandBuffer cb) {
    fprintf(stderr, "[dbg] endCmdBuffer\n");
    vkEndCommandBuffer(cb);
    fprintf(stderr, "[dbg] queueSubmit\n");
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vk_check(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE), "submit");
    fprintf(stderr, "[dbg] queueWaitIdle\n");
    vkQueueWaitIdle(q);
    fprintf(stderr, "[dbg] freeCmdBuf\n");
    vkFreeCommandBuffers(dev, pool, 1, &cb);
    fprintf(stderr, "[dbg] endSubmit done\n");
}

// Create a Vulkan image + view + (optional) sampled.
static inline void createImage(VkDevice dev, VkPhysicalDevice phys, uint32_t w, uint32_t h,
                               VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                               VkImage& img, VkImageView& view, uint32_t layers = 1,
                               VkDeviceMemory* outMem = nullptr,
                               uint32_t memType = 0,
                               VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
                               bool hostVisible = false) {
    VkImageCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent = {w, h, 1};
    ci.mipLevels = 1; ci.arrayLayers = layers; ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = tiling; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk_check(vkCreateImage(dev, &ci, nullptr, &img), "create image");
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr);
    VkPhysicalDeviceMemoryProperties pdmp; vkGetPhysicalDeviceMemoryProperties(phys, &pdmp);
    uint32_t mi = 0; bool found = false;
    if (hostVisible) {
        // pick HOST_VISIBLE | HOST_COHERENT so CPU writes are GPU-visible
        for (uint32_t i = 0; i < pdmp.memoryTypeCount; i++) {
            if ((mr.memoryTypeBits & (1u << i)) &&
                (pdmp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (pdmp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                mi = i; found = true; break;
            }
        }
    } else {
        for (uint32_t i = 0; i < pdmp.memoryTypeCount; i++) {
            if ((mr.memoryTypeBits & (1u << i)) &&
                (pdmp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                mi = i; found = true; break;
            }
        }
    }
    if (!found) mi = memType;
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size; ai.memoryTypeIndex = mi;
    VkDeviceMemory mem; vk_check(vkAllocateMemory(dev, &ai, nullptr, &mem), "alloc image mem");
    vkBindImageMemory(dev, img, mem, 0);
    if (outMem) *outMem = mem;
    VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img; vi.viewType = (layers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {aspect, 0, 1, 0, layers};
    vk_check(vkCreateImageView(dev, &vi, nullptr, &view), "image view");
}
