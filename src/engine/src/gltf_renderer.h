// gltf_renderer.h -- draws glTF models (gltf_model.h) with bgfx: PBR metallic-roughness, GPU
// skinning, morph targets (morphed on the CPU, uploaded when the weights change), instancing
// (EXT_mesh_gpu_instancing: one instanced draw per primitive), alpha mask / blend (blended draws go
// to a second, back-to-front view), up to 4 directional / point / spot lights with shadow maps.
// docs/engine/08_GLTF.md.
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
//
// Image-based lighting (gltf_environment.h): setEnvironment() uploads an HDR picture prefiltered on the CPU (a cubemap
// blurred per roughness, 9 harmonics of its diffuse light); while Frame::environment it replaces the plain sky / ground
// as what the models reflect and are lit by, and Frame::background draws it behind them.
//
// Crowds (gltf_anim_texture.h): a skin's animations baked into one texture (uploadAnimTexture, shared by every
// crowd of that model), and drawCrowd() draws every instance in one instanced draw per primitive -- each with
// its own clip, time, and a second clip to blend or cross-fade to. No posing on the CPU per instance.
#pragma once
#include "gltf_anim_texture.h"
#include "gltf_environment.h"
#include "gltf_model.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bgfx { struct ProgramHandle; }

namespace toms::next {

class GltfRenderer {
public:
    static constexpr int m_iMaxLights = 4;
    static constexpr int m_iMaxShadowTiles = 24;
    static constexpr uint16_t m_u16Views = m_iMaxShadowTiles + 2;   // shadow tiles, opaque, blended

    struct Light {
        enum class Type { Directional, Point, Spot };
        Type m_eType = Type::Directional;
        glm::vec3 m_v3Color{1.0f};
        float m_fIntensity = 3.0f;              // directional: as is; point / spot: / distance^2
        glm::vec3 m_v3Position{0.0f};            // point, spot
        glm::vec3 m_v3Direction{0.0f, -1.0f, 0.0f};   // where it shines (directional, spot)
        float m_fRange = 0.0f;                  // point, spot: 0 = infinite
        float m_fInnerCone = 0.35f, m_fOuterCone = 0.6f;   // spot: radians from the axis
        bool m_bCastShadows = true;
    };

    // The lighting and output of a frame. Defaults: a key light with shadows and a soft fill.
    struct Frame {
        glm::mat4 m_mat4View{1.0f}, m_mat4Proj{1.0f};   // proj from projection() (backend-correct depth range)
        glm::vec3 m_v3Eye{0.0f, 0.0f, 5.0f};
        std::vector<Light> m_vLights = f_DefaultLights();   // up to kMaxLights
        float m_fAmbient = 0.7f;
        glm::vec3 m_v3Sky{0.62f, 0.66f, 0.72f}, m_v3Ground{0.22f, 0.2f, 0.18f};
        float m_fExposure = 1.0f;
        bool m_bTonemap = true;                 // ACES
        bool m_bSrgbOut = true;                 // the backbuffer is not sRGB: encode in the shader
        int m_iDebug = 0;                       // 0 lit, 1 normals, 2 base colour, 3 metal/rough, 4 uv
        // Shadows.
        bool m_bShadows = true;
        int m_iShadowMapSize = 4096;            // the atlas (square)
        glm::vec3 m_v3BoundsMin{-1.0f}, m_v3BoundsMax{1.0f};   // what directional shadows cover; infinite ranges end here
        float m_fShadowBias = 1.0f;             // x the built-in depth bias / normal offset
        // Image-based lighting (setEnvironment): used while one is set.
        bool m_bEnvironment = true;
        float m_fEnvironmentIntensity = 1.0f;
        float m_fEnvironmentYaw = 0.0f;         // radians: turns the picture about +Y
        bool m_bBackground = false;             // the picture behind the models (else the clear colour)
        float m_fBackgroundBlur = 0.0f;         // 0 sharp .. 1 as blurred as roughness 1
    };
    static std::vector<Light> f_DefaultLights();

    struct LinePoint {
        glm::vec3 m_v3Pos;
        uint32_t m_uAbgr;
    };
    struct Stats {
        int m_iDrawCalls = 0, m_iInstances = 0, m_iSkinnedDraws = 0, m_iMorphUploads = 0, m_iShadowTiles = 0, m_iShadowDraws = 0;
        int m_iCrowdInstances = 0;              // drawn through drawCrowd (also counted in instances)
    };

    // Per-model GPU resources (buffers, textures). Made by upload(); free with the renderer alive.
    struct GpuModel;
    // A baked animation texture on the GPU (RGBA32F). Made by uploadAnimTexture(); share it between every crowd
    // of the same model; free with the renderer alive.
    struct AnimTextureGpu;

    GltfRenderer();
    ~GltfRenderer();
    bool f_Init();                             // programs, uniforms, default textures (bgfx must be up)
    void f_Shutdown();
    bool f_Ready() const;
    bool f_ShadowsSupported() const;           // depth textures with compare (every desktop backend)

    std::shared_ptr<GpuModel> f_Upload(const gltf::Model& l_oModel);
    // Null (and a log line) when this backend cannot sample RGBA32F textures or the texture is too big.
    std::shared_ptr<AnimTextureGpu> f_UploadAnimTexture(std::shared_ptr<const gltf::AnimTexture> l_spAnim);
    bool f_CrowdsSupported() const;           // the crowd program exists and RGBA32F textures can be sampled
    // The environment for image-based lighting (an RGBA16F cubemap and the diffuse terms). False (and why) when the
    // backend cannot sample RGBA16F cubemaps; the plain sky / ground stays then.
    bool f_SetEnvironment(const gltf::Environment& l_oEnv, std::string* l_pError = nullptr);
    void f_ClearEnvironment();
    bool f_HasEnvironment() const;

    // A render target's row 0 is its bottom on this backend (OpenGL, WebGL): a picture drawn into one is read upside down.
    static bool f_OriginBottomLeft();
    // Perspective matrix for this backend (depth 0..1 on D3D/Vulkan, -1..1 on OpenGL).
    static glm::mat4 f_Projection(float l_fFovYDegrees, float l_fAspect, float l_fZNear, float l_fZFar);

    void begin(uint16_t l_u16ViewId, const Frame& l_oFrame, uint32_t l_uWidth, uint32_t l_uHeight, uint32_t l_uClearRgba = 0x3a3d42ff);
    // castShadows = false: drawn and shadowed, but not drawn into the shadow maps (e.g. a floor).
    void f_Draw(GpuModel& l_oGpu, const gltf::Pose& l_oPose, const glm::mat4& l_mat4Placement = glm::mat4(1.0f), bool l_bCastShadows = true);
    // Every instance of the model's meshes skinned by the texture's skin, animated from the texture: one
    // instanced draw per primitive (plus its shadow casts). Primitives of other skins / unskinned ones are not
    // drawn (draw those with a Pose). The crowd's texture must be the one `anim` was uploaded from.
    void f_DrawCrowd(GpuModel& l_oGpu, const AnimTextureGpu& l_oAnim, const gltf::Crowd& l_oCrowd, bool l_bCastShadows = true);
    void f_DrawCrowd(GpuModel& l_oGpu, const AnimTextureGpu& l_oAnim, const std::vector<gltf::CrowdGpuInstance>& l_vInstances,
                   bool l_bCastShadows = true);
    void f_Lines(const std::vector<LinePoint>& l_vPoints, bool l_bDepthTest = true);   // pairs of points

    // 2D sprites in the scene (docs/gameplay/21 §4.3: a fighter's sprites, placed in the world by BgfxRenderer's world
    // groups): triangles already in world metres, drawn with the models' shader -- double sided, alpha blended, depth
    // tested, sorted with the other blended draws by `sortDistance` -- lit by this frame's lights (lit), or as they are
    // (unlit; additive: light added). They cast no shadow.
    struct SpriteVertex { glm::vec3 m_v3Pos; glm::vec2 m_v2Uv; glm::vec4 m_v4Color; };
    struct Sprites {
        const SpriteVertex* m_pVerts = nullptr;
        uint32_t m_uVertexCount = 0;
        const uint16_t* m_pIdx = nullptr;
        uint32_t m_uIndexCount = 0;
        uint16_t m_u16Texture = UINT16_MAX;            // a bgfx texture handle index; UINT16_MAX: none (the colour alone)
        bool m_bLinear = false;                        // sampled linear, else point (pixel art)
        bool m_bLit = true, m_bAdditive = false;
        glm::vec3 m_v3Normal{0.0f, 0.0f, 1.0f};        // the sprites' plane, facing the camera
        glm::vec3 m_v3Emissive{0.0f};                  // lit: its own light (x its texture), so it reads in the dark
        float m_fSortDistance = 0.0f;                  // metres from the eye: farther drawn first
    };
    void f_DrawSprites(const Sprites& l_oS);
    const Stats& f_Stats() const { return m_oStats; }

private:
    void f_SubmitPrimitive(GpuModel& l_oGpu, const gltf::Primitive& l_oP, const gltf::Material& l_oMat, const std::function<void()>& l_fnGeometry,
                         bgfx::ProgramHandle l_oColourProg, bgfx::ProgramHandle l_oShadowProg, const glm::mat4& l_mat4SortBase, bool l_bMirrored,
                         bool l_bCastShadows, bool l_bSkinned, float l_fSortDistance = -1.0f);
    struct Impl;
    std::unique_ptr<Impl> m_upImpl;
    Stats m_oStats;
};

}  // namespace toms::next
