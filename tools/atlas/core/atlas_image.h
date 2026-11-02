// atlas_image.h -- RGBA8 images for the atlas tool: load/save (stb), alpha trim, blit, extrude.
// Pixels are straight (not premultiplied) RGBA, rows top to bottom.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace atlas {

struct IRect {
    int m_iX = 0, m_iY = 0, m_iW = 0, m_iH = 0;
    int f_Right() const { return m_iX + m_iW; }    // exclusive
    int f_Bottom() const { return m_iY + m_iH; }   // exclusive
    bool f_Empty() const { return m_iW <= 0 || m_iH <= 0; }
    bool f_Contains(const IRect& l_oR) const {
        return l_oR.m_iX >= m_iX && l_oR.m_iY >= m_iY && l_oR.f_Right() <= f_Right() && l_oR.f_Bottom() <= f_Bottom();
    }
    bool f_Intersects(const IRect& l_oR) const {
        return l_oR.m_iX < f_Right() && m_iX < l_oR.f_Right() && l_oR.m_iY < f_Bottom() && m_iY < l_oR.f_Bottom();
    }
    bool operator==(const IRect& l_oO) const { return m_iX == l_oO.m_iX && m_iY == l_oO.m_iY && m_iW == l_oO.m_iW && m_iH == l_oO.m_iH; }
    bool operator!=(const IRect& l_oO) const { return !(*this == l_oO); }
};

IRect f_Unite(const IRect& l_oA, const IRect& l_oB);   // bounding box; an empty side is ignored

struct Image {
    int m_iW = 0, m_iH = 0;
    std::vector<uint8_t> m_vPx;   // w*h*4

    Image() = default;
    Image(int l_iW, int l_iH) : m_iW(l_iW), m_iH(l_iH), m_vPx((size_t)l_iW * l_iH * 4, 0) {}
    bool f_Valid() const { return m_iW > 0 && m_iH > 0 && m_vPx.size() == (size_t)m_iW * m_iH * 4; }
    uint8_t* f_At(int l_iX, int l_iY) { return &m_vPx[((size_t)l_iY * m_iW + l_iX) * 4]; }
    const uint8_t* f_At(int l_iX, int l_iY) const { return &m_vPx[((size_t)l_iY * m_iW + l_iX) * 4]; }
};

bool f_LoadImage(const std::string& l_strPath, Image& l_oOut, std::string* l_pErr = nullptr);
bool f_LoadImageFromMemory(const void* l_pData, size_t l_nSize, Image& l_oOut, std::string* l_pErr = nullptr);
bool f_SavePng(const std::string& l_strPath, const Image& l_oImg, std::string* l_pErr = nullptr);
std::vector<uint8_t> f_EncodePng(const Image& l_oImg);

// Smallest rect holding every pixel with alpha > threshold. Empty rect for a fully transparent image.
IRect f_OpaqueBounds(const Image& l_oImg, int l_iAlphaThreshold = 0);

Image f_Crop(const Image& l_oSrc, const IRect& l_oR);                     // r may reach outside src (filled transparent)
void f_Blit(Image& l_oDst, const Image& l_oSrc, const IRect& l_oSrcRect, int l_iDx, int l_iDy);
// Repeats the edge pixels of `r` (already in dst) outward by n pixels on every side.
void f_Extrude(Image& l_oDst, const IRect& l_oR, int l_iN);
void f_Premultiply(Image& l_oImg);
void f_Unpremultiply(Image& l_oImg);   // inverse of premultiply (lossy where alpha is small)
// Nearest-neighbour scale, used for variants whose sprites differ in size.
Image f_ScaleNearest(const Image& l_oSrc, int l_iW, int l_iH);
uint64_t f_HashPixels(const Image& l_oImg, const IRect& l_oR);           // FNV-1a over size + pixels
bool f_SamePixels(const Image& l_oA, const IRect& l_oRa, const Image& l_oB, const IRect& l_oRb);

}  // namespace atlas
