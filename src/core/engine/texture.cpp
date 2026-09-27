// texture.cpp — implementation of Texture / TextureManager (Vulkan + stb_image).
#include "texture.h"
#include "vk_util.h"          // createImage / beginOnce / endSubmit / vk_check
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Texture
// ---------------------------------------------------------------------------
Texture::Texture(const VkTextureRefs& refs, const std::string& name)
    : refs_(refs) { SetName(name); }

Texture::~Texture() {
    if (freed_) return;
    freed_ = true;
    destroy();
}

void Texture::freeGpu() {
    if (!refs_.device || refs_.device == VK_NULL_HANDLE) return;
    if (view_ != VK_NULL_HANDLE) { vkDestroyImageView(refs_.device, view_, nullptr); view_ = VK_NULL_HANDLE; }
    if (img_  != VK_NULL_HANDLE) { vkDestroyImage(refs_.device, img_, nullptr);       img_  = VK_NULL_HANDLE; }
    if (mem_  != VK_NULL_HANDLE) { vkFreeMemory(refs_.device, mem_, nullptr);         mem_  = VK_NULL_HANDLE; }
}

void Texture::destroy() { freeGpu(); }

// Upload RGBA8 pixels into a LINEAR VkImage and build a combined sampler descriptor.
// Mirrors Renderer::uploadAtlas (avoids lavapipe copy bug via a mapped linear image).
bool Texture::upload(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    if (!refs_.device || rgba.empty() || w == 0 || h == 0) return false;
    freeGpu();

    // recreate the image (handles size changes from UpdatePixels)
    createImage(refs_.device, refs_.physical, w, h, VK_FORMAT_R8G8B8A8_SRGB,
                VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                img_, view_, 1, &mem_, 0, VK_IMAGE_TILING_LINEAR, true);

    // Undefined -> GENERAL (linear images can be mapped and written from CPU)
    VkCommandBuffer cb = beginOnce(refs_.device, refs_.cmdPool);
    VkImageMemoryBarrier bar{}; bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    bar.image = img_; bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    bar.srcAccessMask = 0; bar.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    endSubmit(refs_.device, refs_.gfxQueue, refs_.cmdPool, cb);

    // map + write pixels with row pitch
    VkImageSubresource sr{}; sr.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout lay{}; vkGetImageSubresourceLayout(refs_.device, img_, &sr, &lay);
    void* p = nullptr; vkMapMemory(refs_.device, mem_, 0, lay.size, 0, &p);
    const uint8_t* src = rgba.data();
    for (uint32_t y = 0; y < h; y++)
        memcpy((uint8_t*)p + lay.offset + y * lay.rowPitch, src + y * w * 4, w * 4);
    vkUnmapMemory(refs_.device, mem_);

    // GENERAL -> SHADER_READ_ONLY_OPTIMAL for sampling
    cb = beginOnce(refs_.device, refs_.cmdPool);
    bar.oldLayout = VK_IMAGE_LAYOUT_GENERAL; bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bar.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    endSubmit(refs_.device, refs_.gfxQueue, refs_.cmdPool, cb);

    // descriptor set (sampled image)
    VkDescriptorSet s; VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = refs_.dsPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &refs_.dsLayout;
    vk_check(vkAllocateDescriptorSets(refs_.device, &ai, &s), "texture ads");
    VkDescriptorImageInfo di{}; di.imageView = view_; di.sampler = refs_.sampler; di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{}; wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wd.dstSet = s; wd.dstBinding = 0;
    wd.descriptorCount = 1; wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wd.pImageInfo = &di;
    vkUpdateDescriptorSets(refs_.device, 1, &wd, 0, nullptr);
    set_ = s;

    w_ = w; h_ = h; ch_ = 4;
    return true;
}

bool Texture::LoadFromFile(const std::string& path) {
    int c = 0; uint8_t* data = stbi_load(path.c_str(), (int*)&w_, (int*)&h_, &c, 4);
    if (!data) return false;
    std::vector<uint8_t> rgba(data, data + (size_t)w_ * h_ * 4);
    stbi_image_free(data);
    ch_ = 4;
    return upload(rgba, w_, h_);
}

bool Texture::LoadFromRGBA(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    return upload(rgba, w, h);
}

bool Texture::UpdatePixels(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    // re-upload (upload() frees the old image first, so this handles size changes)
    return upload(rgba, w, h);
}

// ---------------------------------------------------------------------------
// TextureManager
// ---------------------------------------------------------------------------
std::shared_ptr<Texture> TextureManager::GetByFullPath(const std::string& path,
                                                       const VkTextureRefs& refs) {
    auto it = map_.find(path);
    if (it != map_.end()) return it->second;          // already cached -> return existing
    auto tex = std::make_shared<Texture>(refs, path);  // path is the logical name
    if (!tex->LoadFromFile(path)) return nullptr;      // load failure -> null (not cached)
    map_[path] = tex;
    return tex;
}

void TextureManager::Add(const std::string& name, std::shared_ptr<Texture> tex) {
    map_[name] = tex;
}

std::shared_ptr<Texture> TextureManager::Get(const std::string& name) const {
    auto it = map_.find(name);
    return it == map_.end() ? nullptr : it->second;
}

void TextureManager::Remove(const std::string& name) {
    map_.erase(name);
}

void TextureManager::Clear() {
    map_.clear();
}
