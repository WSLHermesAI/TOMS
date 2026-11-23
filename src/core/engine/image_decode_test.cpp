// image_decode_test.cpp -- image_decode.h and vfs.h's WebP twin (the web build packs the atlas pages as WebP,
// tools/web/pack_webp.py): a WebP is told by its bytes and decoded exactly (lossless here), a PNG still decodes, and
// "x.png" missing is read as "x.webp". Runs in assets/ (tests/CMakeLists.txt).
#include "image_decode.h"
#include "vfs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_iFail; } } while (0)

// 4 x 2 pixels, lossless WebP (Pillow, exact=True): red, green, blue, clear white / half grey-blue, orange, black, faint yellow.
const unsigned char g_arrWebp[] = {
    0x52,0x49,0x46,0x46,0x4c,0x00,0x00,0x00,0x57,0x45,0x42,0x50,0x56,0x50,0x38,0x4c,0x3f,0x00,0x00,0x00,0x2f,0x03,0x40,0x00,
    0x10,0x3f,0x40,0x90,0x6d,0xb3,0x41,0xdc,0xeb,0xfe,0x32,0xd3,0x20,0x93,0xb6,0xc9,0x5f,0xff,0x76,0x66,0x6d,0x64,0xd2,0x36,
    0xad,0x87,0xdd,0xf3,0xef,0xb2,0x04,0xd9,0x36,0x9b,0xd9,0xcc,0x8e,0xfe,0x00,0x47,0x44,0x3c,0x24,0x61,0xeb,0x7d,0x1b,0x04,
    0x02,0x64,0x2c,0x46,0x89,0x03,0x35,0x22,0xfa,0x1f,0x78,0x00};
const unsigned char g_arrPixels[8][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 0},
                                         {10, 20, 30, 128}, {200, 100, 50, 255}, {0, 0, 0, 255}, {255, 255, 0, 64}};
}  // namespace

int main() {
    namespace fs = std::filesystem;
    CHECK(toms::f_IsWebp(g_arrWebp, sizeof(g_arrWebp)), "the WebP is seen as WebP");
    int l_iW = 0, l_iH = 0;
    unsigned char* l_pPx = toms::f_DecodeImageRgba(g_arrWebp, sizeof(g_arrWebp), &l_iW, &l_iH);
    CHECK(l_pPx && l_iW == 4 && l_iH == 2, "the WebP decodes to 4 x 2");
    if (l_pPx && l_iW == 4 && l_iH == 2) {
        bool l_bSame = true;
        for (int l_iI = 0; l_iI < 8; ++l_iI)
            for (int l_iC = 0; l_iC < 4; ++l_iC) l_bSame = l_bSame && l_pPx[l_iI * 4 + l_iC] == g_arrPixels[l_iI][l_iC];
        CHECK(l_bSame, "the WebP's pixels, alpha too, come out exactly");
    }
    toms::f_ImageFree(l_pPx);
    CHECK(!toms::f_DecodeImageRgba(g_arrWebp, 20, &l_iW, &l_iH), "a cut WebP fails");
    CHECK(toms::f_ImageError()[0] != '\0', "and says why");

    std::string l_strPng;
    CHECK(toms::f_VfsReadAll("media/atlas/fx.png", l_strPng), "reads the shipped fx.png");
    CHECK(!toms::f_IsWebp(l_strPng.data(), l_strPng.size()), "a PNG is not WebP");
    l_pPx = toms::f_DecodeImageRgba(l_strPng.data(), l_strPng.size(), &l_iW, &l_iH);
    CHECK(l_pPx && l_iW > 0 && l_iH > 0, "a PNG still decodes");
    toms::f_ImageFree(l_pPx);

    // The web package: only "page.webp" is there, the game asks for "page.png".
    const fs::path l_oDir = fs::temp_directory_path() / "toms_image_decode_test";
    std::error_code l_oEc;
    fs::remove_all(l_oDir, l_oEc);
    fs::create_directories(l_oDir, l_oEc);
    {
        std::ofstream l_oF(l_oDir / "page.webp", std::ios::binary);
        l_oF.write((const char*)g_arrWebp, sizeof(g_arrWebp));
    }
    const std::string l_strAsked = toms::f_VfsUtf8(l_oDir / "page.png");
    CHECK(toms::f_VfsWebpTwin(l_strAsked) == toms::f_VfsUtf8(l_oDir / "page.webp"), "page.png's twin is page.webp");
    CHECK(toms::f_VfsWebpTwin("a/b.json").empty(), "only a .png has a twin");
    CHECK(toms::f_VfsExists(l_strAsked), "page.png exists through its twin");
    std::string l_strGot;
    CHECK(toms::f_VfsReadAll(l_strAsked, l_strGot) && l_strGot.size() == sizeof(g_arrWebp), "page.png reads page.webp");
    CHECK(!toms::f_VfsReadAllExact(l_strAsked, l_strGot), "the exact read does not");
    CHECK(!toms::f_VfsExists(toms::f_VfsUtf8(l_oDir / "none.png")), "a .png with no twin is missing");
    fs::remove_all(l_oDir, l_oEc);

    std::printf("image_decode_test: %s\n", g_iFail ? "FAILED" : "ok");
    return g_iFail ? 1 : 0;
}
