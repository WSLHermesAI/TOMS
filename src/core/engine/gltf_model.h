// gltf_model.h -- glTF 2.0 models (.gltf / .glb) for the engine: loading (cgltf) into plain CPU
// data, and posing (node transforms, animations, skins, morph weights). No GPU code here: the bgfx
// side is src/engine/src/gltf_renderer.* (it uploads a Model and draws a Pose). docs/18_GLTF.md.
//
// Supported: meshes (triangles, lines, points), every accessor type (normalized / quantized ints
// read as floats, sparse accessors), PBR metallic-roughness materials (base colour, metal/rough,
// normal, occlusion, emissive textures; alpha opaque / mask / blend; double-sided), vertex colours,
// two UV sets, skins (joints + inverse bind matrices, up to kMaxJoints per skin), morph targets
// (position / normal / tangent), node animations (translation, rotation, scale, weights; linear,
// step, cubic spline), embedded (.glb, data URIs) and external files.
// Extensions: KHR_materials_unlit, KHR_texture_transform, KHR_materials_emissive_strength,
// KHR_mesh_quantization, EXT_mesh_gpu_instancing (per-instance translation / rotation / scale),
// KHR_materials_transmission + KHR_materials_volume (approximated: see Material::transmission),
// KHR_lights_punctual (directional / point / spot lights placed by nodes: Pose::lights()).
// Other extensions are listed in Model::ignoredExtensions (the model still loads); a file that
// REQUIRES one we cannot read (Draco, meshopt, KTX2 / basisu) is refused with a clear error.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace toms::gltf {

constexpr int kMaxJoints = 128;   // per skin: the vertex shader's joint matrix array

enum class AlphaMode { Opaque, Mask, Blend };

struct TextureRef {
    int texture = -1;            // index into Model::textures; -1 = none
    int uvSet = 0;               // TEXCOORD_n
    // KHR_texture_transform: uv' = offset + rotate(rotation) * (scale * uv)
    glm::vec2 offset{0.0f}, scale{1.0f};
    float rotation = 0.0f;
    bool valid() const { return texture >= 0; }
};

struct Material {
    std::string name;
    glm::vec4 baseColor{1.0f};
    float metallic = 1.0f, roughness = 1.0f;
    glm::vec3 emissive{0.0f};            // already multiplied by KHR_materials_emissive_strength
    float normalScale = 1.0f, occlusionStrength = 1.0f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool unlit = false;                  // KHR_materials_unlit
    // KHR_materials_transmission (+ KHR_materials_volume's attenuation colour): glass. Drawn as an
    // approximation -- see through it (blended), keep its reflections; no refraction of the scene.
    float transmission = 0.0f;
    glm::vec3 attenuationColor{1.0f};
    TextureRef baseColorTex, metalRoughTex, normalTex, occlusionTex, emissiveTex;
};

struct Image {
    std::string name;
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;           // decoded, 4 bytes per pixel; empty = could not decode
};

struct Texture {
    int image = -1;
    bool srgb = false;                   // used as base colour / emissive (colour data)
    bool repeatS = true, repeatT = true, mirrorS = false, mirrorT = false;
    bool nearest = false;                // magFilter NEAREST
};

struct MorphTarget {
    std::vector<glm::vec3> position, normal, tangent;   // deltas; empty = this target does not move it
};

enum class Topology { Triangles, Lines, Points };

struct Primitive {
    Topology topology = Topology::Triangles;
    int material = -1;                   // -1 = the default material (white, rough)
    std::vector<glm::vec3> position, normal;
    std::vector<glm::vec4> tangent;      // xyz + handedness; empty = none (the shader derives one)
    std::vector<glm::vec2> uv0, uv1;
    std::vector<glm::vec4> color;        // COLOR_0 (rgb -> a = 1); empty = white
    std::vector<glm::vec4> joints, weights;   // JOINTS_0 / WEIGHTS_0 (joints as floats)
    std::vector<uint32_t> indices;       // always present (generated when the file has none)
    std::vector<MorphTarget> targets;
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};   // of `position`
    bool skinned() const { return !joints.empty() && !weights.empty(); }
};

struct Mesh {
    std::string name;
    std::vector<Primitive> primitives;
    std::vector<float> weights;          // default morph weights
    std::vector<std::string> targetNames;
};

struct Node {
    std::string name;
    int parent = -1;
    std::vector<int> children;
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    int mesh = -1, skin = -1;
    int light = -1;                      // KHR_lights_punctual
    std::vector<float> weights;          // morph weights on the node (else the mesh's)
    std::vector<glm::mat4> instances;    // EXT_mesh_gpu_instancing: local matrices, empty = one draw
};

// KHR_lights_punctual. A node places it: the light shines down the node's -Z axis.
struct Light {
    enum class Type { Directional, Point, Spot };
    std::string name;
    Type type = Type::Point;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;              // lux (directional) / candela (point, spot), as in the file
    float range = 0.0f;                  // 0 = infinite
    float innerCone = 0.0f, outerCone = 0.7853982f;   // spot, radians from the axis
};

// A light where a node puts it in the world (Pose::lights()).
struct PlacedLight {
    int light = -1;                      // index into Model::lights
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};   // where it shines (normalized)
};

struct Skin {
    std::string name;
    std::vector<int> joints;             // node indices
    std::vector<glm::mat4> inverseBind;  // one per joint (identity when the file has none)
};

enum class Path { Translation, Rotation, Scale, Weights };
enum class Interpolation { Linear, Step, CubicSpline };

struct Sampler {
    std::vector<float> input;            // key times, seconds
    std::vector<float> output;           // values: keys x components (x3 for cubic spline: in, value, out)
    int components = 0;                  // 3, 4, or the number of morph targets
    Interpolation interpolation = Interpolation::Linear;
};

struct Channel {
    int sampler = 0;
    int node = -1;
    Path path = Path::Translation;
};

struct Animation {
    std::string name;
    std::vector<Sampler> samplers;
    std::vector<Channel> channels;
    float duration = 0.0f;               // the last key time over every sampler
};

struct Scene {
    std::string name;
    std::vector<int> roots;
};

struct Model {
    std::string path;
    std::string generator;
    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Node> nodes;
    std::vector<Skin> skins;
    std::vector<Light> lights;           // KHR_lights_punctual
    std::vector<Animation> animations;
    std::vector<Scene> scenes;
    int scene = 0;                       // the default scene
    std::vector<std::string> extensionsUsed;
    std::vector<std::string> ignoredExtensions;   // used by the file, not rendered (the model still loads)
    std::vector<std::string> warnings;            // e.g. a skin with more than kMaxJoints joints, an image that did not decode

    // Counts for a status line.
    size_t vertexCount() const;
    size_t triangleCount() const;
    size_t morphTargetCount() const;     // over every mesh
    size_t instanceCount() const;        // EXT_mesh_gpu_instancing instances over every node
};

// Loads a .gltf (with its .bin / images next to it, or data URIs) or a .glb. False + error on failure.
bool loadModel(const std::string& path, Model& out, std::string* error = nullptr);
// The same from memory (a .glb or a .gltf whose resources are embedded); baseDir resolves files.
bool loadModelFromMemory(const void* data, size_t size, const std::string& baseDir, Model& out, std::string* error = nullptr);

// One posed copy of a model: per-node transforms (from the file, then animated), world matrices,
// morph weights. Several Poses can share one Model (a crowd of the same character).
class Pose {
public:
    void bind(const Model* model);       // the rest pose
    const Model* model() const { return model_; }

    void reset();                        // back to the rest pose
    // Applies animation `index` at time t (seconds, clamped to 0..duration -- wrap it for a loop).
    void apply(int index, float t);
    // Blends animation `index` at time t over the current values with weight w (0..1).
    void blend(int index, float t, float w);
    void updateWorld();                  // world matrices from the local transforms (call after apply)

    const glm::mat4& world(int node) const { return world_[(size_t)node]; }
    const std::vector<glm::mat4>& worlds() const { return world_; }
    // Joint matrices of a skin: world(joint) * inverseBind (vertices go straight to model space, so a
    // skinned primitive is drawn with an identity model matrix, as the glTF spec asks).
    void jointMatrices(int skin, std::vector<glm::mat4>& out) const;
    // Morph weights of a node's mesh (the node's own, else the mesh's defaults; animated).
    const std::vector<float>& weights(int node) const { return weights_[(size_t)node]; }
    void setWeight(int node, size_t target, float w) {   // after apply(): a manual morph weight
        if (node >= 0 && node < (int)weights_.size() && target < weights_[(size_t)node].size()) weights_[(size_t)node][target] = w;
    }

    glm::vec3 translation(int n) const { return t_[(size_t)n]; }
    glm::quat rotation(int n) const { return r_[(size_t)n]; }
    glm::vec3 scale(int n) const { return s_[(size_t)n]; }

    // The file's lights (KHR_lights_punctual) where their nodes are now (call after updateWorld()).
    std::vector<PlacedLight> lights() const;

    // Bounds of the default scene in world space at this pose (skinned meshes skinned on the CPU,
    // instances included). False when there is nothing to bound.
    bool bounds(glm::vec3& mn, glm::vec3& mx) const;

private:
    void sample(int index, float t, float w);

    const Model* model_ = nullptr;
    std::vector<glm::vec3> t_, s_;
    std::vector<glm::quat> r_;
    std::vector<std::vector<float>> weights_;
    std::vector<glm::mat4> world_;
};

// The local matrix T * R * S.
glm::mat4 composeTRS(const glm::vec3& t, const glm::quat& r, const glm::vec3& s);

// Morphs a primitive's base attributes by `weights` (CPU morphing; the renderer uploads the result).
void morph(const Primitive& p, const std::vector<float>& weights, std::vector<glm::vec3>& position,
           std::vector<glm::vec3>& normal, std::vector<glm::vec4>& tangent);

}  // namespace toms::gltf
