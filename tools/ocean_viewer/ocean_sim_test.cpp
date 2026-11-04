// ocean_sim_test.cpp -- the ocean's waves without a GPU (ocean_sim.h): the FFT, real fields, the energy of the spectrum,
// slopes that match the heights, repeatable runs, foam in range. Run: ocean_sim_test
#include "ocean_sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace toms::ocean;

namespace {

int g_iChecks = 0, g_iFailed = 0;

void f_Check(bool l_bOk, const char* l_pszWhat, double l_dValue = 0.0) {
    g_iChecks++;
    if (l_bOk) return;
    g_iFailed++;
    std::printf("FAILED: %s (%g)\n", l_pszWhat, l_dValue);
}

constexpr float g_fPi = 3.14159265f;

void f_TestFft() {
    const int l_iN = 16;
    Fft l_oFft(l_iN);
    // One frequency, k = 3: e^(i 2 pi 3 x / N).
    std::vector<Cx> l_vData((size_t)l_iN);
    l_vData[3] = {1.0f, 0.0f};
    l_oFft.f_Inverse(l_vData.data());
    double l_dWorst = 0.0;
    for (int l_iX = 0; l_iX < l_iN; l_iX++) {
        const float l_fA = 2.0f * g_fPi * 3.0f * l_iX / l_iN;
        l_dWorst = std::max({l_dWorst, (double)std::fabs(l_vData[(size_t)l_iX].m_fR - std::cos(l_fA)), (double)std::fabs(l_vData[(size_t)l_iX].m_fI - std::sin(l_fA))});
    }
    f_Check(l_dWorst < 1e-5, "the FFT of one frequency is that wave", l_dWorst);
}

// The mean of the squared heights: the integral of the spectrum, up to what the patch can hold.
void f_TestEnergy() {
    WaveSettings l_oW;
    Cascade l_oC;
    const int l_iN = 256;
    const float l_fSize = 1200.0f;
    l_oC.f_Init(l_iN, l_fSize, 0.0f, 1e9f, l_oW, 7u);
    double l_dVar = 0.0, l_dMean = 0.0;
    int l_iSamples = 0;
    for (double l_dT : {0.0, 11.0, 37.0, 91.0}) {
        l_oC.f_Step(l_dT, 0.0f);
        const std::vector<float>& l_vD = l_oC.f_Displacement();
        for (size_t l_nI = 0; l_nI + 3 < l_vD.size(); l_nI += 4) {
            l_dVar += (double)l_vD[l_nI + 1] * l_vD[l_nI + 1];
            l_dMean += l_vD[l_nI + 1];
            l_iSamples++;
        }
    }
    l_dVar /= l_iSamples;
    l_dMean /= l_iSamples;
    // The spectrum's own integral, over what this grid can carry (k from 2 pi / L to pi N / L).
    const float l_fK0 = 2.0f * g_fPi / l_fSize, l_fK1 = g_fPi * l_iN / l_fSize;
    const float l_fW0 = std::sqrt(9.81f * l_fK0), l_fW1 = std::sqrt(9.81f * l_fK1);
    double l_dIntegral = 0.0;
    const int l_iSteps = 4000;
    for (int l_iI = 0; l_iI < l_iSteps; l_iI++) {
        const float l_fW = l_fW0 + (l_fW1 - l_fW0) * (l_iI + 0.5f) / l_iSteps;
        l_dIntegral += f_Jonswap(l_fW, l_oW) * (l_fW1 - l_fW0) / l_iSteps;
    }
    const double l_dRatio = l_dVar / l_dIntegral;
    std::printf("height variance %.3f m^2, the spectrum's integral %.3f m^2 (ratio %.2f); significant height %.2f m\n", l_dVar, l_dIntegral, l_dRatio, 4.0 * std::sqrt(l_dVar));
    f_Check(l_dRatio > 0.6 && l_dRatio < 1.4, "the height's variance matches the spectrum", l_dRatio);
    f_Check(std::fabs(l_dMean) < 0.05, "the sea's mean height is zero", l_dMean);
    const double l_dHs = 4.0 * std::sqrt(l_dVar);
    f_Check(l_dHs > 1.0 && l_dHs < 8.0, "14 m/s of wind makes believable waves", l_dHs);
}

// A real field: a texture from the imaginary part would show as a wrong slope; compare the slope with the height itself.
void f_TestSlopes() {
    WaveSettings l_oW;
    l_oW.m_fChoppy = 0.0f;   // straight up and down: the slope is the height's own gradient
    Cascade l_oC;
    const int l_iN = 256;
    const float l_fSize = 400.0f;
    l_oC.f_Init(l_iN, l_fSize, 0.0f, 0.3f, l_oW, 3u);   // long waves only: a finite difference can follow them
    l_oC.f_Step(5.0, 0.0f);
    const std::vector<float>& l_vD = l_oC.f_Displacement();
    const std::vector<float>& l_vS = l_oC.f_Slope();
    const float l_fCell = l_fSize / l_iN;
    double l_dErrX = 0.0, l_dErrZ = 0.0, l_dSum = 0.0;
    auto l_fnH = [&](int l_iX, int l_iZ) { return l_vD[((size_t)((l_iZ + l_iN) % l_iN) * l_iN + (l_iX + l_iN) % l_iN) * 4 + 1]; };
    for (int l_iZ = 0; l_iZ < l_iN; l_iZ++) {
        for (int l_iX = 0; l_iX < l_iN; l_iX++) {
            const float l_fDx = (l_fnH(l_iX + 1, l_iZ) - l_fnH(l_iX - 1, l_iZ)) / (2.0f * l_fCell);
            const float l_fDz = (l_fnH(l_iX, l_iZ + 1) - l_fnH(l_iX, l_iZ - 1)) / (2.0f * l_fCell);
            const size_t l_nI = ((size_t)l_iZ * l_iN + l_iX) * 4;
            l_dErrX += std::fabs(l_vS[l_nI] - l_fDx);
            l_dErrZ += std::fabs(l_vS[l_nI + 1] - l_fDz);
            l_dSum += std::fabs(l_fDx) + std::fabs(l_fDz);
        }
    }
    const double l_dRel = (l_dErrX + l_dErrZ) / std::max(l_dSum, 1e-6);
    f_Check(l_dRel < 0.03, "the slopes are the height's gradient", l_dRel);
}

void f_TestRepeatable() {
    WaveSettings l_oW;
    Cascade l_oA, l_oB;
    l_oA.f_Init(64, 100.0f, 0.0f, 1e9f, l_oW, 11u);
    l_oB.f_Init(64, 100.0f, 0.0f, 1e9f, l_oW, 11u);
    for (int l_iI = 0; l_iI < 5; l_iI++) {
        l_oA.f_Step(l_iI * 0.033, 0.033f);
        l_oB.f_Step(l_iI * 0.033, 0.033f);
    }
    f_Check(l_oA.f_Displacement() == l_oB.f_Displacement() && l_oA.f_Slope() == l_oB.f_Slope(), "the same seed and times give the same sea");
    const std::vector<float> l_vBefore = l_oA.f_Displacement();
    l_oA.f_Step(7.0, 0.0f);
    f_Check(l_vBefore != l_oA.f_Displacement(), "the sea moves with time");
}

void f_TestFoam() {
    WaveSettings l_oW;
    l_oW.m_fWindSpeed = 25.0f;
    l_oW.m_fChoppy = 1.5f;
    Cascade l_oC;
    l_oC.f_Init(128, 200.0f, 0.0f, 1e9f, l_oW, 5u);
    float l_fMax = 0.0f, l_fMin = 1.0f;
    for (int l_iI = 0; l_iI < 30; l_iI++) {
        l_oC.f_Step(l_iI * 0.1, 0.1f);
        const std::vector<float>& l_vS = l_oC.f_Slope();
        for (size_t l_nI = 2; l_nI < l_vS.size(); l_nI += 4) {
            l_fMax = std::max(l_fMax, l_vS[l_nI]);
            l_fMin = std::min(l_fMin, l_vS[l_nI]);
        }
    }
    f_Check(l_fMin >= 0.0f && l_fMax <= 1.0f, "foam stays in 0..1", l_fMax);
    f_Check(l_fMax > 0.2f, "a storm makes foam", l_fMax);
    // Calm water: nothing folds.
    WaveSettings l_oCalm;
    l_oCalm.m_fWindSpeed = 2.0f;
    l_oCalm.m_fFetchKm = 5.0f;
    Cascade l_oD;
    l_oD.f_Init(128, 200.0f, 0.0f, 1e9f, l_oCalm, 5u);
    l_oD.f_Step(3.0, 0.1f);
    float l_fCalmMax = 0.0f;
    for (size_t l_nI = 2; l_nI < l_oD.f_Slope().size(); l_nI += 4) l_fCalmMax = std::max(l_fCalmMax, l_oD.f_Slope()[l_nI]);
    f_Check(l_fCalmMax == 0.0f, "calm water has no foam", l_fCalmMax);
}

}  // namespace

int main() {
    f_TestFft();
    f_TestEnergy();
    f_TestSlopes();
    f_TestRepeatable();
    f_TestFoam();
    std::printf("ocean_sim_test: %d checks, %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed == 0 ? 0 : 1;
}
