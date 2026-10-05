// gltf_model_test.cpp -- glTF loading and posing (gltf_model.h), headless, on the test models in
// tests/gltf/ (made by tools/gltf_viewer/make_test_models.py).
//
//   skin       joints, inverse bind matrices, rest pose = identity joint matrices, linear rotation
//              (slerp), cubic spline, CPU-skinned bounds that follow the bend
//   morph      targets and names, animated weights (linear, step), CPU morphing
//   instancing EXT_mesh_gpu_instancing: 100 instance matrices, bounds over every instance
//   materials  PBR factors, sRGB colour textures, an embedded PNG decoded, KHR_texture_transform,
//              emissive strength, unlit, alpha modes
//   errors     a missing file, a required extension we cannot read
//
// Run: ./toms_tests gltf_model_test   (in assets/: the models are ../tests/gltf/)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "gltf_model.h"

static int g_checks = 0, g_fails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            ++g_fails;                                                            \
            fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fprintf(stderr, "\n");                                                \
        }                                                                         \
    } while (0)

using namespace toms::gltf;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near3(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) <= eps; }
float angleDeg(const glm::quat& q) { return glm::degrees(2.0f * std::acos(std::fabs(std::clamp(q.w, -1.0f, 1.0f)))); }

bool load(const char* name, Model& m) {
    std::string err;
    for (const char* dir : {"../tests/gltf/", "tests/gltf/"})
        if (loadModel(std::string(dir) + name, m, &err)) return true;
    fprintf(stderr, "  cannot load %s: %s\n", name, err.c_str());
    return false;
}

void testSkin() {
    Model m;
    CHECK(load("skin_tube.gltf", m), "skin_tube.gltf loads");
    if (m.meshes.empty()) return;
    const Primitive& p = m.meshes[0].primitives[0];
    CHECK(m.skins.size() == 1 && m.skins[0].joints.size() == 3, "one skin, 3 joints");
    CHECK(p.skinned() && p.position.size() == 13 * 16 && !p.color.empty(), "skinned tube: 208 vertices with colours");
    CHECK(m.animations.size() == 2 && m.animations[0].name == "bend" && m.animations[1].name == "wave", "animations bend, wave");
    CHECK(near(m.animations[0].duration, 2.0f), "bend lasts 2 s");

    Pose pose;
    pose.bind(&m);
    std::vector<glm::mat4> joints;
    pose.jointMatrices(0, joints);
    bool identity = joints.size() == 3;
    for (const glm::mat4& j : joints)
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) identity = identity && near(j[c][r], c == r ? 1.0f : 0.0f);
    CHECK(identity, "rest pose: every joint matrix is the identity (world x inverse bind)");

    pose.apply(0, 0.5f);
    CHECK(near(angleDeg(pose.rotation(2)), 35.0f, 0.01f), "bend at 0.5 s: joint1 at its 35 deg key (%.3f)", angleDeg(pose.rotation(2)));
    pose.reset();
    pose.apply(0, 0.25f);
    CHECK(near(angleDeg(pose.rotation(2)), 17.5f, 0.01f), "bend at 0.25 s: slerp halfway, 17.5 deg (%.3f)", angleDeg(pose.rotation(2)));

    pose.reset();
    pose.apply(0, 0.5f);
    pose.updateWorld();
    glm::vec3 mn, mx;
    CHECK(pose.bounds(mn, mx) && mn.x < -1.0f, "bent tube: CPU-skinned bounds lean to -x (min x %.2f)", mn.x);

    // wave: cubic spline with zero tangents -> smoothstep between the keys.
    pose.reset();
    pose.apply(1, 0.5f);
    CHECK(near3(pose.translation(1), {0, 0.2f, 0}, 1e-4f), "wave at 0.5 s: cubic spline halfway, y 0.2 (%.4f)", pose.translation(1).y);
    pose.apply(1, 0.25f);
    CHECK(near(pose.translation(1).y, 0.4f * (3 * 0.0625f - 2 * 0.015625f), 1e-4f), "wave at 0.25 s: hermite value (%.4f)",
          pose.translation(1).y);
    pose.apply(1, 1.0f);
    CHECK(near3(pose.translation(1), {0, 0.4f, 0}), "wave at 1 s: the key value");
    pose.apply(1, 99.0f);
    CHECK(near3(pose.translation(1), {0, 0, 0}), "past the end: the last key");
}

void testMorph() {
    Model m;
    CHECK(load("morph_cube.gltf", m), "morph_cube.gltf loads");
    if (m.meshes.empty()) return;
    const Mesh& mesh = m.meshes[0];
    CHECK(mesh.primitives[0].targets.size() == 2 && mesh.weights.size() == 2, "2 morph targets");
    CHECK(mesh.targetNames.size() == 2 && mesh.targetNames[0] == "bulge" && mesh.targetNames[1] == "squash", "target names from extras");
    CHECK(m.morphTargetCount() == 2, "morphTargetCount");
    Pose pose;
    pose.bind(&m);
    pose.apply(0, 1.0f);
    CHECK(near(pose.weights(0)[0], 1.0f) && near(pose.weights(0)[1], 0.0f), "breathe at 1 s: bulge 1, squash 0");
    pose.apply(0, 1.5f);
    CHECK(near(pose.weights(0)[0], 0.5f) && near(pose.weights(0)[1], 0.5f), "breathe at 1.5 s: halfway between");
    pose.apply(1, 0.75f);
    CHECK(near(pose.weights(0)[0], 1.0f) && near(pose.weights(0)[1], 0.0f), "steps at 0.75 s: holds the 0.5 s key");
    pose.apply(1, 1.5f);
    CHECK(near(pose.weights(0)[0], 1.0f) && near(pose.weights(0)[1], 1.0f), "steps at 1.5 s: the last key");

    std::vector<glm::vec3> pos, nrm;
    std::vector<glm::vec4> tan;
    morph(mesh.primitives[0], {1.0f, 0.0f}, pos, nrm, tan);
    float top = -1e9f;
    for (const glm::vec3& v : pos) top = std::max(top, v.y);
    CHECK(near(top, 1.1f), "morph(bulge = 1): the top rises 0.6 to 1.1 (%.3f)", top);
    morph(mesh.primitives[0], {0.0f, 1.0f}, pos, nrm, tan);
    float wide = -1e9f;
    for (const glm::vec3& v : pos) wide = std::max(wide, v.x);
    CHECK(near(wide, 0.8f), "morph(squash = 1): wider, x 0.8 (%.3f)", wide);
}

void testInstancing() {
    Model m;
    CHECK(load("instancing.gltf", m), "instancing.gltf loads (EXT_mesh_gpu_instancing is required by the file)");
    if (m.nodes.empty()) return;
    CHECK(m.nodes[0].instances.size() == 100 && m.instanceCount() == 100, "100 instances");
    CHECK(near3(glm::vec3(m.nodes[0].instances[0][3]), {-4.5f, 0.25f, -4.5f}), "instance 0 translation");
    CHECK(m.ignoredExtensions.empty(), "no ignored extensions");
    Pose pose;
    pose.bind(&m);
    glm::vec3 mn, mx;
    CHECK(pose.bounds(mn, mx) && mn.x < -4.6f && mx.x > 4.6f && mn.z < -4.6f && mx.z > 4.6f, "bounds cover every instance");
}

void testMaterials() {
    Model m;
    CHECK(load("pbr_materials.gltf", m), "pbr_materials.gltf loads");
    if (m.materials.size() < 30) return;
    CHECK(near(m.materials[0].metallic, 0.0f) && near(m.materials[0].roughness, 0.05f), "sphere 0: metallic 0, roughness 0.05");
    CHECK(near(m.materials[24].metallic, 1.0f) && near(m.materials[24].roughness, 1.0f), "sphere 24: metallic 1, roughness 1");
    const Material& em = m.materials[25];
    CHECK(near3(em.emissive, {2.0f, 1.2f, 0.2f}), "emissive x KHR_materials_emissive_strength 2");
    const Material& chk = m.materials[26];
    CHECK(chk.baseColorTex.valid() && near(chk.baseColorTex.scale.x, 2.0f), "checker: texture with KHR_texture_transform scale 2");
    CHECK(m.textures.size() == 1 && m.textures[0].srgb && m.textures[0].nearest, "base colour texture: sRGB, nearest");
    CHECK(m.images.size() == 1 && m.images[0].width == 64 && m.images[0].rgba.size() == 64 * 64 * 4, "embedded PNG decoded, 64x64");
    CHECK(m.images[0].rgba[0] == 235 && m.images[0].rgba[(31 * 64 + 31) * 4 + 2] == 255, "pixels: checker + blue cross");
    CHECK(m.materials[27].alphaMode == AlphaMode::Mask && m.materials[27].doubleSided, "mask, double-sided");
    CHECK(m.materials[28].alphaMode == AlphaMode::Blend && near(m.materials[28].baseColor.a, 0.45f), "blend, alpha 0.45");
    CHECK(m.materials[29].unlit, "KHR_materials_unlit");
    CHECK(m.warnings.empty(), "no warnings");
}

void testErrors() {
    Model m;
    std::string err;
    CHECK(!loadModel("../tests/gltf/does_not_exist.gltf", m, &err) && !err.empty(), "a missing file: %s", err.c_str());
    const char* draco = R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_draco_mesh_compression"],
                           "extensionsRequired":["KHR_draco_mesh_compression"]})";
    err.clear();
    CHECK(!loadModelFromMemory(draco, std::strlen(draco), ".", m, &err) && err.find("KHR_draco_mesh_compression") != std::string::npos,
          "a required extension we cannot read is refused: %s", err.c_str());
    const char* glass = R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_transmission","KHR_materials_volume"],
        "materials":[{"extensions":{"KHR_materials_transmission":{"transmissionFactor":1},
                                    "KHR_materials_volume":{"attenuationColor":[0.8,0.7,0.6],"thicknessFactor":0.2}}}]})";
    CHECK(loadModelFromMemory(glass, std::strlen(glass), ".", m) && m.ignoredExtensions.empty() && m.materials.size() == 1 &&
              near(m.materials[0].transmission, 1.0f) && near3(m.materials[0].attenuationColor, {0.8f, 0.7f, 0.6f}),
          "glass: KHR_materials_transmission + volume read (drawn, not ignored)");
    // KHR_lights_punctual: a spot on a node turned to look down -Y (rotation -90 deg about X).
    const char* lights = R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_lights_punctual"],
        "extensions":{"KHR_lights_punctual":{"lights":[{"type":"spot","color":[1,0.5,0.25],"intensity":40,"range":12,
                                                         "spot":{"innerConeAngle":0.2,"outerConeAngle":0.5}}]}},
        "scene":0,"scenes":[{"nodes":[0]}],
        "nodes":[{"translation":[1,4,2],"rotation":[-0.7071068,0,0,0.7071068],
                  "extensions":{"KHR_lights_punctual":{"light":0}}}]})";
    CHECK(loadModelFromMemory(lights, std::strlen(lights), ".", m) && m.lights.size() == 1 && m.ignoredExtensions.empty(),
          "KHR_lights_punctual: one light, not ignored");
    if (m.lights.size() == 1) {
        const Light& l = m.lights[0];
        CHECK(l.type == Light::Type::Spot && near(l.intensity, 40.0f) && near(l.range, 12.0f) && near(l.innerCone, 0.2f) &&
                  near(l.outerCone, 0.5f) && near3(l.color, {1.0f, 0.5f, 0.25f}),
              "spot light: colour, intensity, range, cones");
        Pose pose;
        pose.bind(&m);
        const std::vector<PlacedLight> placed = pose.lights();
        CHECK(placed.size() == 1 && near3(placed[0].position, {1, 4, 2}) && near3(placed[0].direction, {0, -1, 0}, 1e-4f),
              "placed by its node: at (1, 4, 2), shining down -Y");
    }
    const char* extra = R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_clearcoat"]})";
    CHECK(loadModelFromMemory(extra, std::strlen(extra), ".", m) && m.ignoredExtensions.size() == 1, "an optional one is listed as ignored");
}

}  // namespace

int main() {
    testSkin();
    testMorph();
    testInstancing();
    testMaterials();
    testErrors();
    std::printf("gltf_model_test: %d check(s), %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
