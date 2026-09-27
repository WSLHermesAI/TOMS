// bgfx_renderer.cpp -- see bgfx_renderer.h.
#include "bgfx_renderer.h"
#include "embedded_shaders.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace toms::next {
namespace {

// pos2, uv2, tint4, solid1 -- the four corners of a quad are expanded on the CPU.
struct SpriteVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
    float solid;
};

bgfx::VertexLayout& spriteLayout() {
    static bgfx::VertexLayout layout;
    static bool ready = false;
    if (!ready) {
        layout.begin()
            .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 1, bgfx::AttribType::Float)
            .end();
        ready = true;
    }
    return layout;
}

uint32_t packClearColor(const float c[4]) {
    auto b = [](float f) { return (uint32_t)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
    return (b(c[0]) << 24) | (b(c[1]) << 16) | (b(c[2]) << 8) | b(c[3]);
}

}  // namespace

BgfxRenderer::~BgfxRenderer() { destroy(); }

BgfxRenderer::ViewportRect BgfxRenderer::computeAspectFitViewport(uint32_t deviceW, uint32_t deviceH,
                                                                  uint32_t targetW, uint32_t targetH) {
    if (deviceW == 0 || deviceH == 0 || targetW == 0 || targetH == 0) return {0, 0, 0, 0};
    float scale = std::min((float)deviceW / (float)targetW, (float)deviceH / (float)targetH);
    float vw = (float)targetW * scale, vh = (float)targetH * scale;
    return {((float)deviceW - vw) / 2.0f, ((float)deviceH - vh) / 2.0f, vw, vh};
}

bool BgfxRenderer::deviceToDesign(double deviceX, double deviceY, float& outX, float& outY) const {
    ViewportRect fit = computeAspectFitViewport(devW_, devH_, kDesignW, kDesignH);
    if (fit.width <= 0.0f || fit.height <= 0.0f) return false;
    double lx = deviceX - fit.x, ly = deviceY - fit.y;
    if (lx < 0 || ly < 0 || lx > fit.width || ly > fit.height) return false;
    outX = (float)(lx / fit.width * kDesignW);
    outY = (float)(ly / fit.height * kDesignH);
    return true;
}

void BgfxRenderer::init(uint32_t, uint32_t) {
    bgfx::ProgramHandle prog = createEmbeddedProgram(ShaderProgram::Sprite);
    program_ = prog.idx;
    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler).idx;
    if (program_ == kInvalid)
        std::fprintf(stderr, "[toms] BgfxRenderer: sprite program failed to load\n");
}

void BgfxRenderer::destroy() {
    destroyTexture(spriteTex_);
    destroyTexture(fontTex_);
    if (program_ != kInvalid) { bgfx::destroy(bgfx::ProgramHandle{program_}); program_ = kInvalid; }
    if (sampler_ != kInvalid) { bgfx::destroy(bgfx::UniformHandle{sampler_}); sampler_ = kInvalid; }
}

void BgfxRenderer::destroyTexture(uint16_t& handle) {
    if (handle != kInvalid) { bgfx::destroy(bgfx::TextureHandle{handle}); handle = kInvalid; }
}

uint16_t BgfxRenderer::createAtlas(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0 || px.size() < (size_t)w * h * 4) return kInvalid;
    const uint32_t maxSize = bgfx::getCaps()->limits.maxTextureSize;
    if (w > maxSize || h > maxSize) {
        // The Vulkan/WebGL renderers failed silently here (no text at all); say so instead.
        std::fprintf(stderr, "[toms] atlas %ux%u exceeds the GPU limit %u -- not uploaded\n", w, h, maxSize);
        return kInvalid;
    }
#ifdef __EMSCRIPTEN__
    // WebGL has no sRGB backbuffer, so sampling sRGB textures would darken everything. Same as the
    // old WebGL renderer: plain RGBA8, blending in gamma space.
    const uint64_t flags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
#else
    const uint64_t flags = BGFX_TEXTURE_SRGB | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
#endif
    return bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::RGBA8, flags,
                                 bgfx::copy(px.data(), (uint32_t)((size_t)w * h * 4))).idx;
}

void BgfxRenderer::loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) {
    uint32_t aw = 0, ah = 0;
    std::vector<uint8_t> atlas;
    if (!packAtlas(layers, sw, sh, 9, atlas, aw, ah)) {
        std::fprintf(stderr, "[toms] loadSprites: bad layers\n");
        return;
    }
    destroyTexture(spriteTex_);
    spriteTex_ = createAtlas(atlas, aw, ah);
}

void BgfxRenderer::loadFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    destroyTexture(fontTex_);
    fontTex_ = createAtlas(px, w, h);
}

// bgfx::destroy is deferred until the GPU is done with the texture, so replacing it here is safe
// (the Vulkan renderer needed care for exactly this).
void BgfxRenderer::updateFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) { loadFont(px, w, h); }

void BgfxRenderer::begin() { sprites_.clear(); texts_.clear(); }
void BgfxRenderer::setNode(uint8_t n) { node_ = n; }
void BgfxRenderer::setNodeFilter(uint8_t n) { nodeFilter_ = n; }

void BgfxRenderer::drawSprite(const Quad& q) {
    if (nodeFilter_ && node_ != nodeFilter_) return;
    sprites_.push_back(q);
    sprites_.back().node = node_;
}

void BgfxRenderer::drawText(const Quad& q) {
    if (nodeFilter_ && node_ != nodeFilter_) return;
    texts_.push_back(q);
    texts_.back().node = node_;
}

void BgfxRenderer::end() {
    lastDrawCalls_ = 0;
    lastQuadCount_ = sprites_.size() + texts_.size();

    // View 0: clear the whole backbuffer (letterbox bars included) to the background colour.
    bgfx::setViewRect(kViewClear, 0, 0, (uint16_t)devW_, (uint16_t)devH_);
    bgfx::setViewClear(kViewClear, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, packClearColor(kBackgroundClearColor), 1.0f, 0);
    bgfx::touch(kViewClear);

    // View 1: the fixed design resolution, scaled and centred into the backbuffer.
    ViewportRect fit = computeAspectFitViewport(devW_, devH_, kDesignW, kDesignH);
    bgfx::setViewRect(kViewGame, (uint16_t)std::lround(fit.x), (uint16_t)std::lround(fit.y),
                      (uint16_t)std::lround(fit.width), (uint16_t)std::lround(fit.height));
    bgfx::setViewMode(kViewGame, bgfx::ViewMode::Sequential);   // keep submission order
    float proj[16];
    bx::mtxOrtho(proj, 0.0f, (float)kDesignW, (float)kDesignH, 0.0f, 0.0f, 100.0f, 0.0f,
                 bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(kViewGame, nullptr, proj);

    if (program_ == kInvalid) return;
    submitQuads(sprites_, spriteTex_);   // sprites first, then text: the same order as Vulkan/WebGL
    submitQuads(texts_, fontTex_);
}

void BgfxRenderer::submitQuads(const std::vector<Quad>& quads, uint16_t texture) {
    const bgfx::VertexLayout& layout = spriteLayout();
    size_t first = 0;
    while (first < quads.size()) {
        // Transient buffers are limited per frame; submit in chunks that fit (16-bit indices).
        uint32_t want = (uint32_t)std::min<size_t>(quads.size() - first, 65532 / 4);
        uint32_t availV = bgfx::getAvailTransientVertexBuffer(want * 4, layout) / 4;
        uint32_t availI = bgfx::getAvailTransientIndexBuffer(want * 6) / 6;
        uint32_t n = std::min({want, availV, availI});
        if (n == 0) {
            std::fprintf(stderr, "[toms] transient buffers full: %zu quads dropped\n", quads.size() - first);
            return;
        }
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, n * 4, layout);
        bgfx::allocTransientIndexBuffer(&tib, n * 6);
        auto* v = (SpriteVertex*)tvb.data;
        auto* idx = (uint16_t*)tib.data;
        for (uint32_t i = 0; i < n; ++i) {
            const Quad& q = quads[first + i];
            const float x0 = q.rect[0], y0 = q.rect[1], x1 = q.rect[0] + q.rect[2], y1 = q.rect[1] + q.rect[3];
            const float u0 = q.uv[0], v0 = q.uv[1], u1 = q.uv[2], v1 = q.uv[3];
            const float s = q.solid ? 1.0f : 0.0f;
            const float* t = q.tint;
            v[i * 4 + 0] = {x0, y0, u0, v0, t[0], t[1], t[2], t[3], s};
            v[i * 4 + 1] = {x1, y0, u1, v0, t[0], t[1], t[2], t[3], s};
            v[i * 4 + 2] = {x1, y1, u1, v1, t[0], t[1], t[2], t[3], s};
            v[i * 4 + 3] = {x0, y1, u0, v1, t[0], t[1], t[2], t[3], s};
            const uint16_t b = (uint16_t)(i * 4);
            idx[i * 6 + 0] = b; idx[i * 6 + 1] = b + 1; idx[i * 6 + 2] = b + 2;
            idx[i * 6 + 3] = b; idx[i * 6 + 4] = b + 2; idx[i * 6 + 5] = b + 3;
        }
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        if (texture != kInvalid)
            bgfx::setTexture(0, bgfx::UniformHandle{sampler_}, bgfx::TextureHandle{texture});
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        bgfx::submit(kViewGame, bgfx::ProgramHandle{program_});
        ++lastDrawCalls_;
        first += n;
    }
}

void BgfxRenderer::savePNG(const std::string& path) {
    // The host's bgfx callback (bgfx_host.cpp) writes the PNG a frame or two later.
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.c_str());
}

}  // namespace toms::next
