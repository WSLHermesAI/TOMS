// gltf_renderer.cpp -- see gltf_renderer.h.
#include "gltf_renderer.h"

#include "embedded_shaders.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace toms::next {
namespace {

// One vertex layout for every primitive: what a file lacks gets a neutral value (white colour, no
// skin, zero tangent -- the shader then derives a tangent frame).
struct Vertex {
    float m_arrPos[3];
    float m_arrNrm[3];
    float m_arrTan[4];
    float m_arrUv0[2];
    float m_arrUv1[2];
    float m_arrColor[4];
    float m_arrJoints[4];
    float m_arrWeights[4];
};

bgfx::VertexLayout f_MeshLayout() {
    bgfx::VertexLayout l_oL;
    l_oL.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
        .end();
    return l_oL;
}

bgfx::VertexLayout f_LineLayout() {
    bgfx::VertexLayout l_oL;
    l_oL.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
    return l_oL;
}

void f_FillVertices(const gltf::Primitive& l_oP, const std::vector<glm::vec3>& l_vPos, const std::vector<glm::vec3>& l_vNrm,
                  const std::vector<glm::vec4>& l_vTan, std::vector<Vertex>& l_vOut) {
    l_vOut.resize(l_vPos.size());
    for (size_t l_nI = 0; l_nI < l_vPos.size(); l_nI++) {
        Vertex& l_oV = l_vOut[l_nI];
        std::memset(&l_oV, 0, sizeof l_oV);
        std::memcpy(l_oV.m_arrPos, &l_vPos[l_nI], 12);
        const glm::vec3 l_v3N = l_nI < l_vNrm.size() ? l_vNrm[l_nI] : glm::vec3(0, 0, 1);
        std::memcpy(l_oV.m_arrNrm, &l_v3N, 12);
        if (l_nI < l_vTan.size()) std::memcpy(l_oV.m_arrTan, &l_vTan[l_nI], 16);
        if (l_nI < l_oP.m_vUv0.size()) std::memcpy(l_oV.m_arrUv0, &l_oP.m_vUv0[l_nI], 8);
        if (l_nI < l_oP.m_vUv1.size()) std::memcpy(l_oV.m_arrUv1, &l_oP.m_vUv1[l_nI], 8);
        const glm::vec4 l_v4C = l_nI < l_oP.m_vColor.size() ? l_oP.m_vColor[l_nI] : glm::vec4(1.0f);
        std::memcpy(l_oV.m_arrColor, &l_v4C, 16);
        if (l_oP.f_Skinned()) {
            std::memcpy(l_oV.m_arrJoints, &l_oP.m_vJoints[l_nI], 16);
            glm::vec4 l_v4W = l_oP.m_vWeights[l_nI];
            const float l_fSum = l_v4W.x + l_v4W.y + l_v4W.z + l_v4W.w;
            if (l_fSum > 0) l_v4W /= l_fSum;   // exporters do not always normalize
            std::memcpy(l_oV.m_arrWeights, &l_v4W, 16);
        }
    }
}

// RGBA8 with a box-filtered mip chain (bgfx does not make mips itself).
bgfx::TextureHandle f_MakeTexture(const gltf::Image& l_oImg, const gltf::Texture& l_oTex) {
    if (l_oImg.m_vRgba.empty() || l_oImg.m_iWidth <= 0 || l_oImg.m_iHeight <= 0) return BGFX_INVALID_HANDLE;
    std::vector<uint8_t> l_vAll(l_oImg.m_vRgba);
    int l_iW = l_oImg.m_iWidth, l_iH = l_oImg.m_iHeight, l_iLevels = 1;
    std::vector<uint8_t> l_vPrev = l_oImg.m_vRgba;
    while (l_iW > 1 || l_iH > 1) {
        const int l_iNw = std::max(1, l_iW / 2), l_iNh = std::max(1, l_iH / 2);
        std::vector<uint8_t> l_vNext((size_t)l_iNw * l_iNh * 4);
        for (int l_iY = 0; l_iY < l_iNh; l_iY++)
            for (int l_iX = 0; l_iX < l_iNw; l_iX++)
                for (int l_iC = 0; l_iC < 4; l_iC++) {
                    int l_iSum = 0, l_iN = 0;
                    for (int l_iDy = 0; l_iDy < 2; l_iDy++)
                        for (int l_iDx = 0; l_iDx < 2; l_iDx++) {
                            const int l_iSx = std::min(l_iW - 1, l_iX * 2 + l_iDx), l_iSy = std::min(l_iH - 1, l_iY * 2 + l_iDy);
                            l_iSum += l_vPrev[((size_t)l_iSy * l_iW + l_iSx) * 4 + l_iC];
                            l_iN++;
                        }
                    l_vNext[((size_t)l_iY * l_iNw + l_iX) * 4 + l_iC] = uint8_t(l_iSum / l_iN);
                }
        l_vAll.insert(l_vAll.end(), l_vNext.begin(), l_vNext.end());
        l_vPrev.swap(l_vNext);
        l_iW = l_iNw;
        l_iH = l_iNh;
        l_iLevels++;
    }
    uint64_t l_u64Flags = 0;
    if (l_oTex.m_bSrgb) l_u64Flags |= BGFX_TEXTURE_SRGB;
    if (!l_oTex.m_bRepeatS) l_u64Flags |= l_oTex.m_bMirrorS ? BGFX_SAMPLER_U_MIRROR : BGFX_SAMPLER_U_CLAMP;
    if (!l_oTex.m_bRepeatT) l_u64Flags |= l_oTex.m_bMirrorT ? BGFX_SAMPLER_V_MIRROR : BGFX_SAMPLER_V_CLAMP;
    if (l_oTex.m_bNearest) l_u64Flags |= BGFX_SAMPLER_MAG_POINT;
    else l_u64Flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
    return bgfx::createTexture2D((uint16_t)l_oImg.m_iWidth, (uint16_t)l_oImg.m_iHeight, l_iLevels > 1, 1, bgfx::TextureFormat::RGBA8, l_u64Flags,
                                 bgfx::copy(l_vAll.data(), (uint32_t)l_vAll.size()));
}

bgfx::TextureHandle f_SolidTexture(uint32_t l_uRgba) {
    const uint8_t l_arrPx[4] = {uint8_t(l_uRgba >> 24), uint8_t(l_uRgba >> 16), uint8_t(l_uRgba >> 8), uint8_t(l_uRgba)};
    return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(l_arrPx, 4));
}

}  // namespace

// ---- GPU resources ------------------------------------------------------------------------------------

struct GltfRenderer::GpuModel {
    struct Prim {
        bgfx::VertexBufferHandle m_oVb = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle m_oDvb = BGFX_INVALID_HANDLE;   // morph targets: rewritten on change
        bgfx::IndexBufferHandle m_oIb = BGFX_INVALID_HANDLE;
        uint32_t m_uIndexCount = 0;
        std::vector<Vertex> m_vVertices;            // morphed primitives only (the rest is uploaded once)
        std::vector<float> m_vLastWeights;
        bool m_bMorph = false;
    };
    const gltf::Model* m_pModel = nullptr;
    std::vector<std::vector<Prim>> m_vMeshes;
    std::vector<bgfx::TextureHandle> m_vTextures;

    ~GpuModel() {
        for (auto& l_vM : m_vMeshes)
            for (Prim& l_oP : l_vM) {
                if (bgfx::isValid(l_oP.m_oVb)) bgfx::destroy(l_oP.m_oVb);
                if (bgfx::isValid(l_oP.m_oDvb)) bgfx::destroy(l_oP.m_oDvb);
                if (bgfx::isValid(l_oP.m_oIb)) bgfx::destroy(l_oP.m_oIb);
            }
        for (bgfx::TextureHandle l_oT : m_vTextures)
            if (bgfx::isValid(l_oT)) bgfx::destroy(l_oT);
    }
};

struct GltfRenderer::AnimTextureGpu {
    std::shared_ptr<const gltf::AnimTexture> m_spData;
    bgfx::TextureHandle m_oTex = BGFX_INVALID_HANDLE;
    ~AnimTextureGpu() {
        if (bgfx::isValid(m_oTex)) bgfx::destroy(m_oTex);
    }
};

struct GltfRenderer::Impl {
    bgfx::ProgramHandle m_oMesh = BGFX_INVALID_HANDLE, m_oMeshInst = BGFX_INVALID_HANDLE, m_oLine = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_oMeshAnimTex = BGFX_INVALID_HANDLE, m_oShadowAnimTex = BGFX_INVALID_HANDLE;   // crowds
    // Image-based lighting: the prefiltered cubemap (or a black 1 x 1 one, so the sampler is always bound), its
    // diffuse terms, and the background (one screen triangle).
    bgfx::ProgramHandle m_oSky = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_oSkyVb = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_oEnvTex = BGFX_INVALID_HANDLE, m_oEnvDummy = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_oSEnv = BGFX_INVALID_HANDLE, m_oUEnvParams = BGFX_INVALID_HANDLE, m_oUEnvSH = BGFX_INVALID_HANDLE,
                        m_oUEnvSky = BGFX_INVALID_HANDLE, m_oUSkyMtx = BGFX_INVALID_HANDLE;
    glm::vec4 m_arrEnvSH[9];
    glm::vec4 m_v4EnvParams{0.0f};               // this frame: on, roughest mip, yaw, intensity
    bgfx::UniformHandle m_oSAnimTex = BGFX_INVALID_HANDLE;
    bool m_bCrowdCaps = false;
    std::vector<gltf::CrowdGpuInstance> m_vCrowdScratch;
    bgfx::ProgramHandle m_oShadow = BGFX_INVALID_HANDLE, m_oShadowInst = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_oLayout, m_oLineLay;
    bgfx::UniformHandle m_oSBase, m_oSMR, m_oSNormal, m_oSOcc, m_oSEmissive, m_oSShadow, m_oSSpecular, m_oSSpecularColor;
    bgfx::UniformHandle m_oUBaseColor, m_oUPbr, m_oUEmissiveCutoff, m_oUTexSet, m_oUTexSet2, m_oUUvTransform, m_oUUvRotation;
    bgfx::UniformHandle m_oUCamPos, m_oUSky, m_oUGround, m_oUJoints, m_oUMeshFlags, m_oUMatParams, m_oUSpecular;
    bgfx::UniformHandle m_oULightPos, m_oULightDirs, m_oULightColors, m_oULightSpot, m_oULightCount, m_oUShadowMtx, m_oUShadowRect;
    bgfx::TextureHandle m_oWhite = BGFX_INVALID_HANDLE, m_oFlatNormal = BGFX_INVALID_HANDLE;
    bool m_bReady = false;
    uint16_t m_u16Base = 0, m_u16View = 0;             // first view id; the opaque view (base + kMaxShadowTiles)
    Frame m_oFrame;
    std::vector<glm::mat4> m_vJoints;

    // Shadows.
    bool m_bShadowCaps = false;
    bgfx::TextureHandle m_oAtlas = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_oAtlasFb = BGFX_INVALID_HANDLE;
    int m_iAtlasSize = 0;
    bgfx::TextureHandle m_oDummyShadow = BGFX_INVALID_HANDLE;   // bound when shadows are off
    int m_iTiles = 0;                           // shadow tiles this frame (views base .. base + tiles - 1)
    glm::mat4 m_arrShadowMtx[m_iMaxShadowTiles];
    glm::vec4 m_arrShadowRect[m_iMaxShadowTiles];
    glm::vec4 m_arrLightPos[m_iMaxLights], m_arrLightDirs[m_iMaxLights], m_arrLightColors[m_iMaxLights], m_arrLightSpot[m_iMaxLights], m_v4LightCount;

    void f_ReleaseAtlas() {
        if (bgfx::isValid(m_oAtlasFb)) bgfx::destroy(m_oAtlasFb);   // destroys the texture too (it owns it)
        m_oAtlasFb = BGFX_INVALID_HANDLE;
        m_oAtlas = BGFX_INVALID_HANDLE;
        m_iAtlasSize = 0;
    }
    bool f_EnsureAtlas(int l_iSize) {
        if (m_iAtlasSize == l_iSize && bgfx::isValid(m_oAtlasFb)) return true;
        f_ReleaseAtlas();
        const bgfx::Caps* l_pCaps = bgfx::getCaps();
        const bool l_bD32 = (l_pCaps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
        m_oAtlas = bgfx::createTexture2D((uint16_t)l_iSize, (uint16_t)l_iSize, false, 1, l_bD32 ? bgfx::TextureFormat::D32F : bgfx::TextureFormat::D16,
                                      BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(m_oAtlas)) return false;
        m_oAtlasFb = bgfx::createFrameBuffer(1, &m_oAtlas, true);
        m_iAtlasSize = l_iSize;
        return bgfx::isValid(m_oAtlasFb);
    }
};

std::vector<GltfRenderer::Light> GltfRenderer::f_DefaultLights() {
    Light l_oKey;   // upper left-front, with shadows
    l_oKey.m_v3Direction = glm::normalize(glm::vec3(0.45f, -0.75f, -0.5f));
    l_oKey.m_v3Color = glm::vec3(1.0f, 0.97f, 0.92f);
    l_oKey.m_fIntensity = 3.0f;
    Light l_oFill = l_oKey;   // the other side, softer, no shadows
    l_oFill.m_v3Direction = glm::normalize(glm::vec3(-0.45f, -0.35f, 0.5f));
    l_oFill.m_fIntensity = 0.75f;
    l_oFill.m_bCastShadows = false;
    return {l_oKey, l_oFill};
}

GltfRenderer::GltfRenderer() : m_upImpl(new Impl) {}
GltfRenderer::~GltfRenderer() { f_Shutdown(); }
bool GltfRenderer::f_Ready() const { return m_upImpl->m_bReady; }
bool GltfRenderer::f_ShadowsSupported() const { return m_upImpl->m_bShadowCaps; }

bool GltfRenderer::f_Init() {
    Impl& l_oR = *m_upImpl;
    if (l_oR.m_bReady) return true;
    l_oR.m_oMesh = f_CreateEmbeddedProgram(ShaderProgram::Mesh);
    l_oR.m_oMeshInst = f_CreateEmbeddedProgram(ShaderProgram::MeshInstanced);
    l_oR.m_oLine = f_CreateEmbeddedProgram(ShaderProgram::Line);
    l_oR.m_oShadow = f_CreateEmbeddedProgram(ShaderProgram::MeshShadow);
    l_oR.m_oShadowInst = f_CreateEmbeddedProgram(ShaderProgram::MeshShadowInstanced);
    l_oR.m_oMeshAnimTex = f_CreateEmbeddedProgram(ShaderProgram::MeshAnimTex);
    l_oR.m_oShadowAnimTex = f_CreateEmbeddedProgram(ShaderProgram::MeshShadowAnimTex);
    l_oR.m_oSky = f_CreateEmbeddedProgram(ShaderProgram::Sky);
    if (!bgfx::isValid(l_oR.m_oMesh) || !bgfx::isValid(l_oR.m_oLine)) {
        std::fprintf(stderr, "[gltf] no mesh shaders for this backend\n");
        return false;
    }
    l_oR.m_oLayout = f_MeshLayout();
    l_oR.m_oLineLay = f_LineLayout();
    l_oR.m_oSBase = bgfx::createUniform("s_baseColor", bgfx::UniformType::Sampler);
    l_oR.m_oSMR = bgfx::createUniform("s_metalRough", bgfx::UniformType::Sampler);
    l_oR.m_oSNormal = bgfx::createUniform("s_normalMap", bgfx::UniformType::Sampler);
    l_oR.m_oSOcc = bgfx::createUniform("s_occlusion", bgfx::UniformType::Sampler);
    l_oR.m_oSEmissive = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);
    l_oR.m_oSSpecular = bgfx::createUniform("s_specular", bgfx::UniformType::Sampler);
    l_oR.m_oSSpecularColor = bgfx::createUniform("s_specularColor", bgfx::UniformType::Sampler);
    l_oR.m_oSShadow = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    l_oR.m_oSAnimTex = bgfx::createUniform("s_animTex", bgfx::UniformType::Sampler);
    l_oR.m_oUBaseColor = bgfx::createUniform("u_baseColor", bgfx::UniformType::Vec4);
    l_oR.m_oUPbr = bgfx::createUniform("u_pbr", bgfx::UniformType::Vec4);
    l_oR.m_oUEmissiveCutoff = bgfx::createUniform("u_emissiveCutoff", bgfx::UniformType::Vec4);
    l_oR.m_oUTexSet = bgfx::createUniform("u_texSet", bgfx::UniformType::Vec4);
    l_oR.m_oUTexSet2 = bgfx::createUniform("u_texSet2", bgfx::UniformType::Vec4);
    l_oR.m_oUUvTransform = bgfx::createUniform("u_uvTransform", bgfx::UniformType::Vec4);
    l_oR.m_oUUvRotation = bgfx::createUniform("u_uvRotation", bgfx::UniformType::Vec4);
    l_oR.m_oUCamPos = bgfx::createUniform("u_camPos", bgfx::UniformType::Vec4);
    l_oR.m_oUSky = bgfx::createUniform("u_skyColor", bgfx::UniformType::Vec4);
    l_oR.m_oUGround = bgfx::createUniform("u_groundColor", bgfx::UniformType::Vec4);
    l_oR.m_oUJoints = bgfx::createUniform("u_joints", bgfx::UniformType::Mat4, gltf::g_iMaxJoints);
    l_oR.m_oUMeshFlags = bgfx::createUniform("u_meshFlags", bgfx::UniformType::Vec4);
    l_oR.m_oUMatParams = bgfx::createUniform("u_matParams", bgfx::UniformType::Vec4);
    l_oR.m_oUSpecular = bgfx::createUniform("u_specular", bgfx::UniformType::Vec4);
    l_oR.m_oSEnv = bgfx::createUniform("s_envMap", bgfx::UniformType::Sampler);
    l_oR.m_oUEnvParams = bgfx::createUniform("u_envParams", bgfx::UniformType::Vec4);
    l_oR.m_oUEnvSH = bgfx::createUniform("u_envSH", bgfx::UniformType::Vec4, 9);
    l_oR.m_oUEnvSky = bgfx::createUniform("u_envSky", bgfx::UniformType::Vec4);
    l_oR.m_oUSkyMtx = bgfx::createUniform("u_skyMtx", bgfx::UniformType::Mat4);
    {
        static const uint32_t l_arrBlack[6] = {0xff000000u, 0xff000000u, 0xff000000u, 0xff000000u, 0xff000000u, 0xff000000u};
        l_oR.m_oEnvDummy = bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_UVW_CLAMP,
                                             bgfx::copy(l_arrBlack, sizeof(l_arrBlack)));
        bgfx::VertexLayout l_oSkyLay;
        l_oSkyLay.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
        static const float l_arrTri[9] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};   // covers the screen
        l_oR.m_oSkyVb = bgfx::createVertexBuffer(bgfx::copy(l_arrTri, sizeof(l_arrTri)), l_oSkyLay);
    }
    l_oR.m_oULightPos = bgfx::createUniform("u_lightPos", bgfx::UniformType::Vec4, m_iMaxLights);
    l_oR.m_oULightDirs = bgfx::createUniform("u_lightDirs", bgfx::UniformType::Vec4, m_iMaxLights);
    l_oR.m_oULightColors = bgfx::createUniform("u_lightColors", bgfx::UniformType::Vec4, m_iMaxLights);
    l_oR.m_oULightSpot = bgfx::createUniform("u_lightSpot", bgfx::UniformType::Vec4, m_iMaxLights);
    l_oR.m_oULightCount = bgfx::createUniform("u_lightCount", bgfx::UniformType::Vec4);
    l_oR.m_oUShadowMtx = bgfx::createUniform("u_shadowMtx", bgfx::UniformType::Mat4, m_iMaxShadowTiles);
    l_oR.m_oUShadowRect = bgfx::createUniform("u_shadowRect", bgfx::UniformType::Vec4, m_iMaxShadowTiles);
    l_oR.m_oWhite = f_SolidTexture(0xffffffff);
    l_oR.m_oFlatNormal = f_SolidTexture(0x8080ffff);
    l_oR.m_vJoints.assign(gltf::g_iMaxJoints, glm::mat4(1.0f));
    const bgfx::Caps* l_pCaps = bgfx::getCaps();
    // Depth compare sampling is in every bgfx backend; what can differ is a depth format that can be
    // both rendered to and sampled.
    // OpenGL ES / WebGL: fs_mesh.sc is built without shadow lookups there (WebGL on Windows cannot link it
    // with them -- see the shader), so no shadows on that backend.
    l_oR.m_bShadowCaps = bgfx::isValid(l_oR.m_oShadow) && bgfx::getRendererType() != bgfx::RendererType::OpenGLES &&
                   (l_pCaps->formats[bgfx::TextureFormat::D16] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0 &&
                   (l_pCaps->formats[bgfx::TextureFormat::D16] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0;
    if (!l_oR.m_bShadowCaps) std::fprintf(stderr, "[gltf] no shadow maps on this backend\n");
    // Crowds read their joints from an RGBA32F texture in the vertex shader (texelFetch: no filtering needed).
    l_oR.m_bCrowdCaps = bgfx::isValid(l_oR.m_oMeshAnimTex) && (l_pCaps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0;
    if (!l_oR.m_bCrowdCaps) std::fprintf(stderr, "[gltf] no crowd (animation texture) support on this backend\n");
    // A 1x1 depth texture so the sampler always has something of the right kind bound.
    l_oR.m_oDummyShadow = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::D16, BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL);
    for (int l_iI = 0; l_iI < m_iMaxShadowTiles; l_iI++) {
        l_oR.m_arrShadowMtx[l_iI] = glm::mat4(1.0f);
        l_oR.m_arrShadowRect[l_iI] = glm::vec4(0.0f);
    }
    l_oR.m_bReady = true;
    return true;
}

void GltfRenderer::f_Shutdown() {
    Impl& l_oR = *m_upImpl;
    if (!l_oR.m_bReady) return;
    l_oR.f_ReleaseAtlas();
    f_ClearEnvironment();
    for (bgfx::ProgramHandle l_oP : {l_oR.m_oMesh, l_oR.m_oMeshInst, l_oR.m_oLine, l_oR.m_oShadow, l_oR.m_oShadowInst, l_oR.m_oMeshAnimTex, l_oR.m_oShadowAnimTex, l_oR.m_oSky})
        if (bgfx::isValid(l_oP)) bgfx::destroy(l_oP);
    for (bgfx::UniformHandle l_oU : {l_oR.m_oSBase, l_oR.m_oSMR, l_oR.m_oSNormal, l_oR.m_oSOcc, l_oR.m_oSEmissive, l_oR.m_oSShadow, l_oR.m_oUBaseColor, l_oR.m_oUPbr, l_oR.m_oUEmissiveCutoff,
                                  l_oR.m_oUTexSet, l_oR.m_oUTexSet2, l_oR.m_oUUvTransform, l_oR.m_oUUvRotation, l_oR.m_oUCamPos, l_oR.m_oUSky, l_oR.m_oUGround, l_oR.m_oUJoints,
                                  l_oR.m_oUMeshFlags, l_oR.m_oUMatParams, l_oR.m_oULightPos, l_oR.m_oULightDirs, l_oR.m_oULightColors, l_oR.m_oULightSpot, l_oR.m_oULightCount,
                                  l_oR.m_oUShadowMtx, l_oR.m_oUShadowRect, l_oR.m_oSAnimTex, l_oR.m_oSSpecular, l_oR.m_oSSpecularColor, l_oR.m_oUSpecular, l_oR.m_oSEnv,
                                  l_oR.m_oUEnvParams, l_oR.m_oUEnvSH, l_oR.m_oUEnvSky, l_oR.m_oUSkyMtx})
        bgfx::destroy(l_oU);
    bgfx::destroy(l_oR.m_oWhite);
    bgfx::destroy(l_oR.m_oFlatNormal);
    if (bgfx::isValid(l_oR.m_oDummyShadow)) bgfx::destroy(l_oR.m_oDummyShadow);
    if (bgfx::isValid(l_oR.m_oEnvDummy)) bgfx::destroy(l_oR.m_oEnvDummy);
    if (bgfx::isValid(l_oR.m_oSkyVb)) bgfx::destroy(l_oR.m_oSkyVb);
    l_oR.m_oEnvDummy = BGFX_INVALID_HANDLE;
    l_oR.m_oSkyVb = BGFX_INVALID_HANDLE;
    l_oR.m_bReady = false;
}

std::shared_ptr<GltfRenderer::GpuModel> GltfRenderer::f_Upload(const gltf::Model& l_oModel) {
    auto l_spGpu = std::make_shared<GpuModel>();
    l_spGpu->m_pModel = &l_oModel;
    for (size_t l_nI = 0; l_nI < l_oModel.m_vTextures.size(); l_nI++) {
        const gltf::Texture& l_oT = l_oModel.m_vTextures[l_nI];
        l_spGpu->m_vTextures.push_back(l_oT.m_iImage >= 0 && l_oT.m_iImage < (int)l_oModel.m_vImages.size() ? f_MakeTexture(l_oModel.m_vImages[(size_t)l_oT.m_iImage], l_oT)
                                                                                   : bgfx::TextureHandle BGFX_INVALID_HANDLE);
    }
    l_spGpu->m_vMeshes.resize(l_oModel.m_vMeshes.size());
    for (size_t l_nM = 0; l_nM < l_oModel.m_vMeshes.size(); l_nM++)
        for (const gltf::Primitive& l_oP : l_oModel.m_vMeshes[l_nM].m_vPrimitives) {
            GpuModel::Prim l_oG;
            std::vector<Vertex> l_vVerts;
            f_FillVertices(l_oP, l_oP.m_vPosition, l_oP.m_vNormal, l_oP.m_vTangent, l_vVerts);
            l_oG.m_bMorph = !l_oP.m_vTargets.empty();
            const bgfx::Memory* l_pMem = bgfx::copy(l_vVerts.data(), uint32_t(l_vVerts.size() * sizeof(Vertex)));
            if (l_oG.m_bMorph) {
                l_oG.m_oDvb = bgfx::createDynamicVertexBuffer(l_pMem, m_upImpl->m_oLayout);
                l_oG.m_vVertices = std::move(l_vVerts);
            } else {
                l_oG.m_oVb = bgfx::createVertexBuffer(l_pMem, m_upImpl->m_oLayout);
            }
            const bool l_bBig = l_oP.m_vPosition.size() > 65535;
            if (l_bBig) {
                l_oG.m_oIb = bgfx::createIndexBuffer(bgfx::copy(l_oP.m_vIndices.data(), uint32_t(l_oP.m_vIndices.size() * 4)), BGFX_BUFFER_INDEX32);
            } else {
                std::vector<uint16_t> l_vI16(l_oP.m_vIndices.begin(), l_oP.m_vIndices.end());
                l_oG.m_oIb = bgfx::createIndexBuffer(bgfx::copy(l_vI16.data(), uint32_t(l_vI16.size() * 2)));
            }
            l_oG.m_uIndexCount = (uint32_t)l_oP.m_vIndices.size();
            l_spGpu->m_vMeshes[l_nM].push_back(std::move(l_oG));
        }
    return l_spGpu;
}

bool GltfRenderer::f_CrowdsSupported() const { return m_upImpl->m_bReady && m_upImpl->m_bCrowdCaps; }

std::shared_ptr<GltfRenderer::AnimTextureGpu> GltfRenderer::f_UploadAnimTexture(std::shared_ptr<const gltf::AnimTexture> l_spAnim) {
    Impl& l_oR = *m_upImpl;
    if (!l_oR.m_bReady || !l_oR.m_bCrowdCaps || !l_spAnim || l_spAnim->m_iRows <= 0 || l_spAnim->m_iWidth <= 0) return nullptr;
    const uint32_t l_uMaxSize = bgfx::getCaps()->limits.maxTextureSize;
    if ((uint32_t)l_spAnim->m_iWidth > l_uMaxSize || (uint32_t)l_spAnim->m_iRows > l_uMaxSize) {
        std::fprintf(stderr, "[gltf] animation texture %dx%d is larger than this GPU allows (%u): bake at a lower fps\n", l_spAnim->m_iWidth,
                     l_spAnim->m_iRows, l_uMaxSize);
        return nullptr;
    }
    auto l_spGpu = std::make_shared<AnimTextureGpu>();
    l_spGpu->m_spData = l_spAnim;
    l_spGpu->m_oTex = bgfx::createTexture2D((uint16_t)l_spAnim->m_iWidth, (uint16_t)l_spAnim->m_iRows, false, 1, bgfx::TextureFormat::RGBA32F,
                                     BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP, bgfx::copy(l_spAnim->m_vTexels.data(), (uint32_t)l_spAnim->f_Bytes()));
    if (!bgfx::isValid(l_spGpu->m_oTex)) return nullptr;
    return l_spGpu;
}

glm::mat4 GltfRenderer::f_Projection(float l_fFovYDegrees, float l_fAspect, float l_fZNear, float l_fZFar) {
    const bool l_bGl = bgfx::getCaps() && bgfx::getCaps()->homogeneousDepth;
    return l_bGl ? glm::perspectiveRH_NO(glm::radians(l_fFovYDegrees), l_fAspect, l_fZNear, l_fZFar)
              : glm::perspectiveRH_ZO(glm::radians(l_fFovYDegrees), l_fAspect, l_fZNear, l_fZFar);
}

namespace {

glm::mat4 f_OrthoFor(float l_fR, float l_fN, float l_fF) {
    const bool l_bGl = bgfx::getCaps()->homogeneousDepth;
    return l_bGl ? glm::orthoRH_NO(-l_fR, l_fR, -l_fR, l_fR, l_fN, l_fF) : glm::orthoRH_ZO(-l_fR, l_fR, -l_fR, l_fR, l_fN, l_fF);
}

glm::mat4 f_LookAlong(const glm::vec3& l_v3Eye, const glm::vec3& l_v3Dir) {
    const glm::vec3 l_v3D = glm::normalize(l_v3Dir);
    const glm::vec3 l_v3Up = std::fabs(l_v3D.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    return glm::lookAt(l_v3Eye, l_v3Eye + l_v3D, l_v3Up);
}

}  // namespace

void GltfRenderer::begin(uint16_t l_u16ViewId, const Frame& l_oFrame, uint32_t l_uWidth, uint32_t l_uHeight, uint32_t l_uClearRgba) {
    Impl& l_oR = *m_upImpl;
    l_oR.m_u16Base = l_u16ViewId;
    l_oR.m_u16View = uint16_t(l_u16ViewId + m_iMaxShadowTiles);
    l_oR.m_oFrame = l_oFrame;
    m_oStats = Stats();

    // ---- lights and their shadow tiles ----
    const bgfx::Caps* l_pCaps = bgfx::getCaps();
    const glm::vec3 l_v3C = (l_oFrame.m_v3BoundsMin + l_oFrame.m_v3BoundsMax) * 0.5f;
    const float l_fRadius = std::max(glm::length(l_oFrame.m_v3BoundsMax - l_oFrame.m_v3BoundsMin) * 0.5f, 1e-3f);
    struct TileJob {
        glm::mat4 m_mat4View, m_mat4Proj;
    };
    std::vector<TileJob> l_vJobs;
    const int l_iCount = std::min((int)l_oFrame.m_vLights.size(), m_iMaxLights);
    const bool l_bShadows = l_oFrame.m_bShadows && l_oR.m_bShadowCaps;
    // Tile size first: how many tiles the casting lights need.
    int l_iWanted = 0;
    for (int l_iI = 0; l_iI < l_iCount; l_iI++)
        if (l_bShadows && l_oFrame.m_vLights[(size_t)l_iI].m_bCastShadows) l_iWanted += l_oFrame.m_vLights[(size_t)l_iI].m_eType == Light::Type::Point ? 6 : 1;
    l_iWanted = std::min(l_iWanted, m_iMaxShadowTiles);
    const int l_iGrid = l_iWanted > 0 ? (int)std::ceil(std::sqrt((double)l_iWanted)) : 1;
    const int l_iSize = std::clamp(l_oFrame.m_iShadowMapSize, 256, 8192);
    const int l_iTilePx = l_iSize / l_iGrid;
    const float l_fTexelUv = 1.0f / (float)l_iSize;

    for (int l_iI = 0; l_iI < l_iCount; l_iI++) {
        const Light& l_oL = l_oFrame.m_vLights[(size_t)l_iI];
        const glm::vec3 l_v3Dir = glm::length(l_oL.m_v3Direction) > 0 ? glm::normalize(l_oL.m_v3Direction) : glm::vec3(0, -1, 0);
        const float l_fType = l_oL.m_eType == Light::Type::Directional ? 0.0f : l_oL.m_eType == Light::Type::Point ? 1.0f : 2.0f;
        l_oR.m_arrLightPos[l_iI] = glm::vec4(l_oL.m_v3Position, l_fType);
        l_oR.m_arrLightDirs[l_iI] = glm::vec4(l_v3Dir, std::max(l_oL.m_fRange, 0.0f));
        const float l_fCosOuter = std::cos(std::clamp(l_oL.m_fOuterCone, 0.001f, 1.57f));
        const float l_fCosInner = std::cos(std::clamp(std::min(l_oL.m_fInnerCone, l_oL.m_fOuterCone), 0.0f, 1.57f));
        float l_fBias = 0.0f, l_fNormalOffset = 0.0f;
        int l_iFirst = -1;
        const int l_iNeed = l_oL.m_eType == Light::Type::Point ? 6 : 1;
        if (l_bShadows && l_oL.m_bCastShadows && (int)l_vJobs.size() + l_iNeed <= m_iMaxShadowTiles) {
            l_iFirst = (int)l_vJobs.size();
            // How far the light reaches: its range, else the far side of the bounds.
            const float l_fReach = l_oL.m_fRange > 0 ? l_oL.m_fRange : glm::length(l_oL.m_v3Position - l_v3C) + l_fRadius;
            if (l_oL.m_eType == Light::Type::Directional) {
                const glm::mat4 l_mat4V = f_LookAlong(l_v3C - l_v3Dir * l_fRadius * 2.0f, l_v3Dir);
                l_vJobs.push_back({l_mat4V, f_OrthoFor(l_fRadius, 0.0f, l_fRadius * 4.0f)});
                l_fNormalOffset = 2.0f * l_fRadius / (float)l_iTilePx * 2.5f;   // world size of a texel, x 2.5
                l_fBias = 0.001f;   // orthographic: depth is linear, 0.1% of the 4 r depth range
            } else {
                const float l_fFovY = l_oL.m_eType == Light::Type::Spot ? std::min(2.0f * std::clamp(l_oL.m_fOuterCone, 0.01f, 1.5f) + 0.1f, 3.0f)
                                                               : glm::radians(91.0f);   // a cube face (a hair wider: no seams)
                const float l_fZf = std::max(l_fReach, 0.01f), l_fZn = std::max(l_fZf * 0.01f, 0.01f);   // near: depth precision
                const glm::mat4 l_mat4Proj = f_Projection(glm::degrees(l_fFovY), 1.0f, l_fZn, l_fZf);
                if (l_oL.m_eType == Light::Type::Spot) {
                    l_vJobs.push_back({f_LookAlong(l_oL.m_v3Position, l_v3Dir), l_mat4Proj});
                } else {   // faces in the shader's order: +X -X +Y -Y +Z -Z
                    const glm::vec3 l_arrAxes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    for (const glm::vec3& l_v3A : l_arrAxes) l_vJobs.push_back({f_LookAlong(l_oL.m_v3Position, l_v3A), l_mat4Proj});
                }
                l_fNormalOffset = 2.0f * std::tan(l_fFovY * 0.5f) / (float)l_iTilePx * 2.0f;   // x distance in the shader
                l_fBias = 0.0f;   // perspective: the shader steps towards the light instead (in world units)
            }
        }
        l_oR.m_arrLightColors[l_iI] = glm::vec4(l_oL.m_v3Color * l_oL.m_fIntensity, (float)l_iFirst);
        l_oR.m_arrLightSpot[l_iI] = glm::vec4(l_fCosOuter, 1.0f / std::max(l_fCosInner - l_fCosOuter, 1e-4f), l_fBias * l_oFrame.m_fShadowBias,
                                   l_fNormalOffset * l_oFrame.m_fShadowBias);
    }
    l_oR.m_iTiles = (int)l_vJobs.size();
    l_oR.m_v4LightCount = glm::vec4((float)l_iCount, l_oFrame.m_fAmbient, l_fTexelUv, l_oR.m_iTiles > 0 ? 1.0f : 0.0f);
    m_oStats.m_iShadowTiles = l_oR.m_iTiles;

    // The shadow views: one per tile, into its square of the atlas.
    if (l_oR.m_iTiles > 0 && l_oR.f_EnsureAtlas(l_iSize)) {
        const float l_fSy = l_pCaps->originBottomLeft ? 0.5f : -0.5f;
        const float l_fSz = l_pCaps->homogeneousDepth ? 0.5f : 1.0f, l_fTz = l_pCaps->homogeneousDepth ? 0.5f : 0.0f;
        glm::mat4 l_mat4Crop(1.0f);   // clip -> texture (uv 0..1, depth 0..1)
        l_mat4Crop[0][0] = 0.5f;
        l_mat4Crop[1][1] = l_fSy;
        l_mat4Crop[2][2] = l_fSz;
        l_mat4Crop[3] = glm::vec4(0.5f, 0.5f, l_fTz, 1.0f);
        const float l_fS = (float)l_iTilePx / (float)l_iSize;
        for (int l_iK = 0; l_iK < l_oR.m_iTiles; l_iK++) {
            const int l_iTx = (l_iK % l_iGrid) * l_iTilePx, l_iTy = (l_iK / l_iGrid) * l_iTilePx;   // pixels from the top-left
            const uint16_t l_u16V = uint16_t(l_oR.m_u16Base + l_iK);
            bgfx::setViewFrameBuffer(l_u16V, l_oR.m_oAtlasFb);
            bgfx::setViewRect(l_u16V, (uint16_t)l_iTx, (uint16_t)l_iTy, (uint16_t)l_iTilePx, (uint16_t)l_iTilePx);
            bgfx::setViewClear(l_u16V, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
            bgfx::setViewTransform(l_u16V, glm::value_ptr(l_vJobs[(size_t)l_iK].m_mat4View), glm::value_ptr(l_vJobs[(size_t)l_iK].m_mat4Proj));
            bgfx::touch(l_u16V);
            const float l_fU0 = (float)l_iTx / (float)l_iSize;
            const float l_fV0 = l_pCaps->originBottomLeft ? 1.0f - (float)(l_iTy + l_iTilePx) / (float)l_iSize : (float)l_iTy / (float)l_iSize;
            glm::mat4 l_mat4Tile(1.0f);
            l_mat4Tile[0][0] = l_fS;
            l_mat4Tile[1][1] = l_fS;
            l_mat4Tile[3] = glm::vec4(l_fU0, l_fV0, 0.0f, 1.0f);
            l_oR.m_arrShadowMtx[l_iK] = l_mat4Tile * l_mat4Crop * l_vJobs[(size_t)l_iK].m_mat4Proj * l_vJobs[(size_t)l_iK].m_mat4View;
            l_oR.m_arrShadowRect[l_iK] = glm::vec4(l_fU0, l_fV0, l_fU0 + l_fS, l_fV0 + l_fS);
        }
    } else {
        l_oR.m_iTiles = 0;
        l_oR.m_v4LightCount.w = 0.0f;
    }

    // ---- the colour views ----
    bgfx::setViewClear(l_oR.m_u16View, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, l_uClearRgba, 1.0f, 0);
    bgfx::setViewClear(uint16_t(l_oR.m_u16View + 1), BGFX_CLEAR_NONE);
    for (uint16_t l_u16V : {l_oR.m_u16View, uint16_t(l_oR.m_u16View + 1)}) {
        bgfx::setViewFrameBuffer(l_u16V, BGFX_INVALID_HANDLE);
        bgfx::setViewRect(l_u16V, 0, 0, (uint16_t)l_uWidth, (uint16_t)l_uHeight);
        bgfx::setViewTransform(l_u16V, glm::value_ptr(l_oFrame.m_mat4View), glm::value_ptr(l_oFrame.m_mat4Proj));
    }
    bgfx::setViewMode(uint16_t(l_oR.m_u16View + 1), bgfx::ViewMode::DepthDescending);   // blended: far to near
    bgfx::touch(l_oR.m_u16View);

    // ---- the environment: this frame's settings, and the picture behind everything ----
    const bool l_bEnv = l_oFrame.m_bEnvironment && bgfx::isValid(l_oR.m_oEnvTex);
    l_oR.m_v4EnvParams = glm::vec4(l_bEnv ? 1.0f : 0.0f, float(gltf::Environment::m_iRoughLevels - 1), l_oFrame.m_fEnvironmentYaw,
                            l_oFrame.m_fEnvironmentIntensity);
    if (l_bEnv && l_oFrame.m_bBackground && bgfx::isValid(l_oR.m_oSky)) {
        const glm::mat4 l_mat4Inv = glm::inverse(l_oFrame.m_mat4Proj * l_oFrame.m_mat4View);
        const glm::vec4 l_v4Cam(l_oFrame.m_v3Eye, l_oFrame.m_fExposure), l_v4SkyBlur(std::clamp(l_oFrame.m_fBackgroundBlur, 0.0f, 1.0f) * l_oR.m_v4EnvParams.y, 0, 0, 0);
        const glm::vec4 l_v4Sky(l_oFrame.m_v3Sky, l_oFrame.m_bSrgbOut ? 1.0f : 0.0f), l_v4Ground(l_oFrame.m_v3Ground, l_oFrame.m_bTonemap ? 1.0f : 0.0f);
        bgfx::setUniform(l_oR.m_oUSkyMtx, glm::value_ptr(l_mat4Inv));
        bgfx::setUniform(l_oR.m_oUCamPos, &l_v4Cam);
        bgfx::setUniform(l_oR.m_oUSky, &l_v4Sky);
        bgfx::setUniform(l_oR.m_oUGround, &l_v4Ground);
        bgfx::setUniform(l_oR.m_oUEnvParams, &l_oR.m_v4EnvParams);
        bgfx::setUniform(l_oR.m_oUEnvSky, &l_v4SkyBlur);
        bgfx::setTexture(9, l_oR.m_oSEnv, l_oR.m_oEnvTex);
        bgfx::setVertexBuffer(0, l_oR.m_oSkyVb);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL);   // at depth 1: only where nothing is drawn
        bgfx::submit(l_oR.m_u16View, l_oR.m_oSky);
        m_oStats.m_iDrawCalls++;
    }
}

bool GltfRenderer::f_SetEnvironment(const gltf::Environment& l_oEnv, std::string* l_pError) {
    Impl& l_oR = *m_upImpl;
    auto l_fnFail = [&](const char* l_pszWhy) {
        if (l_pError) *l_pError = l_pszWhy;
        std::fprintf(stderr, "[gltf] environment: %s\n", l_pszWhy);
        return false;
    };
    if (!l_oR.m_bReady) return l_fnFail("the renderer is not started");
    if (!l_oEnv.f_Valid() || l_oEnv.m_iFaceSize > 4096) return l_fnFail("an empty environment");
    if ((bgfx::getCaps()->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_CUBE) == 0)
        return l_fnFail("this backend cannot sample RGBA16F cubemaps");
    f_ClearEnvironment();
    const uint16_t l_u16Size = (uint16_t)l_oEnv.m_iFaceSize;
    l_oR.m_oEnvTex = bgfx::createTextureCube(l_u16Size, l_oEnv.f_Mips() > 1, 1, bgfx::TextureFormat::RGBA16F, BGFX_SAMPLER_UVW_CLAMP);
    if (!bgfx::isValid(l_oR.m_oEnvTex)) return l_fnFail("the cubemap could not be made");
    const uint16_t l_u16One = bx::halfFromFloat(1.0f);
    for (int l_iMip = 0; l_iMip < l_oEnv.f_Mips(); l_iMip++)
        for (int l_iFace = 0; l_iFace < 6; l_iFace++) {
            const uint16_t l_u16S = uint16_t(std::max(l_u16Size >> l_iMip, 1));
            const std::vector<float>& l_vPx = l_oEnv.m_vFaces[(size_t)(l_iMip * 6 + l_iFace)];
            const bgfx::Memory* l_pMem = bgfx::alloc(uint32_t(l_u16S) * l_u16S * 8);
            uint16_t* l_pO = (uint16_t*)l_pMem->data;
            for (size_t l_nI = 0; l_nI < (size_t)l_u16S * l_u16S; l_nI++) {
                for (int l_iC = 0; l_iC < 3; l_iC++) {
                    const float l_fV = l_vPx[l_nI * 3 + (size_t)l_iC];
                    l_pO[l_nI * 4 + (size_t)l_iC] = bx::halfFromFloat(l_fV > 0.0f ? std::min(l_fV, 60000.0f) : 0.0f);   // (NaN -> 0)
                }
                l_pO[l_nI * 4 + 3] = l_u16One;
            }
            bgfx::updateTextureCube(l_oR.m_oEnvTex, 0, (uint8_t)l_iFace, (uint8_t)l_iMip, 0, 0, l_u16S, l_u16S, l_pMem);
        }
    for (int l_iI = 0; l_iI < 9; l_iI++) l_oR.m_arrEnvSH[l_iI] = glm::vec4(l_oEnv.m_arrSh[l_iI], 0.0f);
    return true;
}

void GltfRenderer::f_ClearEnvironment() {
    Impl& l_oR = *m_upImpl;
    if (bgfx::isValid(l_oR.m_oEnvTex)) bgfx::destroy(l_oR.m_oEnvTex);
    l_oR.m_oEnvTex = BGFX_INVALID_HANDLE;
}

bool GltfRenderer::f_HasEnvironment() const { return bgfx::isValid(m_upImpl->m_oEnvTex); }

void GltfRenderer::f_Draw(GpuModel& l_oGpu, const gltf::Pose& l_oPose, const glm::mat4& l_mat4Placement, bool l_bCastShadows) {
    Impl& l_oR = *m_upImpl;
    if (!l_oR.m_bReady || !l_oGpu.m_pModel || l_oPose.f_Worlds().size() != l_oGpu.m_pModel->m_vNodes.size()) return;
    const gltf::Model& l_oModel = *l_oGpu.m_pModel;
    const Frame& l_oF = l_oR.m_oFrame;
    static const gltf::Material l_oDefault = [] {
        gltf::Material l_oM;
        l_oM.m_fMetallic = 0.0f;
        l_oM.m_fRoughness = 0.6f;
        return l_oM;
    }();

    if (l_oModel.m_vScenes.empty()) return;
    const gltf::Scene& l_oScene = l_oModel.m_vScenes[(size_t)std::clamp(l_oModel.m_iScene, 0, (int)l_oModel.m_vScenes.size() - 1)];
    std::vector<int> l_vStack(l_oScene.m_vRoots.rbegin(), l_oScene.m_vRoots.rend());
    while (!l_vStack.empty()) {
        const int l_iNi = l_vStack.back();
        l_vStack.pop_back();
        const gltf::Node& l_oNode = l_oModel.m_vNodes[(size_t)l_iNi];
        for (auto l_itIt = l_oNode.m_vChildren.rbegin(); l_itIt != l_oNode.m_vChildren.rend(); ++l_itIt) l_vStack.push_back(*l_itIt);
        if (l_oNode.m_iMesh < 0 || l_oNode.m_iMesh >= (int)l_oGpu.m_vMeshes.size()) continue;
        const gltf::Mesh& l_oMesh = l_oModel.m_vMeshes[(size_t)l_oNode.m_iMesh];
        const glm::mat4 l_mat4World = l_mat4Placement * l_oPose.f_World(l_iNi);
        const bool l_bMirrored = glm::determinant(glm::mat3(l_mat4World)) < 0.0f;
        const bool l_bSkinNode = l_oNode.m_iSkin >= 0;
        if (l_bSkinNode) {
            l_oPose.f_JointMatrices(l_oNode.m_iSkin, l_oR.m_vJoints);
            if (l_oR.m_vJoints.empty()) l_oR.m_vJoints.assign(1, glm::mat4(1.0f));
        }
        for (size_t l_nPi = 0; l_nPi < l_oMesh.m_vPrimitives.size() && l_nPi < l_oGpu.m_vMeshes[(size_t)l_oNode.m_iMesh].size(); l_nPi++) {
            const gltf::Primitive& l_oP = l_oMesh.m_vPrimitives[l_nPi];
            GpuModel::Prim& l_oG = l_oGpu.m_vMeshes[(size_t)l_oNode.m_iMesh][l_nPi];
            const gltf::Material& l_oMat = l_oP.m_iMaterial >= 0 && l_oP.m_iMaterial < (int)l_oModel.m_vMaterials.size() ? l_oModel.m_vMaterials[(size_t)l_oP.m_iMaterial] : l_oDefault;

            // Morph targets: CPU morph when this node's weights differ from the last upload.
            if (l_oG.m_bMorph) {
                const std::vector<float>& l_vW = l_oPose.f_Weights(l_iNi);
                if (l_vW != l_oG.m_vLastWeights) {
                    std::vector<glm::vec3> l_vPos, l_vNrm;
                    std::vector<glm::vec4> l_vTan;
                    gltf::f_Morph(l_oP, l_vW, l_vPos, l_vNrm, l_vTan);
                    f_FillVertices(l_oP, l_vPos, l_vNrm, l_vTan, l_oG.m_vVertices);
                    bgfx::update(l_oG.m_oDvb, 0, bgfx::copy(l_oG.m_vVertices.data(), uint32_t(l_oG.m_vVertices.size() * sizeof(Vertex))));
                    l_oG.m_vLastWeights = l_vW;
                    m_oStats.m_iMorphUploads++;
                }
            }

            // Placement: skinned vertices come out of the joints in model space; instances multiply in.
            const bool l_bSkinned = l_bSkinNode && l_oP.f_Skinned();
            const glm::mat4 l_mat4Base = l_bSkinned ? l_mat4Placement : l_mat4World;
            const size_t l_nInstances = l_oNode.m_vInstances.size();
            bgfx::InstanceDataBuffer l_oIdb{};
            bool l_bInstanced = false;
            if (l_nInstances > 0 && bgfx::isValid(l_oR.m_oMeshInst)) {
                if (bgfx::getAvailInstanceDataBuffer((uint32_t)l_nInstances, 64) < l_nInstances) continue;
                bgfx::allocInstanceDataBuffer(&l_oIdb, (uint32_t)l_nInstances, 64);
                glm::mat4* l_pOut = (glm::mat4*)l_oIdb.data;
                for (size_t l_nK = 0; l_nK < l_nInstances; l_nK++) l_pOut[l_nK] = l_mat4Base * l_oNode.m_vInstances[l_nK];
                l_bInstanced = true;
                m_oStats.m_iInstances += (int)l_nInstances;
            }
            const glm::vec4 l_v4MeshFlags(l_bSkinned ? 1.0f : 0.0f, 0, 0, 0);
            const uint16_t l_u16JointCount = (uint16_t)std::min<size_t>(l_oR.m_vJoints.size(), gltf::g_iMaxJoints);
            auto l_fnGeometry = [&] {   // what the colour and the shadow draws share
                if (l_bInstanced) bgfx::setInstanceDataBuffer(&l_oIdb);
                else bgfx::setTransform(glm::value_ptr(l_mat4Base));
                if (l_bSkinned) bgfx::setUniform(l_oR.m_oUJoints, l_oR.m_vJoints.data(), l_u16JointCount);
                bgfx::setUniform(l_oR.m_oUMeshFlags, &l_v4MeshFlags);
                if (bgfx::isValid(l_oG.m_oDvb)) bgfx::setVertexBuffer(0, l_oG.m_oDvb);
                else bgfx::setVertexBuffer(0, l_oG.m_oVb);
                bgfx::setIndexBuffer(l_oG.m_oIb, 0, l_oG.m_uIndexCount);
            };
            f_SubmitPrimitive(l_oGpu, l_oP, l_oMat, l_fnGeometry, l_bInstanced ? l_oR.m_oMeshInst : l_oR.m_oMesh, l_bInstanced ? l_oR.m_oShadowInst : l_oR.m_oShadow, l_mat4Base, l_bMirrored,
                            l_bCastShadows, l_bSkinned);
        }
    }
}

// One primitive's shadow casts (into every tile) and its colour draw: the material, the lights and the state.
// `geometry` binds what the draws share (vertex / index buffers, the transform or the instance data, joints).
void GltfRenderer::f_SubmitPrimitive(GpuModel& l_oGpu, const gltf::Primitive& l_oP, const gltf::Material& l_oMat, const std::function<void()>& l_fnGeometry,
                                   bgfx::ProgramHandle l_oColourProg, bgfx::ProgramHandle l_oShadowProg, const glm::mat4& l_mat4SortBase,
                                   bool l_bMirrored, bool l_bCastShadows, bool l_bSkinned) {
    Impl& l_oR = *m_upImpl;
    const Frame& l_oF = l_oR.m_oFrame;
    const bool l_bGlass = l_oMat.m_fTransmission > 0.0f;
    const bool l_bBlend = l_bGlass || l_oMat.m_eAlphaMode == gltf::AlphaMode::Blend;

    // Shadow casters: opaque and masked triangles, into every tile.
    if (l_bCastShadows && l_oR.m_iTiles > 0 && !l_bBlend && l_oP.m_eTopology == gltf::Topology::Triangles) {
        const bgfx::ProgramHandle l_oSp = l_oShadowProg;
        for (int l_iK = 0; l_iK < l_oR.m_iTiles; l_iK++) {
            l_fnGeometry();
            bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);   // both faces: thin parts cast too
            bgfx::submit(uint16_t(l_oR.m_u16Base + l_iK), l_oSp);
            m_oStats.m_iShadowDraws++;
        }
    }

    l_fnGeometry();
    if (l_bSkinned) m_oStats.m_iSkinnedDraws++;

    // Material.
    auto l_fnSlot = [&](const gltf::TextureRef& l_oRef, uint8_t l_u8Stage, bgfx::UniformHandle l_oSampler, bgfx::TextureHandle l_oFallback) {
        bgfx::TextureHandle l_oT = l_oRef.f_Valid() && l_oRef.m_iTexture < (int)l_oGpu.m_vTextures.size() ? l_oGpu.m_vTextures[(size_t)l_oRef.m_iTexture]
                                                                                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
        const bool l_bUse = bgfx::isValid(l_oT);
        bgfx::setTexture(l_u8Stage, l_oSampler, l_bUse ? l_oT : l_oFallback);
        return l_bUse ? float(l_oRef.m_iUvSet + 1) : 0.0f;
    };
    const glm::vec4 l_v4TexSet(l_fnSlot(l_oMat.m_oBaseColorTex, 0, l_oR.m_oSBase, l_oR.m_oWhite), l_fnSlot(l_oMat.m_oMetalRoughTex, 1, l_oR.m_oSMR, l_oR.m_oWhite),
                           l_fnSlot(l_oMat.m_oNormalTex, 2, l_oR.m_oSNormal, l_oR.m_oFlatNormal), l_fnSlot(l_oMat.m_oOcclusionTex, 3, l_oR.m_oSOcc, l_oR.m_oWhite));
    const glm::vec4 l_v4TexSet2(l_fnSlot(l_oMat.m_oEmissiveTex, 4, l_oR.m_oSEmissive, l_oR.m_oWhite), l_oMat.m_bUnlit ? 1.0f : 0.0f,
                            l_oP.m_vTangent.empty() ? 0.0f : 1.0f, float(l_oF.m_iDebug));
    bgfx::setTexture(5, l_oR.m_oSShadow, l_oR.m_iTiles > 0 ? l_oR.m_oAtlas : l_oR.m_oDummyShadow);
    const gltf::TextureRef& l_oXf = l_oMat.m_oBaseColorTex.f_Valid() ? l_oMat.m_oBaseColorTex : l_oMat.m_oNormalTex;
    const glm::vec4 l_v4UvT(l_oXf.m_v2Offset, l_oXf.m_v2Scale), l_v4UvR(l_oXf.m_fRotation, 0, 0, 0);
    const glm::vec4 l_v4Pbr(l_oMat.m_fMetallic, l_oMat.m_fRoughness, l_oMat.m_fNormalScale, l_oMat.m_fOcclusionStrength);
    const glm::vec4 l_v4Emc(l_oMat.m_v3Emissive, l_oMat.m_eAlphaMode == gltf::AlphaMode::Mask ? l_oMat.m_fAlphaCutoff : -1.0f);
    bgfx::setUniform(l_oR.m_oUBaseColor, &l_oMat.m_v4BaseColor);
    bgfx::setUniform(l_oR.m_oUPbr, &l_v4Pbr);
    bgfx::setUniform(l_oR.m_oUEmissiveCutoff, &l_v4Emc);
    bgfx::setUniform(l_oR.m_oUTexSet, &l_v4TexSet);
    bgfx::setUniform(l_oR.m_oUTexSet2, &l_v4TexSet2);
    bgfx::setUniform(l_oR.m_oUUvTransform, &l_v4UvT);
    bgfx::setUniform(l_oR.m_oUUvRotation, &l_v4UvR);
    // KHR_materials_specular / _ior (stages 7, 8: 6 is the crowd's animation texture): the dielectric F0 colour before
    // its texture, ((ior - 1) / (ior + 1))^2 x specularColorFactor, and the specular factor.
    const glm::vec4 l_v4MatParams(l_oMat.m_bDoubleSided ? 1.0f : 0.0f, l_oMat.m_fTransmission, l_fnSlot(l_oMat.m_oSpecularTex, 7, l_oR.m_oSSpecular, l_oR.m_oWhite),
                              l_fnSlot(l_oMat.m_oSpecularColorTex, 8, l_oR.m_oSSpecularColor, l_oR.m_oWhite));
    bgfx::setUniform(l_oR.m_oUMatParams, &l_v4MatParams);
    const float l_fF0Ior = (l_oMat.m_fIor - 1.0f) / (l_oMat.m_fIor + 1.0f);
    const glm::vec4 l_v4Specular(l_oMat.m_v3SpecularColor * (l_fF0Ior * l_fF0Ior), l_oMat.m_fSpecular);
    bgfx::setUniform(l_oR.m_oUSpecular, &l_v4Specular);
    bgfx::setTexture(9, l_oR.m_oSEnv, l_oR.m_v4EnvParams.x > 0.5f ? l_oR.m_oEnvTex : l_oR.m_oEnvDummy);
    bgfx::setUniform(l_oR.m_oUEnvParams, &l_oR.m_v4EnvParams);
    bgfx::setUniform(l_oR.m_oUEnvSH, l_oR.m_arrEnvSH, 9);
    const glm::vec4 l_v4Cam(l_oF.m_v3Eye, l_oF.m_fExposure);
    const glm::vec4 l_v4Sky(l_oF.m_v3Sky, l_oF.m_bSrgbOut ? 1.0f : 0.0f), l_v4Ground(l_oF.m_v3Ground, l_oF.m_bTonemap ? 1.0f : 0.0f);
    bgfx::setUniform(l_oR.m_oUCamPos, &l_v4Cam);
    bgfx::setUniform(l_oR.m_oUSky, &l_v4Sky);
    bgfx::setUniform(l_oR.m_oUGround, &l_v4Ground);
    bgfx::setUniform(l_oR.m_oULightPos, l_oR.m_arrLightPos, m_iMaxLights);
    bgfx::setUniform(l_oR.m_oULightDirs, l_oR.m_arrLightDirs, m_iMaxLights);
    bgfx::setUniform(l_oR.m_oULightColors, l_oR.m_arrLightColors, m_iMaxLights);
    bgfx::setUniform(l_oR.m_oULightSpot, l_oR.m_arrLightSpot, m_iMaxLights);
    bgfx::setUniform(l_oR.m_oULightCount, &l_oR.m_v4LightCount);
    bgfx::setUniform(l_oR.m_oUShadowMtx, l_oR.m_arrShadowMtx, m_iMaxShadowTiles);
    bgfx::setUniform(l_oR.m_oUShadowRect, l_oR.m_arrShadowRect, m_iMaxShadowTiles);

    // State. glTF front faces are counter-clockwise: cull the clockwise ones (the other way
    // round under a mirroring transform).
    uint64_t l_u64State = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
    if (!l_oMat.m_bDoubleSided) l_u64State |= l_bMirrored ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
    if (l_oP.m_eTopology == gltf::Topology::Lines) l_u64State |= BGFX_STATE_PT_LINES;
    else if (l_oP.m_eTopology == gltf::Topology::Points) l_u64State |= BGFX_STATE_PT_POINTS;
    // Glass (transmission): premultiplied, so its reflections stay bright over what is behind.
    if (l_bGlass) l_u64State |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    else if (l_bBlend) l_u64State |= BGFX_STATE_BLEND_ALPHA;
    else l_u64State |= BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z;
    bgfx::setState(l_u64State);

    const bgfx::ProgramHandle l_oProg = l_oColourProg;
    if (l_bBlend) {   // the second view sorts these far to near
        const glm::vec3 l_v3Cc = glm::vec3(l_mat4SortBase * glm::vec4((l_oP.m_v3BoundsMin + l_oP.m_v3BoundsMax) * 0.5f, 1.0f));
        const float l_fDist = glm::length(l_v3Cc - l_oF.m_v3Eye);
        bgfx::submit(uint16_t(l_oR.m_u16View + 1), l_oProg, (uint32_t)std::min(l_fDist * 1000.0f, 4.0e9f));
    } else {
        bgfx::submit(l_oR.m_u16View, l_oProg);
    }
    m_oStats.m_iDrawCalls++;
}

void GltfRenderer::f_DrawCrowd(GpuModel& l_oGpu, const AnimTextureGpu& l_oAnim, const gltf::Crowd& l_oCrowd, bool l_bCastShadows) {
    l_oCrowd.f_GpuInstances(m_upImpl->m_vCrowdScratch);
    f_DrawCrowd(l_oGpu, l_oAnim, m_upImpl->m_vCrowdScratch, l_bCastShadows);
}

void GltfRenderer::f_DrawCrowd(GpuModel& l_oGpu, const AnimTextureGpu& l_oAnim, const std::vector<gltf::CrowdGpuInstance>& l_vInstances,
                             bool l_bCastShadows) {
    Impl& l_oR = *m_upImpl;
    if (!l_oR.m_bReady || !l_oR.m_bCrowdCaps || !l_oGpu.m_pModel || !l_oAnim.m_spData || !bgfx::isValid(l_oAnim.m_oTex) || l_vInstances.empty()) return;
    const gltf::Model& l_oModel = *l_oGpu.m_pModel;
    const int l_iSkin = l_oAnim.m_spData->m_iSkin;
    if (l_oModel.m_vScenes.empty() || l_iSkin < 0) return;
    static const gltf::Material l_oDefault = [] {
        gltf::Material l_oM;
        l_oM.m_fMetallic = 0.0f;
        l_oM.m_fRoughness = 0.6f;
        return l_oM;
    }();
    const uint32_t l_uCount = (uint32_t)l_vInstances.size();
    // Blended primitives sort by the first instance (one draw holds them all).
    glm::mat4 l_mat4SortBase(1.0f);
    for (int l_iC = 0; l_iC < 4; l_iC++)
        for (int l_iK = 0; l_iK < 3; l_iK++) l_mat4SortBase[l_iC][l_iK] = l_vInstances[0].m_arrRows[l_iK][l_iC];
    const gltf::Scene& l_oScene = l_oModel.m_vScenes[(size_t)std::clamp(l_oModel.m_iScene, 0, (int)l_oModel.m_vScenes.size() - 1)];
    std::vector<int> l_vStack(l_oScene.m_vRoots.rbegin(), l_oScene.m_vRoots.rend());
    while (!l_vStack.empty()) {
        const int l_iNi = l_vStack.back();
        l_vStack.pop_back();
        const gltf::Node& l_oNode = l_oModel.m_vNodes[(size_t)l_iNi];
        for (auto l_itIt = l_oNode.m_vChildren.rbegin(); l_itIt != l_oNode.m_vChildren.rend(); ++l_itIt) l_vStack.push_back(*l_itIt);
        if (l_oNode.m_iMesh < 0 || l_oNode.m_iMesh >= (int)l_oGpu.m_vMeshes.size() || l_oNode.m_iSkin != l_iSkin) continue;
        const gltf::Mesh& l_oMesh = l_oModel.m_vMeshes[(size_t)l_oNode.m_iMesh];
        for (size_t l_nPi = 0; l_nPi < l_oMesh.m_vPrimitives.size() && l_nPi < l_oGpu.m_vMeshes[(size_t)l_oNode.m_iMesh].size(); l_nPi++) {
            const gltf::Primitive& l_oP = l_oMesh.m_vPrimitives[l_nPi];
            if (!l_oP.f_Skinned()) continue;
            GpuModel::Prim& l_oG = l_oGpu.m_vMeshes[(size_t)l_oNode.m_iMesh][l_nPi];
            const gltf::Material& l_oMat = l_oP.m_iMaterial >= 0 && l_oP.m_iMaterial < (int)l_oModel.m_vMaterials.size() ? l_oModel.m_vMaterials[(size_t)l_oP.m_iMaterial] : l_oDefault;
            if (bgfx::getAvailInstanceDataBuffer(l_uCount, sizeof(gltf::CrowdGpuInstance)) < l_uCount) continue;
            bgfx::InstanceDataBuffer l_oIdb;
            bgfx::allocInstanceDataBuffer(&l_oIdb, l_uCount, sizeof(gltf::CrowdGpuInstance));
            std::memcpy(l_oIdb.data, l_vInstances.data(), l_uCount * sizeof(gltf::CrowdGpuInstance));
            auto l_fnGeometry = [&] {
                bgfx::setInstanceDataBuffer(&l_oIdb);
                bgfx::setTexture(6, l_oR.m_oSAnimTex, l_oAnim.m_oTex);
                if (bgfx::isValid(l_oG.m_oDvb)) bgfx::setVertexBuffer(0, l_oG.m_oDvb);
                else bgfx::setVertexBuffer(0, l_oG.m_oVb);
                bgfx::setIndexBuffer(l_oG.m_oIb, 0, l_oG.m_uIndexCount);
            };
            f_SubmitPrimitive(l_oGpu, l_oP, l_oMat, l_fnGeometry, l_oR.m_oMeshAnimTex, l_oR.m_oShadowAnimTex, l_mat4SortBase, false, l_bCastShadows, true);
            m_oStats.m_iInstances += (int)l_uCount;
            m_oStats.m_iCrowdInstances += (int)l_uCount;
        }
    }
}

void GltfRenderer::f_Lines(const std::vector<LinePoint>& l_vPoints, bool l_bDepthTest) {
    Impl& l_oR = *m_upImpl;
    if (!l_oR.m_bReady || l_vPoints.size() < 2) return;
    const uint32_t l_uN = (uint32_t)(l_vPoints.size() & ~size_t(1));
    if (bgfx::getAvailTransientVertexBuffer(l_uN, l_oR.m_oLineLay) < l_uN) return;
    bgfx::TransientVertexBuffer l_oTvb;
    bgfx::allocTransientVertexBuffer(&l_oTvb, l_uN, l_oR.m_oLineLay);
    std::memcpy(l_oTvb.data, l_vPoints.data(), l_uN * sizeof(LinePoint));
    bgfx::setVertexBuffer(0, &l_oTvb);
    uint64_t l_u64State = BGFX_STATE_WRITE_RGB | BGFX_STATE_PT_LINES | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_LINEAA;
    if (l_bDepthTest) l_u64State |= BGFX_STATE_DEPTH_TEST_LESS;
    bgfx::setState(l_u64State);
    bgfx::submit(uint16_t(l_oR.m_u16View + 1), l_oR.m_oLine, 0xffffffffu);   // with the blended draws, after the opaque ones
    m_oStats.m_iDrawCalls++;
}

}  // namespace toms::next
