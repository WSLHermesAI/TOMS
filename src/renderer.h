// renderer.h — offscreen 2D sprite/text renderer (Vulkan, headless-capable).
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstdint>
#include "vk_util.h"

struct Quad {
    float rect[4];     // x,y,w,h in pixels (dst)
    float uv[4];       // u0,v0,u1,v1 (src atlas)
    float tint[4];     // rgba
};

struct VulkanContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue gfxQueue = VK_NULL_HANDLE;
    uint32_t gfxFamily = 0;
    void init(uint32_t w, uint32_t h);
    void destroy();
};

struct Atlas { VkImage img = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkDescriptorSet set = VK_NULL_HANDLE;
               uint32_t w=0, h=0; };

class Renderer {
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
    Atlas spriteAtlas_, fontAtlas_;
    std::vector<Quad> sprites, texts;

    void init(uint32_t w, uint32_t h);
    void uploadAtlas(const std::vector<uint8_t>& px, uint32_t w, uint32_t h, VkImage& img, VkImageView& view, VkDescriptorSet& set);
    void loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh);
    void loadFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h);
    void begin();
    void drawSprite(const Quad& q);
    void drawText(const Quad& q);
    void end();
    void savePNG(const std::string& path);
    void transitionImage(VkImage img, VkImageLayout from, VkImageLayout to, VkImageAspectFlags asp);
    void ensureVertexBuffer(size_t needBytes);
};

// Sprite index constants (order matches Game::loadAssets)
enum SpriteIdx {
    SP_FLOOR=0, SP_WALL, SP_STAIRS_UP, SP_STAIRS_DOWN, SP_DOOR_YELLOW, SP_DOOR_BLUE,
    SP_DOOR_RED, SP_KEY_YELLOW, SP_KEY_BLUE, SP_KEY_RED, SP_PLAYER, SP_SLIME,
    SP_BAT, SP_GOLEM, SP_SKELETON, SP_WRAITH, SP_DEMON, SP_VILLAGER, SP_SORCERER,
    SP_KING, SP_PRINCESS, SP_BOSS, SP_HP_POTION, SP_ATK_GEM, SP_DEF_GEM, SP_GOLD,
    SP_COUNT
};
