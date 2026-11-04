// ocean_renderer.cpp -- see ocean_renderer.h.
#include "ocean_renderer.h"

#include "embedded_shaders.h"
#include "job_system.h"

#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace toms::ocean {
namespace {

constexpr float g_fPi = 3.14159265f;

uint16_t f_ToHalf(float l_fV) {
    uint32_t l_uBits;
    std::memcpy(&l_uBits, &l_fV, 4);
    const uint32_t l_uSign = (l_uBits >> 16) & 0x8000u;
    const uint32_t l_uAbs = l_uBits & 0x7fffffffu;
    if (l_uAbs < 0x38800000u) return (uint16_t)l_uSign;                 // too small for a normal half: zero
    if (l_uAbs >= 0x47800000u) return (uint16_t)(l_uSign | 0x7bffu);    // too big or not a number: the largest half
    return (uint16_t)(l_uSign | ((l_uAbs - 0x38000000u + 0x1000u) >> 13));
}

int f_MipCount(int l_iN) {
    int l_iCount = 1;
    while (l_iN > 1) { l_iN /= 2; l_iCount++; }
    return l_iCount;
}

void f_MakeHalves(const float* l_pSrc, size_t l_nFloats, std::vector<uint16_t>& l_vOut) {
    l_vOut.resize(l_nFloats);
    for (size_t l_nI = 0; l_nI < l_nFloats; l_nI++) l_vOut[l_nI] = f_ToHalf(l_pSrc[l_nI]);
}

// Every smaller picture of an RGBA float picture, as halves. The picture repeats, and each level is a plain 2 x 2 average.
void f_BuildMips(const std::vector<float>& l_vBase, int l_iN, std::vector<std::vector<float>>& l_vWork, std::vector<std::vector<uint16_t>>& l_vOut) {
    const int l_iMips = f_MipCount(l_iN);
    l_vOut.resize((size_t)l_iMips);
    l_vWork.resize((size_t)l_iMips);
    f_MakeHalves(l_vBase.data(), l_vBase.size(), l_vOut[0]);
    const float* l_pPrev = l_vBase.data();
    int l_iPrevN = l_iN;
    for (int l_iM = 1; l_iM < l_iMips; l_iM++) {
        const int l_iSize = l_iPrevN / 2;
        std::vector<float>& l_vCur = l_vWork[(size_t)l_iM];
        l_vCur.resize((size_t)l_iSize * l_iSize * 4);
        for (int l_iZ = 0; l_iZ < l_iSize; l_iZ++) {
            const float* l_pRow0 = l_pPrev + (size_t)l_iZ * 2 * l_iPrevN * 4;
            const float* l_pRow1 = l_pRow0 + (size_t)l_iPrevN * 4;
            float* l_pDst = l_vCur.data() + (size_t)l_iZ * l_iSize * 4;
            for (int l_iX = 0; l_iX < l_iSize; l_iX++, l_pDst += 4, l_pRow0 += 8, l_pRow1 += 8)
                for (int l_iC = 0; l_iC < 4; l_iC++) l_pDst[l_iC] = 0.25f * (l_pRow0[l_iC] + l_pRow0[l_iC + 4] + l_pRow1[l_iC] + l_pRow1[l_iC + 4]);
        }
        f_MakeHalves(l_vCur.data(), l_vCur.size(), l_vOut[(size_t)l_iM]);
        l_pPrev = l_vCur.data();
        l_iPrevN = l_iSize;
    }
}

void f_Upload(bgfx::TextureHandle l_oTex, const std::vector<std::vector<uint16_t>>& l_vMips, int l_iN) {
    int l_iSize = l_iN;
    for (size_t l_nM = 0; l_nM < l_vMips.size(); l_nM++) {
        bgfx::updateTexture2D(l_oTex, 0, (uint8_t)l_nM, 0, 0, (uint16_t)l_iSize, (uint16_t)l_iSize,
                              bgfx::copy(l_vMips[l_nM].data(), (uint32_t)(l_vMips[l_nM].size() * sizeof(uint16_t))));
        l_iSize = std::max(l_iSize / 2, 1);
    }
}

// Preetham, Shirley, Smits, "A Practical Analytic Model for Daylight" (1999), appendix A.
struct Perez {
    glm::vec4 m_v4Y, m_v4X, m_v4Z;
    glm::vec3 m_v3E;
    glm::vec3 m_v3Zenith;
};

float f_PerezF(const glm::vec4& l_v4C, float l_fE, float l_fTheta, float l_fGamma) {
    const float l_fCos = std::max(std::cos(l_fTheta), 0.02f), l_fCosG = std::cos(l_fGamma);
    return (1.0f + l_v4C.x * std::exp(l_v4C.y / l_fCos)) * (1.0f + l_v4C.z * std::exp(l_v4C.w * l_fGamma) + l_fE * l_fCosG * l_fCosG);
}

Perez f_MakePerez(float l_fT, float l_fSunTheta) {
    Perez l_oP;
    l_oP.m_v4Y = {0.1787f * l_fT - 1.4630f, -0.3554f * l_fT + 0.4275f, -0.0227f * l_fT + 5.3251f, 0.1206f * l_fT - 2.5771f};
    l_oP.m_v4X = {-0.0193f * l_fT - 0.2592f, -0.0665f * l_fT + 0.0008f, -0.0004f * l_fT + 0.2125f, -0.0641f * l_fT - 0.8989f};
    l_oP.m_v4Z = {-0.0167f * l_fT - 0.2608f, -0.0950f * l_fT + 0.0092f, -0.0079f * l_fT + 0.2102f, -0.0441f * l_fT - 1.6537f};
    l_oP.m_v3E = {-0.0670f * l_fT + 0.3703f, -0.0033f * l_fT + 0.0452f, -0.0109f * l_fT + 0.0529f};
    const float l_fTh = l_fSunTheta, l_fTh2 = l_fTh * l_fTh, l_fTh3 = l_fTh2 * l_fTh, l_fT2 = l_fT * l_fT;
    const float l_fChi = (4.0f / 9.0f - l_fT / 120.0f) * (g_fPi - 2.0f * l_fTh);
    const float l_fYz = std::max((4.0453f * l_fT - 4.9710f) * std::tan(l_fChi) - 0.2155f * l_fT + 2.4192f, 0.02f);
    const float l_fXz = l_fT2 * (0.00166f * l_fTh3 - 0.00375f * l_fTh2 + 0.00209f * l_fTh) + l_fT * (-0.02903f * l_fTh3 + 0.06377f * l_fTh2 - 0.03202f * l_fTh + 0.00394f) +
                        (0.11693f * l_fTh3 - 0.21196f * l_fTh2 + 0.06052f * l_fTh + 0.25886f);
    const float l_fZz = l_fT2 * (0.00275f * l_fTh3 - 0.00610f * l_fTh2 + 0.00317f * l_fTh) + l_fT * (-0.04214f * l_fTh3 + 0.08970f * l_fTh2 - 0.04153f * l_fTh + 0.00516f) +
                        (0.15346f * l_fTh3 - 0.26756f * l_fTh2 + 0.06670f * l_fTh + 0.26688f);
    // The zenith's values over F(0, sun angle), so the shader only multiplies by F(theta, gamma). Y from kcd/m^2 to about 1.
    l_oP.m_v3Zenith = {0.035f * l_fYz /f_PerezF(l_oP.m_v4Y, l_oP.m_v3E.x, 0.0f, l_fSunTheta), l_fXz / f_PerezF(l_oP.m_v4X, l_oP.m_v3E.y, 0.0f, l_fSunTheta),
                       l_fZz / f_PerezF(l_oP.m_v4Z, l_oP.m_v3E.z, 0.0f, l_fSunTheta)};
    return l_oP;
}

// The sun's colour through the air: warm and weak low down, white and strong high up.
glm::vec3 f_SunColor(float l_fElevationDeg, float l_fT) {
    const float l_fS = std::max(std::sin(glm::radians(std::max(l_fElevationDeg, 0.0f))), 0.0f);
    const float l_fAir = 1.0f / (l_fS + 0.15f * std::pow(std::max(l_fElevationDeg, 0.0f) + 3.885f, -1.253f));   // Kasten's air mass
    const glm::vec3 l_v3Thin(0.016f, 0.05f, 0.125f);   // how much each of r, g, b is lost per air mass, in clean air
    const glm::vec3 l_v3Lost = l_v3Thin * (0.6f + 0.2f * l_fT);
    const glm::vec3 l_v3C(std::exp(-l_v3Lost.x * l_fAir), std::exp(-l_v3Lost.y * l_fAir), std::exp(-l_v3Lost.z * l_fAir));
    return l_v3C * 3.2f;
}

}  // namespace

struct OceanRenderer::Impl {
    struct Gpu {
        Cascade m_oSim;
        bgfx::TextureHandle m_oDisp = BGFX_INVALID_HANDLE, m_oSlope = BGFX_INVALID_HANDLE;
        std::vector<std::vector<uint16_t>> m_vDispMips, m_vSlopeMips;   // RGBA16F, ready to upload
        std::vector<std::vector<float>> m_vDispWork, m_vSlopeWork;      // the smaller pictures as floats
    };
    bool f_Init(std::string& l_strError);
    void f_Shutdown();
    void f_SetWaves(const WaveSettings& l_oW, const std::vector<CascadeSettings>& l_vCascades);
    void f_SetGrid(int l_iColumns, int l_iRows);
    void f_Step(double l_dTime, float l_fDt);
    void f_Draw(uint16_t l_u16View, const Look& l_oL, double l_dTime, uint32_t l_uWidth, uint32_t l_uHeight);
    void f_FreeCascade(Gpu& l_oG);
    void f_FreeGrid();

    std::array<Gpu, OceanRenderer::m_iMaxCascades> m_arrCascades;
    int m_iCascades = 0;
    bgfx::ProgramHandle m_oWater = BGFX_INVALID_HANDLE, m_oSky = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_oGridVb = BGFX_INVALID_HANDLE, m_oSkyVb = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_oGridIb = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_oFlat = BGFX_INVALID_HANDLE;   // stands in for a cascade not used
    int m_iColumns = 0, m_iRows = 0;
    std::array<bgfx::UniformHandle, 3> m_arrSDisp{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::UniformHandle, 3> m_arrSSlope{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::UniformHandle m_oUInvVP = BGFX_INVALID_HANDLE, m_oUEye = BGFX_INVALID_HANDLE, m_oUGrid = BGFX_INVALID_HANDLE, m_oUGridB = BGFX_INVALID_HANDLE,
                        m_oUInvSize = BGFX_INVALID_HANDLE, m_oUTexels = BGFX_INVALID_HANDLE, m_oUSun = BGFX_INVALID_HANDLE, m_oUSunColor = BGFX_INVALID_HANDLE,
                        m_oUPerezY = BGFX_INVALID_HANDLE, m_oUPerezX = BGFX_INVALID_HANDLE, m_oUPerezZ = BGFX_INVALID_HANDLE, m_oUPerezE = BGFX_INVALID_HANDLE,
                        m_oUZenith = BGFX_INVALID_HANDLE, m_oUClouds = BGFX_INVALID_HANDLE, m_oUDeep = BGFX_INVALID_HANDLE, m_oUScatter = BGFX_INVALID_HANDLE,
                        m_oUFoam = BGFX_INVALID_HANDLE, m_oUDetail = BGFX_INVALID_HANDLE;
    float m_fSimMs = 0.0f, m_fUploadMs = 0.0f;
};

bool OceanRenderer::Impl::f_Init(std::string& l_strError) {
    const bgfx::Caps* l_pCaps = bgfx::getCaps();
    // Only the plain 2D flag: bgfx does not set its vertex-texture flag on OpenGL, where every 4.3 driver can read them.
    if (!(l_pCaps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_2D)) {
        l_strError = "this graphics card has no RGBA16F pictures";
        return false;
    }
    m_oWater = next::f_CreateEmbeddedProgram(next::ShaderProgram::OceanWater);
    m_oSky = next::f_CreateEmbeddedProgram(next::ShaderProgram::OceanSky);
    if (!bgfx::isValid(m_oWater) || !bgfx::isValid(m_oSky)) {
        l_strError = "no ocean shaders for this renderer";
        return false;
    }
    for (int l_iI = 0; l_iI < 3; l_iI++) {
        const std::string l_strD = "s_disp" + std::to_string(l_iI), l_strS = "s_slope" + std::to_string(l_iI);
        m_arrSDisp[(size_t)l_iI] = bgfx::createUniform(l_strD.c_str(), bgfx::UniformType::Sampler);
        m_arrSSlope[(size_t)l_iI] = bgfx::createUniform(l_strS.c_str(), bgfx::UniformType::Sampler);
    }
    auto l_fnVec = [](const char* l_pszName) { return bgfx::createUniform(l_pszName, bgfx::UniformType::Vec4); };
    m_oUInvVP = bgfx::createUniform("u_oceanInvVP", bgfx::UniformType::Mat4);
    m_oUEye = l_fnVec("u_oceanEye");
    m_oUGrid = l_fnVec("u_oceanGrid");
    m_oUGridB = l_fnVec("u_oceanGridB");
    m_oUInvSize = l_fnVec("u_oceanInvSize");
    m_oUTexels = l_fnVec("u_oceanTexels");
    m_oUSun = l_fnVec("u_oceanSun");
    m_oUSunColor = l_fnVec("u_oceanSunColor");
    m_oUPerezY = l_fnVec("u_oceanPerezY");
    m_oUPerezX = l_fnVec("u_oceanPerezX");
    m_oUPerezZ = l_fnVec("u_oceanPerezZ");
    m_oUPerezE = l_fnVec("u_oceanPerezE");
    m_oUZenith = l_fnVec("u_oceanZenith");
    m_oUClouds = l_fnVec("u_oceanClouds");
    m_oUDeep = l_fnVec("u_oceanDeep");
    m_oUScatter = l_fnVec("u_oceanScatter");
    m_oUFoam = l_fnVec("u_oceanFoam");
    m_oUDetail = l_fnVec("u_oceanDetail");

    bgfx::VertexLayout l_oLayout;
    l_oLayout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    const float l_arrTri[] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
    m_oSkyVb = bgfx::createVertexBuffer(bgfx::copy(l_arrTri, sizeof(l_arrTri)), l_oLayout);

    const uint16_t l_arrFlat[4] = {0, 0, 0, 0};
    m_oFlat = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_NONE, bgfx::copy(l_arrFlat, sizeof(l_arrFlat)));
    return true;
}

void OceanRenderer::Impl::f_FreeCascade(Gpu& l_oG) {
    if (bgfx::isValid(l_oG.m_oDisp)) bgfx::destroy(l_oG.m_oDisp);
    if (bgfx::isValid(l_oG.m_oSlope)) bgfx::destroy(l_oG.m_oSlope);
    l_oG.m_oDisp = BGFX_INVALID_HANDLE;
    l_oG.m_oSlope = BGFX_INVALID_HANDLE;
}

void OceanRenderer::Impl::f_FreeGrid() {
    if (bgfx::isValid(m_oGridVb)) bgfx::destroy(m_oGridVb);
    if (bgfx::isValid(m_oGridIb)) bgfx::destroy(m_oGridIb);
    m_oGridVb = BGFX_INVALID_HANDLE;
    m_oGridIb = BGFX_INVALID_HANDLE;
}

void OceanRenderer::Impl::f_Shutdown() {
    for (Gpu& l_oG : m_arrCascades) f_FreeCascade(l_oG);
    f_FreeGrid();
    for (bgfx::ProgramHandle l_oP : {m_oWater, m_oSky})
        if (bgfx::isValid(l_oP)) bgfx::destroy(l_oP);
    if (bgfx::isValid(m_oSkyVb)) bgfx::destroy(m_oSkyVb);
    if (bgfx::isValid(m_oFlat)) bgfx::destroy(m_oFlat);
    for (bgfx::UniformHandle l_oU : {m_arrSDisp[0], m_arrSDisp[1], m_arrSDisp[2], m_arrSSlope[0], m_arrSSlope[1], m_arrSSlope[2], m_oUInvVP, m_oUEye, m_oUGrid, m_oUGridB, m_oUInvSize,
                                     m_oUTexels, m_oUSun, m_oUSunColor, m_oUPerezY, m_oUPerezX, m_oUPerezZ, m_oUPerezE, m_oUZenith, m_oUClouds, m_oUDeep, m_oUScatter, m_oUFoam, m_oUDetail})
        if (bgfx::isValid(l_oU)) bgfx::destroy(l_oU);
}

void OceanRenderer::Impl::f_SetWaves(const WaveSettings& l_oW, const std::vector<CascadeSettings>& l_vCascades) {
    m_iCascades = std::min((int)l_vCascades.size(), OceanRenderer::m_iMaxCascades);
    for (int l_iI = 0; l_iI < OceanRenderer::m_iMaxCascades; l_iI++) {
        Gpu& l_oG = m_arrCascades[(size_t)l_iI];
        if (l_iI >= m_iCascades) {
            f_FreeCascade(l_oG);
            l_oG.m_oSim = Cascade();
            continue;
        }
        const CascadeSettings& l_oC = l_vCascades[(size_t)l_iI];
        // Each patch keeps its waves from 6 of its own wave numbers up to where the next smaller patch takes over; the
        // largest also keeps everything longer.
        const float l_fKMin = l_iI == 0 ? 0.0f : 6.0f * 2.0f * g_fPi / l_oC.m_fSize;
        const float l_fKMax = l_iI + 1 < m_iCascades ? 6.0f * 2.0f * g_fPi / l_vCascades[(size_t)l_iI + 1].m_fSize : 1e9f;
        const bool l_bSame = l_oG.m_oSim.f_N() == l_oC.m_iN && bgfx::isValid(l_oG.m_oDisp);
        l_oG.m_oSim.f_Init(l_oC.m_iN, l_oC.m_fSize, l_fKMin, l_fKMax, l_oW, 1234u + (uint32_t)l_iI * 77u);
        if (l_bSame) continue;
        f_FreeCascade(l_oG);
        const uint64_t l_u64Flags = BGFX_TEXTURE_NONE;
        l_oG.m_oDisp = bgfx::createTexture2D((uint16_t)l_oC.m_iN, (uint16_t)l_oC.m_iN, true, 1, bgfx::TextureFormat::RGBA16F, l_u64Flags);
        l_oG.m_oSlope = bgfx::createTexture2D((uint16_t)l_oC.m_iN, (uint16_t)l_oC.m_iN, true, 1, bgfx::TextureFormat::RGBA16F, l_u64Flags);
    }
}

void OceanRenderer::Impl::f_SetGrid(int l_iColumns, int l_iRows) {
    if (l_iColumns == m_iColumns && l_iRows == m_iRows && bgfx::isValid(m_oGridVb)) return;
    f_FreeGrid();
    m_iColumns = l_iColumns;
    m_iRows = l_iRows;
    bgfx::VertexLayout l_oLayout;
    l_oLayout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    const size_t l_nVerts = (size_t)(l_iColumns + 1) * (l_iRows + 1);
    const bgfx::Memory* l_pV = bgfx::alloc((uint32_t)(l_nVerts * 3 * sizeof(float)));
    float* l_pF = (float*)l_pV->data;
    for (int l_iY = 0; l_iY <= l_iRows; l_iY++)
        for (int l_iX = 0; l_iX <= l_iColumns; l_iX++) {
            *l_pF++ = (float)l_iX / l_iColumns;
            *l_pF++ = (float)l_iY / l_iRows;
            *l_pF++ = 0.0f;
        }
    m_oGridVb = bgfx::createVertexBuffer(l_pV, l_oLayout);
    const bgfx::Memory* l_pI = bgfx::alloc((uint32_t)((size_t)l_iColumns * l_iRows * 6 * sizeof(uint32_t)));
    uint32_t* l_pU = (uint32_t*)l_pI->data;
    const uint32_t l_uStride = (uint32_t)l_iColumns + 1;
    for (int l_iY = 0; l_iY < l_iRows; l_iY++)
        for (int l_iX = 0; l_iX < l_iColumns; l_iX++) {
            const uint32_t l_uA = (uint32_t)l_iY * l_uStride + (uint32_t)l_iX;
            *l_pU++ = l_uA; *l_pU++ = l_uA + 1; *l_pU++ = l_uA + l_uStride;
            *l_pU++ = l_uA + 1; *l_pU++ = l_uA + l_uStride + 1; *l_pU++ = l_uA + l_uStride;
        }
    m_oGridIb = bgfx::createIndexBuffer(l_pI, BGFX_BUFFER_INDEX32);
}

void OceanRenderer::Impl::f_Step(double l_dTime, float l_fDt) {
    using Clock = std::chrono::steady_clock;
    const auto l_oT0 = Clock::now();
    for (int l_iI = 0; l_iI < m_iCascades; l_iI++) m_arrCascades[(size_t)l_iI].m_oSim.f_Step(l_dTime, l_fDt);
    const auto l_oT1 = Clock::now();
    JobSystem::f_ParallelFor(m_iCascades * 2, [&](int l_iJob) {
        Gpu& l_oG = m_arrCascades[(size_t)(l_iJob / 2)];
        if (l_iJob % 2 == 0) f_BuildMips(l_oG.m_oSim.f_Displacement(), l_oG.m_oSim.f_N(), l_oG.m_vDispWork, l_oG.m_vDispMips);
        else f_BuildMips(l_oG.m_oSim.f_Slope(), l_oG.m_oSim.f_N(), l_oG.m_vSlopeWork, l_oG.m_vSlopeMips);
    });
    for (int l_iI = 0; l_iI < m_iCascades; l_iI++) {
        Gpu& l_oG = m_arrCascades[(size_t)l_iI];
        f_Upload(l_oG.m_oDisp, l_oG.m_vDispMips, l_oG.m_oSim.f_N());
        f_Upload(l_oG.m_oSlope, l_oG.m_vSlopeMips, l_oG.m_oSim.f_N());
    }
    const auto l_oT2 = Clock::now();
    m_fSimMs = std::chrono::duration<float, std::milli>(l_oT1 - l_oT0).count();
    m_fUploadMs = std::chrono::duration<float, std::milli>(l_oT2 - l_oT1).count();
}

void OceanRenderer::Impl::f_Draw(uint16_t l_u16View, const Look& l_oL, double l_dTime, uint32_t l_uWidth, uint32_t l_uHeight) {
    bgfx::setViewRect(l_u16View, 0, 0, (uint16_t)l_uWidth, (uint16_t)l_uHeight);
    bgfx::setViewClear(l_u16View, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    bgfx::setViewTransform(l_u16View, glm::value_ptr(l_oL.m_mat4View), glm::value_ptr(l_oL.m_mat4Proj));
    bgfx::setViewMode(l_u16View, bgfx::ViewMode::Sequential);   // the water first: the sky then costs only what still shows
    bgfx::touch(l_u16View);
    if (!bgfx::isValid(m_oGridVb) || m_iCascades == 0) return;

    const glm::mat4 l_mat4ViewProj = l_oL.m_mat4Proj * l_oL.m_mat4View;
    const glm::mat4 l_mat4Inv = glm::inverse(l_mat4ViewProj);
    // Where the horizon is on screen: a point far ahead, level with the eye. The grid's rows stop there.
    const glm::vec3 l_v3Fwd(-l_oL.m_mat4View[0][2], -l_oL.m_mat4View[1][2], -l_oL.m_mat4View[2][2]);
    const glm::vec2 l_v2Flat = glm::length(glm::vec2(l_v3Fwd.x, l_v3Fwd.z)) > 1e-4f ? glm::normalize(glm::vec2(l_v3Fwd.x, l_v3Fwd.z)) : glm::vec2(0.0f, -1.0f);
    const glm::vec4 l_v4H = l_mat4ViewProj * glm::vec4(l_oL.m_v3Eye + glm::vec3(l_v2Flat.x, 0.0f, l_v2Flat.y) * 1e6f, 1.0f);
    const float l_fHorizon = l_v4H.w > 0.0f ? l_v4H.y / l_v4H.w : -1.0f;
    const float l_fTop = std::clamp(l_fHorizon + 0.04f, -1.45f, 1.3f);   // a little over it: the camera may roll
    const float l_fFarthest = 40000.0f;

    const float l_fSunEl = glm::radians(l_oL.m_fSunElevationDeg), l_fSunAz = glm::radians(l_oL.m_fSunAzimuthDeg);
    // Azimuth 0: straight ahead of a camera looking along -Z.
    const glm::vec3 l_v3Sun(std::sin(l_fSunAz) * std::cos(l_fSunEl), std::sin(l_fSunEl), -std::cos(l_fSunAz) * std::cos(l_fSunEl));
    const Perez l_oP = f_MakePerez(l_oL.m_fTurbidity, g_fPi * 0.5f - std::max(l_fSunEl, 0.0f));
    // A closed sky hides the sun.
    const float l_fSunThrough = 1.0f - 0.88f * std::clamp((l_oL.m_fCloudCover - 0.6f) / 0.3f, 0.0f, 1.0f);
    const glm::vec3 l_v3SunColor = f_SunColor(l_oL.m_fSunElevationDeg, l_oL.m_fTurbidity) * l_fSunThrough;

    glm::vec4 l_v4InvSize(0.0f), l_v4Texels(0.0f);
    for (int l_iI = 0; l_iI < m_iCascades; l_iI++) {
        const Cascade& l_oC = m_arrCascades[(size_t)l_iI].m_oSim;
        l_v4InvSize[l_iI] = 1.0f / l_oC.f_Size();
        l_v4Texels[l_iI] = (float)l_oC.f_N() / l_oC.f_Size();
    }
    const float l_fRowAngle = glm::radians(l_oL.m_fFovYDegrees) * (l_fTop + 1.5f) * 0.5f / (float)std::max(m_iRows, 1);
    // How far the grid reaches past the screen's sides, so the waves' sideways motion never pulls its edge into view. A narrow
    // (portrait) screen sees only a few metres of water at its bottom, so the same metres are more of its width.
    const float l_fAspect = (float)l_uWidth / (float)std::max(l_uHeight, 1u);
    const float l_fHalfWidth = 1.0f + 0.3f / std::min(l_fAspect, 1.0f);
    const glm::vec4 l_v4Eye(l_oL.m_v3Eye, l_oL.m_fExposure), l_v4Grid(l_fHalfWidth, -1.5f, l_fTop, l_fFarthest);
    const glm::vec4 l_v4GridB(l_fRowAngle / std::max(l_oL.m_v3Eye.y, 0.5f), l_oL.m_fWaveScale, l_oL.m_bGammaOut ? 1.0f : 0.0f, 0.0f);
    const glm::vec4 l_v4Detail((float)l_oL.m_iSkyOctaves, (float)l_oL.m_iReflectOctaves, (float)l_oL.m_iFoamOctaves, 0.0f);
    const glm::vec4 l_v4Sun(l_v3Sun, (float)std::fmod(l_dTime, 100000.0)), l_v4SunColor(l_v3SunColor, 40.0f);
    const glm::vec4 l_v4E(l_oP.m_v3E, l_oL.m_fSkyStrength), l_v4Zenith(l_oP.m_v3Zenith, 0.0f);
    const glm::vec4 l_v4Clouds(l_oL.m_fCloudCover, l_oL.m_fCloudScale, l_oL.m_fCloudSpeed, l_oL.m_fCloudDark);
    const glm::vec4 l_v4Deep(l_oL.m_v3Deep, std::max(l_oL.m_fRoughness, 0.01f)), l_v4Scatter(l_oL.m_v3Scatter, l_oL.m_fScatterStrength);
    const glm::vec4 l_v4Foam(l_oL.m_fFoam, std::max(l_oL.m_fFogMetres, 1.0f), l_oL.m_fReflection, 0.0f);
    auto l_fnUniforms = [&]() {
        bgfx::setUniform(m_oUInvVP, glm::value_ptr(l_mat4Inv));
        bgfx::setUniform(m_oUEye, &l_v4Eye);
        bgfx::setUniform(m_oUGrid, &l_v4Grid);
        bgfx::setUniform(m_oUGridB, &l_v4GridB);
        bgfx::setUniform(m_oUInvSize, &l_v4InvSize);
        bgfx::setUniform(m_oUTexels, &l_v4Texels);
        bgfx::setUniform(m_oUSun, &l_v4Sun);
        bgfx::setUniform(m_oUSunColor, &l_v4SunColor);
        bgfx::setUniform(m_oUPerezY, &l_oP.m_v4Y);
        bgfx::setUniform(m_oUPerezX, &l_oP.m_v4X);
        bgfx::setUniform(m_oUPerezZ, &l_oP.m_v4Z);
        bgfx::setUniform(m_oUPerezE, &l_v4E);
        bgfx::setUniform(m_oUZenith, &l_v4Zenith);
        bgfx::setUniform(m_oUClouds, &l_v4Clouds);
        bgfx::setUniform(m_oUDeep, &l_v4Deep);
        bgfx::setUniform(m_oUScatter, &l_v4Scatter);
        bgfx::setUniform(m_oUFoam, &l_v4Foam);
        bgfx::setUniform(m_oUDetail, &l_v4Detail);
    };

    // The water.
    l_fnUniforms();
    for (int l_iI = 0; l_iI < 3; l_iI++) {
        const bool l_bOn = l_iI < m_iCascades;
        bgfx::setTexture((uint8_t)l_iI, m_arrSDisp[(size_t)l_iI], l_bOn ? m_arrCascades[(size_t)l_iI].m_oDisp : m_oFlat);
        bgfx::setTexture((uint8_t)(3 + l_iI), m_arrSSlope[(size_t)l_iI], l_bOn ? m_arrCascades[(size_t)l_iI].m_oSlope : m_oFlat);
    }
    bgfx::setVertexBuffer(0, m_oGridVb);
    bgfx::setIndexBuffer(m_oGridIb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA);
    bgfx::submit(l_u16View, m_oWater);

    // The sky, where no water was drawn.
    l_fnUniforms();
    bgfx::setVertexBuffer(0, m_oSkyVb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL);
    bgfx::submit(l_u16View, m_oSky);
}

OceanRenderer::OceanRenderer() : m_upImpl(std::make_unique<Impl>()) {}
OceanRenderer::~OceanRenderer() = default;
bool OceanRenderer::f_Init(std::string& l_strError) { return m_upImpl->f_Init(l_strError); }
void OceanRenderer::f_Shutdown() { m_upImpl->f_Shutdown(); }
void OceanRenderer::f_SetWaves(const WaveSettings& l_oW, const std::vector<CascadeSettings>& l_vCascades) { m_upImpl->f_SetWaves(l_oW, l_vCascades); }
void OceanRenderer::f_SetGrid(int l_iColumns, int l_iRows) { m_upImpl->f_SetGrid(l_iColumns, l_iRows); }
void OceanRenderer::f_SetGridForScreen(int l_iRows, uint32_t l_uWidth, uint32_t l_uHeight) {
    const float l_fAspect = (float)l_uWidth / (float)std::max(l_uHeight, 1u);
    m_upImpl->f_SetGrid(std::clamp((int)((float)l_iRows * std::max(l_fAspect, 0.75f)), 16, 4096), l_iRows);   // (a phone's grid also reaches past its sides)
}
void OceanRenderer::f_Step(double l_dTime, float l_fDt) { m_upImpl->f_Step(l_dTime, l_fDt); }
void OceanRenderer::f_Draw(uint16_t l_u16View, const Look& l_oLook, double l_dTime, uint32_t l_uWidth, uint32_t l_uHeight) { m_upImpl->f_Draw(l_u16View, l_oLook, l_dTime, l_uWidth, l_uHeight); }
float OceanRenderer::f_SimMs() const { return m_upImpl->m_fSimMs; }
float OceanRenderer::f_UploadMs() const { return m_upImpl->m_fUploadMs; }
int OceanRenderer::f_Triangles() const { return m_upImpl->m_iColumns * m_upImpl->m_iRows * 2; }

std::vector<CascadeSettings> f_QualityCascades(const Quality& l_oQ, float l_fSize0, float l_fSize1, float l_fSize2) {
    const float l_arrSizes[] = {l_fSize0, l_fSize1, l_fSize2};
    std::vector<CascadeSettings> l_vC;
    for (int l_iI = 0; l_iI < l_oQ.m_iCascades; l_iI++) {
        CascadeSettings l_oC;
        l_oC.m_iN = l_oQ.m_iResolution;
        l_oC.m_fSize = l_arrSizes[l_iI];
        l_vC.push_back(l_oC);
    }
    return l_vC;
}

void f_PlaceBoatCamera(Look& l_oLook, const BoatCamera& l_oCamera, double l_dTime, uint32_t l_uWidth, uint32_t l_uHeight) {
    const float l_fT = (float)l_dTime;
    const float l_fSway = l_oCamera.m_fSway;
    const float l_fPitch = glm::radians(l_oCamera.m_fPitchDeg + std::sin(l_fT * 0.37f) * 0.5f * l_fSway);
    const float l_fYaw = glm::radians(l_oCamera.m_fYawDeg + std::sin(l_fT * 0.23f) * 0.6f * l_fSway);
    const float l_fHeight = l_oCamera.m_fHeight + std::sin(l_fT * 0.5f) * 0.5f * l_fSway;
    const glm::vec3 l_v3Eye(0.0f, l_fHeight, 0.0f);
    const glm::vec3 l_v3Fwd(-std::sin(l_fYaw) * std::cos(l_fPitch), std::sin(l_fPitch), -std::cos(l_fYaw) * std::cos(l_fPitch));
    const float l_fRoll = glm::radians(std::sin(l_fT * 0.31f) * 0.5f * l_fSway);
    const glm::vec3 l_v3Up(std::sin(l_fRoll), std::cos(l_fRoll), 0.0f);
    l_oLook.m_v3Eye = l_v3Eye;
    l_oLook.m_fFovYDegrees = l_oCamera.m_fFovYDeg;
    l_oLook.m_mat4View = glm::lookAt(l_v3Eye, l_v3Eye + l_v3Fwd, l_v3Up);
    const float l_fAspect = (float)l_uWidth / (float)std::max(l_uHeight, 1u);
    l_oLook.m_mat4Proj = bgfx::getCaps()->homogeneousDepth ? glm::perspectiveRH_NO(glm::radians(l_oCamera.m_fFovYDeg), l_fAspect, 0.5f, 60000.0f)
                                                           : glm::perspectiveRH_ZO(glm::radians(l_oCamera.m_fFovYDeg), l_fAspect, 0.5f, 60000.0f);
}

}  // namespace toms::ocean
