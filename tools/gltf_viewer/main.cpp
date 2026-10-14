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
#include "bgfx_host.h"
#include "gltf_model.h"
#include "gltf_physics.h"
#include "gltf_renderer.h"
#include "imgui_bgfx.h"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
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
    std::string camera;
    float lookYaw = 0.0f, lookPitch = 0.0f;
    int crowd = 0;
    float crowdFps = 30.0f;
    bool physics = true;
    std::string select, tool;
    float pickX = -1.0f, pickY = -1.0f;
    glm::vec4 drag{-1.0f};
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
        else if (const char* v = value(s, "--camera=")) a.camera = v;
        else if (const char* v = value(s, "--look=")) std::sscanf(v, "%f,%f", &a.lookYaw, &a.lookPitch);
        else if (const char* v = value(s, "--crowd=")) a.crowd = std::max(0, std::atoi(v));
        else if (const char* v = value(s, "--crowd-fps=")) a.crowdFps = (float)std::atof(v);
        else if (const char* v = value(s, "--physics=")) a.physics = std::atoi(v) != 0;
        else if (const char* v = value(s, "--select=")) a.select = v;
        else if (const char* v = value(s, "--tool=")) a.tool = v;
        else if (const char* v = value(s, "--pick=")) std::sscanf(v, "%f,%f", &a.pickX, &a.pickY);
        else if (const char* v = value(s, "--drag=")) std::sscanf(v, "%f,%f,%f,%f", &a.drag.x, &a.drag.y, &a.drag.z, &a.drag.w);
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
    // -1 = the free orbit camera, else one of the file's cameras (index into pose.cameras()). Moving
    // the view (drag, wheel, arrows) leaves a file camera for the free one, from the same spot.
    int activeCamera = -1;
    // Looking around from a file camera, in its own frame (it keeps riding its node's animation):
    // a head turn (yaw about the camera's up), a nod (pitch about its right), a zoom of its lens.
    float lookYaw = 0.0f, lookPitch = 0.0f, lookZoom = 1.0f;
    bool dragLook = false;
    void selectCamera(int i) {
        activeCamera = i;
        lookYaw = lookPitch = 0.0f;
        lookZoom = 1.0f;
    }
    // Crowd mode (--crowd=N): N copies of the model's first skin, animated from one baked texture.
    int crowdCount = 0;                   // 0 = off
    std::shared_ptr<gltf::AnimTexture> animTex;
    std::shared_ptr<GltfRenderer::AnimTextureGpu> animGpu;
    gltf::Crowd crowd;
    float crowdBlend = 0.5f;              // the panel's held-blend weight
    double bakeMs = 0.0;
    std::string crowdError;
    glm::vec3 modelMin{-1.0f}, modelMax{1.0f};   // one copy's bounds (the crowd's spacing)

    // Bakes the first skinned node's skin and lays N copies out on a square grid. Deterministic: copy i plays
    // clip i % clips from time i x 0.37 s; every third holds a blend of two clips.
    void buildCrowd(int n) {
        crowdCount = n;
        crowd.clear();
        crowdError.clear();
        if (n <= 0 || !model) return;
        int skin = -1;
        for (const gltf::Node& nd : model->nodes)
            if (nd.skin >= 0 && nd.mesh >= 0) { skin = nd.skin; break; }
        if (skin < 0) { crowdError = "crowd mode needs a skinned model"; return; }
        if (!renderer.crowdsSupported()) { crowdError = "this backend cannot draw crowds (no RGBA32F textures)"; return; }
        if (!animTex || animTex->skin != skin) {
            const uint64_t t0 = SDL_GetTicksNS();
            auto baked = std::make_shared<gltf::AnimTexture>();
            std::string err;
            if (!gltf::bakeAnimTexture(*model, skin, args.crowdFps, *baked, &err)) { crowdError = err; return; }
            bakeMs = double(SDL_GetTicksNS() - t0) * 1e-6;
            animTex = baked;
            animGpu = renderer.uploadAnimTexture(animTex);
            if (!animGpu) { crowdError = "the animation texture could not be uploaded"; return; }
            std::fprintf(stderr, "[gltf] crowd: baked %d joints x %d frames (%zu clips) in %.1f ms, %.1f KB\n", animTex->joints,
                         animTex->rows, animTex->clips.size(), bakeMs, animTex->bytes() / 1024.0);
        }
        crowd.setTexture(animTex);
        const int clips = (int)animTex->clips.size();
        const int side = (int)std::ceil(std::sqrt((double)n));
        const glm::vec3 size = modelMax - modelMin;
        const float spacing = std::max({size.x, size.z, size.y * 0.5f, 1e-3f}) * 1.3f;
        for (int i = 0; i < n; i++) {
            const float x = ((i % side) - (side - 1) * 0.5f) * spacing, z = ((i / side) - (side - 1) * 0.5f) * spacing;
            const int c = crowd.add(glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, z)), i % clips, i * 0.37f);
            if (clips > 1 && i % 3 == 2) crowd.setBlend(c, i % clips, (i + 1) % clips, 0.5f);
        }
        frameScene();
    }

    // ---- node editing: the selected node, its edits (applied after the animations), the view handles ----
    struct NodeEdit {
        glm::vec3 t{0.0f}, s{1.0f};
        glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 euler{0.0f};             // degrees, what the inspector shows (kept, so it does not flip)
    };
    std::map<int, NodeEdit> edits;
    int selected = -1;
    bool revealSelected = false;          // open the tree down to the selection (after a pick in the view)
    enum class Tool { Move, Rotate, Scale };
    Tool tool = Tool::Move;
    int hoverHandle = -1;                 // 0..2 an axis, 3 the centre, -1 none
    int dragHandle = -1;
    glm::vec2 dragFrom{0.0f}, pressAt{0.0f};
    bool pressPending = false;            // a left press that is a click (pick) unless the mouse moves
    NodeEdit dragStart;
    glm::vec3 dragOrigin{0.0f}, dragAxis{0.0f};
    glm::vec2 dragScreen0{0.0f}, dragScreen1{0.0f};
    float dragLength = 1.0f;

    NodeEdit& editOf(int n) {
        auto it = edits.find(n);
        if (it != edits.end()) return it->second;
        NodeEdit e;
        e.t = pose.translation(n);
        e.r = pose.rotation(n);
        e.s = pose.scale(n);
        e.euler = glm::degrees(glm::eulerAngles(e.r));
        return edits[n] = e;
    }
    void editChanged(int n) {   // a body that moved: the simulation starts again from the edited scene
        if (physicsOn && model && model->physics.any()) buildPhysics();
        (void)n;
    }
    static glm::quat rotationOf(const glm::mat4& m) {
        glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
        const float l0 = glm::length(c0), l1 = glm::length(c1), l2 = glm::length(c2);
        return glm::normalize(glm::quat_cast(glm::mat3(c0 / std::max(l0, 1e-8f), c1 / std::max(l1, 1e-8f), c2 / std::max(l2, 1e-8f))));
    }
    glm::mat4 parentWorld(int n) const {
        const int p = model->nodes[(size_t)n].parent;
        return p >= 0 ? pose.world(p) : glm::mat4(1.0f);
    }

    // World <-> backbuffer pixels (x right, y down), with the frame the last tick drew.
    bool toScreen(const glm::vec3& p, glm::vec2& out) const {
        const glm::vec4 c = frame.proj * frame.view * glm::vec4(p, 1.0f);
        if (c.w <= 1e-6f) return false;
        out = {(c.x / c.w * 0.5f + 0.5f) * float(fbW), (0.5f - c.y / c.w * 0.5f) * float(fbH)};
        return true;
    }
    void rayAt(const glm::vec2& px, glm::vec3& origin, glm::vec3& dir) const {
        const glm::mat4 inv = glm::inverse(frame.proj * frame.view);
        const float x = px.x / float(std::max(fbW, 1)) * 2.0f - 1.0f, y = 1.0f - px.y / float(std::max(fbH, 1)) * 2.0f;
        const float zNear = bgfx::getCaps()->homogeneousDepth ? -1.0f : 0.0f;
        const glm::vec4 a = inv * glm::vec4(x, y, zNear, 1.0f), b = inv * glm::vec4(x, y, 0.5f, 1.0f);
        origin = glm::vec3(a) / a.w;
        dir = glm::normalize(glm::vec3(b) / b.w - origin);
    }

    // The node whose mesh a pixel hits first (its mesh bounds, instances included), else -1. Clicking again where the
    // selected node is hit picks the next node behind it (a see-through volume over a mesh, a parent's bounds).
    int pickNode(const glm::vec2& px) const {
        if (!model) return -1;
        glm::vec3 o, d;
        rayAt(px, o, d);
        std::vector<std::pair<float, int>> hits;
        for (size_t n = 0; n < model->nodes.size(); n++) {
            const gltf::Node& node = model->nodes[n];
            if (node.mesh < 0) continue;
            glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
            for (const gltf::Primitive& p : model->meshes[(size_t)node.mesh].primitives) {
                mn = glm::min(mn, p.boundsMin);
                mx = glm::max(mx, p.boundsMax);
            }
            if (mn.x > mx.x) continue;
            const glm::mat4 world = node.skin >= 0 ? glm::mat4(1.0f) : pose.world((int)n);   // skinned: model space
            std::vector<glm::mat4> placements;
            if (node.instances.empty()) placements.push_back(world);
            else for (const glm::mat4& i : node.instances) placements.push_back(world * i);
            for (const glm::mat4& m : placements) {
                const glm::mat4 inv = glm::inverse(m);
                const glm::vec3 lo = glm::vec3(inv * glm::vec4(o, 1.0f)), ld = glm::vec3(inv * glm::vec4(d, 0.0f));
                float t0 = 0.0f, t1 = FLT_MAX;
                bool hit = true;
                for (int k = 0; k < 3 && hit; k++) {
                    if (std::fabs(ld[k]) < 1e-12f) { hit = lo[k] >= mn[k] && lo[k] <= mx[k]; continue; }
                    float a = (mn[k] - lo[k]) / ld[k], b = (mx[k] - lo[k]) / ld[k];
                    if (a > b) std::swap(a, b);
                    t0 = std::max(t0, a);
                    t1 = std::min(t1, b);
                    hit = t0 <= t1;
                }
                if (hit) { hits.push_back({t0, (int)n}); break; }
            }
        }
        if (hits.empty()) return -1;
        std::sort(hits.begin(), hits.end());
        for (size_t i = 0; i + 1 < hits.size(); i++)
            if (hits[i].second == selected) return hits[i + 1].second;
        return hits[0].second;
    }

    // The handles of the selected node: where they are on screen. Move and rotate work in world axes, scale in the
    // node's own axes.
    glm::vec3 handleAxis(int k) const {
        if (tool != Tool::Scale) return glm::vec3(k == 0, k == 1, k == 2);
        const glm::mat4& w = pose.world(selected);
        const glm::vec3 a(w[k]);
        return glm::length(a) > 1e-8f ? glm::normalize(a) : glm::vec3(k == 0, k == 1, k == 2);
    }
    float handleLength(const glm::vec3& origin) const { return 0.16f * std::max(glm::length(frame.eye - origin), 1e-3f); }
    std::vector<glm::vec3> ring(const glm::vec3& origin, const glm::vec3& axis, float r) const {
        const glm::vec3 u = glm::normalize(glm::cross(axis, std::fabs(axis.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
        const glm::vec3 v = glm::cross(axis, u);
        std::vector<glm::vec3> pts;
        for (int i = 0; i <= 48; i++) {
            const float a = 6.2831853f * i / 48.0f;
            pts.push_back(origin + (u * std::cos(a) + v * std::sin(a)) * r);
        }
        return pts;
    }
    static float segmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
        const glm::vec2 ab = b - a;
        const float t = glm::dot(ab, ab) > 0.0f ? std::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
        return glm::length(p - (a + ab * t));
    }
    int handleAt(const glm::vec2& px) const {
        if (selected < 0 || !model) return -1;
        const glm::vec3 origin(pose.world(selected)[3]);
        const float L = handleLength(origin), tol = 8.0f * pixelScale();
        glm::vec2 c;
        if (!toScreen(origin, c)) return -1;
        int best = -1;
        float bestD = tol;
        if (tool != Tool::Rotate && glm::length(px - c) < 10.0f * pixelScale()) return 3;
        for (int k = 0; k < 3; k++) {
            const glm::vec3 axis = handleAxis(k);
            float d = FLT_MAX;
            if (tool == Tool::Rotate) {
                const std::vector<glm::vec3> pts = ring(origin, axis, L);
                for (size_t i = 0; i + 1 < pts.size(); i++) {
                    glm::vec2 a, b;
                    if (toScreen(pts[i], a) && toScreen(pts[i + 1], b)) d = std::min(d, segmentDistance(px, a, b));
                }
            } else {
                glm::vec2 e;
                if (toScreen(origin + axis * L, e)) d = segmentDistance(px, c, e);
            }
            if (d < bestD) { bestD = d; best = k; }
        }
        return best;
    }
    void startHandleDrag(int h, const glm::vec2& px) {
        dragHandle = h;
        dragFrom = px;
        dragStart = editOf(selected);
        dragOrigin = glm::vec3(pose.world(selected)[3]);
        dragLength = handleLength(dragOrigin);
        dragAxis = h < 3 ? handleAxis(h) : glm::vec3(0.0f);
        toScreen(dragOrigin, dragScreen0);
        if (h < 3) toScreen(dragOrigin + dragAxis * dragLength, dragScreen1);
        else {   // the centre: world units per pixel from the camera's right axis
            const glm::mat4& v = frame.view;
            toScreen(dragOrigin + glm::vec3(v[0][0], v[1][0], v[2][0]) * dragLength, dragScreen1);
        }
    }
    void dragHandleTo(const glm::vec2& px) {
        if (dragHandle < 0 || selected < 0) return;
        NodeEdit& e = editOf(selected);
        const glm::vec2 m = px - dragFrom;
        const glm::vec2 dir = dragScreen1 - dragScreen0;
        const float len = std::max(glm::length(dir), 1.0f);
        const glm::mat4 pw = parentWorld(selected);
        if (tool == Tool::Move) {
            glm::vec3 worldDelta;
            if (dragHandle < 3) worldDelta = dragAxis * (glm::dot(m, dir / len) / len * dragLength);
            else {
                const glm::mat4& v = frame.view;
                const glm::vec3 right(v[0][0], v[1][0], v[2][0]), up(v[0][1], v[1][1], v[2][1]);
                worldDelta = (right * m.x - up * m.y) * (dragLength / len);
            }
            e.t = dragStart.t + glm::inverse(glm::mat3(pw)) * worldDelta;
        } else if (tool == Tool::Rotate && dragHandle < 3) {
            const glm::vec2 a0 = dragFrom - dragScreen0, a1 = px - dragScreen0;
            float angle = std::atan2(a1.y, a1.x) - std::atan2(a0.y, a0.x);
            if (glm::dot(dragAxis, frame.eye - dragOrigin) > 0.0f) angle = -angle;   // the axis looks at us
            const glm::quat pr = rotationOf(pw);
            const glm::quat world = glm::angleAxis(angle, dragAxis) * (pr * dragStart.r);
            e.r = glm::normalize(glm::inverse(pr) * world);
            e.euler = glm::degrees(glm::eulerAngles(e.r));
        } else if (tool == Tool::Scale) {
            if (dragHandle < 3) {
                const float f = std::max(0.01f, 1.0f + glm::dot(m, dir / len) / len);
                e.s = dragStart.s;
                e.s[dragHandle] *= f;
            } else {
                e.s = dragStart.s * std::max(0.01f, 1.0f + (m.x - m.y) * 0.005f);
            }
        }
    }
    void drawHandles(std::vector<GltfRenderer::LinePoint>& out) {
        if (selected < 0 || !model || selected >= (int)model->nodes.size()) return;
        auto line = [&](const glm::vec3& a, const glm::vec3& b, uint32_t c) { out.push_back({a, c}); out.push_back({b, c}); };
        const glm::vec3 origin(pose.world(selected)[3]);
        const float L = handleLength(origin);
        const int active = dragHandle >= 0 ? dragHandle : hoverHandle;
        const uint32_t cols[3] = {abgr(1.0f, 0.3f, 0.3f), abgr(0.35f, 1.0f, 0.35f), abgr(0.35f, 0.55f, 1.0f)};
        const uint32_t hot = abgr(1.0f, 0.9f, 0.2f);
        const glm::mat4& v = frame.view;
        const glm::vec3 right(v[0][0], v[1][0], v[2][0]), up(v[0][1], v[1][1], v[2][1]);
        for (int k = 0; k < 3; k++) {
            const glm::vec3 axis = handleAxis(k);
            const uint32_t c = active == k ? hot : cols[k];
            if (tool == Tool::Rotate) {
                const std::vector<glm::vec3> pts = ring(origin, axis, L);
                for (size_t i = 0; i + 1 < pts.size(); i++) line(pts[i], pts[i + 1], c);
                continue;
            }
            const glm::vec3 tip = origin + axis * L;
            line(origin, tip, c);
            const float h = L * 0.07f;
            if (tool == Tool::Move) {   // an arrow head
                const glm::vec3 side = glm::normalize(glm::cross(axis, std::fabs(axis.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
                const glm::vec3 side2 = glm::cross(axis, side);
                for (const glm::vec3& sd : {side, -side, side2, -side2}) line(tip, tip - axis * h * 2.0f + sd * h, c);
            } else {                    // a small box
                for (const glm::vec3& a : {right, up}) {
                    const glm::vec3 b = a == right ? up : right;
                    line(tip + (a - b) * h, tip + (a + b) * h, c);
                    line(tip - (a + b) * h, tip + (-a + b) * h, c);
                }
            }
        }
        if (tool != Tool::Rotate) {   // the centre square
            const uint32_t c = active == 3 ? hot : abgr(0.9f, 0.9f, 0.9f);
            const float h = L * 0.06f;
            const glm::vec3 p[4] = {origin + (-right - up) * h, origin + (right - up) * h, origin + (right + up) * h, origin + (-right + up) * h};
            for (int i = 0; i < 4; i++) line(p[i], p[(i + 1) % 4], c);
        }
    }

    // ---- the Scene tree and the Node inspector ----
    void treeNode(int n) {
        const gltf::Node& node = model->nodes[(size_t)n];
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (n == selected) flags |= ImGuiTreeNodeFlags_Selected;
        if (revealSelected && selected >= 0) {   // open every ancestor of the selection
            for (int a = model->nodes[(size_t)selected].parent; a >= 0; a = model->nodes[(size_t)a].parent)
                if (a == n) ImGui::SetNextItemOpen(true);
        }
        std::string label = node.name.empty() ? "node " + std::to_string(n) : node.name;
        if (node.mesh >= 0) label += node.skin >= 0 ? "  [skinned mesh]" : "  [mesh]";
        if (node.camera >= 0) label += "  [camera]";
        if (node.light >= 0) label += "  [light]";
        if (model->physics.any()) {
            const gltf::NodePhysics& p = model->physics.nodes[(size_t)n];
            if (p.hasMotion) label += p.kinematic ? "  [kinematic]" : "  [body]";
            else if (p.hasCollider) label += "  [collider]";
            if (p.hasTrigger) label += "  [trigger]";
            if (p.hasJoint) label += "  [joint]";
        }
        if (edits.count(n)) label += "  *";
        ImGui::PushID(n);
        const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selected = n;
        if (n == selected && revealSelected) ImGui::SetScrollHereY(0.5f);
        if (open && !node.children.empty()) {
            for (int c : node.children) treeNode(c);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void sceneTreePanel() {
        if (ImGui::CollapsingHeader("Scene tree", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("click to select (or click a mesh in the view)");
            const float h = std::min(260.0f * uiScale, ImGui::GetContentRegionAvail().y * 0.5f);
            if (ImGui::BeginChild("tree", ImVec2(0, std::max(h, 80.0f * uiScale)), true)) {
                const std::vector<int>& roots = model->scenes.empty() ? std::vector<int>() : model->scenes[(size_t)std::max(model->scene, 0)].roots;
                for (int r : roots) treeNode(r);
            }
            ImGui::EndChild();
            revealSelected = false;
        }
        if (selected < 0 || selected >= (int)model->nodes.size()) return;
        if (!ImGui::CollapsingHeader("Node", ImGuiTreeNodeFlags_DefaultOpen)) return;
        const gltf::Node& node = model->nodes[(size_t)selected];
        ImGui::Text("%s  (#%d)", node.name.empty() ? "(no name)" : node.name.c_str(), selected);
        if (node.parent >= 0) {
            ImGui::SameLine();
            const std::string& pn = model->nodes[(size_t)node.parent].name;
            if (ImGui::SmallButton(("parent: " + (pn.empty() ? std::to_string(node.parent) : pn)).c_str())) { selected = node.parent; revealSelected = true; }
        }
        int t = (int)tool;
        ImGui::RadioButton("move (T)", &t, 0);
        ImGui::SameLine();
        ImGui::RadioButton("rotate (E)", &t, 1);
        ImGui::SameLine();
        ImGui::RadioButton("scale (S)", &t, 2);
        tool = (Tool)t;
        // The fields show the node's local transform now (animated), and edit a copy that replaces it.
        NodeEdit shown;
        const auto it = edits.find(selected);
        if (it != edits.end()) shown = it->second;
        else {
            shown.t = pose.translation(selected);
            shown.r = pose.rotation(selected);
            shown.s = pose.scale(selected);
            shown.euler = glm::degrees(glm::eulerAngles(shown.r));
        }
        const float speed = std::max(radius * 0.005f, 1e-4f);
        bool changed = false, done = false;
        if (ImGui::DragFloat3("translation", &shown.t.x, speed, 0, 0, "%.3f")) { editOf(selected).t = shown.t; changed = true; }
        done |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::DragFloat3("rotation (deg)", &shown.euler.x, 0.5f, 0, 0, "%.1f")) {
            NodeEdit& e = editOf(selected);
            e.euler = shown.euler;
            e.r = glm::quat(glm::radians(shown.euler));
            changed = true;
        }
        done |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::DragFloat3("scale", &shown.s.x, 0.005f, 0.0f, 0.0f, "%.3f")) { editOf(selected).s = shown.s; changed = true; }
        done |= ImGui::IsItemDeactivatedAfterEdit();
        float uniform = 1.0f;
        if (ImGui::DragFloat("uniform scale", &uniform, 0.005f, 0.01f, 100.0f, "x %.3f")) {
            NodeEdit& e = editOf(selected);
            e.s *= std::max(uniform, 0.01f);
            changed = true;
        }
        done |= ImGui::IsItemDeactivatedAfterEdit();
        const glm::vec3 wp(pose.world(selected)[3]);
        ImGui::TextDisabled("world position %.3f, %.3f, %.3f", wp.x, wp.y, wp.z);
        if (edits.count(selected)) {
            if (ImGui::Button("reset node (Delete)")) { edits.erase(selected); done = true; }
            ImGui::SameLine();
        }
        if (!edits.empty() && ImGui::Button(("reset all edits (" + std::to_string(edits.size()) + ")").c_str())) {
            edits.clear();
            done = true;
        }
        if (ImGui::Button("look at it")) target = wp;
        ImGui::SameLine();
        if (ImGui::Button("deselect")) selected = -1;
        if (changed) posed();
        if (done) editChanged(selected);
    }

    // Physics (KHR_physics_rigid_bodies, gltf_physics.h): the bodies move the pose's nodes after the animations.
    gltf::PhysicsWorld physics;
    bool physicsOn = false, physicsPaused = false, physicsBoxes = true, physicsFrozenDone = false;
    std::vector<std::string> physicsLog;   // the last trigger events

    void buildPhysics() {
        physics.clear();
        physicsFrozenDone = false;
        physicsLog.clear();
        if (!model || !model->physics.any() || !physicsOn) return;
        const double keep = time;
        time = 0.0;
        posed();   // the bodies start where the file (and the animations at time 0) put them
        std::string err;
        if (!physics.build(*model, pose, &err)) {
            std::fprintf(stderr, "[gltf] physics: %s\n", err.c_str());
            return;
        }
        time = keep;
        const gltf::PhysicsWorld::Stats st = physics.stats();
        std::fprintf(stderr, "[gltf] physics: %d dynamic, %d kinematic, %d static, %d triggers, %d joints\n", st.dynamic,
                     st.kinematic, st.statics, st.triggers, st.joints);
        for (const std::string& w : physics.warnings()) std::fprintf(stderr, "[gltf]   physics warning: %s\n", w.c_str());
    }

    // After posed(): advance the simulation (or, with --time, simulate up to that time once) and put the bodies back
    // into the pose the animations just reset.
    void simulate(float dt) {
        if (!physics.built()) return;
        if (args.time >= 0.0f) {
            if (!physicsFrozenDone) {
                physics.simulateTo(args.time, pose);
                physicsFrozenDone = true;
            }
        } else if (!physicsPaused) {
            physics.step(dt * speed, pose);
        }
        physics.writePose(pose);
        for (const gltf::TriggerEvent& e : physics.takeEvents()) {
            auto name = [&](int n) { return n >= 0 && n < (int)model->nodes.size() && !model->nodes[(size_t)n].name.empty()
                                                ? model->nodes[(size_t)n].name : std::to_string(n); };
            char line[160];
            std::snprintf(line, sizeof line, "%6.2f s  %s %s %s", physics.time(), name(e.other).c_str(), e.enter ? "entered" : "left",
                          name(e.trigger).c_str());
            physicsLog.push_back(line);
            if (physicsLog.size() > 8) physicsLog.erase(physicsLog.begin());
        }
    }

    // Arrow keys: the camera moves moveSpeed x the model's radius per second (+ / - change it).
    float moveSpeed = 1.0f;
    static constexpr float kMoveStep = 1.5f, kMoveMin = 1.0f / 64.0f, kMoveMax = 64.0f;

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
        physics.clear();   // it points at the old model
        edits.clear();
        selected = -1;
        dragHandle = hoverHandle = -1;
        animGpu.reset();
        animTex.reset();
        crowd.clear();
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
        activeCamera = -1;
        if (!args.camera.empty()) {   // --camera=<index|name>
            const std::vector<gltf::PlacedCamera> cams = pose.cameras();
            for (size_t i = 0; i < cams.size(); i++)
                if (std::to_string(i) == args.camera || model->cameras[(size_t)cams[i].camera].name == args.camera) {
                    selectCamera((int)i);
                    lookYaw = args.lookYaw;
                    lookPitch = args.lookPitch;
                    break;
                }
        }
        const std::string title = "TOMS glTF Viewer - " + file;
        SDL_SetWindowTitle(window, title.c_str());
        frameScene();
        if (crowdCount > 0 || args.crowd > 0) buildCrowd(crowdCount > 0 ? crowdCount : args.crowd);
        physicsOn = args.physics && model->physics.any();
        buildPhysics();
        if (!args.select.empty())
            for (size_t i = 0; i < model->nodes.size(); i++)
                if (model->nodes[i].name == args.select || std::to_string(i) == args.select) { selected = (int)i; revealSelected = true; break; }
        if (args.tool == "rotate") tool = Tool::Rotate;
        else if (args.tool == "scale") tool = Tool::Scale;
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
        modelMin = mn;
        modelMax = mx;
        if (crowdCount > 0 && crowd.size() > 0) {   // the whole grid
            for (int i = 0; i < crowd.size(); i++) {
                const glm::vec3 o(crowd[i].placement[3]);
                mn = glm::min(mn, modelMin + o);
                mx = glm::max(mx, modelMax + o);
            }
        }
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
        for (const auto& [n, e] : edits) pose.setLocal(n, e.t, e.r, e.s);   // node edits replace the animation
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
            if (activeCamera >= 0 && e.button.button == SDL_BUTTON_LEFT) {   // look around, still on the camera
                if (e.button.clicks >= 2) lookYaw = lookPitch = 0.0f;
                dragLook = true;
                break;
            }
            if (e.button.button == SDL_BUTTON_LEFT) {
                const glm::vec2 px(e.button.x * pixelScale(), e.button.y * pixelScale());
                const int h = handleAt(px);
                if (h >= 0) {   // a handle of the selected node: drag it
                    startHandleDrag(h, px);
                    break;
                }
                dragOrbit = true;
                pressPending = true;   // a click (no movement) picks a node
                pressAt = px;
            } else {
                dragPan = true;
            }
            toFreeCamera();
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT && dragHandle >= 0) {
                dragHandle = -1;
                editChanged(selected);
            } else if (e.button.button == SDL_BUTTON_LEFT && pressPending) {
                selected = pickNode(pressAt);
                revealSelected = selected >= 0;
            }
            pressPending = false;
            dragOrbit = dragPan = dragLook = false;
            break;
        case SDL_EVENT_MOUSE_MOTION: {
            const glm::vec2 px(e.motion.x * pixelScale(), e.motion.y * pixelScale());
            if (dragHandle >= 0) {
                dragHandleTo(px);
                posed();
                break;
            }
            if (pressPending && glm::length(px - pressAt) > 4.0f * pixelScale()) pressPending = false;
            if (!dragOrbit && !dragPan && !dragLook && !uiMouse) hoverHandle = handleAt(px);
            if (dragLook) {   // drag right: look right; drag up: look up
                lookYaw -= e.motion.xrel * 0.25f;
                lookPitch = std::clamp(lookPitch - e.motion.yrel * 0.25f, -89.0f, 89.0f);
            } else if (dragOrbit) {
                yaw -= e.motion.xrel * 0.4f;
                pitch = std::clamp(pitch + e.motion.yrel * 0.4f, -89.0f, 89.0f);
            } else if (dragPan) {
                const glm::mat4 v = viewMatrix();
                const glm::vec3 right(v[0][0], v[1][0], v[2][0]), up(v[0][1], v[1][1], v[2][1]);
                const float k = distance * std::tan(glm::radians(fov * 0.5f)) * 2.0f / std::max(1, fbH) * pixelScale();
                target += (-e.motion.xrel * right + e.motion.yrel * up) * k;
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            if (uiMouse) break;
            if (activeCamera >= 0) {   // zoom the camera's lens, still on it
                lookZoom = std::clamp(lookZoom * std::pow(0.9f, e.wheel.y), 0.05f, 2.5f);
                break;
            }
            distance *= std::pow(0.88f, e.wheel.y);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) break;
            switch (e.key.key) {
            case SDLK_ESCAPE: quit = true; break;
            case SDLK_SPACE: paused = !paused; break;
            case SDLK_F:
                activeCamera = -1;
                frameScene();
                break;
            case SDLK_C: cycleCamera(); break;
            case SDLK_G: grid = !grid; break;
            case SDLK_K: skeleton = !skeleton; break;
            case SDLK_B: boundsBox = !boundsBox; break;
            case SDLK_W: wireframe = !wireframe; break;
            case SDLK_R: autoRotate = !autoRotate; break;
            case SDLK_H: showUi = !showUi; break;
            case SDLK_T: tool = Tool::Move; break;
            case SDLK_E: tool = Tool::Rotate; break;
            case SDLK_S: tool = Tool::Scale; break;
            case SDLK_DELETE:
            case SDLK_BACKSPACE:
                if (selected >= 0 && edits.erase(selected)) { posed(); editChanged(selected); }
                break;
            case SDLK_EQUALS:   // the + key (Shift not needed)
            case SDLK_PLUS:
            case SDLK_KP_PLUS: moveSpeed = std::min(moveSpeed * kMoveStep, kMoveMax); break;
            case SDLK_MINUS:
            case SDLK_KP_MINUS: moveSpeed = std::max(moveSpeed / kMoveStep, kMoveMin); break;
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

    // ---- the file's cameras -----------------------------------------------------------------------
    std::string cameraLabel(size_t i, const gltf::PlacedCamera& pc) const {
        const gltf::Camera& c = model->cameras[(size_t)pc.camera];
        return std::to_string(i + 1) + ". " + (c.name.empty() ? "camera " + std::to_string(pc.camera) : c.name) +
               (c.perspective ? "" : " (orthographic)");
    }

    void cycleCamera() {
        const int n = model ? (int)pose.cameras().size() : 0;
        selectCamera(n == 0 ? -1 : (activeCamera + 2) % (n + 1) - 1);   // -1, 0 .. n-1, -1, ...
    }

    // The file camera's transform now, with the look-around on top (in the camera's own frame).
    glm::mat4 lookedCamera(const gltf::PlacedCamera& pc) const {
        const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::radians(lookYaw), glm::vec3(0, 1, 0));
        return pc.world * glm::rotate(turn, glm::radians(lookPitch), glm::vec3(1, 0, 0));
    }

    // Leave a file camera for the free orbit camera, looking from the same place the same way.
    void toFreeCamera() {
        if (activeCamera < 0) return;
        const std::vector<gltf::PlacedCamera> cams = pose.cameras();
        if (activeCamera < (int)cams.size()) {
            const glm::mat4 w = lookedCamera(cams[(size_t)activeCamera]);
            const glm::vec3 eyePos(w[3]), fwd = -glm::normalize(glm::vec3(w[2]));
            const glm::vec3 c = (sceneMin + sceneMax) * 0.5f;
            const float d = std::max(glm::dot(c - eyePos, fwd), radius * 0.25f);   // orbit around what it looks at
            target = eyePos + fwd * d;
            distance = d;
            pitch = glm::degrees(std::asin(std::clamp(-fwd.y, -1.0f, 1.0f)));
            yaw = glm::degrees(std::atan2(-fwd.x, -fwd.z));
        }
        activeCamera = -1;
    }

    // Held arrow keys move the camera (and what it looks at) across the screen.
    void moveWithKeys(float dt) {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) return;
        const bool* k = SDL_GetKeyboardState(nullptr);
        const float x = float(k[SDL_SCANCODE_RIGHT]) - float(k[SDL_SCANCODE_LEFT]);
        const float y = float(k[SDL_SCANCODE_UP]) - float(k[SDL_SCANCODE_DOWN]);
        if (x == 0.0f && y == 0.0f) return;
        toFreeCamera();
        const glm::mat4 v = viewMatrix();
        const glm::vec3 right(v[0][0], v[1][0], v[2][0]), up(v[0][1], v[1][1], v[2][1]);
        target += (right * x + up * y) * (moveSpeed * radius * std::min(dt, 0.1f));
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
        // Physics: a box around every body's collider (grey static, blue kinematic, green dynamic -- dark when asleep --,
        // yellow trigger).
        if (showUi && physicsBoxes && physics.built()) {
            std::vector<gltf::PhysicsDebugBox> boxes;
            physics.debugBoxes(boxes);
            for (const gltf::PhysicsDebugBox& b : boxes) {
                using K = gltf::PhysicsDebugBox::Kind;
                const uint32_t col = b.kind == K::Static ? abgr(0.7f, 0.7f, 0.7f, 0.8f)
                                   : b.kind == K::Kinematic ? abgr(0.35f, 0.6f, 1.0f)
                                   : b.kind == K::Trigger ? abgr(1.0f, 0.85f, 0.2f)
                                   : b.sleeping ? abgr(0.2f, 0.5f, 0.25f) : abgr(0.35f, 1.0f, 0.4f);
                glm::vec3 c[8];
                for (int k = 0; k < 8; k++)
                    c[k] = glm::vec3(b.world * glm::vec4((k & 1 ? 1.0f : -1.0f) * b.halfExtents.x, (k & 2 ? 1.0f : -1.0f) * b.halfExtents.y,
                                                         (k & 4 ? 1.0f : -1.0f) * b.halfExtents.z, 1.0f));
                for (int k = 0; k < 8; k++)
                    for (int bit : {1, 2, 4})
                        if (!(k & bit)) line(c[k], c[k | bit], col);
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
        if (selected >= 0 && model) {   // the selection: its mesh bounds, then the handles over everything
            std::vector<GltfRenderer::LinePoint> over;
            const gltf::Node& node = model->nodes[(size_t)selected];
            if (node.mesh >= 0 && node.skin < 0) {
                glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
                for (const gltf::Primitive& p : model->meshes[(size_t)node.mesh].primitives) {
                    mn = glm::min(mn, p.boundsMin);
                    mx = glm::max(mx, p.boundsMax);
                }
                const glm::mat4& w = pose.world(selected);
                glm::vec3 c[8];
                for (int k = 0; k < 8; k++)
                    c[k] = glm::vec3(w * glm::vec4(k & 1 ? mx.x : mn.x, k & 2 ? mx.y : mn.y, k & 4 ? mx.z : mn.z, 1.0f));
                const uint32_t col = abgr(1.0f, 0.6f, 0.1f, 0.9f);
                for (int k = 0; k < 8; k++)
                    for (int bit : {1, 2, 4})
                        if (!(k & bit)) { over.push_back({c[k], col}); over.push_back({c[k | bit], col}); }
            }
            drawHandles(over);
            if (!over.empty()) renderer.lines(over, false);
        }
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

        {
            const std::vector<gltf::PlacedCamera> cams = pose.cameras();
            if (!cams.empty() && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                const std::string current = activeCamera >= 0 && activeCamera < (int)cams.size()
                                                ? cameraLabel((size_t)activeCamera, cams[(size_t)activeCamera])
                                                : std::string("free (orbit)");
                if (ImGui::BeginCombo("camera", current.c_str())) {
                    if (ImGui::Selectable("free (orbit)", activeCamera < 0)) selectCamera(-1);
                    for (size_t i = 0; i < cams.size(); i++)
                        if (ImGui::Selectable(cameraLabel(i, cams[i]).c_str(), activeCamera == (int)i)) selectCamera((int)i);
                    ImGui::EndCombo();
                }
                ImGui::TextWrapped("%zu in the file. C cycles. On a camera: left drag looks around, the wheel zooms "
                                   "(it keeps riding the animation); right drag or the arrows go free from there.", cams.size());
                if (activeCamera >= 0 && activeCamera < (int)cams.size()) {
                    const gltf::Camera& c = model->cameras[(size_t)cams[(size_t)activeCamera].camera];
                    ImGui::Text("look: %.0f deg right, %.0f deg up, zoom %.2fx", -lookYaw, lookPitch, 1.0f / lookZoom);
                    if (ImGui::SmallButton("look ahead (double-click)")) {
                        lookYaw = lookPitch = 0.0f;
                        lookZoom = 1.0f;
                    }
                    if (c.perspective)
                        ImGui::TextDisabled("perspective, fov %.0f deg, near %g, far %s", glm::degrees(c.yfov), c.znear,
                                            c.zfar > 0 ? std::to_string(c.zfar).c_str() : "infinite");
                    else
                        ImGui::TextDisabled("orthographic, %g x %g, near %g, far %g", c.xmag * 2, c.ymag * 2, c.znear, c.zfar);
                }
            }
        }

        sceneTreePanel();

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
            ImGui::SliderFloat("move speed", &moveSpeed, kMoveMin, kMoveMax, "%.2fx", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Arrow keys move the camera; + / - change this");
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

        if (model->physics.any() && ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("simulate (KHR_physics_rigid_bodies)", &physicsOn)) buildPhysics();
            if (physics.built()) {
                const gltf::PhysicsWorld::Stats st = physics.stats();
                ImGui::Text("%.2f s simulated (%d steps of 1/60 s)", physics.time(), physics.steps());
                ImGui::Text("%d dynamic (%d awake), %d kinematic, %d static", st.dynamic, st.awake, st.kinematic, st.statics);
                ImGui::Text("%d triggers, %d joints", st.triggers, st.joints);
                ImGui::Checkbox("paused", &physicsPaused);
                ImGui::SameLine();
                if (ImGui::Button("step") && physicsPaused) physics.step(gltf::PhysicsWorld::kStep, pose);
                ImGui::SameLine();
                if (ImGui::Button("restart")) buildPhysics();
                glm::vec3 g = physics.gravity();
                if (ImGui::SliderFloat("gravity Y", &g.y, -20.0f, 5.0f, "%.2f m/s2")) physics.setGravity(g);
                ImGui::Checkbox("collider boxes", &physicsBoxes);
                for (const std::string& w : physics.warnings()) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", w.c_str());
                if (!physicsLog.empty()) {
                    ImGui::TextDisabled("triggers:");
                    for (const std::string& l : physicsLog) ImGui::TextUnformatted(l.c_str());
                }
            }
        }

        if (ImGui::CollapsingHeader("Crowd (instanced skinning)")) {
            int n = crowdCount;
            if (ImGui::SliderInt("copies", &n, 0, 400)) buildCrowd(n);
            ImGui::TextDisabled("0 = off: the single model with its Pose");
            if (!crowdError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", crowdError.c_str());
            if (crowdCount > 0 && animTex) {
                ImGui::Text("texture %dx%d RGBA32F, %.1f KB", animTex->width, animTex->rows, animTex->bytes() / 1024.0);
                ImGui::Text("%d joints, %zu clips at %.0f fps, baked in %.1f ms", animTex->joints, animTex->clips.size(), animTex->fps, bakeMs);
                if (animTex->maxScaleError > 0.01f) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "non-uniform joint scale averaged (%.0f %%)",
                                                                        animTex->maxScaleError * 100.0f);
                if (ImGui::Button("cross-fade all to the next clip (0.5 s)"))
                    for (int i = 0; i < crowd.size(); i++) crowd.crossFade(i, (crowd[i].a.clip + 1) % (int)animTex->clips.size(), 0.5f);
                if (animTex->clips.size() > 1 && ImGui::SliderFloat("blend clip 0 / 1", &crowdBlend, 0.0f, 1.0f))
                    for (int i = 0; i < crowd.size(); i++) crowd.setBlend(i, 0, 1, crowdBlend);
                if (ImGui::Button("each its own clip again")) buildCrowd(crowdCount);
            }
        }

        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            const GltfRenderer::Stats& s = renderer.stats();
            ImGui::Text("%.1f fps   %s", fps, next::bgfxHostRendererName().c_str());
            ImGui::Text("%d draw calls, %d instances", s.drawCalls, s.instances);
            ImGui::Text("%d skinned draws, %d morph uploads", s.skinnedDraws, s.morphUploads);
            if (s.crowdInstances > 0) ImGui::Text("%d crowd instances (no CPU posing)", s.crowdInstances);
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
        else if (!paused) {
            time += double(dt) * speed;
            crowd.update(dt * speed);
        }
        if (autoRotate) yaw += dt * 20.0f;
        moveWithKeys(dt);

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
        if (model) {
            posed();
            simulate(dt);
        }
        frame.lights = frameLights();
        // Shadows cover the model and the floor around it (where its shadow falls).
        frame.boundsMin = glm::vec3(sceneMin.x - radius, sceneMin.y, sceneMin.z - radius);
        frame.boundsMax = glm::vec3(sceneMax.x + radius, sceneMax.y, sceneMax.z + radius);
        const float aspect = float(fbW) / float(std::max(fbH, 1));
        const std::vector<gltf::PlacedCamera> cams = model ? pose.cameras() : std::vector<gltf::PlacedCamera>();
        if (activeCamera >= (int)cams.size()) activeCamera = -1;
        if (activeCamera >= 0) {   // through the file's camera (it moves with its node's animation)
            const gltf::PlacedCamera& pc = cams[(size_t)activeCamera];
            const gltf::Camera& c = model->cameras[(size_t)pc.camera];
            const glm::mat4 world = lookedCamera(pc);
            frame.eye = glm::vec3(world[3]);
            frame.view = glm::inverse(world);
            const float zn = std::max(c.znear, 1e-4f);
            const float zf = c.zfar > zn ? c.zfar : zn + radius * 100.0f + glm::length(frame.eye - target);
            if (c.perspective) {
                const float fovY = 2.0f * std::atan(std::tan(c.yfov * 0.5f) * lookZoom);   // the wheel zooms the lens
                frame.proj = GltfRenderer::projection(glm::degrees(fovY), aspect, zn, zf);
            } else {   // the window's aspect, the camera's height
                const float ym = c.ymag * lookZoom, xm = ym * aspect;
                frame.proj = bgfx::getCaps()->homogeneousDepth ? glm::orthoRH_NO(-xm, xm, -ym, ym, zn, zf)
                                                               : glm::orthoRH_ZO(-xm, xm, -ym, ym, zn, zf);
            }
        } else {
            frame.eye = eye();
            frame.view = viewMatrix();
            const float zn = std::max(distance - radius * 4.0f, distance * 0.002f);
            frame.proj = GltfRenderer::projection(fov, aspect, zn, distance + radius * 50.0f);
        }
        frame.srgbOut = false;   // the host's backbuffer is sRGB: the GPU encodes
        if (showUi && fbW > 0) {   // centre the model in the part the panel leaves free
            const float panel = std::min(380.0f * uiScale, float(fbW) * 0.34f) + 16.0f * uiScale;
            frame.proj = glm::translate(glm::mat4(1.0f), glm::vec3(-panel / float(fbW), 0.0f, 0.0f)) * frame.proj;
        }
        renderer.begin(0, frame, (uint32_t)fbW, (uint32_t)fbH, rgbaHex(background));   // views 0 .. kViews - 1
        if (model && gpu && crowdCount > 0 && animGpu && crowd.size() > 0) {
            if (args.time >= 0.0f && frameNo == 0) crowd.update(args.time);   // --time: freeze the crowd there too
            renderer.drawCrowd(*gpu, *animGpu, crowd);
        } else if (model && gpu) {
            renderer.draw(*gpu, pose);
        }
        if (groundOn && model && groundGpu) {   // a floor under the model: receives shadows, casts none
            const glm::vec3 c = (sceneMin + sceneMax) * 0.5f;
            const glm::mat4 place = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(c.x, sceneMin.y - radius * 0.002f, c.z)),
                                               glm::vec3(radius * 6.0f));
            renderer.draw(*groundGpu, groundPose, place, false);
        }
        drawLines();
        bgfx::setDebug(wireframe ? BGFX_DEBUG_WIREFRAME : BGFX_DEBUG_NONE);
        gui.render(GltfRenderer::kViews + 4);

        if (frameNo == 1 && args.pickX >= 0.0f) {   // --pick: as a click there, once the frame's matrices exist
            selected = pickNode(glm::vec2(args.pickX, args.pickY));
            revealSelected = selected >= 0;
            std::fprintf(stderr, "[gltf] pick %.0f,%.0f: node %d (%s)\n", args.pickX, args.pickY, selected,
                         selected >= 0 ? model->nodes[(size_t)selected].name.c_str() : "nothing");
        }
        if (frameNo == 2 && args.drag != glm::vec4(-1.0f) && selected >= 0) {   // --drag: as a press, a move and a release
            glm::vec2 o(0.0f);
            toScreen(glm::vec3(pose.world(selected)[3]), o);
            const glm::vec2 p0 = o + glm::vec2(args.drag.x, args.drag.y), p1 = o + glm::vec2(args.drag.z, args.drag.w);
            const int h = handleAt(p0);
            if (h >= 0) {
                startHandleDrag(h, p0);
                dragHandleTo(p1);
                dragHandle = -1;
                posed();
                editChanged(selected);
            }
            const NodeEdit e = edits.count(selected) ? edits[selected] : NodeEdit();
            std::fprintf(stderr, "[gltf] drag handle %d: t %.3f %.3f %.3f  euler %.1f %.1f %.1f  s %.3f %.3f %.3f\n", h, e.t.x, e.t.y,
                         e.t.z, e.euler.x, e.euler.y, e.euler.z, e.s.x, e.s.y, e.s.z);
        }
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
    v.animGpu.reset();   // the crowd's texture: before bgfx goes
    v.groundGpu.reset();
    v.renderer.shutdown();
    v.gui.shutdown();
    next::bgfxHostShutdown();
    SDL_DestroyWindow(v.window);
    SDL_Quit();
    return 0;
}
