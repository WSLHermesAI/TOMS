// gltf_anim_texture.h -- instanced skinning for crowds (docs/18_GLTF.md "Crowds"): a skin's animations baked
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
    std::string name;
    int animation = -1;      // index into Model::animations
    int firstRow = 0;        // its first frame's row in the texture
    int frames = 1;          // rows; frame f is the pose at min(f / fps, duration)
    float duration = 0.0f;
};

struct AnimTexture {
    int skin = -1;
    int joints = 0;
    float fps = 30.0f;
    int width = 0;           // texels per row: joints * 2
    int rows = 0;            // every clip's frames
    std::vector<float> texels;   // rows * width * 4 floats (RGBA32F)
    std::vector<AnimClip> clips;
    uint64_t sourceHash = 0; // modelHash(model, skin): a saved file must match the model it is used with
    // What the bake could not store exactly: the largest relative non-uniform scale it averaged, and joints
    // with a negative determinant (mirrored) -- 0 / false for ordinary rigs.
    float maxScaleError = 0.0f;
    bool mirroredJoints = false;

    int clipIndex(const std::string& name) const;   // -1 when there is no such clip
    size_t bytes() const { return texels.size() * sizeof(float); }
};

// Bakes every animation of the model for `skin` at `fps`. False (+ error) when the skin does not exist, has
// no joints or more than kMaxJoints.
bool bakeAnimTexture(const Model& model, int skin, float fps, AnimTexture& out, std::string* error = nullptr);

// A hash of what the bake depends on: the skin's joint names, inverse bind matrices, and every animation's
// name, duration and channel count. A saved texture whose hash differs is stale.
uint64_t modelHash(const Model& model, int skin);

// Save / load a baked texture (.animtex: a small header, the clip table, the raw floats). load() checks the
// magic and version; pass the model to also check sourceHash (stale = an error).
bool saveAnimTexture(const AnimTexture& tex, const std::string& path, std::string* error = nullptr);
bool loadAnimTexture(const std::string& path, AnimTexture& out, const Model* checkAgainst = nullptr, int skin = -1,
                     std::string* error = nullptr);

// Which two rows to blend for clip `clip` at time t, and how far between them (0..1). loop: t wraps; else it
// holds the last frame. This is what the vertex shader gets per instance.
struct FramePair {
    float row0 = 0, row1 = 0, blend = 0;
};
FramePair framePair(const AnimTexture& tex, int clip, float t, bool loop);

// The CPU version of the shader's joint lookup (tests, and anything that needs a joint on the CPU): clip A at
// frames `a`, optionally mixed with clip B at frames `b` by weight w (0 = A only).
glm::mat4 sampleJoint(const AnimTexture& tex, int joint, const FramePair& a, const FramePair* b = nullptr, float w = 0.0f);

// ---- the crowd: per-instance animation state ----------------------------------------------------------

struct CrowdTrack {
    int clip = 0;
    float time = 0.0f;
    float speed = 1.0f;
    bool loop = true;
};

struct CrowdInstance {
    glm::mat4 placement{1.0f};   // where the model stands (its root transform)
    CrowdTrack a;                // the clip it plays
    CrowdTrack b;                // the second clip when blending / cross-fading
    float weight = 0.0f;         // of b: 0 = a only, 1 = b only
    float fadeSeconds = 0.0f;    // > 0 while cross-fading from a to b
    float fadeElapsed = 0.0f;
    bool hidden = false;
};

// One instance as the shader reads it (80 bytes, bgfx instance data i_data0..4).
struct CrowdGpuInstance {
    glm::vec4 rows[3];   // the placement's first three rows (its last row is 0, 0, 0, 1)
    glm::vec4 a;         // clip A: row0, row1, blend, unused
    glm::vec4 b;         // clip B: row0, row1, blend, weight of B (0: B is skipped)
};
static_assert(sizeof(CrowdGpuInstance) == 80, "bgfx instance data stride");

class Crowd {
public:
    Crowd() = default;
    explicit Crowd(std::shared_ptr<const AnimTexture> tex) : tex_(std::move(tex)) {}
    void setTexture(std::shared_ptr<const AnimTexture> tex) { tex_ = std::move(tex); }
    const std::shared_ptr<const AnimTexture>& texture() const { return tex_; }

    int add(const glm::mat4& placement, int clip = 0, float time = 0.0f, bool loop = true);   // its index
    void remove(int i);                  // later instances move down by one
    void clear() { instances_.clear(); }
    int size() const { return (int)instances_.size(); }
    CrowdInstance& operator[](int i) { return instances_[(size_t)i]; }
    const CrowdInstance& operator[](int i) const { return instances_[(size_t)i]; }

    // Plays `clip` from `time` at once (any blend or fade stops).
    void play(int i, int clip, bool loop = true, float time = 0.0f);
    // Fades from what it plays now to `clip` (from its start) over `seconds`; when done it plays `clip` alone.
    // A fade started during a fade starts from the clip with the larger weight.
    void crossFade(int i, int clip, float seconds, bool loop = true);
    // Holds a mix: clipA and clipB both play, B at `weight` (no fade). Times keep running.
    void setBlend(int i, int clipA, int clipB, float weight);

    void update(float dt);               // advances every instance's clip times and fades
    void gpuInstances(std::vector<CrowdGpuInstance>& out) const;   // the visible instances, for the draw

private:
    std::shared_ptr<const AnimTexture> tex_;
    std::vector<CrowdInstance> instances_;
};

}  // namespace toms::gltf
