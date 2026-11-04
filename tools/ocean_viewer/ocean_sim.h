// ocean_sim.h -- ocean waves on the CPU, no GPU and no window (docs/engine/21_OCEAN_VIEWER.md): Tessendorf's "Simulating
// Ocean Water" (1999) with a JONSWAP spectrum and a cos^2s spread (Horvath 2015). One Cascade is a square patch of sea
// that repeats; a few cascades of different sizes, each with its own band of wave lengths, hide the repeat.
//
// Each step: the spectrum is moved to the time asked for, inverse FFTs give the fields, and the results land in two
// RGBA float pictures of N x N: displacement (x, height, z, -) and slope (dh/dx, dh/dz, foam, -).
#pragma once
#include <cstdint>
#include <vector>

namespace toms::ocean {

struct Cx {
    float m_fR = 0.0f, m_fI = 0.0f;
};

// An inverse FFT without scaling, h(x) = sum of H(k) e^(ikx). The size must be a power of two.
class Fft {
public:
    explicit Fft(int l_iN = 0);
    void f_Inverse(Cx* l_pData) const;   // in place
    int f_N() const { return m_iN; }

private:
    int m_iN = 0;
    std::vector<int> m_vReversed;
    std::vector<Cx> m_vTwiddles;
};

struct WaveSettings {
    float m_fWindSpeed = 14.0f;     // m/s, 10 m above the sea
    float m_fWindDirDeg = 35.0f;    // the way the waves go, degrees from +X toward +Z
    float m_fFetchKm = 300.0f;      // how far the wind has blown over open water: longer = bigger waves
    float m_fSpread = 10.0f;        // s of cos^2s: higher = the waves keep closer to the wind's direction
    float m_fPeak = 3.3f;           // JONSWAP's gamma: higher = a narrower, more regular swell
    float m_fChoppy = 1.0f;         // sideways motion: sharp crests, wide troughs; too much folds the surface
    float m_fFoamStart = 0.8f;      // foam where the surface's Jacobian falls under this (1 = flat, 0 = folding)
    float m_fFoamFadeSeconds = 2.5f;
};

// S(w): the JONSWAP energy at angular frequency w (m^2 s). Public for the test, which integrates it.
float f_Jonswap(float l_fOmega, const WaveSettings& l_oW);

class Cascade {
public:
    // N: a power of two. l_fSize: the patch's side, metres. Only waves of l_fKMin <= |k| < l_fKMax are in this patch.
    void f_Init(int l_iN, float l_fSize, float l_fKMin, float l_fKMax, const WaveSettings& l_oW, uint32_t l_uSeed);
    // The sea at this time. l_fDt only ages the foam (0 = paused). Runs on the job system.
    void f_Step(double l_dTime, float l_fDt);

    int f_N() const { return m_iN; }
    float f_Size() const { return m_fSize; }
    const std::vector<float>& f_Displacement() const { return m_vDisp; }   // 4 floats a texel: dx, height, dz, 0
    const std::vector<float>& f_Slope() const { return m_vSlope; }         // 4 floats a texel: dh/dx, dh/dz, foam, 0
    bool f_Empty() const { return m_iN == 0; }

private:
    int m_iN = 0;
    float m_fSize = 0.0f, m_fChoppy = 1.0f, m_fFoamStart = 0.5f, m_fFoamFade = 2.5f;
    Fft m_oFft;
    std::vector<Cx> m_vH0;              // the starting spectrum h0(k)
    std::vector<float> m_vOmega, m_vKx, m_vKz, m_vKInv;
    std::vector<Cx> m_arrPack[4];        // four FFTs, each carrying two real fields (see ocean_sim.cpp)
    std::vector<float> m_vDisp, m_vSlope, m_vFoam;
};

}  // namespace toms::ocean
