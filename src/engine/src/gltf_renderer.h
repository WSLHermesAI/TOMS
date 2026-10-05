// gltf_renderer.h -- draws glTF models (gltf_model.h) with bgfx: PBR metallic-roughness, GPU
// skinning, morph targets (morphed on the CPU, uploaded when the weights change), instancing
// (EXT_mesh_gpu_instancing: one instanced draw per primitive), alpha mask / blend (blended draws go
// to a second, back-to-front view), up to 4 directional / point / spot lights with shadow maps.
// docs/18_GLTF.md.
//
// Usage, once per frame:
//   GltfRenderer::Frame f;  f.view = ...; f.proj = projection(...); f.eye = ...; f.lights = {...};
//   f.boundsMin / boundsMax = what the shadows must cover (the scene, its floor)
//   renderer.begin(viewId, f, width, height);    // views viewId .. viewId + kViews - 1
//   renderer.draw(*gpu, pose, placement);        // as many models as you like (they cast and receive)
//   renderer.lines(points);                      // optional debug lines (grid, skeleton)
//
// Shadows: one depth texture (the atlas, Frame::shadowMapSize square) holds a tile per shadow:
// a directional light 1 (orthographic, fitted to the bounds), a spot light 1 (its cone), a point
// light 6 (one 90-degree face each). At most kMaxShadowTiles tiles; the tiles shrink as more lights
// cast. Sampled with 3x3 hardware PCF, with a normal offset and a small depth bias against acne.
#pragma once
#include "gltf_model.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace toms::next {

class GltfRenderer {
public:
    static constexpr int kMaxLights = 4;
    static constexpr int kMaxShadowTiles = 24;
    static constexpr uint16_t kViews = kMaxShadowTiles + 2;   // shadow tiles, opaque, blended

    struct Light {
        enum class Type { Directional, Point, Spot };
        Type type = Type::Directional;
        glm::vec3 color{1.0f};
        float intensity = 3.0f;              // directional: as is; point / spot: / distance^2
        glm::vec3 position{0.0f};            // point, spot
        glm::vec3 direction{0.0f, -1.0f, 0.0f};   // where it shines (directional, spot)
        float range = 0.0f;                  // point, spot: 0 = infinite
        float innerCone = 0.35f, outerCone = 0.6f;   // spot: radians from the axis
        bool castShadows = true;
    };

    // The lighting and output of a frame. Defaults: a key light with shadows and a soft fill.
    struct Frame {
        glm::mat4 view{1.0f}, proj{1.0f};   // proj from projection() (backend-correct depth range)
        glm::vec3 eye{0.0f, 0.0f, 5.0f};
        std::vector<Light> lights = defaultLights();   // up to kMaxLights
        float ambient = 0.7f;
        glm::vec3 sky{0.62f, 0.66f, 0.72f}, ground{0.22f, 0.2f, 0.18f};
        float exposure = 1.0f;
        bool tonemap = true;                 // ACES
        bool srgbOut = true;                 // the backbuffer is not sRGB: encode in the shader
        int debug = 0;                       // 0 lit, 1 normals, 2 base colour, 3 metal/rough, 4 uv
        // Shadows.
        bool shadows = true;
        int shadowMapSize = 4096;            // the atlas (square)
        glm::vec3 boundsMin{-1.0f}, boundsMax{1.0f};   // what directional shadows cover; infinite ranges end here
        float shadowBias = 1.0f;             // x the built-in depth bias / normal offset
    };
    static std::vector<Light> defaultLights();

    struct LinePoint {
        glm::vec3 pos;
        uint32_t abgr;
    };
    struct Stats {
        int drawCalls = 0, instances = 0, skinnedDraws = 0, morphUploads = 0, shadowTiles = 0, shadowDraws = 0;
    };

    // Per-model GPU resources (buffers, textures). Made by upload(); free with the renderer alive.
    struct GpuModel;

    GltfRenderer();
    ~GltfRenderer();
    bool init();                             // programs, uniforms, default textures (bgfx must be up)
    void shutdown();
    bool ready() const;
    bool shadowsSupported() const;           // depth textures with compare (every desktop backend)

    std::shared_ptr<GpuModel> upload(const gltf::Model& model);

    // Perspective matrix for this backend (depth 0..1 on D3D/Vulkan, -1..1 on OpenGL).
    static glm::mat4 projection(float fovYDegrees, float aspect, float zNear, float zFar);

    void begin(uint16_t viewId, const Frame& frame, uint32_t width, uint32_t height, uint32_t clearRgba = 0x3a3d42ff);
    // castShadows = false: drawn and shadowed, but not drawn into the shadow maps (e.g. a floor).
    void draw(GpuModel& gpu, const gltf::Pose& pose, const glm::mat4& placement = glm::mat4(1.0f), bool castShadows = true);
    void lines(const std::vector<LinePoint>& points, bool depthTest = true);   // pairs of points
    const Stats& stats() const { return stats_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Stats stats_;
};

}  // namespace toms::next
