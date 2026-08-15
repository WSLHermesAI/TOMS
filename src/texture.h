// texture.h — modern texture class for TOMS (reworked from FM79979
// Core/GameplayUT/Render/Texture/Texture.h + TextureManager.h).
//
// Differences from the FM79979 OpenGL version:
//   * No GL types — it uploads pixel data into a Vulkan VkImage (LINEAR, mapped)
//     and owns a VkDescriptorSet so it can be sampled by the sprite pipeline.
//   * Inherits `Object` (toms::Object) so every Texture is ref-counted via
//     shared_ptr, is leak-tracked, and reports a type/name.
//   * TextureManager is a singleton mapping name -> shared_ptr<Texture>, exactly
//     like the reference's m_NameAndSharedTextureMap (but modern RAII, no wchar_t).
//
// Pixel loading uses stb_image (external/stb/stb_image.h, already in the tree).

#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "object.h"

// Vulkan handles a Texture needs to create/upload its image + descriptor set.
// The Renderer fills this in once and passes it to every Texture it creates.
struct VkTextureRefs {
    VkDevice                device      = VK_NULL_HANDLE;
    VkPhysicalDevice        physical    = VK_NULL_HANDLE;
    VkQueue                 gfxQueue    = VK_NULL_HANDLE;
    VkCommandPool           cmdPool     = VK_NULL_HANDLE;
    VkDescriptorPool        dsPool      = VK_NULL_HANDLE;
    VkDescriptorSetLayout   dsLayout    = VK_NULL_HANDLE;
    VkSampler               sampler     = VK_NULL_HANDLE;
};

class Texture : public Object {
    TOMS_OBJECT(Texture)
public:
    Texture(const VkTextureRefs& refs, const std::string& name = "texture");
    virtual ~Texture();

    // Load from a file (PNG/JPG/...). Returns false on decode failure.
    bool LoadFromFile(const std::string& path);
    // Load from already-decoded RGBA8 pixels.
    bool LoadFromRGBA(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);

    // Update the GPU image with NEW pixel data (the core ask: "update pixel data to
    // the Vulkan rendering texture"). Same size => re-upload in place; different size
    // => the VkImage is recreated. Pixel format is always RGBA8.
    bool UpdatePixels(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);

    uint32_t width()  const { return w_; }
    uint32_t height() const { return h_; }
    uint32_t channels() const { return ch_; }
    VkDescriptorSet descriptorSet() const { return set_; }
    bool valid() const { return img_ != VK_NULL_HANDLE && set_ != VK_NULL_HANDLE; }

    // Release all GPU resources (called automatically on destruction).
    void destroy();

private:
    bool upload(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);
    void freeGpu();
    VkTextureRefs refs_;
    VkImage        img_      = VK_NULL_HANDLE;
    VkImageView    view_    = VK_NULL_HANDLE;
    VkDeviceMemory mem_      = VK_NULL_HANDLE;
    VkDescriptorSet set_     = VK_NULL_HANDLE;
    uint32_t w_ = 0, h_ = 0, ch_ = 4;
    bool freed_ = false;
};

// ---------------------------------------------------------------------------
// TextureManager — singleton, name -> shared_ptr<Texture>.
// ---------------------------------------------------------------------------
class TextureManager : public Object {
    TOMS_OBJECT(TextureManager)
public:
    static TextureManager& instance() { static TextureManager m; return m; }

    // Get an existing texture or load it from disk (cached by name/path).
    std::shared_ptr<Texture> GetOrLoad(const std::string& name,
                                       const VkTextureRefs& refs,
                                       const std::string& path);
    // Register an already-built texture under a name.
    void Add(const std::string& name, std::shared_ptr<Texture> tex);
    // Fetch a previously registered texture (nullptr if absent).
    std::shared_ptr<Texture> Get(const std::string& name) const;
    void Remove(const std::string& name);
    size_t size() const { return map_.size(); }

private:
    TextureManager() = default;
    std::unordered_map<std::string, std::shared_ptr<Texture>> map_;
};
