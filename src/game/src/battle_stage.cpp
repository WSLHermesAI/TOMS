// battle_stage.cpp -- see battle_stage.h.
#include "battle_stage.h"
#include "color_space.h"   // TOMS_GAMMA_SPACE: web and Android blend in gamma space

#include "gltf_environment.h"
#include "save_slots.h"   // toms::f_DefaultSaveDir: the sky cache sits beside the saves
#include "vfs.h"

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace toms::next {
namespace {

const auto g_oProcessStart = std::chrono::steady_clock::now();   // (for the log: when the stage was asked for and ready)

std::string f_DirOf(const std::string& l_strPath) {
    const size_t l_nS = l_strPath.find_last_of("/\\");
    return l_nS == std::string::npos ? std::string(".") : l_strPath.substr(0, l_nS);
}

glm::vec3 f_Vec3(const nlohmann::json& l_jA, const glm::vec3& l_v3Default) {
    if (!l_jA.is_array() || l_jA.size() < 3) return l_v3Default;
    return {l_jA[0].get<float>(), l_jA[1].get<float>(), l_jA[2].get<float>()};
}

}  // namespace

namespace {

// The prefiltered sky on disk (<save folder>/cache/sky_<hash>.env): prefiltering takes 0.7 s (8 s in a Debug build), so
// it is done once per sky file and settings. Contents: "TOMSENV1", face size, mip count, the 9 SH colours, then each
// face of each mip as RGBA16F (Environment::m_vHalfFaces). A file that does not match is simply made again.
std::string f_SkyCachePath(const std::string& l_strHdr, int l_iFaceSize, int l_iSamples) {
    std::error_code l_oEc;
    const auto l_u64Size = std::filesystem::file_size(l_strHdr, l_oEc);
    if (l_oEc) return std::string();
    const auto l_i64Time = std::filesystem::last_write_time(l_strHdr, l_oEc).time_since_epoch().count();
    uint64_t l_u64H = 1469598103934665603ull;   // FNV-1a of what makes this sky
    const std::string l_strKey = l_strHdr + "|" + std::to_string(l_u64Size) + "|" + std::to_string((long long)l_i64Time) + "|" +
                                 std::to_string(l_iFaceSize) + "|" + std::to_string(l_iSamples) + "|v1";
    for (unsigned char l_u8C : l_strKey) l_u64H = (l_u64H ^ l_u8C) * 1099511628211ull;
    char l_pszName[40];
    std::snprintf(l_pszName, sizeof l_pszName, "sky_%016llx.env", (unsigned long long)l_u64H);
    return toms::f_DefaultSaveDir() + "/cache/" + l_pszName;
}

bool f_ReadSkyCache(const std::string& l_strPath, gltf::Environment& l_oEnv) {
    std::ifstream l_oIn(l_strPath, std::ios::binary);
    char l_pszMagic[8] = {};
    int32_t l_iFace = 0, l_iMips = 0;
    if (!l_oIn.read(l_pszMagic, 8) || std::memcmp(l_pszMagic, "TOMSENV1", 8) != 0) return false;
    if (!l_oIn.read((char*)&l_iFace, 4) || !l_oIn.read((char*)&l_iMips, 4) || l_iFace <= 0 || l_iFace > 4096 || l_iMips <= 0 || l_iMips > 14) return false;
    if (!l_oIn.read((char*)l_oEnv.m_arrSh, sizeof(l_oEnv.m_arrSh))) return false;
    l_oEnv.m_iFaceSize = l_iFace;
    l_oEnv.m_vFaces.assign((size_t)l_iMips * 6, {});   // (the float faces are not needed: the halves are what is uploaded)
    l_oEnv.m_vHalfFaces.assign((size_t)l_iMips * 6, {});
    for (int l_iM = 0; l_iM < l_iMips; l_iM++)
        for (int l_iF = 0; l_iF < 6; l_iF++) {
            const size_t l_nS = (size_t)std::max(l_iFace >> l_iM, 1);
            std::vector<uint16_t>& l_vH = l_oEnv.m_vHalfFaces[(size_t)(l_iM * 6 + l_iF)];
            l_vH.resize(l_nS * l_nS * 4);
            if (!l_oIn.read((char*)l_vH.data(), (std::streamsize)(l_vH.size() * 2))) return false;
            l_oEnv.m_vFaces[(size_t)(l_iM * 6 + l_iF)].assign(3, 0.0f);   // (non-empty: Environment::f_Valid)
        }
    return true;
}

void f_WriteSkyCache(const std::string& l_strPath, const gltf::Environment& l_oEnv) {
    std::error_code l_oEc;
    std::filesystem::create_directories(std::filesystem::path(l_strPath).parent_path(), l_oEc);
    const std::string l_strTmp = l_strPath + ".tmp";
    {
        std::ofstream l_oOut(l_strTmp, std::ios::binary | std::ios::trunc);
        const int32_t l_iFace = l_oEnv.m_iFaceSize, l_iMips = l_oEnv.f_Mips();
        l_oOut.write("TOMSENV1", 8);
        l_oOut.write((const char*)&l_iFace, 4);
        l_oOut.write((const char*)&l_iMips, 4);
        l_oOut.write((const char*)l_oEnv.m_arrSh, sizeof(l_oEnv.m_arrSh));
        for (const std::vector<uint16_t>& l_vH : l_oEnv.m_vHalfFaces) l_oOut.write((const char*)l_vH.data(), (std::streamsize)(l_vH.size() * 2));
        if (!l_oOut) return;
    }
    std::filesystem::rename(l_strTmp, l_strPath, l_oEc);   // whole or not at all (two games at once)
}

}  // namespace

BattleStage::~BattleStage() {
    if (m_oPending.valid()) m_oPending.wait();   // the worker uses no member: just let it finish
}

// ---- the worker: everything but the GPU ----
std::shared_ptr<BattleStage::Loaded> BattleStage::f_LoadCpu(nlohmann::json l_jRow, std::string l_strAssetDir) {
    const auto l_oStart = std::chrono::steady_clock::now();
    auto l_spL = std::make_shared<Loaded>();
    Loaded& l_oL = *l_spL;
    l_oL.m_strRowKey = l_jRow.dump();
    auto l_fnPath = [&](const std::string& l_strRel) { return l_strAssetDir + "/" + l_strRel; };
    auto l_fnModel = [&](const std::string& l_strRel) -> std::shared_ptr<Model> {
        const std::string l_strPath = l_fnPath(l_strRel);
        auto l_itModel = l_oL.m_mapModels.find(l_strPath);
        if (l_itModel != l_oL.m_mapModels.end()) return l_itModel->second;
        std::string l_strBytes, l_strWhy;
        if (!toms::f_VfsReadAll(l_strPath, l_strBytes)) {
            l_oL.m_strError = "cannot read " + l_strPath;
            return nullptr;
        }
        auto l_spM = std::make_shared<Model>();
        if (!gltf::f_LoadModelFromMemory(l_strBytes.data(), l_strBytes.size(), f_DirOf(l_strPath), l_spM->m_oModel, &l_strWhy)) {
            l_oL.m_strError = l_strPath + ": " + l_strWhy;
            return nullptr;
        }
        l_spM->m_oPose.f_Bind(&l_spM->m_oModel);
        l_spM->m_oPose.f_UpdateWorld();
        l_oL.m_mapModels[l_strPath] = l_spM;
        return l_spM;
    };

    // the sky: what is behind everything, and the light the models get from it
    const nlohmann::json l_jSky = l_jRow.value("sky", nlohmann::json::object());
    const std::string l_strHdr = l_jSky.value("hdr", std::string());
    if (!l_strHdr.empty()) {
#if defined(__EMSCRIPTEN__)
        // the browser: a smaller sky (4 times quicker to prefilter, on the main thread without threads) and no cache
        // (it would be 17 MB in the browser's storage)
        const int l_iFace = std::min(256, l_jSky.value("faceSize", 512)), l_iSamples = std::min(16, l_jSky.value("samples", 32));
        const std::string l_strCache;
#else
        const int l_iFace = l_jSky.value("faceSize", 512), l_iSamples = l_jSky.value("samples", 32);
        const std::string l_strCache = f_SkyCachePath(l_fnPath(l_strHdr), l_iFace, l_iSamples);
#endif
        if (!l_strCache.empty() && f_ReadSkyCache(l_strCache, l_oL.m_oEnv)) {
            l_oL.m_bSkyCached = true;
        } else {
            gltf::Equirect l_oPic;
            std::string l_strWhy;
            if (!gltf::f_LoadEquirect(l_fnPath(l_strHdr), l_oPic, &l_strWhy)) {
                l_oL.m_strError = l_strHdr + ": " + l_strWhy;
                return l_spL;
            }
            l_oL.m_oEnv = gltf::f_PrefilterEnvironment(l_oPic, l_iFace, l_iSamples);
            gltf::f_PackHalf(l_oL.m_oEnv);   // the GPU's format here, so the upload is a copy
            if (!l_strCache.empty()) f_WriteSkyCache(l_strCache, l_oL.m_oEnv);
        }
        l_oL.m_bSky = true;
    }
    GltfRenderer::Frame& l_oLook = l_oL.m_oLook;
    l_oLook.m_bEnvironment = l_oL.m_bSky;
    l_oLook.m_bBackground = l_oL.m_bSky;
    l_oLook.m_fEnvironmentIntensity = l_jSky.value("intensity", 1.0f);
    l_oLook.m_fEnvironmentYaw = glm::radians(l_jSky.value("yaw", 0.0f));
    l_oLook.m_fBackgroundBlur = l_jSky.value("blur", 0.0f);
    l_oLook.m_fExposure = l_jRow.value("exposure", 1.0f);
    // the moon: one directional light with shadows
    const nlohmann::json l_jLight = l_jRow.value("light", nlohmann::json::object());
    GltfRenderer::Light l_oMoon;
    l_oMoon.m_eType = GltfRenderer::Light::Type::Directional;
    l_oMoon.m_v3Direction = glm::normalize(f_Vec3(l_jLight.value("dir", nlohmann::json()), glm::vec3(0.3f, -1.0f, -0.4f)));
    l_oMoon.m_v3Color = f_Vec3(l_jLight.value("color", nlohmann::json()), glm::vec3(0.7f, 0.78f, 1.0f));
    l_oMoon.m_fIntensity = l_jLight.value("intensity", 2.0f);
    l_oLook.m_vLights = {l_oMoon};
    l_oLook.m_fAmbient = l_jLight.value("ambient", 0.4f);

    if (!l_jRow.value("ground", std::string()).empty() && !(l_oL.m_spGround = l_fnModel(l_jRow["ground"].get<std::string>()))) return l_spL;
    if (!l_jRow.value("shadow", std::string()).empty() && !(l_oL.m_spShadow = l_fnModel(l_jRow["shadow"].get<std::string>()))) return l_spL;
    for (const auto& l_jP : l_jRow.value("props", nlohmann::json::array())) {
        Prop l_oP;
        if (!(l_oP.m_spModel = l_fnModel(l_jP.value("model", std::string())))) return l_spL;
        const std::string l_strNode = l_jP.value("node", std::string());
        if (!l_strNode.empty()) {   // one piece of a kit: every other root node hidden, this one at the origin
            const gltf::Model& l_oM = l_oP.m_spModel->m_oModel;
            auto l_spPose = std::make_shared<gltf::Pose>();
            l_spPose->f_Bind(&l_oM);
            bool l_bFound = false;
            const std::vector<int> l_vRoots = l_oM.m_vScenes.empty() ? std::vector<int>() : l_oM.m_vScenes[(size_t)std::max(0, l_oM.m_iScene)].m_vRoots;
            for (int l_iN : l_vRoots) {
                const bool l_bThis = l_oM.m_vNodes[(size_t)l_iN].m_strName == l_strNode;
                l_bFound = l_bFound || l_bThis;
                l_spPose->f_SetLocal(l_iN, l_bThis ? glm::vec3(0.0f) : l_spPose->f_Translation(l_iN), l_spPose->f_Rotation(l_iN),
                                     l_bThis ? l_spPose->f_Scale(l_iN) : glm::vec3(0.0f));
            }
            if (!l_bFound) {
                l_oL.m_strError = l_jP.value("model", std::string()) + " has no root node \"" + l_strNode + "\"";
                return l_spL;
            }
            l_spPose->f_UpdateWorld();
            l_oP.m_spPose = l_spPose;
        }
        glm::mat4 l_mat4M = glm::translate(glm::mat4(1.0f), f_Vec3(l_jP.value("at", nlohmann::json()), glm::vec3(0.0f)));
        l_mat4M = glm::rotate(l_mat4M, glm::radians(l_jP.value("turn", 0.0f)), glm::vec3(0, 1, 0));
        const nlohmann::json l_jS = l_jP.value("scale", nlohmann::json(1.0f));
        l_mat4M = glm::scale(l_mat4M, l_jS.is_array() ? f_Vec3(l_jS, glm::vec3(1.0f)) : glm::vec3(l_jS.get<float>()));
        l_oP.m_mat4Place = l_mat4M;
        l_oL.m_vProps.push_back(std::move(l_oP));
    }
    // the fighters' sprites (docs/gameplay/21 §4.3): their own light, x 1 / exposure ("glow")
    l_oL.m_fSpriteGlow = l_jRow.value("sprites", nlohmann::json::object()).value("glow", 0.6f);
    // what the moon's shadow must cover: the stage's middle (the fighters, the props near them)
    const nlohmann::json l_jB = l_jRow.value("shadowBounds", nlohmann::json::array());
    if (l_jB.is_array() && l_jB.size() == 2) {
        l_oL.m_v3BoundsMin = f_Vec3(l_jB[0], l_oL.m_v3BoundsMin);
        l_oL.m_v3BoundsMax = f_Vec3(l_jB[1], l_oL.m_v3BoundsMax);
    }
    l_oL.m_dCpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l_oStart).count();
    return l_spL;
}

// ---- the main thread: requests, the GPU ----
void BattleStage::f_Request(const nlohmann::json& l_jRow, const std::string& l_strAssetDir, bool l_bWait) {
    const std::string l_strKey = l_jRow.dump();
    if (l_strKey == m_strFailedKey) return;
    const bool l_bLoading = m_oPending.valid();
    if (l_strKey == m_strRowKey && m_bReady && !l_bLoading) return;
    if (l_bLoading && l_strKey != m_strLoadingKey) {   // another is on its way: this one after it
        m_jNext = l_jRow;
        m_strNextDir = l_strAssetDir;
    } else if (!l_bLoading && !(l_strKey == m_strRowKey && m_bReady)) {
        m_strLoadingKey = l_strKey;
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        // no threads (the single-threaded web build): made on the next poll, on the main thread, a short pause on
        // entering the floor rather than at the battle
        m_oPending = std::async(std::launch::deferred, &BattleStage::f_LoadCpu, l_jRow, l_strAssetDir);
#else
        m_oPending = std::async(std::launch::async, &BattleStage::f_LoadCpu, l_jRow, l_strAssetDir);
#endif
        std::fprintf(stderr, "[battle3d] loading the stage (in the background), %.1f s after the start\n",
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - g_oProcessStart).count());
    }
    if (l_bWait && m_oPending.valid()) {
        std::string l_strWhy;
        m_oPending.wait();
        if (!f_Poll(l_strWhy)) std::fprintf(stderr, "[battle3d] no 3D stage (the painted backdrop): %s\n", l_strWhy.c_str());
        if (m_oPending.valid()) {   // the one queued behind it
            m_oPending.wait();
            if (!f_Poll(l_strWhy)) std::fprintf(stderr, "[battle3d] no 3D stage (the painted backdrop): %s\n", l_strWhy.c_str());
        }
    }
}

bool BattleStage::f_Poll(std::string& l_strError) {
    if (!m_oPending.valid()) return true;
    const std::future_status l_eS = m_oPending.wait_for(std::chrono::seconds(0));
    if (l_eS != std::future_status::ready && l_eS != std::future_status::deferred) return true;   // (deferred: made now, by get())
    std::shared_ptr<Loaded> l_spL = m_oPending.get();
    m_strLoadingKey.clear();
    bool l_bOk = l_spL && l_spL->m_strError.empty();
    if (l_bOk) l_bOk = f_Upload(*l_spL, l_strError);
    else if (l_spL) l_strError = l_spL->m_strError;
    if (!l_bOk && l_spL) m_strFailedKey = l_spL->m_strRowKey;
    if (!m_jNext.is_null()) {   // the newest request next
        const nlohmann::json l_jRow = std::move(m_jNext);
        m_jNext = nullptr;
        f_Request(l_jRow, m_strNextDir, false);
    }
    return l_bOk;
}

bool BattleStage::f_Upload(Loaded& l_oL, std::string& l_strError) {
    const auto l_oStart = std::chrono::steady_clock::now();
    m_bReady = false;
    if (!m_bInit) {
        if (!m_oRenderer.f_Init()) {
            l_strError = "no mesh shaders for this backend";
            return false;
        }
        m_bInit = true;
    }
    if (l_oL.m_bSky) {
        if (!m_oRenderer.f_SetEnvironment(l_oL.m_oEnv, &l_strError)) return false;
    } else {
        m_oRenderer.f_ClearEnvironment();
    }
    m_vProps.clear();   // the stage before: its GPU models go with the last reference
    m_spGround.reset();
    m_spShadow.reset();
    m_mapModels = std::move(l_oL.m_mapModels);
    for (auto& l_pairModel : m_mapModels) l_pairModel.second->m_spGpu = m_oRenderer.f_Upload(l_pairModel.second->m_oModel);
    m_spGround = l_oL.m_spGround;
    m_spShadow = l_oL.m_spShadow;
    m_vProps = std::move(l_oL.m_vProps);
    m_fSpriteGlow = l_oL.m_fSpriteGlow;
    m_oLook = l_oL.m_oLook;
    m_v3BoundsMin = l_oL.m_v3BoundsMin;
    m_v3BoundsMax = l_oL.m_v3BoundsMax;
    m_strRowKey = l_oL.m_strRowKey;
    m_bReady = true;
    const double l_dGpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l_oStart).count();
    std::fprintf(stderr, "[battle3d] %.1f s after the start: stage: %zu props, %zu models, sky %s%s; loaded in the background %.0f ms, uploaded %.0f ms\n",
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - g_oProcessStart).count(), m_vProps.size(), m_mapModels.size(), l_oL.m_bSky ? "yes" : "none", l_oL.m_bSkyCached ? " (cached)" : "", l_oL.m_dCpuMs, l_dGpuMs);
    return true;
}

void BattleStage::f_Shutdown() {
    if (m_oPending.valid()) m_oPending.wait();
    m_oPending = {};
    m_vProps.clear();
    m_spGround.reset();
    m_spShadow.reset();
    m_mapModels.clear();   // their buffers first, while bgfx is up
    if (m_bInit) m_oRenderer.f_Shutdown();
    m_bInit = m_bReady = false;
    m_strRowKey.clear();
}

glm::mat4 BattleStage::f_Draw(float l_fDtSeconds, uint32_t l_uWidth, uint32_t l_uHeight, const Camera& l_oCam, const std::vector<glm::vec4>& l_vShadows) {
    if (!m_bReady || l_uWidth == 0 || l_uHeight == 0) return glm::mat4(1.0f);
    GltfRenderer::Frame l_oF = m_oLook;
    const float l_fAspect = float(l_uWidth) / float(l_uHeight);
    l_oF.m_v3Eye = l_oCam.m_v3Eye;
    l_oF.m_mat4View = glm::lookAt(l_oCam.m_v3Eye, l_oCam.m_v3Look, glm::vec3(0, 1, 0));
    l_oF.m_mat4Proj = GltfRenderer::f_Projection(l_oCam.m_fFovYDeg, l_fAspect, 0.1f, 400.0f);
    l_oF.m_v3BoundsMin = m_v3BoundsMin;
    l_oF.m_v3BoundsMax = m_v3BoundsMax;
    l_oF.m_fExposure *= 1.0f - 0.82f * glm::clamp(l_oCam.m_fDim, 0.0f, 1.0f);
    // the moon (clouds passing), then the fires and the flashes as point lights (no shadows: one each costs 6 maps)
    if (!l_oF.m_vLights.empty()) l_oF.m_vLights[0].m_fIntensity *= l_oCam.m_fMoon;
    l_oF.m_fEnvironmentIntensity *= 0.55f + 0.45f * l_oCam.m_fMoon;
    for (const Camera::Point& l_oP : l_oCam.m_vPoints) {
        if ((int)l_oF.m_vLights.size() >= GltfRenderer::m_iMaxLights) break;
        GltfRenderer::Light l_oL;
        l_oL.m_eType = GltfRenderer::Light::Type::Point;
        l_oL.m_v3Position = l_oP.m_v3Pos;
        l_oL.m_v3Color = l_oP.m_v3Color;
        l_oL.m_fIntensity = l_oP.m_fIntensity;
        l_oL.m_fRange = l_oP.m_fRange;
        l_oL.m_bCastShadows = false;
        l_oF.m_vLights.push_back(l_oL);
    }
    m_fShown = std::min(1.0f, m_fShown + l_fDtSeconds / 0.45f);   // a battle opens on darkness, the stage fades in
    l_oF.m_fExposure *= m_fShown * m_fShown * (3.0f - 2.0f * m_fShown);
#if TOMS_GAMMA_SPACE
    l_oF.m_bSrgbOut = true;     // no sRGB backbuffer (the web, Android): the shader encodes
#else
    l_oF.m_bSrgbOut = false;    // the desktop backbuffer is sRGB
#endif
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    l_oF.m_bShadows = false;
#else
    l_oF.m_iShadowMapSize = 2048;
#endif
    if (++m_iFramesDrawn == 3) {   // (the stats are the frame before's: once, so the log says what the stage costs)
        const GltfRenderer::Stats& l_oS = m_oRenderer.f_Stats();
        std::fprintf(stderr, "[battle3d] a frame: %d draw calls, %d shadow draws\n", l_oS.m_iDrawCalls, l_oS.m_iShadowDraws);
    }
    m_v3Eye = l_oCam.m_v3Eye;
    m_fExposure = l_oF.m_fExposure;
    m_oRenderer.begin(m_u16FirstView, l_oF, l_uWidth, l_uHeight, 0x05060cff);
    if (m_spGround) m_oRenderer.f_Draw(*m_spGround->m_spGpu, m_spGround->m_oPose, glm::mat4(1.0f), false);
    for (const Prop& l_oP : m_vProps)
        m_oRenderer.f_Draw(*l_oP.m_spModel->m_spGpu, l_oP.m_spPose ? *l_oP.m_spPose : l_oP.m_spModel->m_oPose, l_oP.m_mat4Place);
    if (m_spShadow)   // a soft disc on the ground under each fighter (blended: drawn after the opaque stage)
        for (const glm::vec4& l_v4S : l_vShadows) {
            const glm::mat4 l_mat4M = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(l_v4S.x, l_v4S.y + 0.01f, l_v4S.z)),
                                                 glm::vec3(l_v4S.w * 2.0f, 1.0f, l_v4S.w * 1.3f));
            m_oRenderer.f_Draw(*m_spShadow->m_spGpu, m_spShadow->m_oPose, l_mat4M, false);
        }
    return l_oF.m_mat4Proj * l_oF.m_mat4View;
}

// The game's world groups (BgfxRenderer::f_WorldGroups: the fighters' and the fires' sprites, already in metres) into
// this frame's scene: each run a draw, lit by the stage's lights (a lit group) with a little of its own light, sorted
// among the blended draws by its distance -- a bit nearer than its middle, so the shadow disc under it goes first, and
// each run after the one before (the order it was drawn in 2D).
void BattleStage::f_DrawSprites(const std::vector<BgfxRenderer::WorldGroup>& l_vGroups) {
    if (!m_bReady) return;
    std::vector<GltfRenderer::SpriteVertex> l_vV;
    for (const BgfxRenderer::WorldGroup& l_oG : l_vGroups) {
        if (l_oG.m_vVerts.empty() || l_oG.m_vRuns.empty()) continue;
        l_vV.resize(l_oG.m_vVerts.size());
        glm::vec3 l_v3Min(1e30f), l_v3Max(-1e30f);
        for (size_t l_nI = 0; l_nI < l_oG.m_vVerts.size(); l_nI++) {
            const BgfxRenderer::WorldVertex& l_oW = l_oG.m_vVerts[l_nI];
            l_vV[l_nI].m_v3Pos = glm::vec3(l_oW.m_fX, l_oW.m_fY, l_oW.m_fZ);
            l_vV[l_nI].m_v2Uv = glm::vec2(l_oW.m_fU, l_oW.m_fV);
            l_vV[l_nI].m_v4Color = glm::vec4(l_oW.m_fR, l_oW.m_fG, l_oW.m_fB, l_oW.m_fA);
            l_v3Min = glm::min(l_v3Min, l_vV[l_nI].m_v3Pos);
            l_v3Max = glm::max(l_v3Max, l_vV[l_nI].m_v3Pos);
        }
        const glm::vec3 l_v3N(l_oG.m_arrMat[8], l_oG.m_arrMat[9], l_oG.m_arrMat[10]);   // the matrix's third column: the plane's normal
        const float l_fDist = std::max(0.0f, glm::length((l_v3Min + l_v3Max) * 0.5f - m_v3Eye) - 0.3f);
        for (size_t l_nR = 0; l_nR < l_oG.m_vRuns.size(); l_nR++) {
            const BgfxRenderer::WorldRun& l_oR = l_oG.m_vRuns[l_nR];
            if (l_oR.m_uIndexCount < 3) continue;
            GltfRenderer::Sprites l_oS;
            l_oS.m_pVerts = l_vV.data();
            l_oS.m_uVertexCount = (uint32_t)l_vV.size();
            l_oS.m_pIdx = l_oG.m_vIdx.data() + l_oR.m_uFirstIndex;
            l_oS.m_uIndexCount = l_oR.m_uIndexCount;
            l_oS.m_u16Texture = l_oR.m_u16Texture;
            l_oS.m_bLinear = l_oR.m_bLinear;
            l_oS.m_bLit = l_oG.m_bLit;
            l_oS.m_bAdditive = l_oR.m_bAdditive;
            l_oS.m_v3Normal = glm::length(l_v3N) > 1e-6f ? glm::normalize(l_v3N) : glm::vec3(0.0f, 0.0f, 1.0f);
            l_oS.m_v3Emissive = glm::vec3(m_fSpriteGlow / std::max(0.05f, m_fExposure));   // as bright on any stage
            l_oS.m_fSortDistance = std::max(0.0f, l_fDist - 0.001f * (float)l_nR);
            m_oRenderer.f_DrawSprites(l_oS);
        }
    }
}

}  // namespace toms::next
