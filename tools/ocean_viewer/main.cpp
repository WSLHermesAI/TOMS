// ocean_viewer -- try an ocean and a sky on their own, outside the game (docs/engine/21_OCEAN_VIEWER.md). A real-time FFT
// ocean (ocean_sim.h) drawn with bgfx (ocean_renderer.h), with every setting on sliders.
//
//   ocean_viewer [options]
//     --renderer=auto|d3d11|d3d12|vulkan|opengl
//     --preset=<name>        start with a preset (day, sunset, storm, calm) instead of the saved settings
//     --quality=<name>       auto, high, medium, low, lowest (the table g_arrQualities; auto: starts by the device and
//                            steps down while frames are slow). Default: high on the desktop, auto on the web
//     --size=<w>x<h>         the window (default 1280x720)
//     --no-ui                hide the panel; H toggles it
//     --frames=<n>           quit after n frames, and print the timings
//     --fixed-dt=<ms>        every frame advances exactly this long
//     --start-time=<s>       the sea's time at the first frame
//     --screenshot=<png>     save the last frame (with --frames; the window stays hidden)
//     --record=<folder>      save every frame as <folder>/frame_00000.png (with --frames; the window stays hidden);
//                            tools/frames_to_gif.py merges them
//     --record-step=<n>      only every nth frame
//
// Mouse: left drag looks around, the wheel moves the camera up and down. Keys: H panel, Space pause, Esc quit.
//
// The web build (tools\build_ocean_web.cmd, WebGL2, single-threaded): the options come from the page's URL
// (?preset=storm&quality=low). A phone draws at one pixel per CSS pixel. "Save settings" keeps them in the browser.
#include "bgfx_host.h"
#include "color_space.h"   // TOMS_GAMMA_SPACE: the web has no sRGB backbuffer
#include "imgui_bgfx.h"
#include "job_system.h"
#include "ocean_renderer.h"
#include "ocean_sim.h"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace toms;
using namespace toms::ocean;

namespace {

constexpr uint16_t g_u16ViewScene = 0, g_u16ViewUi = 1;

struct Args {
    std::string m_strRenderer = "auto", m_strPreset, m_strQuality, m_strScreenshot, m_strRecord;
    int m_iW = 1280, m_iH = 720, m_iFrames = 0, m_iFixedDtMs = 0, m_iRecordStep = 1;
    double m_dStartTime = 0.0;
    bool m_bUi = true;
};

const char* f_Value(const std::string& l_strS, const char* l_pszKey) {
    const size_t l_nN = std::strlen(l_pszKey);
    return l_strS.compare(0, l_nN, l_pszKey) == 0 ? l_strS.c_str() + l_nN : nullptr;
}

Args f_ParseArgs(int l_iArgc, char** l_pArgv) {
    Args l_oA;
    std::vector<std::string> l_vArgs(l_pArgv + std::min(l_iArgc, 1), l_pArgv + l_iArgc);
#ifdef __EMSCRIPTEN__
    // The page's URL: ?preset=storm&no-ui becomes --preset=storm --no-ui.
    char* l_pszQ = (char*)EM_ASM_PTR({ return stringToNewUTF8(location.search ? location.search.substring(1) : ""); });
    const std::string l_strQuery = l_pszQ ? l_pszQ : "";
    free(l_pszQ);
    for (size_t l_nPos = 0; l_nPos < l_strQuery.size();) {
        const size_t l_nAmp = l_strQuery.find('&', l_nPos);
        const std::string l_strKv = l_strQuery.substr(l_nPos, l_nAmp == std::string::npos ? std::string::npos : l_nAmp - l_nPos);
        if (!l_strKv.empty()) l_vArgs.push_back("--" + l_strKv);
        if (l_nAmp == std::string::npos) break;
        l_nPos = l_nAmp + 1;
    }
#endif
    for (const std::string& l_strS : l_vArgs) {
        if (const char* l_pszV = f_Value(l_strS, "--renderer=")) l_oA.m_strRenderer = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--preset=")) l_oA.m_strPreset = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--quality=")) l_oA.m_strQuality = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--size=")) std::sscanf(l_pszV, "%dx%d", &l_oA.m_iW, &l_oA.m_iH);
        else if (const char* l_pszV = f_Value(l_strS, "--frames=")) l_oA.m_iFrames = std::atoi(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--fixed-dt=")) l_oA.m_iFixedDtMs = std::atoi(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--start-time=")) l_oA.m_dStartTime = std::atof(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--screenshot=")) l_oA.m_strScreenshot = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--record=")) l_oA.m_strRecord = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--record-step=")) l_oA.m_iRecordStep = std::max(1, std::atoi(l_pszV));
        else if (l_strS == "--no-ui") l_oA.m_bUi = false;
    }
    return l_oA;
}

// Everything on the sliders, and everything the settings file holds.
struct Settings {
    WaveSettings m_oWave;
    Look m_oLook;
    float m_fCameraHeight = 10.0f, m_fCameraPitch = -3.0f, m_fCameraYaw = 0.0f, m_fFov = 50.0f, m_fCameraSway = 0.5f;
    float m_fTimeScale = 1.0f;
    float m_fSize0 = 700.0f, m_fSize1 = 110.0f, m_fSize2 = 19.0f;
    int m_iCascades = 3, m_iResolution = 256, m_iGridRows = 360;
    int m_iQuality = 0;       // an index into g_arrQualities
    int m_iSimEvery = 1;      // simulate the waves every nth frame
};

void f_ApplyQuality(Settings& l_oS, int l_iQuality) {
    const Quality& l_oQ = g_arrQualities[std::clamp(l_iQuality, 0, g_iQualities - 1)];
    l_oS.m_iQuality = std::clamp(l_iQuality, 0, g_iQualities - 1);
    l_oS.m_iCascades = l_oQ.m_iCascades;
    l_oS.m_iResolution = l_oQ.m_iResolution;
    l_oS.m_iGridRows = l_oQ.m_iGridRows;
    l_oS.m_oLook.m_iSkyOctaves = l_oQ.m_iSkyOctaves;
    l_oS.m_oLook.m_iReflectOctaves = l_oQ.m_iReflectOctaves;
    l_oS.m_oLook.m_iFoamOctaves = l_oQ.m_iFoamOctaves;
    l_oS.m_iSimEvery = l_oQ.m_iSimEvery;
}

struct Slider {
    const char* m_pszGroup;
    const char* m_pszKey;      // its name in the settings file
    const char* m_pszLabel;
    float* m_pValue;
    float m_fMin, m_fMax;
};

std::vector<Slider> f_Sliders(Settings& l_oS) {
    WaveSettings& l_oW = l_oS.m_oWave;
    Look& l_oL = l_oS.m_oLook;
    return {
        {"Waves", "windSpeed", "Wind (m/s)", &l_oW.m_fWindSpeed, 1.0f, 35.0f},
        {"Waves", "windDir", "Wind direction", &l_oW.m_fWindDirDeg, 0.0f, 360.0f},
        {"Waves", "fetchKm", "Fetch (km)", &l_oW.m_fFetchKm, 1.0f, 1500.0f},
        {"Waves", "spread", "Keep to the wind", &l_oW.m_fSpread, 1.0f, 40.0f},
        {"Waves", "peak", "Swell (peak)", &l_oW.m_fPeak, 1.0f, 7.0f},
        {"Waves", "choppy", "Choppiness", &l_oW.m_fChoppy, 0.0f, 2.0f},
        {"Waves", "waveScale", "Wave scale (drawn)", &l_oL.m_fWaveScale, 0.1f, 3.0f},
        {"Waves", "timeScale", "Time scale", &l_oS.m_fTimeScale, 0.0f, 3.0f},
        {"Foam", "foamStart", "Foam start", &l_oW.m_fFoamStart, 0.0f, 1.2f},
        {"Foam", "foamFade", "Foam fade (s)", &l_oW.m_fFoamFadeSeconds, 0.1f, 15.0f},
        {"Foam", "foam", "Foam brightness", &l_oL.m_fFoam, 0.0f, 2.0f},
        {"Water", "deepR", "Deep red", &l_oL.m_v3Deep.x, 0.0f, 0.2f},
        {"Water", "deepG", "Deep green", &l_oL.m_v3Deep.y, 0.0f, 0.3f},
        {"Water", "deepB", "Deep blue", &l_oL.m_v3Deep.z, 0.0f, 0.4f},
        {"Water", "scatterR", "Glow red", &l_oL.m_v3Scatter.x, 0.0f, 1.0f},
        {"Water", "scatterG", "Glow green", &l_oL.m_v3Scatter.y, 0.0f, 1.0f},
        {"Water", "scatterB", "Glow blue", &l_oL.m_v3Scatter.z, 0.0f, 1.0f},
        {"Water", "scatter", "Glow strength", &l_oL.m_fScatterStrength, 0.0f, 2.0f},
        {"Water", "rough", "Roughness", &l_oL.m_fRoughness, 0.02f, 0.6f},
        {"Water", "reflection", "Reflection", &l_oL.m_fReflection, 0.0f, 1.5f},
        {"Sky", "sunElevation", "Sun height", &l_oL.m_fSunElevationDeg, 0.5f, 85.0f},
        {"Sky", "sunAzimuth", "Sun direction", &l_oL.m_fSunAzimuthDeg, -180.0f, 180.0f},
        {"Sky", "turbidity", "Haze (turbidity)", &l_oL.m_fTurbidity, 2.0f, 10.0f},
        {"Sky", "skyStrength", "Sky strength", &l_oL.m_fSkyStrength, 0.1f, 3.0f},
        {"Sky", "cloudCover", "Cloud cover", &l_oL.m_fCloudCover, 0.0f, 1.0f},
        {"Sky", "cloudScale", "Cloud scale", &l_oL.m_fCloudScale, 0.2f, 4.0f},
        {"Sky", "cloudSpeed", "Cloud speed", &l_oL.m_fCloudSpeed, 0.0f, 0.1f},
        {"Sky", "cloudDark", "Cloud shade", &l_oL.m_fCloudDark, 0.0f, 1.0f},
        {"Sky", "fog", "Haze distance (m)", &l_oL.m_fFogMetres, 500.0f, 30000.0f},
        {"Sky", "exposure", "Exposure", &l_oL.m_fExposure, 0.1f, 2.5f},
        {"Camera", "camHeight", "Height (m)", &l_oS.m_fCameraHeight, 2.0f, 120.0f},
        {"Camera", "camPitch", "Pitch", &l_oS.m_fCameraPitch, -45.0f, 25.0f},
        {"Camera", "camYaw", "Turn", &l_oS.m_fCameraYaw, -180.0f, 180.0f},
        {"Camera", "fov", "Field of view", &l_oS.m_fFov, 20.0f, 90.0f},
        {"Camera", "sway", "Sway", &l_oS.m_fCameraSway, 0.0f, 3.0f},
        {"Detail", "size0", "Big patch (m)", &l_oS.m_fSize0, 200.0f, 2000.0f},
        {"Detail", "size1", "Middle patch (m)", &l_oS.m_fSize1, 40.0f, 300.0f},
        {"Detail", "size2", "Small patch (m)", &l_oS.m_fSize2, 5.0f, 40.0f},
    };
}

void f_ApplyPreset(Settings& l_oS, const std::string& l_strName) {
    const int l_iQuality = l_oS.m_iQuality;
    l_oS = Settings();   // "day" is the defaults
    f_ApplyQuality(l_oS, l_iQuality);
    WaveSettings& l_oW = l_oS.m_oWave;
    Look& l_oL = l_oS.m_oLook;
    if (l_strName == "sunset") {
        l_oL.m_fSunElevationDeg = 5.0f;
        l_oL.m_fSunAzimuthDeg = 6.0f;
        l_oL.m_fTurbidity = 4.5f;
        l_oL.m_fCloudCover = 0.45f;
        l_oL.m_fExposure = 0.8f;
        l_oL.m_fSkyStrength = 0.5f;
        l_oW.m_fWindSpeed = 9.0f;
    } else if (l_strName == "storm") {
        l_oW.m_fWindSpeed = 26.0f;
        l_oW.m_fChoppy = 1.3f;
        l_oW.m_fFoamStart = 0.75f;
        l_oW.m_fFoamFadeSeconds = 5.0f;
        l_oL.m_fSunElevationDeg = 28.0f;
        l_oL.m_fTurbidity = 8.0f;
        l_oL.m_fCloudCover = 0.92f;
        l_oL.m_fCloudDark = 0.8f;
        l_oL.m_fFogMetres = 3500.0f;
        l_oL.m_fSkyStrength = 0.55f;
        l_oL.m_fRoughness = 0.22f;
        l_oL.m_v3Deep = {0.012f, 0.05f, 0.075f};
        l_oS.m_fCameraHeight = 22.0f;
    } else if (l_strName == "calm") {
        l_oW.m_fWindSpeed = 4.5f;
        l_oW.m_fFetchKm = 40.0f;
        l_oW.m_fChoppy = 0.7f;
        l_oL.m_fCloudCover = 0.25f;
        l_oL.m_fSunElevationDeg = 40.0f;
        l_oL.m_v3Deep = {0.005f, 0.06f, 0.1f};
        l_oS.m_fCameraHeight = 7.0f;
    }
}

// Settings live in a file on the desktop, in the browser's storage on the web.
bool f_WriteText(const std::string& l_strPath, const std::string& l_strText) {
#ifdef __EMSCRIPTEN__
    (void)l_strPath;
    return EM_ASM_INT({ try { localStorage.setItem('tomsOceanSettings', UTF8ToString($0)); return 1; } catch (e) { return 0; } }, l_strText.c_str()) != 0;
#else
    std::ofstream l_oF(l_strPath);
    if (!l_oF) return false;
    l_oF << l_strText;
    return true;
#endif
}

std::string f_ReadText(const std::string& l_strPath) {
#ifdef __EMSCRIPTEN__
    (void)l_strPath;
    char* l_pszT = (char*)EM_ASM_PTR({ var s = null; try { s = localStorage.getItem('tomsOceanSettings'); } catch (e) {} return stringToNewUTF8(s || ""); });
    const std::string l_strT = l_pszT ? l_pszT : "";
    free(l_pszT);
    return l_strT;
#else
    std::ifstream l_oF(l_strPath);
    return std::string(std::istreambuf_iterator<char>(l_oF), std::istreambuf_iterator<char>());
#endif
}

bool f_SaveSettings(Settings& l_oS, const std::string& l_strPath) {
    nlohmann::json l_jOut;
    for (const Slider& l_oSl : f_Sliders(l_oS)) l_jOut[l_oSl.m_pszKey] = *l_oSl.m_pValue;
    l_jOut["cascades"] = l_oS.m_iCascades;
    l_jOut["resolution"] = l_oS.m_iResolution;
    l_jOut["gridRows"] = l_oS.m_iGridRows;
    return f_WriteText(l_strPath, l_jOut.dump(2) + "\n");
}

void f_LoadSettings(Settings& l_oS, const std::string& l_strPath) {
    const nlohmann::json l_jIn = nlohmann::json::parse(f_ReadText(l_strPath), nullptr, false);
    if (!l_jIn.is_object()) return;
    for (const Slider& l_oSl : f_Sliders(l_oS))
        if (l_jIn.contains(l_oSl.m_pszKey) && l_jIn[l_oSl.m_pszKey].is_number()) *l_oSl.m_pValue = l_jIn[l_oSl.m_pszKey].get<float>();
    l_oS.m_iCascades = std::clamp(l_jIn.value("cascades", l_oS.m_iCascades), 1, OceanRenderer::m_iMaxCascades);
    l_oS.m_iResolution = l_jIn.value("resolution", l_oS.m_iResolution);
    l_oS.m_iGridRows = l_jIn.value("gridRows", l_oS.m_iGridRows);
}

std::vector<CascadeSettings> f_CascadesOf(const Settings& l_oS) {
    std::vector<CascadeSettings> l_vC;
    const float l_arrSizes[] = {l_oS.m_fSize0, l_oS.m_fSize1, l_oS.m_fSize2};
    for (int l_iI = 0; l_iI < l_oS.m_iCascades; l_iI++) {
        CascadeSettings l_oC;
        l_oC.m_iN = l_oS.m_iResolution;
        l_oC.m_fSize = l_arrSizes[l_iI];
        l_vC.push_back(l_oC);
    }
    return l_vC;
}

// Returns true when the waves must be made again.
bool f_Panel(Settings& l_oS, bool& l_bPaused, bool& l_bAuto, const OceanRenderer& l_oR, float l_fFps, bool& l_bGridChanged, const std::string& l_strSettingsPath, std::string& l_strNotice,
             int l_iFbW, int l_iFbH) {
    bool l_bWaves = false;
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    // It fits a phone, and on the web it starts folded: the sea is what the page is for.
    ImGui::SetNextWindowSize(ImVec2(std::min(600.0f, (float)l_iFbW - 24.0f), std::min(660.0f, (float)l_iFbH - 24.0f)), ImGuiCond_FirstUseEver);
#ifdef __EMSCRIPTEN__
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
#endif
    char l_pszTitle[64];
    std::snprintf(l_pszTitle, sizeof(l_pszTitle), "Ocean  %.0f fps  %s###Ocean", l_fFps, g_arrQualities[l_oS.m_iQuality].m_pszName);
    ImGui::Begin(l_pszTitle);
    ImGui::Text("%.1f fps   GPU %.1f ms   %s", l_fFps, next::f_BgfxHostGpuMs(), next::f_BgfxHostRendererName().c_str());
    ImGui::Text("waves %.1f ms, pictures %.1f ms   %d triangles", l_oR.f_SimMs(), l_oR.f_UploadMs(), l_oR.f_Triangles());
    ImGui::Checkbox("Pause (Space)", &l_bPaused);
    ImGui::SameLine();
    for (const char* l_pszP : {"day", "sunset", "storm", "calm"}) {
        if (ImGui::SmallButton(l_pszP)) {
            f_ApplyPreset(l_oS, l_pszP);
            l_bWaves = true;
            l_bGridChanged = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Checkbox("Auto quality", &l_bAuto);
    for (int l_iQ = 0; l_iQ < g_iQualities; l_iQ++) {
        ImGui::SameLine();
        if (ImGui::RadioButton(g_arrQualities[l_iQ].m_pszName, l_oS.m_iQuality == l_iQ)) {
            f_ApplyQuality(l_oS, l_iQ);
            l_bAuto = false;
            l_bWaves = true;
            l_bGridChanged = true;
        }
    }
    if (ImGui::Button("Save settings")) l_strNotice = f_SaveSettings(l_oS, l_strSettingsPath) ? "Saved to " + l_strSettingsPath : "Could not write " + l_strSettingsPath;
    if (!l_strNotice.empty()) ImGui::TextWrapped("%s", l_strNotice.c_str());
    const std::vector<Slider> l_vSliders = f_Sliders(l_oS);
    const char* l_pszOpen = nullptr;
    bool l_bShown = false;
    for (const Slider& l_oSl : l_vSliders) {
        if (!l_pszOpen || std::strcmp(l_pszOpen, l_oSl.m_pszGroup) != 0) {
            l_pszOpen = l_oSl.m_pszGroup;
            ImGui::SetNextItemOpen(std::strcmp(l_pszOpen, "Detail") != 0, ImGuiCond_FirstUseEver);
            l_bShown = ImGui::CollapsingHeader(l_pszOpen);
        }
        if (!l_bShown) continue;
        const bool l_bChanged = ImGui::SliderFloat(l_oSl.m_pszLabel, l_oSl.m_pValue, l_oSl.m_fMin, l_oSl.m_fMax);
        const std::string l_strGroup = l_oSl.m_pszGroup;
        const std::string l_strKey = l_oSl.m_pszKey;
        const bool l_bDrawnOnly = l_strKey == "waveScale" || l_strKey == "timeScale" || l_strKey == "foam";
        if (l_bChanged && !l_bDrawnOnly && (l_strGroup == "Waves" || l_strGroup == "Foam" || l_strGroup == "Detail")) l_bWaves = true;
    }
    if (l_bShown) {   // the last group is Detail
        if (ImGui::SliderInt("Patches", &l_oS.m_iCascades, 1, OceanRenderer::m_iMaxCascades)) l_bWaves = true;
        int l_iPower = 0;
        while ((1 << l_iPower) < l_oS.m_iResolution) l_iPower++;
        if (ImGui::SliderInt("Resolution (2^n)", &l_iPower, 6, 10)) {
            l_oS.m_iResolution = 1 << l_iPower;
            l_bWaves = true;
        }
        if (ImGui::SliderInt("Grid rows", &l_oS.m_iGridRows, 60, 1080)) l_bGridChanged = true;
    }
    ImGui::End();
    return l_bWaves;
}

std::string f_SettingsPath() {
#ifdef TOMS_OCEAN_SETTINGS
    return TOMS_OCEAN_SETTINGS;
#else
    return "ocean_settings.json";
#endif
}

// Everything a frame needs: on the web, main returns while the browser keeps calling f_Tick.
struct App {
    Args m_oArgs;
    Settings m_oS;
    std::string m_strSettingsPath;
    SDL_Window* m_pWindow = nullptr;
    OceanRenderer m_oRenderer;
    next::ImGuiBgfx m_oGui;
    bool m_bQuit = false, m_bPaused = false, m_bShowUi = true, m_bDragging = false, m_bAuto = false;
    float m_fWheel = 0.0f, m_fFps = 0.0f, m_fLastX = 0.0f, m_fLastY = 0.0f;
    int m_iFbW = 0, m_iFbH = 0, m_iFrame = 0, m_iShotsAsked = 0, m_iWaitFrames = 0;
    double m_dTime = 0.0, m_dSumSim = 0.0, m_dSumUpload = 0.0;
    float m_fPendingDt = 0.0f;           // time the waves have not been simulated for yet (Settings::m_iSimEvery)
    float m_fSlowClock = 0.0f;           // auto quality: this measuring window's length ...
    int m_iSlowFrames = 0;               // ... and its frames
    float m_fSettle = 2.0f;              // seconds before auto quality measures (loading, a change of quality)
    std::string m_strNotice;
    std::chrono::steady_clock::time_point m_oLast = std::chrono::steady_clock::now();

    void f_OnEvents();
    void f_AutoQuality(float l_fRealDt, bool& l_bWavesChanged, bool& l_bGridChanged);
    void f_Tick();
};

void App::f_OnEvents() {
    SDL_Event l_oE;
    while (SDL_PollEvent(&l_oE)) {
        const bool l_bUiMouse = m_bShowUi && ImGui::GetIO().WantCaptureMouse;
        switch (l_oE.type) {
        case SDL_EVENT_QUIT: m_bQuit = true; break;
        case SDL_EVENT_MOUSE_WHEEL:
            m_fWheel += l_oE.wheel.y;
            if (!l_bUiMouse) m_oS.m_fCameraHeight = std::clamp(m_oS.m_fCameraHeight * std::pow(1.08f, l_oE.wheel.y), 2.0f, 120.0f);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (l_oE.button.button == SDL_BUTTON_LEFT && !l_bUiMouse) { m_bDragging = true; m_fLastX = l_oE.button.x; m_fLastY = l_oE.button.y; }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (l_oE.button.button == SDL_BUTTON_LEFT) m_bDragging = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (m_bDragging) {
                m_oS.m_fCameraYaw = std::remainder(m_oS.m_fCameraYaw - (l_oE.motion.x - m_fLastX) * 0.15f, 360.0f);
                m_oS.m_fCameraPitch = std::clamp(m_oS.m_fCameraPitch - (l_oE.motion.y - m_fLastY) * 0.15f, -45.0f, 25.0f);
                m_fLastX = l_oE.motion.x;
                m_fLastY = l_oE.motion.y;
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ImGui::GetIO().WantCaptureKeyboard) break;
#ifndef __EMSCRIPTEN__
            if (l_oE.key.key == SDLK_ESCAPE) m_bQuit = true;   // a web page stays
#endif
            if (l_oE.key.key == SDLK_H) m_bShowUi = !m_bShowUi;
            else if (l_oE.key.key == SDLK_SPACE) m_bPaused = !m_bPaused;
            break;
        default: break;
        }
    }
}

// Steps down one quality while frames are slow; never back up by itself, so the picture does not keep changing.
void App::f_AutoQuality(float l_fRealDt, bool& l_bWavesChanged, bool& l_bGridChanged) {
    if (!m_bAuto || l_fRealDt > 0.25f) return;   // (a long gap: the tab was hidden)
    if (m_fSettle > 0.0f) {
        m_fSettle -= l_fRealDt;
        return;
    }
    m_fSlowClock += l_fRealDt;
    m_iSlowFrames++;
    if (m_fSlowClock < 2.0f) return;
    const float l_fAverageMs = m_fSlowClock * 1000.0f / (float)m_iSlowFrames;
    m_fSlowClock = 0.0f;
    m_iSlowFrames = 0;
    if (l_fAverageMs <= 24.0f || m_oS.m_iQuality >= g_iQualities - 1) return;
    f_ApplyQuality(m_oS, m_oS.m_iQuality + 1);
    l_bWavesChanged = l_bGridChanged = true;
    m_fSettle = 1.0f;
    std::fprintf(stderr, "[ocean] %.1f ms a frame: quality now %s\n", l_fAverageMs, g_arrQualities[m_oS.m_iQuality].m_pszName);
}

void App::f_Tick() {
    f_OnEvents();
    int l_iNewW = 0, l_iNewH = 0;
    SDL_GetWindowSizeInPixels(m_pWindow, &l_iNewW, &l_iNewH);
    bool l_bGridChanged = false;
    if (l_iNewW != m_iFbW || l_iNewH != m_iFbH) {
        m_iFbW = l_iNewW;
        m_iFbH = l_iNewH;
        next::f_BgfxHostReset((uint32_t)m_iFbW, (uint32_t)m_iFbH);
        l_bGridChanged = true;
    }
    const auto l_oNow = std::chrono::steady_clock::now();
    const float l_fRealDt = std::chrono::duration<float>(l_oNow - m_oLast).count();
    m_oLast = l_oNow;
    m_fFps = m_fFps > 0.0f ? m_fFps * 0.95f + (l_fRealDt > 0.0f ? 1.0f / l_fRealDt : 0.0f) * 0.05f : 60.0f;
    const float l_fFrameDt = m_oArgs.m_iFixedDtMs > 0 ? m_oArgs.m_iFixedDtMs / 1000.0f : std::min(l_fRealDt, 0.1f);

    float l_fMx = 0, l_fMy = 0;
    const SDL_MouseButtonFlags l_uButtons = SDL_GetMouseState(&l_fMx, &l_fMy);
    next::ImGuiBgfx::Input l_oIn;
    const float l_fPs = SDL_GetWindowPixelDensity(m_pWindow);
    l_oIn.m_fMouseX = l_fMx * l_fPs;
    l_oIn.m_fMouseY = l_fMy * l_fPs;
    l_oIn.m_arrMouseDown[0] = (l_uButtons & SDL_BUTTON_LMASK) != 0;
    l_oIn.m_arrMouseDown[1] = (l_uButtons & SDL_BUTTON_RMASK) != 0;
    l_oIn.m_arrMouseDown[2] = (l_uButtons & SDL_BUTTON_MMASK) != 0;
    l_oIn.m_fWheel = m_fWheel;
    m_fWheel = 0.0f;
    m_oGui.f_NewFrame((uint32_t)m_iFbW, (uint32_t)m_iFbH, std::max(l_fRealDt, 0.001f), l_oIn);
    bool l_bWavesChanged = false;
    if (m_bShowUi) l_bWavesChanged = f_Panel(m_oS, m_bPaused, m_bAuto, m_oRenderer, m_fFps, l_bGridChanged, m_strSettingsPath, m_strNotice, m_iFbW, m_iFbH);
    f_AutoQuality(l_fRealDt, l_bWavesChanged, l_bGridChanged);
    if (l_bWavesChanged) m_oRenderer.f_SetWaves(m_oS.m_oWave, f_CascadesOf(m_oS));
    if (l_bGridChanged) m_oRenderer.f_SetGridForScreen(m_oS.m_iGridRows, (uint32_t)m_iFbW, (uint32_t)m_iFbH);

    const float l_fDt = m_bPaused ? 0.0f : l_fFrameDt * m_oS.m_fTimeScale;
    m_dTime += l_fDt;
    m_fPendingDt += l_fDt;
    // A new wave texture is needed at once after the waves were made again (their pictures are empty until the first step).
    if (l_bWavesChanged || m_iFrame % std::max(m_oS.m_iSimEvery, 1) == 0) {
        m_oRenderer.f_Step(m_dTime, m_fPendingDt);
        m_fPendingDt = 0.0f;
    }
    m_dSumSim += m_oRenderer.f_SimMs();
    m_dSumUpload += m_oRenderer.f_UploadMs();

    Look& l_oLook = m_oS.m_oLook;
    BoatCamera l_oCamera;
    l_oCamera.m_fHeight = m_oS.m_fCameraHeight;
    l_oCamera.m_fPitchDeg = m_oS.m_fCameraPitch;
    l_oCamera.m_fYawDeg = m_oS.m_fCameraYaw;
    l_oCamera.m_fFovYDeg = m_oS.m_fFov;
    l_oCamera.m_fSway = m_oS.m_fCameraSway;
    f_PlaceBoatCamera(l_oLook, l_oCamera, m_dTime, (uint32_t)m_iFbW, (uint32_t)m_iFbH);
    m_oRenderer.f_Draw(g_u16ViewScene, l_oLook, m_dTime, (uint32_t)m_iFbW, (uint32_t)m_iFbH);
    m_oGui.f_Render(g_u16ViewUi);

    if (!m_oArgs.m_strRecord.empty() && m_iFrame % m_oArgs.m_iRecordStep == 0) {
        char l_pszName[64];
        std::snprintf(l_pszName, sizeof(l_pszName), "/frame_%05d.png", m_iShotsAsked);
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, (m_oArgs.m_strRecord + l_pszName).c_str());
        m_iShotsAsked++;
    }
    const bool l_bLast = m_oArgs.m_iFrames > 0 && m_iFrame == m_oArgs.m_iFrames - 1;
    if (l_bLast && !m_oArgs.m_strScreenshot.empty()) {
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, m_oArgs.m_strScreenshot.c_str());
        m_iShotsAsked++;
    }
    next::f_BgfxHostFrame();
    m_iFrame++;
#ifdef __EMSCRIPTEN__
    // For tools/ocean_viewer/web/ocean_web_test.mjs and the browser's console.
    // (No commas outside brackets in here: EM_ASM is a macro.)
    EM_ASM({
        var o = window.tomsOcean || (window.tomsOcean = {});
        o.frames = $0; o.fps = $1; o.simMs = $2; o.uploadMs = $3; o.quality = UTF8ToString($4); o.width = $5; o.height = $6;
    }, m_iFrame, m_fFps, m_oRenderer.f_SimMs(), m_oRenderer.f_UploadMs(), g_arrQualities[m_oS.m_iQuality].m_pszName, m_iFbW, m_iFbH);
#endif
    if (m_oArgs.m_iFrames > 0 && m_iFrame >= m_oArgs.m_iFrames) {   // a test: quit once the pictures are on disk
        if (next::f_BgfxHostScreenshotsWritten() >= m_iShotsAsked || ++m_iWaitFrames > 60) m_bQuit = true;
    }
}

#ifdef __EMSCRIPTEN__
void f_WebFrame(void* l_pApp) { ((App*)l_pApp)->f_Tick(); }

bool f_IsPhone() {
    return EM_ASM_INT({
        if (navigator.userAgentData && navigator.userAgentData.mobile) return 1;
        return (/Android|iPhone|iPad|iPod|Mobile/i.test(navigator.userAgent) || (navigator.maxTouchPoints > 1 && Math.min(screen.width, screen.height) < 900)) ? 1 : 0;
    }) != 0;
}
#else
bool f_IsPhone() { return false; }
#endif

int f_FindQuality(const std::string& l_strName) {
    for (int l_iQ = 0; l_iQ < g_iQualities; l_iQ++)
        if (l_strName == g_arrQualities[l_iQ].m_pszName) return l_iQ;
    return -1;
}

}  // namespace

int main(int l_iArgc, char** l_pArgv) {
    // On the heap: on the web, main returns while the browser keeps calling f_Tick.
    App& l_oApp = *new App;
    l_oApp.m_oArgs = f_ParseArgs(l_iArgc, l_pArgv);
    const Args& l_oArgs = l_oApp.m_oArgs;
    Settings& l_oS = l_oApp.m_oS;
    l_oApp.m_strSettingsPath = f_SettingsPath();
    l_oApp.m_bShowUi = l_oArgs.m_bUi;
    l_oApp.m_dTime = l_oArgs.m_dStartTime;
    if (l_oArgs.m_strPreset.empty()) f_LoadSettings(l_oS, l_oApp.m_strSettingsPath);

    const bool l_bPhone = f_IsPhone();
#ifdef __EMSCRIPTEN__
    const bool l_bWeb = true;
#else
    const bool l_bWeb = false;
#endif
    // Which quality: as asked; else on the desktop the settings as they are, and on the web by the device, stepping down.
    const int l_iAsked = f_FindQuality(l_oArgs.m_strQuality);
    if (l_iAsked >= 0) f_ApplyQuality(l_oS, l_iAsked);
    else if (l_bWeb || l_oArgs.m_strQuality == "auto") {
        f_ApplyQuality(l_oS, l_bWeb ? (l_bPhone ? 2 : 1) : 0);
        l_oApp.m_bAuto = true;
    }
    if (!l_oArgs.m_strPreset.empty()) f_ApplyPreset(l_oS, l_oArgs.m_strPreset);
    l_oS.m_oLook.m_bGammaOut = TOMS_GAMMA_SPACE != 0;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    const bool l_bHidden = !l_oArgs.m_strScreenshot.empty() || !l_oArgs.m_strRecord.empty();
    // A phone's screen has 2-3 device pixels per CSS pixel: at one per CSS pixel, a ninth of the pixels to shade at most.
    const SDL_WindowFlags l_u64Density = l_bPhone ? SDL_WindowFlags(0) : SDL_WINDOW_HIGH_PIXEL_DENSITY;
    l_oApp.m_pWindow = SDL_CreateWindow("TOMS Ocean Viewer", l_oArgs.m_iW, l_oArgs.m_iH, SDL_WINDOW_RESIZABLE | l_u64Density | (l_bHidden ? SDL_WINDOW_HIDDEN : SDL_WindowFlags(0)));
    SDL_Window* l_pWindow = l_oApp.m_pWindow;
    if (!l_pWindow) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    next::BgfxHostConfig l_oCfg;
#if defined(__EMSCRIPTEN__)
    // bgfx's WebGL backend takes the canvas CSS selector as its "window handle".
    static std::string l_strCanvas;
    const char* l_pszId = SDL_GetStringProperty(SDL_GetWindowProperties(l_pWindow), SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, "#canvas");
    l_strCanvas = (l_pszId && l_pszId[0] == '#') ? l_pszId : std::string("#") + (l_pszId ? l_pszId : "canvas");
    l_oCfg.m_pNativeWindow = (void*)l_strCanvas.c_str();
#elif defined(_WIN32)
    l_oCfg.m_pNativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(l_pWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__linux__)
    l_oCfg.m_pNativeWindow = (void*)(uintptr_t)SDL_GetNumberProperty(SDL_GetWindowProperties(l_pWindow), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    l_oCfg.m_pNativeDisplay = SDL_GetPointerProperty(SDL_GetWindowProperties(l_pWindow), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
#endif
    SDL_GetWindowSizeInPixels(l_pWindow, &l_oApp.m_iFbW, &l_oApp.m_iFbH);
    l_oCfg.m_uWidth = (uint32_t)l_oApp.m_iFbW;
    l_oCfg.m_uHeight = (uint32_t)l_oApp.m_iFbH;
    l_oCfg.m_strRenderer = l_oArgs.m_strRenderer;
#ifdef __EMSCRIPTEN__
    l_oCfg.m_iMsaa = 0;   // bgfx's MSAA back buffer on WebGL calls glFramebufferTexture2DMultisampleEXT, which WebGL lacks
#else
    l_oCfg.m_iMsaa = 4;
#endif
    l_oCfg.m_bVsync = !l_bHidden && l_oArgs.m_iFrames == 0;
    std::string l_strErr;
    auto l_fnFail = [&](const std::string& l_strWhy) {
        std::fprintf(stderr, "[ocean] %s\n", l_strWhy.c_str());
#ifdef __EMSCRIPTEN__
        EM_ASM({ if (typeof tomsShowError === 'function') tomsShowError('The ocean could not start', UTF8ToString($0)); }, l_strWhy.c_str());
#else
        if (!l_bHidden) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ocean Viewer", l_strWhy.c_str(), l_pWindow);
#endif
    };
    if (!next::f_BgfxHostInit(l_oCfg, l_strErr)) {
        l_fnFail(l_strErr);
        return 1;
    }
    JobSystem::f_Start();
    if (!l_oApp.m_oRenderer.f_Init(l_strErr) || !l_oApp.m_oGui.f_Init()) {
        l_fnFail(l_strErr.empty() ? "the panel could not start" : l_strErr);
        JobSystem::f_Stop();
        next::f_BgfxHostShutdown();
        return 1;
    }
    const float l_fUiScale = std::max(1.0f, SDL_GetWindowDisplayScale(l_pWindow) * 0.8f);
    ImGui::GetIO().FontGlobalScale = l_fUiScale;
    ImGui::GetStyle().ScaleAllSizes(l_fUiScale);
    l_oApp.m_oRenderer.f_SetWaves(l_oS.m_oWave, f_CascadesOf(l_oS));
    l_oApp.m_oRenderer.f_SetGridForScreen(l_oS.m_iGridRows, (uint32_t)l_oApp.m_iFbW, (uint32_t)l_oApp.m_iFbH);

    if (!l_oArgs.m_strRecord.empty()) {
        std::error_code l_oEc;
        std::filesystem::create_directories(l_oArgs.m_strRecord, l_oEc);
    }
    std::fprintf(stderr, "[ocean] quality %s%s, %d x %d\n", g_arrQualities[l_oS.m_iQuality].m_pszName, l_oApp.m_bAuto ? " (auto)" : "", l_oApp.m_iFbW, l_oApp.m_iFbH);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(f_WebFrame, &l_oApp, 0, false);   // every animation frame; main returns, the page runs on
    return 0;
#else
    while (!l_oApp.m_bQuit) l_oApp.f_Tick();
    if (l_oApp.m_iFrame > 0)
        std::fprintf(stderr, "[ocean] %d frames: waves %.2f ms, pictures %.2f ms a frame, GPU %.2f ms (%d x %d, %d patches of %d)\n", l_oApp.m_iFrame, l_oApp.m_dSumSim / l_oApp.m_iFrame,
                     l_oApp.m_dSumUpload / l_oApp.m_iFrame, next::f_BgfxHostGpuMs(), l_oApp.m_iFbW, l_oApp.m_iFbH, l_oS.m_iCascades, l_oS.m_iResolution);
    l_oApp.m_oRenderer.f_Shutdown();
    l_oApp.m_oGui.f_Shutdown();
    JobSystem::f_Stop();
    next::f_BgfxHostShutdown();
    SDL_DestroyWindow(l_pWindow);
    SDL_Quit();
    delete &l_oApp;
    return 0;
#endif
}
