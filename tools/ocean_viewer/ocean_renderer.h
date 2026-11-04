// ocean_renderer.h -- draws the sea of ocean_sim.h and a sky with bgfx (docs/engine/21_OCEAN_VIEWER.md). Holds up to three
// cascades, uploads their pictures every frame, and draws into one bgfx view: the water on a screen-space grid, then the
// sky behind it. The sky and the water's shading are in shaders/.
#pragma once
#include "ocean_sim.h"

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace toms::ocean {

struct CascadeSettings {
    int m_iN = 256;
    float m_fSize = 600.0f;   // the patch's side, metres
};

struct Look {
    glm::vec3 m_v3Eye{0.0f, 12.0f, 0.0f};
    glm::mat4 m_mat4View{1.0f}, m_mat4Proj{1.0f};
    float m_fFovYDegrees = 45.0f;
    float m_fSunElevationDeg = 36.0f, m_fSunAzimuthDeg = 38.0f;
    float m_fTurbidity = 2.2f, m_fSkyStrength = 1.0f, m_fExposure = 0.8f;
    float m_fCloudCover = 0.42f, m_fCloudScale = 1.5f, m_fCloudSpeed = 0.012f, m_fCloudDark = 0.45f;
    glm::vec3 m_v3Deep{0.004f, 0.05f, 0.12f};
    glm::vec3 m_v3Scatter{0.02f, 0.34f, 0.32f};
    float m_fScatterStrength = 0.45f, m_fRoughness = 0.12f, m_fFoam = 1.0f, m_fReflection = 1.0f;
    float m_fFogMetres = 9000.0f;
    float m_fWaveScale = 1.0f;     // scales the waves as drawn: a quick way to try bigger or flatter seas
    int m_iSkyOctaves = 5, m_iReflectOctaves = 3, m_iFoamOctaves = 3;   // noise per pixel: the costliest part of a pixel
    bool m_bGammaOut = false;      // no sRGB screen (the web): the shaders encode
};

class OceanRenderer {
public:
    static constexpr int m_iMaxCascades = 3;

    bool f_Init(std::string& l_strError);    // programs, uniforms, the grid (bgfx must be up)
    void f_Shutdown();

    // (Re)makes the cascades; each takes the band of waves its size can show best, so none is counted twice.
    void f_SetWaves(const WaveSettings& l_oW, const std::vector<CascadeSettings>& l_vCascades);
    void f_SetGrid(int l_iColumns, int l_iRows);
    void f_Step(double l_dTime, float l_fDt);   // simulate and upload
    void f_Draw(uint16_t l_u16View, const Look& l_oLook, double l_dTime, uint32_t l_uWidth, uint32_t l_uHeight);

    float f_SimMs() const { return m_fSimMs; }        // the last step's waves on the CPU
    float f_UploadMs() const { return m_fUploadMs; }  // its mip pictures and half floats
    int f_Triangles() const { return m_iColumns * m_iRows * 2; }

private:
    struct Gpu {
        Cascade m_oSim;
        bgfx::TextureHandle m_oDisp = BGFX_INVALID_HANDLE, m_oSlope = BGFX_INVALID_HANDLE;
        std::vector<std::vector<uint16_t>> m_vDispMips, m_vSlopeMips;   // RGBA16F, ready to upload
        std::vector<std::vector<float>> m_vDispWork, m_vSlopeWork;      // the smaller pictures as floats
    };
    void f_FreeCascade(Gpu& l_oG);
    void f_FreeGrid();

    std::array<Gpu, m_iMaxCascades> m_arrCascades;
    int m_iCascades = 0;
    bgfx::ProgramHandle m_oWater = BGFX_INVALID_HANDLE, m_oSky = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_oGridVb = BGFX_INVALID_HANDLE, m_oSkyVb = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_oGridIb = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_oFlat = BGFX_INVALID_HANDLE;   // stands in for a cascade not used
    int m_iColumns = 0, m_iRows = 0;
    std::array<bgfx::UniformHandle, 3> m_arrSDisp{}, m_arrSSlope{};
    bgfx::UniformHandle m_oUInvVP = BGFX_INVALID_HANDLE, m_oUEye = BGFX_INVALID_HANDLE, m_oUGrid = BGFX_INVALID_HANDLE, m_oUGridB = BGFX_INVALID_HANDLE,
                        m_oUInvSize = BGFX_INVALID_HANDLE, m_oUTexels = BGFX_INVALID_HANDLE, m_oUSun = BGFX_INVALID_HANDLE, m_oUSunColor = BGFX_INVALID_HANDLE,
                        m_oUPerezY = BGFX_INVALID_HANDLE, m_oUPerezX = BGFX_INVALID_HANDLE, m_oUPerezZ = BGFX_INVALID_HANDLE, m_oUPerezE = BGFX_INVALID_HANDLE,
                        m_oUZenith = BGFX_INVALID_HANDLE, m_oUClouds = BGFX_INVALID_HANDLE, m_oUDeep = BGFX_INVALID_HANDLE, m_oUScatter = BGFX_INVALID_HANDLE,
                        m_oUFoam = BGFX_INVALID_HANDLE, m_oUDetail = BGFX_INVALID_HANDLE;
    float m_fSimMs = 0.0f, m_fUploadMs = 0.0f;
};

}  // namespace toms::ocean
