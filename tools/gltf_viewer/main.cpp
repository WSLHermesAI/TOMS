// gltf_viewer -- a glTF 2.0 model viewer on the engine's renderer (docs/engine/08_GLTF.md), in the spirit of
// https://gltf-viewer.donmccurdy.com/: drop a .gltf / .glb on the window (or File > Open), orbit it,
// play its animations, scrub, change the morph weights, check skins and instancing, light it.
//
//   gltf_viewer [model.gltf|.glb] [options]
//     --renderer=auto|d3d11|d3d12|vulkan|opengl
//     --anim=<index or name>    the animation to play (default: the first; "none" = rest pose)
//     --time=<seconds>          freeze the animations at this time (screenshots)
//     --frames=<n> --screenshot=<png>   save frame n, then quit (tests/CMakeLists.txt: smoke.gltf_*)
//     --yaw=<deg> --pitch=<deg> --zoom=<factor>   the camera (default 35, 20, 1)
//     --size=<w>x<h>            the window (default 1280x800)
//     --no-ui                   hide the panel (screenshots); H toggles it
//     --debug=<0..5>            lit, normals, base colour, metal/rough, uv, shadow of the first light
//     --light=default|point|spot|all|file   the light rig (file = the model's KHR_lights_punctual)
//     --light-hints=1,0,...                 each light's hint (its shapes and markers) shown or not, in the panel's order
//     --spot-aim=<yaw>,<pitch>              the rig's spot lights shine that way (degrees) instead of at the model
//     --light-spin=<deg/s>                  every rig light rotates round the model at that speed
//     --shadows=0|1 --ground=0|1            shadow maps (default on), the floor that catches them
//     --shadow-bias=<x>                     scales the shadow offsets (acne vs. detached shadows)
//     --camera=<index|name>                 look through one of the file's cameras (C cycles them)
//     --look=<yaw>,<pitch>                  ... turned by that much in its own frame (degrees; as a left drag)
//     --crowd=<n>                           crowd mode: n copies of a skinned model, each with its own clip and time,
//                                           drawn by GltfRenderer::drawCrowd from one baked animation texture
//                                           (gltf_anim_texture.h); every third holds a 50 % blend of two clips
//     --crowd-fps=<fps>                     the bake's frames per second (default 30)
//     --physics=0|1                         simulate KHR_physics_rigid_bodies (default 1 when the file has it;
//                                           gltf_physics.h, Jolt). With --time, the scene is simulated up to that time
//     --select=<index|name>                 select a node (the Scene tree, the Node inspector, the view handles)
//     --tool=move|rotate|scale              the handles shown for the selection (T / E / S)
//     --pick=<x>,<y>                        click-pick at that pixel of the window after the first frame (tests)
//     --drag=<x0>,<y0>,<x1>,<y1>            then press on a handle at x0,y0, drag to x1,y1, release; pixels from the
//                                           selected node's origin on screen (tests)
//     --env=<file.hdr|none>                 the environment picture the models reflect and are lit by (image-based
//                                           lighting, gltf_environment.h); default: env.default in resources.json
//     --env-rotation=<deg> --env-intensity=<x>   turn it about +Y, scale it
//     --background=0|1 --background-blur=<0..1>  show it behind the model (default 1, blurred 0.3)
//
// Mouse: left drag orbits, right / middle drag pans, wheel zooms. Keys: Space play/pause, F frame,
// G grid, K skeleton, B bounds, W wireframe, R auto-rotate, H panel, 1..9 solo an animation, Esc quit;
// arrow keys move the camera (left / right / up / down on the screen, while held), + / - change that speed,
// C cycles through the file's cameras (and back to the free orbit camera). On a file camera, left drag
// looks around and the wheel zooms -- still riding the camera's animation (double-click: look ahead again).
// Editing nodes: the Scene tree lists the nodes; click one there, or click a mesh in the view, to select it. The
// Node inspector edits its translation, rotation (degrees) and scale; in the view, drag the handles: T move (axis
// arrows, the centre square moves in the view plane), E rotate (the rings), S scale (axis boxes; the centre square
// scales evenly). Delete resets the selected node. Edits replace the node's animation and restart the physics.
//
// The web build (tools\build_gltf_web.cmd, WebGL2): the options come from the page's URL (?file=morph_cube.gltf&anim=breathe,
// a bare name is one of the test models in /models = tests/gltf); Open... and drag-and-drop go through web/shell.html,
// which writes the chosen files (a .gltf with its .bin and images) into one folder of the in-memory file system.
#include "bgfx_host.h"
#include "color_space.h"   // TOMS_GAMMA_SPACE: the web has no sRGB backbuffer
#include "gltf_environment.h"
#include "gltf_model.h"
#include "gltf_physics.h"
#include "gltf_renderer.h"
#include "imgui_bgfx.h"
#include "res_keys.h"   // toms::key::kEnvDefault: the default environment, by its key
#include "resource.h"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW (utf8Args)
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <filesystem>
#endif

using namespace toms;
using toms::next::GltfRenderer;

namespace {

struct Args {
    std::string m_strFile, m_strScreenshot, m_strRenderer = "auto", m_strAnim;
    int m_iFrames = 0, m_iW = 1280, m_iH = 800, m_iDebug = 0;
    float m_fTime = -1.0f, m_fYaw = 35.0f, m_fPitch = 20.0f, m_fZoom = 1.0f;
    bool m_bUi = true, m_bShadows = true, m_bGround = true;
    std::string m_strLight = "default";
    std::vector<char> m_vLightHints;   // --light-hints
    float m_fLightSpin = 0.0f;         // --light-spin (0 = off)
    bool m_bSpotAim = false;           // --spot-aim
    float m_fSpotAimYaw = 0.0f, m_fSpotAimPitch = -45.0f;
    float m_fShadowBias = 1.0f;
    std::string m_strCamera;
    float m_fLookYaw = 0.0f, m_fLookPitch = 0.0f;
    int m_iCrowd = 0;
    float m_fCrowdFps = 30.0f;
    bool m_bPhysics = true;
    std::string m_strSelect, m_strTool;
    float m_fPickX = -1.0f, m_fPickY = -1.0f;
    glm::vec4 m_v4Drag{-1.0f};
    std::string m_strEnv;                     // --env: an .hdr, "none", or "" = the default
    float m_fEnvRotation = 0.0f, m_fEnvIntensity = 1.0f, m_fBackgroundBlur = 0.3f;
    int m_iBackground = 1;
};

const char* f_Value(const std::string& l_strS, const char* l_pszKey) {
    const size_t l_nN = std::strlen(l_pszKey);
    return l_strS.compare(0, l_nN, l_pszKey) == 0 ? l_strS.c_str() + l_nN : nullptr;
}

Args f_ParseArgs(const std::vector<std::string>& l_vArgv) {
    Args l_oA;
    for (size_t l_nI = 1; l_nI < l_vArgv.size(); l_nI++) {
        const std::string& l_strS = l_vArgv[l_nI];
        if (const char* l_pszV = f_Value(l_strS, "--renderer=")) l_oA.m_strRenderer = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--anim=")) l_oA.m_strAnim = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--time=")) l_oA.m_fTime = (float)std::atof(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--frames=")) l_oA.m_iFrames = std::atoi(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--screenshot=")) l_oA.m_strScreenshot = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--yaw=")) l_oA.m_fYaw = (float)std::atof(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--pitch=")) l_oA.m_fPitch = (float)std::atof(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--zoom=")) l_oA.m_fZoom = (float)std::atof(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--debug=")) l_oA.m_iDebug = std::atoi(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--size=")) std::sscanf(l_pszV, "%dx%d", &l_oA.m_iW, &l_oA.m_iH);
        else if (l_strS == "--no-ui") l_oA.m_bUi = false;
        else if (const char* l_pszV = f_Value(l_strS, "--light=")) l_oA.m_strLight = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--light-spin=")) l_oA.m_fLightSpin = (float)std::atof(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--spot-aim=")) l_oA.m_bSpotAim = std::sscanf(l_pszV, "%f,%f", &l_oA.m_fSpotAimYaw, &l_oA.m_fSpotAimPitch) == 2;
        else if (const char* l_pszV = f_Value(l_strS, "--light-hints=")) {
            for (const char* l_pszC = l_pszV; *l_pszC; l_pszC++)
                if (*l_pszC == '0' || *l_pszC == '1') l_oA.m_vLightHints.push_back(*l_pszC == '1' ? 1 : 0);
        }
        else if (const char* l_pszV = f_Value(l_strS, "--shadows=")) l_oA.m_bShadows = std::atoi(l_pszV) != 0;
        else if (const char* l_pszV = f_Value(l_strS, "--ground=")) l_oA.m_bGround = std::atoi(l_pszV) != 0;
        else if (const char* l_pszV = f_Value(l_strS, "--shadow-bias=")) l_oA.m_fShadowBias = (float)std::atof(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--camera=")) l_oA.m_strCamera = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--look=")) std::sscanf(l_pszV, "%f,%f", &l_oA.m_fLookYaw, &l_oA.m_fLookPitch);
        else if (const char* l_pszV = f_Value(l_strS, "--crowd=")) l_oA.m_iCrowd = std::max(0, std::atoi(l_pszV));
        else if (const char* l_pszV = f_Value(l_strS, "--crowd-fps=")) l_oA.m_fCrowdFps = (float)std::atof(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--physics=")) l_oA.m_bPhysics = std::atoi(l_pszV) != 0;
        else if (const char* l_pszV = f_Value(l_strS, "--select=")) l_oA.m_strSelect = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--tool=")) l_oA.m_strTool = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--pick=")) std::sscanf(l_pszV, "%f,%f", &l_oA.m_fPickX, &l_oA.m_fPickY);
        else if (const char* l_pszV = f_Value(l_strS, "--drag=")) std::sscanf(l_pszV, "%f,%f,%f,%f", &l_oA.m_v4Drag.x, &l_oA.m_v4Drag.y, &l_oA.m_v4Drag.z, &l_oA.m_v4Drag.w);
        else if (const char* l_pszV = f_Value(l_strS, "--env=")) l_oA.m_strEnv = l_pszV;
        else if (const char* l_pszV = f_Value(l_strS, "--env-rotation=")) l_oA.m_fEnvRotation = (float)std::atof(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--env-intensity=")) l_oA.m_fEnvIntensity = (float)std::atof(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--background=")) l_oA.m_iBackground = std::atoi(l_pszV);
        else if (const char* l_pszV = f_Value(l_strS, "--background-blur=")) l_oA.m_fBackgroundBlur = (float)std::atof(l_pszV);
        else if (!l_strS.empty() && l_strS[0] != '-') l_oA.m_strFile = l_strS;
    }
    return l_oA;
}

uint32_t f_Abgr(float l_fR, float l_fG, float l_fB, float l_fA = 1.0f) {
    auto l_fnC = [](float l_fV) { return (uint32_t)std::lround(std::clamp(l_fV, 0.0f, 1.0f) * 255.0f); };
    return l_fnC(l_fR) | l_fnC(l_fG) << 8 | l_fnC(l_fB) << 16 | l_fnC(l_fA) << 24;
}
uint32_t f_RgbaHex(const glm::vec3& l_v3C) {   // bgfx clear colour 0xRRGGBBAA
    auto l_fnB = [](float l_fV) { return (uint32_t)std::lround(std::clamp(l_fV, 0.0f, 1.0f) * 255.0f); };
    return l_fnB(l_v3C.r) << 24 | l_fnB(l_v3C.g) << 16 | l_fnB(l_v3C.b) << 8 | 0xff;
}

struct Viewer {
    Args m_oArgs;
    SDL_Window* m_pWindow = nullptr;
    GltfRenderer m_oRenderer;
    next::ImGuiBgfx m_oGui;
    float m_fUiScale = 1.0f;

    // The model.
    std::unique_ptr<gltf::Model> m_upModel;
    gltf::Pose m_oPose;
    std::shared_ptr<GltfRenderer::GpuModel> m_spGpu;
    std::string m_strPath, m_strError;
    std::mutex m_oPendingLock;
    std::string m_strPending;   // a file chosen in the open dialog (its callback may run on another thread)
    std::vector<std::string> m_vTestModels;   // web: the names in /models (tests/gltf), for the panel's list

    // Camera (orbit around target).
    glm::vec3 m_v3Target{0.0f};
    float m_fYaw = 35.0f, m_fPitch = 20.0f, m_fDistance = 5.0f, m_fFov = 45.0f, m_fRadius = 1.0f;
    glm::vec3 m_v3SceneMin{-1.0f}, m_v3SceneMax{1.0f};

    // Animation.
    std::vector<char> m_vPlaying;
    double m_dTime = 0.0;
    float m_fSpeed = 1.0f;
    bool m_bPaused = false;
    std::map<std::pair<int, int>, float> m_mapManualWeights;   // (node, target) -> weight set by a slider

    // Display.
    GltfRenderer::Frame m_oFrame;
    glm::vec3 m_v3Background{0.20f, 0.21f, 0.23f};
    // The viewer's lights. Directional / spot lights aim by yaw / pitch (where the light comes FROM);
    // point / spot lights sit at a position. Rebuilt around the model when it loads (rigKind).
    struct RigLight {
        GltfRenderer::Light m_oL;
        float m_fYaw = -35.0f, m_fPitch = 45.0f;
        bool m_bHint = true;                 // its shapes and markers in the view (the panel's "show hint")
        // A spot light shines at the model's middle, or (aimAtModel off) where its own aim says: a yaw round the up
        // axis and a pitch above (+) or below (-) the horizon, as fromAngles.
        bool m_bAimAtModel = true;
        float m_fAimYaw = 0.0f, m_fAimPitch = -45.0f;
        // "rotate": it circles the model round the up axis (a directional light turns its yaw), speed in degrees a
        // second (below 0: the other way). A spot light with its own aim turns its aim with it.
        bool m_bSpin = false;
        float m_fSpinSpeed = 30.0f;
    };
    std::vector<RigLight> m_vRig;
    std::string m_strRigKind = "default";
    bool m_bUseFileLights = true;            // the model's KHR_lights_punctual, when it has some
    // The hemisphere ambient (sky / ground) the renderer adds to every light: with the file's own lights only a
    // little of it, so they light the scene (not the viewer's default look); with the rig, the usual amount.
    float m_fAmbientFile = 0.1f, m_fAmbientRig = 0.7f;
    bool f_FileLightsOn() const { return m_bUseFileLights && m_upModel && !m_upModel->m_vLights.empty(); }
    // The floor that catches the shadows (a built-in quad model).
    bool m_bGroundOn = true;
    gltf::Model m_oGroundModel;
    gltf::Pose m_oGroundPose;
    std::shared_ptr<GltfRenderer::GpuModel> m_spGroundGpu;
    // Image-based lighting (gltf_environment.h): an HDR picture of a room that the models reflect and are lit by,
    // instead of the plain sky / ground.
    bool m_bEnvOn = true;
    std::string m_strEnvName, m_strEnvError;       // the picture's file name; why the last one did not load
    float m_fEnvRotation = 0.0f;            // degrees about +Y
    float m_fEnvIntensity = 1.0f;
    bool m_bEnvBackground = true;
    float m_fEnvBlur = 0.3f;
    bool f_EnvActive() const { return m_bEnvOn && m_oRenderer.f_HasEnvironment(); }
    // The lights' gizmos, solid shapes in each light's colour (with the panel only: never in --no-ui screenshots):
    // a glowing ball at a point / spot light with a line down to a mark on the floor under it; a spot light's cones
    // (outer and inner) reaching the floor, their base the lit spot; a directional light's sun and arrow at the
    // model. Unit shapes built in code, placed and coloured per light each frame.
    struct GizmoShape {
        gltf::Model m_oModel;
        gltf::Pose m_oPose;
        std::shared_ptr<GltfRenderer::GpuModel> m_spGpu;
    };
    GizmoShape m_oGizmoSphere, m_oGizmoCone, m_oGizmoCylinder, m_oGizmoDisc;
    std::vector<char> m_vFileLightHint;      // per light of the file (Model::lights), 1 = shown
    std::vector<char> m_vFrameLightHint;     // per light of frame.lights (frameLights fills it)
    bool m_bHintsGiven = false;              // --light-hints applied (once, to the first lights shown)
    bool m_bSpotAimGiven = false;            // --spot-aim applied
    bool m_bLightSpinGiven = false;          // --light-spin applied
    bool m_bGrid = true, m_bAxes = false, m_bSkeleton = false, m_bBoundsBox = false, m_bWireframe = false, m_bAutoRotate = false;
    bool m_bShowUi = true;

    // Frame state.
    int m_iFbW = 0, m_iFbH = 0, m_iFrameNo = 0, m_iShotsBefore = -1;
    uint64_t m_u64LastTicks = 0;
    float m_fFps = 0.0f, m_fFpsAccum = 0.0f;
    int m_iFpsFrames = 0;
    bool m_bDragOrbit = false, m_bDragPan = false;
    // -1 = the free orbit camera, else one of the file's cameras (index into pose.cameras()). Moving
    // the view (drag, wheel, arrows) leaves a file camera for the free one, from the same spot.
    int m_iActiveCamera = -1;
    // Looking around from a file camera, in its own frame (it keeps riding its node's animation):
    // a head turn (yaw about the camera's up), a nod (pitch about its right), a zoom of its lens.
    float m_fLookYaw = 0.0f, m_fLookPitch = 0.0f, m_fLookZoom = 1.0f;
    bool m_bDragLook = false;
    void f_SelectCamera(int l_iI) {
        m_iActiveCamera = l_iI;
        m_fLookYaw = m_fLookPitch = 0.0f;
        m_fLookZoom = 1.0f;
    }
    // Crowd mode (--crowd=N): N copies of the model's first skin, animated from one baked texture.
    int m_iCrowdCount = 0;                   // 0 = off
    std::shared_ptr<gltf::AnimTexture> m_spAnimTex;
    std::shared_ptr<GltfRenderer::AnimTextureGpu> m_spAnimGpu;
    gltf::Crowd m_oCrowd;
    float m_fCrowdBlend = 0.5f;              // the panel's held-blend weight
    double m_dBakeMs = 0.0;
    std::string m_strCrowdError;
    glm::vec3 m_v3ModelMin{-1.0f}, m_v3ModelMax{1.0f};   // one copy's bounds (the crowd's spacing)

    // Bakes the first skinned node's skin and lays N copies out on a square grid. Deterministic: copy i plays
    // clip i % clips from time i x 0.37 s; every third holds a blend of two clips.
    void f_BuildCrowd(int l_iN) {
        m_iCrowdCount = l_iN;
        m_oCrowd.f_Clear();
        m_strCrowdError.clear();
        if (l_iN <= 0 || !m_upModel) return;
        int l_iSkin = -1;
        for (const gltf::Node& l_oNd : m_upModel->m_vNodes)
            if (l_oNd.m_iSkin >= 0 && l_oNd.m_iMesh >= 0) { l_iSkin = l_oNd.m_iSkin; break; }
        if (l_iSkin < 0) { m_strCrowdError = "crowd mode needs a skinned model"; return; }
        if (!m_oRenderer.f_CrowdsSupported()) { m_strCrowdError = "this backend cannot draw crowds (no RGBA32F textures)"; return; }
        if (!m_spAnimTex || m_spAnimTex->m_iSkin != l_iSkin) {
            const uint64_t l_u64T0 = SDL_GetTicksNS();
            auto l_spBaked = std::make_shared<gltf::AnimTexture>();
            std::string l_strErr;
            if (!gltf::f_BakeAnimTexture(*m_upModel, l_iSkin, m_oArgs.m_fCrowdFps, *l_spBaked, &l_strErr)) { m_strCrowdError = l_strErr; return; }
            m_dBakeMs = double(SDL_GetTicksNS() - l_u64T0) * 1e-6;
            m_spAnimTex = l_spBaked;
            m_spAnimGpu = m_oRenderer.f_UploadAnimTexture(m_spAnimTex);
            if (!m_spAnimGpu) { m_strCrowdError = "the animation texture could not be uploaded"; return; }
            std::fprintf(stderr, "[gltf] crowd: baked %d joints x %d frames (%zu clips) in %.1f ms, %.1f KB\n", m_spAnimTex->m_iJoints,
                         m_spAnimTex->m_iRows, m_spAnimTex->m_vClips.size(), m_dBakeMs, m_spAnimTex->f_Bytes() / 1024.0);
        }
        m_oCrowd.f_SetTexture(m_spAnimTex);
        const int l_iClips = (int)m_spAnimTex->m_vClips.size();
        const int l_iSide = (int)std::ceil(std::sqrt((double)l_iN));
        const glm::vec3 l_v3Size = m_v3ModelMax - m_v3ModelMin;
        const float l_fSpacing = std::max({l_v3Size.x, l_v3Size.z, l_v3Size.y * 0.5f, 1e-3f}) * 1.3f;
        for (int l_iI = 0; l_iI < l_iN; l_iI++) {
            const float l_fX = ((l_iI % l_iSide) - (l_iSide - 1) * 0.5f) * l_fSpacing, l_fZ = ((l_iI / l_iSide) - (l_iSide - 1) * 0.5f) * l_fSpacing;
            const int l_iC = m_oCrowd.f_Add(glm::translate(glm::mat4(1.0f), glm::vec3(l_fX, 0.0f, l_fZ)), l_iI % l_iClips, l_iI * 0.37f);
            if (l_iClips > 1 && l_iI % 3 == 2) m_oCrowd.f_SetBlend(l_iC, l_iI % l_iClips, (l_iI + 1) % l_iClips, 0.5f);
        }
        f_FrameScene();
    }

    // ---- node editing: the selected node, its edits (applied after the animations), the view handles ----
    struct NodeEdit {
        glm::vec3 m_v3T{0.0f}, m_v3S{1.0f};
        glm::quat m_qR{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 m_v3Euler{0.0f};             // degrees, what the inspector shows (kept, so it does not flip)
    };
    std::map<int, NodeEdit> m_mapEdits;
    int m_iSelected = -1;
    bool m_bRevealSelected = false;          // open the tree down to the selection (after a pick in the view)
    enum class Tool { Move, Rotate, Scale };
    Tool m_eTool = Tool::Move;
    int m_iHoverHandle = -1;                 // 0..2 an axis, 3 the centre, -1 none
    int m_iDragHandle = -1;
    glm::vec2 m_v2DragFrom{0.0f}, m_v2PressAt{0.0f};
    bool m_bPressPending = false;            // a left press that is a click (pick) unless the mouse moves
    NodeEdit m_oDragStart;
    glm::vec3 m_v3DragOrigin{0.0f}, m_v3DragAxis{0.0f};
    glm::vec2 m_v2DragScreen0{0.0f}, m_v2DragScreen1{0.0f};
    float m_fDragLength = 1.0f;

    NodeEdit& f_EditOf(int l_iN) {
        auto l_itIt = m_mapEdits.find(l_iN);
        if (l_itIt != m_mapEdits.end()) return l_itIt->second;
        NodeEdit l_oE;
        l_oE.m_v3T = m_oPose.f_Translation(l_iN);
        l_oE.m_qR = m_oPose.f_Rotation(l_iN);
        l_oE.m_v3S = m_oPose.f_Scale(l_iN);
        l_oE.m_v3Euler = glm::degrees(glm::eulerAngles(l_oE.m_qR));
        return m_mapEdits[l_iN] = l_oE;
    }
    void f_EditChanged(int l_iN) {   // a body that moved: the simulation starts again from the edited scene
        if (m_bPhysicsOn && m_upModel && m_upModel->m_oPhysics.f_Any()) f_BuildPhysics();
        (void)l_iN;
    }
    static glm::quat f_RotationOf(const glm::mat4& l_mat4M) {
        glm::vec3 l_v3C0(l_mat4M[0]), l_v3C1(l_mat4M[1]), l_v3C2(l_mat4M[2]);
        const float l_fL0 = glm::length(l_v3C0), l_fL1 = glm::length(l_v3C1), l_fL2 = glm::length(l_v3C2);
        return glm::normalize(glm::quat_cast(glm::mat3(l_v3C0 / std::max(l_fL0, 1e-8f), l_v3C1 / std::max(l_fL1, 1e-8f), l_v3C2 / std::max(l_fL2, 1e-8f))));
    }
    glm::mat4 f_ParentWorld(int l_iN) const {
        const int l_iP = m_upModel->m_vNodes[(size_t)l_iN].m_iParent;
        return l_iP >= 0 ? m_oPose.f_World(l_iP) : glm::mat4(1.0f);
    }

    // World <-> backbuffer pixels (x right, y down), with the frame the last tick drew.
    bool f_ToScreen(const glm::vec3& l_v3P, glm::vec2& l_v2Out) const {
        const glm::vec4 l_v4C = m_oFrame.m_mat4Proj * m_oFrame.m_mat4View * glm::vec4(l_v3P, 1.0f);
        if (l_v4C.w <= 1e-6f) return false;
        l_v2Out = {(l_v4C.x / l_v4C.w * 0.5f + 0.5f) * float(m_iFbW), (0.5f - l_v4C.y / l_v4C.w * 0.5f) * float(m_iFbH)};
        return true;
    }
    void f_RayAt(const glm::vec2& l_v2Px, glm::vec3& l_v3Origin, glm::vec3& l_v3Dir) const {
        const glm::mat4 l_mat4Inv = glm::inverse(m_oFrame.m_mat4Proj * m_oFrame.m_mat4View);
        const float l_fX = l_v2Px.x / float(std::max(m_iFbW, 1)) * 2.0f - 1.0f, l_fY = 1.0f - l_v2Px.y / float(std::max(m_iFbH, 1)) * 2.0f;
        const float l_fZNear = bgfx::getCaps()->homogeneousDepth ? -1.0f : 0.0f;
        const glm::vec4 l_v4A = l_mat4Inv * glm::vec4(l_fX, l_fY, l_fZNear, 1.0f), l_v4B = l_mat4Inv * glm::vec4(l_fX, l_fY, 0.5f, 1.0f);
        l_v3Origin = glm::vec3(l_v4A) / l_v4A.w;
        l_v3Dir = glm::normalize(glm::vec3(l_v4B) / l_v4B.w - l_v3Origin);
    }

    // The node whose mesh a pixel hits first (its mesh bounds, instances included), else -1. Clicking again where the
    // selected node is hit picks the next node behind it (a see-through volume over a mesh, a parent's bounds).
    int f_PickNode(const glm::vec2& l_v2Px) const {
        if (!m_upModel) return -1;
        glm::vec3 l_v3O, l_v3D;
        f_RayAt(l_v2Px, l_v3O, l_v3D);
        std::vector<std::pair<float, int>> l_vHits;
        for (size_t l_nN = 0; l_nN < m_upModel->m_vNodes.size(); l_nN++) {
            const gltf::Node& l_oNode = m_upModel->m_vNodes[l_nN];
            if (l_oNode.m_iMesh < 0) continue;
            glm::vec3 l_v3Mn(FLT_MAX), l_v3Mx(-FLT_MAX);
            for (const gltf::Primitive& l_oP : m_upModel->m_vMeshes[(size_t)l_oNode.m_iMesh].m_vPrimitives) {
                l_v3Mn = glm::min(l_v3Mn, l_oP.m_v3BoundsMin);
                l_v3Mx = glm::max(l_v3Mx, l_oP.m_v3BoundsMax);
            }
            if (l_v3Mn.x > l_v3Mx.x) continue;
            const glm::mat4 l_mat4World = l_oNode.m_iSkin >= 0 ? glm::mat4(1.0f) : m_oPose.f_World((int)l_nN);   // skinned: model space
            std::vector<glm::mat4> l_vPlacements;
            if (l_oNode.m_vInstances.empty()) l_vPlacements.push_back(l_mat4World);
            else for (const glm::mat4& l_mat4I : l_oNode.m_vInstances) l_vPlacements.push_back(l_mat4World * l_mat4I);
            for (const glm::mat4& l_mat4M : l_vPlacements) {
                const glm::mat4 l_mat4Inv = glm::inverse(l_mat4M);
                const glm::vec3 l_v3Lo = glm::vec3(l_mat4Inv * glm::vec4(l_v3O, 1.0f)), l_v3Ld = glm::vec3(l_mat4Inv * glm::vec4(l_v3D, 0.0f));
                float l_fT0 = 0.0f, l_fT1 = FLT_MAX;
                bool l_bHit = true;
                for (int l_iK = 0; l_iK < 3 && l_bHit; l_iK++) {
                    if (std::fabs(l_v3Ld[l_iK]) < 1e-12f) { l_bHit = l_v3Lo[l_iK] >= l_v3Mn[l_iK] && l_v3Lo[l_iK] <= l_v3Mx[l_iK]; continue; }
                    float l_fA = (l_v3Mn[l_iK] - l_v3Lo[l_iK]) / l_v3Ld[l_iK], l_fB = (l_v3Mx[l_iK] - l_v3Lo[l_iK]) / l_v3Ld[l_iK];
                    if (l_fA > l_fB) std::swap(l_fA, l_fB);
                    l_fT0 = std::max(l_fT0, l_fA);
                    l_fT1 = std::min(l_fT1, l_fB);
                    l_bHit = l_fT0 <= l_fT1;
                }
                if (l_bHit) { l_vHits.push_back({l_fT0, (int)l_nN}); break; }
            }
        }
        if (l_vHits.empty()) return -1;
        std::sort(l_vHits.begin(), l_vHits.end());
        for (size_t l_nI = 0; l_nI + 1 < l_vHits.size(); l_nI++)
            if (l_vHits[l_nI].second == m_iSelected) return l_vHits[l_nI + 1].second;
        return l_vHits[0].second;
    }

    // The handles of the selected node: where they are on screen. Move and rotate work in world axes, scale in the
    // node's own axes.
    glm::vec3 f_HandleAxis(int l_iK) const {
        if (m_eTool != Tool::Scale) return glm::vec3(l_iK == 0, l_iK == 1, l_iK == 2);
        const glm::mat4& l_mat4W = m_oPose.f_World(m_iSelected);
        const glm::vec3 l_v3A(l_mat4W[l_iK]);
        return glm::length(l_v3A) > 1e-8f ? glm::normalize(l_v3A) : glm::vec3(l_iK == 0, l_iK == 1, l_iK == 2);
    }
    float f_HandleLength(const glm::vec3& l_v3Origin) const { return 0.16f * std::max(glm::length(m_oFrame.m_v3Eye - l_v3Origin), 1e-3f); }
    std::vector<glm::vec3> f_Ring(const glm::vec3& l_v3Origin, const glm::vec3& l_v3Axis, float l_fR) const {
        const glm::vec3 l_v3U = glm::normalize(glm::cross(l_v3Axis, std::fabs(l_v3Axis.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
        const glm::vec3 l_v3V = glm::cross(l_v3Axis, l_v3U);
        std::vector<glm::vec3> l_vPts;
        for (int l_iI = 0; l_iI <= 48; l_iI++) {
            const float l_fA = 6.2831853f * l_iI / 48.0f;
            l_vPts.push_back(l_v3Origin + (l_v3U * std::cos(l_fA) + l_v3V * std::sin(l_fA)) * l_fR);
        }
        return l_vPts;
    }
    static float f_SegmentDistance(const glm::vec2& l_v2P, const glm::vec2& l_v2A, const glm::vec2& l_v2B) {
        const glm::vec2 l_v2Ab = l_v2B - l_v2A;
        const float l_fT = glm::dot(l_v2Ab, l_v2Ab) > 0.0f ? std::clamp(glm::dot(l_v2P - l_v2A, l_v2Ab) / glm::dot(l_v2Ab, l_v2Ab), 0.0f, 1.0f) : 0.0f;
        return glm::length(l_v2P - (l_v2A + l_v2Ab * l_fT));
    }
    int f_HandleAt(const glm::vec2& l_v2Px) const {
        if (m_iSelected < 0 || !m_upModel) return -1;
        const glm::vec3 l_v3Origin(m_oPose.f_World(m_iSelected)[3]);
        const float l_fL = f_HandleLength(l_v3Origin), l_fTol = 8.0f * f_PixelScale();
        glm::vec2 l_v2C;
        if (!f_ToScreen(l_v3Origin, l_v2C)) return -1;
        int l_iBest = -1;
        float l_fBestD = l_fTol;
        if (m_eTool != Tool::Rotate && glm::length(l_v2Px - l_v2C) < 10.0f * f_PixelScale()) return 3;
        for (int l_iK = 0; l_iK < 3; l_iK++) {
            const glm::vec3 l_v3Axis = f_HandleAxis(l_iK);
            float l_fD = FLT_MAX;
            if (m_eTool == Tool::Rotate) {
                const std::vector<glm::vec3> l_vPts = f_Ring(l_v3Origin, l_v3Axis, l_fL);
                for (size_t l_nI = 0; l_nI + 1 < l_vPts.size(); l_nI++) {
                    glm::vec2 l_v2A, l_v2B;
                    if (f_ToScreen(l_vPts[l_nI], l_v2A) && f_ToScreen(l_vPts[l_nI + 1], l_v2B)) l_fD = std::min(l_fD, f_SegmentDistance(l_v2Px, l_v2A, l_v2B));
                }
            } else {
                glm::vec2 l_v2E;
                if (f_ToScreen(l_v3Origin + l_v3Axis * l_fL, l_v2E)) l_fD = f_SegmentDistance(l_v2Px, l_v2C, l_v2E);
            }
            if (l_fD < l_fBestD) { l_fBestD = l_fD; l_iBest = l_iK; }
        }
        return l_iBest;
    }
    void f_StartHandleDrag(int l_iH, const glm::vec2& l_v2Px) {
        m_iDragHandle = l_iH;
        m_v2DragFrom = l_v2Px;
        m_oDragStart = f_EditOf(m_iSelected);
        m_v3DragOrigin = glm::vec3(m_oPose.f_World(m_iSelected)[3]);
        m_fDragLength = f_HandleLength(m_v3DragOrigin);
        m_v3DragAxis = l_iH < 3 ? f_HandleAxis(l_iH) : glm::vec3(0.0f);
        f_ToScreen(m_v3DragOrigin, m_v2DragScreen0);
        if (l_iH < 3) f_ToScreen(m_v3DragOrigin + m_v3DragAxis * m_fDragLength, m_v2DragScreen1);
        else {   // the centre: world units per pixel from the camera's right axis
            const glm::mat4& l_mat4V = m_oFrame.m_mat4View;
            f_ToScreen(m_v3DragOrigin + glm::vec3(l_mat4V[0][0], l_mat4V[1][0], l_mat4V[2][0]) * m_fDragLength, m_v2DragScreen1);
        }
    }
    void f_DragHandleTo(const glm::vec2& l_v2Px) {
        if (m_iDragHandle < 0 || m_iSelected < 0) return;
        NodeEdit& l_oE = f_EditOf(m_iSelected);
        const glm::vec2 l_v2M = l_v2Px - m_v2DragFrom;
        const glm::vec2 l_v2Dir = m_v2DragScreen1 - m_v2DragScreen0;
        const float l_fLen = std::max(glm::length(l_v2Dir), 1.0f);
        const glm::mat4 l_mat4Pw = f_ParentWorld(m_iSelected);
        if (m_eTool == Tool::Move) {
            glm::vec3 l_v3WorldDelta;
            if (m_iDragHandle < 3) l_v3WorldDelta = m_v3DragAxis * (glm::dot(l_v2M, l_v2Dir / l_fLen) / l_fLen * m_fDragLength);
            else {
                const glm::mat4& l_mat4V = m_oFrame.m_mat4View;
                const glm::vec3 l_v3Right(l_mat4V[0][0], l_mat4V[1][0], l_mat4V[2][0]), l_v3Up(l_mat4V[0][1], l_mat4V[1][1], l_mat4V[2][1]);
                l_v3WorldDelta = (l_v3Right * l_v2M.x - l_v3Up * l_v2M.y) * (m_fDragLength / l_fLen);
            }
            l_oE.m_v3T = m_oDragStart.m_v3T + glm::inverse(glm::mat3(l_mat4Pw)) * l_v3WorldDelta;
        } else if (m_eTool == Tool::Rotate && m_iDragHandle < 3) {
            const glm::vec2 l_v2A0 = m_v2DragFrom - m_v2DragScreen0, l_v2A1 = l_v2Px - m_v2DragScreen0;
            float l_fAngle = std::atan2(l_v2A1.y, l_v2A1.x) - std::atan2(l_v2A0.y, l_v2A0.x);
            if (glm::dot(m_v3DragAxis, m_oFrame.m_v3Eye - m_v3DragOrigin) > 0.0f) l_fAngle = -l_fAngle;   // the axis looks at us
            const glm::quat l_qPr = f_RotationOf(l_mat4Pw);
            const glm::quat l_qWorld = glm::angleAxis(l_fAngle, m_v3DragAxis) * (l_qPr * m_oDragStart.m_qR);
            l_oE.m_qR = glm::normalize(glm::inverse(l_qPr) * l_qWorld);
            l_oE.m_v3Euler = glm::degrees(glm::eulerAngles(l_oE.m_qR));
        } else if (m_eTool == Tool::Scale) {
            if (m_iDragHandle < 3) {
                const float l_fF = std::max(0.01f, 1.0f + glm::dot(l_v2M, l_v2Dir / l_fLen) / l_fLen);
                l_oE.m_v3S = m_oDragStart.m_v3S;
                l_oE.m_v3S[m_iDragHandle] *= l_fF;
            } else {
                l_oE.m_v3S = m_oDragStart.m_v3S * std::max(0.01f, 1.0f + (l_v2M.x - l_v2M.y) * 0.005f);
            }
        }
    }
    void f_DrawHandles(std::vector<GltfRenderer::LinePoint>& l_vOut) {
        if (m_iSelected < 0 || !m_upModel || m_iSelected >= (int)m_upModel->m_vNodes.size()) return;
        auto l_fnLine = [&](const glm::vec3& l_v3A, const glm::vec3& l_v3B, uint32_t l_uC) { l_vOut.push_back({l_v3A, l_uC}); l_vOut.push_back({l_v3B, l_uC}); };
        const glm::vec3 l_v3Origin(m_oPose.f_World(m_iSelected)[3]);
        const float l_fL = f_HandleLength(l_v3Origin);
        const int l_iActive = m_iDragHandle >= 0 ? m_iDragHandle : m_iHoverHandle;
        const uint32_t l_arrCols[3] = {f_Abgr(1.0f, 0.3f, 0.3f), f_Abgr(0.35f, 1.0f, 0.35f), f_Abgr(0.35f, 0.55f, 1.0f)};
        const uint32_t l_uHot = f_Abgr(1.0f, 0.9f, 0.2f);
        const glm::mat4& l_mat4V = m_oFrame.m_mat4View;
        const glm::vec3 l_v3Right(l_mat4V[0][0], l_mat4V[1][0], l_mat4V[2][0]), l_v3Up(l_mat4V[0][1], l_mat4V[1][1], l_mat4V[2][1]);
        for (int l_iK = 0; l_iK < 3; l_iK++) {
            const glm::vec3 l_v3Axis = f_HandleAxis(l_iK);
            const uint32_t l_uC = l_iActive == l_iK ? l_uHot : l_arrCols[l_iK];
            if (m_eTool == Tool::Rotate) {
                const std::vector<glm::vec3> l_vPts = f_Ring(l_v3Origin, l_v3Axis, l_fL);
                for (size_t l_nI = 0; l_nI + 1 < l_vPts.size(); l_nI++) l_fnLine(l_vPts[l_nI], l_vPts[l_nI + 1], l_uC);
                continue;
            }
            const glm::vec3 l_v3Tip = l_v3Origin + l_v3Axis * l_fL;
            l_fnLine(l_v3Origin, l_v3Tip, l_uC);
            const float l_fH = l_fL * 0.07f;
            if (m_eTool == Tool::Move) {   // an arrow head
                const glm::vec3 l_v3Side = glm::normalize(glm::cross(l_v3Axis, std::fabs(l_v3Axis.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
                const glm::vec3 l_v3Side2 = glm::cross(l_v3Axis, l_v3Side);
                for (const glm::vec3& l_v3Sd : {l_v3Side, -l_v3Side, l_v3Side2, -l_v3Side2}) l_fnLine(l_v3Tip, l_v3Tip - l_v3Axis * l_fH * 2.0f + l_v3Sd * l_fH, l_uC);
            } else {                    // a small box
                for (const glm::vec3& l_v3A : {l_v3Right, l_v3Up}) {
                    const glm::vec3 l_v3B = l_v3A == l_v3Right ? l_v3Up : l_v3Right;
                    l_fnLine(l_v3Tip + (l_v3A - l_v3B) * l_fH, l_v3Tip + (l_v3A + l_v3B) * l_fH, l_uC);
                    l_fnLine(l_v3Tip - (l_v3A + l_v3B) * l_fH, l_v3Tip + (-l_v3A + l_v3B) * l_fH, l_uC);
                }
            }
        }
        if (m_eTool != Tool::Rotate) {   // the centre square
            const uint32_t l_uC = l_iActive == 3 ? l_uHot : f_Abgr(0.9f, 0.9f, 0.9f);
            const float l_fH = l_fL * 0.06f;
            const glm::vec3 l_arrP[4] = {l_v3Origin + (-l_v3Right - l_v3Up) * l_fH, l_v3Origin + (l_v3Right - l_v3Up) * l_fH, l_v3Origin + (l_v3Right + l_v3Up) * l_fH, l_v3Origin + (-l_v3Right + l_v3Up) * l_fH};
            for (int l_iI = 0; l_iI < 4; l_iI++) l_fnLine(l_arrP[l_iI], l_arrP[(l_iI + 1) % 4], l_uC);
        }
    }

    // ---- the Scene tree and the Node inspector ----
    void f_TreeNode(int l_iN) {
        const gltf::Node& l_oNode = m_upModel->m_vNodes[(size_t)l_iN];
        ImGuiTreeNodeFlags l_iFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (l_oNode.m_vChildren.empty()) l_iFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (l_iN == m_iSelected) l_iFlags |= ImGuiTreeNodeFlags_Selected;
        if (m_bRevealSelected && m_iSelected >= 0) {   // open every ancestor of the selection
            for (int l_iA = m_upModel->m_vNodes[(size_t)m_iSelected].m_iParent; l_iA >= 0; l_iA = m_upModel->m_vNodes[(size_t)l_iA].m_iParent)
                if (l_iA == l_iN) ImGui::SetNextItemOpen(true);
        }
        std::string l_strLabel = l_oNode.m_strName.empty() ? "node " + std::to_string(l_iN) : l_oNode.m_strName;
        if (l_oNode.m_iMesh >= 0) l_strLabel += l_oNode.m_iSkin >= 0 ? "  [skinned mesh]" : "  [mesh]";
        if (l_oNode.m_iCamera >= 0) l_strLabel += "  [camera]";
        if (l_oNode.m_iLight >= 0) l_strLabel += "  [light]";
        if (m_upModel->m_oPhysics.f_Any()) {
            const gltf::NodePhysics& l_oP = m_upModel->m_oPhysics.m_vNodes[(size_t)l_iN];
            if (l_oP.m_bHasMotion) l_strLabel += l_oP.m_bKinematic ? "  [kinematic]" : "  [body]";
            else if (l_oP.m_bHasCollider) l_strLabel += "  [collider]";
            if (l_oP.m_bHasTrigger) l_strLabel += "  [trigger]";
            if (l_oP.m_bHasJoint) l_strLabel += "  [joint]";
        }
        if (m_mapEdits.count(l_iN)) l_strLabel += "  *";
        ImGui::PushID(l_iN);
        const bool l_bOpen = ImGui::TreeNodeEx("##node", l_iFlags, "%s", l_strLabel.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_iSelected = l_iN;
        if (l_iN == m_iSelected && m_bRevealSelected) ImGui::SetScrollHereY(0.5f);
        if (l_bOpen && !l_oNode.m_vChildren.empty()) {
            for (int l_iC : l_oNode.m_vChildren) f_TreeNode(l_iC);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void f_SceneTreePanel() {
        if (ImGui::CollapsingHeader("Scene tree", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("click to select (or click a mesh in the view)");
            const float l_fH = std::min(260.0f * m_fUiScale, ImGui::GetContentRegionAvail().y * 0.5f);
            if (ImGui::BeginChild("tree", ImVec2(0, std::max(l_fH, 80.0f * m_fUiScale)), true)) {
                const std::vector<int>& l_vRoots = m_upModel->m_vScenes.empty() ? std::vector<int>() : m_upModel->m_vScenes[(size_t)std::max(m_upModel->m_iScene, 0)].m_vRoots;
                for (int l_iR : l_vRoots) f_TreeNode(l_iR);
            }
            ImGui::EndChild();
            m_bRevealSelected = false;
        }
        if (m_iSelected < 0 || m_iSelected >= (int)m_upModel->m_vNodes.size()) return;
        if (!ImGui::CollapsingHeader("Node", ImGuiTreeNodeFlags_DefaultOpen)) return;
        const gltf::Node& l_oNode = m_upModel->m_vNodes[(size_t)m_iSelected];
        ImGui::Text("%s  (#%d)", l_oNode.m_strName.empty() ? "(no name)" : l_oNode.m_strName.c_str(), m_iSelected);
        if (l_oNode.m_iParent >= 0) {
            ImGui::SameLine();
            const std::string& l_strPn = m_upModel->m_vNodes[(size_t)l_oNode.m_iParent].m_strName;
            if (ImGui::SmallButton(("parent: " + (l_strPn.empty() ? std::to_string(l_oNode.m_iParent) : l_strPn)).c_str())) { m_iSelected = l_oNode.m_iParent; m_bRevealSelected = true; }
        }
        int l_iT = (int)m_eTool;
        ImGui::RadioButton("move (T)", &l_iT, 0);
        ImGui::SameLine();
        ImGui::RadioButton("rotate (E)", &l_iT, 1);
        ImGui::SameLine();
        ImGui::RadioButton("scale (S)", &l_iT, 2);
        m_eTool = (Tool)l_iT;
        // The fields show the node's local transform now (animated), and edit a copy that replaces it.
        NodeEdit l_oShown;
        const auto l_itIt = m_mapEdits.find(m_iSelected);
        if (l_itIt != m_mapEdits.end()) l_oShown = l_itIt->second;
        else {
            l_oShown.m_v3T = m_oPose.f_Translation(m_iSelected);
            l_oShown.m_qR = m_oPose.f_Rotation(m_iSelected);
            l_oShown.m_v3S = m_oPose.f_Scale(m_iSelected);
            l_oShown.m_v3Euler = glm::degrees(glm::eulerAngles(l_oShown.m_qR));
        }
        const float l_fSpeed = std::max(m_fRadius * 0.005f, 1e-4f);
        bool l_bChanged = false, l_bDone = false;
        if (ImGui::DragFloat3("translation", &l_oShown.m_v3T.x, l_fSpeed, 0, 0, "%.3f")) { f_EditOf(m_iSelected).m_v3T = l_oShown.m_v3T; l_bChanged = true; }
        l_bDone |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::DragFloat3("rotation (deg)", &l_oShown.m_v3Euler.x, 0.5f, 0, 0, "%.1f")) {
            NodeEdit& l_oE = f_EditOf(m_iSelected);
            l_oE.m_v3Euler = l_oShown.m_v3Euler;
            l_oE.m_qR = glm::quat(glm::radians(l_oShown.m_v3Euler));
            l_bChanged = true;
        }
        l_bDone |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::DragFloat3("scale", &l_oShown.m_v3S.x, 0.005f, 0.0f, 0.0f, "%.3f")) { f_EditOf(m_iSelected).m_v3S = l_oShown.m_v3S; l_bChanged = true; }
        l_bDone |= ImGui::IsItemDeactivatedAfterEdit();
        float l_fUniform = 1.0f;
        if (ImGui::DragFloat("uniform scale", &l_fUniform, 0.005f, 0.01f, 100.0f, "x %.3f")) {
            NodeEdit& l_oE = f_EditOf(m_iSelected);
            l_oE.m_v3S *= std::max(l_fUniform, 0.01f);
            l_bChanged = true;
        }
        l_bDone |= ImGui::IsItemDeactivatedAfterEdit();
        const glm::vec3 l_v3Wp(m_oPose.f_World(m_iSelected)[3]);
        ImGui::TextDisabled("world position %.3f, %.3f, %.3f", l_v3Wp.x, l_v3Wp.y, l_v3Wp.z);
        if (m_mapEdits.count(m_iSelected)) {
            if (ImGui::Button("reset node (Delete)")) { m_mapEdits.erase(m_iSelected); l_bDone = true; }
            ImGui::SameLine();
        }
        if (!m_mapEdits.empty() && ImGui::Button(("reset all edits (" + std::to_string(m_mapEdits.size()) + ")").c_str())) {
            m_mapEdits.clear();
            l_bDone = true;
        }
        if (ImGui::Button("look at it")) m_v3Target = l_v3Wp;
        ImGui::SameLine();
        if (ImGui::Button("deselect")) m_iSelected = -1;
        if (l_bChanged) f_Posed();
        if (l_bDone) f_EditChanged(m_iSelected);
    }

    // Physics (KHR_physics_rigid_bodies, gltf_physics.h): the bodies move the pose's nodes after the animations.
    gltf::PhysicsWorld m_oPhysics;
    bool m_bPhysicsOn = false, m_bPhysicsPaused = false, m_bPhysicsBoxes = true, m_bPhysicsFrozenDone = false;
    std::vector<std::string> m_vPhysicsLog;   // the last trigger events

    void f_BuildPhysics() {
        m_oPhysics.f_Clear();
        m_bPhysicsFrozenDone = false;
        m_vPhysicsLog.clear();
        if (!m_upModel || !m_upModel->m_oPhysics.f_Any() || !m_bPhysicsOn) return;
        const double l_dKeep = m_dTime;
        m_dTime = 0.0;
        f_Posed();   // the bodies start where the file (and the animations at time 0) put them
        std::string l_strErr;
        if (!m_oPhysics.f_Build(*m_upModel, m_oPose, &l_strErr)) {
            std::fprintf(stderr, "[gltf] physics: %s\n", l_strErr.c_str());
            return;
        }
        m_dTime = l_dKeep;
        const gltf::PhysicsWorld::Stats l_oSt = m_oPhysics.f_Stats();
        std::fprintf(stderr, "[gltf] physics: %d dynamic, %d kinematic, %d static, %d triggers, %d joints\n", l_oSt.m_iDynamic,
                     l_oSt.m_iKinematic, l_oSt.m_iStatics, l_oSt.m_iTriggers, l_oSt.m_iJoints);
        for (const std::string& l_strW : m_oPhysics.f_Warnings()) std::fprintf(stderr, "[gltf]   physics warning: %s\n", l_strW.c_str());
    }

    // After posed(): advance the simulation (or, with --time, simulate up to that time once) and put the bodies back
    // into the pose the animations just reset.
    void f_Simulate(float l_fDt) {
        if (!m_oPhysics.f_Built()) return;
        if (m_oArgs.m_fTime >= 0.0f) {
            if (!m_bPhysicsFrozenDone) {
                m_oPhysics.f_SimulateTo(m_oArgs.m_fTime, m_oPose);
                m_bPhysicsFrozenDone = true;
            }
        } else if (!m_bPhysicsPaused) {
            m_oPhysics.f_Step(l_fDt * m_fSpeed, m_oPose);
        }
        m_oPhysics.f_WritePose(m_oPose);
        for (const gltf::TriggerEvent& l_oE : m_oPhysics.f_TakeEvents()) {
            auto l_fnName = [&](int l_iN) { return l_iN >= 0 && l_iN < (int)m_upModel->m_vNodes.size() && !m_upModel->m_vNodes[(size_t)l_iN].m_strName.empty()
                                                ? m_upModel->m_vNodes[(size_t)l_iN].m_strName : std::to_string(l_iN); };
            char l_pszLine[160];
            std::snprintf(l_pszLine, sizeof l_pszLine, "%6.2f s  %s %s %s", m_oPhysics.f_Time(), l_fnName(l_oE.m_iOther).c_str(), l_oE.m_bEnter ? "entered" : "left",
                          l_fnName(l_oE.m_iTrigger).c_str());
            m_vPhysicsLog.push_back(l_pszLine);
            if (m_vPhysicsLog.size() > 8) m_vPhysicsLog.erase(m_vPhysicsLog.begin());
        }
    }

    // Arrow keys: the camera moves moveSpeed x the model's radius per second (+ / - change it).
    float m_fMoveSpeed = 1.0f;
    static constexpr float m_fMoveStep = 1.5f, m_fMoveMin = 1.0f / 64.0f, m_fMoveMax = 64.0f;

    float f_MaxDuration() const {
        float l_fD = 0.0f;
        if (m_upModel)
            for (size_t l_nI = 0; l_nI < m_upModel->m_vAnimations.size(); l_nI++)
                if (m_vPlaying[l_nI]) l_fD = std::max(l_fD, m_upModel->m_vAnimations[l_nI].m_fDuration);
        return l_fD;
    }

    // An .hdr: prefiltered on the CPU (about a second for the 1K default), then on the GPU.
    bool f_LoadEnvironment(const std::string& l_strFile) {
        const auto l_oT0 = std::chrono::steady_clock::now();
        gltf::Equirect l_oPicture;
        std::string l_strErr;
        if (!gltf::f_LoadEquirect(l_strFile, l_oPicture, &l_strErr)) {
            m_strEnvError = l_strFile + ": " + l_strErr;
            std::fprintf(stderr, "[gltf] %s\n", m_strEnvError.c_str());
            return false;
        }
        if (!m_oRenderer.f_SetEnvironment(gltf::f_PrefilterEnvironment(l_oPicture, 256, 48), &l_strErr)) {
            m_strEnvError = l_strErr;
            return false;
        }
        const size_t l_nSlash = l_strFile.find_last_of("/\\");
        m_strEnvName = l_nSlash == std::string::npos ? l_strFile : l_strFile.substr(l_nSlash + 1);
        m_strEnvError.clear();
        std::fprintf(stderr, "[gltf] environment %s: %d x %d, prefiltered in %.0f ms\n", m_strEnvName.c_str(), l_oPicture.m_iWidth,
                     l_oPicture.m_iHeight, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l_oT0).count());
        return true;
    }

    // A file from Open..., a drop or the web page: an .hdr is the environment, anything else a model.
    void f_OpenFile(const std::string& l_strFile) {
        const size_t l_nDot = l_strFile.find_last_of('.');
        std::string l_strExt = l_nDot == std::string::npos ? std::string() : l_strFile.substr(l_nDot + 1);
        for (char& l_cC : l_strExt) l_cC = (char)std::tolower((unsigned char)l_cC);
        if (l_strExt == "hdr") {
            if (f_LoadEnvironment(l_strFile)) m_bEnvOn = true;
        } else {
            f_Load(l_strFile);
        }
    }

    bool f_Load(const std::string& l_strFile) {
        auto l_upM = std::make_unique<gltf::Model>();
        std::string l_strErr;
        if (!gltf::f_LoadModel(l_strFile, *l_upM, &l_strErr)) {
            m_strError = l_strFile + ": " + l_strErr;
            std::fprintf(stderr, "[gltf] %s\n", m_strError.c_str());
            return false;
        }
        m_spGpu.reset();   // the old model's buffers go first
        m_oPhysics.f_Clear();   // it points at the old model
        m_mapEdits.clear();
        m_iSelected = -1;
        m_iDragHandle = m_iHoverHandle = -1;
        m_spAnimGpu.reset();
        m_spAnimTex.reset();
        m_oCrowd.f_Clear();
        m_upModel = std::move(l_upM);
        m_strPath = l_strFile;
        m_strError.clear();
        m_oPose.f_Bind(m_upModel.get());
        m_spGpu = m_oRenderer.f_Upload(*m_upModel);
        m_vFileLightHint.assign(m_upModel->m_vLights.size(), 1);   // a new model: every light's hint shown
        m_vPlaying.assign(m_upModel->m_vAnimations.size(), 0);
        m_mapManualWeights.clear();
        m_dTime = 0.0;
        // The animation to play: --anim (index or name), else the first one.
        int l_iPick = m_upModel->m_vAnimations.empty() ? -1 : 0;
        if (!m_oArgs.m_strAnim.empty()) {
            l_iPick = m_oArgs.m_strAnim == "none" ? -1 : -2;
            for (size_t l_nI = 0; l_nI < m_upModel->m_vAnimations.size() && l_iPick == -2; l_nI++)
                if (m_upModel->m_vAnimations[l_nI].m_strName == m_oArgs.m_strAnim || std::to_string(l_nI) == m_oArgs.m_strAnim) l_iPick = (int)l_nI;
            if (l_iPick == -2) l_iPick = -1;
        }
        if (l_iPick >= 0) m_vPlaying[(size_t)l_iPick] = 1;
        std::fprintf(stderr, "[gltf] %s: %zu vertices, %zu triangles, %zu meshes, %zu skins, %zu morph targets, %zu instances, %zu animations\n",
                     l_strFile.c_str(), m_upModel->f_VertexCount(), m_upModel->f_TriangleCount(), m_upModel->m_vMeshes.size(), m_upModel->m_vSkins.size(),
                     m_upModel->f_MorphTargetCount(), m_upModel->f_InstanceCount(), m_upModel->m_vAnimations.size());
        for (const std::string& l_strW : m_upModel->m_vWarnings) std::fprintf(stderr, "[gltf]   warning: %s\n", l_strW.c_str());
        for (const std::string& l_strE : m_upModel->m_vIgnoredExtensions) std::fprintf(stderr, "[gltf]   ignored extension: %s\n", l_strE.c_str());
        m_iActiveCamera = -1;
        if (!m_oArgs.m_strCamera.empty()) {   // --camera=<index|name>
            const std::vector<gltf::PlacedCamera> l_vCams = m_oPose.f_Cameras();
            for (size_t l_nI = 0; l_nI < l_vCams.size(); l_nI++)
                if (std::to_string(l_nI) == m_oArgs.m_strCamera || m_upModel->m_vCameras[(size_t)l_vCams[l_nI].m_iCamera].m_strName == m_oArgs.m_strCamera) {
                    f_SelectCamera((int)l_nI);
                    m_fLookYaw = m_oArgs.m_fLookYaw;
                    m_fLookPitch = m_oArgs.m_fLookPitch;
                    break;
                }
        }
        const std::string l_strTitle = "TOMS glTF Viewer - " + l_strFile;
        SDL_SetWindowTitle(m_pWindow, l_strTitle.c_str());
        f_FrameScene();
        if (m_iCrowdCount > 0 || m_oArgs.m_iCrowd > 0) f_BuildCrowd(m_iCrowdCount > 0 ? m_iCrowdCount : m_oArgs.m_iCrowd);
        m_bPhysicsOn = m_oArgs.m_bPhysics && m_upModel->m_oPhysics.f_Any();
        f_BuildPhysics();
        if (!m_oArgs.m_strSelect.empty())
            for (size_t l_nI = 0; l_nI < m_upModel->m_vNodes.size(); l_nI++)
                if (m_upModel->m_vNodes[l_nI].m_strName == m_oArgs.m_strSelect || std::to_string(l_nI) == m_oArgs.m_strSelect) { m_iSelected = (int)l_nI; m_bRevealSelected = true; break; }
        if (m_oArgs.m_strTool == "rotate") m_eTool = Tool::Rotate;
        else if (m_oArgs.m_strTool == "scale") m_eTool = Tool::Scale;
        return true;
    }

    // Camera on the whole scene: bounds over the playing animations (sampled at a few times, so a
    // morph or a walk does not leave the view), then the pose at the current time again.
    void f_FrameScene() {
        if (!m_upModel) return;
        const double l_dKeep = m_dTime;
        const float l_fDur = f_MaxDuration();
        glm::vec3 l_v3Mn(0.0f), l_v3Mx(0.0f);
        bool l_bAny = false;
        for (int l_iK = 0; l_iK <= (l_fDur > 0 ? 8 : 0); l_iK++) {
            m_dTime = l_fDur * l_iK / 8.0;
            f_Posed();
            glm::vec3 l_v3A, l_v3B;
            if (!m_oPose.f_Bounds(l_v3A, l_v3B)) continue;
            l_v3Mn = l_bAny ? glm::min(l_v3Mn, l_v3A) : l_v3A;
            l_v3Mx = l_bAny ? glm::max(l_v3Mx, l_v3B) : l_v3B;
            l_bAny = true;
        }
        m_dTime = l_dKeep;
        f_Posed();
        if (!l_bAny) l_v3Mn = glm::vec3(-1.0f), l_v3Mx = glm::vec3(1.0f);
        m_v3ModelMin = l_v3Mn;
        m_v3ModelMax = l_v3Mx;
        if (m_iCrowdCount > 0 && m_oCrowd.size() > 0) {   // the whole grid
            for (int l_iI = 0; l_iI < m_oCrowd.size(); l_iI++) {
                const glm::vec3 l_v3O(m_oCrowd[l_iI].m_mat4Placement[3]);
                l_v3Mn = glm::min(l_v3Mn, m_v3ModelMin + l_v3O);
                l_v3Mx = glm::max(l_v3Mx, m_v3ModelMax + l_v3O);
            }
        }
        m_v3SceneMin = l_v3Mn;
        m_v3SceneMax = l_v3Mx;
        m_v3Target = (l_v3Mn + l_v3Mx) * 0.5f;
        m_fRadius = std::max(glm::length(l_v3Mx - l_v3Mn) * 0.5f, 1e-3f);
        f_BuildRig(m_strRigKind);
        m_fDistance = m_fRadius / std::sin(glm::radians(m_fFov * 0.5f)) * 1.05f / std::max(m_oArgs.m_fZoom, 0.01f);
    }

    // The pose at the current time: every ticked animation, then the morph sliders.
    void f_Posed() {
        m_oPose.f_Reset();
        for (size_t l_nI = 0; l_nI < m_upModel->m_vAnimations.size(); l_nI++) {
            if (!m_vPlaying[l_nI]) continue;
            const float l_fD = m_upModel->m_vAnimations[l_nI].m_fDuration;
            m_oPose.f_Apply((int)l_nI, l_fD > 0 ? (float)std::fmod(m_dTime, (double)l_fD) : 0.0f);
        }
        for (const auto& [l_pairKey, l_fW] : m_mapManualWeights) m_oPose.f_SetWeight(l_pairKey.first, (size_t)l_pairKey.second, l_fW);
        for (const auto& [l_iN, l_oE] : m_mapEdits) m_oPose.f_SetLocal(l_iN, l_oE.m_v3T, l_oE.m_qR, l_oE.m_v3S);   // node edits replace the animation
        m_oPose.f_UpdateWorld();
    }

    // ---- lights --------------------------------------------------------------------------------------
    static glm::vec3 f_FromAngles(float l_fYawDeg, float l_fPitchDeg) {   // a unit vector towards (yaw, pitch)
        const float l_fY = glm::radians(l_fYawDeg), l_fP = glm::radians(l_fPitchDeg);
        return glm::vec3(std::cos(l_fP) * std::sin(l_fY), std::sin(l_fP), std::cos(l_fP) * std::cos(l_fY));
    }

    // The rig's rotating lights (RigLight::spin): round the up axis through the model's middle.
    void f_SpinLights(float l_fDt) {
        if (m_oArgs.m_fLightSpin != 0.0f && !m_bLightSpinGiven && m_upModel) {   // --light-spin: every rig light, once
            m_bLightSpinGiven = true;
            for (RigLight& l_oR : m_vRig) { l_oR.m_bSpin = true; l_oR.m_fSpinSpeed = m_oArgs.m_fLightSpin; }
        }
        auto l_fnWrap = [](float l_fDeg) { return std::remainder(l_fDeg, 360.0f); };   // -180 .. 180
        const glm::vec3 l_v3C = (m_v3SceneMin + m_v3SceneMax) * 0.5f;
        for (RigLight& l_oR : m_vRig) {
            if (!l_oR.m_bSpin || l_oR.m_fSpinSpeed == 0.0f) continue;
            const float l_fA = l_oR.m_fSpinSpeed * l_fDt;
            l_oR.m_fYaw = l_fnWrap(l_oR.m_fYaw + l_fA);
            if (l_oR.m_oL.m_eType == GltfRenderer::Light::Type::Directional) continue;
            l_oR.m_oL.m_v3Position = l_v3C + glm::vec3(glm::rotate(glm::mat4(1.0f), glm::radians(l_fA), glm::vec3(0, 1, 0)) *
                                         glm::vec4(l_oR.m_oL.m_v3Position - l_v3C, 0.0f));
            if (!l_oR.m_bAimAtModel) l_oR.m_fAimYaw = l_fnWrap(l_oR.m_fAimYaw + l_fA);
        }
    }

    // A rig around the model's bounds: "default" (key + fill directional), "point", "spot", "all".
    void f_BuildRig(const std::string& l_strKind) {
        m_strRigKind = l_strKind;
        m_vRig.clear();
        const glm::vec3 l_v3C = (m_v3SceneMin + m_v3SceneMax) * 0.5f;
        auto l_fnDirLight = [&](float l_fYawDeg, float l_fPitchDeg, float l_fIntensity, bool l_bShadows) {
            RigLight l_oR;
            l_oR.m_fYaw = l_fYawDeg;
            l_oR.m_fPitch = l_fPitchDeg;
            l_oR.m_oL.m_eType = GltfRenderer::Light::Type::Directional;
            l_oR.m_oL.m_fIntensity = l_fIntensity;
            l_oR.m_oL.m_bCastShadows = l_bShadows;
            l_oR.m_oL.m_v3Color = glm::vec3(1.0f, 0.97f, 0.92f);
            return l_oR;
        };
        auto l_fnPlaced = [&](GltfRenderer::Light::Type l_eType, float l_fYawDeg, float l_fPitchDeg, float l_fDist, glm::vec3 l_v3Color) {
            RigLight l_oR;
            l_oR.m_fYaw = l_fYawDeg;
            l_oR.m_fPitch = l_fPitchDeg;
            l_oR.m_oL.m_eType = l_eType;
            l_oR.m_oL.m_v3Position = l_v3C + f_FromAngles(l_fYawDeg, l_fPitchDeg) * l_fDist;
            l_oR.m_oL.m_fIntensity = 3.5f * l_fDist * l_fDist;   // about the key light's strength at the model
            l_oR.m_oL.m_v3Color = l_v3Color;
            l_oR.m_oL.m_fInnerCone = 0.3f;
            l_oR.m_oL.m_fOuterCone = 0.55f;
            return l_oR;
        };
        const float l_fD = m_fRadius * 2.2f;
        if (l_strKind == "point") {
            m_vRig.push_back(l_fnPlaced(GltfRenderer::Light::Type::Point, -70.0f, 40.0f, l_fD, glm::vec3(1.0f, 0.92f, 0.8f)));
            m_vRig.push_back(l_fnDirLight(150.0f, 25.0f, 0.4f, false));
        } else if (l_strKind == "spot") {
            m_vRig.push_back(l_fnPlaced(GltfRenderer::Light::Type::Spot, -40.0f, 50.0f, l_fD, glm::vec3(0.9f, 0.95f, 1.0f)));
            m_vRig.push_back(l_fnDirLight(150.0f, 25.0f, 0.4f, false));
        } else if (l_strKind == "all") {
            m_vRig.push_back(l_fnDirLight(-35.0f, 45.0f, 1.6f, true));
            m_vRig.push_back(l_fnPlaced(GltfRenderer::Light::Type::Point, 80.0f, 30.0f, l_fD, glm::vec3(1.0f, 0.6f, 0.35f)));
            m_vRig.push_back(l_fnPlaced(GltfRenderer::Light::Type::Spot, -120.0f, 55.0f, l_fD, glm::vec3(0.45f, 0.65f, 1.0f)));
        } else {
            m_vRig.push_back(l_fnDirLight(-35.0f, 45.0f, 3.0f, true));
            m_vRig.push_back(l_fnDirLight(145.0f, 20.0f, 0.75f, false));
        }
    }

    // This frame's lights: the model's own (KHR_lights_punctual) or the rig.
    std::vector<GltfRenderer::Light> f_FrameLights() {
        std::vector<GltfRenderer::Light> l_vOut;
        m_vFrameLightHint.clear();
        if (!m_bHintsGiven && m_upModel && !m_oArgs.m_vLightHints.empty()) {   // --light-hints, in the panel's order
            m_bHintsGiven = true;
            const bool l_bFile = m_bUseFileLights && !m_upModel->m_vLights.empty();
            if (l_bFile) m_vFileLightHint.resize(m_upModel->m_vLights.size(), 1);
            for (size_t l_nI = 0; l_nI < m_oArgs.m_vLightHints.size(); l_nI++) {
                if (l_bFile && l_nI < m_vFileLightHint.size()) m_vFileLightHint[l_nI] = m_oArgs.m_vLightHints[l_nI];
                if (!l_bFile && l_nI < m_vRig.size()) m_vRig[l_nI].m_bHint = m_oArgs.m_vLightHints[l_nI] != 0;
            }
        }
        if (m_oArgs.m_bSpotAim && !m_bSpotAimGiven && m_upModel) {   // --spot-aim: the rig's spot lights, once
            m_bSpotAimGiven = true;
            for (RigLight& l_oR : m_vRig)
                if (l_oR.m_oL.m_eType == GltfRenderer::Light::Type::Spot) {
                    l_oR.m_bAimAtModel = false;
                    l_oR.m_fAimYaw = m_oArgs.m_fSpotAimYaw;
                    l_oR.m_fAimPitch = m_oArgs.m_fSpotAimPitch;
                }
        }
        if (m_bUseFileLights && m_upModel && !m_upModel->m_vLights.empty()) {
            m_vFileLightHint.resize(m_upModel->m_vLights.size(), 1);
            for (const gltf::PlacedLight& l_oP : m_oPose.f_Lights()) {
                const gltf::Light& l_oS = m_upModel->m_vLights[(size_t)l_oP.m_iLight];
                GltfRenderer::Light l_oL;
                l_oL.m_eType = l_oS.m_eType == gltf::Light::Type::Directional ? GltfRenderer::Light::Type::Directional
                       : l_oS.m_eType == gltf::Light::Type::Spot ? GltfRenderer::Light::Type::Spot : GltfRenderer::Light::Type::Point;
                l_oL.m_v3Color = l_oS.m_v3Color;
                l_oL.m_fIntensity = l_oS.m_fIntensity;
                l_oL.m_v3Position = l_oP.m_v3Position;
                l_oL.m_v3Direction = l_oP.m_v3Direction;
                l_oL.m_fRange = l_oS.m_fRange;
                l_oL.m_fInnerCone = l_oS.m_fInnerCone;
                l_oL.m_fOuterCone = l_oS.m_fOuterCone;
                l_vOut.push_back(l_oL);
                m_vFrameLightHint.push_back(m_vFileLightHint[(size_t)l_oP.m_iLight]);
                if ((int)l_vOut.size() == GltfRenderer::m_iMaxLights) break;
            }
            return l_vOut;
        }
        for (RigLight& l_oR : m_vRig) {
            if (l_oR.m_oL.m_eType != GltfRenderer::Light::Type::Point) {   // aim: from (yaw, pitch) at the model
                if (l_oR.m_oL.m_eType == GltfRenderer::Light::Type::Directional) l_oR.m_oL.m_v3Direction = -f_FromAngles(l_oR.m_fYaw, l_oR.m_fPitch);
                else if (!l_oR.m_bAimAtModel) l_oR.m_oL.m_v3Direction = f_FromAngles(l_oR.m_fAimYaw, l_oR.m_fAimPitch);   // its own aim
                else l_oR.m_oL.m_v3Direction = glm::normalize((m_v3SceneMin + m_v3SceneMax) * 0.5f - l_oR.m_oL.m_v3Position);
            }
            l_vOut.push_back(l_oR.m_oL);
            m_vFrameLightHint.push_back(l_oR.m_bHint ? 1 : 0);
        }
        return l_vOut;
    }
    bool f_LightHintShown(size_t l_nI) const { return l_nI >= m_vFrameLightHint.size() || m_vFrameLightHint[l_nI] != 0; }

    void f_MakeGround() {   // a unit quad, scaled under the model each frame
        gltf::Model& l_oG = m_oGroundModel;
        gltf::Primitive l_oP;
        l_oP.m_vPosition = {{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}};
        l_oP.m_vNormal.assign(4, glm::vec3(0, 1, 0));
        l_oP.m_vUv0 = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        l_oP.m_vIndices = {0, 2, 1, 0, 3, 2};
        l_oP.m_iMaterial = 0;
        l_oP.m_v3BoundsMin = {-1, 0, -1};
        l_oP.m_v3BoundsMax = {1, 0, 1};
        gltf::Mesh l_oMesh;
        l_oMesh.m_strName = "ground";
        l_oMesh.m_vPrimitives.push_back(l_oP);
        l_oG.m_vMeshes.push_back(l_oMesh);
        gltf::Material l_oMat;
        l_oMat.m_strName = "ground";
        l_oMat.m_v4BaseColor = glm::vec4(0.5f, 0.5f, 0.52f, 1.0f);
        l_oMat.m_fMetallic = 0.0f;
        l_oMat.m_fRoughness = 0.9f;
        l_oMat.m_bDoubleSided = true;
        l_oG.m_vMaterials.push_back(l_oMat);
        gltf::Node l_oNode;
        l_oNode.m_iMesh = 0;
        l_oG.m_vNodes.push_back(l_oNode);
        l_oG.m_vScenes.push_back({"ground", {0}});
        m_oGroundPose.f_Bind(&m_oGroundModel);
        m_spGroundGpu = m_oRenderer.f_Upload(m_oGroundModel);
    }

    // The gizmos' unit shapes: a sphere (radius 1), a cone and a cylinder along +Y from 0 to 1 (radius 1 at y = 1; the
    // cone's tip at 0), a disc on y = 0. All closed, two-sided, unlit; drawLightGizmos colours and places them.
    void f_MakeGizmos() {
        const int l_iSeg = 24, l_iRings = 12;
        auto l_fnFinish = [this](GizmoShape& l_oS, gltf::Primitive l_oP, const char* l_pszName) {
            l_oP.m_iMaterial = 0;
            l_oP.m_v3BoundsMin = glm::vec3(FLT_MAX);
            l_oP.m_v3BoundsMax = glm::vec3(-FLT_MAX);
            for (const glm::vec3& l_v3V : l_oP.m_vPosition) { l_oP.m_v3BoundsMin = glm::min(l_oP.m_v3BoundsMin, l_v3V); l_oP.m_v3BoundsMax = glm::max(l_oP.m_v3BoundsMax, l_v3V); }
            gltf::Mesh l_oMesh;
            l_oMesh.m_strName = l_pszName;
            l_oMesh.m_vPrimitives.push_back(std::move(l_oP));
            l_oS.m_oModel.m_vMeshes.push_back(std::move(l_oMesh));
            gltf::Material l_oMat;
            l_oMat.m_strName = l_pszName;
            l_oMat.m_bUnlit = true;
            l_oMat.m_bDoubleSided = true;
            l_oMat.m_fMetallic = 0.0f;
            l_oS.m_oModel.m_vMaterials.push_back(l_oMat);
            gltf::Node l_oNode;
            l_oNode.m_iMesh = 0;
            l_oS.m_oModel.m_vNodes.push_back(l_oNode);
            l_oS.m_oModel.m_vScenes.push_back({l_pszName, {0}});
            l_oS.m_oPose.f_Bind(&l_oS.m_oModel);
            l_oS.m_spGpu = m_oRenderer.f_Upload(l_oS.m_oModel);
        };
        auto l_fnRing = [l_iSeg](float l_fA) { return glm::vec2(std::cos(l_fA * 6.2831853f / l_iSeg), std::sin(l_fA * 6.2831853f / l_iSeg)); };
        {   // sphere
            gltf::Primitive l_oP;
            for (int l_iR = 0; l_iR <= l_iRings; l_iR++) {
                const float l_fT = 3.14159265f * l_iR / l_iRings;
                for (int l_iK = 0; l_iK <= l_iSeg; l_iK++) {
                    const glm::vec2 l_v2C = l_fnRing((float)l_iK);
                    const glm::vec3 l_v3V(std::sin(l_fT) * l_v2C.x, std::cos(l_fT), std::sin(l_fT) * l_v2C.y);
                    l_oP.m_vPosition.push_back(l_v3V);
                    l_oP.m_vNormal.push_back(l_v3V);
                }
            }
            for (int l_iR = 0; l_iR < l_iRings; l_iR++)
                for (int l_iK = 0; l_iK < l_iSeg; l_iK++) {
                    const uint32_t l_uA = (uint32_t)(l_iR * (l_iSeg + 1) + l_iK), l_uB = l_uA + l_iSeg + 1;
                    l_oP.m_vIndices.insert(l_oP.m_vIndices.end(), {l_uA, l_uB, l_uA + 1, l_uA + 1, l_uB, l_uB + 1});
                }
            l_fnFinish(m_oGizmoSphere, l_oP, "gizmo_sphere");
        }
        auto l_fnTube = [&](GizmoShape& l_oS, float l_fBottomRadius, const char* l_pszName) {   // y 0 (bottomRadius) .. 1 (radius 1), capped
            gltf::Primitive l_oP;
            for (int l_iK = 0; l_iK <= l_iSeg; l_iK++) {
                const glm::vec2 l_v2C = l_fnRing((float)l_iK);
                l_oP.m_vPosition.push_back(glm::vec3(l_v2C.x * l_fBottomRadius, 0.0f, l_v2C.y * l_fBottomRadius));
                l_oP.m_vPosition.push_back(glm::vec3(l_v2C.x, 1.0f, l_v2C.y));
                l_oP.m_vNormal.push_back(glm::vec3(l_v2C.x, 0.0f, l_v2C.y));
                l_oP.m_vNormal.push_back(glm::vec3(l_v2C.x, 0.0f, l_v2C.y));
            }
            for (int l_iK = 0; l_iK < l_iSeg; l_iK++) {
                const uint32_t l_uA = (uint32_t)(l_iK * 2);
                l_oP.m_vIndices.insert(l_oP.m_vIndices.end(), {l_uA, l_uA + 1, l_uA + 2, l_uA + 2, l_uA + 1, l_uA + 3});
            }
            for (float l_fY : {0.0f, 1.0f}) {   // the caps
                if (l_fY == 0.0f && l_fBottomRadius <= 0.0f) continue;
                const float l_fRr = l_fY == 0.0f ? l_fBottomRadius : 1.0f;
                const uint32_t l_uCentre = (uint32_t)l_oP.m_vPosition.size();
                l_oP.m_vPosition.push_back(glm::vec3(0.0f, l_fY, 0.0f));
                l_oP.m_vNormal.push_back(glm::vec3(0.0f, l_fY == 0.0f ? -1.0f : 1.0f, 0.0f));
                for (int l_iK = 0; l_iK <= l_iSeg; l_iK++) {
                    const glm::vec2 l_v2C = l_fnRing((float)l_iK);
                    l_oP.m_vPosition.push_back(glm::vec3(l_v2C.x * l_fRr, l_fY, l_v2C.y * l_fRr));
                    l_oP.m_vNormal.push_back(glm::vec3(0.0f, l_fY == 0.0f ? -1.0f : 1.0f, 0.0f));
                }
                for (int l_iK = 0; l_iK < l_iSeg; l_iK++) l_oP.m_vIndices.insert(l_oP.m_vIndices.end(), {l_uCentre, l_uCentre + 1 + l_iK, l_uCentre + 2 + l_iK});
            }
            l_fnFinish(l_oS, l_oP, l_pszName);
        };
        l_fnTube(m_oGizmoCone, 0.0f, "gizmo_cone");
        l_fnTube(m_oGizmoCylinder, 1.0f, "gizmo_cylinder");
        {   // disc
            gltf::Primitive l_oP;
            l_oP.m_vPosition.push_back(glm::vec3(0.0f));
            l_oP.m_vNormal.push_back(glm::vec3(0, 1, 0));
            for (int l_iK = 0; l_iK <= l_iSeg; l_iK++) {
                const glm::vec2 l_v2C = l_fnRing((float)l_iK);
                l_oP.m_vPosition.push_back(glm::vec3(l_v2C.x, 0.0f, l_v2C.y));
                l_oP.m_vNormal.push_back(glm::vec3(0, 1, 0));
            }
            for (int l_iK = 0; l_iK < l_iSeg; l_iK++) l_oP.m_vIndices.insert(l_oP.m_vIndices.end(), {0u, (uint32_t)(1 + l_iK), (uint32_t)(2 + l_iK)});
            l_fnFinish(m_oGizmoDisc, l_oP, "gizmo_disc");
        }
    }

    // Each light as solid shapes (see gizmoSphere): where it is, which way it shines and where its light lands.
    void f_DrawLightGizmos() {
        if (!m_bShowUi || !m_oGizmoSphere.m_spGpu) return;
        const float l_fGroundY = m_v3SceneMin.y;
        // a matrix that takes +Y to `d` (any roll), scaled (xz, y), at `at`
        auto l_fnAlong = [](const glm::vec3& l_v3At, glm::vec3 l_v3D, float l_fWidthScale, float l_fLengthScale) {
            l_v3D = glm::normalize(l_v3D);
            const glm::vec3 l_v3Ref = std::fabs(l_v3D.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            const glm::vec3 l_v3X = glm::normalize(glm::cross(l_v3D, l_v3Ref)), l_v3Z = glm::cross(l_v3X, l_v3D);
            glm::mat4 l_mat4M(1.0f);
            l_mat4M[0] = glm::vec4(l_v3X * l_fWidthScale, 0.0f);
            l_mat4M[1] = glm::vec4(l_v3D * l_fLengthScale, 0.0f);
            l_mat4M[2] = glm::vec4(l_v3Z * l_fWidthScale, 0.0f);
            l_mat4M[3] = glm::vec4(l_v3At, 1.0f);
            return l_mat4M;
        };
        auto l_fnPut = [this](GizmoShape& l_oS, const glm::mat4& l_mat4M, const glm::vec3& l_v3Colour, float l_fAlpha) {
            gltf::Material& l_oMat = l_oS.m_oModel.m_vMaterials[0];   // read by draw() as it submits: one shape, any colour
            l_oMat.m_v4BaseColor = glm::vec4(l_v3Colour, l_fAlpha);
            l_oMat.m_eAlphaMode = l_fAlpha < 1.0f ? gltf::AlphaMode::Blend : gltf::AlphaMode::Opaque;
            m_oRenderer.f_Draw(*l_oS.m_spGpu, l_oS.m_oPose, l_mat4M, false);
        };
        auto l_fnBall = [](const glm::vec3& l_v3At, float l_fR) { return glm::scale(glm::translate(glm::mat4(1.0f), l_v3At), glm::vec3(l_fR)); };
        for (size_t l_nLi = 0; l_nLi < m_oFrame.m_vLights.size(); l_nLi++) {
            const GltfRenderer::Light& l_oL = m_oFrame.m_vLights[l_nLi];
            if (!f_LightHintShown(l_nLi)) continue;   // this light's hint is off (its "show hint")
            // its colour at full brightness (a dim or very bright light still shows its hue)
            const float l_fTop = std::max({l_oL.m_v3Color.r, l_oL.m_v3Color.g, l_oL.m_v3Color.b, 1e-3f});
            const glm::vec3 l_v3Col = l_oL.m_v3Color / l_fTop;
            const float l_fS = m_fRadius * 0.05f;
            if (l_oL.m_eType == GltfRenderer::Light::Type::Directional) {
                // the sun up where the light comes from, and a solid arrow from it towards the model
                const glm::vec3 l_v3D = glm::normalize(l_oL.m_v3Direction);
                const glm::vec3 l_v3Tail = m_v3Target - l_v3D * m_fRadius * 1.7f;
                const float l_fShaft = m_fRadius * 0.55f, l_fHead = m_fRadius * 0.2f;
                l_fnPut(m_oGizmoSphere, l_fnBall(l_v3Tail, l_fS * 1.3f), l_v3Col, 1.0f);
                l_fnPut(m_oGizmoSphere, l_fnBall(l_v3Tail, l_fS * 2.6f), l_v3Col, 0.18f);
                l_fnPut(m_oGizmoCylinder, l_fnAlong(l_v3Tail, l_v3D, m_fRadius * 0.02f, l_fShaft), l_v3Col, 1.0f);
                l_fnPut(m_oGizmoCone, l_fnAlong(l_v3Tail + l_v3D * (l_fShaft + l_fHead), -l_v3D, m_fRadius * 0.065f, l_fHead), l_v3Col, 1.0f);
                continue;
            }
            // a point or spot light: the glowing ball, and a mark on the floor under it
            l_fnPut(m_oGizmoSphere, l_fnBall(l_oL.m_v3Position, l_fS), l_v3Col, 1.0f);
            l_fnPut(m_oGizmoSphere, l_fnBall(l_oL.m_v3Position, l_fS * 2.4f), l_v3Col, 0.2f);
            if (l_oL.m_v3Position.y > l_fGroundY) l_fnPut(m_oGizmoDisc, l_fnBall(glm::vec3(l_oL.m_v3Position.x, l_fGroundY + m_fRadius * 0.004f, l_oL.m_v3Position.z), l_fS * 1.6f), l_v3Col, 0.45f);
            if (l_oL.m_eType == GltfRenderer::Light::Type::Point) {
                if (l_oL.m_fRange > 0.0f) l_fnPut(m_oGizmoSphere, l_fnBall(l_oL.m_v3Position, l_oL.m_fRange), l_v3Col, 0.035f);   // how far it reaches
                continue;
            }
            // a spot light's cones, to the floor (or its range): the outer one faint, the inner one brighter
            const glm::vec3 l_v3D = glm::normalize(l_oL.m_v3Direction);
            float l_fLen = l_v3D.y < -0.05f ? (l_oL.m_v3Position.y - l_fGroundY) / -l_v3D.y : m_fRadius * 2.0f;
            if (l_oL.m_fRange > 0.0f) l_fLen = std::min(l_fLen, l_oL.m_fRange);
            l_fLen = std::clamp(l_fLen, m_fRadius * 0.3f, m_fRadius * 6.0f);
            l_fnPut(m_oGizmoCone, l_fnAlong(l_oL.m_v3Position, l_v3D, l_fLen * std::tan(std::min(l_oL.m_fOuterCone, 1.45f)), l_fLen), l_v3Col, 0.12f);
            l_fnPut(m_oGizmoCone, l_fnAlong(l_oL.m_v3Position, l_v3D, l_fLen * std::tan(std::min(l_oL.m_fInnerCone, 1.45f)), l_fLen), l_v3Col, 0.14f);
        }
    }

    // ---- input -----------------------------------------------------------------------------------
    void f_OnEvent(const SDL_Event& l_oE, bool& l_bQuit) {
        const bool l_bUiMouse = m_bShowUi && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
        switch (l_oE.type) {
        case SDL_EVENT_QUIT: l_bQuit = true; break;
        case SDL_EVENT_DROP_FILE:
            if (l_oE.drop.data) f_OpenFile(l_oE.drop.data);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (l_bUiMouse) break;
            if (m_iActiveCamera >= 0 && l_oE.button.button == SDL_BUTTON_LEFT) {   // look around, still on the camera
                if (l_oE.button.clicks >= 2) m_fLookYaw = m_fLookPitch = 0.0f;
                m_bDragLook = true;
                break;
            }
            if (l_oE.button.button == SDL_BUTTON_LEFT) {
                const glm::vec2 l_v2Px(l_oE.button.x * f_PixelScale(), l_oE.button.y * f_PixelScale());
                const int l_iH = f_HandleAt(l_v2Px);
                if (l_iH >= 0) {   // a handle of the selected node: drag it
                    f_StartHandleDrag(l_iH, l_v2Px);
                    break;
                }
                m_bDragOrbit = true;
                m_bPressPending = true;   // a click (no movement) picks a node
                m_v2PressAt = l_v2Px;
            } else {
                m_bDragPan = true;
            }
            f_ToFreeCamera();
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (l_oE.button.button == SDL_BUTTON_LEFT && m_iDragHandle >= 0) {
                m_iDragHandle = -1;
                f_EditChanged(m_iSelected);
            } else if (l_oE.button.button == SDL_BUTTON_LEFT && m_bPressPending) {
                m_iSelected = f_PickNode(m_v2PressAt);
                m_bRevealSelected = m_iSelected >= 0;
            }
            m_bPressPending = false;
            m_bDragOrbit = m_bDragPan = m_bDragLook = false;
            break;
        case SDL_EVENT_MOUSE_MOTION: {
            const glm::vec2 l_v2Px(l_oE.motion.x * f_PixelScale(), l_oE.motion.y * f_PixelScale());
            if (m_iDragHandle >= 0) {
                f_DragHandleTo(l_v2Px);
                f_Posed();
                break;
            }
            if (m_bPressPending && glm::length(l_v2Px - m_v2PressAt) > 4.0f * f_PixelScale()) m_bPressPending = false;
            if (!m_bDragOrbit && !m_bDragPan && !m_bDragLook && !l_bUiMouse) m_iHoverHandle = f_HandleAt(l_v2Px);
            if (m_bDragLook) {   // drag right: look right; drag up: look up
                m_fLookYaw -= l_oE.motion.xrel * 0.25f;
                m_fLookPitch = std::clamp(m_fLookPitch - l_oE.motion.yrel * 0.25f, -89.0f, 89.0f);
            } else if (m_bDragOrbit) {
                m_fYaw -= l_oE.motion.xrel * 0.4f;
                m_fPitch = std::clamp(m_fPitch + l_oE.motion.yrel * 0.4f, -89.0f, 89.0f);
            } else if (m_bDragPan) {
                const glm::mat4 l_mat4V = f_ViewMatrix();
                const glm::vec3 l_v3Right(l_mat4V[0][0], l_mat4V[1][0], l_mat4V[2][0]), l_v3Up(l_mat4V[0][1], l_mat4V[1][1], l_mat4V[2][1]);
                const float l_fK = m_fDistance * std::tan(glm::radians(m_fFov * 0.5f)) * 2.0f / std::max(1, m_iFbH) * f_PixelScale();
                m_v3Target += (-l_oE.motion.xrel * l_v3Right + l_oE.motion.yrel * l_v3Up) * l_fK;
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            if (l_bUiMouse) break;
            if (m_iActiveCamera >= 0) {   // zoom the camera's lens, still on it
                m_fLookZoom = std::clamp(m_fLookZoom * std::pow(0.9f, l_oE.wheel.y), 0.05f, 2.5f);
                break;
            }
            m_fDistance *= std::pow(0.88f, l_oE.wheel.y);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) break;
            switch (l_oE.key.key) {
            case SDLK_ESCAPE: l_bQuit = true; break;
            case SDLK_SPACE: m_bPaused = !m_bPaused; break;
            case SDLK_F:
                m_iActiveCamera = -1;
                f_FrameScene();
                break;
            case SDLK_C: f_CycleCamera(); break;
            case SDLK_G: m_bGrid = !m_bGrid; break;
            case SDLK_K: m_bSkeleton = !m_bSkeleton; break;
            case SDLK_B: m_bBoundsBox = !m_bBoundsBox; break;
            case SDLK_W: m_bWireframe = !m_bWireframe; break;
            case SDLK_R: m_bAutoRotate = !m_bAutoRotate; break;
            case SDLK_H: m_bShowUi = !m_bShowUi; break;
            case SDLK_T: m_eTool = Tool::Move; break;
            case SDLK_E: m_eTool = Tool::Rotate; break;
            case SDLK_S: m_eTool = Tool::Scale; break;
            case SDLK_DELETE:
            case SDLK_BACKSPACE:
                if (m_iSelected >= 0 && m_mapEdits.erase(m_iSelected)) { f_Posed(); f_EditChanged(m_iSelected); }
                break;
            case SDLK_EQUALS:   // the + key (Shift not needed)
            case SDLK_PLUS:
            case SDLK_KP_PLUS: m_fMoveSpeed = std::min(m_fMoveSpeed * m_fMoveStep, m_fMoveMax); break;
            case SDLK_MINUS:
            case SDLK_KP_MINUS: m_fMoveSpeed = std::max(m_fMoveSpeed / m_fMoveStep, m_fMoveMin); break;
            default:
                if (m_upModel && l_oE.key.key >= SDLK_1 && l_oE.key.key <= SDLK_9) {
                    const size_t l_nI = size_t(l_oE.key.key - SDLK_1);
                    if (l_nI < m_vPlaying.size()) {
                        std::fill(m_vPlaying.begin(), m_vPlaying.end(), 0);
                        m_vPlaying[l_nI] = 1;
                        m_dTime = 0.0;
                    }
                }
            }
            break;
        default: break;
        }
    }

    // ---- the file's cameras -----------------------------------------------------------------------
    std::string f_CameraLabel(size_t l_nI, const gltf::PlacedCamera& l_oPc) const {
        const gltf::Camera& l_oC = m_upModel->m_vCameras[(size_t)l_oPc.m_iCamera];
        return std::to_string(l_nI + 1) + ". " + (l_oC.m_strName.empty() ? "camera " + std::to_string(l_oPc.m_iCamera) : l_oC.m_strName) +
               (l_oC.m_bPerspective ? "" : " (orthographic)");
    }

    void f_CycleCamera() {
        const int l_iN = m_upModel ? (int)m_oPose.f_Cameras().size() : 0;
        f_SelectCamera(l_iN == 0 ? -1 : (m_iActiveCamera + 2) % (l_iN + 1) - 1);   // -1, 0 .. n-1, -1, ...
    }

    // The file camera's transform now, with the look-around on top (in the camera's own frame).
    glm::mat4 f_LookedCamera(const gltf::PlacedCamera& l_oPc) const {
        const glm::mat4 l_mat4Turn = glm::rotate(glm::mat4(1.0f), glm::radians(m_fLookYaw), glm::vec3(0, 1, 0));
        return l_oPc.m_mat4World * glm::rotate(l_mat4Turn, glm::radians(m_fLookPitch), glm::vec3(1, 0, 0));
    }

    // Leave a file camera for the free orbit camera, looking from the same place the same way.
    void f_ToFreeCamera() {
        if (m_iActiveCamera < 0) return;
        const std::vector<gltf::PlacedCamera> l_vCams = m_oPose.f_Cameras();
        if (m_iActiveCamera < (int)l_vCams.size()) {
            const glm::mat4 l_mat4W = f_LookedCamera(l_vCams[(size_t)m_iActiveCamera]);
            const glm::vec3 l_v3EyePos(l_mat4W[3]), l_v3Fwd = -glm::normalize(glm::vec3(l_mat4W[2]));
            const glm::vec3 l_v3C = (m_v3SceneMin + m_v3SceneMax) * 0.5f;
            const float l_fD = std::max(glm::dot(l_v3C - l_v3EyePos, l_v3Fwd), m_fRadius * 0.25f);   // orbit around what it looks at
            m_v3Target = l_v3EyePos + l_v3Fwd * l_fD;
            m_fDistance = l_fD;
            m_fPitch = glm::degrees(std::asin(std::clamp(-l_v3Fwd.y, -1.0f, 1.0f)));
            m_fYaw = glm::degrees(std::atan2(-l_v3Fwd.x, -l_v3Fwd.z));
        }
        m_iActiveCamera = -1;
    }

    // Held arrow keys move the camera (and what it looks at) across the screen.
    void f_MoveWithKeys(float l_fDt) {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) return;
        const bool* l_pK = SDL_GetKeyboardState(nullptr);
        const float l_fX = float(l_pK[SDL_SCANCODE_RIGHT]) - float(l_pK[SDL_SCANCODE_LEFT]);
        const float l_fY = float(l_pK[SDL_SCANCODE_UP]) - float(l_pK[SDL_SCANCODE_DOWN]);
        if (l_fX == 0.0f && l_fY == 0.0f) return;
        f_ToFreeCamera();
        const glm::mat4 l_mat4V = f_ViewMatrix();
        const glm::vec3 l_v3Right(l_mat4V[0][0], l_mat4V[1][0], l_mat4V[2][0]), l_v3Up(l_mat4V[0][1], l_mat4V[1][1], l_mat4V[2][1]);
        m_v3Target += (l_v3Right * l_fX + l_v3Up * l_fY) * (m_fMoveSpeed * m_fRadius * std::min(l_fDt, 0.1f));
    }

    float f_PixelScale() const {   // window points -> backbuffer pixels
        int l_iW = 0, l_iH = 0;
        SDL_GetWindowSize(m_pWindow, &l_iW, &l_iH);
        return l_iW > 0 ? float(m_iFbW) / float(l_iW) : 1.0f;
    }

    glm::vec3 f_Eye() const {
        const float l_fY = glm::radians(m_fYaw), l_fP = glm::radians(m_fPitch);
        return m_v3Target + m_fDistance * glm::vec3(std::cos(l_fP) * std::sin(l_fY), std::sin(l_fP), std::cos(l_fP) * std::cos(l_fY));
    }
    glm::mat4 f_ViewMatrix() const { return glm::lookAt(f_Eye(), m_v3Target, glm::vec3(0, 1, 0)); }

    // ---- debug lines -----------------------------------------------------------------------------
    void f_DrawLines() {
        std::vector<GltfRenderer::LinePoint> l_vPts;
        auto l_fnLine = [&](const glm::vec3& l_v3A, const glm::vec3& l_v3B, uint32_t l_uC) {
            l_vPts.push_back({l_v3A, l_uC});
            l_vPts.push_back({l_v3B, l_uC});
        };
        if (m_bGrid) {   // on the floor under the model, 20 cells over its size
            const float l_fStep = std::pow(10.0f, std::floor(std::log10(m_fRadius * 2.0f))) * 0.25f;
            const int l_iN = 20;
            const float l_fY = m_v3SceneMin.y + m_fRadius * 0.001f;   // over the ground quad
            const glm::vec3 l_v3C(std::round(m_v3Target.x / l_fStep) * l_fStep, l_fY, std::round(m_v3Target.z / l_fStep) * l_fStep);
            for (int l_iI = -l_iN; l_iI <= l_iN; l_iI++) {
                const uint32_t l_uCol = l_iI == 0 ? f_Abgr(0.55f, 0.55f, 0.55f, 0.8f) : f_Abgr(0.42f, 0.42f, 0.42f, 0.45f);
                l_fnLine(l_v3C + glm::vec3(l_iI * l_fStep, 0, -l_iN * l_fStep), l_v3C + glm::vec3(l_iI * l_fStep, 0, l_iN * l_fStep), l_uCol);
                l_fnLine(l_v3C + glm::vec3(-l_iN * l_fStep, 0, l_iI * l_fStep), l_v3C + glm::vec3(l_iN * l_fStep, 0, l_iI * l_fStep), l_uCol);
            }
        }
        if (m_bAxes) {
            const float l_fL = m_fRadius * 0.6f;
            l_fnLine(glm::vec3(0), glm::vec3(l_fL, 0, 0), f_Abgr(1, 0.25f, 0.25f));
            l_fnLine(glm::vec3(0), glm::vec3(0, l_fL, 0), f_Abgr(0.3f, 1, 0.3f));
            l_fnLine(glm::vec3(0), glm::vec3(0, 0, l_fL), f_Abgr(0.3f, 0.5f, 1));
        }
        if (m_bBoundsBox && m_upModel) {
            glm::vec3 l_v3Mn, l_v3Mx;
            if (m_oPose.f_Bounds(l_v3Mn, l_v3Mx))
                for (int l_iA = 0; l_iA < 3; l_iA++)
                    for (int l_iC = 0; l_iC < 4; l_iC++) {
                        glm::vec3 l_v3P0, l_v3P1;
                        for (int l_iK = 0, l_iBit = 0; l_iK < 3; l_iK++) {
                            if (l_iK == l_iA) {
                                l_v3P0[l_iK] = l_v3Mn[l_iK];
                                l_v3P1[l_iK] = l_v3Mx[l_iK];
                            } else {
                                l_v3P0[l_iK] = l_v3P1[l_iK] = ((l_iC >> l_iBit) & 1) ? l_v3Mx[l_iK] : l_v3Mn[l_iK];
                                l_iBit++;
                            }
                        }
                        l_fnLine(l_v3P0, l_v3P1, f_Abgr(1, 0.85f, 0.2f));
                    }
        }
        // Physics: a box around every body's collider (grey static, blue kinematic, green dynamic -- dark when asleep --,
        // yellow trigger).
        if (m_bShowUi && m_bPhysicsBoxes && m_oPhysics.f_Built()) {
            std::vector<gltf::PhysicsDebugBox> l_vBoxes;
            m_oPhysics.f_DebugBoxes(l_vBoxes);
            for (const gltf::PhysicsDebugBox& l_oB : l_vBoxes) {
                using K = gltf::PhysicsDebugBox::Kind;
                const uint32_t l_uCol = l_oB.m_eKind == K::Static ? f_Abgr(0.7f, 0.7f, 0.7f, 0.8f)
                                   : l_oB.m_eKind == K::Kinematic ? f_Abgr(0.35f, 0.6f, 1.0f)
                                   : l_oB.m_eKind == K::Trigger ? f_Abgr(1.0f, 0.85f, 0.2f)
                                   : l_oB.m_bSleeping ? f_Abgr(0.2f, 0.5f, 0.25f) : f_Abgr(0.35f, 1.0f, 0.4f);
                glm::vec3 l_arrC[8];
                for (int l_iK = 0; l_iK < 8; l_iK++)
                    l_arrC[l_iK] = glm::vec3(l_oB.m_mat4World * glm::vec4((l_iK & 1 ? 1.0f : -1.0f) * l_oB.m_v3HalfExtents.x, (l_iK & 2 ? 1.0f : -1.0f) * l_oB.m_v3HalfExtents.y,
                                                         (l_iK & 4 ? 1.0f : -1.0f) * l_oB.m_v3HalfExtents.z, 1.0f));
                for (int l_iK = 0; l_iK < 8; l_iK++)
                    for (int l_iBit : {1, 2, 4})
                        if (!(l_iK & l_iBit)) l_fnLine(l_arrC[l_iK], l_arrC[l_iK | l_iBit], l_uCol);
            }
        }
        // The lights: a directional light's arrow, a point light's star, a spot light's cone (the solid shapes are
        // drawLightGizmos'); a point or spot light's line down to the floor.
        if (m_bShowUi)
            for (size_t l_nLi = 0; l_nLi < m_oFrame.m_vLights.size(); l_nLi++) {
                const GltfRenderer::Light& l_oL = m_oFrame.m_vLights[l_nLi];
                if (!f_LightHintShown(l_nLi)) continue;   // this light's hint is off: no markers either
                if (l_oL.m_eType != GltfRenderer::Light::Type::Directional && l_oL.m_v3Position.y > m_v3SceneMin.y)
                    l_fnLine(l_oL.m_v3Position, glm::vec3(l_oL.m_v3Position.x, m_v3SceneMin.y, l_oL.m_v3Position.z),
                         f_Abgr(std::min(l_oL.m_v3Color.r, 1.0f), std::min(l_oL.m_v3Color.g, 1.0f), std::min(l_oL.m_v3Color.b, 1.0f), 0.5f));
                const uint32_t l_uCol = f_Abgr(std::min(l_oL.m_v3Color.r, 1.0f), std::min(l_oL.m_v3Color.g, 1.0f), std::min(l_oL.m_v3Color.b, 1.0f), 0.9f);
                const float l_fS = m_fRadius * 0.12f;
                if (l_oL.m_eType == GltfRenderer::Light::Type::Directional) {
                    const glm::vec3 l_v3A = m_v3Target - l_oL.m_v3Direction * m_fRadius * 1.6f, l_v3B = m_v3Target - l_oL.m_v3Direction * m_fRadius * 1.1f;
                    l_fnLine(l_v3A, l_v3B, l_uCol);
                    const glm::vec3 l_v3Side = glm::normalize(glm::cross(l_oL.m_v3Direction, std::fabs(l_oL.m_v3Direction.y) > 0.9f ? glm::vec3(1, 0, 0)
                                                                                                                 : glm::vec3(0, 1, 0)));
                    l_fnLine(l_v3B, l_v3B - l_oL.m_v3Direction * l_fS * -1.0f + l_v3Side * l_fS * 0.5f, l_uCol);
                    l_fnLine(l_v3B, l_v3B - l_oL.m_v3Direction * l_fS * -1.0f - l_v3Side * l_fS * 0.5f, l_uCol);
                } else {
                    for (const glm::vec3& l_v3Ax : {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)})
                        l_fnLine(l_oL.m_v3Position - l_v3Ax * l_fS, l_oL.m_v3Position + l_v3Ax * l_fS, l_uCol);
                    if (l_oL.m_eType == GltfRenderer::Light::Type::Spot) {
                        const glm::vec3 l_v3D = glm::normalize(l_oL.m_v3Direction);
                        const glm::vec3 l_v3U = glm::normalize(glm::cross(l_v3D, std::fabs(l_v3D.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
                        const glm::vec3 l_v3W = glm::cross(l_v3D, l_v3U);
                        const float l_fLen = m_fRadius * 0.8f, l_fRr = l_fLen * std::tan(l_oL.m_fOuterCone);
                        for (int l_iK = 0; l_iK < 8; l_iK++) {
                            const float l_fA0 = 6.2831853f * l_iK / 8, l_fA1 = 6.2831853f * (l_iK + 1) / 8;
                            const glm::vec3 l_v3P0 = l_oL.m_v3Position + l_v3D * l_fLen + (l_v3U * std::cos(l_fA0) + l_v3W * std::sin(l_fA0)) * l_fRr;
                            const glm::vec3 l_v3P1 = l_oL.m_v3Position + l_v3D * l_fLen + (l_v3U * std::cos(l_fA1) + l_v3W * std::sin(l_fA1)) * l_fRr;
                            l_fnLine(l_v3P0, l_v3P1, l_uCol);
                            if (l_iK % 2 == 0) l_fnLine(l_oL.m_v3Position, l_v3P0, l_uCol);
                        }
                    }
                }
            }
        m_oRenderer.f_Lines(l_vPts, true);
        if (m_iSelected >= 0 && m_upModel) {   // the selection: its mesh bounds, then the handles over everything
            std::vector<GltfRenderer::LinePoint> l_vOver;
            const gltf::Node& l_oNode = m_upModel->m_vNodes[(size_t)m_iSelected];
            if (l_oNode.m_iMesh >= 0 && l_oNode.m_iSkin < 0) {
                glm::vec3 l_v3Mn(FLT_MAX), l_v3Mx(-FLT_MAX);
                for (const gltf::Primitive& l_oP : m_upModel->m_vMeshes[(size_t)l_oNode.m_iMesh].m_vPrimitives) {
                    l_v3Mn = glm::min(l_v3Mn, l_oP.m_v3BoundsMin);
                    l_v3Mx = glm::max(l_v3Mx, l_oP.m_v3BoundsMax);
                }
                const glm::mat4& l_mat4W = m_oPose.f_World(m_iSelected);
                glm::vec3 l_arrC[8];
                for (int l_iK = 0; l_iK < 8; l_iK++)
                    l_arrC[l_iK] = glm::vec3(l_mat4W * glm::vec4(l_iK & 1 ? l_v3Mx.x : l_v3Mn.x, l_iK & 2 ? l_v3Mx.y : l_v3Mn.y, l_iK & 4 ? l_v3Mx.z : l_v3Mn.z, 1.0f));
                const uint32_t l_uCol = f_Abgr(1.0f, 0.6f, 0.1f, 0.9f);
                for (int l_iK = 0; l_iK < 8; l_iK++)
                    for (int l_iBit : {1, 2, 4})
                        if (!(l_iK & l_iBit)) { l_vOver.push_back({l_arrC[l_iK], l_uCol}); l_vOver.push_back({l_arrC[l_iK | l_iBit], l_uCol}); }
            }
            f_DrawHandles(l_vOver);
            if (!l_vOver.empty()) m_oRenderer.f_Lines(l_vOver, false);
        }
        if (m_bSkeleton && m_upModel) {   // over the model: joints to their parent joints
            std::vector<GltfRenderer::LinePoint> l_vBones;
            for (const gltf::Skin& l_oS : m_upModel->m_vSkins)
                for (int l_iJ : l_oS.m_vJoints) {
                    const int l_iParent = m_upModel->m_vNodes[(size_t)l_iJ].m_iParent;
                    if (l_iParent < 0 || std::find(l_oS.m_vJoints.begin(), l_oS.m_vJoints.end(), l_iParent) == l_oS.m_vJoints.end()) continue;
                    l_vBones.push_back({glm::vec3(m_oPose.f_World(l_iParent)[3]), f_Abgr(0.3f, 1.0f, 0.9f)});
                    l_vBones.push_back({glm::vec3(m_oPose.f_World(l_iJ)[3]), f_Abgr(1.0f, 0.4f, 0.9f)});
                }
            m_oRenderer.f_Lines(l_vBones, false);
        }
    }

    // ---- the panel -------------------------------------------------------------------------------
    void f_Panel() {
        const ImGuiIO& l_oIo = ImGui::GetIO();
        const float l_fW = std::min(380.0f * m_fUiScale, l_oIo.DisplaySize.x * 0.34f);
        ImGui::SetNextWindowPos(ImVec2(l_oIo.DisplaySize.x - l_fW - 8 * m_fUiScale, 8 * m_fUiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(l_fW, l_oIo.DisplaySize.y - 16 * m_fUiScale), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        ImGui::Begin("glTF Viewer", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

#ifdef __EMSCRIPTEN__
        if (ImGui::Button("Open...")) EM_ASM({ if (window.tomsPickFiles) tomsPickFiles(); });   // web/shell.html
        if (!m_vTestModels.empty()) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            if (ImGui::BeginCombo("##test", "Test models")) {
                for (const std::string& l_strF : m_vTestModels)
                    if (ImGui::Selectable(l_strF.c_str(), m_strPath == "/models/" + l_strF)) f_Load("/models/" + l_strF);
                ImGui::EndCombo();
            }
        }
#else
        if (ImGui::Button("Open...")) {
            static const SDL_DialogFileFilter l_arrFilters[] = {{"glTF 2.0 / HDR environment", "gltf;glb;hdr"}};
            SDL_ShowOpenFileDialog(
                [](void* l_pUser, const char* const* l_pList, int) {
                    if (!l_pList || !l_pList[0]) return;
                    Viewer* l_pV = (Viewer*)l_pUser;
                    std::lock_guard<std::mutex> l_oG(l_pV->m_oPendingLock);
                    l_pV->m_strPending = l_pList[0];
                },
                this, m_pWindow, l_arrFilters, 1, nullptr, false);
        }
#endif
        ImGui::SameLine();
        if (ImGui::Button("Frame (F)")) f_FrameScene();
        ImGui::TextDisabled("or drop a .gltf / .glb (or an .hdr environment)");
        if (!m_strError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_strError.c_str());
        if (!m_upModel) {
            ImGui::TextWrapped("No model. Drop a .gltf or .glb on the window.");
            ImGui::End();
            return;
        }
        ImGui::TextWrapped("%s", m_strPath.c_str());

        if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen)) {
            const gltf::Model& l_oM = *m_upModel;
            size_t l_nJoints = 0;
            for (const gltf::Skin& l_oS : l_oM.m_vSkins) l_nJoints += l_oS.m_vJoints.size();
            ImGui::TextWrapped("%zu vertices, %zu triangles", l_oM.f_VertexCount(), l_oM.f_TriangleCount());
            ImGui::TextWrapped("%zu nodes, %zu meshes, %zu materials, %zu textures", l_oM.m_vNodes.size(), l_oM.m_vMeshes.size(), l_oM.m_vMaterials.size(),
                        l_oM.m_vTextures.size());
            ImGui::TextWrapped("%zu skins (%zu joints), %zu morph targets", l_oM.m_vSkins.size(), l_nJoints, l_oM.f_MorphTargetCount());
            ImGui::TextWrapped("%zu instances (EXT_mesh_gpu_instancing)", l_oM.f_InstanceCount());
            if (!l_oM.m_strGenerator.empty()) ImGui::TextWrapped("generator: %s", l_oM.m_strGenerator.c_str());
            for (const std::string& l_strE : l_oM.m_vExtensionsUsed) {
                const bool l_bIgnored = std::find(l_oM.m_vIgnoredExtensions.begin(), l_oM.m_vIgnoredExtensions.end(), l_strE) != l_oM.m_vIgnoredExtensions.end();
                ImGui::TextColored(l_bIgnored ? ImVec4(1, 0.75f, 0.3f, 1) : ImVec4(0.5f, 0.9f, 0.5f, 1), "%s %s", l_bIgnored ? "-" : "+", l_strE.c_str());
                if (l_bIgnored && ImGui::IsItemHovered()) ImGui::SetTooltip("used by the file, not drawn by this engine yet");
            }
            for (const std::string& l_strWn : l_oM.m_vWarnings) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "! %s", l_strWn.c_str());
        }

        {
            const std::vector<gltf::PlacedCamera> l_vCams = m_oPose.f_Cameras();
            if (!l_vCams.empty() && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                const std::string l_strCurrent = m_iActiveCamera >= 0 && m_iActiveCamera < (int)l_vCams.size()
                                                ? f_CameraLabel((size_t)m_iActiveCamera, l_vCams[(size_t)m_iActiveCamera])
                                                : std::string("free (orbit)");
                if (ImGui::BeginCombo("camera", l_strCurrent.c_str())) {
                    if (ImGui::Selectable("free (orbit)", m_iActiveCamera < 0)) f_SelectCamera(-1);
                    for (size_t l_nI = 0; l_nI < l_vCams.size(); l_nI++)
                        if (ImGui::Selectable(f_CameraLabel(l_nI, l_vCams[l_nI]).c_str(), m_iActiveCamera == (int)l_nI)) f_SelectCamera((int)l_nI);
                    ImGui::EndCombo();
                }
                // One button per view: the free camera and each of the file's cameras (the one in use lit), and
                // previous / next through them.
                const int l_iN = (int)l_vCams.size();
                if (ImGui::ArrowButton("##camprev", ImGuiDir_Left)) f_SelectCamera(m_iActiveCamera <= -1 ? l_iN - 1 : m_iActiveCamera - 1);
                ImGui::SameLine();
                if (ImGui::ArrowButton("##camnext", ImGuiDir_Right)) f_CycleCamera();
                const float l_fRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                for (int l_iI = -1; l_iI < l_iN; l_iI++) {
                    const gltf::Camera* l_pC = l_iI >= 0 ? &m_upModel->m_vCameras[(size_t)l_vCams[(size_t)l_iI].m_iCamera] : nullptr;
                    const std::string l_strLabel = l_iI < 0 ? std::string("free")
                                            : std::to_string(l_iI + 1) + " " + (l_pC->m_strName.empty() ? "camera" : l_pC->m_strName);
                    const float l_fW = ImGui::CalcTextSize(l_strLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
                    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + l_fW < l_fRight) ImGui::SameLine();
                    const bool l_bOn = m_iActiveCamera == l_iI;
                    if (l_bOn) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button((l_strLabel + "##cam" + std::to_string(l_iI)).c_str())) f_SelectCamera(l_iI);
                    if (l_bOn) ImGui::PopStyleColor();
                    if (l_pC && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", l_pC->m_bPerspective ? "perspective" : "orthographic");
                }
                ImGui::TextWrapped("%zu in the file. C cycles. On a camera: left drag looks around, the wheel zooms "
                                   "(it keeps riding the animation); right drag or the arrows go free from there.", l_vCams.size());
                if (m_iActiveCamera >= 0 && m_iActiveCamera < (int)l_vCams.size()) {
                    const gltf::Camera& l_oC = m_upModel->m_vCameras[(size_t)l_vCams[(size_t)m_iActiveCamera].m_iCamera];
                    ImGui::Text("look: %.0f deg right, %.0f deg up, zoom %.2fx", -m_fLookYaw, m_fLookPitch, 1.0f / m_fLookZoom);
                    if (ImGui::SmallButton("look ahead (double-click)")) {
                        m_fLookYaw = m_fLookPitch = 0.0f;
                        m_fLookZoom = 1.0f;
                    }
                    if (l_oC.m_bPerspective)
                        ImGui::TextDisabled("perspective, fov %.0f deg, near %g, far %s", glm::degrees(l_oC.m_fYfov), l_oC.m_fZnear,
                                            l_oC.m_fZfar > 0 ? std::to_string(l_oC.m_fZfar).c_str() : "infinite");
                    else
                        ImGui::TextDisabled("orthographic, %g x %g, near %g, far %g", l_oC.m_fXmag * 2, l_oC.m_fYmag * 2, l_oC.m_fZnear, l_oC.m_fZfar);
                }
            }
        }

        f_SceneTreePanel();

        if (!m_upModel->m_vAnimations.empty() && ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button(m_bPaused ? "Play" : "Pause")) m_bPaused = !m_bPaused;
            ImGui::SameLine();
            if (ImGui::Button("All")) std::fill(m_vPlaying.begin(), m_vPlaying.end(), 1);
            ImGui::SameLine();
            if (ImGui::Button("None")) std::fill(m_vPlaying.begin(), m_vPlaying.end(), 0);
            ImGui::SliderFloat("speed", &m_fSpeed, 0.0f, 3.0f, "%.2fx");
            float l_fT = (float)std::fmod(m_dTime, std::max(f_MaxDuration(), 1e-6f));
            if (ImGui::SliderFloat("time", &l_fT, 0.0f, std::max(f_MaxDuration(), 0.001f), "%.3f s")) {
                m_dTime = l_fT;
                m_bPaused = true;
            }
            for (size_t l_nI = 0; l_nI < m_upModel->m_vAnimations.size(); l_nI++) {
                const gltf::Animation& l_oA = m_upModel->m_vAnimations[l_nI];
                bool l_bOn = m_vPlaying[l_nI] != 0;
                const std::string l_strLabel = std::to_string(l_nI + 1) + ". " + l_oA.m_strName + "##anim" + std::to_string(l_nI);
                if (ImGui::Checkbox(l_strLabel.c_str(), &l_bOn)) m_vPlaying[l_nI] = l_bOn;
                ImGui::SameLine();
                ImGui::TextDisabled("%.2f s, %zu channels", l_oA.m_fDuration, l_oA.m_vChannels.size());
            }
        }

        if (m_upModel->f_MorphTargetCount() > 0 && ImGui::CollapsingHeader("Morph targets", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("A slider overrides the animation for that weight.");
            for (size_t l_nN = 0; l_nN < m_upModel->m_vNodes.size(); l_nN++) {
                const gltf::Node& l_oNode = m_upModel->m_vNodes[l_nN];
                if (l_oNode.m_iMesh < 0) continue;
                const gltf::Mesh& l_oMesh = m_upModel->m_vMeshes[(size_t)l_oNode.m_iMesh];
                if (l_oMesh.m_vWeights.empty()) continue;
                ImGui::Text("%s", l_oNode.m_strName.empty() ? l_oMesh.m_strName.c_str() : l_oNode.m_strName.c_str());
                for (size_t l_nK = 0; l_nK < l_oMesh.m_vWeights.size(); l_nK++) {
                    float l_fWv = m_oPose.f_Weights((int)l_nN)[l_nK];
                    const std::string l_strName = (l_nK < l_oMesh.m_vTargetNames.size() ? l_oMesh.m_vTargetNames[l_nK] : "target " + std::to_string(l_nK)) + "##w" +
                                             std::to_string(l_nN) + "_" + std::to_string(l_nK);
                    if (ImGui::SliderFloat(l_strName.c_str(), &l_fWv, 0.0f, 1.0f)) m_mapManualWeights[{(int)l_nN, (int)l_nK}] = l_fWv;
                }
            }
            if (!m_mapManualWeights.empty() && ImGui::Button("Back to the animation")) m_mapManualWeights.clear();
        }

        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("grid (G)", &m_bGrid);
            ImGui::SameLine();
            ImGui::Checkbox("axes", &m_bAxes);
            ImGui::SameLine();
            ImGui::Checkbox("bounds (B)", &m_bBoundsBox);
            ImGui::Checkbox("skeleton (K)", &m_bSkeleton);
            ImGui::SameLine();
            ImGui::Checkbox("wireframe (W)", &m_bWireframe);
            ImGui::Checkbox("auto-rotate (R)", &m_bAutoRotate);
            ImGui::ColorEdit3("background", &m_v3Background.x, ImGuiColorEditFlags_NoInputs);
            ImGui::SliderFloat("fov", &m_fFov, 10.0f, 100.0f, "%.0f deg");
            ImGui::SliderFloat("move speed", &m_fMoveSpeed, m_fMoveMin, m_fMoveMax, "%.2fx", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Arrow keys move the camera; + / - change this");
            static const char* l_arrViews[] = {"lit", "normals", "base colour", "metal / rough", "uv", "shadow (first light)"};
            ImGui::Combo("view", &m_oFrame.m_iDebug, l_arrViews, 6);
        }

        if (ImGui::CollapsingHeader("Lights and shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!m_oRenderer.f_ShadowsSupported()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "This backend has no shadow maps.");
            ImGui::Checkbox("shadows", &m_oFrame.m_bShadows);
            ImGui::SameLine();
            ImGui::Checkbox("ground", &m_bGroundOn);
            static const int l_arrSizes[] = {1024, 2048, 4096, 8192};
            static const char* l_arrSizeNames[] = {"1024", "2048", "4096", "8192"};
            int l_iSi = 2;
            for (int l_iI = 0; l_iI < 4; l_iI++)
                if (l_arrSizes[l_iI] == m_oFrame.m_iShadowMapSize) l_iSi = l_iI;
            if (ImGui::Combo("shadow atlas", &l_iSi, l_arrSizeNames, 4)) m_oFrame.m_iShadowMapSize = l_arrSizes[l_iSi];
            ImGui::SliderFloat("shadow bias", &m_oFrame.m_fShadowBias, 0.2f, 5.0f, "x %.2f");
            if (m_upModel && !m_upModel->m_vLights.empty())
                ImGui::Checkbox(("the file's lights (" + std::to_string(m_upModel->m_vLights.size()) + ", KHR_lights_punctual)").c_str(),
                                &m_bUseFileLights);
            const bool l_bFileLights = m_bUseFileLights && m_upModel && !m_upModel->m_vLights.empty();
            if (l_bFileLights) {   // the file's lights: each one's hint
                m_vFileLightHint.resize(m_upModel->m_vLights.size(), 1);
                static const char* l_arrKinds[] = {"directional", "point", "spot"};
                for (size_t l_nI = 0; l_nI < m_upModel->m_vLights.size(); l_nI++) {
                    const gltf::Light& l_oFl = m_upModel->m_vLights[l_nI];
                    bool l_bOn = m_vFileLightHint[l_nI] != 0;
                    const std::string l_strLabel = std::to_string(l_nI + 1) + ". " + (l_oFl.m_strName.empty() ? std::string("light") : l_oFl.m_strName) + "  (" +
                                              l_arrKinds[(int)l_oFl.m_eType] + ")  show hint##filelight" + std::to_string(l_nI);
                    ImGui::ColorButton(("##col" + std::to_string(l_nI)).c_str(), ImVec4(l_oFl.m_v3Color.r, l_oFl.m_v3Color.g, l_oFl.m_v3Color.b, 1.0f),
                                       ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                    ImGui::SameLine();
                    if (ImGui::Checkbox(l_strLabel.c_str(), &l_bOn)) m_vFileLightHint[l_nI] = l_bOn ? 1 : 0;
                }
            }
            if (!l_bFileLights) {
                static const char* l_arrRigs[] = {"default", "point", "spot", "all"};
                int l_iRi = 0;
                for (int l_iI = 0; l_iI < 4; l_iI++)
                    if (m_strRigKind == l_arrRigs[l_iI]) l_iRi = l_iI;
                if (ImGui::Combo("rig", &l_iRi, l_arrRigs, 4)) f_BuildRig(l_arrRigs[l_iRi]);
                static const char* l_arrTypes[] = {"directional", "point", "spot"};
                int l_iRemove = -1;
                for (size_t l_nI = 0; l_nI < m_vRig.size(); l_nI++) {
                    RigLight& l_oR = m_vRig[l_nI];
                    ImGui::PushID((int)l_nI);
                    const std::string l_strTitle = std::to_string(l_nI + 1) + ". " + l_arrTypes[(int)l_oR.m_oL.m_eType] + (l_oR.m_oL.m_bCastShadows ? "  (shadows)" : "");
                    if (ImGui::TreeNodeEx(l_strTitle.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        int l_iT = (int)l_oR.m_oL.m_eType;
                        if (ImGui::Combo("type", &l_iT, l_arrTypes, 3)) l_oR.m_oL.m_eType = (GltfRenderer::Light::Type)l_iT;
                        ImGui::ColorEdit3("colour", &l_oR.m_oL.m_v3Color.x, ImGuiColorEditFlags_NoInputs);
                        ImGui::SameLine();
                        ImGui::Checkbox("cast shadows", &l_oR.m_oL.m_bCastShadows);
                        ImGui::SameLine();
                        ImGui::Checkbox("show hint", &l_oR.m_bHint);
                        ImGui::Checkbox("rotate", &l_oR.m_bSpin);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("It circles the model round the up axis (a directional light turns its yaw).");
                        if (l_oR.m_bSpin) {
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
                            ImGui::SliderFloat("speed", &l_oR.m_fSpinSpeed, -180.0f, 180.0f, "%.0f deg/s");
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Degrees a second; below 0 the other way round.");
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("This light's hint in the view, in its colour: a ball at a point or spot light, with a\n"
                                              "line down to a mark on the floor; a spot light's outer and inner cones, reaching the\n"
                                              "floor where it shines; a directional light's sun and an arrow towards the model.");
                        if (l_oR.m_oL.m_eType == GltfRenderer::Light::Type::Directional) {
                            ImGui::SliderFloat("intensity", &l_oR.m_oL.m_fIntensity, 0.0f, 10.0f);
                            ImGui::SliderFloat("yaw", &l_oR.m_fYaw, -180.0f, 180.0f, "%.0f deg");
                            ImGui::SliderFloat("pitch", &l_oR.m_fPitch, -10.0f, 90.0f, "%.0f deg");
                        } else {
                            ImGui::DragFloat("intensity", &l_oR.m_oL.m_fIntensity, std::max(l_oR.m_oL.m_fIntensity * 0.01f, 0.01f), 0.0f, 1e6f, "%.2f");
                            ImGui::DragFloat3("position", &l_oR.m_oL.m_v3Position.x, m_fRadius * 0.01f);
                            ImGui::DragFloat("range", &l_oR.m_oL.m_fRange, m_fRadius * 0.01f, 0.0f, 1e5f, l_oR.m_oL.m_fRange > 0 ? "%.2f" : "infinite");
                            if (l_oR.m_oL.m_eType == GltfRenderer::Light::Type::Spot) {
                                float l_fInner = glm::degrees(l_oR.m_oL.m_fInnerCone), l_fOuter = glm::degrees(l_oR.m_oL.m_fOuterCone);
                                if (ImGui::SliderFloat("inner cone", &l_fInner, 0.0f, 89.0f, "%.0f deg")) l_oR.m_oL.m_fInnerCone = glm::radians(l_fInner);
                                if (ImGui::SliderFloat("outer cone", &l_fOuter, 1.0f, 89.0f, "%.0f deg")) l_oR.m_oL.m_fOuterCone = glm::radians(l_fOuter);
                                if (ImGui::Checkbox("aim at the model", &l_oR.m_bAimAtModel) && !l_oR.m_bAimAtModel) {
                                    // its own aim starts where it shines now (no jump)
                                    const glm::vec3 l_v3D = glm::normalize(l_oR.m_oL.m_v3Direction);
                                    l_oR.m_fAimPitch = glm::degrees(std::asin(std::clamp(l_v3D.y, -1.0f, 1.0f)));
                                    l_oR.m_fAimYaw = glm::degrees(std::atan2(l_v3D.x, l_v3D.z));
                                }
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip("On: it always shines at the model's middle.\n"
                                                      "Off: it shines where its aim yaw and pitch say.");
                                if (!l_oR.m_bAimAtModel) {
                                    ImGui::SliderFloat("aim yaw", &l_oR.m_fAimYaw, -180.0f, 180.0f, "%.0f deg");
                                    ImGui::SliderFloat("aim pitch", &l_oR.m_fAimPitch, -90.0f, 90.0f, "%.0f deg");
                                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Below 0: down towards the floor; above 0: up.");
                                }
                            }
                        }
                        if (ImGui::SmallButton("remove")) l_iRemove = (int)l_nI;
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                if (l_iRemove >= 0) m_vRig.erase(m_vRig.begin() + l_iRemove);
                if ((int)m_vRig.size() < GltfRenderer::m_iMaxLights) {
                    for (int l_iT = 0; l_iT < 3; l_iT++) {
                        if (l_iT) ImGui::SameLine();
                        if (ImGui::Button((std::string("+ ") + l_arrTypes[l_iT]).c_str())) {
                            RigLight l_oR;
                            l_oR.m_oL.m_eType = (GltfRenderer::Light::Type)l_iT;
                            l_oR.m_fYaw = 60.0f * (float)m_vRig.size();
                            l_oR.m_fPitch = 45.0f;
                            l_oR.m_oL.m_v3Position = (m_v3SceneMin + m_v3SceneMax) * 0.5f + f_FromAngles(l_oR.m_fYaw, l_oR.m_fPitch) * m_fRadius * 2.2f;
                            if (l_iT) l_oR.m_oL.m_fIntensity = 3.5f * m_fRadius * m_fRadius * 4.84f;
                            m_vRig.push_back(l_oR);
                        }
                    }
                }
            }
            ImGui::Separator();
            ImGui::SliderFloat("exposure", &m_oFrame.m_fExposure, 0.1f, 4.0f);
            ImGui::BeginDisabled(f_EnvActive());   // the environment lights the scene instead (its intensity: Environment)
            ImGui::SliderFloat(f_FileLightsOn() ? "ambient (file lights)" : "ambient", f_FileLightsOn() ? &m_fAmbientFile : &m_fAmbientRig,
                               0.0f, 3.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The sky / ground fill added to every light. With the file's lights it starts low (%.1f), so\n"
                                  "they light the scene; with the viewer's rig it starts at %.1f.", 0.1f, 0.7f);
            ImGui::ColorEdit3("sky", &m_oFrame.m_v3Sky.x, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine();
            ImGui::ColorEdit3("ground", &m_oFrame.m_v3Ground.x, ImGuiColorEditFlags_NoInputs);
            ImGui::EndDisabled();
            ImGui::Checkbox("ACES tone mapping", &m_oFrame.m_bTonemap);
        }

        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_oRenderer.f_HasEnvironment()) {
                ImGui::Checkbox("image-based lighting", &m_bEnvOn);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", m_strEnvName.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open... or drop another .hdr to change it");
                ImGui::BeginDisabled(!m_bEnvOn);
                ImGui::SliderFloat("intensity##env", &m_fEnvIntensity, 0.0f, 4.0f);
                ImGui::SliderFloat("rotation##env", &m_fEnvRotation, -180.0f, 180.0f, "%.0f deg");
                ImGui::Checkbox("background##env", &m_bEnvBackground);
                ImGui::SameLine();
                ImGui::BeginDisabled(!m_bEnvBackground);
                ImGui::SliderFloat("blur##env", &m_fEnvBlur, 0.0f, 1.0f);
                ImGui::EndDisabled();
                ImGui::EndDisabled();
            } else {
                ImGui::TextDisabled("None: the plain sky / ground lights the model.");
            }
            if (!m_strEnvError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_strEnvError.c_str());
        }

        if (m_upModel->m_oPhysics.f_Any() && ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("simulate (KHR_physics_rigid_bodies)", &m_bPhysicsOn)) f_BuildPhysics();
            if (m_oPhysics.f_Built()) {
                const gltf::PhysicsWorld::Stats l_oSt = m_oPhysics.f_Stats();
                ImGui::Text("%.2f s simulated (%d steps of 1/60 s)", m_oPhysics.f_Time(), m_oPhysics.f_Steps());
                ImGui::Text("%d dynamic (%d awake), %d kinematic, %d static", l_oSt.m_iDynamic, l_oSt.m_iAwake, l_oSt.m_iKinematic, l_oSt.m_iStatics);
                ImGui::Text("%d triggers, %d joints", l_oSt.m_iTriggers, l_oSt.m_iJoints);
                ImGui::Checkbox("paused", &m_bPhysicsPaused);
                ImGui::SameLine();
                if (ImGui::Button("step") && m_bPhysicsPaused) m_oPhysics.f_Step(gltf::PhysicsWorld::m_fStep, m_oPose);
                ImGui::SameLine();
                if (ImGui::Button("restart")) f_BuildPhysics();
                glm::vec3 l_v3G = m_oPhysics.f_Gravity();
                if (ImGui::SliderFloat("gravity Y", &l_v3G.y, -20.0f, 5.0f, "%.2f m/s2")) m_oPhysics.f_SetGravity(l_v3G);
                ImGui::Checkbox("collider boxes", &m_bPhysicsBoxes);
                for (const std::string& l_strW : m_oPhysics.f_Warnings()) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", l_strW.c_str());
                if (!m_vPhysicsLog.empty()) {
                    ImGui::TextDisabled("triggers:");
                    for (const std::string& l_strL : m_vPhysicsLog) ImGui::TextUnformatted(l_strL.c_str());
                }
            }
        }

        if (ImGui::CollapsingHeader("Crowd (instanced skinning)")) {
            int l_iN = m_iCrowdCount;
            if (ImGui::SliderInt("copies", &l_iN, 0, 400)) f_BuildCrowd(l_iN);
            ImGui::TextDisabled("0 = off: the single model with its Pose");
            if (!m_strCrowdError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_strCrowdError.c_str());
            if (m_iCrowdCount > 0 && m_spAnimTex) {
                ImGui::Text("texture %dx%d RGBA32F, %.1f KB", m_spAnimTex->m_iWidth, m_spAnimTex->m_iRows, m_spAnimTex->f_Bytes() / 1024.0);
                ImGui::Text("%d joints, %zu clips at %.0f fps, baked in %.1f ms", m_spAnimTex->m_iJoints, m_spAnimTex->m_vClips.size(), m_spAnimTex->m_fFps, m_dBakeMs);
                if (m_spAnimTex->m_fMaxScaleError > 0.01f) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "non-uniform joint scale averaged (%.0f %%)",
                                                                        m_spAnimTex->m_fMaxScaleError * 100.0f);
                if (ImGui::Button("cross-fade all to the next clip (0.5 s)"))
                    for (int l_iI = 0; l_iI < m_oCrowd.size(); l_iI++) m_oCrowd.f_CrossFade(l_iI, (m_oCrowd[l_iI].m_oA.m_iClip + 1) % (int)m_spAnimTex->m_vClips.size(), 0.5f);
                if (m_spAnimTex->m_vClips.size() > 1 && ImGui::SliderFloat("blend clip 0 / 1", &m_fCrowdBlend, 0.0f, 1.0f))
                    for (int l_iI = 0; l_iI < m_oCrowd.size(); l_iI++) m_oCrowd.f_SetBlend(l_iI, 0, 1, m_fCrowdBlend);
                if (ImGui::Button("each its own clip again")) f_BuildCrowd(m_iCrowdCount);
            }
        }

        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            const GltfRenderer::Stats& l_oS = m_oRenderer.f_Stats();
            ImGui::Text("%.1f fps   %s", m_fFps, next::f_BgfxHostRendererName().c_str());
            ImGui::Text("%d draw calls, %d instances", l_oS.m_iDrawCalls, l_oS.m_iInstances);
            ImGui::Text("%d skinned draws, %d morph uploads", l_oS.m_iSkinnedDraws, l_oS.m_iMorphUploads);
            if (l_oS.m_iCrowdInstances > 0) ImGui::Text("%d crowd instances (no CPU posing)", l_oS.m_iCrowdInstances);
            ImGui::Text("%d shadow tiles, %d shadow draws", l_oS.m_iShadowTiles, l_oS.m_iShadowDraws);
        }
        ImGui::End();
    }

    // ---- one frame -------------------------------------------------------------------------------
    void f_Tick() {
        const uint64_t l_u64Now = SDL_GetTicksNS();
        const float l_fDt = m_u64LastTicks ? float(double(l_u64Now - m_u64LastTicks) * 1e-9) : 1.0f / 60.0f;
        m_u64LastTicks = l_u64Now;
        m_fFpsAccum += l_fDt;
        if (++m_iFpsFrames >= 20) {
            m_fFps = m_iFpsFrames / std::max(m_fFpsAccum, 1e-6f);
            m_fFpsAccum = 0;
            m_iFpsFrames = 0;
        }
        {
            std::string l_strNext;
            {
                std::lock_guard<std::mutex> l_oG(m_oPendingLock);
                l_strNext.swap(m_strPending);
            }
            if (!l_strNext.empty()) f_OpenFile(l_strNext);
        }
        int l_iW = 0, l_iH = 0;
        SDL_GetWindowSizeInPixels(m_pWindow, &l_iW, &l_iH);
        if (l_iW != m_iFbW || l_iH != m_iFbH) {
            m_iFbW = l_iW;
            m_iFbH = l_iH;
            next::f_BgfxHostReset((uint32_t)l_iW, (uint32_t)l_iH);
        }

        if (m_oArgs.m_fTime >= 0.0f) m_dTime = m_oArgs.m_fTime;
        else if (!m_bPaused) {
            m_dTime += double(l_fDt) * m_fSpeed;
            m_oCrowd.f_Update(l_fDt * m_fSpeed);
        }
        if (m_bAutoRotate) m_fYaw += l_fDt * 20.0f;
        f_SpinLights(l_fDt);
        f_MoveWithKeys(l_fDt);

        // UI first (it decides whether the mouse belongs to it).
        float l_fMx = 0, l_fMy = 0;
        const SDL_MouseButtonFlags l_uButtons = SDL_GetMouseState(&l_fMx, &l_fMy);
        next::ImGuiBgfx::Input l_oIn;
        const float l_fPs = f_PixelScale();
        l_oIn.m_fMouseX = l_fMx * l_fPs;
        l_oIn.m_fMouseY = l_fMy * l_fPs;
        l_oIn.m_arrMouseDown[0] = (l_uButtons & SDL_BUTTON_LMASK) != 0;
        l_oIn.m_arrMouseDown[1] = (l_uButtons & SDL_BUTTON_RMASK) != 0;
        l_oIn.m_arrMouseDown[2] = (l_uButtons & SDL_BUTTON_MMASK) != 0;
        l_oIn.m_fWheel = m_fWheel;
        m_fWheel = 0;
        m_oGui.f_NewFrame((uint32_t)m_iFbW, (uint32_t)m_iFbH, l_fDt, l_oIn);
        if (m_bShowUi) f_Panel();

        // The scene.
        if (m_upModel) {
            f_Posed();
            f_Simulate(l_fDt);
        }
        m_oFrame.m_vLights = f_FrameLights();
        m_oFrame.m_fAmbient = f_EnvActive() ? 1.0f : f_FileLightsOn() ? m_fAmbientFile : m_fAmbientRig;   // an environment: its own intensity
        m_oFrame.m_bEnvironment = m_bEnvOn;
        m_oFrame.m_fEnvironmentIntensity = m_fEnvIntensity;
        m_oFrame.m_fEnvironmentYaw = glm::radians(m_fEnvRotation);
        m_oFrame.m_bBackground = m_bEnvBackground;
        m_oFrame.m_fBackgroundBlur = m_fEnvBlur;
        // Shadows cover the model and the floor around it (where its shadow falls).
        m_oFrame.m_v3BoundsMin = glm::vec3(m_v3SceneMin.x - m_fRadius, m_v3SceneMin.y, m_v3SceneMin.z - m_fRadius);
        m_oFrame.m_v3BoundsMax = glm::vec3(m_v3SceneMax.x + m_fRadius, m_v3SceneMax.y, m_v3SceneMax.z + m_fRadius);
        const float l_fAspect = float(m_iFbW) / float(std::max(m_iFbH, 1));
        const std::vector<gltf::PlacedCamera> l_vCams = m_upModel ? m_oPose.f_Cameras() : std::vector<gltf::PlacedCamera>();
        if (m_iActiveCamera >= (int)l_vCams.size()) m_iActiveCamera = -1;
        if (m_iActiveCamera >= 0) {   // through the file's camera (it moves with its node's animation)
            const gltf::PlacedCamera& l_oPc = l_vCams[(size_t)m_iActiveCamera];
            const gltf::Camera& l_oC = m_upModel->m_vCameras[(size_t)l_oPc.m_iCamera];
            const glm::mat4 l_mat4World = f_LookedCamera(l_oPc);
            m_oFrame.m_v3Eye = glm::vec3(l_mat4World[3]);
            m_oFrame.m_mat4View = glm::inverse(l_mat4World);
            const float l_fZn = std::max(l_oC.m_fZnear, 1e-4f);
            const float l_fZf = l_oC.m_fZfar > l_fZn ? l_oC.m_fZfar : l_fZn + m_fRadius * 100.0f + glm::length(m_oFrame.m_v3Eye - m_v3Target);
            if (l_oC.m_bPerspective) {
                const float l_fFovY = 2.0f * std::atan(std::tan(l_oC.m_fYfov * 0.5f) * m_fLookZoom);   // the wheel zooms the lens
                m_oFrame.m_mat4Proj = GltfRenderer::f_Projection(glm::degrees(l_fFovY), l_fAspect, l_fZn, l_fZf);
            } else {   // the window's aspect, the camera's height
                const float l_fYm = l_oC.m_fYmag * m_fLookZoom, l_fXm = l_fYm * l_fAspect;
                m_oFrame.m_mat4Proj = bgfx::getCaps()->homogeneousDepth ? glm::orthoRH_NO(-l_fXm, l_fXm, -l_fYm, l_fYm, l_fZn, l_fZf)
                                                               : glm::orthoRH_ZO(-l_fXm, l_fXm, -l_fYm, l_fYm, l_fZn, l_fZf);
            }
        } else {
            m_oFrame.m_v3Eye = f_Eye();
            m_oFrame.m_mat4View = f_ViewMatrix();
            const float l_fZn = std::max(m_fDistance - m_fRadius * 4.0f, m_fDistance * 0.002f);
            m_oFrame.m_mat4Proj = GltfRenderer::f_Projection(m_fFov, l_fAspect, l_fZn, m_fDistance + m_fRadius * 50.0f);
        }
#if TOMS_GAMMA_SPACE
        // No sRGB backbuffer (the web: color_space.h): the shader encodes, and so does the clear colour here, which the
        // desktop's sRGB backbuffer encodes by itself -- otherwise everything is drawn too dark.
        m_oFrame.m_bSrgbOut = true;
        const glm::vec3 l_v3Clear = glm::pow(glm::max(m_v3Background, glm::vec3(0.0f)), glm::vec3(1.0f / 2.2f));
#else
        m_oFrame.m_bSrgbOut = false;   // the host's backbuffer is sRGB: the GPU encodes
        const glm::vec3 l_v3Clear = m_v3Background;
#endif
        if (m_bShowUi && m_iFbW > 0) {   // centre the model in the part the panel leaves free
            const float l_fPanel = std::min(380.0f * m_fUiScale, float(m_iFbW) * 0.34f) + 16.0f * m_fUiScale;
            m_oFrame.m_mat4Proj = glm::translate(glm::mat4(1.0f), glm::vec3(-l_fPanel / float(m_iFbW), 0.0f, 0.0f)) * m_oFrame.m_mat4Proj;
        }
        m_oRenderer.begin(0, m_oFrame, (uint32_t)m_iFbW, (uint32_t)m_iFbH, f_RgbaHex(l_v3Clear));   // views 0 .. kViews - 1
        if (m_upModel && m_spGpu && m_iCrowdCount > 0 && m_spAnimGpu && m_oCrowd.size() > 0) {
            if (m_oArgs.m_fTime >= 0.0f && m_iFrameNo == 0) m_oCrowd.f_Update(m_oArgs.m_fTime);   // --time: freeze the crowd there too
            m_oRenderer.f_DrawCrowd(*m_spGpu, *m_spAnimGpu, m_oCrowd);
        } else if (m_upModel && m_spGpu) {
            m_oRenderer.f_Draw(*m_spGpu, m_oPose);
        }
        if (m_bGroundOn && m_upModel && m_spGroundGpu) {   // a floor under the model: receives shadows, casts none
            const glm::vec3 l_v3C = (m_v3SceneMin + m_v3SceneMax) * 0.5f;
            const glm::mat4 l_mat4Place = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(l_v3C.x, m_v3SceneMin.y - m_fRadius * 0.002f, l_v3C.z)),
                                               glm::vec3(m_fRadius * 6.0f));
            m_oRenderer.f_Draw(*m_spGroundGpu, m_oGroundPose, l_mat4Place, false);
        }
        f_DrawLightGizmos();
        f_DrawLines();
        bgfx::setDebug(m_bWireframe ? BGFX_DEBUG_WIREFRAME : BGFX_DEBUG_NONE);
        m_oGui.f_Render(GltfRenderer::m_u16Views + 4);

        if (m_iFrameNo == 1 && m_oArgs.m_fPickX >= 0.0f) {   // --pick: as a click there, once the frame's matrices exist
            m_iSelected = f_PickNode(glm::vec2(m_oArgs.m_fPickX, m_oArgs.m_fPickY));
            m_bRevealSelected = m_iSelected >= 0;
            std::fprintf(stderr, "[gltf] pick %.0f,%.0f: node %d (%s)\n", m_oArgs.m_fPickX, m_oArgs.m_fPickY, m_iSelected,
                         m_iSelected >= 0 ? m_upModel->m_vNodes[(size_t)m_iSelected].m_strName.c_str() : "nothing");
        }
        if (m_iFrameNo == 2 && m_oArgs.m_v4Drag != glm::vec4(-1.0f) && m_iSelected >= 0) {   // --drag: as a press, a move and a release
            glm::vec2 l_v2O(0.0f);
            f_ToScreen(glm::vec3(m_oPose.f_World(m_iSelected)[3]), l_v2O);
            const glm::vec2 l_v2P0 = l_v2O + glm::vec2(m_oArgs.m_v4Drag.x, m_oArgs.m_v4Drag.y), l_v2P1 = l_v2O + glm::vec2(m_oArgs.m_v4Drag.z, m_oArgs.m_v4Drag.w);
            const int l_iH = f_HandleAt(l_v2P0);
            if (l_iH >= 0) {
                f_StartHandleDrag(l_iH, l_v2P0);
                f_DragHandleTo(l_v2P1);
                m_iDragHandle = -1;
                f_Posed();
                f_EditChanged(m_iSelected);
            }
            const NodeEdit l_oE = m_mapEdits.count(m_iSelected) ? m_mapEdits[m_iSelected] : NodeEdit();
            std::fprintf(stderr, "[gltf] drag handle %d: t %.3f %.3f %.3f  euler %.1f %.1f %.1f  s %.3f %.3f %.3f\n", l_iH, l_oE.m_v3T.x, l_oE.m_v3T.y,
                         l_oE.m_v3T.z, l_oE.m_v3Euler.x, l_oE.m_v3Euler.y, l_oE.m_v3Euler.z, l_oE.m_v3S.x, l_oE.m_v3S.y, l_oE.m_v3S.z);
        }
        if (m_oArgs.m_iFrames > 0 && m_iFrameNo == m_oArgs.m_iFrames && !m_oArgs.m_strScreenshot.empty()) {
            m_iShotsBefore = next::f_BgfxHostScreenshotsWritten();
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, m_oArgs.m_strScreenshot.c_str());
        }
        next::f_BgfxHostFrame();
        m_iFrameNo++;
    }

    float m_fWheel = 0.0f;
};

}  // namespace

// assets/media (resources.json): env ASSET_DIR, else the folder baked in at build time; the web page's /assets.
std::string f_AssetDir() {
    if (const char* l_pszEnv = std::getenv("ASSET_DIR")) return l_pszEnv;
#if defined(__EMSCRIPTEN__)
    return "/assets";
#elif defined(TOMS_DEFAULT_ASSET_DIR)
    return TOMS_DEFAULT_ASSET_DIR;
#else
    return "assets/media";
#endif
}

// The command line as UTF-8. On Windows argv is in the ANSI code page, so a file like "Unicode❤♻Test.glb" came in
// as "Unicode??Test.glb" and could not be found: read the wide command line instead.
std::vector<std::string> f_Utf8Args(int l_iArgc, char** l_pArgv) {
    std::vector<std::string> l_vOut;
#ifdef _WIN32
    int l_iN = 0;
    if (LPWSTR* l_pW = CommandLineToArgvW(GetCommandLineW(), &l_iN)) {
        for (int l_iI = 0; l_iI < l_iN; l_iI++) {
            const int l_iLen = WideCharToMultiByte(CP_UTF8, 0, l_pW[l_iI], -1, nullptr, 0, nullptr, nullptr);
            std::string l_strA(l_iLen > 0 ? (size_t)l_iLen - 1 : 0, char(0));
            if (l_iLen > 1) WideCharToMultiByte(CP_UTF8, 0, l_pW[l_iI], -1, &l_strA[0], l_iLen, nullptr, nullptr);
            l_vOut.push_back(l_strA);
        }
        LocalFree(l_pW);
        return l_vOut;
    }
#endif
    for (int l_iI = 0; l_iI < l_iArgc; l_iI++) l_vOut.push_back(l_pArgv[l_iI]);
#ifdef __EMSCRIPTEN__
    // The page's URL: ?file=skin_tube.gltf&anim=bend&no-ui -> {"skin_tube.gltf", "--anim=bend", "--no-ui"}
    // (as src/game/src/main_sdl.cpp urlArgs; the values are taken as they are, not %-decoded).
    char* l_pszQ = (char*)EM_ASM_PTR({
        var s = (typeof location !== 'undefined' && location.search) ? location.search.substring(1) : "";
        return stringToNewUTF8(s);
    });
    const std::string l_strQuery = l_pszQ ? l_pszQ : "";
    free(l_pszQ);
    for (size_t l_nPos = 0; l_nPos < l_strQuery.size();) {
        const size_t l_nAmp = l_strQuery.find('&', l_nPos);
        const std::string l_strKv = l_strQuery.substr(l_nPos, l_nAmp == std::string::npos ? std::string::npos : l_nAmp - l_nPos);
        if (l_strKv.compare(0, 5, "file=") == 0) l_vOut.push_back(l_strKv.substr(5));
        else if (!l_strKv.empty()) l_vOut.push_back("--" + l_strKv);
        if (l_nAmp == std::string::npos) break;
        l_nPos = l_nAmp + 1;
    }
#endif
    return l_vOut;
}

#ifdef __EMSCRIPTEN__
Viewer* g_pViewer = nullptr;   // for the page's hooks below

void f_WebFrame(void*) {
    SDL_Event l_oE;
    bool l_bQuit = false;   // a web page stays: Esc and closing the tab do nothing here
    while (SDL_PollEvent(&l_oE)) {
        if (l_oE.type == SDL_EVENT_MOUSE_WHEEL) g_pViewer->m_fWheel += l_oE.wheel.y;
        g_pViewer->f_OnEvent(l_oE, l_bQuit);
    }
    g_pViewer->f_Tick();
}

extern "C" {
// web/shell.html wrote the picked or dropped files into the in-memory file system: open this one.
EMSCRIPTEN_KEEPALIVE void jsOpenFile(const char* l_pszPath) {
    if (!g_pViewer || !l_pszPath) return;
    std::lock_guard<std::mutex> l_oG(g_pViewer->m_oPendingLock);
    g_pViewer->m_strPending = l_pszPath;
}
}
#endif

int main(int l_iArgc, char** l_pArgv) {
    // On the heap: on the web, main returns while the browser keeps calling webFrame.
    Viewer& l_oV = *new Viewer;
    l_oV.m_oArgs = f_ParseArgs(f_Utf8Args(l_iArgc, l_pArgv));
#ifdef __EMSCRIPTEN__
    std::error_code l_oEc;
    for (const auto& l_oF : std::filesystem::directory_iterator("/models", l_oEc)) l_oV.m_vTestModels.push_back(l_oF.path().filename().string());
    std::sort(l_oV.m_vTestModels.begin(), l_oV.m_vTestModels.end());
    if (l_oV.m_oArgs.m_strFile.empty() && !l_oV.m_vTestModels.empty())
        l_oV.m_oArgs.m_strFile = std::find(l_oV.m_vTestModels.begin(), l_oV.m_vTestModels.end(), "skin_tube.gltf") != l_oV.m_vTestModels.end() ? "skin_tube.gltf"
                                                                                                                  : l_oV.m_vTestModels[0];
    if (!l_oV.m_oArgs.m_strFile.empty() && l_oV.m_oArgs.m_strFile[0] != '/') l_oV.m_oArgs.m_strFile = "/models/" + l_oV.m_oArgs.m_strFile;
#endif
    l_oV.m_bShowUi = l_oV.m_oArgs.m_bUi;
    l_oV.m_fYaw = l_oV.m_oArgs.m_fYaw;
    l_oV.m_fPitch = l_oV.m_oArgs.m_fPitch;
    l_oV.m_oFrame.m_iDebug = std::clamp(l_oV.m_oArgs.m_iDebug, 0, 5);
    l_oV.m_oFrame.m_bShadows = l_oV.m_oArgs.m_bShadows;
    l_oV.m_oFrame.m_fShadowBias = l_oV.m_oArgs.m_fShadowBias;
    l_oV.m_bGroundOn = l_oV.m_oArgs.m_bGround;
    l_oV.m_bUseFileLights = l_oV.m_oArgs.m_strLight == "file" || l_oV.m_oArgs.m_strLight == "default";
    l_oV.m_strRigKind = l_oV.m_oArgs.m_strLight == "file" ? std::string("default") : l_oV.m_oArgs.m_strLight;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    // A screenshot run draws into a hidden window: no window flashing open and shut on the desktop.
    l_oV.m_pWindow = SDL_CreateWindow("TOMS glTF Viewer", l_oV.m_oArgs.m_iW, l_oV.m_oArgs.m_iH,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                    (l_oV.m_oArgs.m_strScreenshot.empty() ? SDL_WindowFlags(0) : SDL_WINDOW_HIDDEN));
    if (!l_oV.m_pWindow) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    next::BgfxHostConfig l_oCfg;
#if defined(__EMSCRIPTEN__)
    // bgfx's WebGL backend takes the canvas CSS selector as its "window handle" (as src/game/src/main_sdl.cpp).
    static std::string l_strCanvas;
    const char* l_pszId = SDL_GetStringProperty(SDL_GetWindowProperties(l_oV.m_pWindow), SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, "#canvas");
    l_strCanvas = (l_pszId && l_pszId[0] == '#') ? l_pszId : std::string("#") + (l_pszId ? l_pszId : "canvas");
    l_oCfg.m_pNativeWindow = (void*)l_strCanvas.c_str();
#elif defined(_WIN32)
    l_oCfg.m_pNativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(l_oV.m_pWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__linux__)
    l_oCfg.m_pNativeWindow = (void*)(uintptr_t)SDL_GetNumberProperty(SDL_GetWindowProperties(l_oV.m_pWindow), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    l_oCfg.m_pNativeDisplay = SDL_GetPointerProperty(SDL_GetWindowProperties(l_oV.m_pWindow), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
#endif
    SDL_GetWindowSizeInPixels(l_oV.m_pWindow, &l_oV.m_iFbW, &l_oV.m_iFbH);
    l_oCfg.m_uWidth = (uint32_t)l_oV.m_iFbW;
    l_oCfg.m_uHeight = (uint32_t)l_oV.m_iFbH;
    l_oCfg.m_strRenderer = l_oV.m_oArgs.m_strRenderer;
#ifdef __EMSCRIPTEN__
    l_oCfg.m_iMsaa = 0;   // bgfx's MSAA back buffer on WebGL calls glFramebufferTexture2DMultisampleEXT, which WebGL lacks
#else
    l_oCfg.m_iMsaa = 4;
#endif
    std::string l_strErr;
    if (!next::f_BgfxHostInit(l_oCfg, l_strErr)) {
#ifdef __EMSCRIPTEN__
        EM_ASM({ if (typeof tomsShowError === 'function') tomsShowError('The glTF viewer could not start', UTF8ToString($0)); }, l_strErr.c_str());
#else
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "glTF Viewer", l_strErr.c_str(), l_oV.m_pWindow);
#endif
        return 1;
    }
    if (!l_oV.m_oRenderer.f_Init() || !l_oV.m_oGui.f_Init()) {
        std::fprintf(stderr, "[gltf] the renderer could not start\n");
        next::f_BgfxHostShutdown();
        return 1;
    }
    l_oV.f_MakeGround();
    l_oV.f_MakeGizmos();
    l_oV.m_fEnvRotation = l_oV.m_oArgs.m_fEnvRotation;
    l_oV.m_fEnvIntensity = l_oV.m_oArgs.m_fEnvIntensity;
    l_oV.m_bEnvBackground = l_oV.m_oArgs.m_iBackground != 0;
    l_oV.m_fEnvBlur = l_oV.m_oArgs.m_fBackgroundBlur;
    if (l_oV.m_oArgs.m_strEnv == "none") {
        l_oV.m_bEnvOn = false;
    } else {
        std::string l_strEnvFile = l_oV.m_oArgs.m_strEnv;
        if (l_strEnvFile.empty()) {   // the default, by its key in the resource manifest
            res::ResourceManager l_oRes;
            l_oRes.f_SetRoot(f_AssetDir());
            if (l_oRes.f_LoadManifest("resources.json")) l_strEnvFile = l_oRes.f_Resolve(key::g_pszEnvDefault);
            else l_oV.m_strEnvError = "no resources.json in " + f_AssetDir();
        }
        if (l_strEnvFile.empty() || !l_oV.f_LoadEnvironment(l_strEnvFile)) l_oV.m_bEnvOn = false;
    }
    l_oV.m_fUiScale = std::max(1.0f, SDL_GetWindowDisplayScale(l_oV.m_pWindow) * 0.8f);
    ImGui::GetIO().FontGlobalScale = l_oV.m_fUiScale;
    ImGui::GetStyle().ScaleAllSizes(l_oV.m_fUiScale);
    ImGui::GetStyle().WindowRounding = 6.0f * l_oV.m_fUiScale;
    if (!l_oV.m_oArgs.m_strFile.empty() && !l_oV.f_Load(l_oV.m_oArgs.m_strFile) && l_oV.m_oArgs.m_iFrames > 0) {
        next::f_BgfxHostShutdown();
        return 2;   // a test asked for a model that does not load
    }

#ifdef __EMSCRIPTEN__
    g_pViewer = &l_oV;
    emscripten_set_main_loop_arg(f_WebFrame, nullptr, 0, false);   // every animation frame; main returns, the page runs on
    return 0;
#endif
    bool l_bQuit = false;
    while (!l_bQuit) {
        SDL_Event l_oE;
        while (SDL_PollEvent(&l_oE)) {
            if (l_oE.type == SDL_EVENT_MOUSE_WHEEL) l_oV.m_fWheel += l_oE.wheel.y;
            l_oV.f_OnEvent(l_oE, l_bQuit);
        }
        l_oV.f_Tick();
        // A test: quit once the screenshot is on disk (or a while after asking).
        if (l_oV.m_oArgs.m_iFrames > 0 && l_oV.m_iFrameNo > l_oV.m_oArgs.m_iFrames &&
            (l_oV.m_oArgs.m_strScreenshot.empty() || next::f_BgfxHostScreenshotsWritten() > l_oV.m_iShotsBefore || l_oV.m_iFrameNo > l_oV.m_oArgs.m_iFrames + 60))
            l_bQuit = true;
    }
    l_oV.m_spGpu.reset();
    l_oV.m_spAnimGpu.reset();   // the crowd's texture: before bgfx goes
    l_oV.m_spGroundGpu.reset();
    for (auto* l_pG : {&l_oV.m_oGizmoSphere, &l_oV.m_oGizmoCone, &l_oV.m_oGizmoCylinder, &l_oV.m_oGizmoDisc}) l_pG->m_spGpu.reset();
    l_oV.m_oRenderer.f_Shutdown();
    l_oV.m_oGui.f_Shutdown();
    next::f_BgfxHostShutdown();
    SDL_DestroyWindow(l_oV.m_pWindow);
    SDL_Quit();
    delete &l_oV;
    return 0;
}
