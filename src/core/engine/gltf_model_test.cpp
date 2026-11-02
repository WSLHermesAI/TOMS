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
#include <filesystem>
#include <fstream>
#include <string>

#include "gltf_model.h"

static int g_iChecks = 0, g_iFails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_iChecks;                                                               \
        if (!(cond)) {                                                            \
            ++g_iFails;                                                            \
            fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fprintf(stderr, "\n");                                                \
        }                                                                         \
    } while (0)

using namespace toms::gltf;

namespace {

bool f_Near(float l_fA, float l_fB, float l_fEps = 1e-4f) { return std::fabs(l_fA - l_fB) <= l_fEps; }
bool f_Near3(const glm::vec3& l_v3A, const glm::vec3& l_v3B, float l_fEps = 1e-4f) { return glm::length(l_v3A - l_v3B) <= l_fEps; }
float f_AngleDeg(const glm::quat& l_qQ) { return glm::degrees(2.0f * std::acos(std::fabs(std::clamp(l_qQ.w, -1.0f, 1.0f)))); }

bool f_Load(const char* l_pszName, Model& l_oM) {
    std::string l_strErr;
    for (const char* l_pszDir : {"../tests/gltf/", "tests/gltf/"})
        if (f_LoadModel(std::string(l_pszDir) + l_pszName, l_oM, &l_strErr)) return true;
    fprintf(stderr, "  cannot load %s: %s\n", l_pszName, l_strErr.c_str());
    return false;
}

void f_TestSkin() {
    Model l_oM;
    CHECK(f_Load("skin_tube.gltf", l_oM), "skin_tube.gltf loads");
    if (l_oM.m_vMeshes.empty()) return;
    const Primitive& l_oP = l_oM.m_vMeshes[0].m_vPrimitives[0];
    CHECK(l_oM.m_vSkins.size() == 1 && l_oM.m_vSkins[0].m_vJoints.size() == 3, "one skin, 3 joints");
    CHECK(l_oP.f_Skinned() && l_oP.m_vPosition.size() == 13 * 16 && !l_oP.m_vColor.empty(), "skinned tube: 208 vertices with colours");
    CHECK(l_oM.m_vAnimations.size() == 2 && l_oM.m_vAnimations[0].m_strName == "bend" && l_oM.m_vAnimations[1].m_strName == "wave", "animations bend, wave");
    CHECK(f_Near(l_oM.m_vAnimations[0].m_fDuration, 2.0f), "bend lasts 2 s");

    Pose l_oPose;
    l_oPose.f_Bind(&l_oM);
    std::vector<glm::mat4> l_vJoints;
    l_oPose.f_JointMatrices(0, l_vJoints);
    bool l_bIdentity = l_vJoints.size() == 3;
    for (const glm::mat4& l_mat4J : l_vJoints)
        for (int l_iC = 0; l_iC < 4; l_iC++)
            for (int l_iR = 0; l_iR < 4; l_iR++) l_bIdentity = l_bIdentity && f_Near(l_mat4J[l_iC][l_iR], l_iC == l_iR ? 1.0f : 0.0f);
    CHECK(l_bIdentity, "rest pose: every joint matrix is the identity (world x inverse bind)");

    l_oPose.f_Apply(0, 0.5f);
    CHECK(f_Near(f_AngleDeg(l_oPose.f_Rotation(2)), 35.0f, 0.01f), "bend at 0.5 s: joint1 at its 35 deg key (%.3f)", f_AngleDeg(l_oPose.f_Rotation(2)));
    l_oPose.f_Reset();
    l_oPose.f_Apply(0, 0.25f);
    CHECK(f_Near(f_AngleDeg(l_oPose.f_Rotation(2)), 17.5f, 0.01f), "bend at 0.25 s: slerp halfway, 17.5 deg (%.3f)", f_AngleDeg(l_oPose.f_Rotation(2)));

    l_oPose.f_Reset();
    l_oPose.f_Apply(0, 0.5f);
    l_oPose.f_UpdateWorld();
    glm::vec3 l_v3Mn, l_v3Mx;
    CHECK(l_oPose.f_Bounds(l_v3Mn, l_v3Mx) && l_v3Mn.x < -1.0f, "bent tube: CPU-skinned bounds lean to -x (min x %.2f)", l_v3Mn.x);

    // wave: cubic spline with zero tangents -> smoothstep between the keys.
    l_oPose.f_Reset();
    l_oPose.f_Apply(1, 0.5f);
    CHECK(f_Near3(l_oPose.f_Translation(1), {0, 0.2f, 0}, 1e-4f), "wave at 0.5 s: cubic spline halfway, y 0.2 (%.4f)", l_oPose.f_Translation(1).y);
    l_oPose.f_Apply(1, 0.25f);
    CHECK(f_Near(l_oPose.f_Translation(1).y, 0.4f * (3 * 0.0625f - 2 * 0.015625f), 1e-4f), "wave at 0.25 s: hermite value (%.4f)",
          l_oPose.f_Translation(1).y);
    l_oPose.f_Apply(1, 1.0f);
    CHECK(f_Near3(l_oPose.f_Translation(1), {0, 0.4f, 0}), "wave at 1 s: the key value");
    l_oPose.f_Apply(1, 99.0f);
    CHECK(f_Near3(l_oPose.f_Translation(1), {0, 0, 0}), "past the end: the last key");
}

void f_TestMorph() {
    Model l_oM;
    CHECK(f_Load("morph_cube.gltf", l_oM), "morph_cube.gltf loads");
    if (l_oM.m_vMeshes.empty()) return;
    const Mesh& l_oMesh = l_oM.m_vMeshes[0];
    CHECK(l_oMesh.m_vPrimitives[0].m_vTargets.size() == 2 && l_oMesh.m_vWeights.size() == 2, "2 morph targets");
    CHECK(l_oMesh.m_vTargetNames.size() == 2 && l_oMesh.m_vTargetNames[0] == "bulge" && l_oMesh.m_vTargetNames[1] == "squash", "target names from extras");
    CHECK(l_oM.f_MorphTargetCount() == 2, "morphTargetCount");
    Pose l_oPose;
    l_oPose.f_Bind(&l_oM);
    l_oPose.f_Apply(0, 1.0f);
    CHECK(f_Near(l_oPose.f_Weights(0)[0], 1.0f) && f_Near(l_oPose.f_Weights(0)[1], 0.0f), "breathe at 1 s: bulge 1, squash 0");
    l_oPose.f_Apply(0, 1.5f);
    CHECK(f_Near(l_oPose.f_Weights(0)[0], 0.5f) && f_Near(l_oPose.f_Weights(0)[1], 0.5f), "breathe at 1.5 s: halfway between");
    l_oPose.f_Apply(1, 0.75f);
    CHECK(f_Near(l_oPose.f_Weights(0)[0], 1.0f) && f_Near(l_oPose.f_Weights(0)[1], 0.0f), "steps at 0.75 s: holds the 0.5 s key");
    l_oPose.f_Apply(1, 1.5f);
    CHECK(f_Near(l_oPose.f_Weights(0)[0], 1.0f) && f_Near(l_oPose.f_Weights(0)[1], 1.0f), "steps at 1.5 s: the last key");

    std::vector<glm::vec3> l_vPos, l_vNrm;
    std::vector<glm::vec4> l_vTan;
    f_Morph(l_oMesh.m_vPrimitives[0], {1.0f, 0.0f}, l_vPos, l_vNrm, l_vTan);
    float l_fTop = -1e9f;
    for (const glm::vec3& l_v3V : l_vPos) l_fTop = std::max(l_fTop, l_v3V.y);
    CHECK(f_Near(l_fTop, 1.1f), "morph(bulge = 1): the top rises 0.6 to 1.1 (%.3f)", l_fTop);
    f_Morph(l_oMesh.m_vPrimitives[0], {0.0f, 1.0f}, l_vPos, l_vNrm, l_vTan);
    float l_fWide = -1e9f;
    for (const glm::vec3& l_v3V : l_vPos) l_fWide = std::max(l_fWide, l_v3V.x);
    CHECK(f_Near(l_fWide, 0.8f), "morph(squash = 1): wider, x 0.8 (%.3f)", l_fWide);
}

void f_TestInstancing() {
    Model l_oM;
    CHECK(f_Load("instancing.gltf", l_oM), "instancing.gltf loads (EXT_mesh_gpu_instancing is required by the file)");
    if (l_oM.m_vNodes.empty()) return;
    CHECK(l_oM.m_vNodes[0].m_vInstances.size() == 100 && l_oM.f_InstanceCount() == 100, "100 instances");
    CHECK(f_Near3(glm::vec3(l_oM.m_vNodes[0].m_vInstances[0][3]), {-4.5f, 0.25f, -4.5f}), "instance 0 translation");
    CHECK(l_oM.m_vIgnoredExtensions.empty(), "no ignored extensions");
    Pose l_oPose;
    l_oPose.f_Bind(&l_oM);
    glm::vec3 l_v3Mn, l_v3Mx;
    CHECK(l_oPose.f_Bounds(l_v3Mn, l_v3Mx) && l_v3Mn.x < -4.6f && l_v3Mx.x > 4.6f && l_v3Mn.z < -4.6f && l_v3Mx.z > 4.6f, "bounds cover every instance");
}

void f_TestMaterials() {
    Model l_oM;
    CHECK(f_Load("pbr_materials.gltf", l_oM), "pbr_materials.gltf loads");
    if (l_oM.m_vMaterials.size() < 30) return;
    CHECK(f_Near(l_oM.m_vMaterials[0].m_fMetallic, 0.0f) && f_Near(l_oM.m_vMaterials[0].m_fRoughness, 0.05f), "sphere 0: metallic 0, roughness 0.05");
    CHECK(f_Near(l_oM.m_vMaterials[24].m_fMetallic, 1.0f) && f_Near(l_oM.m_vMaterials[24].m_fRoughness, 1.0f), "sphere 24: metallic 1, roughness 1");
    const Material& l_oEm = l_oM.m_vMaterials[25];
    CHECK(f_Near3(l_oEm.m_v3Emissive, {2.0f, 1.2f, 0.2f}), "emissive x KHR_materials_emissive_strength 2");
    const Material& l_oChk = l_oM.m_vMaterials[26];
    CHECK(l_oChk.m_oBaseColorTex.f_Valid() && f_Near(l_oChk.m_oBaseColorTex.m_v2Scale.x, 2.0f), "checker: texture with KHR_texture_transform scale 2");
    CHECK(l_oM.m_vTextures.size() == 1 && l_oM.m_vTextures[0].m_bSrgb && l_oM.m_vTextures[0].m_bNearest, "base colour texture: sRGB, nearest");
    CHECK(l_oM.m_vImages.size() == 1 && l_oM.m_vImages[0].m_iWidth == 64 && l_oM.m_vImages[0].m_vRgba.size() == 64 * 64 * 4, "embedded PNG decoded, 64x64");
    CHECK(l_oM.m_vImages[0].m_vRgba[0] == 235 && l_oM.m_vImages[0].m_vRgba[(31 * 64 + 31) * 4 + 2] == 255, "pixels: checker + blue cross");
    CHECK(l_oM.m_vMaterials[27].m_eAlphaMode == AlphaMode::Mask && l_oM.m_vMaterials[27].m_bDoubleSided, "mask, double-sided");
    CHECK(l_oM.m_vMaterials[28].m_eAlphaMode == AlphaMode::Blend && f_Near(l_oM.m_vMaterials[28].m_v4BaseColor.a, 0.45f), "blend, alpha 0.45");
    CHECK(l_oM.m_vMaterials[29].m_bUnlit, "KHR_materials_unlit");
    CHECK(l_oM.m_vWarnings.empty(), "no warnings");
}

void f_TestErrors() {
    Model l_oM;
    std::string l_strErr;
    CHECK(!f_LoadModel("../tests/gltf/does_not_exist.gltf", l_oM, &l_strErr) && !l_strErr.empty(), "a missing file: %s", l_strErr.c_str());
    const char* l_pszDraco = R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_draco_mesh_compression"],
                           "extensionsRequired":["KHR_draco_mesh_compression"]})";
    l_strErr.clear();
    CHECK(!f_LoadModelFromMemory(l_pszDraco, std::strlen(l_pszDraco), ".", l_oM, &l_strErr) && l_strErr.find("KHR_draco_mesh_compression") != std::string::npos,
          "a required extension we cannot read is refused: %s", l_strErr.c_str());
    const char* l_pszGlass = R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_transmission","KHR_materials_volume"],
        "materials":[{"extensions":{"KHR_materials_transmission":{"transmissionFactor":1},
                                    "KHR_materials_volume":{"attenuationColor":[0.8,0.7,0.6],"thicknessFactor":0.2}}}]})";
    CHECK(f_LoadModelFromMemory(l_pszGlass, std::strlen(l_pszGlass), ".", l_oM) && l_oM.m_vIgnoredExtensions.empty() && l_oM.m_vMaterials.size() == 1 &&
              f_Near(l_oM.m_vMaterials[0].m_fTransmission, 1.0f) && f_Near3(l_oM.m_vMaterials[0].m_v3AttenuationColor, {0.8f, 0.7f, 0.6f}),
          "glass: KHR_materials_transmission + volume read (drawn, not ignored)");
    // KHR_materials_specular + _ior: the factors, the two textures (the colour one is sRGB, the other is not), the defaults.
    const char* l_pszSpec = R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_specular","KHR_materials_ior"],
        "textures":[{},{}],
        "materials":[{"extensions":{"KHR_materials_specular":{"specularFactor":0.5,"specularColorFactor":[2,1,0],
                                                              "specularTexture":{"index":0},"specularColorTexture":{"index":1}},
                                    "KHR_materials_ior":{"ior":1.8}}},{}]})";
    CHECK(f_LoadModelFromMemory(l_pszSpec, std::strlen(l_pszSpec), ".", l_oM) && l_oM.m_vIgnoredExtensions.empty() && l_oM.m_vMaterials.size() == 2,
          "KHR_materials_specular + _ior read (drawn, not ignored)");
    if (l_oM.m_vMaterials.size() == 2 && l_oM.m_vTextures.size() == 2) {
        const Material& l_oS = l_oM.m_vMaterials[0];
        CHECK(f_Near(l_oS.m_fSpecular, 0.5f) && f_Near3(l_oS.m_v3SpecularColor, {2.0f, 1.0f, 0.0f}) && f_Near(l_oS.m_fIor, 1.8f), "specular factors and ior");
        CHECK(l_oS.m_oSpecularTex.m_iTexture == 0 && l_oS.m_oSpecularColorTex.m_iTexture == 1 && !l_oM.m_vTextures[0].m_bSrgb && l_oM.m_vTextures[1].m_bSrgb,
              "specular textures: the colour texture is sRGB, the factor texture (alpha) is not");
        const Material& l_oD = l_oM.m_vMaterials[1];
        CHECK(f_Near(l_oD.m_fSpecular, 1.0f) && f_Near3(l_oD.m_v3SpecularColor, {1.0f, 1.0f, 1.0f}) && f_Near(l_oD.m_fIor, 1.5f) && !l_oD.m_oSpecularTex.f_Valid(),
              "without the extensions: specular 1, colour white, ior 1.5 (F0 0.04)");
    }
    // KHR_lights_punctual: a spot on a node turned to look down -Y (rotation -90 deg about X).
    const char* l_pszLights = R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_lights_punctual"],
        "extensions":{"KHR_lights_punctual":{"lights":[{"type":"spot","color":[1,0.5,0.25],"intensity":40,"range":12,
                                                         "spot":{"innerConeAngle":0.2,"outerConeAngle":0.5}}]}},
        "scene":0,"scenes":[{"nodes":[0]}],
        "nodes":[{"translation":[1,4,2],"rotation":[-0.7071068,0,0,0.7071068],
                  "extensions":{"KHR_lights_punctual":{"light":0}}}]})";
    CHECK(f_LoadModelFromMemory(l_pszLights, std::strlen(l_pszLights), ".", l_oM) && l_oM.m_vLights.size() == 1 && l_oM.m_vIgnoredExtensions.empty(),
          "KHR_lights_punctual: one light, not ignored");
    if (l_oM.m_vLights.size() == 1) {
        const Light& l_oL = l_oM.m_vLights[0];
        CHECK(l_oL.m_eType == Light::Type::Spot && f_Near(l_oL.m_fIntensity, 40.0f) && f_Near(l_oL.m_fRange, 12.0f) && f_Near(l_oL.m_fInnerCone, 0.2f) &&
                  f_Near(l_oL.m_fOuterCone, 0.5f) && f_Near3(l_oL.m_v3Color, {1.0f, 0.5f, 0.25f}),
              "spot light: colour, intensity, range, cones");
        Pose l_oPose;
        l_oPose.f_Bind(&l_oM);
        const std::vector<PlacedLight> l_vPlaced = l_oPose.f_Lights();
        CHECK(l_vPlaced.size() == 1 && f_Near3(l_vPlaced[0].m_v3Position, {1, 4, 2}) && f_Near3(l_vPlaced[0].m_v3Direction, {0, -1, 0}, 1e-4f),
              "placed by its node: at (1, 4, 2), shining down -Y");
    }
    // Cameras: a perspective one on a scaled node under a named parent, and an orthographic one.
    const char* l_pszCams = R"({"asset":{"version":"2.0"},
        "cameras":[{"type":"perspective","perspective":{"yfov":0.9,"znear":0.5,"zfar":500,"aspectRatio":1.5}},
                   {"type":"orthographic","name":"top","orthographic":{"xmag":4,"ymag":3,"znear":0.1,"zfar":50}}],
        "scene":0,"scenes":[{"nodes":[0,2]}],
        "nodes":[{"name":"car","translation":[10,0,0],"children":[1]},
                 {"camera":0,"translation":[0,2,5],"scale":[20,20,20]},
                 {"camera":1,"translation":[0,30,0],"rotation":[-0.7071068,0,0,0.7071068]}]})";
    CHECK(f_LoadModelFromMemory(l_pszCams, std::strlen(l_pszCams), ".", l_oM) && l_oM.m_vCameras.size() == 2, "two cameras");
    if (l_oM.m_vCameras.size() == 2) {
        const Camera& l_oA = l_oM.m_vCameras[0];
        const Camera& l_oB = l_oM.m_vCameras[1];
        CHECK(l_oA.m_bPerspective && f_Near(l_oA.m_fYfov, 0.9f) && f_Near(l_oA.m_fZnear, 0.5f) && f_Near(l_oA.m_fZfar, 500.0f) && f_Near(l_oA.m_fAspectRatio, 1.5f) &&
                  l_oA.m_strName == "car",
              "perspective: fov, near, far, aspect; unnamed -> its parent's name (%s)", l_oA.m_strName.c_str());
        CHECK(!l_oB.m_bPerspective && f_Near(l_oB.m_fXmag, 4.0f) && f_Near(l_oB.m_fYmag, 3.0f) && l_oB.m_strName == "top", "orthographic: xmag, ymag, name");
        Pose l_oPose;
        l_oPose.f_Bind(&l_oM);
        const std::vector<PlacedCamera> l_vPlaced = l_oPose.f_Cameras();
        CHECK(l_vPlaced.size() == 2 && f_Near3(glm::vec3(l_vPlaced[0].m_mat4World[3]), {10, 2, 5}) && f_Near(glm::length(glm::vec3(l_vPlaced[0].m_mat4World[0])), 1.0f),
              "placed: at (10, 2, 5) through its parent, the node's x20 scale taken out");
        CHECK(l_vPlaced.size() == 2 && f_Near3(-glm::vec3(l_vPlaced[1].m_mat4World[2]), {0, -1, 0}, 1e-4f), "the top camera looks straight down");
    }
    const char* l_pszExtra = R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_clearcoat"]})";
    CHECK(f_LoadModelFromMemory(l_pszExtra, std::strlen(l_pszExtra), ".", l_oM) && l_oM.m_vIgnoredExtensions.size() == 1, "an optional one is listed as ignored");
}

}  // namespace

// Paths are UTF-8: a .gltf whose own name and whose external .bin have non-ASCII names (glTF-Sample-Models'
// "Unicode❤♻Test"). On Windows a plain std::ifstream / fopen read them in the ANSI code page and found nothing.
static void f_TestUnicodePaths() {
    namespace fs = std::filesystem;
    const fs::path l_oDir = fs::temp_directory_path() / fs::u8path("toms_gltf_❤♻");
    std::error_code l_oEc;
    fs::create_directories(l_oDir, l_oEc);
    // one triangle: 3 positions in an external buffer named "tri❤♻.bin"
    const float l_arrPos[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    {
        std::ofstream l_oBin(l_oDir / fs::u8path("tri❤♻.bin"), std::ios::binary);
        l_oBin.write((const char*)l_arrPos, sizeof l_arrPos);
    }
    const char* l_pszJson = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"tri❤♻.bin","byteLength":36}],
        "bufferViews":[{"buffer":0,"byteLength":36}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    {
        std::ofstream l_oF(l_oDir / fs::u8path("Unicode❤♻Test.gltf"), std::ios::binary);
        l_oF << l_pszJson;
    }
    const std::string l_strPath = (l_oDir / fs::u8path("Unicode❤♻Test.gltf")).u8string();
    Model l_oM;
    std::string l_strError;
    CHECK(f_LoadModel(l_strPath, l_oM, &l_strError) && l_oM.f_VertexCount() == 3, "a .gltf with a non-ASCII name and .bin loads (%s)", l_strError.c_str());
    fs::remove_all(l_oDir, l_oEc);
}

int main() {
    f_TestSkin();
    f_TestMorph();
    f_TestInstancing();
    f_TestMaterials();
    f_TestErrors();
    f_TestUnicodePaths();
    std::printf("gltf_model_test: %d check(s), %d failed\n", g_iChecks, g_iFails);
    return g_iFails ? 1 : 0;
}
