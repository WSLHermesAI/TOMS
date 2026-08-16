// renderer.h — offscreen 2D sprite/text renderer (Vulkan, headless-capable).
// Implements IRenderer. Desktop-only; not compiled under Emscripten.
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstdint>
#include "vk_util.h"
#include "render_iface.h"
#include "texture.h"     // VkTextureRefs (Texture shares the renderer's Vulkan handles)

struct VulkanContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue gfxQueue = VK_NULL_HANDLE;
    uint32_t gfxFamily = 0;
    void init(uint32_t w, uint32_t h);
    void destroy();
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
    VkFramebuffer fb=VK_NULL_HANDLE;
    VkImage colorImg=VK_NULL_HANDLE; VkImageView colorView=VK_NULL_HANDLE;
    VkDescriptorSetLayout dsLayout=VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;
    VkDescriptorPool dsPool=VK_NULL_HANDLE;
    VkDescriptorSet spriteSet=VK_NULL_HANDLE, fontSet=VK_NULL_HANDLE;
    VkBuffer vbuf=VK_NULL_HANDLE; VkDeviceMemory vbufMem=VK_NULL_HANDLE; size_t vbufCap=0;
    VkBuffer ibuf=VK_NULL_HANDLE; VkDeviceMemory ibufMem=VK_NULL_HANDLE; size_t ibufCap=0;
    uint32_t lastDrawCalls=0;   // draw calls issued by the last end() (batch metric)
    size_t   lastQuadCount=0;   // quads batched by the last end() (batch metric)
    Atlas spriteAtlas_, fontAtlas_;
    // dummy 1x1 (vec4(0)) texture used by solid-color batches so the shader can
    // sample "nothing" instead of the real atlas.
    VkImage   solidImg_   = VK_NULL_HANDLE;
    VkImageView solidView_ = VK_NULL_HANDLE;
    VkDeviceMemory solidMem_ = VK_NULL_HANDLE;
    VkDescriptorSet solidSet_ = VK_NULL_HANDLE;
    std::vector<Quad> sprites, texts;

    void init(uint32_t w, uint32_t h) override;
    void destroy();   // free Vulkan resources (atlases, pools, layout, sampler, pipeline)
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
    uint32_t width()  const override { return W; }
    uint32_t height() const override { return H; }

    void uploadAtlas(const std::vector<uint8_t>& px, uint32_t w, uint32_t h,
                    VkImage& img, VkImageView& view, VkDescriptorSet& set, VkDeviceMemory& mem);
    void transitionImage(VkImage img, VkImageLayout from, VkImageLayout to, VkImageAspectFlags asp);
    void ensureVertexBuffer(size_t needBytes);
    void ensureIndexBuffer(size_t needBytes);
    // Bundle the Vulkan handles a Texture needs to create/upload its image + descriptor.
    VkTextureRefs textureRefs() const;
};
