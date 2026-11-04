// ocean_renderer.h -- draws the sea of ocean_sim.h and a sky with bgfx (docs/engine/21_OCEAN_VIEWER.md). Holds up to three
// cascades, uploads their pictures every frame, and draws into one bgfx view: the water on a screen-space grid, then the
// sky behind it. The sky and the water's shading are in shaders/.
#pragma once
#include "ocean_sim.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
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

// What each quality costs. In order: the sim's patches and their size (CPU, and the web has one thread), the grid's rows
// (vertices), noise octaves per pixel in the sky, in the water's reflections (0: no clouds there) and in the foam (GPU),
// and how often the waves are simulated.
struct Quality {
    const char* m_pszName;
    int m_iCascades, m_iResolution, m_iGridRows, m_iSkyOctaves, m_iReflectOctaves, m_iFoamOctaves, m_iSimEvery;
};
inline constexpr Quality g_arrQualities[] = {
    {"high", 3, 256, 360, 5, 3, 3, 1},
    {"medium", 3, 128, 220, 4, 2, 3, 1},
    {"low", 2, 128, 140, 3, 0, 1, 2},
    {"lowest", 2, 64, 90, 2, 0, 1, 2},
};
inline constexpr int g_iQualities = (int)(sizeof(g_arrQualities) / sizeof(g_arrQualities[0]));

// The patches of a quality, at these sizes (metres).
std::vector<CascadeSettings> f_QualityCascades(const Quality& l_oQ, float l_fSize0 = 700.0f, float l_fSize1 = 110.0f, float l_fSize2 = 19.0f);

// Where the camera is: low over the water, with a slow bob, as from a boat.
struct BoatCamera {
    float m_fHeight = 10.0f, m_fPitchDeg = -3.0f, m_fYawDeg = 0.0f, m_fFovYDeg = 50.0f, m_fSway = 0.5f;
};
// Fills the look's eye, view, projection and field of view (bgfx must be up: the depth range is the backend's).
void f_PlaceBoatCamera(Look& l_oLook, const BoatCamera& l_oCamera, double l_dTime, uint32_t l_uWidth, uint32_t l_uHeight);

class OceanRenderer {
public:
    static constexpr int m_iMaxCascades = 3;

    OceanRenderer();
    ~OceanRenderer();
    bool f_Init(std::string& l_strError);    // programs, uniforms, the grid (bgfx must be up)
    void f_Shutdown();

    // (Re)makes the cascades; each takes the band of waves its size can show best, so none is counted twice.
    void f_SetWaves(const WaveSettings& l_oW, const std::vector<CascadeSettings>& l_vCascades);
    void f_SetGrid(int l_iColumns, int l_iRows);
    // The columns follow the screen's shape, so the cells are about square on it. Nothing happens if nothing changed.
    void f_SetGridForScreen(int l_iRows, uint32_t l_uWidth, uint32_t l_uHeight);
    void f_Step(double l_dTime, float l_fDt);   // simulate and upload
    void f_Draw(uint16_t l_u16View, const Look& l_oLook, double l_dTime, uint32_t l_uWidth, uint32_t l_uHeight);

    float f_SimMs() const;        // the last step's waves on the CPU
    float f_UploadMs() const;     // its mip pictures and half floats
    int f_Triangles() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_upImpl;
};

}  // namespace toms::ocean
