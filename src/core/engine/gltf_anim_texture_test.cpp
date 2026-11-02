// Tests for gltf_anim_texture.h: the bake matches Pose::jointMatrices (exactly at frame times, closely between
// them), clips blend through rotation + translation, the crowd's play / cross-fade / blend state, the per-instance
// GPU record, and save / load (including a stale file). Model: tests/gltf/skin_tube.gltf (3 joints; animations
// "bend", a rotation, and "wave", a translation).
// Run: ./toms_tests gltf_anim_texture_test   (in assets/: the models are ../tests/gltf/)
#include "gltf_anim_texture.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <string>

using namespace toms::gltf;

static int g_iChecks = 0;
static int g_iFailed = 0;
#define CHECK(cond, ...) do { ++g_iChecks; if (!(cond)) { ++g_iFailed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static bool f_Load(const char* l_pszName, Model& l_oM) {
    std::string l_strErr;
    for (const char* l_pszDir : {"../tests/gltf/", "tests/gltf/"})
        if (f_LoadModel(std::string(l_pszDir) + l_pszName, l_oM, &l_strErr)) return true;
    std::printf("  could not load %s: %s\n", l_pszName, l_strErr.c_str());
    return false;
}

// Largest difference between two matrices' elements.
static float f_Diff(const glm::mat4& l_mat4A, const glm::mat4& l_mat4B) {
    float l_fD = 0.0f;
    for (int l_iC = 0; l_iC < 4; l_iC++)
        for (int l_iR = 0; l_iR < 4; l_iR++) l_fD = std::max(l_fD, std::fabs(l_mat4A[l_iC][l_iR] - l_mat4B[l_iC][l_iR]));
    return l_fD;
}

// The worst joint difference between the texture at clip / time and the real Pose there.
static float f_Worst(const Model& l_oM, const AnimTexture& l_oTex, int l_iClip, float l_fT) {
    Pose l_oPose;
    l_oPose.f_Bind(&l_oM);
    l_oPose.f_Apply(l_oTex.m_vClips[(size_t)l_iClip].m_iAnimation, l_fT);
    l_oPose.f_UpdateWorld();
    std::vector<glm::mat4> l_vReal;
    l_oPose.f_JointMatrices(l_oTex.m_iSkin, l_vReal);
    const FramePair l_oFp = f_FramePair(l_oTex, l_iClip, l_fT, false);
    float l_fD = 0.0f;
    for (int l_iJ = 0; l_iJ < l_oTex.m_iJoints; l_iJ++) l_fD = std::max(l_fD, f_Diff(f_SampleJoint(l_oTex, l_iJ, l_oFp), l_vReal[(size_t)l_iJ]));
    return l_fD;
}

int main() {
    Model l_oM;
    if (!f_Load("skin_tube.gltf", l_oM)) { std::printf("gltf_anim_texture_test: model missing\n"); return 1; }

    // ---- the bake ----
    AnimTexture l_oTex;
    std::string l_strErr;
    CHECK(f_BakeAnimTexture(l_oM, 0, 30.0f, l_oTex, &l_strErr), "bakes: %s", l_strErr.c_str());
    CHECK(l_oTex.m_iJoints == 3 && l_oTex.m_iWidth == 6, "3 joints, 2 texels each (%d, %d)", l_oTex.m_iJoints, l_oTex.m_iWidth);
    CHECK(l_oTex.m_vClips.size() == 2 && l_oTex.f_ClipIndex("bend") == 0 && l_oTex.f_ClipIndex("wave") == 1 && l_oTex.f_ClipIndex("nope") == -1,
          "one clip per animation, found by name");
    const AnimClip& l_oBend = l_oTex.m_vClips[0];
    CHECK(l_oBend.m_iFrames == 61 && l_oBend.m_iFirstRow == 0, "bend: 2 s at 30 fps = 61 frames, both ends (%d)", l_oBend.m_iFrames);
    CHECK(l_oTex.m_vClips[1].m_iFirstRow == l_oBend.m_iFrames, "the next clip's rows follow");
    CHECK(l_oTex.m_iRows == l_oBend.m_iFrames + l_oTex.m_vClips[1].m_iFrames && l_oTex.m_vTexels.size() == (size_t)l_oTex.m_iRows * l_oTex.m_iWidth * 4, "the texel count");
    CHECK(l_oTex.m_fMaxScaleError < 1e-4f && !l_oTex.m_bMirroredJoints, "an ordinary rig: nothing averaged or mirrored");
    AnimTexture l_oBad;
    CHECK(!f_BakeAnimTexture(l_oM, 5, 30.0f, l_oBad, &l_strErr) && !l_strErr.empty(), "a skin that does not exist is an error");

    // ---- it matches the real animation ----
    for (int l_iClip = 0; l_iClip < 2; l_iClip++) {
        float l_fAtFrames = 0.0f;
        for (int l_iF = 0; l_iF < l_oTex.m_vClips[(size_t)l_iClip].m_iFrames; l_iF += 7) l_fAtFrames = std::max(l_fAtFrames, f_Worst(l_oM, l_oTex, l_iClip, l_iF / 30.0f));
        CHECK(l_fAtFrames < 1e-4f, "clip %d at its frame times: the joint matrices of Pose (worst %.6f)", l_iClip, l_fAtFrames);
        float l_fBetween = 0.0f;
        for (float l_fT = 0.013f; l_fT < l_oTex.m_vClips[(size_t)l_iClip].m_fDuration; l_fT += 0.11f) l_fBetween = std::max(l_fBetween, f_Worst(l_oM, l_oTex, l_iClip, l_fT));
        CHECK(l_fBetween < 0.01f, "clip %d between frames: within 0.01 of Pose (worst %.5f)", l_iClip, l_fBetween);
    }
    CHECK(f_Worst(l_oM, l_oTex, 0, l_oBend.m_fDuration) < 1e-4f, "the end pose is the last frame");

    // ---- frame pairs: looping wraps, a one-shot holds its last frame ----
    FramePair l_oP = f_FramePair(l_oTex, 0, 0.5f + 1.0f / 60.0f, false);
    CHECK(l_oP.m_fRow0 == 15 && l_oP.m_fRow1 == 16 && std::fabs(l_oP.m_fBlend - 0.5f) < 1e-3f, "halfway between frames 15 and 16 (%.0f %.0f %.3f)", l_oP.m_fRow0, l_oP.m_fRow1, l_oP.m_fBlend);
    l_oP = f_FramePair(l_oTex, 0, 2.5f, true);
    FramePair l_oQ = f_FramePair(l_oTex, 0, 0.5f, true);
    CHECK(l_oP.m_fRow0 == l_oQ.m_fRow0 && std::fabs(l_oP.m_fBlend - l_oQ.m_fBlend) < 1e-3f, "looping: 2.5 s is 0.5 s");
    l_oP = f_FramePair(l_oTex, 0, 9.0f, false);
    CHECK(l_oP.m_fRow0 == l_oBend.m_iFrames - 1 && l_oP.m_fRow1 == l_oBend.m_iFrames - 1, "one-shot: past the end holds the last frame");
    l_oP = f_FramePair(l_oTex, 1, 0.0f, false);
    CHECK(l_oP.m_fRow0 == l_oTex.m_vClips[1].m_iFirstRow, "the second clip starts at its own rows");

    // ---- blending two clips ----
    {
        const FramePair l_oA = f_FramePair(l_oTex, 0, 1.0f, false), l_oB = f_FramePair(l_oTex, 1, 1.0f, false);
        CHECK(f_Diff(f_SampleJoint(l_oTex, 2, l_oA, &l_oB, 0.0f), f_SampleJoint(l_oTex, 2, l_oA)) < 1e-6f, "weight 0 = clip A");
        CHECK(f_Diff(f_SampleJoint(l_oTex, 2, l_oA, &l_oB, 1.0f), f_SampleJoint(l_oTex, 2, l_oB)) < 1e-5f, "weight 1 = clip B");
        const glm::mat4 l_mat4Half = f_SampleJoint(l_oTex, 2, l_oA, &l_oB, 0.5f);
        const float l_fSx = glm::length(glm::vec3(l_mat4Half[0])), l_fSy = glm::length(glm::vec3(l_mat4Half[1]));
        CHECK(std::fabs(l_fSx - 1.0f) < 1e-4f && std::fabs(l_fSy - 1.0f) < 1e-4f,
              "a half blend keeps the joint's size (rotation is nlerped, not a shrunken matrix): %.5f %.5f", l_fSx, l_fSy);
    }

    // ---- the crowd ----
    auto l_spShared = std::make_shared<AnimTexture>(l_oTex);
    Crowd l_oCrowd(l_spShared);
    const int l_iI0 = l_oCrowd.f_Add(glm::mat4(1.0f), 0, 0.0f);
    const int l_iI1 = l_oCrowd.f_Add(glm::translate(glm::mat4(1.0f), glm::vec3(3, 0, 0)), 1, 0.25f);
    CHECK(l_oCrowd.size() == 2 && l_iI1 == 1, "two instances");
    l_oCrowd.f_Update(0.5f);
    CHECK(std::fabs(l_oCrowd[l_iI0].m_oA.m_fTime - 0.5f) < 1e-6f && std::fabs(l_oCrowd[l_iI1].m_oA.m_fTime - 0.75f) < 1e-6f, "update advances each clip");
    l_oCrowd.f_CrossFade(l_iI0, 1, 1.0f);
    l_oCrowd.f_Update(0.25f);
    CHECK(std::fabs(l_oCrowd[l_iI0].m_fWeight - 0.25f) < 1e-5f && l_oCrowd[l_iI0].m_oB.m_iClip == 1, "a quarter into a 1 s fade: weight 0.25");
    std::vector<CrowdGpuInstance> l_vGpu;
    l_oCrowd.f_GpuInstances(l_vGpu);
    CHECK(l_vGpu.size() == 2 && std::fabs(l_vGpu[0].m_v4B.w - 0.25f) < 1e-5f && l_vGpu[0].m_v4B.x >= l_oTex.m_vClips[1].m_iFirstRow,
          "the fading instance sends both clips and the weight");
    CHECK(std::fabs(l_vGpu[1].m_arrRows[0].w - 3.0f) < 1e-6f && l_vGpu[1].m_arrRows[2].z == 1.0f, "the placement goes as three rows (x = 3)");
    l_oCrowd.f_Update(1.0f);
    CHECK(l_oCrowd[l_iI0].m_oA.m_iClip == 1 && l_oCrowd[l_iI0].m_fWeight == 0.0f && l_oCrowd[l_iI0].m_fFadeSeconds == 0.0f, "after the fade it plays the new clip alone");
    l_oCrowd.f_SetBlend(l_iI1, 0, 1, 0.3f);
    l_oCrowd.f_Update(0.1f);
    CHECK(std::fabs(l_oCrowd[l_iI1].m_fWeight - 0.3f) < 1e-6f, "a held blend keeps its weight");
    l_oCrowd.f_Play(l_iI1, 0);
    CHECK(l_oCrowd[l_iI1].m_fWeight == 0.0f && l_oCrowd[l_iI1].m_oA.m_fTime == 0.0f, "play: one clip, from its start");
    l_oCrowd[l_iI1].m_bHidden = true;
    l_oCrowd.f_GpuInstances(l_vGpu);
    CHECK(l_vGpu.size() == 1, "a hidden instance is not drawn");
    l_oCrowd.f_Remove(0);
    CHECK(l_oCrowd.size() == 1, "remove");

    // ---- save / load ----
    const std::string l_strPath = "gltf_anim_texture_test.animtex";
    CHECK(f_SaveAnimTexture(l_oTex, l_strPath, &l_strErr), "saves: %s", l_strErr.c_str());
    AnimTexture l_oBack;
    CHECK(f_LoadAnimTexture(l_strPath, l_oBack, &l_oM, 0, &l_strErr), "loads, matching the model: %s", l_strErr.c_str());
    CHECK(l_oBack.m_iRows == l_oTex.m_iRows && l_oBack.m_vClips.size() == 2 && l_oBack.m_vClips[1].m_strName == "wave" && l_oBack.m_vTexels == l_oTex.m_vTexels,
          "the same texture comes back");
    Model l_oChanged = l_oM;
    l_oChanged.m_vAnimations[0].m_fDuration += 1.0f;
    CHECK(!f_LoadAnimTexture(l_strPath, l_oBack, &l_oChanged, 0, &l_strErr) && l_strErr.find("stale") != std::string::npos,
          "a model that changed since the bake: stale (%s)", l_strErr.c_str());
    CHECK(!f_LoadAnimTexture("does_not_exist.animtex", l_oBack, nullptr, -1, &l_strErr), "a missing file is an error");
    std::remove(l_strPath.c_str());

    std::printf("gltf_anim_texture_test: %d checks, %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed == 0 ? 0 : 1;
}
