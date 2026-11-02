// gltf_anim_texture.h -- instanced skinning for crowds (docs/engine/08_GLTF.md "Crowds"): a skin's animations baked
// into one float texture, shared by every instance of the model, and the per-instance animation state
// (play / cross-fade / blend two clips). No GPU code here: GltfRenderer uploads an AnimTexture and draws a
// Crowd in one instanced draw per primitive.
//
// The texture: RGBA32F, 2 texels per joint per frame, one row per frame, every clip's frames stacked:
//   texel 2j   = the joint's rotation (quaternion x, y, z, w)
//   texel 2j+1 = its translation (x, y, z) and uniform scale (w)
// of the joint matrix world(joint) x inverseBind, i.e. exactly what Pose::jointMatrices gives. Rotation +
// translation (instead of a matrix) blends correctly between two clips: the quaternions are nlerped, so a
// limb never shrinks the way a blended matrix does. Limitation: non-uniform joint scale is averaged (the bake
// reports it); a mirrored joint (negative scale) cannot be stored.
//
// Frames: frame f of a clip is the pose at min(f / fps, duration); the last frame is the end pose, so
// frames = ceil(duration * fps) + 1. Between two frames the shader blends linearly (nlerp).
#pragma once
#include "gltf_model.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace toms::gltf {

struct AnimClip {
    std::string m_strName;
    int m_iAnimation = -1;      // index into Model::animations
    int m_iFirstRow = 0;        // its first frame's row in the texture
    int m_iFrames = 1;          // rows; frame f is the pose at min(f / fps, duration)
    float m_fDuration = 0.0f;
};

struct AnimTexture {
    int m_iSkin = -1;
    int m_iJoints = 0;
    float m_fFps = 30.0f;
    int m_iWidth = 0;           // texels per row: joints * 2
    int m_iRows = 0;            // every clip's frames
    std::vector<float> m_vTexels;   // rows * width * 4 floats (RGBA32F)
    std::vector<AnimClip> m_vClips;
    uint64_t m_u64SourceHash = 0; // modelHash(model, skin): a saved file must match the model it is used with
    // What the bake could not store exactly: the largest relative non-uniform scale it averaged, and joints
    // with a negative determinant (mirrored) -- 0 / false for ordinary rigs.
    float m_fMaxScaleError = 0.0f;
    bool m_bMirroredJoints = false;

    int f_ClipIndex(const std::string& l_strName) const;   // -1 when there is no such clip
    size_t f_Bytes() const { return m_vTexels.size() * sizeof(float); }
};

// Bakes every animation of the model for `skin` at `fps`. False (+ error) when the skin does not exist, has
// no joints or more than kMaxJoints.
bool f_BakeAnimTexture(const Model& l_oModel, int l_iSkin, float l_fFps, AnimTexture& l_oOut, std::string* l_pError = nullptr);

// A hash of what the bake depends on: the skin's joint names, inverse bind matrices, and every animation's
// name, duration and channel count. A saved texture whose hash differs is stale.
uint64_t f_ModelHash(const Model& l_oModel, int l_iSkin);

// Save / load a baked texture (.animtex: a small header, the clip table, the raw floats). load() checks the
// magic and version; pass the model to also check sourceHash (stale = an error).
bool f_SaveAnimTexture(const AnimTexture& l_oTex, const std::string& l_strPath, std::string* l_pError = nullptr);
bool f_LoadAnimTexture(const std::string& l_strPath, AnimTexture& l_oOut, const Model* l_pCheckAgainst = nullptr, int l_iSkin = -1,
                     std::string* l_pError = nullptr);

// Which two rows to blend for clip `clip` at time t, and how far between them (0..1). loop: t wraps; else it
// holds the last frame. This is what the vertex shader gets per instance.
struct FramePair {
    float m_fRow0 = 0, m_fRow1 = 0, m_fBlend = 0;
};
FramePair f_FramePair(const AnimTexture& l_oTex, int l_iClip, float l_fT, bool l_bLoop);

// The CPU version of the shader's joint lookup (tests, and anything that needs a joint on the CPU): clip A at
// frames `a`, optionally mixed with clip B at frames `b` by weight w (0 = A only).
glm::mat4 f_SampleJoint(const AnimTexture& l_oTex, int l_iJoint, const FramePair& l_oA, const FramePair* l_pB = nullptr, float l_fW = 0.0f);

// ---- the crowd: per-instance animation state ----------------------------------------------------------

struct CrowdTrack {
    int m_iClip = 0;
    float m_fTime = 0.0f;
    float m_fSpeed = 1.0f;
    bool m_bLoop = true;
};

struct CrowdInstance {
    glm::mat4 m_mat4Placement{1.0f};   // where the model stands (its root transform)
    CrowdTrack m_oA;                // the clip it plays
    CrowdTrack m_oB;                // the second clip when blending / cross-fading
    float m_fWeight = 0.0f;         // of b: 0 = a only, 1 = b only
    float m_fFadeSeconds = 0.0f;    // > 0 while cross-fading from a to b
    float m_fFadeElapsed = 0.0f;
    bool m_bHidden = false;
};

// One instance as the shader reads it (80 bytes, bgfx instance data i_data0..4).
struct CrowdGpuInstance {
    glm::vec4 m_arrRows[3];   // the placement's first three rows (its last row is 0, 0, 0, 1)
    glm::vec4 m_v4A;         // clip A: row0, row1, blend, unused
    glm::vec4 m_v4B;         // clip B: row0, row1, blend, weight of B (0: B is skipped)
};
static_assert(sizeof(CrowdGpuInstance) == 80, "bgfx instance data stride");

class Crowd {
public:
    Crowd() = default;
    explicit Crowd(std::shared_ptr<const AnimTexture> l_spTex) : m_spTex(std::move(l_spTex)) {}
    void f_SetTexture(std::shared_ptr<const AnimTexture> l_spTex) { m_spTex = std::move(l_spTex); }
    const std::shared_ptr<const AnimTexture>& f_Texture() const { return m_spTex; }

    int f_Add(const glm::mat4& l_mat4Placement, int l_iClip = 0, float l_fTime = 0.0f, bool l_bLoop = true);   // its index
    void f_Remove(int l_iI);                  // later instances move down by one
    void f_Clear() { m_vInstances.clear(); }
    int size() const { return (int)m_vInstances.size(); }
    CrowdInstance& operator[](int l_iI) { return m_vInstances[(size_t)l_iI]; }
    const CrowdInstance& operator[](int l_iI) const { return m_vInstances[(size_t)l_iI]; }

    // Plays `clip` from `time` at once (any blend or fade stops).
    void f_Play(int l_iI, int l_iClip, bool l_bLoop = true, float l_fTime = 0.0f);
    // Fades from what it plays now to `clip` (from its start) over `seconds`; when done it plays `clip` alone.
    // A fade started during a fade starts from the clip with the larger weight.
    void f_CrossFade(int l_iI, int l_iClip, float l_fSeconds, bool l_bLoop = true);
    // Holds a mix: clipA and clipB both play, B at `weight` (no fade). Times keep running.
    void f_SetBlend(int l_iI, int l_iClipA, int l_iClipB, float l_fWeight);

    void f_Update(float l_fDt);               // advances every instance's clip times and fades
    void f_GpuInstances(std::vector<CrowdGpuInstance>& l_vOut) const;   // the visible instances, for the draw

private:
    std::shared_ptr<const AnimTexture> m_spTex;
    std::vector<CrowdInstance> m_vInstances;
};

}  // namespace toms::gltf
