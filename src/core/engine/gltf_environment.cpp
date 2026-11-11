// gltf_environment.cpp -- see gltf_environment.h.
#include "gltf_environment.h"

#include "vfs.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace toms::gltf {

namespace {

constexpr float g_fPi = 3.14159265358979f;

// The picture and its halvings (box-filtered; the columns wrap): sampled at a level that matches how much of the
// sphere a sample stands for, so a blurred level does not alias.
struct Pyramid {
    struct Level {
        int m_iW = 0, m_iH = 0;
        std::vector<float> m_vRgb;
    };
    std::vector<Level> m_vLevels;

    explicit Pyramid(const Equirect& l_oP) {
        m_vLevels.push_back({l_oP.m_iWidth, l_oP.m_iHeight, l_oP.m_vRgb});
        while (m_vLevels.back().m_iW > 8 && m_vLevels.back().m_iH > 4) {
            const Level& l_oA = m_vLevels.back();
            Level l_oB{l_oA.m_iW / 2, l_oA.m_iH / 2, {}};
            l_oB.m_vRgb.resize((size_t)l_oB.m_iW * l_oB.m_iH * 3);
            for (int l_iY = 0; l_iY < l_oB.m_iH; l_iY++)
                for (int l_iX = 0; l_iX < l_oB.m_iW; l_iX++)
                    for (int l_iC = 0; l_iC < 3; l_iC++) {
                        float l_fS = 0.0f;
                        for (int l_iDy = 0; l_iDy < 2; l_iDy++)
                            for (int l_iDx = 0; l_iDx < 2; l_iDx++)
                                l_fS += l_oA.m_vRgb[((size_t)(2 * l_iY + l_iDy) * l_oA.m_iW + (size_t)(2 * l_iX + l_iDx)) * 3 + l_iC];
                        l_oB.m_vRgb[((size_t)l_iY * l_oB.m_iW + l_iX) * 3 + l_iC] = l_fS * 0.25f;
                    }
            m_vLevels.push_back(std::move(l_oB));
        }
    }

    // Bilinear at (u, v) of one level: u wraps, v clamps.
    glm::vec3 f_Fetch(int l_iLevel, float l_fU, float l_fV) const {
        const Level& l_oL = m_vLevels[(size_t)std::clamp(l_iLevel, 0, (int)m_vLevels.size() - 1)];
        const float l_fFx = l_fU * l_oL.m_iW - 0.5f, l_fFy = std::clamp(l_fV * l_oL.m_iH - 0.5f, 0.0f, (float)(l_oL.m_iH - 1));
        const int l_iX0 = (int)std::floor(l_fFx), l_iY0 = (int)l_fFy;
        const float l_fTx = l_fFx - (float)l_iX0, l_fTy = l_fFy - (float)l_iY0;
        auto l_fnAt = [&](int l_iX, int l_iY) {
            l_iX = ((l_iX % l_oL.m_iW) + l_oL.m_iW) % l_oL.m_iW;
            l_iY = std::min(l_iY, l_oL.m_iH - 1);
            const float* l_pP = &l_oL.m_vRgb[((size_t)l_iY * l_oL.m_iW + l_iX) * 3];
            return glm::vec3(l_pP[0], l_pP[1], l_pP[2]);
        };
        return glm::mix(glm::mix(l_fnAt(l_iX0, l_iY0), l_fnAt(l_iX0 + 1, l_iY0), l_fTx), glm::mix(l_fnAt(l_iX0, l_iY0 + 1), l_fnAt(l_iX0 + 1, l_iY0 + 1), l_fTx), l_fTy);
    }

    // The picture in direction d, blurred to `lod` (0 = full size; fractions blend two levels).
    glm::vec3 f_Sample(const glm::vec3& l_v3D, float l_fLod) const {
        const float l_fU = 0.5f + std::atan2(-l_v3D.x, l_v3D.z) / (2.0f * g_fPi);   // the Khronos sample viewer's way round
        const float l_fV = std::acos(std::clamp(l_v3D.y, -1.0f, 1.0f)) / g_fPi;
        l_fLod = std::clamp(l_fLod, 0.0f, (float)(m_vLevels.size() - 1));
        const int l_iL0 = (int)l_fLod;
        const float l_fF = l_fLod - (float)l_iL0;
        const glm::vec3 l_v3A = f_Fetch(l_iL0, l_fU, l_fV);
        return l_fF > 0.0f ? glm::mix(l_v3A, f_Fetch(l_iL0 + 1, l_fU, l_fV), l_fF) : l_v3A;
    }
};

float f_RadicalInverse(uint32_t l_uB) {   // Van der Corput, for the Hammersley points
    l_uB = (l_uB << 16u) | (l_uB >> 16u);
    l_uB = ((l_uB & 0x55555555u) << 1u) | ((l_uB & 0xAAAAAAAAu) >> 1u);
    l_uB = ((l_uB & 0x33333333u) << 2u) | ((l_uB & 0xCCCCCCCCu) >> 2u);
    l_uB = ((l_uB & 0x0F0F0F0Fu) << 4u) | ((l_uB & 0xF0F0F0F0u) >> 4u);
    l_uB = ((l_uB & 0x00FF00FFu) << 8u) | ((l_uB & 0xFF00FF00u) >> 8u);
    return (float)l_uB * 2.3283064365386963e-10f;
}

// The 9 real spherical harmonics (bands 0..2) at unit direction n.
void f_ShBasis(const glm::vec3& l_v3N, float l_arrY[9]) {
    l_arrY[0] = 0.282095f;
    l_arrY[1] = 0.488603f * l_v3N.y;
    l_arrY[2] = 0.488603f * l_v3N.z;
    l_arrY[3] = 0.488603f * l_v3N.x;
    l_arrY[4] = 1.092548f * l_v3N.x * l_v3N.y;
    l_arrY[5] = 1.092548f * l_v3N.y * l_v3N.z;
    l_arrY[6] = 0.315392f * (3.0f * l_v3N.z * l_v3N.z - 1.0f);
    l_arrY[7] = 1.092548f * l_v3N.x * l_v3N.z;
    l_arrY[8] = 0.546274f * (l_v3N.x * l_v3N.x - l_v3N.y * l_v3N.y);
}

}  // namespace

glm::vec3 f_DirectionOf(int l_iFace, float l_fS, float l_fT) {
    glm::vec3 l_v3D;
    switch (l_iFace) {
    case 0: l_v3D = glm::vec3(1.0f, -l_fT, -l_fS); break;    // +X
    case 1: l_v3D = glm::vec3(-1.0f, -l_fT, l_fS); break;    // -X
    case 2: l_v3D = glm::vec3(l_fS, 1.0f, l_fT); break;      // +Y
    case 3: l_v3D = glm::vec3(l_fS, -1.0f, -l_fT); break;    // -Y
    case 4: l_v3D = glm::vec3(l_fS, -l_fT, 1.0f); break;     // +Z
    default: l_v3D = glm::vec3(-l_fS, -l_fT, -1.0f); break;  // -Z
    }
    return glm::normalize(l_v3D);
}

glm::vec3 f_EvalSH(const glm::vec3 l_arrSh[9], const glm::vec3& l_v3N) {
    float l_arrY[9];
    f_ShBasis(l_v3N, l_arrY);
    glm::vec3 l_v3C(0.0f);
    for (int l_iI = 0; l_iI < 9; l_iI++) l_v3C += l_arrSh[l_iI] * l_arrY[l_iI];
    return glm::max(l_v3C, glm::vec3(0.0f));
}

bool f_LoadEquirectFromMemory(const void* l_pData, size_t l_nSize, Equirect& l_oOut, std::string* l_pError) {
    int l_iW = 0, l_iH = 0, l_iN = 0;
    float* l_pPx = stbi_loadf_from_memory((const stbi_uc*)l_pData, (int)l_nSize, &l_iW, &l_iH, &l_iN, 3);
    if (!l_pPx) {
        if (l_pError) *l_pError = std::string("not a picture stb_image can read (") + stbi_failure_reason() + ")";
        return false;
    }
    if (l_iW < 8 || l_iH < 4) {
        stbi_image_free(l_pPx);
        if (l_pError) *l_pError = "the picture is too small";
        return false;
    }
    l_oOut.m_iWidth = l_iW;
    l_oOut.m_iHeight = l_iH;
    l_oOut.m_vRgb.assign(l_pPx, l_pPx + (size_t)l_iW * l_iH * 3);
    stbi_image_free(l_pPx);
    return true;
}

bool f_LoadEquirect(const std::string& l_strPath, Equirect& l_oOut, std::string* l_pError) {
    std::string l_strBytes;
    if (!f_VfsReadAll(l_strPath, l_strBytes)) {
        if (l_pError) *l_pError = "cannot read " + l_strPath;
        return false;
    }
    return f_LoadEquirectFromMemory(l_strBytes.data(), l_strBytes.size(), l_oOut, l_pError);
}

uint16_t f_HalfFromFloat(float l_fV) {
    if (!(l_fV > 0.0f)) return 0;   // (negative, zero, NaN)
    l_fV = std::min(l_fV, 60000.0f);
    uint32_t l_uBits;
    std::memcpy(&l_uBits, &l_fV, 4);
    const int l_iExp = int((l_uBits >> 23) & 0xff) - 127 + 15;
    uint32_t l_uMant = l_uBits & 0x7fffff;
    if (l_iExp <= 0) {   // a subnormal half (or zero)
        if (l_iExp < -10) return 0;
        l_uMant |= 0x800000;
        const int l_iShift = 14 - l_iExp;
        uint32_t l_uH = l_uMant >> l_iShift;
        if ((l_uMant >> (l_iShift - 1)) & 1) l_uH++;   // round
        return (uint16_t)l_uH;
    }
    uint32_t l_uH = (uint32_t(l_iExp) << 10) | (l_uMant >> 13);
    if (l_uMant & 0x1000) l_uH++;   // round to nearest (a carry into the exponent is still right)
    return (uint16_t)std::min<uint32_t>(l_uH, 0x7bff);
}

void f_PackHalf(Environment& l_oEnv) {
    const uint16_t l_u16One = 0x3c00;
    l_oEnv.m_vHalfFaces.assign(l_oEnv.m_vFaces.size(), {});
    for (size_t l_nF = 0; l_nF < l_oEnv.m_vFaces.size(); l_nF++) {
        const std::vector<float>& l_vPx = l_oEnv.m_vFaces[l_nF];
        std::vector<uint16_t>& l_vOut = l_oEnv.m_vHalfFaces[l_nF];
        const size_t l_nCount = l_vPx.size() / 3;
        l_vOut.resize(l_nCount * 4);
        for (size_t l_nI = 0; l_nI < l_nCount; l_nI++) {
            l_vOut[l_nI * 4 + 0] = f_HalfFromFloat(l_vPx[l_nI * 3 + 0]);
            l_vOut[l_nI * 4 + 1] = f_HalfFromFloat(l_vPx[l_nI * 3 + 1]);
            l_vOut[l_nI * 4 + 2] = f_HalfFromFloat(l_vPx[l_nI * 3 + 2]);
            l_vOut[l_nI * 4 + 3] = l_u16One;
        }
    }
}

Environment f_PrefilterEnvironment(const Equirect& l_oPicture, int l_iFaceSize, int l_iSamples) {
    Environment l_oEnv;
    if (l_oPicture.m_iWidth <= 0 || l_oPicture.m_iHeight <= 0 || l_oPicture.m_vRgb.size() < (size_t)l_oPicture.m_iWidth * l_oPicture.m_iHeight * 3) return l_oEnv;
    int l_iSize = 1;
    while (l_iSize < l_iFaceSize) l_iSize <<= 1;
    l_oEnv.m_iFaceSize = l_iSize;
    const Pyramid l_oPyr(l_oPicture);
    // How much of the sphere one pixel of the picture covers (at the equator); a sample covering more reads a
    // smaller level.
    const float l_fTexelP = (2.0f * g_fPi / l_oPicture.m_iWidth) * (g_fPi / l_oPicture.m_iHeight);

    for (int l_iMip = 0, l_iS = l_iSize; l_iS >= 1; l_iMip++, l_iS >>= 1) {
        const float l_fRough = std::min((float)l_iMip / (float)(Environment::m_iRoughLevels - 1), 1.0f);
        const float l_fA = l_fRough * l_fRough, l_fA2 = l_fA * l_fA;   // GGX alpha, as fs_mesh.sc (a = rough^2)
        // The GGX lobe around +Z (the normal = the view = the reflection), the same for every texel of this level:
        // each sample's direction, weight and pyramid level.
        struct Tap {
            glm::vec3 m_v3L;
            float m_fWeight, m_fLod;
        };
        std::vector<Tap> l_vTaps;
        const float l_fTexelCube = (g_fPi * 0.5f / (float)l_iS) * (g_fPi * 0.5f / (float)l_iS);
        if (l_iMip == 0 || l_fRough <= 0.0f) {
            l_vTaps.push_back({glm::vec3(0, 0, 1), 1.0f, std::max(0.5f * std::log2(l_fTexelCube / l_fTexelP), 0.0f)});
        } else {
            for (int l_iI = 0; l_iI < l_iSamples; l_iI++) {
                const float l_fU1 = (l_iI + 0.5f) / (float)l_iSamples, l_fU2 = f_RadicalInverse((uint32_t)l_iI);
                const float l_fPhi = 2.0f * g_fPi * l_fU1;
                const float l_fCosT = std::sqrt((1.0f - l_fU2) / (1.0f + (l_fA2 - 1.0f) * l_fU2));
                const float l_fSinT = std::sqrt(std::max(1.0f - l_fCosT * l_fCosT, 0.0f));
                const glm::vec3 l_v3Hv(l_fSinT * std::cos(l_fPhi), l_fSinT * std::sin(l_fPhi), l_fCosT);
                const glm::vec3 l_v3L = 2.0f * l_v3Hv.z * l_v3Hv - glm::vec3(0, 0, 1);
                if (l_v3L.z <= 0.0f) continue;
                // pdf of l = D(h) * NoH / (4 VoH) = D / 4 here (NoH = VoH); the solid angle the sample stands for.
                const float l_fF = l_fCosT * l_fCosT * (l_fA2 - 1.0f) + 1.0f;
                const float l_fD = l_fA2 / (g_fPi * l_fF * l_fF);
                const float l_fOmegaS = 1.0f / ((float)l_iSamples * l_fD * 0.25f + 1e-6f);
                const float l_fLod = std::max(0.5f * std::log2(l_fOmegaS / l_fTexelP) + 1.0f, 0.0f);
                l_vTaps.push_back({l_v3L, l_v3L.z, l_fLod});
            }
        }
        for (int l_iFace = 0; l_iFace < 6; l_iFace++) {
            std::vector<float> l_vPx((size_t)l_iS * l_iS * 3);
            for (int l_iY = 0; l_iY < l_iS; l_iY++)
                for (int l_iX = 0; l_iX < l_iS; l_iX++) {
                    const glm::vec3 l_v3N = f_DirectionOf(l_iFace, 2.0f * (l_iX + 0.5f) / l_iS - 1.0f, 2.0f * (l_iY + 0.5f) / l_iS - 1.0f);
                    const glm::vec3 l_v3Up = std::fabs(l_v3N.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    const glm::vec3 l_v3Tx = glm::normalize(glm::cross(l_v3Up, l_v3N)), l_v3Ty = glm::cross(l_v3N, l_v3Tx);
                    glm::vec3 l_v3Sum(0.0f);
                    float l_fWsum = 0.0f;
                    for (const Tap& l_oT : l_vTaps) {
                        l_v3Sum += l_oPyr.f_Sample(l_v3Tx * l_oT.m_v3L.x + l_v3Ty * l_oT.m_v3L.y + l_v3N * l_oT.m_v3L.z, l_oT.m_fLod) * l_oT.m_fWeight;
                        l_fWsum += l_oT.m_fWeight;
                    }
                    const glm::vec3 l_v3C = l_fWsum > 0.0f ? l_v3Sum / l_fWsum : glm::vec3(0.0f);
                    float* l_pO = &l_vPx[((size_t)l_iY * l_iS + l_iX) * 3];
                    l_pO[0] = l_v3C.r;
                    l_pO[1] = l_v3C.g;
                    l_pO[2] = l_v3C.b;
                }
            l_oEnv.m_vFaces.push_back(std::move(l_vPx));
        }
    }

    // The diffuse light: project a small level of the picture on the 9 harmonics (each pixel weighted by the solid
    // angle it covers), then convolve with the cosine lobe and divide by pi (bands x 1, 2/3, 1/4).
    size_t l_nLi = 0;
    while (l_nLi + 1 < l_oPyr.m_vLevels.size() && l_oPyr.m_vLevels[l_nLi].m_iW > 128) l_nLi++;
    const Pyramid::Level& l_oL = l_oPyr.m_vLevels[l_nLi];
    glm::vec3 l_arrSh[9] = {};
    for (int l_iY = 0; l_iY < l_oL.m_iH; l_iY++) {
        const float l_fTheta = g_fPi * (l_iY + 0.5f) / l_oL.m_iH;
        const float l_fDOmega = (2.0f * g_fPi / l_oL.m_iW) * (g_fPi / l_oL.m_iH) * std::sin(l_fTheta);
        for (int l_iX = 0; l_iX < l_oL.m_iW; l_iX++) {
            const float l_fPhi = 2.0f * g_fPi * ((l_iX + 0.5f) / l_oL.m_iW - 0.5f);   // u = 0.5 + atan2(-x, z) / 2 pi
            const glm::vec3 l_v3D(-std::sin(l_fTheta) * std::sin(l_fPhi), std::cos(l_fTheta), std::sin(l_fTheta) * std::cos(l_fPhi));
            const float* l_pP = &l_oL.m_vRgb[((size_t)l_iY * l_oL.m_iW + l_iX) * 3];
            float l_arrB[9];
            f_ShBasis(l_v3D, l_arrB);
            for (int l_iI = 0; l_iI < 9; l_iI++) l_arrSh[l_iI] += glm::vec3(l_pP[0], l_pP[1], l_pP[2]) * (l_arrB[l_iI] * l_fDOmega);
        }
    }
    const float l_arrBand[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    for (int l_iI = 0; l_iI < 9; l_iI++) l_oEnv.m_arrSh[l_iI] = l_arrSh[l_iI] * l_arrBand[l_iI];
    return l_oEnv;
}

}  // namespace toms::gltf
