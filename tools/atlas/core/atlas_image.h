// atlas_image.h -- RGBA8 images for the atlas tool: load/save (stb), alpha trim, blit, extrude.
// Pixels are straight (not premultiplied) RGBA, rows top to bottom.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace atlas {

struct IRect {
    int x = 0, y = 0, w = 0, h = 0;
    int right() const { return x + w; }    // exclusive
    int bottom() const { return y + h; }   // exclusive
    bool empty() const { return w <= 0 || h <= 0; }
    bool contains(const IRect& r) const {
        return r.x >= x && r.y >= y && r.right() <= right() && r.bottom() <= bottom();
    }
    bool intersects(const IRect& r) const {
        return r.x < right() && x < r.right() && r.y < bottom() && y < r.bottom();
    }
    bool operator==(const IRect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const IRect& o) const { return !(*this == o); }
};

IRect unite(const IRect& a, const IRect& b);   // bounding box; an empty side is ignored

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // w*h*4

    Image() = default;
    Image(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
    bool valid() const { return w > 0 && h > 0 && px.size() == (size_t)w * h * 4; }
    uint8_t* at(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
};

bool loadImage(const std::string& path, Image& out, std::string* err = nullptr);
bool loadImageFromMemory(const void* data, size_t size, Image& out, std::string* err = nullptr);
bool savePng(const std::string& path, const Image& img, std::string* err = nullptr);
std::vector<uint8_t> encodePng(const Image& img);

// Smallest rect holding every pixel with alpha > threshold. Empty rect for a fully transparent image.
IRect opaqueBounds(const Image& img, int alphaThreshold = 0);

Image crop(const Image& src, const IRect& r);                     // r may reach outside src (filled transparent)
void blit(Image& dst, const Image& src, const IRect& srcRect, int dx, int dy);
// Repeats the edge pixels of `r` (already in dst) outward by n pixels on every side.
void extrude(Image& dst, const IRect& r, int n);
void premultiply(Image& img);
void unpremultiply(Image& img);   // inverse of premultiply (lossy where alpha is small)
// Nearest-neighbour scale, used for variants whose sprites differ in size.
Image scaleNearest(const Image& src, int w, int h);
uint64_t hashPixels(const Image& img, const IRect& r);           // FNV-1a over size + pixels
bool samePixels(const Image& a, const IRect& ra, const Image& b, const IRect& rb);

}  // namespace atlas
