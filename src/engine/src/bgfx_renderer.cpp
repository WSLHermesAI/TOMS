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
    float m_fX, m_fY;
    float m_fU, m_fV;
    float m_fR, m_fG, m_fB, m_fA;
    float m_fSolid;
};

bgfx::VertexLayout& f_SpriteLayout() {
    static bgfx::VertexLayout l_oLayout;
    static bool l_bReady = false;
    if (!l_bReady) {
        l_oLayout.begin()
            .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 1, bgfx::AttribType::Float)
            .end();
        l_bReady = true;
    }
    return l_oLayout;
}

// One instance of the GPU path: what vs_sprite_inst.sc reads as i_data0..3.
struct QuadInstance {
    float m_fC0x, m_fC0y, m_fC1x, m_fC1y;   // corners 0, 1
    float m_fC2x, m_fC2y, m_fC3x, m_fC3y;   // corners 2, 3
    float m_fU0, m_fV0, m_fU1, m_fV1;       // uv rect; u0 = -1: a solid quad (tint only)
    float m_fR, m_fG, m_fB, m_fA;
};
static_assert(sizeof(QuadInstance) == 64, "instance stride must be a multiple of 16 bytes");

// Compute path layouts: the input is 4 vec4 per quad, the output 3 vec4 per vertex.
bgfx::VertexLayout& f_CsInLayout() {
    static bgfx::VertexLayout l_oLayout;
    static bool l_bReady = false;
    if (!l_bReady) {
        l_oLayout.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();   // one vec4 element
        l_bReady = true;
    }
    return l_oLayout;
}
bgfx::VertexLayout& f_CsOutLayout() {
    static bgfx::VertexLayout l_oLayout;
    static bool l_bReady = false;
    if (!l_bReady) {
        l_oLayout.begin()
            .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)   // x, y, u, v
            .add(bgfx::Attrib::Color1,    4, bgfx::AttribType::Float)   // tint
            .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)   // solid, 0, 0, 0
            .end();
        l_bReady = true;
    }
    return l_oLayout;
}
constexpr uint32_t g_uChunk = 16384;   // quads per draw on the CPU / compute paths: 65536 vertices, 16-bit indices

// The instancing path's per-quad data in a vertex buffer: 4 vec4 = 64 bytes (QuadInstance).
bgfx::VertexLayout& f_InstanceLayout() {
    static bgfx::VertexLayout l_oLayout;
    static bool l_bReady = false;
    if (!l_bReady) {
        l_oLayout.begin()
            .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
            .end();
        l_bReady = true;
    }
    return l_oLayout;
}
constexpr uint16_t g_u16ComputeBufferFlags = BGFX_BUFFER_NONE;   // this bgfx takes the element format from the layout (vec4)

// The shared unit quad: just its corner, (0,0) (1,0) (1,1) (0,1).
bgfx::VertexLayout& f_UnitQuadLayout() {
    static bgfx::VertexLayout l_oLayout;
    static bool l_bReady = false;
    if (!l_bReady) {
        l_oLayout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();
        l_bReady = true;
    }
    return l_oLayout;
}

uint32_t f_PackClearColor(const float l_arrC[4]) {
    auto l_fnB = [](float l_fF) { return (uint32_t)std::lround(std::clamp(l_fF, 0.0f, 1.0f) * 255.0f); };
    return (l_fnB(l_arrC[0]) << 24) | (l_fnB(l_arrC[1]) << 16) | (l_fnB(l_arrC[2]) << 8) | l_fnB(l_arrC[3]);
}

}  // namespace

BgfxRenderer::~BgfxRenderer() { f_Destroy(); }

BgfxRenderer::ViewportRect BgfxRenderer::f_ComputeAspectFitViewport(uint32_t l_uDeviceW, uint32_t l_uDeviceH,
                                                                  uint32_t l_uTargetW, uint32_t l_uTargetH) {
    if (l_uDeviceW == 0 || l_uDeviceH == 0 || l_uTargetW == 0 || l_uTargetH == 0) return {0, 0, 0, 0};
    float l_fScale = std::min((float)l_uDeviceW / (float)l_uTargetW, (float)l_uDeviceH / (float)l_uTargetH);
    float l_fVw = (float)l_uTargetW * l_fScale, l_fVh = (float)l_uTargetH * l_fScale;
    return {((float)l_uDeviceW - l_fVw) / 2.0f, ((float)l_uDeviceH - l_fVh) / 2.0f, l_fVw, l_fVh};
}

bool BgfxRenderer::f_DeviceToDesign(double l_dDeviceX, double l_dDeviceY, float& l_fOutX, float& l_fOutY) const {
    ViewportRect l_oFit = f_ComputeAspectFitViewport(m_uDevW, m_uDevH, m_uDesignW, m_uDesignH);
    if (l_oFit.m_fWidth <= 0.0f || l_oFit.m_fHeight <= 0.0f) return false;
    double l_dLx = l_dDeviceX - l_oFit.m_fX, l_dLy = l_dDeviceY - l_oFit.m_fY;
    if (l_dLx < 0 || l_dLy < 0 || l_dLx > l_oFit.m_fWidth || l_dLy > l_oFit.m_fHeight) return false;
    l_fOutX = (float)(l_dLx / l_oFit.m_fWidth * m_uDesignW);
    l_fOutY = (float)(l_dLy / l_oFit.m_fHeight * m_uDesignH);
    return true;
}

void BgfxRenderer::f_Init(uint32_t, uint32_t) {
    bgfx::ProgramHandle l_oProg = f_CreateEmbeddedProgram(ShaderProgram::Sprite);
    m_u16Program = l_oProg.idx;
    m_u16Sampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler).idx;
    if (m_u16Program == m_u16Invalid)
        std::fprintf(stderr, "[toms] BgfxRenderer: sprite program failed to load\n");
    // The CPU and compute paths' index buffer: kChunk quads, 0 1 2 0 2 3 per quad.
    {
        static std::vector<uint16_t> l_vIndices;
        if (l_vIndices.empty())
            for (uint32_t l_uQ = 0; l_uQ < g_uChunk; l_uQ++)
                for (uint16_t l_u16K : {0, 1, 2, 0, 2, 3}) l_vIndices.push_back((uint16_t)(l_uQ * 4 + l_u16K));
        m_u16ChunkIb = bgfx::createIndexBuffer(bgfx::makeRef(l_vIndices.data(), (uint32_t)(l_vIndices.size() * sizeof(uint16_t)))).idx;
    }
    // The GPU path (instancing): every backend this bgfx supports can instance, so it is on whenever
    // the instanced program exists for the active backend.
    m_u16InstProgram = f_CreateEmbeddedProgram(ShaderProgram::SpriteInstanced).idx;
    if (m_u16InstProgram != m_u16Invalid) {
        static const float l_arrCorners[8] = {0, 0, 1, 0, 1, 1, 0, 1};
        static const uint16_t l_arrIndices[6] = {0, 1, 2, 0, 2, 3};
        m_u16QuadVb = bgfx::createVertexBuffer(bgfx::makeRef(l_arrCorners, sizeof(l_arrCorners)), f_UnitQuadLayout()).idx;
        m_u16QuadIb = bgfx::createIndexBuffer(bgfx::makeRef(l_arrIndices, sizeof(l_arrIndices))).idx;
    }
    // The compute path: only where the backend runs compute shaders and has the programs.
    if (bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE) {
        m_u16CsProgram = f_CreateEmbeddedProgram(ShaderProgram::SpriteCompute).idx;
        m_u16CsDrawProgram = f_CreateEmbeddedProgram(ShaderProgram::SpriteFromCompute).idx;
        if (m_u16CsProgram != m_u16Invalid && m_u16CsDrawProgram != m_u16Invalid) {
            m_u16CsParams = bgfx::createUniform("u_spriteParams", bgfx::UniformType::Vec4).idx;
            // GPU-simulated particles: the same compute support, two more programs.
            m_u16FxSpawn = f_CreateEmbeddedProgram(ShaderProgram::FxSpawn).idx;
            m_u16FxUpdate = f_CreateEmbeddedProgram(ShaderProgram::FxUpdate).idx;
            if (m_u16FxSpawn != m_u16Invalid && m_u16FxUpdate != m_u16Invalid) {
                m_u16UFxSim = bgfx::createUniform("u_fxSim", bgfx::UniformType::Vec4).idx;
                m_u16UFxForce = bgfx::createUniform("u_fxForce", bgfx::UniformType::Vec4).idx;
                m_u16UFxOrigin = bgfx::createUniform("u_fxOrigin", bgfx::UniformType::Vec4).idx;
                m_u16UFxXform = bgfx::createUniform("u_fxXform", bgfx::UniformType::Vec4, 2).idx;
                m_u16UFxTint = bgfx::createUniform("u_fxTint", bgfx::UniformType::Vec4).idx;
                m_u16UFxColorLut = bgfx::createUniform("u_fxColorLut", bgfx::UniformType::Vec4, GpuParticleFrame::m_uLut).idx;
                m_u16UFxScalarLut = bgfx::createUniform("u_fxScalarLut", bgfx::UniformType::Vec4, GpuParticleFrame::m_uLut).idx;
                m_u16UFxFrameUv = bgfx::createUniform("u_fxFrameUv", bgfx::UniformType::Vec4, GpuParticleFrame::m_uMaxFrames).idx;
                m_u16UFxFrameRect = bgfx::createUniform("u_fxFrameRect", bgfx::UniformType::Vec4, GpuParticleFrame::m_uMaxFrames).idx;
            }
        }
    }
    // Layers (beginLayer): the composite program and its uniforms.
    m_u16LayerProgram = f_CreateEmbeddedProgram(ShaderProgram::Layer).idx;
    if (m_u16LayerProgram != m_u16Invalid) {
        m_u16ULayerMode = bgfx::createUniform("u_layerMode", bgfx::UniformType::Vec4).idx;
        m_u16ULayerParams = bgfx::createUniform("u_layerParams", bgfx::UniformType::Vec4).idx;
        m_u16ULayerColor = bgfx::createUniform("u_layerColor", bgfx::UniformType::Vec4).idx;
        m_u16ULayerTexel = bgfx::createUniform("u_layerTexel", bgfx::UniformType::Vec4).idx;
    }
    std::fprintf(stderr, "[toms] sprite batch: %s (compute %s, instancing %s, GPU particles %s, layers %s)\n", f_SpritePathName(f_SpritePath()),
                 f_HasComputePath() ? "yes" : "no", f_HasInstancingPath() ? "yes" : "no", f_SupportsGpuParticles() ? "yes" : "no",
                 f_SupportsLayers() ? "yes" : "no");
}

BgfxRenderer::SpritePath BgfxRenderer::f_SpritePath() const {
    // Auto: instancing (measured fastest: 30000 particles, D3D11 -- instancing 1.37 ms per frame,
    // CPU vertices 1.70 ms, compute 1.76 ms: compute expansion adds a pass and removes no CPU work).
    if (m_eWantPath == SpritePath::Auto) {
        if (f_HasInstancingPath()) return SpritePath::Instancing;
        if (f_HasComputePath()) return SpritePath::Compute;
        return SpritePath::Cpu;
    }
    // A wanted path, else the next one down: compute -> instancing -> CPU.
    switch (m_eWantPath) {
    case SpritePath::Auto:
    case SpritePath::Compute:
        if (f_HasComputePath()) return SpritePath::Compute;
        [[fallthrough]];
    case SpritePath::Instancing:
        if (f_HasInstancingPath()) return SpritePath::Instancing;
        [[fallthrough]];
    case SpritePath::Cpu: break;
    }
    return SpritePath::Cpu;
}

const char* BgfxRenderer::f_SpritePathName(SpritePath l_eP) {
    switch (l_eP) {
    case SpritePath::Compute: return "GPU compute";
    case SpritePath::Instancing: return "GPU instancing";
    case SpritePath::Cpu: return "CPU vertices";
    case SpritePath::Auto: break;
    }
    return "auto";
}

void BgfxRenderer::f_Destroy() {
    f_WithdrawAllTextures();
    f_DestroyTexture(m_u16SpriteTex);
    for (uint16_t& l_u16T : m_vExtraTex) f_DestroyTexture(l_u16T);
    m_vExtraTex.clear();
    if (m_u16Program != m_u16Invalid) { bgfx::destroy(bgfx::ProgramHandle{m_u16Program}); m_u16Program = m_u16Invalid; }
    if (m_u16InstProgram != m_u16Invalid) { bgfx::destroy(bgfx::ProgramHandle{m_u16InstProgram}); m_u16InstProgram = m_u16Invalid; }
    if (m_u16QuadVb != m_u16Invalid) { bgfx::destroy(bgfx::VertexBufferHandle{m_u16QuadVb}); m_u16QuadVb = m_u16Invalid; }
    if (m_u16QuadIb != m_u16Invalid) { bgfx::destroy(bgfx::IndexBufferHandle{m_u16QuadIb}); m_u16QuadIb = m_u16Invalid; }
    if (m_u16CsProgram != m_u16Invalid) { bgfx::destroy(bgfx::ProgramHandle{m_u16CsProgram}); m_u16CsProgram = m_u16Invalid; }
    if (m_u16CsDrawProgram != m_u16Invalid) { bgfx::destroy(bgfx::ProgramHandle{m_u16CsDrawProgram}); m_u16CsDrawProgram = m_u16Invalid; }
    if (m_u16CsParams != m_u16Invalid) { bgfx::destroy(bgfx::UniformHandle{m_u16CsParams}); m_u16CsParams = m_u16Invalid; }
    for (uint32_t l_uId = 1; l_uId <= m_vGpuEmitters.size(); l_uId++) f_ReleaseGpuParticles(l_uId);
    m_vGpuEmitters.clear();
    for (uint16_t* l_pP : {&m_u16FxSpawn, &m_u16FxUpdate})
        if (*l_pP != m_u16Invalid) { bgfx::destroy(bgfx::ProgramHandle{*l_pP}); *l_pP = m_u16Invalid; }
    for (uint16_t* l_pU : {&m_u16UFxSim, &m_u16UFxForce, &m_u16UFxOrigin, &m_u16UFxXform, &m_u16UFxTint, &m_u16UFxColorLut, &m_u16UFxScalarLut, &m_u16UFxFrameUv, &m_u16UFxFrameRect})
        if (*l_pU != m_u16Invalid) { bgfx::destroy(bgfx::UniformHandle{*l_pU}); *l_pU = m_u16Invalid; }
    if (m_u16CsIn != m_u16Invalid) { bgfx::destroy(bgfx::DynamicVertexBufferHandle{m_u16CsIn}); m_u16CsIn = m_u16Invalid; }
    if (m_u16CsOut != m_u16Invalid) { bgfx::destroy(bgfx::DynamicVertexBufferHandle{m_u16CsOut}); m_u16CsOut = m_u16Invalid; }
    if (m_u16ChunkIb != m_u16Invalid) { bgfx::destroy(bgfx::IndexBufferHandle{m_u16ChunkIb}); m_u16ChunkIb = m_u16Invalid; }
    for (uint16_t* l_pVb : {&m_u16CpuVb, &m_u16InstVb, &m_u16MeshVb})
        if (*l_pVb != m_u16Invalid) { bgfx::destroy(bgfx::DynamicVertexBufferHandle{*l_pVb}); *l_pVb = m_u16Invalid; }
    if (m_u16MeshIb != m_u16Invalid) { bgfx::destroy(bgfx::DynamicIndexBufferHandle{m_u16MeshIb}); m_u16MeshIb = m_u16Invalid; }
    m_uCsCapacity = m_uCsOutCap = m_uCpuCap = m_uInstCap = m_uMeshVbCap = m_uMeshIbCap = 0;
    if (m_u16Sampler != m_u16Invalid) { bgfx::destroy(bgfx::UniformHandle{m_u16Sampler}); m_u16Sampler = m_u16Invalid; }
    for (uint16_t l_u16Fb : m_vLayerFb) bgfx::destroy(bgfx::FrameBufferHandle{l_u16Fb});
    m_vLayerFb.clear();
    m_u16LayerW = m_u16LayerH = 0;
    if (m_u16LayerProgram != m_u16Invalid) { bgfx::destroy(bgfx::ProgramHandle{m_u16LayerProgram}); m_u16LayerProgram = m_u16Invalid; }
    for (uint16_t* l_pU : {&m_u16ULayerMode, &m_u16ULayerParams, &m_u16ULayerColor, &m_u16ULayerTexel})
        if (*l_pU != m_u16Invalid) { bgfx::destroy(bgfx::UniformHandle{*l_pU}); *l_pU = m_u16Invalid; }
    m_vLinearTex.clear();
    for (uint16_t* l_pT : {&m_u16CaptureRead})
        if (*l_pT != m_u16Invalid) { bgfx::destroy(bgfx::TextureHandle{*l_pT}); *l_pT = m_u16Invalid; }
    if (m_u16CaptureFb != m_u16Invalid) { bgfx::destroy(bgfx::FrameBufferHandle{m_u16CaptureFb}); m_u16CaptureFb = m_u16Invalid; }
    m_u16CaptureFbW = m_u16CaptureFbH = 0;
}

void BgfxRenderer::f_SetCapture(uint16_t l_u16W, uint16_t l_u16H) {
    m_u16CaptureW = l_u16W && l_u16H ? l_u16W : 0;
    m_u16CaptureH = l_u16W && l_u16H ? l_u16H : 0;
}

void BgfxRenderer::f_PrepareCapture() {
    m_uCaptureFrame = 0;
    if (!m_u16CaptureW) {
        bgfx::setViewFrameBuffer(m_u16ViewGame, BGFX_INVALID_HANDLE);   // the window again (kViewClear clears it)
        bgfx::setViewClear(m_u16ViewGame, BGFX_CLEAR_NONE);
        return;
    }
    if (m_u16CaptureFbW != m_u16CaptureW || m_u16CaptureFbH != m_u16CaptureH || m_u16CaptureFb == m_u16Invalid) {
        if (m_u16CaptureFb != m_u16Invalid) bgfx::destroy(bgfx::FrameBufferHandle{m_u16CaptureFb});
        if (m_u16CaptureRead != m_u16Invalid) bgfx::destroy(bgfx::TextureHandle{m_u16CaptureRead});
#if TOMS_GAMMA_SPACE
        const uint64_t l_u64Srgb = 0;
#else
        const uint64_t l_u64Srgb = BGFX_TEXTURE_SRGB;   // written as the sRGB backbuffer would show it
#endif
        const bgfx::TextureHandle l_oT = bgfx::createTexture2D(m_u16CaptureW, m_u16CaptureH, false, 1, bgfx::TextureFormat::RGBA8,
                                                            BGFX_TEXTURE_RT | l_u64Srgb | BGFX_SAMPLER_UVW_CLAMP);
        m_u16CaptureFb = bgfx::createFrameBuffer(1, &l_oT, true).idx;
        m_u16CaptureRead = bgfx::createTexture2D(m_u16CaptureW, m_u16CaptureH, false, 1, bgfx::TextureFormat::RGBA8,
                                             BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | l_u64Srgb).idx;
        m_u16CaptureFbW = m_u16CaptureW;
        m_u16CaptureFbH = m_u16CaptureH;
    }
    if (m_u16CaptureFb == m_u16Invalid || m_u16CaptureRead == m_u16Invalid) return;
    bgfx::setViewFrameBuffer(m_u16ViewGame, bgfx::FrameBufferHandle{m_u16CaptureFb});
    bgfx::setViewRect(m_u16ViewGame, 0, 0, m_u16CaptureW, m_u16CaptureH);
    bgfx::setViewClear(m_u16ViewGame, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    bgfx::touch(m_u16ViewGame);
    m_vCapturePx.resize((size_t)m_u16CaptureW * m_u16CaptureH * 4);
    bgfx::touch(m_u16ViewCapture);
    bgfx::TextureRegion l_oDst, l_oSrc;
    l_oDst.init(bgfx::TextureHandle{m_u16CaptureRead});
    l_oSrc.init(bgfx::getTexture(bgfx::FrameBufferHandle{m_u16CaptureFb}));
    bgfx::blit(m_u16ViewCapture, l_oDst, l_oSrc);
    bgfx::TextureRegion l_oAll;
    l_oAll.init(bgfx::TextureHandle{m_u16CaptureRead});
    m_uCaptureFrame = bgfx::read(l_oAll, m_vCapturePx.data());
}

void BgfxRenderer::f_DestroyTexture(uint16_t& l_u16Handle) {
    if (l_u16Handle != m_u16Invalid) { bgfx::destroy(bgfx::TextureHandle{l_u16Handle}); l_u16Handle = m_u16Invalid; }
}

uint16_t BgfxRenderer::f_CreateAtlas(const std::vector<uint8_t>& l_vPx, uint32_t l_uW, uint32_t l_uH) {
    if (l_uW == 0 || l_uH == 0 || l_vPx.size() < (size_t)l_uW * l_uH * 4) return m_u16Invalid;
    const uint32_t l_uMaxSize = bgfx::getCaps()->limits.maxTextureSize;
    if (l_uW > l_uMaxSize || l_uH > l_uMaxSize) {
        std::fprintf(stderr, "[toms] atlas %ux%u exceeds the GPU limit %u -- not uploaded\n", l_uW, l_uH, l_uMaxSize);
        return m_u16Invalid;
    }
#if TOMS_GAMMA_SPACE
    // No sRGB backbuffer (the web, Android: color_space.h), so sampling sRGB textures would darken everything. Same
    // as the old WebGL renderer: plain RGBA8, blending in gamma space.
    const uint64_t l_u64Flags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
#else
    const uint64_t l_u64Flags = BGFX_TEXTURE_SRGB | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
#endif
    return bgfx::createTexture2D((uint16_t)l_uW, (uint16_t)l_uH, false, 1, bgfx::TextureFormat::RGBA8, l_u64Flags,
                                 bgfx::copy(l_vPx.data(), (uint32_t)((size_t)l_uW * l_uH * 4))).idx;
}

void BgfxRenderer::f_LoadSprites(const std::vector<std::vector<uint8_t>>& l_vLayers, uint32_t l_uSw, uint32_t l_uSh) {
    uint32_t l_uAw = 0, l_uAh = 0;
    std::vector<uint8_t> l_vAtlas;
    if (!f_PackAtlas(l_vLayers, l_uSw, l_uSh, 9, l_vAtlas, l_uAw, l_uAh)) {
        std::fprintf(stderr, "[toms] loadSprites: bad layers\n");
        return;
    }
    f_WithdrawAllTextures();   // the runtime grid is no file the UI could ask for
    f_DestroyTexture(m_u16SpriteTex);
    m_u16SpriteTex = f_CreateAtlas(l_vAtlas, l_uAw, l_uAh);
}

void BgfxRenderer::f_LoadSpriteAtlas(const std::vector<uint8_t>& l_vRgba, uint32_t l_uW, uint32_t l_uH, const std::string& l_strFile) {
    f_WithdrawAllTextures();   // before the old texture goes: the UI must not draw with it again
    f_DestroyTexture(m_u16SpriteTex);
    m_u16SpriteTex = f_CreateAtlas(l_vRgba, l_uW, l_uH);
    if (m_u16SpriteTex != m_u16Invalid && !l_strFile.empty())   // the UI's icons come from the same file (shared_textures.h)
        f_LendTexture(l_strFile, SharedTexture{m_u16SpriteTex, (int)l_uW, (int)l_uH, true});
}

uint16_t BgfxRenderer::f_LoadTexture(const std::vector<uint8_t>& l_vRgba, uint32_t l_uW, uint32_t l_uH) {
    const uint16_t l_u16T = f_CreateAtlas(l_vRgba, l_uW, l_uH);   // same sampling as the sprite atlas
    if (l_u16T == m_u16Invalid) return g_u16SpriteAtlasTexture;
    m_vExtraTex.push_back(l_u16T);
    return l_u16T;
}

uint16_t BgfxRenderer::f_CreateDynamicTexture(uint32_t l_uW, uint32_t l_uH) {
    if (l_uW == 0 || l_uH == 0) return g_u16SpriteAtlasTexture;
#if TOMS_GAMMA_SPACE
    const uint64_t l_u64Flags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
#else
    const uint64_t l_u64Flags = BGFX_TEXTURE_SRGB | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
#endif
    // No initial pixels: an updatable texture (updateTexture).
    const uint16_t l_u16T = bgfx::createTexture2D((uint16_t)l_uW, (uint16_t)l_uH, false, 1, bgfx::TextureFormat::RGBA8, l_u64Flags, nullptr).idx;
    if (l_u16T == m_u16Invalid) return g_u16SpriteAtlasTexture;
    m_vExtraTex.push_back(l_u16T);
    return l_u16T;
}

void BgfxRenderer::f_UpdateTexture(uint16_t l_u16Texture, const uint8_t* l_pRgba, uint32_t l_uW, uint32_t l_uH) {
    if (l_u16Texture == m_u16Invalid || l_u16Texture == g_u16SpriteAtlasTexture || !l_pRgba) return;
    bgfx::updateTexture2D(bgfx::TextureHandle{l_u16Texture}, 0, 0, 0, 0, (uint16_t)l_uW, (uint16_t)l_uH, bgfx::copy(l_pRgba, l_uW * l_uH * 4));
}

void BgfxRenderer::f_ReleaseTexture(uint16_t l_u16Texture) {
    auto l_itIt = std::find(m_vExtraTex.begin(), m_vExtraTex.end(), l_u16Texture);
    if (l_itIt == m_vExtraTex.end()) return;   // not ours (or the sprite atlas): nothing to free
    m_vLinearTex.erase(std::remove(m_vLinearTex.begin(), m_vLinearTex.end(), l_u16Texture), m_vLinearTex.end());
    f_DestroyTexture(*l_itIt);
    m_vExtraTex.erase(l_itIt);
}

void BgfxRenderer::f_SetTextureFilter(uint16_t l_u16Texture, bool l_bLinear) {
    if (l_u16Texture == m_u16Invalid) return;
    m_vLinearTex.erase(std::remove(m_vLinearTex.begin(), m_vLinearTex.end(), l_u16Texture), m_vLinearTex.end());
    if (l_bLinear) m_vLinearTex.push_back(l_u16Texture);
}

void BgfxRenderer::f_BindTexture(uint16_t l_u16Texture) {
    if (l_u16Texture == m_u16Invalid) return;
    // A linear texture: this draw's sampler flags replace the texture's point sampling (sRGB belongs to the texture).
    const bool l_bLinear = std::find(m_vLinearTex.begin(), m_vLinearTex.end(), l_u16Texture) != m_vLinearTex.end();
    bgfx::setTexture(0, bgfx::UniformHandle{m_u16Sampler}, bgfx::TextureHandle{l_u16Texture}, l_bLinear ? BGFX_SAMPLER_UVW_CLAMP : UINT32_MAX);
}

void BgfxRenderer::begin() {
    m_vSprites.clear();
    m_vTopSprites.clear();
    m_iLayer = 0;
    m_vGpuDraws.clear();
    m_vMeshVerts.clear();
    m_vMeshIdx.clear();
    m_vLayers.clear();
    m_vLayerStack.clear();
    m_vLayerClosed.clear();
}
void BgfxRenderer::f_SetNode(uint8_t l_u8N) { m_u8Node = l_u8N; }
void BgfxRenderer::f_SetNodeFilter(uint8_t l_u8N) { m_u8NodeFilter = l_u8N; }

void BgfxRenderer::f_DrawSprite(const Quad& l_oQ) {
    if (m_u8NodeFilter && m_u8Node != m_u8NodeFilter) return;
    std::vector<Quad>& l_vList = m_iLayer == 1 ? m_vTopSprites : f_CurrentQuads();
    l_vList.push_back(l_oQ);
    l_vList.back().m_u8Node = m_u8Node;
}

void BgfxRenderer::f_DrawMesh(const Mesh& l_oM) {
    if (m_u8NodeFilter && m_u8Node != m_u8NodeFilter) return;
    if (!f_ValidMesh(l_oM)) return;
    const uint32_t l_uFirst = (uint32_t)(m_vMeshVerts.size() / 9);
    // The CPU path's vertex (SpriteVertex): position, uv, the mesh's tint (times the vertex's colour), not solid.
    for (uint32_t l_uI = 0; l_uI < l_oM.m_uVertexCount; l_uI++) {
        const MeshVertex& l_oV = l_oM.m_pVertices[l_uI];
        const float* l_pC = l_oM.m_pColors ? l_oM.m_pColors + 4 * l_uI : nullptr;
        m_vMeshVerts.insert(m_vMeshVerts.end(), {l_oV.m_fX, l_oV.m_fY, l_oV.m_fU, l_oV.m_fV, l_oM.m_arrTint[0] * (l_pC ? l_pC[0] : 1.0f), l_oM.m_arrTint[1] * (l_pC ? l_pC[1] : 1.0f),
                                             l_oM.m_arrTint[2] * (l_pC ? l_pC[2] : 1.0f), l_oM.m_arrTint[3] * (l_pC ? l_pC[3] : 1.0f), 0.0f});
    }
    // Right after another mesh with the same texture and blending (no quad, particles or layer between): one draw for
    // both -- its indices then count from that mesh's first vertex. A crowd of skinned characters is a few draws.
    std::vector<GpuDraw>& l_vDraws = f_CurrentDraws();
    if (!l_vDraws.empty()) {
        GpuDraw& l_oPrev = l_vDraws.back();
        if (l_oPrev.m_uEmitter == 0 && l_oPrev.m_iLayer < 0 && l_oPrev.m_nAt == f_CurrentQuads().size() && l_oPrev.m_u16Texture == l_oM.m_u16Texture &&
            l_oPrev.m_bAdditive == l_oM.m_bAdditive && l_oPrev.m_uFirstIndex + l_oPrev.m_uIndexCount == m_vMeshIdx.size() &&
            (l_uFirst - l_oPrev.m_uFirstVertex) + l_oM.m_uVertexCount <= 65536u) {
            const uint16_t l_u16Base = uint16_t(l_uFirst - l_oPrev.m_uFirstVertex);
            for (uint32_t l_uI = 0; l_uI < l_oM.m_uIndexCount; l_uI++) m_vMeshIdx.push_back(uint16_t(l_oM.m_pIndices[l_uI] + l_u16Base));
            l_oPrev.m_uVertexCount += l_oM.m_uVertexCount;
            l_oPrev.m_uIndexCount += l_oM.m_uIndexCount;
            return;
        }
    }
    GpuDraw l_oD{f_CurrentQuads().size(), 0, l_oM.m_u16Texture, l_oM.m_bAdditive};
    l_oD.m_uFirstVertex = l_uFirst;
    l_oD.m_uVertexCount = l_oM.m_uVertexCount;
    l_oD.m_uFirstIndex = (uint32_t)m_vMeshIdx.size();
    l_oD.m_uIndexCount = l_oM.m_uIndexCount;
    m_vMeshIdx.insert(m_vMeshIdx.end(), l_oM.m_pIndices, l_oM.m_pIndices + l_oM.m_uIndexCount);
    l_vDraws.push_back(l_oD);
}

// This frame's meshes into their buffers, once (end(), before any path draws).
void BgfxRenderer::f_UploadMeshes() {
    if (m_vMeshIdx.empty()) return;
    const uint32_t l_uVerts = (uint32_t)(m_vMeshVerts.size() / 9), l_uIndices = (uint32_t)m_vMeshIdx.size();
    auto l_fnGrow = [](uint32_t l_uCap, uint32_t l_uNeed) { l_uCap = std::max(l_uCap, 1024u); while (l_uCap < l_uNeed) l_uCap *= 2; return l_uCap; };
    if (m_u16MeshVb == m_u16Invalid || l_uVerts > m_uMeshVbCap) {
        if (m_u16MeshVb != m_u16Invalid) bgfx::destroy(bgfx::DynamicVertexBufferHandle{m_u16MeshVb});
        m_uMeshVbCap = l_fnGrow(m_uMeshVbCap, l_uVerts);
        m_u16MeshVb = bgfx::createDynamicVertexBuffer(m_uMeshVbCap, f_SpriteLayout(), BGFX_BUFFER_NONE).idx;
    }
    if (m_u16MeshIb == m_u16Invalid || l_uIndices > m_uMeshIbCap) {
        if (m_u16MeshIb != m_u16Invalid) bgfx::destroy(bgfx::DynamicIndexBufferHandle{m_u16MeshIb});
        m_uMeshIbCap = l_fnGrow(m_uMeshIbCap, l_uIndices);
        m_u16MeshIb = bgfx::createDynamicIndexBuffer(m_uMeshIbCap, BGFX_BUFFER_NONE).idx;
    }
    if (m_u16MeshVb == m_u16Invalid || m_u16MeshIb == m_u16Invalid) {
        std::fprintf(stderr, "[toms] meshes: no buffers for %u vertices\n", l_uVerts);
        m_uMeshVbCap = m_uMeshIbCap = 0;
        return;
    }
    bgfx::update(bgfx::DynamicVertexBufferHandle{m_u16MeshVb}, 0, bgfx::copy(m_vMeshVerts.data(), (uint32_t)(m_vMeshVerts.size() * sizeof(float))));
    bgfx::update(bgfx::DynamicIndexBufferHandle{m_u16MeshIb}, 0, bgfx::copy(m_vMeshIdx.data(), l_uIndices * (uint32_t)sizeof(uint16_t)));
}

void BgfxRenderer::end() {
    m_uLastDrawCalls = 0;
    m_nLastDrawnQuads = 0;
    m_uLastGpuEmitters = 0;
    m_nLastMeshCount = 0;
    m_nLastQuadCount = m_vSprites.size();
    while (!m_vLayerStack.empty()) f_EndLayer();   // a layer left open ends with the frame
    for (const LayerDraw& l_oL : m_vLayers) m_nLastQuadCount += l_oL.m_vQuads.size();

    // View 0: clear the whole backbuffer (letterbox bars included) to the background colour.
    bgfx::setViewRect(m_u16ViewClear, 0, 0, (uint16_t)m_uDevW, (uint16_t)m_uDevH);
    bgfx::setViewClear(m_u16ViewClear, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, f_PackClearColor(g_arrBackgroundClearColor), 1.0f, 0);
    bgfx::touch(m_u16ViewClear);

    // View 1: the fixed design resolution, scaled and centred into the backbuffer.
    ViewportRect l_oFit = f_ComputeAspectFitViewport(m_uDevW, m_uDevH, m_uDesignW, m_uDesignH);
    bgfx::setViewRect(m_u16ViewGame, (uint16_t)std::lround(l_oFit.m_fX), (uint16_t)std::lround(l_oFit.m_fY),
                      (uint16_t)std::lround(l_oFit.m_fWidth), (uint16_t)std::lround(l_oFit.m_fHeight));
    bgfx::setViewMode(m_u16ViewGame, bgfx::ViewMode::Sequential);   // keep submission order
    float l_arrProj[16];
    bx::mtxOrtho(l_arrProj, 0.0f, (float)m_uDesignW, (float)m_uDesignH, 0.0f, 0.0f, 100.0f, 0.0f,
                 bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(m_u16ViewGame, nullptr, l_arrProj);
    f_PrepareCapture();
    // The top layer: the same space, a view after kViewUi, so its sprites are drawn over the UI.
    bgfx::setViewRect(m_u16ViewTop, (uint16_t)std::lround(l_oFit.m_fX), (uint16_t)std::lround(l_oFit.m_fY),
                      (uint16_t)std::lround(l_oFit.m_fWidth), (uint16_t)std::lround(l_oFit.m_fHeight));
    bgfx::setViewMode(m_u16ViewTop, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(m_u16ViewTop, nullptr, l_arrProj);

    if (m_u16Program == m_u16Invalid) return;
    f_SubmitTop();
    f_UploadMeshes();
    f_PrepareLayers();
    f_ApplyViewOrder();
    // text and UI are RmlUi's, in view kViewUi
    if (m_vSprites.empty()) {   // only GPU particles and meshes (if any)
        size_t l_nNext = 0;
        f_DrawGpuAt(SIZE_MAX, l_nNext, m_u16SpriteTex);
        return;
    }
    const SpritePath l_ePath = f_SpritePath();
    if (l_ePath == SpritePath::Compute && f_SubmitQuadsCompute(m_vSprites, m_u16SpriteTex)) return;
    if (l_ePath != SpritePath::Cpu && f_SubmitQuadsInstanced(m_vSprites, m_u16SpriteTex)) return;
    f_SubmitQuads(m_vSprites, m_u16SpriteTex);
}

namespace {
bool f_EnsureBuffer(uint16_t& l_u16Handle, uint32_t& l_uCapacity, uint32_t l_uNeed, const bgfx::VertexLayout& l_oLayout, uint16_t l_u16Flags) {
    constexpr uint16_t l_u16Invalid = UINT16_MAX;
    if (l_uNeed <= l_uCapacity && l_u16Handle != l_u16Invalid) return true;
    uint32_t l_uCap = 1024;
    while (l_uCap < l_uNeed) l_uCap *= 2;
    if (l_u16Handle != l_u16Invalid) bgfx::destroy(bgfx::DynamicVertexBufferHandle{l_u16Handle});
    l_u16Handle = bgfx::createDynamicVertexBuffer(l_uCap, l_oLayout, l_u16Flags).idx;
    l_uCapacity = l_u16Handle == l_u16Invalid ? 0 : l_uCap;
    return l_u16Handle != l_u16Invalid;
}
}  // namespace

bool BgfxRenderer::f_SubmitQuadsCompute(const std::vector<Quad>& l_vQuads, uint16_t l_u16Texture) {
    const uint32_t l_uN = (uint32_t)l_vQuads.size();
    if (l_uN == 0) return true;
    uint32_t l_uOutCap = m_uCsOutCap;
    if (!f_EnsureBuffer(m_u16CsIn, m_uCsCapacity, l_uN * 4, f_CsInLayout(), BGFX_BUFFER_COMPUTE_READ | g_u16ComputeBufferFlags) ||
        !f_EnsureBuffer(m_u16CsOut, l_uOutCap, l_uN * 4, f_CsOutLayout(), BGFX_BUFFER_COMPUTE_WRITE | g_u16ComputeBufferFlags))
        return false;
    m_uCsOutCap = l_uOutCap;
    // The quads in, as m_pParticleInSSO was filled: corners, uv rect (u0 = -1: solid), tint.
    m_vCsScratch.resize((size_t)l_uN * 16);
    float* l_pD = m_vCsScratch.data();
    for (const Quad& l_oQ : l_vQuads) {
        const float l_fX0 = l_oQ.m_arrRect[0], l_fY0 = l_oQ.m_arrRect[1], l_fX1 = l_oQ.m_arrRect[0] + l_oQ.m_arrRect[2], l_fY1 = l_oQ.m_arrRect[1] + l_oQ.m_arrRect[3];
        const float l_arrRectCorners[8] = {l_fX0, l_fY0, l_fX1, l_fY0, l_fX1, l_fY1, l_fX0, l_fY1};
        const float* l_pC = l_oQ.m_bHasCorners ? l_oQ.m_arrCorners : l_arrRectCorners;
        std::memcpy(l_pD, l_pC, 8 * sizeof(float));
        l_pD[8] = l_oQ.m_bSolid ? -1.0f : l_oQ.m_arrUv[0]; l_pD[9] = l_oQ.m_arrUv[1]; l_pD[10] = l_oQ.m_arrUv[2]; l_pD[11] = l_oQ.m_arrUv[3];
        std::memcpy(l_pD + 12, l_oQ.m_arrTint, 4 * sizeof(float));
        l_pD += 16;
    }
    bgfx::update(bgfx::DynamicVertexBufferHandle{m_u16CsIn}, 0, bgfx::copy(m_vCsScratch.data(), (uint32_t)(m_vCsScratch.size() * sizeof(float))));
    // Expand them on the GPU, in the clear view: views run in order, so before kViewGame draws.
    const float l_arrParams[4] = {(float)l_uN, 0, 0, 0};
    bgfx::setUniform(bgfx::UniformHandle{m_u16CsParams}, l_arrParams);
    bgfx::setBuffer(0, bgfx::DynamicVertexBufferHandle{m_u16CsIn}, bgfx::Access::Read);
    bgfx::setBuffer(1, bgfx::DynamicVertexBufferHandle{m_u16CsOut}, bgfx::Access::Write);
    bgfx::dispatch(m_u16ViewClear, bgfx::ProgramHandle{m_u16CsProgram}, (l_uN + 63) / 64, 1, 1);
    // Draw straight from the output buffer: one draw per run of texture / blending, in chunks.
    size_t l_nFirst = 0, l_nNextGpu = 0;
    while (l_nFirst < l_uN) {
        const size_t l_nLimit = f_DrawGpuAt(l_nFirst, l_nNextGpu, l_u16Texture);
        const bool l_bAdditive = l_vQuads[l_nFirst].m_bAdditive;
        const uint16_t l_u16Tex = l_vQuads[l_nFirst].m_u16Texture;
        size_t l_nRunEnd = l_nFirst + 1;
        while (l_nRunEnd < l_uN && l_nRunEnd < l_nLimit && l_vQuads[l_nRunEnd].m_bAdditive == l_bAdditive && l_vQuads[l_nRunEnd].m_u16Texture == l_u16Tex) ++l_nRunEnd;
        for (size_t l_nAt = l_nFirst; l_nAt < l_nRunEnd; l_nAt += g_uChunk) {
            const uint32_t l_uCount = (uint32_t)std::min<size_t>(g_uChunk, l_nRunEnd - l_nAt);
            bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{m_u16CsOut}, (uint32_t)l_nAt * 4, l_uCount * 4);
            bgfx::setIndexBuffer(bgfx::IndexBufferHandle{m_u16ChunkIb}, 0, l_uCount * 6);
            const uint16_t l_u16Use = l_u16Tex == g_u16SpriteAtlasTexture ? l_u16Texture : l_u16Tex;
            f_BindTexture(l_u16Use);
            bgfx::setState(f_BlendState(l_bAdditive));
            bgfx::submit(m_u16ViewGame, bgfx::ProgramHandle{m_u16CsDrawProgram});
            ++m_uLastDrawCalls;
            m_nLastDrawnQuads += l_uCount;
        }
        l_nFirst = l_nRunEnd;
    }
    f_DrawGpuAt(SIZE_MAX, l_nNextGpu, l_u16Texture);   // GPU particles after the last quad
    return true;
}

uint64_t BgfxRenderer::f_BlendState(bool l_bAdditive) const {
    if (m_u16CaptureW) return f_LayerBlendState(l_bAdditive);   // a capture keeps its alpha: premultiplied, as a layer
    // Straight alpha: additive scales the colour by its alpha and adds it.
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
           (l_bAdditive ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE) : BGFX_STATE_BLEND_ALPHA);
}

// Grows a persistent dynamic vertex buffer to hold `need` vertices (powers of two: few regrowths).
// Persistent buffers instead of bgfx's per-frame transient ones: those are a few MB per frame, so
// 300000 quads did not fit and most were dropped.
bool BgfxRenderer::f_SubmitQuadsInstanced(const std::vector<Quad>& l_vQuads, uint16_t l_u16Texture) {
    const uint32_t l_uTotal = (uint32_t)l_vQuads.size();
    if (l_uTotal == 0) return true;
    if (!f_EnsureBuffer(m_u16InstVb, m_uInstCap, l_uTotal, f_InstanceLayout(), BGFX_BUFFER_NONE)) return false;
    m_vInstScratch.resize((size_t)l_uTotal * 16);
    auto* l_pInst = reinterpret_cast<QuadInstance*>(m_vInstScratch.data());
    for (uint32_t l_uI = 0; l_uI < l_uTotal; ++l_uI) {
        const Quad& l_oQ = l_vQuads[l_uI];
        const float l_fX0 = l_oQ.m_arrRect[0], l_fY0 = l_oQ.m_arrRect[1], l_fX1 = l_oQ.m_arrRect[0] + l_oQ.m_arrRect[2], l_fY1 = l_oQ.m_arrRect[1] + l_oQ.m_arrRect[3];
        const float l_arrRectCorners[8] = {l_fX0, l_fY0, l_fX1, l_fY0, l_fX1, l_fY1, l_fX0, l_fY1};
        const float* l_pC = l_oQ.m_bHasCorners ? l_oQ.m_arrCorners : l_arrRectCorners;
        l_pInst[l_uI] = {l_pC[0], l_pC[1], l_pC[2], l_pC[3], l_pC[4], l_pC[5], l_pC[6], l_pC[7],
                           l_oQ.m_bSolid ? -1.0f : l_oQ.m_arrUv[0], l_oQ.m_arrUv[1], l_oQ.m_arrUv[2], l_oQ.m_arrUv[3],
                           l_oQ.m_arrTint[0], l_oQ.m_arrTint[1], l_oQ.m_arrTint[2], l_oQ.m_arrTint[3]};
    }
    bgfx::update(bgfx::DynamicVertexBufferHandle{m_u16InstVb}, 0, bgfx::copy(l_pInst, l_uTotal * (uint32_t)sizeof(QuadInstance)));
    size_t l_nFirst = 0, l_nNextGpu = 0;
    while (l_nFirst < l_uTotal) {
        const size_t l_nLimit = f_DrawGpuAt(l_nFirst, l_nNextGpu, l_u16Texture);   // GPU particles placed before quad `first`
        // One draw call per run of quads with the same texture and blending, as the CPU path.
        const bool l_bAdditive = l_vQuads[l_nFirst].m_bAdditive;
        const uint16_t l_u16Tex = l_vQuads[l_nFirst].m_u16Texture;
        size_t l_nRunEnd = l_nFirst + 1;
        while (l_nRunEnd < l_uTotal && l_nRunEnd < l_nLimit && l_vQuads[l_nRunEnd].m_bAdditive == l_bAdditive && l_vQuads[l_nRunEnd].m_u16Texture == l_u16Tex) ++l_nRunEnd;
        bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{m_u16QuadVb});
        bgfx::setIndexBuffer(bgfx::IndexBufferHandle{m_u16QuadIb});
        bgfx::setInstanceDataBuffer(bgfx::DynamicVertexBufferHandle{m_u16InstVb}, (uint32_t)l_nFirst, (uint32_t)(l_nRunEnd - l_nFirst));
        const uint16_t l_u16Use = l_u16Tex == g_u16SpriteAtlasTexture ? l_u16Texture : l_u16Tex;
        f_BindTexture(l_u16Use);
        bgfx::setState(f_BlendState(l_bAdditive));
        bgfx::submit(m_u16ViewGame, bgfx::ProgramHandle{m_u16InstProgram});
        ++m_uLastDrawCalls;
        m_nLastDrawnQuads += l_nRunEnd - l_nFirst;
        l_nFirst = l_nRunEnd;
    }
    f_DrawGpuAt(SIZE_MAX, l_nNextGpu, l_u16Texture);   // GPU particles after the last quad
    return true;
}

void BgfxRenderer::f_SubmitQuads(const std::vector<Quad>& l_vQuads, uint16_t l_u16Texture) {
    const uint32_t l_uTotal = (uint32_t)l_vQuads.size();
    if (l_uTotal == 0) return;
    if (!f_EnsureBuffer(m_u16CpuVb, m_uCpuCap, l_uTotal * 4, f_SpriteLayout(), BGFX_BUFFER_NONE)) {
        std::fprintf(stderr, "[toms] sprite batch: no vertex buffer for %u quads\n", l_uTotal);
        return;
    }
    static_assert(sizeof(SpriteVertex) == 9 * sizeof(float), "SpriteVertex is packed floats");
    m_vCpuScratch.resize((size_t)l_uTotal * 4 * 9);
    auto* l_pV = reinterpret_cast<SpriteVertex*>(m_vCpuScratch.data());
    for (uint32_t l_uI = 0; l_uI < l_uTotal; ++l_uI) {
        const Quad& l_oQ = l_vQuads[l_uI];
        const float l_fX0 = l_oQ.m_arrRect[0], l_fY0 = l_oQ.m_arrRect[1], l_fX1 = l_oQ.m_arrRect[0] + l_oQ.m_arrRect[2], l_fY1 = l_oQ.m_arrRect[1] + l_oQ.m_arrRect[3];
        const float l_arrRectCorners[8] = {l_fX0, l_fY0, l_fX1, l_fY0, l_fX1, l_fY1, l_fX0, l_fY1};
        const float* l_pC = l_oQ.m_bHasCorners ? l_oQ.m_arrCorners : l_arrRectCorners;
        const float l_fU0 = l_oQ.m_arrUv[0], l_fV0 = l_oQ.m_arrUv[1], l_fU1 = l_oQ.m_arrUv[2], l_fV1 = l_oQ.m_arrUv[3];
        const float l_fS = l_oQ.m_bSolid ? 1.0f : 0.0f;
        const float* l_pT = l_oQ.m_arrTint;
        l_pV[l_uI * 4 + 0] = {l_pC[0], l_pC[1], l_fU0, l_fV0, l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
        l_pV[l_uI * 4 + 1] = {l_pC[2], l_pC[3], l_fU1, l_fV0, l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
        l_pV[l_uI * 4 + 2] = {l_pC[4], l_pC[5], l_fU1, l_fV1, l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
        l_pV[l_uI * 4 + 3] = {l_pC[6], l_pC[7], l_fU0, l_fV1, l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
    }
    bgfx::update(bgfx::DynamicVertexBufferHandle{m_u16CpuVb}, 0, bgfx::copy(l_pV, l_uTotal * 4 * (uint32_t)sizeof(SpriteVertex)));
    size_t l_nFirst = 0, l_nNextGpu = 0;
    while (l_nFirst < l_uTotal) {
        const size_t l_nLimit = f_DrawGpuAt(l_nFirst, l_nNextGpu, l_u16Texture);   // GPU particles placed before quad `first`
        // One draw call per run of quads with the same texture and blending (normally the whole
        // frame), in chunks of kChunk quads (16-bit indices into the shared index buffer).
        const bool l_bAdditive = l_vQuads[l_nFirst].m_bAdditive;
        const uint16_t l_u16Tex = l_vQuads[l_nFirst].m_u16Texture;
        size_t l_nRunEnd = l_nFirst + 1;
        while (l_nRunEnd < l_uTotal && l_nRunEnd < l_nLimit && l_vQuads[l_nRunEnd].m_bAdditive == l_bAdditive && l_vQuads[l_nRunEnd].m_u16Texture == l_u16Tex) ++l_nRunEnd;
        for (size_t l_nAt = l_nFirst; l_nAt < l_nRunEnd; l_nAt += g_uChunk) {
            const uint32_t l_uCount = (uint32_t)std::min<size_t>(g_uChunk, l_nRunEnd - l_nAt);
            bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{m_u16CpuVb}, (uint32_t)l_nAt * 4, l_uCount * 4);
            bgfx::setIndexBuffer(bgfx::IndexBufferHandle{m_u16ChunkIb}, 0, l_uCount * 6);
            const uint16_t l_u16Use = l_u16Tex == g_u16SpriteAtlasTexture ? l_u16Texture : l_u16Tex;   // the sprite atlas, or a loadTexture one
            f_BindTexture(l_u16Use);
            bgfx::setState(f_BlendState(l_bAdditive));
            bgfx::submit(m_u16ViewGame, bgfx::ProgramHandle{m_u16Program});
            ++m_uLastDrawCalls;
            m_nLastDrawnQuads += l_uCount;
        }
        l_nFirst = l_nRunEnd;
    }
    f_DrawGpuAt(SIZE_MAX, l_nNextGpu, l_u16Texture);   // GPU particles after the last quad
}

void BgfxRenderer::f_SubmitTop() {
    const uint32_t l_uTotal = (uint32_t)m_vTopSprites.size();
    if (l_uTotal == 0) return;
    for (size_t l_nFirst = 0; l_nFirst < l_uTotal;) {
        // one draw per run of quads with the same texture and blending, at most kChunk quads (the shared index buffer)
        const bool l_bAdditive = m_vTopSprites[l_nFirst].m_bAdditive;
        const uint16_t l_u16Tex = m_vTopSprites[l_nFirst].m_u16Texture;
        size_t l_nRunEnd = l_nFirst + 1;
        while (l_nRunEnd < l_uTotal && l_nRunEnd - l_nFirst < g_uChunk && m_vTopSprites[l_nRunEnd].m_bAdditive == l_bAdditive &&
               m_vTopSprites[l_nRunEnd].m_u16Texture == l_u16Tex)
            ++l_nRunEnd;
        const uint32_t l_uCount = (uint32_t)(l_nRunEnd - l_nFirst);
        if (bgfx::getAvailTransientVertexBuffer(l_uCount * 4, f_SpriteLayout()) < l_uCount * 4) return;
        bgfx::TransientVertexBuffer l_oTvb;
        bgfx::allocTransientVertexBuffer(&l_oTvb, l_uCount * 4, f_SpriteLayout());
        auto* l_pV = reinterpret_cast<SpriteVertex*>(l_oTvb.data);
        for (uint32_t l_uI = 0; l_uI < l_uCount; ++l_uI) {
            const Quad& l_oQ = m_vTopSprites[l_nFirst + l_uI];
            const float l_fX0 = l_oQ.m_arrRect[0], l_fY0 = l_oQ.m_arrRect[1], l_fX1 = l_oQ.m_arrRect[0] + l_oQ.m_arrRect[2], l_fY1 = l_oQ.m_arrRect[1] + l_oQ.m_arrRect[3];
            const float l_arrRectCorners[8] = {l_fX0, l_fY0, l_fX1, l_fY0, l_fX1, l_fY1, l_fX0, l_fY1};
            const float* l_pC = l_oQ.m_bHasCorners ? l_oQ.m_arrCorners : l_arrRectCorners;
            const float l_fS = l_oQ.m_bSolid ? 1.0f : 0.0f;
            const float* l_pT = l_oQ.m_arrTint;
            l_pV[l_uI * 4 + 0] = {l_pC[0], l_pC[1], l_oQ.m_arrUv[0], l_oQ.m_arrUv[1], l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
            l_pV[l_uI * 4 + 1] = {l_pC[2], l_pC[3], l_oQ.m_arrUv[2], l_oQ.m_arrUv[1], l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
            l_pV[l_uI * 4 + 2] = {l_pC[4], l_pC[5], l_oQ.m_arrUv[2], l_oQ.m_arrUv[3], l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
            l_pV[l_uI * 4 + 3] = {l_pC[6], l_pC[7], l_oQ.m_arrUv[0], l_oQ.m_arrUv[3], l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
        }
        bgfx::setVertexBuffer(0, &l_oTvb);
        bgfx::setIndexBuffer(bgfx::IndexBufferHandle{m_u16ChunkIb}, 0, l_uCount * 6);
        const uint16_t l_u16Use = l_u16Tex == g_u16SpriteAtlasTexture ? m_u16SpriteTex : l_u16Tex;
        f_BindTexture(l_u16Use);
        bgfx::setState(f_BlendState(l_bAdditive));
        bgfx::submit(m_u16ViewTop, bgfx::ProgramHandle{m_u16Program});
        ++m_uLastDrawCalls;
        m_nLastDrawnQuads += l_uCount;
        l_nFirst = l_nRunEnd;
    }
}

// ---- GPU-simulated particles --------------------------------------------------------------------

bool BgfxRenderer::f_SupportsGpuParticles() const {
    return m_u16FxSpawn != m_u16Invalid && m_u16FxUpdate != m_u16Invalid && m_u16CsDrawProgram != m_u16Invalid && m_u16ChunkIb != m_u16Invalid;
}

uint32_t BgfxRenderer::f_CreateGpuParticles(uint32_t l_uCapacity) {
    if (!f_SupportsGpuParticles() || l_uCapacity == 0) return 0;
    GpuEmitterBuffers l_oB;
    l_oB.m_uCapacity = l_uCapacity;
    // Every slot starts empty (life 0): zeroed memory.
    const bgfx::Memory* l_pZeros = bgfx::alloc(l_uCapacity * 4 * 16);
    std::memset(l_pZeros->data, 0, l_pZeros->size);
    l_oB.m_u16State = bgfx::createDynamicVertexBuffer(l_pZeros, f_CsInLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE).idx;
    l_oB.m_u16Out = bgfx::createDynamicVertexBuffer(l_uCapacity * 4, f_CsOutLayout(), BGFX_BUFFER_COMPUTE_WRITE).idx;
    if (l_oB.m_u16State == m_u16Invalid || l_oB.m_u16Out == m_u16Invalid) {
        if (l_oB.m_u16State != m_u16Invalid) bgfx::destroy(bgfx::DynamicVertexBufferHandle{l_oB.m_u16State});
        if (l_oB.m_u16Out != m_u16Invalid) bgfx::destroy(bgfx::DynamicVertexBufferHandle{l_oB.m_u16Out});
        return 0;
    }
    for (size_t l_nI = 0; l_nI < m_vGpuEmitters.size(); l_nI++)   // reuse a released id
        if (m_vGpuEmitters[l_nI].m_u16State == m_u16Invalid) {
            m_vGpuEmitters[l_nI] = l_oB;
            return uint32_t(l_nI + 1);
        }
    m_vGpuEmitters.push_back(l_oB);
    return uint32_t(m_vGpuEmitters.size());
}

void BgfxRenderer::f_ReleaseGpuParticles(uint32_t l_uId) {
    if (l_uId == 0 || l_uId > m_vGpuEmitters.size()) return;
    GpuEmitterBuffers& l_oB = m_vGpuEmitters[l_uId - 1];
    for (uint16_t* l_pH : {&l_oB.m_u16State, &l_oB.m_u16Out, &l_oB.m_u16Spawn})
        if (*l_pH != m_u16Invalid) { bgfx::destroy(bgfx::DynamicVertexBufferHandle{*l_pH}); *l_pH = m_u16Invalid; }
    l_oB.m_uCapacity = l_oB.m_uSpawnCap = 0;
}

void BgfxRenderer::f_DrawGpuParticles(const GpuParticleFrame& l_oF) {
    if (!f_SupportsGpuParticles() || l_oF.m_uEmitter == 0 || l_oF.m_uEmitter > m_vGpuEmitters.size()) return;
    GpuEmitterBuffers& l_oB = m_vGpuEmitters[l_oF.m_uEmitter - 1];
    if (l_oB.m_u16State == m_u16Invalid) return;
    // The simulation runs in the clear view, in order (newborns before the update), so before kViewGame draws.
    bgfx::setViewMode(m_u16ViewClear, bgfx::ViewMode::Sequential);
    float l_arrSim[4] = {(float)l_oF.m_uSteps, l_oF.m_fDt, (float)l_oB.m_uCapacity, (float)l_oF.m_uSpawnCount};
    if (l_oF.m_uSpawnCount > 0) {
        if (!f_EnsureBuffer(l_oB.m_u16Spawn, l_oB.m_uSpawnCap, l_oF.m_uSpawnCount * 5, f_CsInLayout(), BGFX_BUFFER_COMPUTE_READ)) return;
        bgfx::update(bgfx::DynamicVertexBufferHandle{l_oB.m_u16Spawn}, 0, bgfx::copy(l_oF.m_pSpawn, l_oF.m_uSpawnCount * 20 * (uint32_t)sizeof(float)));
        bgfx::setUniform(bgfx::UniformHandle{m_u16UFxSim}, l_arrSim);
        bgfx::setBuffer(0, bgfx::DynamicVertexBufferHandle{l_oB.m_u16State}, bgfx::Access::ReadWrite);
        bgfx::setBuffer(1, bgfx::DynamicVertexBufferHandle{l_oB.m_u16Spawn}, bgfx::Access::Read);
        bgfx::dispatch(m_u16ViewClear, bgfx::ProgramHandle{m_u16FxSpawn}, (l_oF.m_uSpawnCount + 63) / 64, 1, 1);
    }
    const float l_arrForce[4] = {l_oF.m_arrGravity[0], l_oF.m_arrGravity[1], l_oF.m_fDragPerStep, l_oF.m_bAlignToVelocity ? 1.0f : 0.0f};
    const float l_arrOrigin[4] = {l_oF.m_arrOrigin[0], l_oF.m_arrOrigin[1], (float)l_oF.m_uFrameCount, l_oF.m_fFps};
    const float l_arrXform[8] = {l_oF.m_arrXform[0], l_oF.m_arrXform[1], l_oF.m_arrXform[2], 0, l_oF.m_arrXform[3], l_oF.m_arrXform[4], l_oF.m_arrXform[5], 0};
    bgfx::setUniform(bgfx::UniformHandle{m_u16UFxSim}, l_arrSim);
    bgfx::setUniform(bgfx::UniformHandle{m_u16UFxForce}, l_arrForce);
    bgfx::setUniform(bgfx::UniformHandle{m_u16UFxOrigin}, l_arrOrigin);
    bgfx::setUniform(bgfx::UniformHandle{m_u16UFxXform}, l_arrXform, 2);
    bgfx::setUniform(bgfx::UniformHandle{m_u16UFxTint}, l_oF.m_arrTint);
    bgfx::setUniform(bgfx::UniformHandle{m_u16UFxColorLut}, l_oF.m_pColorLut, GpuParticleFrame::m_uLut);
    bgfx::setUniform(bgfx::UniformHandle{m_u16UFxScalarLut}, l_oF.m_pScalarLut, GpuParticleFrame::m_uLut);
    bgfx::setUniform(bgfx::UniformHandle{m_u16UFxFrameUv}, l_oF.m_pFrameUv, std::max(1u, l_oF.m_uFrameCount));
    bgfx::setUniform(bgfx::UniformHandle{m_u16UFxFrameRect}, l_oF.m_pFrameRect, std::max(1u, l_oF.m_uFrameCount));
    bgfx::setBuffer(0, bgfx::DynamicVertexBufferHandle{l_oB.m_u16State}, bgfx::Access::ReadWrite);
    bgfx::setBuffer(1, bgfx::DynamicVertexBufferHandle{l_oB.m_u16Out}, bgfx::Access::Write);
    bgfx::dispatch(m_u16ViewClear, bgfx::ProgramHandle{m_u16FxUpdate}, (l_oB.m_uCapacity + 63) / 64, 1, 1);
    f_CurrentDraws().push_back({f_CurrentQuads().size(), l_oF.m_uEmitter, l_oF.m_u16Texture, l_oF.m_bAdditive});
}

size_t BgfxRenderer::f_DrawMarkers(uint16_t l_u16View, const std::vector<GpuDraw>& l_vDraws, bool l_bInLayer, size_t l_nAt, size_t& l_nNext,
                                 uint16_t l_u16Texture) {
    for (; l_nNext < l_vDraws.size() && l_vDraws[l_nNext].m_nAt <= l_nAt; l_nNext++) {
        const GpuDraw& l_oD = l_vDraws[l_nNext];
        if (l_oD.m_iLayer >= 0) { f_SubmitComposite(l_oD.m_iLayer, l_u16View); continue; }
        const uint16_t l_u16Use = l_oD.m_u16Texture == g_u16SpriteAtlasTexture ? l_u16Texture : l_oD.m_u16Texture;
        const uint64_t l_u64State = l_bInLayer ? f_LayerBlendState(l_oD.m_bAdditive) : f_BlendState(l_oD.m_bAdditive);
        if (l_oD.m_uEmitter == 0) {   // a mesh (drawMesh): the CPU path's program, its own part of the frame's buffers
            if (m_u16MeshVb == m_u16Invalid || m_u16MeshIb == m_u16Invalid) continue;
            bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{m_u16MeshVb}, l_oD.m_uFirstVertex, l_oD.m_uVertexCount);
            bgfx::setIndexBuffer(bgfx::DynamicIndexBufferHandle{m_u16MeshIb}, l_oD.m_uFirstIndex, l_oD.m_uIndexCount);
            f_BindTexture(l_u16Use);
            bgfx::setState(l_u64State);
            bgfx::submit(l_u16View, bgfx::ProgramHandle{m_u16Program});
            ++m_uLastDrawCalls;
            ++m_nLastMeshCount;
            continue;
        }
        const GpuEmitterBuffers& l_oB = m_vGpuEmitters[l_oD.m_uEmitter - 1];
        if (l_oB.m_u16Out == m_u16Invalid) continue;
        // Every slot's quad (empty slots are degenerate: nothing to rasterize), in chunks.
        for (uint32_t l_uFirst = 0; l_uFirst < l_oB.m_uCapacity; l_uFirst += g_uChunk) {
            const uint32_t l_uCount = std::min(g_uChunk, l_oB.m_uCapacity - l_uFirst);
            bgfx::setVertexBuffer(0, bgfx::DynamicVertexBufferHandle{l_oB.m_u16Out}, l_uFirst * 4, l_uCount * 4);
            bgfx::setIndexBuffer(bgfx::IndexBufferHandle{m_u16ChunkIb}, 0, l_uCount * 6);
            f_BindTexture(l_u16Use);
            bgfx::setState(l_u64State);
            bgfx::submit(l_u16View, bgfx::ProgramHandle{m_u16CsDrawProgram});
            ++m_uLastDrawCalls;
        }
        ++m_uLastGpuEmitters;
    }
    return l_nNext < l_vDraws.size() ? l_vDraws[l_nNext].m_nAt : SIZE_MAX;
}

// ---- layers -------------------------------------------------------------------------------------

uint64_t BgfxRenderer::f_LayerBlendState(bool l_bAdditive) const {
    // Into a layer's premultiplied picture: the colour blends as on screen, the alpha accumulates coverage (an additive
    // sprite adds light, no coverage).
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
           (l_bAdditive ? BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ZERO,
                                                      BGFX_STATE_BLEND_ONE)
                     : BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                                      BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
}

bool BgfxRenderer::f_SupportsLayers() const {
    if (m_u16LayerProgram == m_u16Invalid || m_u16Program == m_u16Invalid || m_u16ChunkIb == m_u16Invalid) return false;
    const auto* l_pCaps = bgfx::getCaps();
    return (l_pCaps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0 ||
           (l_pCaps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
}

void BgfxRenderer::f_BeginLayer(const LayerEffect& l_oFx) {
    // Drawn straight through: the top layer, no support, too many this frame, or inside a layer drawn straight through.
    if (m_iLayer == 1 || !f_SupportsLayers() || m_vLayers.size() >= m_uMaxLayers || (!m_vLayerStack.empty() && m_vLayerStack.back() < 0)) {
        m_vLayerStack.push_back(-1);
        return;
    }
    LayerDraw l_oL;
    l_oL.m_oFx = l_oFx;
    m_vLayers.push_back(std::move(l_oL));
    m_vLayerStack.push_back(int(m_vLayers.size() - 1));
}

void BgfxRenderer::f_EndLayer() {
    if (m_vLayerStack.empty()) return;
    const int l_iIdx = m_vLayerStack.back();
    m_vLayerStack.pop_back();
    if (l_iIdx < 0) return;
    m_vLayerClosed.push_back(l_iIdx);
    // Its picture goes where the layer began, among what is around it.
    GpuDraw l_oD{f_CurrentQuads().size(), 0, g_u16SpriteAtlasTexture, false};
    l_oD.m_iLayer = l_iIdx;
    f_CurrentDraws().push_back(l_oD);
}

void BgfxRenderer::f_PrepareLayers() {
    m_nLastLayerCount = 0;
    if (m_vLayerClosed.empty()) return;
    // One picture per layer this frame, the game view's size in device pixels (a texel per screen pixel).
    const ViewportRect l_oFit = f_ComputeAspectFitViewport(m_uDevW, m_uDevH, m_uDesignW, m_uDesignH);
    const uint16_t l_u16W = (uint16_t)std::max(1L, std::lround(l_oFit.m_fWidth)), l_u16H = (uint16_t)std::max(1L, std::lround(l_oFit.m_fHeight));
    if (l_u16W != m_u16LayerW || l_u16H != m_u16LayerH) {
        for (uint16_t l_u16Fb : m_vLayerFb) bgfx::destroy(bgfx::FrameBufferHandle{l_u16Fb});
        m_vLayerFb.clear();
        m_u16LayerW = l_u16W;
        m_u16LayerH = l_u16H;
    }
    const bool l_bHdr = (bgfx::getCaps()->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
    while (m_vLayerFb.size() < m_vLayerClosed.size()) {
        const bgfx::TextureHandle l_oT = bgfx::createTexture2D(l_u16W, l_u16H, false, 1, l_bHdr ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8,
                                                            BGFX_TEXTURE_RT | BGFX_SAMPLER_UVW_CLAMP);
        const bgfx::FrameBufferHandle l_oFb = bgfx::createFrameBuffer(1, &l_oT, true);
        if (!bgfx::isValid(l_oFb)) break;
        m_vLayerFb.push_back(l_oFb.idx);
    }
    const float l_fW = (float)m_uDesignW, l_fH = (float)m_uDesignH, l_fSx = float(l_u16W) / l_fW, l_fSy = float(l_u16H) / l_fH;
    // Inner layers close first: in closing order, every picture is drawn before the layer that uses it (and its bounds
    // are known when the layer around it is measured).
    for (size_t l_nK = 0; l_nK < m_vLayerClosed.size() && l_nK < m_vLayerFb.size(); l_nK++) {
        LayerDraw& l_oL = m_vLayers[(size_t)m_vLayerClosed[l_nK]];
        // What it draws: its quads, its meshes' vertices, its inner layers' pictures; particles: anywhere
        l_oL.m_bAny = false;
        auto l_fnGrow = [&](float l_fX0, float l_fY0, float l_fX1, float l_fY1) {
            if (!l_oL.m_bAny) { l_oL.m_fX0 = l_fX0; l_oL.m_fY0 = l_fY0; l_oL.m_fX1 = l_fX1; l_oL.m_fY1 = l_fY1; l_oL.m_bAny = true; return; }
            l_oL.m_fX0 = std::min(l_oL.m_fX0, l_fX0); l_oL.m_fY0 = std::min(l_oL.m_fY0, l_fY0); l_oL.m_fX1 = std::max(l_oL.m_fX1, l_fX1); l_oL.m_fY1 = std::max(l_oL.m_fY1, l_fY1);
        };
        for (const Quad& l_oQ : l_oL.m_vQuads) l_fnGrow(l_oQ.m_arrRect[0], l_oQ.m_arrRect[1], l_oQ.m_arrRect[0] + l_oQ.m_arrRect[2], l_oQ.m_arrRect[1] + l_oQ.m_arrRect[3]);
        for (const GpuDraw& l_oD : l_oL.m_vDraws) {
            if (l_oD.m_iLayer >= 0) {
                const LayerDraw& l_oIn = m_vLayers[(size_t)l_oD.m_iLayer];
                if (l_oIn.m_bAny) l_fnGrow(l_oIn.m_fX0 - l_oIn.m_fReach, l_oIn.m_fY0 - l_oIn.m_fReach, l_oIn.m_fX1 + l_oIn.m_fReach, l_oIn.m_fY1 + l_oIn.m_fReach);
            } else if (l_oD.m_uEmitter == 0) {
                for (uint32_t l_uV = l_oD.m_uFirstVertex; l_uV < l_oD.m_uFirstVertex + l_oD.m_uVertexCount; l_uV++)
                    l_fnGrow(m_vMeshVerts[size_t(l_uV) * 9], m_vMeshVerts[size_t(l_uV) * 9 + 1], m_vMeshVerts[size_t(l_uV) * 9], m_vMeshVerts[size_t(l_uV) * 9 + 1]);
            } else {
                l_fnGrow(0, 0, l_fW, l_fH);
            }
        }
        const float* l_pP = l_oL.m_oFx.m_arrParams;
        l_oL.m_fReach = 2.0f + (l_oL.m_oFx.m_eKind == LayerEffect::MotionBlur ? 0.5f * std::max(std::fabs(l_pP[0]), std::fabs(l_pP[1]))
                         : l_oL.m_oFx.m_eKind == LayerEffect::Glow    ? std::fabs(l_pP[1])
                         : l_oL.m_oFx.m_eKind == LayerEffect::Outline ? std::fabs(l_pP[0]) : 0.0f);
        if (!l_oL.m_bAny) continue;   // nothing drawn: no picture, no composite
        // the picture's part used: the content and twice the reach (a blur tap at the composite's edge reads cleared
        // pixels), whole device pixels, in the picture
        const float l_fCx0 = std::max(0.0f, l_oL.m_fX0 - 2 * l_oL.m_fReach), l_fCy0 = std::max(0.0f, l_oL.m_fY0 - 2 * l_oL.m_fReach);
        const float l_fCx1 = std::min(l_fW, l_oL.m_fX1 + 2 * l_oL.m_fReach), l_fCy1 = std::min(l_fH, l_oL.m_fY1 + 2 * l_oL.m_fReach);
        if (l_fCx1 <= l_fCx0 || l_fCy1 <= l_fCy0) { l_oL.m_bAny = false; continue; }
        const int l_iPx0 = std::max(0, (int)std::floor(l_fCx0 * l_fSx)), l_iPy0 = std::max(0, (int)std::floor(l_fCy0 * l_fSy));
        const int l_iPx1 = std::min((int)l_u16W, (int)std::ceil(l_fCx1 * l_fSx)), l_iPy1 = std::min((int)l_u16H, (int)std::ceil(l_fCy1 * l_fSy));
        l_oL.m_iSlot = (int)l_nK;
        const uint16_t l_u16View = uint16_t(m_u16ViewLayerFirst + l_nK);
        bgfx::setViewFrameBuffer(l_u16View, bgfx::FrameBufferHandle{m_vLayerFb[l_nK]});
        bgfx::setViewRect(l_u16View, uint16_t(l_iPx0), uint16_t(l_iPy0), uint16_t(l_iPx1 - l_iPx0), uint16_t(l_iPy1 - l_iPy0));
        bgfx::setViewClear(l_u16View, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
        bgfx::setViewMode(l_u16View, bgfx::ViewMode::Sequential);
        float l_arrProj[16];   // that part of the design space onto that part of the picture
        bx::mtxOrtho(l_arrProj, float(l_iPx0) / l_fSx, float(l_iPx1) / l_fSx, float(l_iPy1) / l_fSy, float(l_iPy0) / l_fSy, 0.0f, 100.0f, 0.0f,
                     bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(l_u16View, nullptr, l_arrProj);
        bgfx::touch(l_u16View);
        m_nLastLayerCount++;
    }
    for (size_t l_nK = 0; l_nK < m_vLayerClosed.size() && l_nK < m_vLayerFb.size(); l_nK++)
        if (m_vLayers[(size_t)m_vLayerClosed[l_nK]].m_iSlot >= 0) f_SubmitLayer(m_vLayers[(size_t)m_vLayerClosed[l_nK]], uint16_t(m_u16ViewLayerFirst + l_nK));
}

void BgfxRenderer::f_SubmitLayer(const LayerDraw& l_oL, uint16_t l_u16View) {
    // Its quads in transient buffers, one draw per run of texture and blending; its markers (meshes, particles, inner
    // layers' pictures) in their places.
    const size_t l_nTotal = l_oL.m_vQuads.size();
    size_t l_nFirst = 0, l_nNext = 0;
    while (l_nFirst < l_nTotal) {
        const size_t l_nLimit = f_DrawMarkers(l_u16View, l_oL.m_vDraws, true, l_nFirst, l_nNext, m_u16SpriteTex);
        const bool l_bAdditive = l_oL.m_vQuads[l_nFirst].m_bAdditive;
        const uint16_t l_u16Tex = l_oL.m_vQuads[l_nFirst].m_u16Texture;
        size_t l_nRunEnd = l_nFirst + 1;
        while (l_nRunEnd < l_nTotal && l_nRunEnd < l_nLimit && l_nRunEnd - l_nFirst < g_uChunk && l_oL.m_vQuads[l_nRunEnd].m_bAdditive == l_bAdditive &&
               l_oL.m_vQuads[l_nRunEnd].m_u16Texture == l_u16Tex)
            ++l_nRunEnd;
        const uint32_t l_uCount = (uint32_t)(l_nRunEnd - l_nFirst);
        if (bgfx::getAvailTransientVertexBuffer(l_uCount * 4, f_SpriteLayout()) < l_uCount * 4) break;
        bgfx::TransientVertexBuffer l_oTvb;
        bgfx::allocTransientVertexBuffer(&l_oTvb, l_uCount * 4, f_SpriteLayout());
        auto* l_pV = reinterpret_cast<SpriteVertex*>(l_oTvb.data);
        for (uint32_t l_uI = 0; l_uI < l_uCount; ++l_uI) {
            const Quad& l_oQ = l_oL.m_vQuads[l_nFirst + l_uI];
            const float l_fX0 = l_oQ.m_arrRect[0], l_fY0 = l_oQ.m_arrRect[1], l_fX1 = l_oQ.m_arrRect[0] + l_oQ.m_arrRect[2], l_fY1 = l_oQ.m_arrRect[1] + l_oQ.m_arrRect[3];
            const float l_arrRectCorners[8] = {l_fX0, l_fY0, l_fX1, l_fY0, l_fX1, l_fY1, l_fX0, l_fY1};
            const float* l_pC = l_oQ.m_bHasCorners ? l_oQ.m_arrCorners : l_arrRectCorners;
            const float l_fS = l_oQ.m_bSolid ? 1.0f : 0.0f;
            const float* l_pT = l_oQ.m_arrTint;
            l_pV[l_uI * 4 + 0] = {l_pC[0], l_pC[1], l_oQ.m_arrUv[0], l_oQ.m_arrUv[1], l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
            l_pV[l_uI * 4 + 1] = {l_pC[2], l_pC[3], l_oQ.m_arrUv[2], l_oQ.m_arrUv[1], l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
            l_pV[l_uI * 4 + 2] = {l_pC[4], l_pC[5], l_oQ.m_arrUv[2], l_oQ.m_arrUv[3], l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
            l_pV[l_uI * 4 + 3] = {l_pC[6], l_pC[7], l_oQ.m_arrUv[0], l_oQ.m_arrUv[3], l_pT[0], l_pT[1], l_pT[2], l_pT[3], l_fS};
        }
        bgfx::setVertexBuffer(0, &l_oTvb);
        bgfx::setIndexBuffer(bgfx::IndexBufferHandle{m_u16ChunkIb}, 0, l_uCount * 6);
        f_BindTexture(l_u16Tex == g_u16SpriteAtlasTexture ? m_u16SpriteTex : l_u16Tex);
        bgfx::setState(f_LayerBlendState(l_bAdditive));
        bgfx::submit(l_u16View, bgfx::ProgramHandle{m_u16Program});
        ++m_uLastDrawCalls;
        m_nLastDrawnQuads += l_uCount;
        l_nFirst = l_nRunEnd;
    }
    f_DrawMarkers(l_u16View, l_oL.m_vDraws, true, SIZE_MAX, l_nNext, m_u16SpriteTex);   // what was drawn after its last quad
}

void BgfxRenderer::f_SubmitComposite(int l_iLayer, uint16_t l_u16View) {
    if (l_iLayer < 0 || (size_t)l_iLayer >= m_vLayers.size()) return;
    const LayerDraw& l_oL = m_vLayers[(size_t)l_iLayer];
    if (l_oL.m_iSlot < 0 || (size_t)l_oL.m_iSlot >= m_vLayerFb.size()) return;
    if (bgfx::getAvailTransientVertexBuffer(4, f_SpriteLayout()) < 4) return;
    if (!l_oL.m_bAny) return;
    // The picture's content and its effect's reach (the rest of the picture is not cleared this frame); a
    // bottom-left-origin backend (OpenGL) stored it upside down.
    const float l_fW = (float)m_uDesignW, l_fH = (float)m_uDesignH;
    const float l_fX0 = std::max(0.0f, l_oL.m_fX0 - l_oL.m_fReach), l_fY0 = std::max(0.0f, l_oL.m_fY0 - l_oL.m_fReach);
    const float l_fX1 = std::min(l_fW, l_oL.m_fX1 + l_oL.m_fReach), l_fY1 = std::min(l_fH, l_oL.m_fY1 + l_oL.m_fReach);
    if (l_fX1 <= l_fX0 || l_fY1 <= l_fY0) return;
    const bool l_bFlip = bgfx::getCaps()->originBottomLeft;
    auto l_fnVOf = [&](float l_fY) { return l_bFlip ? 1.0f - l_fY / l_fH : l_fY / l_fH; };
    bgfx::TransientVertexBuffer l_oTvb;
    bgfx::allocTransientVertexBuffer(&l_oTvb, 4, f_SpriteLayout());
    auto* l_pV = reinterpret_cast<SpriteVertex*>(l_oTvb.data);
    l_pV[0] = {l_fX0, l_fY0, l_fX0 / l_fW, l_fnVOf(l_fY0), 1, 1, 1, 1, 0};
    l_pV[1] = {l_fX1, l_fY0, l_fX1 / l_fW, l_fnVOf(l_fY0), 1, 1, 1, 1, 0};
    l_pV[2] = {l_fX1, l_fY1, l_fX1 / l_fW, l_fnVOf(l_fY1), 1, 1, 1, 1, 0};
    l_pV[3] = {l_fX0, l_fY1, l_fX0 / l_fW, l_fnVOf(l_fY1), 1, 1, 1, 1, 0};
    bgfx::setVertexBuffer(0, &l_oTvb);
    bgfx::setIndexBuffer(bgfx::IndexBufferHandle{m_u16ChunkIb}, 0, 6);
    // Lengths are design pixels; the shader takes them in texture units (y turned for a flipped picture).
    const float l_arrMode[4] = {(float)l_oL.m_oFx.m_eKind, std::clamp(l_oL.m_oFx.m_fOpacity, 0.0f, 1.0f), 0, 0};
    const float l_arrTexel[4] = {1.0f / l_fW, (l_bFlip ? -1.0f : 1.0f) / l_fH, l_fW, l_fH};
    bgfx::setUniform(bgfx::UniformHandle{m_u16ULayerMode}, l_arrMode);
    bgfx::setUniform(bgfx::UniformHandle{m_u16ULayerParams}, l_oL.m_oFx.m_arrParams);
    bgfx::setUniform(bgfx::UniformHandle{m_u16ULayerColor}, l_oL.m_oFx.m_arrColor);
    bgfx::setUniform(bgfx::UniformHandle{m_u16ULayerTexel}, l_arrTexel);
    bgfx::setTexture(0, bgfx::UniformHandle{m_u16Sampler}, bgfx::getTexture(bgfx::FrameBufferHandle{m_vLayerFb[(size_t)l_oL.m_iSlot]}),
                     BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(l_u16View, bgfx::ProgramHandle{m_u16LayerProgram});
    ++m_uLastDrawCalls;
}

void BgfxRenderer::f_ApplyViewOrder() {
    // Plain id order, unless a 3D scene (setSceneViews) or layer pictures must run right after the clear: then the clear,
    // the scene's views, the layers' views (inner first), and every other view in id order.
    const bool l_bScene = m_u16SceneCount > 0 && m_u16SceneFirst > m_u16ViewOverlay;
    if (!l_bScene && m_nLastLayerCount == 0) {
        if (m_bOrderApplied) bgfx::setViewOrder(0, UINT16_MAX, nullptr);   // back to the plain id order
        m_bOrderApplied = false;
        return;
    }
    const uint16_t l_u16MaxViews = (uint16_t)bgfx::getCaps()->limits.maxViews;
    std::vector<bgfx::ViewId> l_vOrder;
    std::vector<unsigned char> l_vPlaced(l_u16MaxViews, 0);
    auto l_fnPut = [&](uint16_t l_u16V) {
        if (l_u16V < l_u16MaxViews && !l_vPlaced[l_u16V]) { l_vPlaced[l_u16V] = 1; l_vOrder.push_back(l_u16V); }
    };
    l_fnPut(m_u16ViewClear);
    if (l_bScene)
        for (uint16_t l_u16V = m_u16SceneFirst; l_u16V < uint16_t(m_u16SceneFirst + m_u16SceneCount); l_u16V++) l_fnPut(l_u16V);
    for (size_t l_nK = 0; l_nK < std::min(m_vLayerClosed.size(), m_vLayerFb.size()); l_nK++) l_fnPut(uint16_t(m_u16ViewLayerFirst + l_nK));   // an empty one: untouched, a no-op
    for (uint16_t l_u16V = 0; l_u16V < l_u16MaxViews; l_u16V++) l_fnPut(l_u16V);
    bgfx::setViewOrder(0, (uint16_t)l_vOrder.size(), l_vOrder.data());
    m_bOrderApplied = true;
}

void BgfxRenderer::f_SavePNG(const std::string& l_strPath) {
    // The host's bgfx callback (bgfx_host.cpp) writes the PNG a frame or two later.
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, l_strPath.c_str());
}

}  // namespace toms::next
