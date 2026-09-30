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

constexpr int kChannelTolerance = 40;

struct Image { int w = 0, h = 0; std::vector<unsigned char> px; };   // RGBA8

bool load(const char* path, Image& img) {
    int n = 0;
    unsigned char* d = stbi_load(path, &img.w, &img.h, &n, 4);
    if (!d) return false;
    img.px.assign(d, d + (size_t)img.w * img.h * 4);
    stbi_image_free(d);
    return true;
}

Image resample(const Image& src, int w, int h) {   // bilinear
    Image out; out.w = w; out.h = h; out.px.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        const float sy = (y + 0.5f) * src.h / h - 0.5f;
        const int y0 = std::max(0, (int)std::floor(sy)), y1 = std::min(src.h - 1, y0 + 1);
        const float fy = std::min(1.0f, std::max(0.0f, sy - y0));
        for (int x = 0; x < w; ++x) {
            const float sx = (x + 0.5f) * src.w / w - 0.5f;
            const int x0 = std::max(0, (int)std::floor(sx)), x1 = std::min(src.w - 1, x0 + 1);
            const float fx = std::min(1.0f, std::max(0.0f, sx - x0));
            for (int c = 0; c < 4; ++c) {
                auto at = [&](int xx, int yy) { return (float)src.px[((size_t)yy * src.w + xx) * 4 + c]; };
                const float v = (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
                out.px[((size_t)y * w + x) * 4 + c] = (unsigned char)std::lround(v);
            }
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: image_diff actual.png golden.png [max_bad_percent] [diff.png]\n"); return 2; }
    const double maxBad = argc > 3 ? std::atof(argv[3]) : 1.0;
    Image actual, golden;
    if (!load(argv[1], actual)) { std::fprintf(stderr, "image_diff: cannot read %s\n", argv[1]); return 2; }
    if (!load(argv[2], golden)) { std::fprintf(stderr, "image_diff: cannot read %s\n", argv[2]); return 2; }
    if (actual.w != golden.w || actual.h != golden.h) {
        const double a = (double)actual.w / actual.h, g = (double)golden.w / golden.h;
        if (std::fabs(a - g) > 0.01) {
            std::fprintf(stderr, "image_diff: size %dx%d does not match the reference %dx%d\n", actual.w, actual.h, golden.w, golden.h);
            return 1;
        }
        std::printf("image_diff: resampling %dx%d to the reference size %dx%d\n", actual.w, actual.h, golden.w, golden.h);
        actual = resample(actual, golden.w, golden.h);
    }
    size_t bad = 0;
    std::vector<unsigned char> diff(golden.px.size());
    for (size_t i = 0; i < golden.px.size(); i += 4) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs((int)actual.px[i + c] - (int)golden.px[i + c]));
        const bool isBad = worst > kChannelTolerance;
        bad += isBad;
        for (int c = 0; c < 3; ++c) diff[i + c] = (unsigned char)(golden.px[i + c] / 4);
        if (isBad) { diff[i] = 255; diff[i + 1] = 0; diff[i + 2] = 0; }
        diff[i + 3] = 255;
    }
    const double pct = 100.0 * bad / ((double)golden.w * golden.h);
    const bool pass = pct <= maxBad;
    std::printf("image_diff: %.3f%% of pixels differ (limit %.3f%%) -> %s\n", pct, maxBad, pass ? "PASS" : "FAIL");
    if (!pass && argc > 4 && stbi_write_png(argv[4], golden.w, golden.h, 4, diff.data(), golden.w * 4))
        std::printf("image_diff: bad pixels marked in %s\n", argv[4]);
    return pass ? 0 : 1;
}
