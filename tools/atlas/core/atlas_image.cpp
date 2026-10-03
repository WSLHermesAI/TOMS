// atlas_image.cpp -- see atlas_image.h. This translation unit owns the stb implementations for the
// atlas tool (the game has its own copies in other binaries).
#include "atlas_image.h"
#include "atlas_project.h"   // u8path

#include <algorithm>
#include <cstring>
#include <fstream>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace atlas {

IRect unite(const IRect& a, const IRect& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const int x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.right(), b.right()), y1 = std::max(a.bottom(), b.bottom());
    return {x0, y0, x1 - x0, y1 - y0};
}

static bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return false;
    out.resize((size_t)n);
    f.seekg(0);
    if (n > 0) f.read((char*)out.data(), n);
    return (bool)f;
}

bool loadImageFromMemory(const void* data, size_t size, Image& out, std::string* err) {
    int w = 0, h = 0, ch = 0;
    stbi_uc* d = stbi_load_from_memory((const stbi_uc*)data, (int)size, &w, &h, &ch, 4);
    if (!d) {
        if (err) *err = stbi_failure_reason() ? stbi_failure_reason() : "decode failed";
        return false;
    }
    out.w = w; out.h = h;
    out.px.assign(d, d + (size_t)w * h * 4);
    stbi_image_free(d);
    return true;
}

bool loadImage(const std::string& path, Image& out, std::string* err) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    if (!loadImageFromMemory(bytes.data(), bytes.size(), out, err)) {
        if (err) *err = path + ": " + *err;
        return false;
    }
    return true;
}

static void appendBytes(void* ctx, void* data, int size) {
    auto* v = (std::vector<uint8_t>*)ctx;
    v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + size);
}

std::vector<uint8_t> encodePng(const Image& img) {
    std::vector<uint8_t> out;
    if (img.valid()) stbi_write_png_to_func(appendBytes, &out, img.w, img.h, 4, img.px.data(), img.w * 4);
    return out;
}

bool savePng(const std::string& path, const Image& img, std::string* err) {
    const std::vector<uint8_t> bytes = encodePng(img);
    if (bytes.empty()) {
        if (err) *err = "PNG encode failed for " + path;
        return false;
    }
    std::ofstream f(u8path(path), std::ios::binary);
    if (!f || !f.write((const char*)bytes.data(), (std::streamsize)bytes.size())) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    return true;
}

IRect opaqueBounds(const Image& img, int alphaThreshold) {
    int x0 = img.w, y0 = img.h, x1 = -1, y1 = -1;
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++)
            if (img.at(x, y)[3] > alphaThreshold) {
                x0 = std::min(x0, x); x1 = std::max(x1, x);
                y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
    if (x1 < 0) return {};
    return {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

Image crop(const Image& src, const IRect& r) {
    Image out(std::max(r.w, 0), std::max(r.h, 0));
    blit(out, src, r, 0, 0);
    return out;
}

void blit(Image& dst, const Image& src, const IRect& srcRect, int dx, int dy) {
    for (int y = 0; y < srcRect.h; y++) {
        const int sy = srcRect.y + y, ty = dy + y;
        if (sy < 0 || sy >= src.h || ty < 0 || ty >= dst.h) continue;
        for (int x = 0; x < srcRect.w; x++) {
            const int sx = srcRect.x + x, tx = dx + x;
            if (sx < 0 || sx >= src.w || tx < 0 || tx >= dst.w) continue;
            std::memcpy(dst.at(tx, ty), src.at(sx, sy), 4);
        }
    }
}

void extrude(Image& dst, const IRect& r, int n) {
    if (n <= 0 || r.empty()) return;
    auto clampX = [&](int x) { return std::min(std::max(x, r.x), r.right() - 1); };
    auto clampY = [&](int y) { return std::min(std::max(y, r.y), r.bottom() - 1); };
    for (int y = r.y - n; y < r.bottom() + n; y++) {
        if (y < 0 || y >= dst.h) continue;
        for (int x = r.x - n; x < r.right() + n; x++) {
            if (x < 0 || x >= dst.w) continue;
            if (x >= r.x && x < r.right() && y >= r.y && y < r.bottom()) continue;
            std::memcpy(dst.at(x, y), dst.at(clampX(x), clampY(y)), 4);
        }
    }
}

void premultiply(Image& img) {
    for (size_t i = 0; i + 3 < img.px.size(); i += 4) {
        const unsigned a = img.px[i + 3];
        for (int c = 0; c < 3; c++) img.px[i + c] = (uint8_t)((img.px[i + c] * a + 127) / 255);
    }
}

void unpremultiply(Image& img) {
    for (size_t i = 0; i + 3 < img.px.size(); i += 4) {
        const unsigned a = img.px[i + 3];
        if (a == 0 || a == 255) continue;
        for (int c = 0; c < 3; c++) img.px[i + c] = (uint8_t)std::min(255u, (img.px[i + c] * 255 + a / 2) / a);
    }
}

Image scaleNearest(const Image& src, int w, int h) {
    if (w == src.w && h == src.h) return src;
    Image out(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            std::memcpy(out.at(x, y), src.at((int)((int64_t)x * src.w / w), (int)((int64_t)y * src.h / h)), 4);
    return out;
}

uint64_t hashPixels(const Image& img, const IRect& r) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint8_t b) { h ^= b; h *= 1099511628211ull; };
    for (int v : {r.w, r.h})
        for (int i = 0; i < 4; i++) mix((uint8_t)(v >> (i * 8)));
    for (int y = r.y; y < r.bottom(); y++)
        for (int x = r.x; x < r.right(); x++) {
            const uint8_t* p = img.at(x, y);
            for (int c = 0; c < 4; c++) mix(p[c]);
        }
    return h;
}

bool samePixels(const Image& a, const IRect& ra, const Image& b, const IRect& rb) {
    if (ra.w != rb.w || ra.h != rb.h) return false;
    for (int y = 0; y < ra.h; y++)
        if (std::memcmp(a.at(ra.x, ra.y + y), b.at(rb.x, rb.y + y), (size_t)ra.w * 4) != 0) return false;
    return true;
}

}  // namespace atlas
