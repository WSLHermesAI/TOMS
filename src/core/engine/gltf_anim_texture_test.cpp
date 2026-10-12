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

static int g_checks = 0;
static int g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static bool load(const char* name, Model& m) {
    std::string err;
    for (const char* dir : {"../tests/gltf/", "tests/gltf/"})
        if (loadModel(std::string(dir) + name, m, &err)) return true;
    std::printf("  could not load %s: %s\n", name, err.c_str());
    return false;
}

// Largest difference between two matrices' elements.
static float diff(const glm::mat4& a, const glm::mat4& b) {
    float d = 0.0f;
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) d = std::max(d, std::fabs(a[c][r] - b[c][r]));
    return d;
}

// The worst joint difference between the texture at clip / time and the real Pose there.
static float worst(const Model& m, const AnimTexture& tex, int clip, float t) {
    Pose pose;
    pose.bind(&m);
    pose.apply(tex.clips[(size_t)clip].animation, t);
    pose.updateWorld();
    std::vector<glm::mat4> real;
    pose.jointMatrices(tex.skin, real);
    const FramePair fp = framePair(tex, clip, t, false);
    float d = 0.0f;
    for (int j = 0; j < tex.joints; j++) d = std::max(d, diff(sampleJoint(tex, j, fp), real[(size_t)j]));
    return d;
}

int main() {
    Model m;
    if (!load("skin_tube.gltf", m)) { std::printf("gltf_anim_texture_test: model missing\n"); return 1; }

    // ---- the bake ----
    AnimTexture tex;
    std::string err;
    CHECK(bakeAnimTexture(m, 0, 30.0f, tex, &err), "bakes: %s", err.c_str());
    CHECK(tex.joints == 3 && tex.width == 6, "3 joints, 2 texels each (%d, %d)", tex.joints, tex.width);
    CHECK(tex.clips.size() == 2 && tex.clipIndex("bend") == 0 && tex.clipIndex("wave") == 1 && tex.clipIndex("nope") == -1,
          "one clip per animation, found by name");
    const AnimClip& bend = tex.clips[0];
    CHECK(bend.frames == 61 && bend.firstRow == 0, "bend: 2 s at 30 fps = 61 frames, both ends (%d)", bend.frames);
    CHECK(tex.clips[1].firstRow == bend.frames, "the next clip's rows follow");
    CHECK(tex.rows == bend.frames + tex.clips[1].frames && tex.texels.size() == (size_t)tex.rows * tex.width * 4, "the texel count");
    CHECK(tex.maxScaleError < 1e-4f && !tex.mirroredJoints, "an ordinary rig: nothing averaged or mirrored");
    AnimTexture bad;
    CHECK(!bakeAnimTexture(m, 5, 30.0f, bad, &err) && !err.empty(), "a skin that does not exist is an error");

    // ---- it matches the real animation ----
    for (int clip = 0; clip < 2; clip++) {
        float atFrames = 0.0f;
        for (int f = 0; f < tex.clips[(size_t)clip].frames; f += 7) atFrames = std::max(atFrames, worst(m, tex, clip, f / 30.0f));
        CHECK(atFrames < 1e-4f, "clip %d at its frame times: the joint matrices of Pose (worst %.6f)", clip, atFrames);
        float between = 0.0f;
        for (float t = 0.013f; t < tex.clips[(size_t)clip].duration; t += 0.11f) between = std::max(between, worst(m, tex, clip, t));
        CHECK(between < 0.01f, "clip %d between frames: within 0.01 of Pose (worst %.5f)", clip, between);
    }
    CHECK(worst(m, tex, 0, bend.duration) < 1e-4f, "the end pose is the last frame");

    // ---- frame pairs: looping wraps, a one-shot holds its last frame ----
    FramePair p = framePair(tex, 0, 0.5f + 1.0f / 60.0f, false);
    CHECK(p.row0 == 15 && p.row1 == 16 && std::fabs(p.blend - 0.5f) < 1e-3f, "halfway between frames 15 and 16 (%.0f %.0f %.3f)", p.row0, p.row1, p.blend);
    p = framePair(tex, 0, 2.5f, true);
    FramePair q = framePair(tex, 0, 0.5f, true);
    CHECK(p.row0 == q.row0 && std::fabs(p.blend - q.blend) < 1e-3f, "looping: 2.5 s is 0.5 s");
    p = framePair(tex, 0, 9.0f, false);
    CHECK(p.row0 == bend.frames - 1 && p.row1 == bend.frames - 1, "one-shot: past the end holds the last frame");
    p = framePair(tex, 1, 0.0f, false);
    CHECK(p.row0 == tex.clips[1].firstRow, "the second clip starts at its own rows");

    // ---- blending two clips ----
    {
        const FramePair a = framePair(tex, 0, 1.0f, false), b = framePair(tex, 1, 1.0f, false);
        CHECK(diff(sampleJoint(tex, 2, a, &b, 0.0f), sampleJoint(tex, 2, a)) < 1e-6f, "weight 0 = clip A");
        CHECK(diff(sampleJoint(tex, 2, a, &b, 1.0f), sampleJoint(tex, 2, b)) < 1e-5f, "weight 1 = clip B");
        const glm::mat4 half = sampleJoint(tex, 2, a, &b, 0.5f);
        const float sx = glm::length(glm::vec3(half[0])), sy = glm::length(glm::vec3(half[1]));
        CHECK(std::fabs(sx - 1.0f) < 1e-4f && std::fabs(sy - 1.0f) < 1e-4f,
              "a half blend keeps the joint's size (rotation is nlerped, not a shrunken matrix): %.5f %.5f", sx, sy);
    }

    // ---- the crowd ----
    auto shared = std::make_shared<AnimTexture>(tex);
    Crowd crowd(shared);
    const int i0 = crowd.add(glm::mat4(1.0f), 0, 0.0f);
    const int i1 = crowd.add(glm::translate(glm::mat4(1.0f), glm::vec3(3, 0, 0)), 1, 0.25f);
    CHECK(crowd.size() == 2 && i1 == 1, "two instances");
    crowd.update(0.5f);
    CHECK(std::fabs(crowd[i0].a.time - 0.5f) < 1e-6f && std::fabs(crowd[i1].a.time - 0.75f) < 1e-6f, "update advances each clip");
    crowd.crossFade(i0, 1, 1.0f);
    crowd.update(0.25f);
    CHECK(std::fabs(crowd[i0].weight - 0.25f) < 1e-5f && crowd[i0].b.clip == 1, "a quarter into a 1 s fade: weight 0.25");
    std::vector<CrowdGpuInstance> gpu;
    crowd.gpuInstances(gpu);
    CHECK(gpu.size() == 2 && std::fabs(gpu[0].b.w - 0.25f) < 1e-5f && gpu[0].b.x >= tex.clips[1].firstRow,
          "the fading instance sends both clips and the weight");
    CHECK(std::fabs(gpu[1].rows[0].w - 3.0f) < 1e-6f && gpu[1].rows[2].z == 1.0f, "the placement goes as three rows (x = 3)");
    crowd.update(1.0f);
    CHECK(crowd[i0].a.clip == 1 && crowd[i0].weight == 0.0f && crowd[i0].fadeSeconds == 0.0f, "after the fade it plays the new clip alone");
    crowd.setBlend(i1, 0, 1, 0.3f);
    crowd.update(0.1f);
    CHECK(std::fabs(crowd[i1].weight - 0.3f) < 1e-6f, "a held blend keeps its weight");
    crowd.play(i1, 0);
    CHECK(crowd[i1].weight == 0.0f && crowd[i1].a.time == 0.0f, "play: one clip, from its start");
    crowd[i1].hidden = true;
    crowd.gpuInstances(gpu);
    CHECK(gpu.size() == 1, "a hidden instance is not drawn");
    crowd.remove(0);
    CHECK(crowd.size() == 1, "remove");

    // ---- save / load ----
    const std::string path = "gltf_anim_texture_test.animtex";
    CHECK(saveAnimTexture(tex, path, &err), "saves: %s", err.c_str());
    AnimTexture back;
    CHECK(loadAnimTexture(path, back, &m, 0, &err), "loads, matching the model: %s", err.c_str());
    CHECK(back.rows == tex.rows && back.clips.size() == 2 && back.clips[1].name == "wave" && back.texels == tex.texels,
          "the same texture comes back");
    Model changed = m;
    changed.animations[0].duration += 1.0f;
    CHECK(!loadAnimTexture(path, back, &changed, 0, &err) && err.find("stale") != std::string::npos,
          "a model that changed since the bake: stale (%s)", err.c_str());
    CHECK(!loadAnimTexture("does_not_exist.animtex", back, nullptr, -1, &err), "a missing file is an error");
    std::remove(path.c_str());

    std::printf("gltf_anim_texture_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
