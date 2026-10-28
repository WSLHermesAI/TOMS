// Tests for gltf_environment.h: a uniform picture stays uniform at every blur level and its diffuse light is the
// same value; which way the picture faces (left half / right half, top / bottom); the cube face convention; the
// shipped HDRI (assets/media/env) loads and prefilters.
// Run: ./toms_tests gltf_environment_test   (in assets/: the HDRI is media/env/)
#include "gltf_environment.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace toms::gltf;

static int g_checks = 0;
static int g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {

bool near3(const glm::vec3& a, const glm::vec3& b, float eps) { return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(eps))); }

// The colour at the centre of a face of a mip.
glm::vec3 centre(const Environment& env, int mip, int face) {
    const int s = env.faceSize >> mip;
    const std::vector<float>& px = env.faces[(size_t)(mip * 6 + face)];
    const size_t i = ((size_t)(s / 2) * s + (size_t)(s / 2)) * 3;
    return glm::vec3(px[i], px[i + 1], px[i + 2]);
}

Equirect picture(int w, int h, glm::vec3 (*colour)(float u, float v)) {
    Equirect p;
    p.width = w;
    p.height = h;
    p.rgb.resize((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const glm::vec3 c = colour((x + 0.5f) / w, (y + 0.5f) / h);
            float* o = &p.rgb[((size_t)y * w + x) * 3];
            o[0] = c.r;
            o[1] = c.g;
            o[2] = c.b;
        }
    return p;
}

}  // namespace

static void testUniform() {
    const Environment env = prefilterEnvironment(picture(64, 32, [](float, float) { return glm::vec3(0.5f, 1.0f, 2.0f); }), 16, 16);
    CHECK(env.valid() && env.faceSize == 16 && env.mips() == 5, "16 px faces: mips 16, 8, 4, 2, 1 (%d)", env.mips());
    bool flat = true;
    for (int m = 0; m < env.mips(); m++)
        for (int f = 0; f < 6; f++) flat = flat && near3(centre(env, m, f), glm::vec3(0.5f, 1.0f, 2.0f), 1e-3f);
    CHECK(flat, "a uniform picture is the same at every blur level and on every face");
    bool diffuse = true;
    for (int f = 0; f < 6; f++) diffuse = diffuse && near3(evalSH(env.sh, directionOf(f, 0, 0)), glm::vec3(0.5f, 1.0f, 2.0f), 0.02f);
    CHECK(diffuse, "its diffuse light is the same value from every side (%.3f %.3f %.3f)", evalSH(env.sh, glm::vec3(0, 1, 0)).x,
          evalSH(env.sh, glm::vec3(0, 1, 0)).y, evalSH(env.sh, glm::vec3(0, 1, 0)).z);
}

static void testOrientation() {
    // Left half of the picture red, right half blue; the top half bright.
    const Environment env = prefilterEnvironment(picture(64, 32, [](float u, float v) {
        return (u < 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1)) * (v < 0.5f ? 2.0f : 1.0f);
    }), 16, 16);
    CHECK(centre(env, 0, 0).r > 0.9f && centre(env, 0, 0).b < 0.1f, "+X shows the picture's left half (u 0.25)");
    CHECK(centre(env, 0, 1).b > 0.9f && centre(env, 0, 1).r < 0.1f, "-X shows its right half (u 0.75)");
    CHECK(evalSH(env.sh, glm::vec3(0, 1, 0)).r > evalSH(env.sh, glm::vec3(0, -1, 0)).r * 1.3f, "more light from above (the top half)");
    CHECK(evalSH(env.sh, glm::vec3(1, 0, 0)).r > evalSH(env.sh, glm::vec3(-1, 0, 0)).r, "red light from +X, the picture's left");
}

static void testFaces() {
    CHECK(near3(directionOf(0, 0, 0), {1, 0, 0}, 1e-5f) && near3(directionOf(3, 0, 0), {0, -1, 0}, 1e-5f) &&
              near3(directionOf(5, 0, 0), {0, 0, -1}, 1e-5f),
          "face centres: +X, -Y, -Z");
    CHECK(directionOf(0, 0, -1).y > 0.5f && directionOf(4, 0, -1).y > 0.5f, "a side face's top row looks up");
}

static void testShipped() {
    Equirect p;
    std::string err;
    bool ok = false;
    for (const char* path : {"media/env/artist_workshop_1k.hdr", "assets/media/env/artist_workshop_1k.hdr",
                             "../assets/media/env/artist_workshop_1k.hdr"})
        if ((ok = loadEquirect(path, p, &err))) break;
    CHECK(ok && p.width == 1024 && p.height == 512, "the shipped HDRI loads, 1024 x 512 (%s)", err.c_str());
    if (!ok) return;
    const Environment env = prefilterEnvironment(p, 32, 16);
    const glm::vec3 up = evalSH(env.sh, glm::vec3(0, 1, 0));
    CHECK(env.valid() && up.r > 0.0f && std::isfinite(up.r), "it prefilters, with light in it");
}

int main() {
    testUniform();
    testOrientation();
    testFaces();
    testShipped();
    std::printf("gltf_environment_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
