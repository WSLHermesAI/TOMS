// renderer_webgpu.h — WebGPU (Dawn C API) backend for Emscripten (browser).
// Implements IRenderer. Compiled ONLY under __EMSCRIPTEN__ && WEBGPU.
// Mirrors the WebGL2 backend's quad-batching logic but uses WebGPU pipelines,
// bind groups, and a WGSL shader. Isolated from Vulkan and WebGL.
#pragma once
#include "render_iface.h"
#ifdef __EMSCRIPTEN__
#ifdef WEBGPU

#include <webgpu/webgpu.h>
#include <vector>
#include <string>
#include <cstdint>

class WebGPURenderer : public IRenderer {
public:
    WebGPURenderer();
    ~WebGPURenderer();
    void init(uint32_t w, uint32_t h) override;
    void loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) override;
    void loadFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) override;
    void begin() override;
    void drawSprite(const Quad& q) override;
    void drawText(const Quad& q) override;
    void end() override;
    uint32_t width() const override { return W_; }
    uint32_t height() const override { return H_; }

private:
    void ensureBuilt();                 // create pipeline/textures/buffers once device is ready
    void buildTexture(const std::vector<uint8_t>& data, uint32_t w, uint32_t h,
                      WGPUTexture* tex, WGPUTextureView* view, WGPUBindGroup* bg);
    void flush(std::vector<float>& verts, WGPUBindGroup bg);

    uint32_t W_ = 1024, H_ = 768;
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;
    bool ready = false;        // device acquired (async)
    bool built = false;        // pipeline/resources created

    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroupLayout bgl = nullptr;
    WGPUBuffer vbuf = nullptr;          // vertex buffer (dynamic)
    size_t vbufCap = 0;
    WGPUBuffer ubo = nullptr;           // resolution uniform (vec2)
    WGPUSampler sampler = nullptr;

    WGPUTexture spriteTex = nullptr, fontTex = nullptr;
    WGPUTextureView spriteView = nullptr, fontView = nullptr;
    WGPUBindGroup spriteBG = nullptr, fontBG = nullptr;

    // pending atlas data (device may not be ready when loadSprites/loadFont called)
    bool haveSprite = false, haveFont = false;
    std::vector<uint8_t> spriteData; uint32_t spriteW = 0, spriteH = 0;
    std::vector<uint8_t> fontData;    uint32_t fontW = 0, fontH = 0;

    std::vector<float> sprites_, texts_;
};

#endif // WEBGPU
#endif // __EMSCRIPTEN__
