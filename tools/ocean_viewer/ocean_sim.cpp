// ocean_sim.cpp -- see ocean_sim.h.
#include "ocean_sim.h"

#include "job_system.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace toms::ocean {
namespace {

constexpr float g_fGravity = 9.81f;
constexpr double g_dTwoPi = 6.283185307179586;

Cx f_Mul(const Cx& l_oA, const Cx& l_oB) {
    return {l_oA.m_fR * l_oB.m_fR - l_oA.m_fI * l_oB.m_fI, l_oA.m_fR * l_oB.m_fI + l_oA.m_fI * l_oB.m_fR};
}

}  // namespace

Fft::Fft(int l_iN) : m_iN(l_iN) {
    if (l_iN <= 0) return;
    int l_iBits = 0;
    while ((1 << l_iBits) < l_iN) l_iBits++;
    m_vReversed.resize((size_t)l_iN);
    for (int l_iI = 0; l_iI < l_iN; l_iI++) {
        int l_iR = 0;
        for (int l_iB = 0; l_iB < l_iBits; l_iB++)
            if (l_iI & (1 << l_iB)) l_iR |= 1 << (l_iBits - 1 - l_iB);
        m_vReversed[(size_t)l_iI] = l_iR;
    }
    m_vTwiddles.resize((size_t)l_iN / 2);
    for (int l_iI = 0; l_iI < l_iN / 2; l_iI++) {
        const double l_dA = g_dTwoPi * l_iI / l_iN;
        m_vTwiddles[(size_t)l_iI] = {(float)std::cos(l_dA), (float)std::sin(l_dA)};
    }
}

void Fft::f_Inverse(Cx* l_pData) const {
    for (int l_iI = 0; l_iI < m_iN; l_iI++) {
        const int l_iR = m_vReversed[(size_t)l_iI];
        if (l_iI < l_iR) std::swap(l_pData[l_iI], l_pData[l_iR]);
    }
    for (int l_iSize = 2; l_iSize <= m_iN; l_iSize *= 2) {
        const int l_iHalf = l_iSize / 2, l_iStep = m_iN / l_iSize;
        for (int l_iStart = 0; l_iStart < m_iN; l_iStart += l_iSize) {
            for (int l_iJ = 0; l_iJ < l_iHalf; l_iJ++) {
                Cx& l_oA = l_pData[l_iStart + l_iJ];
                Cx& l_oB = l_pData[l_iStart + l_iJ + l_iHalf];
                const Cx l_oT = f_Mul(l_oB, m_vTwiddles[(size_t)(l_iJ * l_iStep)]);
                l_oB = {l_oA.m_fR - l_oT.m_fR, l_oA.m_fI - l_oT.m_fI};
                l_oA = {l_oA.m_fR + l_oT.m_fR, l_oA.m_fI + l_oT.m_fI};
            }
        }
    }
}

float f_Jonswap(float l_fOmega, const WaveSettings& l_oW) {
    if (l_fOmega <= 0.0f) return 0.0f;
    const float l_fU = std::max(l_oW.m_fWindSpeed, 0.1f), l_fF = std::max(l_oW.m_fFetchKm, 0.1f) * 1000.0f;
    const float l_fG = g_fGravity;
    const float l_fAlpha = 0.076f * std::pow(l_fU * l_fU / (l_fF * l_fG), 0.22f);
    const float l_fPeak = 22.0f * std::pow(l_fG * l_fG / (l_fU * l_fF), 1.0f / 3.0f);
    const float l_fSigma = l_fOmega <= l_fPeak ? 0.07f : 0.09f;
    const float l_fD = (l_fOmega - l_fPeak) / (l_fSigma * l_fPeak);
    const float l_fR = std::exp(-0.5f * l_fD * l_fD);
    const float l_fX = l_fPeak / l_fOmega;
    return l_fAlpha * l_fG * l_fG / std::pow(l_fOmega, 5.0f) * std::exp(-1.25f * l_fX * l_fX * l_fX * l_fX) * std::pow(std::max(l_oW.m_fPeak, 1.0f), l_fR);
}

void Cascade::f_Init(int l_iN, float l_fSize, float l_fKMin, float l_fKMax, const WaveSettings& l_oW, uint32_t l_uSeed) {
    m_iN = l_iN;
    m_fSize = l_fSize;
    m_fChoppy = l_oW.m_fChoppy;
    m_fFoamStart = l_oW.m_fFoamStart;
    m_fFoamFade = std::max(l_oW.m_fFoamFadeSeconds, 0.01f);
    if (m_oFft.f_N() != l_iN) m_oFft = Fft(l_iN);
    const size_t l_nTexels = (size_t)l_iN * l_iN;
    m_vH0.assign(l_nTexels, Cx{});
    m_vOmega.assign(l_nTexels, 0.0f);
    m_vKx.assign(l_nTexels, 0.0f);
    m_vKz.assign(l_nTexels, 0.0f);
    m_vKInv.assign(l_nTexels, 0.0f);
    for (auto& l_vP : m_arrPack) l_vP.assign(l_nTexels, Cx{});
    m_vDisp.assign(l_nTexels * 4, 0.0f);
    m_vSlope.assign(l_nTexels * 4, 0.0f);
    m_vFoam.assign(l_nTexels, 0.0f);

    const float l_fDk = (float)g_dTwoPi / l_fSize;
    const float l_fWind = l_oW.m_fWindDirDeg * (float)g_dTwoPi / 360.0f;
    // The cos^2s spread's scale, so it integrates to 1 over the circle.
    const float l_fS = std::max(l_oW.m_fSpread, 0.5f);
    const float l_fQ = std::exp(std::lgamma(l_fS + 1.0f) - std::lgamma(l_fS + 0.5f)) / (2.0f * std::sqrt((float)g_dTwoPi * 0.5f));
    std::mt19937 l_oRng(l_uSeed);
    std::normal_distribution<float> l_oGauss(0.0f, 1.0f);
    for (int l_iZ = 0; l_iZ < l_iN; l_iZ++) {
        for (int l_iX = 0; l_iX < l_iN; l_iX++) {
            const size_t l_nI = (size_t)l_iZ * l_iN + l_iX;
            const float l_fRe = l_oGauss(l_oRng), l_fIm = l_oGauss(l_oRng);   // drawn for every texel, so a band change keeps the rest
            // The frequency of each texel: 0, 1, ... N/2 - 1, -N/2, ... -1. No centring, so no sign flips after the FFT.
            const int l_iNx = l_iX < l_iN / 2 ? l_iX : l_iX - l_iN, l_iNz = l_iZ < l_iN / 2 ? l_iZ : l_iZ - l_iN;
            const float l_fKx = l_iNx * l_fDk, l_fKz = l_iNz * l_fDk;
            const float l_fK = std::sqrt(l_fKx * l_fKx + l_fKz * l_fKz);
            m_vKx[l_nI] = l_fKx;
            m_vKz[l_nI] = l_fKz;
            if (l_fK <= 0.0f) continue;
            m_vKInv[l_nI] = 1.0f / l_fK;
            const float l_fOmega = std::sqrt(g_fGravity * l_fK);
            m_vOmega[l_nI] = l_fOmega;
            // The Nyquist row and column have no matching -k: leave them out, and the fields stay real.
            if (l_iNx == -l_iN / 2 || l_iNz == -l_iN / 2 || l_fK < l_fKMin || l_fK >= l_fKMax) continue;
            const float l_fHalfAngle = 0.5f * (std::atan2(l_fKz, l_fKx) - l_fWind);
            const float l_fSpread = l_fQ * std::pow(std::fabs(std::cos(l_fHalfAngle)), 2.0f * l_fS);
            const float l_fDwDk = g_fGravity / (2.0f * l_fOmega);
            // |h0|^2 = 1/2 S(w) D(theta) dw/dk / k dk^2. A real field gets each wave from k and from -k, so with the 1/2
            // the variance of the height is the integral of S (the test checks it).
            const float l_fEnergy = f_Jonswap(l_fOmega, l_oW) * l_fSpread * l_fDwDk * m_vKInv[l_nI] * l_fDk * l_fDk * 0.5f;
            const float l_fAmp = std::sqrt(std::max(l_fEnergy, 0.0f)) * 0.70710678f;   // E|xi|^2 = 2 for two unit Gaussians
            m_vH0[l_nI] = {l_fRe * l_fAmp, l_fIm * l_fAmp};
        }
    }
}

// Two real fields fit one complex FFT: the spectrum A + iB comes back as a(x) + i b(x). The eight fields, in four packs:
//   0: height, dx    1: dz, dh/dx    2: dh/dz, d(dx)/dx    3: d(dz)/dz, d(dx)/dz
void Cascade::f_Step(double l_dTime, float l_fDt) {
    const int l_iN = m_iN;
    if (l_iN == 0) return;
    JobSystem::f_ParallelFor(l_iN, [&](int l_iZ) {
        const int l_iMz = (l_iN - l_iZ) % l_iN;
        for (int l_iX = 0; l_iX < l_iN; l_iX++) {
            const size_t l_nI = (size_t)l_iZ * l_iN + l_iX;
            const int l_iMx = (l_iN - l_iX) % l_iN;
            const Cx& l_oH0 = m_vH0[l_nI];
            const Cx& l_oM = m_vH0[(size_t)l_iMz * l_iN + l_iMx];
            const double l_dPhase = std::fmod((double)m_vOmega[l_nI] * l_dTime, g_dTwoPi);
            const float l_fC = (float)std::cos(l_dPhase), l_fS = (float)std::sin(l_dPhase);
            // H = h0(k) e^(iwt) + conj(h0(-k)) e^(-iwt)
            const float l_fHr = l_oH0.m_fR * l_fC - l_oH0.m_fI * l_fS + l_oM.m_fR * l_fC - l_oM.m_fI * l_fS;
            const float l_fHi = l_oH0.m_fR * l_fS + l_oH0.m_fI * l_fC - l_oM.m_fR * l_fS - l_oM.m_fI * l_fC;
            const float l_fKx = m_vKx[l_nI], l_fKz = m_vKz[l_nI], l_fInv = m_vKInv[l_nI];
            const float l_fUx = l_fKx * l_fInv, l_fUz = l_fKz * l_fInv;
            // A + iB with A = a + ib, B = c + id is (a - d) + i(b + c).
            auto l_fnPack = [&](std::vector<Cx>& l_vP, float l_fAr, float l_fAi, float l_fBr, float l_fBi) {
                l_vP[l_nI] = {l_fAr - l_fBi, l_fAi + l_fBr};
            };
            // dx = -i (kx/k) H: (kx/k Hi, -kx/k Hr). dh/dx = i kx H: (-kx Hi, kx Hr). d(dx)/dx = kx^2/k H.
            l_fnPack(m_arrPack[0], l_fHr, l_fHi, l_fUx * l_fHi, -l_fUx * l_fHr);
            l_fnPack(m_arrPack[1], l_fUz * l_fHi, -l_fUz * l_fHr, -l_fKx * l_fHi, l_fKx * l_fHr);
            l_fnPack(m_arrPack[2], -l_fKz * l_fHi, l_fKz * l_fHr, l_fKx * l_fUx * l_fHr, l_fKx * l_fUx * l_fHi);
            l_fnPack(m_arrPack[3], l_fKz * l_fUz * l_fHr, l_fKz * l_fUz * l_fHi, l_fKx * l_fUz * l_fHr, l_fKx * l_fUz * l_fHi);
        }
    });
    JobSystem::f_ParallelFor(l_iN * 4, [&](int l_iJob) {
        m_oFft.f_Inverse(m_arrPack[l_iJob / l_iN].data() + (size_t)(l_iJob % l_iN) * l_iN);
    });
    JobSystem::f_ParallelFor(l_iN * 4, [&](int l_iJob) {
        thread_local std::vector<Cx> l_vColumn;
        l_vColumn.resize((size_t)l_iN);
        Cx* l_pData = m_arrPack[l_iJob / l_iN].data();
        const int l_iX = l_iJob % l_iN;
        for (int l_iZ = 0; l_iZ < l_iN; l_iZ++) l_vColumn[(size_t)l_iZ] = l_pData[(size_t)l_iZ * l_iN + l_iX];
        m_oFft.f_Inverse(l_vColumn.data());
        for (int l_iZ = 0; l_iZ < l_iN; l_iZ++) l_pData[(size_t)l_iZ * l_iN + l_iX] = l_vColumn[(size_t)l_iZ];
    });
    const float l_fFade = l_fDt > 0.0f ? std::exp(-l_fDt / m_fFoamFade) : 1.0f;
    const float l_fChop = m_fChoppy;
    JobSystem::f_ParallelFor(l_iN, [&](int l_iZ) {
        for (int l_iX = 0; l_iX < l_iN; l_iX++) {
            const size_t l_nI = (size_t)l_iZ * l_iN + l_iX;
            const Cx& l_oP0 = m_arrPack[0][l_nI];
            const Cx& l_oP1 = m_arrPack[1][l_nI];
            const Cx& l_oP2 = m_arrPack[2][l_nI];
            const Cx& l_oP3 = m_arrPack[3][l_nI];
            const float l_fJxx = 1.0f + l_fChop * l_oP2.m_fI, l_fJzz = 1.0f + l_fChop * l_oP3.m_fR, l_fJxz = l_fChop * l_oP3.m_fI;
            const float l_fJacobian = l_fJxx * l_fJzz - l_fJxz * l_fJxz;
            float* l_pD = m_vDisp.data() + l_nI * 4;
            l_pD[0] = l_fChop * l_oP0.m_fI;
            l_pD[1] = l_oP0.m_fR;
            l_pD[2] = l_fChop * l_oP1.m_fR;
            l_pD[3] = 0.0f;
            // The slope on the moved surface: squeezed crests are steeper (kept finite where it folds).
            float* l_pS = m_vSlope.data() + l_nI * 4;
            l_pS[0] = l_oP1.m_fI / std::max(l_fJxx, 0.25f);
            l_pS[1] = l_oP2.m_fR / std::max(l_fJzz, 0.25f);
            const float l_fNew = std::clamp((m_fFoamStart - l_fJacobian) * 2.0f, 0.0f, 1.0f);
            const float l_fFoam = std::max(m_vFoam[l_nI] * l_fFade, l_fNew);
            m_vFoam[l_nI] = l_fFoam;
            l_pS[2] = l_fFoam;
            l_pS[3] = 0.0f;
        }
    });
}

}  // namespace toms::ocean
