// title_scene.cpp -- see title_scene.h.
#include "title_scene.h"
#include "color_space.h"   // TOMS_GAMMA_SPACE: web and Android blend in gamma space

#include "vfs.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace toms::next {
namespace {

constexpr double g_dSecondsPerCamera = 10.0;

std::string f_DirOf(const std::string& l_strPath) {
    const size_t l_nS = l_strPath.find_last_of("/\\");
    return l_nS == std::string::npos ? std::string(".") : l_strPath.substr(0, l_nS);
}

// A camera's view with its horizon level: same place, same direction, world up (no roll).
glm::mat4 f_LevelView(const glm::mat4& l_mat4World) {
    const glm::vec3 l_v3Eye(l_mat4World[3]), l_v3Fwd = -glm::normalize(glm::vec3(l_mat4World[2]));
    if (std::fabs(l_v3Fwd.y) > 0.98f) return glm::inverse(l_mat4World);   // straight up / down: no horizon to level
    return glm::lookAt(l_v3Eye, l_v3Eye + l_v3Fwd, glm::vec3(0, 1, 0));
}

}  // namespace

bool TitleScene::f_Load(const std::string& l_strPath, std::string& l_strError) {
    std::string l_strBytes;
    if (!toms::f_VfsReadAll(l_strPath, l_strBytes)) {
        l_strError = "cannot read " + l_strPath;
        return false;
    }
    if (!gltf::f_LoadModelFromMemory(l_strBytes.data(), l_strBytes.size(), f_DirOf(l_strPath), m_oModel, &l_strError)) return false;
    if (!m_oRenderer.f_Init()) {
        l_strError = "no mesh shaders for this backend";
        return false;
    }
    m_oPose.f_Bind(&m_oModel);
    if (!m_oModel.m_vAnimations.empty()) m_oPose.f_Apply(0, 0.0f);
    m_oPose.f_UpdateWorld();
    if (!m_oPose.f_Bounds(m_v3BoundsMin, m_v3BoundsMax)) m_v3BoundsMin = glm::vec3(-1.0f), m_v3BoundsMax = glm::vec3(1.0f);
    const std::vector<gltf::PlacedCamera> l_vCams = m_oPose.f_Cameras();
    for (size_t l_nI = 0; l_nI < l_vCams.size(); l_nI++)
        if (m_oModel.m_vCameras[(size_t)l_vCams[l_nI].m_iCamera].m_bPerspective) m_vCameras.push_back((int)l_nI);
    m_spGpu = m_oRenderer.f_Upload(m_oModel);
    std::fprintf(stderr, "[title] scene %s: %zu triangles, %zu of %zu cameras used\n", l_strPath.c_str(), m_oModel.f_TriangleCount(),
                 m_vCameras.size(), m_oModel.m_vCameras.size());
    return true;
}

bool TitleScene::f_LoadOcean(std::string& l_strError) {
    auto l_upOcean = std::make_unique<ocean::OceanRenderer>();
    if (!l_upOcean->f_Init(l_strError)) {
        l_upOcean->f_Shutdown();
        return false;
    }
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    const ocean::Quality& l_oQ = ocean::g_arrQualities[2];   // "low": one thread on the web, a phone's GPU
#else
    const ocean::Quality& l_oQ = ocean::g_arrQualities[0];   // "high"
#endif
    m_oOceanLook.m_iSkyOctaves = l_oQ.m_iSkyOctaves;
    m_oOceanLook.m_iReflectOctaves = l_oQ.m_iReflectOctaves;
    m_oOceanLook.m_iFoamOctaves = l_oQ.m_iFoamOctaves;
    m_oOceanLook.m_bGammaOut = TOMS_GAMMA_SPACE != 0;
    // Dark, with few clouds: the title's pale gold letters vanish against a white cloud.
    m_oOceanLook.m_fExposure = 0.4f;
    m_oOceanLook.m_fCloudCover = 0.28f;
    m_iOceanRows = l_oQ.m_iGridRows;
    m_iOceanSimEvery = l_oQ.m_iSimEvery;
    l_upOcean->f_SetWaves(ocean::WaveSettings(), ocean::f_QualityCascades(l_oQ));
    m_upOcean = std::move(l_upOcean);
    std::fprintf(stderr, "[title] the ocean, quality %s\n", l_oQ.m_pszName);
    return true;
}

void TitleScene::f_Shutdown() {
    if (m_upOcean) m_upOcean->f_Shutdown();
    m_upOcean.reset();
    m_spGpu.reset();   // its buffers first, while bgfx is up
    m_oRenderer.f_Shutdown();
}

void TitleScene::f_DrawOcean(double l_dDtSeconds, uint32_t l_uWidth, uint32_t l_uHeight) {
    const bool l_bFirst = m_iOceanFrame == 0;   // the wave pictures are empty until the first step
    m_dTime += l_dDtSeconds;
    m_fOceanPendingDt += (float)l_dDtSeconds;
    m_upOcean->f_SetGridForScreen(m_iOceanRows, l_uWidth, l_uHeight);
    if (l_bFirst || m_iOceanFrame % m_iOceanSimEvery == 0) {
        m_upOcean->f_Step(m_dTime, m_fOceanPendingDt);
        m_fOceanPendingDt = 0.0f;
    }
    m_iOceanFrame++;
    ocean::f_PlaceBoatCamera(m_oOceanLook, ocean::BoatCamera(), m_dTime, l_uWidth, l_uHeight);
    m_upOcean->f_Draw(m_u16FirstView, m_oOceanLook, m_dTime, l_uWidth, l_uHeight);
}

void TitleScene::f_Draw(double l_dDtSeconds, uint32_t l_uWidth, uint32_t l_uHeight) {
    if (l_uWidth == 0 || l_uHeight == 0) return;
    if (m_upOcean) {
        f_DrawOcean(l_dDtSeconds, l_uWidth, l_uHeight);
        return;
    }
    if (!m_spGpu) return;
    m_dTime += l_dDtSeconds;
    m_oPose.f_Reset();
    if (!m_oModel.m_vAnimations.empty()) {
        const float l_fD = m_oModel.m_vAnimations[0].m_fDuration;
        m_oPose.f_Apply(0, l_fD > 0 ? (float)std::fmod(m_dTime, (double)l_fD) : 0.0f);
    }
    m_oPose.f_UpdateWorld();

    GltfRenderer::Frame l_oF;
    const float l_fAspect = float(l_uWidth) / float(l_uHeight);
    const glm::vec3 l_v3C = (m_v3BoundsMin + m_v3BoundsMax) * 0.5f;
    const float l_fRadius = std::max(glm::length(m_v3BoundsMax - m_v3BoundsMin) * 0.5f, 1e-3f);
    const std::vector<gltf::PlacedCamera> l_vCams = m_oPose.f_Cameras();
    if (!m_vCameras.empty() && !l_vCams.empty()) {   // the file's cameras, levelled, a cut every kSecondsPerCamera
        const size_t l_nPick = (size_t)m_vCameras[(size_t)(m_dTime / g_dSecondsPerCamera) % m_vCameras.size()];
        const gltf::PlacedCamera& l_oPc = l_vCams[std::min(l_nPick, l_vCams.size() - 1)];
        const gltf::Camera& l_oCam = m_oModel.m_vCameras[(size_t)l_oPc.m_iCamera];
        l_oF.m_v3Eye = glm::vec3(l_oPc.m_mat4World[3]);
        l_oF.m_mat4View = f_LevelView(l_oPc.m_mat4World);
        const float l_fZn = std::max(l_oCam.m_fZnear, 0.01f);
        const float l_fZf = l_oCam.m_fZfar > l_fZn ? l_oCam.m_fZfar : l_fZn + l_fRadius * 20.0f;
        l_oF.m_mat4Proj = GltfRenderer::f_Projection(glm::degrees(l_oCam.m_fYfov), l_fAspect, l_fZn, l_fZf);
    } else {   // no usable camera: a slow orbit around the whole scene
        const float l_fA = (float)m_dTime * 0.15f;
        const float l_fDist = l_fRadius * 1.6f;
        l_oF.m_v3Eye = l_v3C + glm::vec3(std::sin(l_fA) * l_fDist, l_fRadius * 0.6f, std::cos(l_fA) * l_fDist);
        l_oF.m_mat4View = glm::lookAt(l_oF.m_v3Eye, l_v3C, glm::vec3(0, 1, 0));
        l_oF.m_mat4Proj = GltfRenderer::f_Projection(45.0f, l_fAspect, l_fRadius * 0.01f, l_fRadius * 10.0f);
    }
    l_oF.m_v3BoundsMin = m_v3BoundsMin;
    l_oF.m_v3BoundsMax = m_v3BoundsMax;
#if TOMS_GAMMA_SPACE
    l_oF.m_bSrgbOut = true;     // no sRGB backbuffer (the web, Android: color_space.h): the shader encodes
#else
    l_oF.m_bSrgbOut = false;    // the desktop backbuffer is sRGB
#endif
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    l_oF.m_bShadows = false;    // phones and the browser: keep the title cheap
#else
    l_oF.m_iShadowMapSize = 2048;
#endif
    l_oF.m_fExposure = 0.75f;   // a little dark: the menu stays readable over it
    m_oRenderer.begin(m_u16FirstView, l_oF, l_uWidth, l_uHeight, 0x101018ff);
    m_oRenderer.f_Draw(*m_spGpu, m_oPose);
}

}  // namespace toms::next
