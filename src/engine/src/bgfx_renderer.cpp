// bgfx_renderer.cpp -- see bgfx_renderer.h.
#include "bgfx_renderer.h"
#include "color_space.h"   // TOMS_GAMMA_SPACE: web and Android blend in gamma space
#include "embedded_shaders.h"
#include "shared_textures.h"

#include <bgfx/bgfx.h>

#include <vector>
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

// One instance of the GPU path: what vs_sprite_inst.sc reads as i_data0..3.
struct QuadInstance {
    float c0x, c0y, c1x, c1y;   // corners 0, 1
    float c2x, c2y, c3x, c3y;   // corners 2, 3
    float u0, v0, u1, v1;       // uv rect; u0 = -1: a solid quad (tint only)
    float r, g, b, a;
};
static_assert(sizeof(QuadInstance) == 64, "instance stride must be a multiple of 16 bytes");

// Compute path layouts: the input is 4 vec4 per quad, the output 3 vec4 per vertex.
bgfx::VertexLayout& csInLayout() {
    static bgfx::VertexLayout layout;
    static bool ready = false;
    if (!ready) {
        layout.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();   // one vec4 element
        ready = true;
    }
    return layout;
}
bgfx::VertexLayout& csOutLayout() {
    static bgfx::VertexLayout layout;
    static bool ready = false;
    if (!ready) {
        layout.begin()
            .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)   // x, y, u, v
            .add(bgfx::Attrib::Color1,    4, bgfx::AttribType::Float)   // tint
            .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)   // solid, 0, 0, 0
            .end();
        ready = true;
    }
    return layout;
}
constexpr uint32_t kChunk = 16384;   // quads per draw on the CPU / compute paths: 65536 vertices, 16-bit indices

// The instancing path's per-quad data in a vertex buffer: 4 vec4 = 64 bytes (QuadInstance).
bgfx::VertexLayout& instanceLayout() {
    static bgfx::VertexLayout layout;
    static bool ready = false;
    if (!ready) {
        layout.begin()
            .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
            .end();
        ready = true;
    }
    return layout;
}
constexpr uint16_t kComputeBufferFlags = BGFX_BUFFER_NONE;   // this bgfx takes the element format from the layout (vec4)

// The shared unit quad: just its corner, (0,0) (1,0) (1,1) (0,1).
bgfx::VertexLayout& unitQuadLayout() {
    static bgfx::VertexLayout layout;
    static bool ready = false;
    if (!ready) {
        layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();
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
    // The CPU and compute paths' index buffer: kChunk quads, 0 1 2 0 2 3 per quad.
    {
        static std::vector<uint16_t> indices;
        if (indices.empty())
            for (uint32_t q = 0; q < kChunk; q++)
                for (uint16_t k : {0, 1, 2, 0, 2, 3}) indices.push_back((uint16_t)(q * 4 + k));
        chunkIb_ = bgfx::createIndexBuffer(bgfx::makeRef(indices.data(), (uint32_t)(indices.size() * sizeof(uint16_t)))).idx;
    }
    // The GPU path (instancing): every backend this bgfx supports can instance, so it is on whenever
    // the instanced program exists for the active backend.
    instProgram_ = createEmbeddedProgram(ShaderProgram::SpriteInstanced).idx;
    if (instProgram_ != kInvalid) {
        static const float corners[8] = {0, 0, 1, 0, 1, 1, 0, 1};
        static const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        quadVb_ = bgfx::createVertexBuffer(bgfx::makeRef(corners, sizeof(corners)), unitQuadLayout()).idx;
        quadIb_ = bgfx::createIndexBuffer(bgfx::makeRef(indices, sizeof(indices))).idx;
    }
    // The compute path: only where the backend runs compute shaders and has the programs.
    if (bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE) {
        csProgram_ = createEmbeddedProgram(ShaderProgram::SpriteCompute).idx;
        csDrawProgram_ = createEmbeddedProgram(ShaderProgram::SpriteFromCompute).idx;
        if (csProgram_ != kInvalid && csDrawProgram_ != kInvalid) {
            csParams_ = bgfx::createUniform("u_spriteParams", bgfx::UniformType::Vec4).idx;
            // GPU-simulated particles: the same compute support, two more programs.
            fxSpawn_ = createEmbeddedProgram(ShaderProgram::FxSpawn).idx;
            fxUpdate_ = createEmbeddedProgram(ShaderProgram::FxUpdate).idx;
            if (fxSpawn_ != kInvalid && fxUpdate_ != kInvalid) {
                uFxSim_ = bgfx::createUniform("u_fxSim", bgfx::UniformType::Vec4).idx;
                uFxForce_ = bgfx::createUniform("u_fxForce", bgfx::UniformType::Vec4).idx;
                uFxOrigin_ = bgfx::createUniform("u_fxOrigin", bgfx::UniformType::Vec4).idx;
                uFxXform_ = bgfx::createUniform("u_fxXform", bgfx::UniformType::Vec4, 2).idx;
                uFxTint_ = bgfx::createUniform("u_fxTint", bgfx::UniformType::Vec4).idx;
                uFxColorLut_ = bgfx::createUniform("u_fxColorLut", bgfx::UniformType::Vec4, GpuParticleFrame::kLut).idx;
                uFxScalarLut_ = bgfx::createUniform("u_fxScalarLut", bgfx::UniformType::Vec4, GpuParticleFrame::kLut).idx;
                uFxFrameUv_ = bgfx::createUniform("u_fxFrameUv", bgfx::UniformType::Vec4, GpuParticleFrame::kMaxFrames).idx;
                uFxFrameRect_ = bgfx::createUniform("u_fxFrameRect", bgfx::UniformType::Vec4, GpuParticleFrame::kMaxFrames).idx;
            }
        }
    }
    // Layers (beginLayer): the composite program and its uniforms.
    layerProgram_ = createEmbeddedProgram(ShaderProgram::Layer).idx;
    if (layerProgram_ != kInvalid) {
        uLayerMode_ = bgfx::createUniform("u_layerMode", bgfx::UniformType::Vec4).idx;
        uLayerParams_ = bgfx::createUniform("u_layerParams", bgfx::UniformType::Vec4).idx;
        uLayerColor_ = bgfx::createUniform("u_layerColor", bgfx::UniformType::Vec4).idx;
        uLayerTexel_ = bgfx::createUniform("u_layerTexel", bgfx::UniformType::Vec4).idx;
    }
    std::fprintf(stderr, "[toms] sprite batch: %s (compute %s, instancing %s, GPU particles %s, layers %s)\n", spritePathName(spritePath()),
                 hasComputePath() ? "yes" : "no", hasInstancingPath() ? "yes" : "no", supportsGpuParticles() ? "yes" : "no",
                 supportsLayers() ? "yes" : "no");
}

BgfxRenderer::SpritePath BgfxRenderer::spritePath() const {
    // Auto: instancing (measured fastest: 30000 particles, D3D11 -- instancing 1.37 ms per frame,
    // CPU vertices 1.70 ms, compute 1.76 ms: compute expansion adds a pass and removes no CPU work).
    if (wantPath_ == SpritePath::Auto) {
        if (hasInstancingPath()) return SpritePath::Instancing;
        if (hasComputePath()) return SpritePath::Compute;
        return SpritePath::Cpu;
    }
    // A wanted path, else the next one down: compute -> instancing -> CPU.
    switch (wantPath_) {
    case SpritePath::Auto:
    case SpritePath::Compute:
        if (hasComputePath()) return SpritePath::Compute;
        [[fallthrough]];
    case SpritePath::Instancing:
        if (hasInstancingPath()) return SpritePath::Instancing;
        [[fallthrough]];
    case SpritePath::Cpu: break;
    }
    return SpritePath::Cpu;
}

const char* BgfxRenderer::spritePathName(SpritePath p) {
    switch (p) {
    case SpritePath::Compute: return "GPU compute";
    case SpritePath::Instancing: return "GPU instancing";
    case SpritePath::Cpu: return "CPU vertices";
    case SpritePath::Auto: break;
    }
    return "auto";
}

void BgfxRenderer::destroy() {
    withdrawAllTextures();
    destroyTexture(spriteTex_);
    for (uint16_t& t : extraTex_) destroyTexture(t);
    extraTex_.clear();
    if (program_ != kInvalid) { bgfx::destroy(bgfx::ProgramHandle{program_}); program_ = kInvalid; }
    if (instProgram_ != kInvalid) { bgfx::destroy(bgfx::ProgramHandle{instProgram_}); instProgram_ = kInvalid; }
    if (quadVb_ != kInvalid) { bgfx::destroy(bgfx::VertexBufferHandle{quadVb_}); quadVb_ = kInvalid; }
    if (quadIb_ != kInvalid) { bgfx::destroy(bgfx::IndexBufferHandle{quadIb_}); quadIb_ = kInvalid; }
    if (csProgram_ != kInvalid) { bgfx::destroy(bgfx::ProgramHandle{csProgram_}); csProgram_ = kInvalid; }
    if (csDrawProgram_ != kInvalid) { bgfx::destroy(bgfx::ProgramHandle{csDrawProgram_}); csDrawProgram_ = kInvalid; }
    if (csParams_ != kInvalid) { bgfx::destroy(bgfx::UniformHandle{csParams_}); csParams_ = kInvalid; }
    for (uint32_t id = 1; id <= gpuEmitters_.size(); id++) releaseGpuParticles(id);
    gpuEmitters_.clear();
    for (uint16_t* p : {&fxSpawn_, &fxUpdate_})
        if (*p != kInvalid) { bgfx::destroy(bgfx::ProgramHandle{*p}); *p = kInvalid; }
    for (uint16_t* u : {&uFxSim_, &uFxForce_, &uFxOrigin_, &uFxXform_, &uFxTint_, &uFxColorLut_, &uFxScalarLut_, &uFxFrameUv_, &uFxFrameRect_})
        if (*u != kInvalid) { bgfx::destroy(bgfx::UniformHandle{*u}); *u = kInvalid; }
    if (csIn_ != kInvalid) { bgfx::destroy(bgfx::DynamicVertexBufferHandle{csIn_}); csIn_ = kInvalid; }
    if (csOut_ != kInvalid) { bgfx::destroy(bgfx::DynamicVertexBufferHandle{csOut_}); csOut_ = kInvalid; }
    if (chunkIb_ != kInvalid) { bgfx::destroy(bgfx::IndexBufferHandle{chunkIb_}); chunkIb_ = kInvalid; }
    for (uint16_t* vb : {&cpuVb_, &instVb_, &meshVb_})
        if (*vb != kInvalid) { bgfx::destroy(bgfx::DynamicVertexBufferHandle{*vb}); *vb = kInvalid; }
    if (meshIb_ != kInvalid) { bgfx::destroy(bgfx::DynamicIndexBufferHandle{meshIb_}); meshIb_ = kInvalid; }
    csCapacity_ = csOutCap_ = cpuCap_ = instCap_ = meshVbCap_ = meshIbCap_ = 0;
    if (sampler_ != kInvalid) { bgfx::destroy(bgfx::UniformHandle{sampler_}); sampler_ = kInvalid; }
    for (uint16_t fb : layerFb_) bgfx::destroy(bgfx::FrameBufferHandle{fb});
    layerFb_.clear();
    layerW_ = layerH_ = 0;
    if (layerProgram_ != kInvalid) { bgfx::destroy(bgfx::ProgramHandle{layerProgram_}); layerProgram_ = kInvalid; }
    for (uint16_t* u : {&uLayerMode_, &uLayerParams_, &uLayerColor_, &uLayerTexel_})
        if (*u != kInvalid) { bgfx::destroy(bgfx::UniformHandle{*u}); *u = kInvalid; }
    linearTex_.clear();
    for (uint16_t* t : {&captureRead_})
        if (*t != kInvalid) { bgfx::destroy(bgfx::TextureHandle{*t}); *t = kInvalid; }
    if (captureFb_ != kInvalid) { bgfx::destroy(bgfx::FrameBufferHandle{captureFb_}); captureFb_ = kInvalid; }
    captureFbW_ = captureFbH_ = 0;
}

void BgfxRenderer::setCapture(uint16_t w, uint16_t h) {
    captureW_ = w && h ? w : 0;
    captureH_ = w && h ? h : 0;
}

void BgfxRenderer::prepareCapture() {
    captureFrame_ = 0;
    if (!captureW_) {
        bgfx::setViewFrameBuffer(kViewGame, BGFX_INVALID_HANDLE);   // the window again (kViewClear clears it)
        bgfx::setViewClear(kViewGame, BGFX_CLEAR_NONE);
        return;
    }
    if (captureFbW_ != captureW_ || captureFbH_ != captureH_ || captureFb_ == kInvalid) {
        if (captureFb_ != kInvalid) bgfx::destroy(bgfx::FrameBufferHandle{captureFb_});
        if (captureRead_ != kInvalid) bgfx::destroy(bgfx::TextureHandle{captureRead_});
#if TOMS_GAMMA_SPACE
        const uint64_t srgb = 0;
#else
        const uint64_t srgb = BGFX_TEXTURE_SRGB;   // written as the sRGB backbuffer would show it
#endif
        const bgfx::TextureHandle t = bgfx::createTexture2D(captureW_, captureH_, false, 1, bgfx::TextureFormat::RGBA8,
                                                            BGFX_TEXTURE_RT | srgb | BGFX_SAMPLER_UVW_CLAMP);
        captureFb_ = bgfx::createFrameBuffer(1, &t, true).idx;
        captureRead_ = bgfx::createTexture2D(captureW_, captureH_, false, 1, bgfx::TextureFormat::RGBA8,
                                             BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | srgb).idx;
        captureFbW_ = captureW_;
        captureFbH_ = captureH_;
    }
    if (captureFb_ == kInvalid || captureRead_ == kInvalid) return;
    bgfx::setViewFrameBuffer(kViewGame, bgfx::FrameBufferHandle{captureFb_});
    bgfx::setViewRect(kViewGame, 0, 0, captureW_, captureH_);
    bgfx::setViewClear(kViewGame, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    bgfx::touch(kViewGame);
    capturePx_.resize((size_t)captureW_ * captureH_ * 4);
    bgfx::touch(kViewCapture);
    bgfx::TextureRegion dst, src;
    dst.init(bgfx::TextureHandle{captureRead_});
    src.init(bgfx::getTexture(bgfx::FrameBufferHandle{captureFb_}));
    bgfx::blit(kViewCapture, dst, src);
    bgfx::TextureRegion all;
    all.init(bgfx::TextureHandle{captureRead_});
    captureFrame_ = bgfx::read(all, capturePx_.data());
}

void BgfxRenderer::destroyTexture(uint16_t& handle) {
    if (handle != kInvalid) { bgfx::destroy(bgfx::TextureHandle{handle}); handle = kInvalid; }
}

uint16_t BgfxRenderer::createAtlas(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0 || px.size() < (size_t)w * h * 4) return kInvalid;
    const uint32_t maxSize = bgfx::getCaps()->limits.maxTextureSize;
    if (w > maxSize || h > maxSize) {
        std::fprintf(stderr, "[toms] atlas %ux%u exceeds the GPU limit %u -- not uploaded\n", w, h, maxSize);
        return kInvalid;
    }
#if TOMS_GAMMA_SPACE
    // No sRGB backbuffer (the web, Android: color_space.h), so sampling sRGB textures would darken everything. Same
    // as the old WebGL renderer: plain RGBA8, blending in gamma space.
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
    withdrawAllTextures();   // the runtime grid is no file the UI could ask for
    destroyTexture(spriteTex_);
    spriteTex_ = createAtlas(atlas, aw, ah);
}

void BgfxRenderer::loadSpriteAtlas(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, const std::string& file) {
    withdrawAllTextures();   // before the old texture goes: the UI must not draw with it again
    destroyTexture(spriteTex_);
    spriteTex_ = createAtlas(rgba, w, h);
    if (spriteTex_ != kInvalid && !file.empty())   // the UI's icons come from the same file (shared_textures.h)
        lendTexture(file, SharedTexture{spriteTex_, (int)w, (int)h, true});
}

uint16_t BgfxRenderer::loadTexture(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    const uint16_t t = createAtlas(rgba, w, h);   // same sampling as the sprite atlas
    if (t == kInvalid) return kSpriteAtlasTexture;
    extraTex_.push_back(t);
    return t;
}

uint16_t BgfxRenderer::createDynamicTexture(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return kSpriteAtlasTexture;
#if TOMS_GAMMA_SPACE
    const uint64_t flags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
#else
    const uint64_t flags = BGFX_TEXTURE_SRGB | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
#endif
    // No initial pixels: an updatable texture (updateTexture).
    const uint16_t t = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::RGBA8, flags, nullptr).idx;
    if (t == kInvalid) return kSpriteAtlasTexture;
    extraTex_.push_back(t);
    return t;
}

void BgfxRenderer::updateTexture(uint16_t texture, const uint8_t* rgba, uint32_t w, uint32_t h) {
    if (texture == kInvalid || texture == kSpriteAtlasTexture || !rgba) return;
    bgfx::updateTexture2D(bgfx::TextureHandle{texture}, 0, 0, 0, 0, (uint16_t)w, (uint16_t)h, bgfx::copy(rgba, w * h * 4));
}

void BgfxRenderer::releaseTexture(uint16_t texture) {
    auto it = std::find(extraTex_.begin(), extraTex_.end(), texture);
    if (it == extraTex_.end()) return;   // not ours (or the sprite atlas): nothing to free
    linearTex_.erase(std::remove(linearTex_.begin(), linearTex_.end(), texture), linearTex_.end());
    destroyTexture(*it);
    extraTex_.erase(it);
}

void BgfxRenderer::setTextureFilter(uint16_t texture, bool linear) {
    if (texture == kInvalid) return;
    linearTex_.erase(std::remove(linearTex_.begin(), linearTex_.end(), texture), linearTex_.end());
    if (linear) linearTex_.push_back(texture);
}

void BgfxRenderer::bindTexture(uint16_t texture) {
    if (texture == kInvalid) return;
    // A linear texture: this draw's sampler flags replace the texture's point sampling (sRGB belongs to the texture).
    const bool linear = std::find(linearTex_.begin(), linearTex_.end(), texture) != linearTex_.end();
    bgfx::setTexture(0, bgfx::UniformHandle{sampler_}, bgfx::TextureHandle{texture}, linear ? BGFX_SAMPLER_UVW_CLAMP : UINT32_MAX);
}

void BgfxRenderer::begin() {
    sprites_.clear();
    topSprites_.clear();
    layer_ = 0;
    gpuDraws_.clear();
    meshVerts_.clear();
    meshIdx_.clear();
    layers_.clear();
    layerStack_.clear();
    layerClosed_.clear();
}
void BgfxRenderer::setNode(uint8_t n) { node_ = n; }
void BgfxRenderer::setNodeFilter(uint8_t n) { nodeFilter_ = n; }

void BgfxRenderer::drawSprite(const Quad& q) {
    if (nodeFilter_ && node_ != nodeFilter_) return;
    std::vector<Quad>& list = layer_ == 1 ? topSprites_ : currentQuads();
    list.push_back(q);
    list.back().node = node_;
}

void BgfxRenderer::drawMesh(const Mesh& m) {
    if (nodeFilter_ && node_ != nodeFilter_) return;
    if (!validMesh(m)) return;
    const uint32_t first = (uint32_t)(meshVerts_.size() / 9);
    // The CPU path's vertex (SpriteVertex): position, uv, the mesh's tint (times the vertex's colour), not solid.
    for (uint32_t i = 0; i < m.vertexCount; i++) {
        const MeshVertex& v = m.vertices[i];
        const float* c = m.colors ? m.colors + 4 * i : nullptr;
        meshVerts_.insert(meshVerts_.end(), {v.x, v.y, v.u, v.v, m.tint[0] * (c ? c[0] : 1.0f), m.tint[1] * (c ? c[1] : 1.0f),
                                             m.tint[2] * (c ? c[2] : 1.0f), m.tint[3] * (c ? c[3] : 1.0f), 0.0f});
    }
    // Right after another mesh with the same texture and blending (no quad, particles or layer between): one draw for
    // both -- its indices then count from that mesh's first vertex. A crowd of skinned characters is a few draws.
    std::vector<GpuDraw>& draws = currentDraws();
    if (!draws.empty()) {
        GpuDraw& prev = draws.back();
        if (prev.emitter == 0 && prev.layer < 0 && prev.at == currentQuads().size() && prev.texture == m.texture &&
            prev.additive == m.additive && prev.firstIndex + prev.indexCount == meshIdx_.size() &&
            (first - prev.firstVertex) + m.vertexCount <= 65536u) {
            const uint16_t base = uint16_t(first - prev.firstVertex);
            for (uint32_t i = 0; i < m.indexCount; i++) meshIdx_.push_back(uint16_t(m.indices[i] + base));
            prev.vertexCount += m.vertexCount;
            prev.indexCount += m.indexCount;
            return;
        }
    }
    GpuDraw d{currentQuads().size(), 0, m.texture, m.additive};
    d.firstVertex = first;
    d.vertexCount = m.vertexCount;
    d.firstIndex = (uint32_t)meshIdx_.size();
    d.indexCount = m.indexCount;
    meshIdx_.insert(meshIdx_.end(), m.indices, m.indices + m.indexCount);
    draws.push_back(d);
}

// This frame's meshes into their buffers, once (end(), before any path draws).
void BgfxRenderer::uploadMeshes() {
    if (meshIdx_.empty()) return;
    const uint32_t verts = (uint32_t)(meshVerts_.size() / 9), indices = (uint32_t)meshIdx_.size();
    auto grow = [](uint32_t cap, uint32_t need) { cap = std::max(cap, 1024u); while (cap < need) cap *= 2; return cap; };
    if (meshVb_ == kInvalid || verts > meshVbCap_) {
        if (meshVb_ != kInvalid) bgfx::destroy(bgfx::DynamicVertexBufferHandle{meshVb_});
        meshVbCap_ = grow(meshVbCap_, verts);
        meshVb_ = bgfx::createDynamicVertexBuffer(meshVbCap_, spriteLayout(), BGFX_BUFFER_NONE).idx;
    }
    if (meshIb_ == kInvalid || indices > meshIbCap_) {
        if (meshIb_ != kInvalid) bgfx::destroy(bgfx::DynamicIndexBufferHandle{meshIb_});
        meshIbCap_ = grow(meshIbCap_, indices);
        meshIb_ = bgfx::createDynamicIndexBuffer(meshIbCap_, BGFX_BUFFER_NONE).idx;
    }
    if (meshVb_ == kInvalid || meshIb_ == kInvalid) {
        std::fprintf(stderr, "[toms] meshes: no buffers for %u vertices\n", verts);
        meshVbCap_ = meshIbCap_ = 0;
        return;
    }
    bgfx::update(bgfx::DynamicVertexBufferHandle{meshVb_}, 0, bgfx::copy(meshVerts_.data(), (uint32_t)(meshVerts_.size() * sizeof(float))));
    bgfx::update(bgfx::DynamicIndexBufferHandle{meshIb_}, 0, bgfx::copy(meshIdx_.data(), indices * (uint32_t)sizeof(uint16_t)));
}

void BgfxRenderer::end() {
    lastDrawCalls_ = 0;
    lastDrawnQuads_ = 0;
    lastGpuEmitters_ = 0;
    lastMeshCount_ = 0;
    lastQuadCount_ = sprites_.size();
    while (!layerStack_.empty()) endLayer();   // a layer left open ends with the frame
    for (const LayerDraw& l : layers_) lastQuadCount_ += l.quads.size();

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
    prepareCapture();
    // The top layer: the same space, a view after kViewUi, so its sprites are drawn over the UI.
    bgfx::setViewRect(kViewTop, (uint16_t)std::lround(fit.x), (uint16_t)std::lround(fit.y),
                      (uint16_t)std::lround(fit.width), (uint16_t)std::lround(fit.height));
    bgfx::setViewMode(kViewTop, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(kViewTop, nullptr, proj);

    if (program_ == kInvalid) return;
    submitTop();
    uploadMeshes();
    prepareLayers();
    applyViewOrder();
    // text and UI are RmlUi's, in view kViewUi
    if (sprites_.empty()) {   // only GPU particles and meshes (if any)
        size_t next = 0;
        drawGpuAt(SIZE_MAX, next, spriteTex_);
        return;
    }
    const SpritePath path = spritePath();
    if (path == SpritePath::Compute && submitQuadsCompute(sprites_, spriteTex_)) return;
    if (path != SpritePath::Cpu && submitQuadsInstanced(sprites_, spriteTex_)) return;
    submitQuads(sprites_, spriteTex_);
}

namespace {
bool ensureBuffer(uint16_t& handle, uint32_t& capacity, uint32_t need, const bgfx::VertexLayout& layout, uint16_t flags) {
    constexpr uint16_t kInvalid = UINT16_MAX;
    if (need <= capacity && handle != kInvalid) return true;
    uint32_t cap = 1024;
    while (cap < need) cap *= 2;
    if (handle != kInvalid) bgfx::destroy(bgfx::DynamicVertexBufferHandle{handle});
    handle = bgfx::createDynamicVertexBuffer(cap, layout, flags).idx;
    capacity = handle == kInvalid ? 0 : cap;
    return handle != kInvalid;
}
}  // namespace

bool BgfxRenderer::submitQuadsCompute(const std::vector<Quad>& quads, uint16_t texture) {
    const uint32_t n = (uint32_t)quads.size();
    if (n == 0) return true;
    uint32_t outCap = csOutCap_;
    if (!ensureBuffer(csIn_, csCapacity_, n * 4, csInLayout(), BGFX_BUFFER_COMPUTE_READ | kComputeBufferFlags) ||
        !ensureBuffer(csOut_, outCap, n * 4, csOutLayout(), BGFX_BUFFER_COMPUTE_WRITE | kComputeBufferFlags))
        return false;
    csOutCap_ = outCap;
    // The quads in, as m_pParticleInSSO was filled: corners, uv rect (u0 = -1: solid), tint.
    csScratch_.resize((size_t)n * 16);
    float* d = csScratch_.data();
    for (const Quad& q : quads) {
        const float x0 = q.rect[0], y0 = q.rect[1], x1 = q.rect[0] + q.rect[2], y1 = q.rect[1] + q.rect[3];
        const float rectCorners[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
        const float* c = q.hasCorners ? q.corners : rectCorners;
        std::memcpy(d, c, 8 * sizeof(float));
        d[8] = q.solid ? -1.0f : q.uv[0]; d[9] = q.uv[1]; d[10] = q.uv[2]; d[11] = q.uv[3];
        std::memcpy(d + 12, q.tint, 4 * sizeof(float));
        d += 16;
    }
    bgfx::update(bgfx::DynamicVertexBufferHandle{csIn_}, 0, bgfx::copy(csScratch_.data(), (uint32_t)(csScratch_.size() * sizeof(float))));
    // Expand them on the GPU, in the clear view: views run in order, so before kViewGame draws.
    const float params[4] = {(float)n, 0, 0, 0};
    bgfx::setUniform(bgfx::UniformHandle{csParams_}, params);
    bgfx::setBuffer(0, bgfx::DynamicVertexBufferHandle{csIn_}, bgfx::Access::Read);
    bgfx::setBuffer(1, bgfx::DynamicVertexBufferHandle{csOut_}, bgfx::Access::Write);
    bgfx::dispatch(kViewClear, bgfx::ProgramHandle{csProgram_}, (n + 63) / 64, 1, 1);
    // Draw straight from the output buffer: one draw per run of texture / blending, in chunks.
    size_t first = 0, nextGpu = 0;
    while (first < n) {
        const size_t limit = drawGpuAt(first, nextGpu, texture);
        const bool additive = quads[first].additive;
        const uint16_t tex = quads[first].texture;
        size_t runEnd = first + 1;
        while (runEnd < n && runEnd < limit && quads[runEnd].additive == additive && quads[runEnd].texture == tex) ++runEnd;
        for (size_t at = first; at < runEnd; at += kChunk) {
            const uint32_t count = (uint32_t)std::min<size_t>(kChunk, runEnd - at);
            bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{csOut_}, (uint32_t)at * 4, count * 4);
            bgfx::setIndexBuffer(bgfx::IndexBufferHandle{chunkIb_}, 0, count * 6);
            const uint16_t use = tex == kSpriteAtlasTexture ? texture : tex;
            bindTexture(use);
            bgfx::setState(blendState(additive));
            bgfx::submit(kViewGame, bgfx::ProgramHandle{csDrawProgram_});
            ++lastDrawCalls_;
            lastDrawnQuads_ += count;
        }
        first = runEnd;
    }
    drawGpuAt(SIZE_MAX, nextGpu, texture);   // GPU particles after the last quad
    return true;
}

uint64_t BgfxRenderer::blendState(bool additive) const {
    if (captureW_) return layerBlendState(additive);   // a capture keeps its alpha: premultiplied, as a layer
    // Straight alpha: additive scales the colour by its alpha and adds it.
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
           (additive ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE) : BGFX_STATE_BLEND_ALPHA);
}

// Grows a persistent dynamic vertex buffer to hold `need` vertices (powers of two: few regrowths).
// Persistent buffers instead of bgfx's per-frame transient ones: those are a few MB per frame, so
// 300000 quads did not fit and most were dropped.
bool BgfxRenderer::submitQuadsInstanced(const std::vector<Quad>& quads, uint16_t texture) {
    const uint32_t total = (uint32_t)quads.size();
    if (total == 0) return true;
    if (!ensureBuffer(instVb_, instCap_, total, instanceLayout(), BGFX_BUFFER_NONE)) return false;
    instScratch_.resize((size_t)total * 16);
    auto* inst = reinterpret_cast<QuadInstance*>(instScratch_.data());
    for (uint32_t i = 0; i < total; ++i) {
        const Quad& q = quads[i];
        const float x0 = q.rect[0], y0 = q.rect[1], x1 = q.rect[0] + q.rect[2], y1 = q.rect[1] + q.rect[3];
        const float rectCorners[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
        const float* c = q.hasCorners ? q.corners : rectCorners;
        inst[i] = {c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7],
                           q.solid ? -1.0f : q.uv[0], q.uv[1], q.uv[2], q.uv[3],
                           q.tint[0], q.tint[1], q.tint[2], q.tint[3]};
    }
    bgfx::update(bgfx::DynamicVertexBufferHandle{instVb_}, 0, bgfx::copy(inst, total * (uint32_t)sizeof(QuadInstance)));
    size_t first = 0, nextGpu = 0;
    while (first < total) {
        const size_t limit = drawGpuAt(first, nextGpu, texture);   // GPU particles placed before quad `first`
        // One draw call per run of quads with the same texture and blending, as the CPU path.
        const bool additive = quads[first].additive;
        const uint16_t tex = quads[first].texture;
        size_t runEnd = first + 1;
        while (runEnd < total && runEnd < limit && quads[runEnd].additive == additive && quads[runEnd].texture == tex) ++runEnd;
        bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{quadVb_});
        bgfx::setIndexBuffer(bgfx::IndexBufferHandle{quadIb_});
        bgfx::setInstanceDataBuffer(bgfx::DynamicVertexBufferHandle{instVb_}, (uint32_t)first, (uint32_t)(runEnd - first));
        const uint16_t use = tex == kSpriteAtlasTexture ? texture : tex;
        bindTexture(use);
        bgfx::setState(blendState(additive));
        bgfx::submit(kViewGame, bgfx::ProgramHandle{instProgram_});
        ++lastDrawCalls_;
        lastDrawnQuads_ += runEnd - first;
        first = runEnd;
    }
    drawGpuAt(SIZE_MAX, nextGpu, texture);   // GPU particles after the last quad
    return true;
}

void BgfxRenderer::submitQuads(const std::vector<Quad>& quads, uint16_t texture) {
    const uint32_t total = (uint32_t)quads.size();
    if (total == 0) return;
    if (!ensureBuffer(cpuVb_, cpuCap_, total * 4, spriteLayout(), BGFX_BUFFER_NONE)) {
        std::fprintf(stderr, "[toms] sprite batch: no vertex buffer for %u quads\n", total);
        return;
    }
    static_assert(sizeof(SpriteVertex) == 9 * sizeof(float), "SpriteVertex is packed floats");
    cpuScratch_.resize((size_t)total * 4 * 9);
    auto* v = reinterpret_cast<SpriteVertex*>(cpuScratch_.data());
    for (uint32_t i = 0; i < total; ++i) {
        const Quad& q = quads[i];
        const float x0 = q.rect[0], y0 = q.rect[1], x1 = q.rect[0] + q.rect[2], y1 = q.rect[1] + q.rect[3];
        const float rectCorners[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
        const float* c = q.hasCorners ? q.corners : rectCorners;
        const float u0 = q.uv[0], v0 = q.uv[1], u1 = q.uv[2], v1 = q.uv[3];
        const float s = q.solid ? 1.0f : 0.0f;
        const float* t = q.tint;
        v[i * 4 + 0] = {c[0], c[1], u0, v0, t[0], t[1], t[2], t[3], s};
        v[i * 4 + 1] = {c[2], c[3], u1, v0, t[0], t[1], t[2], t[3], s};
        v[i * 4 + 2] = {c[4], c[5], u1, v1, t[0], t[1], t[2], t[3], s};
        v[i * 4 + 3] = {c[6], c[7], u0, v1, t[0], t[1], t[2], t[3], s};
    }
    bgfx::update(bgfx::DynamicVertexBufferHandle{cpuVb_}, 0, bgfx::copy(v, total * 4 * (uint32_t)sizeof(SpriteVertex)));
    size_t first = 0, nextGpu = 0;
    while (first < total) {
        const size_t limit = drawGpuAt(first, nextGpu, texture);   // GPU particles placed before quad `first`
        // One draw call per run of quads with the same texture and blending (normally the whole
        // frame), in chunks of kChunk quads (16-bit indices into the shared index buffer).
        const bool additive = quads[first].additive;
        const uint16_t tex = quads[first].texture;
        size_t runEnd = first + 1;
        while (runEnd < total && runEnd < limit && quads[runEnd].additive == additive && quads[runEnd].texture == tex) ++runEnd;
        for (size_t at = first; at < runEnd; at += kChunk) {
            const uint32_t count = (uint32_t)std::min<size_t>(kChunk, runEnd - at);
            bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{cpuVb_}, (uint32_t)at * 4, count * 4);
            bgfx::setIndexBuffer(bgfx::IndexBufferHandle{chunkIb_}, 0, count * 6);
            const uint16_t use = tex == kSpriteAtlasTexture ? texture : tex;   // the sprite atlas, or a loadTexture one
            bindTexture(use);
            bgfx::setState(blendState(additive));
            bgfx::submit(kViewGame, bgfx::ProgramHandle{program_});
            ++lastDrawCalls_;
            lastDrawnQuads_ += count;
        }
        first = runEnd;
    }
    drawGpuAt(SIZE_MAX, nextGpu, texture);   // GPU particles after the last quad
}

void BgfxRenderer::submitTop() {
    const uint32_t total = (uint32_t)topSprites_.size();
    if (total == 0) return;
    for (size_t first = 0; first < total;) {
        // one draw per run of quads with the same texture and blending, at most kChunk quads (the shared index buffer)
        const bool additive = topSprites_[first].additive;
        const uint16_t tex = topSprites_[first].texture;
        size_t runEnd = first + 1;
        while (runEnd < total && runEnd - first < kChunk && topSprites_[runEnd].additive == additive &&
               topSprites_[runEnd].texture == tex)
            ++runEnd;
        const uint32_t count = (uint32_t)(runEnd - first);
        if (bgfx::getAvailTransientVertexBuffer(count * 4, spriteLayout()) < count * 4) return;
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, count * 4, spriteLayout());
        auto* v = reinterpret_cast<SpriteVertex*>(tvb.data);
        for (uint32_t i = 0; i < count; ++i) {
            const Quad& q = topSprites_[first + i];
            const float x0 = q.rect[0], y0 = q.rect[1], x1 = q.rect[0] + q.rect[2], y1 = q.rect[1] + q.rect[3];
            const float rectCorners[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
            const float* c = q.hasCorners ? q.corners : rectCorners;
            const float s = q.solid ? 1.0f : 0.0f;
            const float* t = q.tint;
            v[i * 4 + 0] = {c[0], c[1], q.uv[0], q.uv[1], t[0], t[1], t[2], t[3], s};
            v[i * 4 + 1] = {c[2], c[3], q.uv[2], q.uv[1], t[0], t[1], t[2], t[3], s};
            v[i * 4 + 2] = {c[4], c[5], q.uv[2], q.uv[3], t[0], t[1], t[2], t[3], s};
            v[i * 4 + 3] = {c[6], c[7], q.uv[0], q.uv[3], t[0], t[1], t[2], t[3], s};
        }
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(bgfx::IndexBufferHandle{chunkIb_}, 0, count * 6);
        const uint16_t use = tex == kSpriteAtlasTexture ? spriteTex_ : tex;
        bindTexture(use);
        bgfx::setState(blendState(additive));
        bgfx::submit(kViewTop, bgfx::ProgramHandle{program_});
        ++lastDrawCalls_;
        lastDrawnQuads_ += count;
        first = runEnd;
    }
}

// ---- GPU-simulated particles --------------------------------------------------------------------

bool BgfxRenderer::supportsGpuParticles() const {
    return fxSpawn_ != kInvalid && fxUpdate_ != kInvalid && csDrawProgram_ != kInvalid && chunkIb_ != kInvalid;
}

uint32_t BgfxRenderer::createGpuParticles(uint32_t capacity) {
    if (!supportsGpuParticles() || capacity == 0) return 0;
    GpuEmitterBuffers b;
    b.capacity = capacity;
    // Every slot starts empty (life 0): zeroed memory.
    const bgfx::Memory* zeros = bgfx::alloc(capacity * 4 * 16);
    std::memset(zeros->data, 0, zeros->size);
    b.state = bgfx::createDynamicVertexBuffer(zeros, csInLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE).idx;
    b.out = bgfx::createDynamicVertexBuffer(capacity * 4, csOutLayout(), BGFX_BUFFER_COMPUTE_WRITE).idx;
    if (b.state == kInvalid || b.out == kInvalid) {
        if (b.state != kInvalid) bgfx::destroy(bgfx::DynamicVertexBufferHandle{b.state});
        if (b.out != kInvalid) bgfx::destroy(bgfx::DynamicVertexBufferHandle{b.out});
        return 0;
    }
    for (size_t i = 0; i < gpuEmitters_.size(); i++)   // reuse a released id
        if (gpuEmitters_[i].state == kInvalid) {
            gpuEmitters_[i] = b;
            return uint32_t(i + 1);
        }
    gpuEmitters_.push_back(b);
    return uint32_t(gpuEmitters_.size());
}

void BgfxRenderer::releaseGpuParticles(uint32_t id) {
    if (id == 0 || id > gpuEmitters_.size()) return;
    GpuEmitterBuffers& b = gpuEmitters_[id - 1];
    for (uint16_t* h : {&b.state, &b.out, &b.spawn})
        if (*h != kInvalid) { bgfx::destroy(bgfx::DynamicVertexBufferHandle{*h}); *h = kInvalid; }
    b.capacity = b.spawnCap = 0;
}

void BgfxRenderer::drawGpuParticles(const GpuParticleFrame& f) {
    if (!supportsGpuParticles() || f.emitter == 0 || f.emitter > gpuEmitters_.size()) return;
    GpuEmitterBuffers& b = gpuEmitters_[f.emitter - 1];
    if (b.state == kInvalid) return;
    // The simulation runs in the clear view, in order (newborns before the update), so before kViewGame draws.
    bgfx::setViewMode(kViewClear, bgfx::ViewMode::Sequential);
    float sim[4] = {(float)f.steps, f.dt, (float)b.capacity, (float)f.spawnCount};
    if (f.spawnCount > 0) {
        if (!ensureBuffer(b.spawn, b.spawnCap, f.spawnCount * 5, csInLayout(), BGFX_BUFFER_COMPUTE_READ)) return;
        bgfx::update(bgfx::DynamicVertexBufferHandle{b.spawn}, 0, bgfx::copy(f.spawn, f.spawnCount * 20 * (uint32_t)sizeof(float)));
        bgfx::setUniform(bgfx::UniformHandle{uFxSim_}, sim);
        bgfx::setBuffer(0, bgfx::DynamicVertexBufferHandle{b.state}, bgfx::Access::ReadWrite);
        bgfx::setBuffer(1, bgfx::DynamicVertexBufferHandle{b.spawn}, bgfx::Access::Read);
        bgfx::dispatch(kViewClear, bgfx::ProgramHandle{fxSpawn_}, (f.spawnCount + 63) / 64, 1, 1);
    }
    const float force[4] = {f.gravity[0], f.gravity[1], f.dragPerStep, f.alignToVelocity ? 1.0f : 0.0f};
    const float origin[4] = {f.origin[0], f.origin[1], (float)f.frameCount, f.fps};
    const float xform[8] = {f.xform[0], f.xform[1], f.xform[2], 0, f.xform[3], f.xform[4], f.xform[5], 0};
    bgfx::setUniform(bgfx::UniformHandle{uFxSim_}, sim);
    bgfx::setUniform(bgfx::UniformHandle{uFxForce_}, force);
    bgfx::setUniform(bgfx::UniformHandle{uFxOrigin_}, origin);
    bgfx::setUniform(bgfx::UniformHandle{uFxXform_}, xform, 2);
    bgfx::setUniform(bgfx::UniformHandle{uFxTint_}, f.tint);
    bgfx::setUniform(bgfx::UniformHandle{uFxColorLut_}, f.colorLut, GpuParticleFrame::kLut);
    bgfx::setUniform(bgfx::UniformHandle{uFxScalarLut_}, f.scalarLut, GpuParticleFrame::kLut);
    bgfx::setUniform(bgfx::UniformHandle{uFxFrameUv_}, f.frameUv, std::max(1u, f.frameCount));
    bgfx::setUniform(bgfx::UniformHandle{uFxFrameRect_}, f.frameRect, std::max(1u, f.frameCount));
    bgfx::setBuffer(0, bgfx::DynamicVertexBufferHandle{b.state}, bgfx::Access::ReadWrite);
    bgfx::setBuffer(1, bgfx::DynamicVertexBufferHandle{b.out}, bgfx::Access::Write);
    bgfx::dispatch(kViewClear, bgfx::ProgramHandle{fxUpdate_}, (b.capacity + 63) / 64, 1, 1);
    currentDraws().push_back({currentQuads().size(), f.emitter, f.texture, f.additive});
}

size_t BgfxRenderer::drawMarkers(uint16_t view, const std::vector<GpuDraw>& draws, bool inLayer, size_t at, size_t& next,
                                 uint16_t texture) {
    for (; next < draws.size() && draws[next].at <= at; next++) {
        const GpuDraw& d = draws[next];
        if (d.layer >= 0) { submitComposite(d.layer, view); continue; }
        const uint16_t use = d.texture == kSpriteAtlasTexture ? texture : d.texture;
        const uint64_t state = inLayer ? layerBlendState(d.additive) : blendState(d.additive);
        if (d.emitter == 0) {   // a mesh (drawMesh): the CPU path's program, its own part of the frame's buffers
            if (meshVb_ == kInvalid || meshIb_ == kInvalid) continue;
            bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{meshVb_}, d.firstVertex, d.vertexCount);
            bgfx::setIndexBuffer(bgfx::DynamicIndexBufferHandle{meshIb_}, d.firstIndex, d.indexCount);
            bindTexture(use);
            bgfx::setState(state);
            bgfx::submit(view, bgfx::ProgramHandle{program_});
            ++lastDrawCalls_;
            ++lastMeshCount_;
            continue;
        }
        const GpuEmitterBuffers& b = gpuEmitters_[d.emitter - 1];
        if (b.out == kInvalid) continue;
        // Every slot's quad (empty slots are degenerate: nothing to rasterize), in chunks.
        for (uint32_t first = 0; first < b.capacity; first += kChunk) {
            const uint32_t count = std::min(kChunk, b.capacity - first);
            bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{b.out}, first * 4, count * 4);
            bgfx::setIndexBuffer(bgfx::IndexBufferHandle{chunkIb_}, 0, count * 6);
            bindTexture(use);
            bgfx::setState(state);
            bgfx::submit(view, bgfx::ProgramHandle{csDrawProgram_});
            ++lastDrawCalls_;
        }
        ++lastGpuEmitters_;
    }
    return next < draws.size() ? draws[next].at : SIZE_MAX;
}

// ---- layers -------------------------------------------------------------------------------------

uint64_t BgfxRenderer::layerBlendState(bool additive) const {
    // Into a layer's premultiplied picture: the colour blends as on screen, the alpha accumulates coverage (an additive
    // sprite adds light, no coverage).
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
           (additive ? BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ZERO,
                                                      BGFX_STATE_BLEND_ONE)
                     : BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                                      BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
}

bool BgfxRenderer::supportsLayers() const {
    if (layerProgram_ == kInvalid || program_ == kInvalid || chunkIb_ == kInvalid) return false;
    const auto* caps = bgfx::getCaps();
    return (caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0 ||
           (caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
}

void BgfxRenderer::beginLayer(const LayerEffect& fx) {
    // Drawn straight through: the top layer, no support, too many this frame, or inside a layer drawn straight through.
    if (layer_ == 1 || !supportsLayers() || layers_.size() >= kMaxLayers || (!layerStack_.empty() && layerStack_.back() < 0)) {
        layerStack_.push_back(-1);
        return;
    }
    LayerDraw l;
    l.fx = fx;
    layers_.push_back(std::move(l));
    layerStack_.push_back(int(layers_.size() - 1));
}

void BgfxRenderer::endLayer() {
    if (layerStack_.empty()) return;
    const int idx = layerStack_.back();
    layerStack_.pop_back();
    if (idx < 0) return;
    layerClosed_.push_back(idx);
    // Its picture goes where the layer began, among what is around it.
    GpuDraw d{currentQuads().size(), 0, kSpriteAtlasTexture, false};
    d.layer = idx;
    currentDraws().push_back(d);
}

void BgfxRenderer::prepareLayers() {
    lastLayerCount_ = 0;
    if (layerClosed_.empty()) return;
    // One picture per layer this frame, the game view's size in device pixels (a texel per screen pixel).
    const ViewportRect fit = computeAspectFitViewport(devW_, devH_, kDesignW, kDesignH);
    const uint16_t w = (uint16_t)std::max(1L, std::lround(fit.width)), h = (uint16_t)std::max(1L, std::lround(fit.height));
    if (w != layerW_ || h != layerH_) {
        for (uint16_t fb : layerFb_) bgfx::destroy(bgfx::FrameBufferHandle{fb});
        layerFb_.clear();
        layerW_ = w;
        layerH_ = h;
    }
    const bool hdr = (bgfx::getCaps()->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
    while (layerFb_.size() < layerClosed_.size()) {
        const bgfx::TextureHandle t = bgfx::createTexture2D(w, h, false, 1, hdr ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8,
                                                            BGFX_TEXTURE_RT | BGFX_SAMPLER_UVW_CLAMP);
        const bgfx::FrameBufferHandle fb = bgfx::createFrameBuffer(1, &t, true);
        if (!bgfx::isValid(fb)) break;
        layerFb_.push_back(fb.idx);
    }
    const float W = (float)kDesignW, H = (float)kDesignH, sx = float(w) / W, sy = float(h) / H;
    // Inner layers close first: in closing order, every picture is drawn before the layer that uses it (and its bounds
    // are known when the layer around it is measured).
    for (size_t k = 0; k < layerClosed_.size() && k < layerFb_.size(); k++) {
        LayerDraw& l = layers_[(size_t)layerClosed_[k]];
        // What it draws: its quads, its meshes' vertices, its inner layers' pictures; particles: anywhere
        l.any = false;
        auto grow = [&](float x0, float y0, float x1, float y1) {
            if (!l.any) { l.x0 = x0; l.y0 = y0; l.x1 = x1; l.y1 = y1; l.any = true; return; }
            l.x0 = std::min(l.x0, x0); l.y0 = std::min(l.y0, y0); l.x1 = std::max(l.x1, x1); l.y1 = std::max(l.y1, y1);
        };
        for (const Quad& q : l.quads) grow(q.rect[0], q.rect[1], q.rect[0] + q.rect[2], q.rect[1] + q.rect[3]);
        for (const GpuDraw& d : l.draws) {
            if (d.layer >= 0) {
                const LayerDraw& in = layers_[(size_t)d.layer];
                if (in.any) grow(in.x0 - in.reach, in.y0 - in.reach, in.x1 + in.reach, in.y1 + in.reach);
            } else if (d.emitter == 0) {
                for (uint32_t v = d.firstVertex; v < d.firstVertex + d.vertexCount; v++)
                    grow(meshVerts_[size_t(v) * 9], meshVerts_[size_t(v) * 9 + 1], meshVerts_[size_t(v) * 9], meshVerts_[size_t(v) * 9 + 1]);
            } else {
                grow(0, 0, W, H);
            }
        }
        const float* p = l.fx.params;
        l.reach = 2.0f + (l.fx.kind == LayerEffect::MotionBlur ? 0.5f * std::max(std::fabs(p[0]), std::fabs(p[1]))
                         : l.fx.kind == LayerEffect::Glow    ? std::fabs(p[1])
                         : l.fx.kind == LayerEffect::Outline ? std::fabs(p[0]) : 0.0f);
        if (!l.any) continue;   // nothing drawn: no picture, no composite
        // the picture's part used: the content and twice the reach (a blur tap at the composite's edge reads cleared
        // pixels), whole device pixels, in the picture
        const float cx0 = std::max(0.0f, l.x0 - 2 * l.reach), cy0 = std::max(0.0f, l.y0 - 2 * l.reach);
        const float cx1 = std::min(W, l.x1 + 2 * l.reach), cy1 = std::min(H, l.y1 + 2 * l.reach);
        if (cx1 <= cx0 || cy1 <= cy0) { l.any = false; continue; }
        const int px0 = std::max(0, (int)std::floor(cx0 * sx)), py0 = std::max(0, (int)std::floor(cy0 * sy));
        const int px1 = std::min((int)w, (int)std::ceil(cx1 * sx)), py1 = std::min((int)h, (int)std::ceil(cy1 * sy));
        l.slot = (int)k;
        const uint16_t view = uint16_t(kViewLayerFirst + k);
        bgfx::setViewFrameBuffer(view, bgfx::FrameBufferHandle{layerFb_[k]});
        bgfx::setViewRect(view, uint16_t(px0), uint16_t(py0), uint16_t(px1 - px0), uint16_t(py1 - py0));
        bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
        float proj[16];   // that part of the design space onto that part of the picture
        bx::mtxOrtho(proj, float(px0) / sx, float(px1) / sx, float(py1) / sy, float(py0) / sy, 0.0f, 100.0f, 0.0f,
                     bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(view, nullptr, proj);
        bgfx::touch(view);
        lastLayerCount_++;
    }
    for (size_t k = 0; k < layerClosed_.size() && k < layerFb_.size(); k++)
        if (layers_[(size_t)layerClosed_[k]].slot >= 0) submitLayer(layers_[(size_t)layerClosed_[k]], uint16_t(kViewLayerFirst + k));
}

void BgfxRenderer::submitLayer(const LayerDraw& l, uint16_t view) {
    // Its quads in transient buffers, one draw per run of texture and blending; its markers (meshes, particles, inner
    // layers' pictures) in their places.
    const size_t total = l.quads.size();
    size_t first = 0, next = 0;
    while (first < total) {
        const size_t limit = drawMarkers(view, l.draws, true, first, next, spriteTex_);
        const bool additive = l.quads[first].additive;
        const uint16_t tex = l.quads[first].texture;
        size_t runEnd = first + 1;
        while (runEnd < total && runEnd < limit && runEnd - first < kChunk && l.quads[runEnd].additive == additive &&
               l.quads[runEnd].texture == tex)
            ++runEnd;
        const uint32_t count = (uint32_t)(runEnd - first);
        if (bgfx::getAvailTransientVertexBuffer(count * 4, spriteLayout()) < count * 4) break;
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, count * 4, spriteLayout());
        auto* v = reinterpret_cast<SpriteVertex*>(tvb.data);
        for (uint32_t i = 0; i < count; ++i) {
            const Quad& q = l.quads[first + i];
            const float x0 = q.rect[0], y0 = q.rect[1], x1 = q.rect[0] + q.rect[2], y1 = q.rect[1] + q.rect[3];
            const float rectCorners[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
            const float* c = q.hasCorners ? q.corners : rectCorners;
            const float s = q.solid ? 1.0f : 0.0f;
            const float* t = q.tint;
            v[i * 4 + 0] = {c[0], c[1], q.uv[0], q.uv[1], t[0], t[1], t[2], t[3], s};
            v[i * 4 + 1] = {c[2], c[3], q.uv[2], q.uv[1], t[0], t[1], t[2], t[3], s};
            v[i * 4 + 2] = {c[4], c[5], q.uv[2], q.uv[3], t[0], t[1], t[2], t[3], s};
            v[i * 4 + 3] = {c[6], c[7], q.uv[0], q.uv[3], t[0], t[1], t[2], t[3], s};
        }
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(bgfx::IndexBufferHandle{chunkIb_}, 0, count * 6);
        bindTexture(tex == kSpriteAtlasTexture ? spriteTex_ : tex);
        bgfx::setState(layerBlendState(additive));
        bgfx::submit(view, bgfx::ProgramHandle{program_});
        ++lastDrawCalls_;
        lastDrawnQuads_ += count;
        first = runEnd;
    }
    drawMarkers(view, l.draws, true, SIZE_MAX, next, spriteTex_);   // what was drawn after its last quad
}

void BgfxRenderer::submitComposite(int layer, uint16_t view) {
    if (layer < 0 || (size_t)layer >= layers_.size()) return;
    const LayerDraw& l = layers_[(size_t)layer];
    if (l.slot < 0 || (size_t)l.slot >= layerFb_.size()) return;
    if (bgfx::getAvailTransientVertexBuffer(4, spriteLayout()) < 4) return;
    if (!l.any) return;
    // The picture's content and its effect's reach (the rest of the picture is not cleared this frame); a
    // bottom-left-origin backend (OpenGL) stored it upside down.
    const float W = (float)kDesignW, H = (float)kDesignH;
    const float x0 = std::max(0.0f, l.x0 - l.reach), y0 = std::max(0.0f, l.y0 - l.reach);
    const float x1 = std::min(W, l.x1 + l.reach), y1 = std::min(H, l.y1 + l.reach);
    if (x1 <= x0 || y1 <= y0) return;
    const bool flip = bgfx::getCaps()->originBottomLeft;
    auto vOf = [&](float y) { return flip ? 1.0f - y / H : y / H; };
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 4, spriteLayout());
    auto* v = reinterpret_cast<SpriteVertex*>(tvb.data);
    v[0] = {x0, y0, x0 / W, vOf(y0), 1, 1, 1, 1, 0};
    v[1] = {x1, y0, x1 / W, vOf(y0), 1, 1, 1, 1, 0};
    v[2] = {x1, y1, x1 / W, vOf(y1), 1, 1, 1, 1, 0};
    v[3] = {x0, y1, x0 / W, vOf(y1), 1, 1, 1, 1, 0};
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(bgfx::IndexBufferHandle{chunkIb_}, 0, 6);
    // Lengths are design pixels; the shader takes them in texture units (y turned for a flipped picture).
    const float mode[4] = {(float)l.fx.kind, std::clamp(l.fx.opacity, 0.0f, 1.0f), 0, 0};
    const float texel[4] = {1.0f / W, (flip ? -1.0f : 1.0f) / H, W, H};
    bgfx::setUniform(bgfx::UniformHandle{uLayerMode_}, mode);
    bgfx::setUniform(bgfx::UniformHandle{uLayerParams_}, l.fx.params);
    bgfx::setUniform(bgfx::UniformHandle{uLayerColor_}, l.fx.color);
    bgfx::setUniform(bgfx::UniformHandle{uLayerTexel_}, texel);
    bgfx::setTexture(0, bgfx::UniformHandle{sampler_}, bgfx::getTexture(bgfx::FrameBufferHandle{layerFb_[(size_t)l.slot]}),
                     BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(view, bgfx::ProgramHandle{layerProgram_});
    ++lastDrawCalls_;
}

void BgfxRenderer::applyViewOrder() {
    // Plain id order, unless a 3D scene (setSceneViews) or layer pictures must run right after the clear: then the clear,
    // the scene's views, the layers' views (inner first), and every other view in id order.
    const bool scene = sceneCount_ > 0 && sceneFirst_ > kViewOverlay;
    if (!scene && lastLayerCount_ == 0) {
        if (orderApplied_) bgfx::setViewOrder(0, UINT16_MAX, nullptr);   // back to the plain id order
        orderApplied_ = false;
        return;
    }
    const uint16_t maxViews = (uint16_t)bgfx::getCaps()->limits.maxViews;
    std::vector<bgfx::ViewId> order;
    std::vector<unsigned char> placed(maxViews, 0);
    auto put = [&](uint16_t v) {
        if (v < maxViews && !placed[v]) { placed[v] = 1; order.push_back(v); }
    };
    put(kViewClear);
    if (scene)
        for (uint16_t v = sceneFirst_; v < uint16_t(sceneFirst_ + sceneCount_); v++) put(v);
    for (size_t k = 0; k < std::min(layerClosed_.size(), layerFb_.size()); k++) put(uint16_t(kViewLayerFirst + k));   // an empty one: untouched, a no-op
    for (uint16_t v = 0; v < maxViews; v++) put(v);
    bgfx::setViewOrder(0, (uint16_t)order.size(), order.data());
    orderApplied_ = true;
}

void BgfxRenderer::savePNG(const std::string& path) {
    // The host's bgfx callback (bgfx_host.cpp) writes the PNG a frame or two later.
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.c_str());
}

}  // namespace toms::next
