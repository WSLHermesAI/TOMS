// Tests for gltf_environment.h: a uniform picture stays uniform at every blur level and its diffuse light is the
// same value; which way the picture faces (left half / right half, top / bottom); the cube face convention; the
// shipped HDRI (assets/media/env) loads and prefilters.
// Run: ./toms_tests gltf_environment_test   (in assets/: the HDRI is media/env/)
#include "gltf_environment.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace toms::gltf;

static int g_iChecks = 0;
static int g_iFailed = 0;
#define CHECK(cond, ...) do { ++g_iChecks; if (!(cond)) { ++g_iFailed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {

bool f_Near3(const glm::vec3& l_v3A, const glm::vec3& l_v3B, float l_fEps) { return glm::all(glm::lessThan(glm::abs(l_v3A - l_v3B), glm::vec3(l_fEps))); }

// The colour at the centre of a face of a mip.
glm::vec3 f_Centre(const Environment& l_oEnv, int l_iMip, int l_iFace) {
    const int l_iS = l_oEnv.m_iFaceSize >> l_iMip;
    const std::vector<float>& l_vPx = l_oEnv.m_vFaces[(size_t)(l_iMip * 6 + l_iFace)];
    const size_t l_nI = ((size_t)(l_iS / 2) * l_iS + (size_t)(l_iS / 2)) * 3;
    return glm::vec3(l_vPx[l_nI], l_vPx[l_nI + 1], l_vPx[l_nI + 2]);
}

Equirect f_Picture(int l_iW, int l_iH, glm::vec3 (*l_fnColour)(float l_fU, float l_fV)) {
    Equirect l_oP;
    l_oP.m_iWidth = l_iW;
    l_oP.m_iHeight = l_iH;
    l_oP.m_vRgb.resize((size_t)l_iW * l_iH * 3);
    for (int l_iY = 0; l_iY < l_iH; l_iY++)
        for (int l_iX = 0; l_iX < l_iW; l_iX++) {
            const glm::vec3 l_v3C = l_fnColour((l_iX + 0.5f) / l_iW, (l_iY + 0.5f) / l_iH);
            float* l_pO = &l_oP.m_vRgb[((size_t)l_iY * l_iW + l_iX) * 3];
            l_pO[0] = l_v3C.r;
            l_pO[1] = l_v3C.g;
            l_pO[2] = l_v3C.b;
        }
    return l_oP;
}

}  // namespace

static void f_TestUniform() {
    const Environment l_oEnv = f_PrefilterEnvironment(f_Picture(64, 32, [](float, float) { return glm::vec3(0.5f, 1.0f, 2.0f); }), 16, 16);
    CHECK(l_oEnv.f_Valid() && l_oEnv.m_iFaceSize == 16 && l_oEnv.f_Mips() == 5, "16 px faces: mips 16, 8, 4, 2, 1 (%d)", l_oEnv.f_Mips());
    bool l_bFlat = true;
    for (int l_iM = 0; l_iM < l_oEnv.f_Mips(); l_iM++)
        for (int l_iF = 0; l_iF < 6; l_iF++) l_bFlat = l_bFlat && f_Near3(f_Centre(l_oEnv, l_iM, l_iF), glm::vec3(0.5f, 1.0f, 2.0f), 1e-3f);
    CHECK(l_bFlat, "a uniform picture is the same at every blur level and on every face");
    bool l_bDiffuse = true;
    for (int l_iF = 0; l_iF < 6; l_iF++) l_bDiffuse = l_bDiffuse && f_Near3(f_EvalSH(l_oEnv.m_arrSh, f_DirectionOf(l_iF, 0, 0)), glm::vec3(0.5f, 1.0f, 2.0f), 0.02f);
    CHECK(l_bDiffuse, "its diffuse light is the same value from every side (%.3f %.3f %.3f)", f_EvalSH(l_oEnv.m_arrSh, glm::vec3(0, 1, 0)).x,
          f_EvalSH(l_oEnv.m_arrSh, glm::vec3(0, 1, 0)).y, f_EvalSH(l_oEnv.m_arrSh, glm::vec3(0, 1, 0)).z);
}

static void f_TestOrientation() {
    // Left half of the picture red, right half blue; the top half bright.
    const Environment l_oEnv = f_PrefilterEnvironment(f_Picture(64, 32, [](float l_fU, float l_fV) {
        return (l_fU < 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1)) * (l_fV < 0.5f ? 2.0f : 1.0f);
    }), 16, 16);
    CHECK(f_Centre(l_oEnv, 0, 0).r > 0.9f && f_Centre(l_oEnv, 0, 0).b < 0.1f, "+X shows the picture's left half (u 0.25)");
    CHECK(f_Centre(l_oEnv, 0, 1).b > 0.9f && f_Centre(l_oEnv, 0, 1).r < 0.1f, "-X shows its right half (u 0.75)");
    CHECK(f_EvalSH(l_oEnv.m_arrSh, glm::vec3(0, 1, 0)).r > f_EvalSH(l_oEnv.m_arrSh, glm::vec3(0, -1, 0)).r * 1.3f, "more light from above (the top half)");
    CHECK(f_EvalSH(l_oEnv.m_arrSh, glm::vec3(1, 0, 0)).r > f_EvalSH(l_oEnv.m_arrSh, glm::vec3(-1, 0, 0)).r, "red light from +X, the picture's left");
}

static void f_TestFaces() {
    CHECK(f_Near3(f_DirectionOf(0, 0, 0), {1, 0, 0}, 1e-5f) && f_Near3(f_DirectionOf(3, 0, 0), {0, -1, 0}, 1e-5f) &&
              f_Near3(f_DirectionOf(5, 0, 0), {0, 0, -1}, 1e-5f),
          "face centres: +X, -Y, -Z");
    CHECK(f_DirectionOf(0, 0, -1).y > 0.5f && f_DirectionOf(4, 0, -1).y > 0.5f, "a side face's top row looks up");
}

static void f_TestShipped() {
    Equirect l_oP;
    std::string l_strErr;
    bool l_bOk = false;
    for (const char* l_pszPath : {"media/env/artist_workshop_1k.hdr", "assets/media/env/artist_workshop_1k.hdr",
                             "../assets/media/env/artist_workshop_1k.hdr"})
        if ((l_bOk = f_LoadEquirect(l_pszPath, l_oP, &l_strErr))) break;
    CHECK(l_bOk && l_oP.m_iWidth == 1024 && l_oP.m_iHeight == 512, "the shipped HDRI loads, 1024 x 512 (%s)", l_strErr.c_str());
    if (!l_bOk) return;
    const Environment l_oEnv = f_PrefilterEnvironment(l_oP, 32, 16);
    const glm::vec3 l_v3Up = f_EvalSH(l_oEnv.m_arrSh, glm::vec3(0, 1, 0));
    CHECK(l_oEnv.f_Valid() && l_v3Up.r > 0.0f && std::isfinite(l_v3Up.r), "it prefilters, with light in it");
}

int main() {
    f_TestUniform();
    f_TestOrientation();
    f_TestFaces();
    f_TestShipped();
    std::printf("gltf_environment_test: %d checks, %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed ? 1 : 0;
}
