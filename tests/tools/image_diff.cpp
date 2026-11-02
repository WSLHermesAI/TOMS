// image_diff -- compares a smoke-test screenshot with its reference image (tests/golden).
//
//   image_diff <actual.png> <golden.png> [max_bad_percent=1.0] [diff_out.png]
//
// A pixel is "bad" when any colour channel differs by more than kChannelTolerance (small driver /
// rasterisation differences are fine; a missing panel or wrong text is not). The test passes when
// at most max_bad_percent of the pixels are bad. When the sizes differ but the aspect ratio is the
// same (a HiDPI display scales the window), the screenshot is resampled to the reference size.
// On failure an optional diff image marks the bad pixels in red over a dimmed reference.
// Exit code: 0 pass, 1 fail, 2 bad input.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr int g_iChannelTolerance = 40;

struct Image { int m_iW = 0, m_iH = 0; std::vector<unsigned char> m_vPx; };   // RGBA8

bool f_Load(const char* l_pszPath, Image& l_oImg) {
    int l_iN = 0;
    unsigned char* l_pD = stbi_load(l_pszPath, &l_oImg.m_iW, &l_oImg.m_iH, &l_iN, 4);
    if (!l_pD) return false;
    l_oImg.m_vPx.assign(l_pD, l_pD + (size_t)l_oImg.m_iW * l_oImg.m_iH * 4);
    stbi_image_free(l_pD);
    return true;
}

Image f_Resample(const Image& l_oSrc, int l_iW, int l_iH) {   // bilinear
    Image l_oOut; l_oOut.m_iW = l_iW; l_oOut.m_iH = l_iH; l_oOut.m_vPx.resize((size_t)l_iW * l_iH * 4);
    for (int l_iY = 0; l_iY < l_iH; ++l_iY) {
        const float l_fSy = (l_iY + 0.5f) * l_oSrc.m_iH / l_iH - 0.5f;
        const int l_iY0 = std::max(0, (int)std::floor(l_fSy)), l_iY1 = std::min(l_oSrc.m_iH - 1, l_iY0 + 1);
        const float l_fFy = std::min(1.0f, std::max(0.0f, l_fSy - l_iY0));
        for (int l_iX = 0; l_iX < l_iW; ++l_iX) {
            const float l_fSx = (l_iX + 0.5f) * l_oSrc.m_iW / l_iW - 0.5f;
            const int l_iX0 = std::max(0, (int)std::floor(l_fSx)), l_iX1 = std::min(l_oSrc.m_iW - 1, l_iX0 + 1);
            const float l_fFx = std::min(1.0f, std::max(0.0f, l_fSx - l_iX0));
            for (int l_iC = 0; l_iC < 4; ++l_iC) {
                auto l_fnAt = [&](int l_iXx, int l_iYy) { return (float)l_oSrc.m_vPx[((size_t)l_iYy * l_oSrc.m_iW + l_iXx) * 4 + l_iC]; };
                const float l_fV = (l_fnAt(l_iX0, l_iY0) * (1 - l_fFx) + l_fnAt(l_iX1, l_iY0) * l_fFx) * (1 - l_fFy) + (l_fnAt(l_iX0, l_iY1) * (1 - l_fFx) + l_fnAt(l_iX1, l_iY1) * l_fFx) * l_fFy;
                l_oOut.m_vPx[((size_t)l_iY * l_iW + l_iX) * 4 + l_iC] = (unsigned char)std::lround(l_fV);
            }
        }
    }
    return l_oOut;
}

}  // namespace

int main(int l_iArgc, char** l_pArgv) {
    if (l_iArgc < 3) { std::fprintf(stderr, "usage: image_diff actual.png golden.png [max_bad_percent] [diff.png]\n"); return 2; }
    const double l_dMaxBad = l_iArgc > 3 ? std::atof(l_pArgv[3]) : 1.0;
    Image l_oActual, l_oGolden;
    if (!f_Load(l_pArgv[1], l_oActual)) { std::fprintf(stderr, "image_diff: cannot read %s\n", l_pArgv[1]); return 2; }
    if (!f_Load(l_pArgv[2], l_oGolden)) { std::fprintf(stderr, "image_diff: cannot read %s\n", l_pArgv[2]); return 2; }
    if (l_oActual.m_iW != l_oGolden.m_iW || l_oActual.m_iH != l_oGolden.m_iH) {
        const double l_dA = (double)l_oActual.m_iW / l_oActual.m_iH, l_dG = (double)l_oGolden.m_iW / l_oGolden.m_iH;
        if (std::fabs(l_dA - l_dG) > 0.01) {
            std::fprintf(stderr, "image_diff: size %dx%d does not match the reference %dx%d\n", l_oActual.m_iW, l_oActual.m_iH, l_oGolden.m_iW, l_oGolden.m_iH);
            return 1;
        }
        std::printf("image_diff: resampling %dx%d to the reference size %dx%d\n", l_oActual.m_iW, l_oActual.m_iH, l_oGolden.m_iW, l_oGolden.m_iH);
        l_oActual = f_Resample(l_oActual, l_oGolden.m_iW, l_oGolden.m_iH);
    }
    size_t l_nBad = 0;
    std::vector<unsigned char> l_vDiff(l_oGolden.m_vPx.size());
    for (size_t l_nI = 0; l_nI < l_oGolden.m_vPx.size(); l_nI += 4) {
        int l_iWorst = 0;
        for (int l_iC = 0; l_iC < 3; ++l_iC) l_iWorst = std::max(l_iWorst, std::abs((int)l_oActual.m_vPx[l_nI + l_iC] - (int)l_oGolden.m_vPx[l_nI + l_iC]));
        const bool l_bIsBad = l_iWorst > g_iChannelTolerance;
        l_nBad += l_bIsBad;
        for (int l_iC = 0; l_iC < 3; ++l_iC) l_vDiff[l_nI + l_iC] = (unsigned char)(l_oGolden.m_vPx[l_nI + l_iC] / 4);
        if (l_bIsBad) { l_vDiff[l_nI] = 255; l_vDiff[l_nI + 1] = 0; l_vDiff[l_nI + 2] = 0; }
        l_vDiff[l_nI + 3] = 255;
    }
    const double l_dPct = 100.0 * l_nBad / ((double)l_oGolden.m_iW * l_oGolden.m_iH);
    const bool l_bPass = l_dPct <= l_dMaxBad;
    std::printf("image_diff: %.3f%% of pixels differ (limit %.3f%%) -> %s\n", l_dPct, l_dMaxBad, l_bPass ? "PASS" : "FAIL");
    if (!l_bPass && l_iArgc > 4 && stbi_write_png(l_pArgv[4], l_oGolden.m_iW, l_oGolden.m_iH, 4, l_vDiff.data(), l_oGolden.m_iW * 4))
        std::printf("image_diff: bad pixels marked in %s\n", l_pArgv[4]);
    return l_bPass ? 0 : 1;
}
