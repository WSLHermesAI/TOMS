// gltf_viewer -- a glTF 2.0 model viewer on the engine's renderer (docs/18_GLTF.md), in the spirit of
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
//     --shadows=0|1 --ground=0|1            shadow maps (default on), the floor that catches them
//     --shadow-bias=<x>                     scales the shadow offsets (acne vs. detached shadows)
//
// Mouse: left drag orbits, right / middle drag pans, wheel zooms. Keys: Space play/pause, F frame,
// G grid, K skeleton, B bounds, W wireframe, R auto-rotate, H panel, 1..9 solo an animation, Esc quit.
#include "bgfx_host.h"
#include "gltf_model.h"
#include "gltf_renderer.h"
#include "imgui_bgfx.h"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace toms;
using toms::next::GltfRenderer;

namespace {

struct Args {
    std::string file, screenshot, renderer = "auto", anim;
    int frames = 0, w = 1280, h = 800, debug = 0;
    float time = -1.0f, yaw = 35.0f, pitch = 20.0f, zoom = 1.0f;
    bool ui = true, shadows = true, ground = true;
    std::string light = "default";
    float shadowBias = 1.0f;
};

const char* value(const std::string& s, const char* key) {
    const size_t n = std::strlen(key);
    return s.compare(0, n, key) == 0 ? s.c_str() + n : nullptr;
}

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        const std::string s = argv[i];
        if (const char* v = value(s, "--renderer=")) a.renderer = v;
        else if (const char* v = value(s, "--anim=")) a.anim = v;
        else if (const char* v = value(s, "--time=")) a.time = (float)std::atof(v);
        else if (const char* v = value(s, "--frames=")) a.frames = std::atoi(v);
        else if (const char* v = value(s, "--screenshot=")) a.screenshot = v;
        else if (const char* v = value(s, "--yaw=")) a.yaw = (float)std::atof(v);
        else if (const char* v = value(s, "--pitch=")) a.pitch = (float)std::atof(v);
        else if (const char* v = value(s, "--zoom=")) a.zoom = (float)std::atof(v);
        else if (const char* v = value(s, "--debug=")) a.debug = std::atoi(v);
        else if (const char* v = value(s, "--size=")) std::sscanf(v, "%dx%d", &a.w, &a.h);
        else if (s == "--no-ui") a.ui = false;
        else if (const char* v = value(s, "--light=")) a.light = v;
        else if (const char* v = value(s, "--shadows=")) a.shadows = std::atoi(v) != 0;
        else if (const char* v = value(s, "--ground=")) a.ground = std::atoi(v) != 0;
        else if (const char* v = value(s, "--shadow-bias=")) a.shadowBias = (float)std::atof(v);
        else if (!s.empty() && s[0] != '-') a.file = s;
    }
    return a;
}

uint32_t abgr(float r, float g, float b, float a = 1.0f) {
    auto c = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
}
uint32_t rgbaHex(const glm::vec3& c) {   // bgfx clear colour 0xRRGGBBAA
    auto b = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return b(c.r) << 24 | b(c.g) << 16 | b(c.b) << 8 | 0xff;
}

struct Viewer {
    Args args;
    SDL_Window* window = nullptr;
    GltfRenderer renderer;
    next::ImGuiBgfx gui;
    float uiScale = 1.0f;

    // The model.
    std::unique_ptr<gltf::Model> model;
    gltf::Pose pose;
    std::shared_ptr<GltfRenderer::GpuModel> gpu;
    std::string path, error;
    std::mutex pendingLock;
    std::string pending;   // a file chosen in the open dialog (its callback may run on another thread)

    // Camera (orbit around target).
    glm::vec3 target{0.0f};
    float yaw = 35.0f, pitch = 20.0f, distance = 5.0f, fov = 45.0f, radius = 1.0f;
    glm::vec3 sceneMin{-1.0f}, sceneMax{1.0f};

    // Animation.
    std::vector<char> playing;
    double time = 0.0;
    float speed = 1.0f;
    bool paused = false;
    std::map<std::pair<int, int>, float> manualWeights;   // (node, target) -> weight set by a slider

    // Display.
    GltfRenderer::Frame frame;
    glm::vec3 background{0.20f, 0.21f, 0.23f};
    // The viewer's lights. Directional / spot lights aim by yaw / pitch (where the light comes FROM);
    // point / spot lights sit at a position. Rebuilt around the model when it loads (rigKind).
    struct RigLight {
        GltfRenderer::Light l;
        float yaw = -35.0f, pitch = 45.0f;
    };
    std::vector<RigLight> rig;
    std::string rigKind = "default";
    bool useFileLights = true;            // the model's KHR_lights_punctual, when it has some
    // The floor that catches the shadows (a built-in quad model).
    bool groundOn = true;
    gltf::Model groundModel;
    gltf::Pose groundPose;
    std::shared_ptr<GltfRenderer::GpuModel> groundGpu;
    bool grid = true, axes = false, skeleton = false, boundsBox = false, wireframe = false, autoRotate = false;
    bool showUi = true;

    // Frame state.
    int fbW = 0, fbH = 0, frameNo = 0, shotsBefore = -1;
    uint64_t lastTicks = 0;
    float fps = 0.0f, fpsAccum = 0.0f;
    int fpsFrames = 0;
    bool dragOrbit = false, dragPan = false;

    float maxDuration() const {
        float d = 0.0f;
        if (model)
            for (size_t i = 0; i < model->animations.size(); i++)
                if (playing[i]) d = std::max(d, model->animations[i].duration);
        return d;
    }

    bool load(const std::string& file) {
        auto m = std::make_unique<gltf::Model>();
        std::string err;
        if (!gltf::loadModel(file, *m, &err)) {
            error = file + ": " + err;
            std::fprintf(stderr, "[gltf] %s\n", error.c_str());
            return false;
        }
        gpu.reset();   // the old model's buffers go first
        model = std::move(m);
        path = file;
        error.clear();
        pose.bind(model.get());
        gpu = renderer.upload(*model);
        playing.assign(model->animations.size(), 0);
        manualWeights.clear();
        time = 0.0;
        // The animation to play: --anim (index or name), else the first one.
        int pick = model->animations.empty() ? -1 : 0;
        if (!args.anim.empty()) {
            pick = args.anim == "none" ? -1 : -2;
            for (size_t i = 0; i < model->animations.size() && pick == -2; i++)
                if (model->animations[i].name == args.anim || std::to_string(i) == args.anim) pick = (int)i;
            if (pick == -2) pick = -1;
        }
        if (pick >= 0) playing[(size_t)pick] = 1;
        std::fprintf(stderr, "[gltf] %s: %zu vertices, %zu triangles, %zu meshes, %zu skins, %zu morph targets, %zu instances, %zu animations\n",
                     file.c_str(), model->vertexCount(), model->triangleCount(), model->meshes.size(), model->skins.size(),
                     model->morphTargetCount(), model->instanceCount(), model->animations.size());
        for (const std::string& w : model->warnings) std::fprintf(stderr, "[gltf]   warning: %s\n", w.c_str());
        for (const std::string& e : model->ignoredExtensions) std::fprintf(stderr, "[gltf]   ignored extension: %s\n", e.c_str());
        const std::string title = "TOMS glTF Viewer - " + file;
        SDL_SetWindowTitle(window, title.c_str());
        frameScene();
        return true;
    }

    // Camera on the whole scene: bounds over the playing animations (sampled at a few times, so a
    // morph or a walk does not leave the view), then the pose at the current time again.
    void frameScene() {
        if (!model) return;
        const double keep = time;
        const float dur = maxDuration();
        glm::vec3 mn(0.0f), mx(0.0f);
        bool any = false;
        for (int k = 0; k <= (dur > 0 ? 8 : 0); k++) {
            time = dur * k / 8.0;
            posed();
            glm::vec3 a, b;
            if (!pose.bounds(a, b)) continue;
            mn = any ? glm::min(mn, a) : a;
            mx = any ? glm::max(mx, b) : b;
            any = true;
        }
        time = keep;
        posed();
        if (!any) mn = glm::vec3(-1.0f), mx = glm::vec3(1.0f);
        sceneMin = mn;
        sceneMax = mx;
        target = (mn + mx) * 0.5f;
        radius = std::max(glm::length(mx - mn) * 0.5f, 1e-3f);
        buildRig(rigKind);
        distance = radius / std::sin(glm::radians(fov * 0.5f)) * 1.05f / std::max(args.zoom, 0.01f);
    }

    // The pose at the current time: every ticked animation, then the morph sliders.
    void posed() {
        pose.reset();
        for (size_t i = 0; i < model->animations.size(); i++) {
            if (!playing[i]) continue;
            const float d = model->animations[i].duration;
            pose.apply((int)i, d > 0 ? (float)std::fmod(time, (double)d) : 0.0f);
        }
        for (const auto& [key, w] : manualWeights) pose.setWeight(key.first, (size_t)key.second, w);
        pose.updateWorld();
    }

    // ---- lights --------------------------------------------------------------------------------------
    static glm::vec3 fromAngles(float yawDeg, float pitchDeg) {   // a unit vector towards (yaw, pitch)
        const float y = glm::radians(yawDeg), p = glm::radians(pitchDeg);
        return glm::vec3(std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y));
    }

    // A rig around the model's bounds: "default" (key + fill directional), "point", "spot", "all".
    void buildRig(const std::string& kind) {
        rigKind = kind;
        rig.clear();
        const glm::vec3 c = (sceneMin + sceneMax) * 0.5f;
        auto dirLight = [&](float yawDeg, float pitchDeg, float intensity, bool shadows) {
            RigLight r;
            r.yaw = yawDeg;
            r.pitch = pitchDeg;
            r.l.type = GltfRenderer::Light::Type::Directional;
            r.l.intensity = intensity;
            r.l.castShadows = shadows;
            r.l.color = glm::vec3(1.0f, 0.97f, 0.92f);
            return r;
        };
        auto placed = [&](GltfRenderer::Light::Type type, float yawDeg, float pitchDeg, float dist, glm::vec3 color) {
            RigLight r;
            r.yaw = yawDeg;
            r.pitch = pitchDeg;
            r.l.type = type;
            r.l.position = c + fromAngles(yawDeg, pitchDeg) * dist;
            r.l.intensity = 3.5f * dist * dist;   // about the key light's strength at the model
            r.l.color = color;
            r.l.innerCone = 0.3f;
            r.l.outerCone = 0.55f;
            return r;
        };
        const float d = radius * 2.2f;
        if (kind == "point") {
            rig.push_back(placed(GltfRenderer::Light::Type::Point, -70.0f, 40.0f, d, glm::vec3(1.0f, 0.92f, 0.8f)));
            rig.push_back(dirLight(150.0f, 25.0f, 0.4f, false));
        } else if (kind == "spot") {
            rig.push_back(placed(GltfRenderer::Light::Type::Spot, -40.0f, 50.0f, d, glm::vec3(0.9f, 0.95f, 1.0f)));
            rig.push_back(dirLight(150.0f, 25.0f, 0.4f, false));
        } else if (kind == "all") {
            rig.push_back(dirLight(-35.0f, 45.0f, 1.6f, true));
            rig.push_back(placed(GltfRenderer::Light::Type::Point, 80.0f, 30.0f, d, glm::vec3(1.0f, 0.6f, 0.35f)));
            rig.push_back(placed(GltfRenderer::Light::Type::Spot, -120.0f, 55.0f, d, glm::vec3(0.45f, 0.65f, 1.0f)));
        } else {
            rig.push_back(dirLight(-35.0f, 45.0f, 3.0f, true));
            rig.push_back(dirLight(145.0f, 20.0f, 0.75f, false));
        }
    }

    // This frame's lights: the model's own (KHR_lights_punctual) or the rig.
    std::vector<GltfRenderer::Light> frameLights() {
        std::vector<GltfRenderer::Light> out;
        if (useFileLights && model && !model->lights.empty()) {
            for (const gltf::PlacedLight& p : pose.lights()) {
                const gltf::Light& s = model->lights[(size_t)p.light];
                GltfRenderer::Light l;
                l.type = s.type == gltf::Light::Type::Directional ? GltfRenderer::Light::Type::Directional
                       : s.type == gltf::Light::Type::Spot ? GltfRenderer::Light::Type::Spot : GltfRenderer::Light::Type::Point;
                l.color = s.color;
                l.intensity = s.intensity;
                l.position = p.position;
                l.direction = p.direction;
                l.range = s.range;
                l.innerCone = s.innerCone;
                l.outerCone = s.outerCone;
                out.push_back(l);
                if ((int)out.size() == GltfRenderer::kMaxLights) break;
            }
            return out;
        }
        for (RigLight& r : rig) {
            if (r.l.type != GltfRenderer::Light::Type::Point) {   // aim: from (yaw, pitch) at the model
                if (r.l.type == GltfRenderer::Light::Type::Directional) r.l.direction = -fromAngles(r.yaw, r.pitch);
                else r.l.direction = glm::normalize((sceneMin + sceneMax) * 0.5f - r.l.position);
            }
            out.push_back(r.l);
        }
        return out;
    }

    void makeGround() {   // a unit quad, scaled under the model each frame
        gltf::Model& g = groundModel;
        gltf::Primitive p;
        p.position = {{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}};
        p.normal.assign(4, glm::vec3(0, 1, 0));
        p.uv0 = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        p.indices = {0, 2, 1, 0, 3, 2};
        p.material = 0;
        p.boundsMin = {-1, 0, -1};
        p.boundsMax = {1, 0, 1};
        gltf::Mesh mesh;
        mesh.name = "ground";
        mesh.primitives.push_back(p);
        g.meshes.push_back(mesh);
        gltf::Material mat;
        mat.name = "ground";
        mat.baseColor = glm::vec4(0.5f, 0.5f, 0.52f, 1.0f);
        mat.metallic = 0.0f;
        mat.roughness = 0.9f;
        mat.doubleSided = true;
        g.materials.push_back(mat);
        gltf::Node node;
        node.mesh = 0;
        g.nodes.push_back(node);
        g.scenes.push_back({"ground", {0}});
        groundPose.bind(&groundModel);
        groundGpu = renderer.upload(groundModel);
    }

    // ---- input -----------------------------------------------------------------------------------
    void onEvent(const SDL_Event& e, bool& quit) {
        const bool uiMouse = showUi && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
        switch (e.type) {
        case SDL_EVENT_QUIT: quit = true; break;
        case SDL_EVENT_DROP_FILE:
            if (e.drop.data) load(e.drop.data);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (uiMouse) break;
            if (e.button.button == SDL_BUTTON_LEFT) dragOrbit = true;
            else dragPan = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            dragOrbit = dragPan = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (dragOrbit) {
                yaw -= e.motion.xrel * 0.4f;
                pitch = std::clamp(pitch + e.motion.yrel * 0.4f, -89.0f, 89.0f);
            } else if (dragPan) {
                const glm::mat4 v = viewMatrix();
                const glm::vec3 right(v[0][0], v[1][0], v[2][0]), up(v[0][1], v[1][1], v[2][1]);
                const float k = distance * std::tan(glm::radians(fov * 0.5f)) * 2.0f / std::max(1, fbH) * pixelScale();
                target += (-e.motion.xrel * right + e.motion.yrel * up) * k;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (!uiMouse) distance *= std::pow(0.88f, e.wheel.y);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) break;
            switch (e.key.key) {
            case SDLK_ESCAPE: quit = true; break;
            case SDLK_SPACE: paused = !paused; break;
            case SDLK_F: frameScene(); break;
            case SDLK_G: grid = !grid; break;
            case SDLK_K: skeleton = !skeleton; break;
            case SDLK_B: boundsBox = !boundsBox; break;
            case SDLK_W: wireframe = !wireframe; break;
            case SDLK_R: autoRotate = !autoRotate; break;
            case SDLK_H: showUi = !showUi; break;
            default:
                if (model && e.key.key >= SDLK_1 && e.key.key <= SDLK_9) {
                    const size_t i = size_t(e.key.key - SDLK_1);
                    if (i < playing.size()) {
                        std::fill(playing.begin(), playing.end(), 0);
                        playing[i] = 1;
                        time = 0.0;
                    }
                }
            }
            break;
        default: break;
        }
    }

    float pixelScale() const {   // window points -> backbuffer pixels
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        return w > 0 ? float(fbW) / float(w) : 1.0f;
    }

    glm::vec3 eye() const {
        const float y = glm::radians(yaw), p = glm::radians(pitch);
        return target + distance * glm::vec3(std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y));
    }
    glm::mat4 viewMatrix() const { return glm::lookAt(eye(), target, glm::vec3(0, 1, 0)); }

    // ---- debug lines -----------------------------------------------------------------------------
    void drawLines() {
        std::vector<GltfRenderer::LinePoint> pts;
        auto line = [&](const glm::vec3& a, const glm::vec3& b, uint32_t c) {
            pts.push_back({a, c});
            pts.push_back({b, c});
        };
        if (grid) {   // on the floor under the model, 20 cells over its size
            const float step = std::pow(10.0f, std::floor(std::log10(radius * 2.0f))) * 0.25f;
            const int n = 20;
            const float y = sceneMin.y + radius * 0.001f;   // over the ground quad
            const glm::vec3 c(std::round(target.x / step) * step, y, std::round(target.z / step) * step);
            for (int i = -n; i <= n; i++) {
                const uint32_t col = i == 0 ? abgr(0.55f, 0.55f, 0.55f, 0.8f) : abgr(0.42f, 0.42f, 0.42f, 0.45f);
                line(c + glm::vec3(i * step, 0, -n * step), c + glm::vec3(i * step, 0, n * step), col);
                line(c + glm::vec3(-n * step, 0, i * step), c + glm::vec3(n * step, 0, i * step), col);
            }
        }
        if (axes) {
            const float l = radius * 0.6f;
            line(glm::vec3(0), glm::vec3(l, 0, 0), abgr(1, 0.25f, 0.25f));
            line(glm::vec3(0), glm::vec3(0, l, 0), abgr(0.3f, 1, 0.3f));
            line(glm::vec3(0), glm::vec3(0, 0, l), abgr(0.3f, 0.5f, 1));
        }
        if (boundsBox && model) {
            glm::vec3 mn, mx;
            if (pose.bounds(mn, mx))
                for (int a = 0; a < 3; a++)
                    for (int c = 0; c < 4; c++) {
                        glm::vec3 p0, p1;
                        for (int k = 0, bit = 0; k < 3; k++) {
                            if (k == a) {
                                p0[k] = mn[k];
                                p1[k] = mx[k];
                            } else {
                                p0[k] = p1[k] = ((c >> bit) & 1) ? mx[k] : mn[k];
                                bit++;
                            }
                        }
                        line(p0, p1, abgr(1, 0.85f, 0.2f));
                    }
        }
        // The lights: a directional light's arrow, a point light's star, a spot light's cone.
        if (showUi)
            for (const GltfRenderer::Light& l : frame.lights) {
                const uint32_t col = abgr(std::min(l.color.r, 1.0f), std::min(l.color.g, 1.0f), std::min(l.color.b, 1.0f), 0.9f);
                const float s = radius * 0.12f;
                if (l.type == GltfRenderer::Light::Type::Directional) {
                    const glm::vec3 a = target - l.direction * radius * 1.6f, b = target - l.direction * radius * 1.1f;
                    line(a, b, col);
                    const glm::vec3 side = glm::normalize(glm::cross(l.direction, std::fabs(l.direction.y) > 0.9f ? glm::vec3(1, 0, 0)
                                                                                                                 : glm::vec3(0, 1, 0)));
                    line(b, b - l.direction * s * -1.0f + side * s * 0.5f, col);
                    line(b, b - l.direction * s * -1.0f - side * s * 0.5f, col);
                } else {
                    for (const glm::vec3& ax : {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)})
                        line(l.position - ax * s, l.position + ax * s, col);
                    if (l.type == GltfRenderer::Light::Type::Spot) {
                        const glm::vec3 d = glm::normalize(l.direction);
                        const glm::vec3 u = glm::normalize(glm::cross(d, std::fabs(d.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
                        const glm::vec3 w = glm::cross(d, u);
                        const float len = radius * 0.8f, rr = len * std::tan(l.outerCone);
                        for (int k = 0; k < 8; k++) {
                            const float a0 = 6.2831853f * k / 8, a1 = 6.2831853f * (k + 1) / 8;
                            const glm::vec3 p0 = l.position + d * len + (u * std::cos(a0) + w * std::sin(a0)) * rr;
                            const glm::vec3 p1 = l.position + d * len + (u * std::cos(a1) + w * std::sin(a1)) * rr;
                            line(p0, p1, col);
                            if (k % 2 == 0) line(l.position, p0, col);
                        }
                    }
                }
            }
        renderer.lines(pts, true);
        if (skeleton && model) {   // over the model: joints to their parent joints
            std::vector<GltfRenderer::LinePoint> bones;
            for (const gltf::Skin& s : model->skins)
                for (int j : s.joints) {
                    const int parent = model->nodes[(size_t)j].parent;
                    if (parent < 0 || std::find(s.joints.begin(), s.joints.end(), parent) == s.joints.end()) continue;
                    bones.push_back({glm::vec3(pose.world(parent)[3]), abgr(0.3f, 1.0f, 0.9f)});
                    bones.push_back({glm::vec3(pose.world(j)[3]), abgr(1.0f, 0.4f, 0.9f)});
                }
            renderer.lines(bones, false);
        }
    }

    // ---- the panel -------------------------------------------------------------------------------
    void panel() {
        const ImGuiIO& io = ImGui::GetIO();
        const float w = std::min(380.0f * uiScale, io.DisplaySize.x * 0.34f);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - w - 8 * uiScale, 8 * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(w, io.DisplaySize.y - 16 * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        ImGui::Begin("glTF Viewer", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        if (ImGui::Button("Open...")) {
            static const SDL_DialogFileFilter filters[] = {{"glTF 2.0", "gltf;glb"}};
            SDL_ShowOpenFileDialog(
                [](void* user, const char* const* list, int) {
                    if (!list || !list[0]) return;
                    Viewer* v = (Viewer*)user;
                    std::lock_guard<std::mutex> g(v->pendingLock);
                    v->pending = list[0];
                },
                this, window, filters, 1, nullptr, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Frame (F)")) frameScene();
        ImGui::TextDisabled("or drop a .gltf / .glb on the window");
        if (!error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", error.c_str());
        if (!model) {
            ImGui::TextWrapped("No model. Drop a .gltf or .glb on the window.");
            ImGui::End();
            return;
        }
        ImGui::TextWrapped("%s", path.c_str());

        if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen)) {
            const gltf::Model& m = *model;
            size_t joints = 0;
            for (const gltf::Skin& s : m.skins) joints += s.joints.size();
            ImGui::TextWrapped("%zu vertices, %zu triangles", m.vertexCount(), m.triangleCount());
            ImGui::TextWrapped("%zu nodes, %zu meshes, %zu materials, %zu textures", m.nodes.size(), m.meshes.size(), m.materials.size(),
                        m.textures.size());
            ImGui::TextWrapped("%zu skins (%zu joints), %zu morph targets", m.skins.size(), joints, m.morphTargetCount());
            ImGui::TextWrapped("%zu instances (EXT_mesh_gpu_instancing)", m.instanceCount());
            if (!m.generator.empty()) ImGui::TextWrapped("generator: %s", m.generator.c_str());
            for (const std::string& e : m.extensionsUsed) {
                const bool ignored = std::find(m.ignoredExtensions.begin(), m.ignoredExtensions.end(), e) != m.ignoredExtensions.end();
                ImGui::TextColored(ignored ? ImVec4(1, 0.75f, 0.3f, 1) : ImVec4(0.5f, 0.9f, 0.5f, 1), "%s %s", ignored ? "-" : "+", e.c_str());
                if (ignored && ImGui::IsItemHovered()) ImGui::SetTooltip("used by the file, not drawn by this engine yet");
            }
            for (const std::string& wn : m.warnings) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "! %s", wn.c_str());
        }

        if (!model->animations.empty() && ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button(paused ? "Play" : "Pause")) paused = !paused;
            ImGui::SameLine();
            if (ImGui::Button("All")) std::fill(playing.begin(), playing.end(), 1);
            ImGui::SameLine();
            if (ImGui::Button("None")) std::fill(playing.begin(), playing.end(), 0);
            ImGui::SliderFloat("speed", &speed, 0.0f, 3.0f, "%.2fx");
            float t = (float)std::fmod(time, std::max(maxDuration(), 1e-6f));
            if (ImGui::SliderFloat("time", &t, 0.0f, std::max(maxDuration(), 0.001f), "%.3f s")) {
                time = t;
                paused = true;
            }
            for (size_t i = 0; i < model->animations.size(); i++) {
                const gltf::Animation& a = model->animations[i];
                bool on = playing[i] != 0;
                const std::string label = std::to_string(i + 1) + ". " + a.name + "##anim" + std::to_string(i);
                if (ImGui::Checkbox(label.c_str(), &on)) playing[i] = on;
                ImGui::SameLine();
                ImGui::TextDisabled("%.2f s, %zu channels", a.duration, a.channels.size());
            }
        }

        if (model->morphTargetCount() > 0 && ImGui::CollapsingHeader("Morph targets", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("A slider overrides the animation for that weight.");
            for (size_t n = 0; n < model->nodes.size(); n++) {
                const gltf::Node& node = model->nodes[n];
                if (node.mesh < 0) continue;
                const gltf::Mesh& mesh = model->meshes[(size_t)node.mesh];
                if (mesh.weights.empty()) continue;
                ImGui::Text("%s", node.name.empty() ? mesh.name.c_str() : node.name.c_str());
                for (size_t k = 0; k < mesh.weights.size(); k++) {
                    float wv = pose.weights((int)n)[k];
                    const std::string name = (k < mesh.targetNames.size() ? mesh.targetNames[k] : "target " + std::to_string(k)) + "##w" +
                                             std::to_string(n) + "_" + std::to_string(k);
                    if (ImGui::SliderFloat(name.c_str(), &wv, 0.0f, 1.0f)) manualWeights[{(int)n, (int)k}] = wv;
                }
            }
            if (!manualWeights.empty() && ImGui::Button("Back to the animation")) manualWeights.clear();
        }

        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("grid (G)", &grid);
            ImGui::SameLine();
            ImGui::Checkbox("axes", &axes);
            ImGui::SameLine();
            ImGui::Checkbox("bounds (B)", &boundsBox);
            ImGui::Checkbox("skeleton (K)", &skeleton);
            ImGui::SameLine();
            ImGui::Checkbox("wireframe (W)", &wireframe);
            ImGui::Checkbox("auto-rotate (R)", &autoRotate);
            ImGui::ColorEdit3("background", &background.x, ImGuiColorEditFlags_NoInputs);
            ImGui::SliderFloat("fov", &fov, 10.0f, 100.0f, "%.0f deg");
            static const char* views[] = {"lit", "normals", "base colour", "metal / rough", "uv", "shadow (first light)"};
            ImGui::Combo("view", &frame.debug, views, 6);
        }

        if (ImGui::CollapsingHeader("Lights and shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!renderer.shadowsSupported()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "This backend has no shadow maps.");
            ImGui::Checkbox("shadows", &frame.shadows);
            ImGui::SameLine();
            ImGui::Checkbox("ground", &groundOn);
            static const int sizes[] = {1024, 2048, 4096, 8192};
            static const char* sizeNames[] = {"1024", "2048", "4096", "8192"};
            int si = 2;
            for (int i = 0; i < 4; i++)
                if (sizes[i] == frame.shadowMapSize) si = i;
            if (ImGui::Combo("shadow atlas", &si, sizeNames, 4)) frame.shadowMapSize = sizes[si];
            ImGui::SliderFloat("shadow bias", &frame.shadowBias, 0.2f, 5.0f, "x %.2f");
            if (model && !model->lights.empty())
                ImGui::Checkbox(("the file's lights (" + std::to_string(model->lights.size()) + ", KHR_lights_punctual)").c_str(),
                                &useFileLights);
            const bool fileLights = useFileLights && model && !model->lights.empty();
            if (!fileLights) {
                static const char* rigs[] = {"default", "point", "spot", "all"};
                int ri = 0;
                for (int i = 0; i < 4; i++)
                    if (rigKind == rigs[i]) ri = i;
                if (ImGui::Combo("rig", &ri, rigs, 4)) buildRig(rigs[ri]);
                static const char* types[] = {"directional", "point", "spot"};
                int remove = -1;
                for (size_t i = 0; i < rig.size(); i++) {
                    RigLight& r = rig[i];
                    ImGui::PushID((int)i);
                    const std::string title = std::to_string(i + 1) + ". " + types[(int)r.l.type] + (r.l.castShadows ? "  (shadows)" : "");
                    if (ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        int t = (int)r.l.type;
                        if (ImGui::Combo("type", &t, types, 3)) r.l.type = (GltfRenderer::Light::Type)t;
                        ImGui::ColorEdit3("colour", &r.l.color.x, ImGuiColorEditFlags_NoInputs);
                        ImGui::SameLine();
                        ImGui::Checkbox("cast shadows", &r.l.castShadows);
                        if (r.l.type == GltfRenderer::Light::Type::Directional) {
                            ImGui::SliderFloat("intensity", &r.l.intensity, 0.0f, 10.0f);
                            ImGui::SliderFloat("yaw", &r.yaw, -180.0f, 180.0f, "%.0f deg");
                            ImGui::SliderFloat("pitch", &r.pitch, -10.0f, 90.0f, "%.0f deg");
                        } else {
                            ImGui::DragFloat("intensity", &r.l.intensity, std::max(r.l.intensity * 0.01f, 0.01f), 0.0f, 1e6f, "%.2f");
                            ImGui::DragFloat3("position", &r.l.position.x, radius * 0.01f);
                            ImGui::DragFloat("range", &r.l.range, radius * 0.01f, 0.0f, 1e5f, r.l.range > 0 ? "%.2f" : "infinite");
                            if (r.l.type == GltfRenderer::Light::Type::Spot) {
                                float inner = glm::degrees(r.l.innerCone), outer = glm::degrees(r.l.outerCone);
                                if (ImGui::SliderFloat("inner cone", &inner, 0.0f, 89.0f, "%.0f deg")) r.l.innerCone = glm::radians(inner);
                                if (ImGui::SliderFloat("outer cone", &outer, 1.0f, 89.0f, "%.0f deg")) r.l.outerCone = glm::radians(outer);
                                ImGui::TextDisabled("(aims at the model)");
                            }
                        }
                        if (ImGui::SmallButton("remove")) remove = (int)i;
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                if (remove >= 0) rig.erase(rig.begin() + remove);
                if ((int)rig.size() < GltfRenderer::kMaxLights) {
                    for (int t = 0; t < 3; t++) {
                        if (t) ImGui::SameLine();
                        if (ImGui::Button((std::string("+ ") + types[t]).c_str())) {
                            RigLight r;
                            r.l.type = (GltfRenderer::Light::Type)t;
                            r.yaw = 60.0f * (float)rig.size();
                            r.pitch = 45.0f;
                            r.l.position = (sceneMin + sceneMax) * 0.5f + fromAngles(r.yaw, r.pitch) * radius * 2.2f;
                            if (t) r.l.intensity = 3.5f * radius * radius * 4.84f;
                            rig.push_back(r);
                        }
                    }
                }
            }
            ImGui::Separator();
            ImGui::SliderFloat("exposure", &frame.exposure, 0.1f, 4.0f);
            ImGui::SliderFloat("ambient", &frame.ambient, 0.0f, 3.0f);
            ImGui::ColorEdit3("sky", &frame.sky.x, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine();
            ImGui::ColorEdit3("ground", &frame.ground.x, ImGuiColorEditFlags_NoInputs);
            ImGui::Checkbox("ACES tone mapping", &frame.tonemap);
        }

        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            const GltfRenderer::Stats& s = renderer.stats();
            ImGui::Text("%.1f fps   %s", fps, next::bgfxHostRendererName().c_str());
            ImGui::Text("%d draw calls, %d instances", s.drawCalls, s.instances);
            ImGui::Text("%d skinned draws, %d morph uploads", s.skinnedDraws, s.morphUploads);
            ImGui::Text("%d shadow tiles, %d shadow draws", s.shadowTiles, s.shadowDraws);
        }
        ImGui::End();
    }

    // ---- one frame -------------------------------------------------------------------------------
    void tick() {
        const uint64_t now = SDL_GetTicksNS();
        const float dt = lastTicks ? float(double(now - lastTicks) * 1e-9) : 1.0f / 60.0f;
        lastTicks = now;
        fpsAccum += dt;
        if (++fpsFrames >= 20) {
            fps = fpsFrames / std::max(fpsAccum, 1e-6f);
            fpsAccum = 0;
            fpsFrames = 0;
        }
        {
            std::string next;
            {
                std::lock_guard<std::mutex> g(pendingLock);
                next.swap(pending);
            }
            if (!next.empty()) load(next);
        }
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        if (w != fbW || h != fbH) {
            fbW = w;
            fbH = h;
            next::bgfxHostReset((uint32_t)w, (uint32_t)h);
        }

        if (args.time >= 0.0f) time = args.time;
        else if (!paused) time += double(dt) * speed;
        if (autoRotate) yaw += dt * 20.0f;

        // UI first (it decides whether the mouse belongs to it).
        float mx = 0, my = 0;
        const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
        next::ImGuiBgfx::Input in;
        const float ps = pixelScale();
        in.mouseX = mx * ps;
        in.mouseY = my * ps;
        in.mouseDown[0] = (buttons & SDL_BUTTON_LMASK) != 0;
        in.mouseDown[1] = (buttons & SDL_BUTTON_RMASK) != 0;
        in.mouseDown[2] = (buttons & SDL_BUTTON_MMASK) != 0;
        in.wheel = wheel;
        wheel = 0;
        gui.newFrame((uint32_t)fbW, (uint32_t)fbH, dt, in);
        if (showUi) panel();

        // The scene.
        if (model) posed();
        frame.lights = frameLights();
        // Shadows cover the model and the floor around it (where its shadow falls).
        frame.boundsMin = glm::vec3(sceneMin.x - radius, sceneMin.y, sceneMin.z - radius);
        frame.boundsMax = glm::vec3(sceneMax.x + radius, sceneMax.y, sceneMax.z + radius);
        frame.eye = eye();
        frame.view = viewMatrix();
        const float zn = std::max(distance - radius * 4.0f, distance * 0.002f);
        frame.proj = GltfRenderer::projection(fov, float(fbW) / float(std::max(fbH, 1)), zn, distance + radius * 50.0f);
        frame.srgbOut = false;   // the host's backbuffer is sRGB: the GPU encodes
        if (showUi && fbW > 0) {   // centre the model in the part the panel leaves free
            const float panel = std::min(380.0f * uiScale, float(fbW) * 0.34f) + 16.0f * uiScale;
            frame.proj = glm::translate(glm::mat4(1.0f), glm::vec3(-panel / float(fbW), 0.0f, 0.0f)) * frame.proj;
        }
        renderer.begin(0, frame, (uint32_t)fbW, (uint32_t)fbH, rgbaHex(background));   // views 0 .. kViews - 1
        if (model && gpu) renderer.draw(*gpu, pose);
        if (groundOn && model && groundGpu) {   // a floor under the model: receives shadows, casts none
            const glm::vec3 c = (sceneMin + sceneMax) * 0.5f;
            const glm::mat4 place = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(c.x, sceneMin.y - radius * 0.002f, c.z)),
                                               glm::vec3(radius * 6.0f));
            renderer.draw(*groundGpu, groundPose, place, false);
        }
        drawLines();
        bgfx::setDebug(wireframe ? BGFX_DEBUG_WIREFRAME : BGFX_DEBUG_NONE);
        gui.render(GltfRenderer::kViews + 4);

        if (args.frames > 0 && frameNo == args.frames && !args.screenshot.empty()) {
            shotsBefore = next::bgfxHostScreenshotsWritten();
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, args.screenshot.c_str());
        }
        next::bgfxHostFrame();
        frameNo++;
    }

    float wheel = 0.0f;
};

}  // namespace

int main(int argc, char** argv) {
    Viewer v;
    v.args = parseArgs(argc, argv);
    v.showUi = v.args.ui;
    v.yaw = v.args.yaw;
    v.pitch = v.args.pitch;
    v.frame.debug = std::clamp(v.args.debug, 0, 5);
    v.frame.shadows = v.args.shadows;
    v.frame.shadowBias = v.args.shadowBias;
    v.groundOn = v.args.ground;
    v.useFileLights = v.args.light == "file" || v.args.light == "default";
    v.rigKind = v.args.light == "file" ? std::string("default") : v.args.light;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    v.window = SDL_CreateWindow("TOMS glTF Viewer", v.args.w, v.args.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!v.window) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    next::BgfxHostConfig cfg;
#if defined(_WIN32)
    cfg.nativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(v.window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__linux__)
    cfg.nativeWindow = (void*)(uintptr_t)SDL_GetNumberProperty(SDL_GetWindowProperties(v.window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    cfg.nativeDisplay = SDL_GetPointerProperty(SDL_GetWindowProperties(v.window), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
#endif
    SDL_GetWindowSizeInPixels(v.window, &v.fbW, &v.fbH);
    cfg.width = (uint32_t)v.fbW;
    cfg.height = (uint32_t)v.fbH;
    cfg.renderer = v.args.renderer;
    cfg.msaa = 4;
    std::string err;
    if (!next::bgfxHostInit(cfg, err)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "glTF Viewer", err.c_str(), v.window);
        return 1;
    }
    if (!v.renderer.init() || !v.gui.init()) {
        std::fprintf(stderr, "[gltf] the renderer could not start\n");
        next::bgfxHostShutdown();
        return 1;
    }
    v.makeGround();
    v.uiScale = std::max(1.0f, SDL_GetWindowDisplayScale(v.window) * 0.8f);
    ImGui::GetIO().FontGlobalScale = v.uiScale;
    ImGui::GetStyle().ScaleAllSizes(v.uiScale);
    ImGui::GetStyle().WindowRounding = 6.0f * v.uiScale;
    if (!v.args.file.empty() && !v.load(v.args.file) && v.args.frames > 0) {
        next::bgfxHostShutdown();
        return 2;   // a test asked for a model that does not load
    }

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_MOUSE_WHEEL) v.wheel += e.wheel.y;
            v.onEvent(e, quit);
        }
        v.tick();
        // A test: quit once the screenshot is on disk (or a while after asking).
        if (v.args.frames > 0 && v.frameNo > v.args.frames &&
            (v.args.screenshot.empty() || next::bgfxHostScreenshotsWritten() > v.shotsBefore || v.frameNo > v.args.frames + 60))
            quit = true;
    }
    v.gpu.reset();
    v.groundGpu.reset();
    v.renderer.shutdown();
    v.gui.shutdown();
    next::bgfxHostShutdown();
    SDL_DestroyWindow(v.window);
    SDL_Quit();
    return 0;
}
