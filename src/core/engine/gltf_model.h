// gltf_model.h -- glTF 2.0 models (.gltf / .glb) for the engine: loading (cgltf) into plain CPU
// data, and posing (node transforms, animations, skins, morph weights). No GPU code here: the bgfx
// side is src/engine/src/gltf_renderer.* (it uploads a Model and draws a Pose). docs/engine/08_GLTF.md.
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
// KHR_lights_punctual (directional / point / spot lights placed by nodes: Pose::lights()),
// KHR_physics_rigid_bodies + KHR_implicit_shapes (read into Model::physics; gltf_physics.h simulates them).
// Other extensions are listed in Model::ignoredExtensions (the model still loads); a file that
// REQUIRES one we cannot read (Draco, meshopt, KTX2 / basisu) is refused with a clear error.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace toms::gltf {

// Per skin: the vertex shader's joint matrix array (mesh_common.sh). The web and Android draw on OpenGL ES, which has room for 48.
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
constexpr int g_iMaxJoints = 48;
#else
constexpr int g_iMaxJoints = 128;
#endif

enum class AlphaMode { Opaque, Mask, Blend };

struct TextureRef {
    int m_iTexture = -1;            // index into Model::textures; -1 = none
    int m_iUvSet = 0;               // TEXCOORD_n
    // KHR_texture_transform: uv' = offset + rotate(rotation) * (scale * uv)
    glm::vec2 m_v2Offset{0.0f}, m_v2Scale{1.0f};
    float m_fRotation = 0.0f;
    bool f_Valid() const { return m_iTexture >= 0; }
};

struct Material {
    std::string m_strName;
    glm::vec4 m_v4BaseColor{1.0f};
    float m_fMetallic = 1.0f, m_fRoughness = 1.0f;
    glm::vec3 m_v3Emissive{0.0f};            // already multiplied by KHR_materials_emissive_strength
    float m_fNormalScale = 1.0f, m_fOcclusionStrength = 1.0f;
    AlphaMode m_eAlphaMode = AlphaMode::Opaque;
    float m_fAlphaCutoff = 0.5f;
    bool m_bDoubleSided = false;
    bool m_bUnlit = false;                  // KHR_materials_unlit
    bool m_bAdditive = false;               // not from a file: set by code (light added: a 2D glow in the scene); blended SRC_ALPHA + ONE
    // KHR_materials_transmission (+ KHR_materials_volume's attenuation colour): glass. Drawn as an
    // approximation -- see through it (blended), keep its reflections; no refraction of the scene.
    float m_fTransmission = 0.0f;
    glm::vec3 m_v3AttenuationColor{1.0f};
    // KHR_materials_specular: a dielectric's reflection -- specular x texture alpha scales both F0 and F90, the colour
    // (x texture rgb, sRGB) only F0. KHR_materials_ior sets F0's base, ((ior - 1) / (ior + 1))^2 (0.04 at 1.5).
    float m_fSpecular = 1.0f;
    glm::vec3 m_v3SpecularColor{1.0f};
    float m_fIor = 1.5f;
    TextureRef m_oBaseColorTex, m_oMetalRoughTex, m_oNormalTex, m_oOcclusionTex, m_oEmissiveTex;
    TextureRef m_oSpecularTex, m_oSpecularColorTex;
};

struct Image {
    std::string m_strName;
    int m_iWidth = 0, m_iHeight = 0;
    std::vector<uint8_t> m_vRgba;           // decoded, 4 bytes per pixel; empty = could not decode
};

struct Texture {
    int m_iImage = -1;
    bool m_bSrgb = false;                   // used as base colour / emissive (colour data)
    bool m_bRepeatS = true, m_bRepeatT = true, m_bMirrorS = false, m_bMirrorT = false;
    bool m_bNearest = false;                // magFilter NEAREST
};

struct MorphTarget {
    std::vector<glm::vec3> m_vPosition, m_vNormal, m_vTangent;   // deltas; empty = this target does not move it
};

enum class Topology { Triangles, Lines, Points };

struct Primitive {
    Topology m_eTopology = Topology::Triangles;
    int m_iMaterial = -1;                   // -1 = the default material (white, rough)
    std::vector<glm::vec3> m_vPosition, m_vNormal;
    std::vector<glm::vec4> m_vTangent;      // xyz + handedness; empty = none (the shader derives one)
    std::vector<glm::vec2> m_vUv0, m_vUv1;
    std::vector<glm::vec4> m_vColor;        // COLOR_0 (rgb -> a = 1); empty = white
    std::vector<glm::vec4> m_vJoints, m_vWeights;   // JOINTS_0 / WEIGHTS_0 (joints as floats)
    std::vector<uint32_t> m_vIndices;       // always present (generated when the file has none)
    std::vector<MorphTarget> m_vTargets;
    glm::vec3 m_v3BoundsMin{0.0f}, m_v3BoundsMax{0.0f};   // of `position`
    bool f_Skinned() const { return !m_vJoints.empty() && !m_vWeights.empty(); }
};

struct Mesh {
    std::string m_strName;
    std::vector<Primitive> m_vPrimitives;
    std::vector<float> m_vWeights;          // default morph weights
    std::vector<std::string> m_vTargetNames;
};

struct Node {
    std::string m_strName;
    int m_iParent = -1;
    std::vector<int> m_vChildren;
    glm::vec3 m_v3Translation{0.0f};
    glm::quat m_qRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 m_v3Scale{1.0f};
    int m_iMesh = -1, m_iSkin = -1;
    int m_iLight = -1;                      // KHR_lights_punctual
    int m_iCamera = -1;
    std::vector<float> m_vWeights;          // morph weights on the node (else the mesh's)
    std::vector<glm::mat4> m_vInstances;    // EXT_mesh_gpu_instancing: local matrices, empty = one draw
};

// A camera of the file. A node places it: it looks down the node's -Z axis, +Y up.
struct Camera {
    std::string m_strName;                    // the camera's name, else its node's (or a parent's)
    bool m_bPerspective = true;
    float m_fYfov = 0.8f;                   // perspective: vertical field of view, radians
    float m_fAspectRatio = 0.0f;            // perspective: 0 = use the window's
    float m_fXmag = 1.0f, m_fYmag = 1.0f;      // orthographic: half width / height
    float m_fZnear = 0.01f;
    float m_fZfar = 0.0f;                   // 0 = infinite (perspective)
};

// A camera where its node is now (Pose::cameras()).
struct PlacedCamera {
    int m_iCamera = -1;                     // index into Model::cameras
    int m_iNode = -1;
    glm::mat4 m_mat4World{1.0f};               // rotation + position only (a node's scale taken out)
};

// KHR_lights_punctual. A node places it: the light shines down the node's -Z axis.
struct Light {
    enum class Type { Directional, Point, Spot };
    std::string m_strName;
    Type m_eType = Type::Point;
    glm::vec3 m_v3Color{1.0f};
    float m_fIntensity = 1.0f;              // lux (directional) / candela (point, spot), as in the file
    float m_fRange = 0.0f;                  // 0 = infinite
    float m_fInnerCone = 0.0f, m_fOuterCone = 0.7853982f;   // spot, radians from the axis
};

// A light where a node puts it in the world (Pose::lights()).
struct PlacedLight {
    int m_iLight = -1;                      // index into Model::lights
    glm::vec3 m_v3Position{0.0f};
    glm::vec3 m_v3Direction{0.0f, 0.0f, -1.0f};   // where it shines (normalized)
};

struct Skin {
    std::string m_strName;
    std::vector<int> m_vJoints;             // node indices
    std::vector<glm::mat4> m_vInverseBind;  // one per joint (identity when the file has none)
};

enum class Path { Translation, Rotation, Scale, Weights };
enum class Interpolation { Linear, Step, CubicSpline };

struct Sampler {
    std::vector<float> m_vInput;            // key times, seconds
    std::vector<float> m_vOutput;           // values: keys x components (x3 for cubic spline: in, value, out)
    int m_iComponents = 0;                  // 3, 4, or the number of morph targets
    Interpolation m_eInterpolation = Interpolation::Linear;
};

struct Channel {
    int m_iSampler = 0;
    int m_iNode = -1;
    Path m_ePath = Path::Translation;
};

struct Animation {
    std::string m_strName;
    std::vector<Sampler> m_vSamplers;
    std::vector<Channel> m_vChannels;
    float m_fDuration = 0.0f;               // the last key time over every sampler
};

struct Scene {
    std::string m_strName;
    std::vector<int> m_vRoots;
};

// ---- Physics: KHR_implicit_shapes + KHR_physics_rigid_bodies (Khronos drafts; docs/engine/08_GLTF.md §7) ----
// Data only, as the file says it (defaults from the extensions' schemas). The simulation is gltf_physics.h.

// KHR_implicit_shapes: a shape centred on its node's origin; capsules and cylinders run along +Y.
struct PhysicsShape {
    enum class Type { Sphere, Box, Capsule, Cylinder, Plane };
    Type m_eType = Type::Box;
    float m_fRadius = 0.5f;                       // sphere
    glm::vec3 m_v3Size{1.0f};                      // box: full extents
    float m_fHeight = 0.5f;                       // capsule: between the two sphere centres; cylinder: full height
    float m_fRadiusTop = 0.25f, m_fRadiusBottom = 0.25f;   // capsule / cylinder (a cone has one radius 0)
    float m_fSizeX = 0.0f, m_fSizeZ = 0.0f;          // plane: 0 = infinite on that axis; the normal is +Y
    bool m_bDoubleSided = false;                  // plane
};

enum class PhysicsCombine { Unset, Average, Minimum, Maximum, Multiply };

struct PhysicsMaterial {
    float m_fStaticFriction = 0.6f, m_fDynamicFriction = 0.6f, m_fRestitution = 0.0f;
    PhysicsCombine m_eFrictionCombine = PhysicsCombine::Unset, m_eRestitutionCombine = PhysicsCombine::Unset;
};

// Two bodies collide when each one's filter lets the other's systems through. A collider with no filter collides
// with everything.
struct PhysicsFilter {
    std::vector<std::string> m_vSystems;          // collisionSystems: what this collider is
    std::vector<std::string> m_vCollideWith;      // collideWithSystems: only these (when not empty)
    std::vector<std::string> m_vNotCollideWith;   // notCollideWithSystems: everything but these
};

struct PhysicsJointLimit {
    std::vector<int> m_vLinearAxes, m_vAngularAxes;  // 0 = X, 1 = Y, 2 = Z of the joint frames
    bool m_bHasMin = false, m_bHasMax = false;
    float m_fMin = 0.0f, m_fMax = 0.0f;              // metres or radians; no min / max = free on that side
    float m_fStiffness = -1.0f;                   // < 0: a hard limit; else a spring beyond the limit
    float m_fDamping = 0.0f;
};

struct PhysicsJointDrive {
    bool m_bAngular = false;                      // type "angular" (else "linear")
    bool m_bAcceleration = false;                 // mode "acceleration" (else "force")
    int m_iAxis = 0;
    float m_fMaxForce = -1.0f;                    // < 0: unlimited
    bool m_bHasPositionTarget = false, m_bHasVelocityTarget = false;
    float m_fPositionTarget = 0.0f, m_fVelocityTarget = 0.0f;
    float m_fStiffness = 0.0f, m_fDamping = 0.0f;
};

struct PhysicsJoint {
    std::vector<PhysicsJointLimit> m_vLimits;
    std::vector<PhysicsJointDrive> m_vDrives;
};

// A collider's or trigger's geometry: an implicit shape, or a mesh of the file (as triangles, or its convex hull).
struct PhysicsGeometry {
    int m_iShape = -1;                            // into Physics::shapes
    int m_iMesh = -1;                             // into Model::meshes
    bool m_bConvexHull = false;
    bool f_Valid() const { return m_iShape >= 0 || m_iMesh >= 0; }
};

// What one node says (KHR_physics_rigid_bodies on the node).
struct NodePhysics {
    // motion: the node (and its descendants without their own motion) is one rigid body
    bool m_bHasMotion = false;
    bool m_bKinematic = false;
    float m_fMass = -1.0f;                        // kg; < 0: from the colliders' volume (density 1000); 0 = infinite
    bool m_bHasCenterOfMass = false;
    glm::vec3 m_v3CenterOfMass{0.0f};              // node space
    bool m_bHasInertia = false;
    glm::vec3 m_v3InertiaDiagonal{0.0f};           // kg m^2; a 0 component = no rotation about that axis
    glm::quat m_qInertiaOrientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 m_v3LinearVelocity{0.0f}, m_v3AngularVelocity{0.0f};   // node space, at the start
    float m_fGravityFactor = 1.0f;
    // collider: solid geometry (on a node without motion in its ancestry: a static collider)
    bool m_bHasCollider = false;
    PhysicsGeometry m_oCollider;
    int m_iMaterial = -1, m_iFilter = -1;
    // trigger: reports what enters and leaves it, no physical response
    bool m_bHasTrigger = false;
    PhysicsGeometry m_oTrigger;
    std::vector<int> m_vTriggerNodes;             // a compound trigger: descendant nodes with their own trigger
    int m_iTriggerFilter = -1;
    // joint: constrains this node's body to connectedNode's body; both nodes are the joint's frames
    bool m_bHasJoint = false;
    int m_iConnectedNode = -1, m_iJoint = -1;
    bool m_bEnableCollision = false;
};

struct Physics {
    std::vector<PhysicsShape> m_vShapes;          // KHR_implicit_shapes
    std::vector<PhysicsMaterial> m_vMaterials;
    std::vector<PhysicsFilter> m_vFilters;
    std::vector<PhysicsJoint> m_vJoints;
    std::vector<NodePhysics> m_vNodes;            // one per Model::nodes entry when the file uses physics, else empty
    bool f_Any() const { return !m_vNodes.empty(); }
};

struct Model {
    std::string m_strPath;
    std::string m_strGenerator;
    std::vector<Image> m_vImages;
    std::vector<Texture> m_vTextures;
    std::vector<Material> m_vMaterials;
    std::vector<Mesh> m_vMeshes;
    std::vector<Node> m_vNodes;
    std::vector<Skin> m_vSkins;
    std::vector<Light> m_vLights;           // KHR_lights_punctual
    std::vector<Camera> m_vCameras;
    std::vector<Animation> m_vAnimations;
    std::vector<Scene> m_vScenes;
    int m_iScene = 0;                       // the default scene
    Physics m_oPhysics;                     // KHR_physics_rigid_bodies + KHR_implicit_shapes (gltf_physics.h simulates it)
    std::vector<std::string> m_vExtensionsUsed;
    std::vector<std::string> m_vIgnoredExtensions;   // used by the file, not rendered (the model still loads)
    std::vector<std::string> m_vWarnings;            // e.g. a skin with more than kMaxJoints joints, an image that did not decode

    // Counts for a status line.
    size_t f_VertexCount() const;
    size_t f_TriangleCount() const;
    size_t f_MorphTargetCount() const;     // over every mesh
    size_t f_InstanceCount() const;        // EXT_mesh_gpu_instancing instances over every node
};

// Loads a .gltf (with its .bin / images next to it, or data URIs) or a .glb. False + error on failure.
bool f_LoadModel(const std::string& l_strPath, Model& l_oOut, std::string* l_pError = nullptr);
// The same from memory (a .glb or a .gltf whose resources are embedded); baseDir resolves files.
bool f_LoadModelFromMemory(const void* l_pData, size_t l_nSize, const std::string& l_strBaseDir, Model& l_oOut, std::string* l_pError = nullptr);

// One posed copy of a model: per-node transforms (from the file, then animated), world matrices,
// morph weights. Several Poses can share one Model (a crowd of the same character).
class Pose {
public:
    void f_Bind(const Model* l_pModel);       // the rest pose
    const Model* f_Model() const { return m_pModel; }

    void f_Reset();                        // back to the rest pose
    // Applies animation `index` at time t (seconds, clamped to 0..duration -- wrap it for a loop).
    void f_Apply(int l_iIndex, float l_fT);
    // Blends animation `index` at time t over the current values with weight w (0..1).
    void f_Blend(int l_iIndex, float l_fT, float l_fW);
    void f_UpdateWorld();                  // world matrices from the local transforms (call after apply)

    const glm::mat4& f_World(int l_iNode) const { return m_vWorld[(size_t)l_iNode]; }
    const std::vector<glm::mat4>& f_Worlds() const { return m_vWorld; }
    // Joint matrices of a skin: world(joint) * inverseBind (vertices go straight to model space, so a
    // skinned primitive is drawn with an identity model matrix, as the glTF spec asks).
    void f_JointMatrices(int l_iSkin, std::vector<glm::mat4>& l_vOut) const;
    // Morph weights of a node's mesh (the node's own, else the mesh's defaults; animated).
    const std::vector<float>& f_Weights(int l_iNode) const { return m_vWeights[(size_t)l_iNode]; }
    void f_SetWeight(int l_iNode, size_t l_nTarget, float l_fW) {   // after apply(): a manual morph weight
        if (l_iNode >= 0 && l_iNode < (int)m_vWeights.size() && l_nTarget < m_vWeights[(size_t)l_iNode].size()) m_vWeights[(size_t)l_iNode][l_nTarget] = l_fW;
    }

    glm::vec3 f_Translation(int l_iN) const { return m_vT[(size_t)l_iN]; }
    glm::quat f_Rotation(int l_iN) const { return m_vR[(size_t)l_iN]; }
    glm::vec3 f_Scale(int l_iN) const { return m_vS[(size_t)l_iN]; }
    // Sets a node's local transform (physics writes its bodies back this way); call updateWorld() after.
    void f_SetLocal(int l_iN, const glm::vec3& l_v3T, const glm::quat& l_qR, const glm::vec3& l_v3S) {
        if (l_iN < 0 || l_iN >= (int)m_vT.size()) return;
        m_vT[(size_t)l_iN] = l_v3T; m_vR[(size_t)l_iN] = l_qR; m_vS[(size_t)l_iN] = l_v3S;
    }

    // The file's lights (KHR_lights_punctual) where their nodes are now (call after updateWorld()).
    std::vector<PlacedLight> f_Lights() const;
    // The file's cameras where their nodes are now (animated cameras move; call after updateWorld()).
    std::vector<PlacedCamera> f_Cameras() const;

    // Bounds of the default scene in world space at this pose (skinned meshes skinned on the CPU,
    // instances included). False when there is nothing to bound.
    bool f_Bounds(glm::vec3& l_v3Mn, glm::vec3& l_v3Mx) const;

private:
    void f_Sample(int l_iIndex, float l_fT, float l_fW);

    const Model* m_pModel = nullptr;
    std::vector<glm::vec3> m_vT, m_vS;
    std::vector<glm::quat> m_vR;
    std::vector<std::vector<float>> m_vWeights;
    std::vector<glm::mat4> m_vWorld;
};

// The local matrix T * R * S.
glm::mat4 f_ComposeTRS(const glm::vec3& l_v3T, const glm::quat& l_qR, const glm::vec3& l_v3S);

// Morphs a primitive's base attributes by `weights` (CPU morphing; the renderer uploads the result).
void f_Morph(const Primitive& l_oP, const std::vector<float>& l_vWeights, std::vector<glm::vec3>& l_vPosition,
           std::vector<glm::vec3>& l_vNormal, std::vector<glm::vec4>& l_vTangent);

}  // namespace toms::gltf
