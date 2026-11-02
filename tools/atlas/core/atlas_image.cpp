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

IRect f_Unite(const IRect& l_oA, const IRect& l_oB) {
    if (l_oA.f_Empty()) return l_oB;
    if (l_oB.f_Empty()) return l_oA;
    const int l_iX0 = std::min(l_oA.m_iX, l_oB.m_iX), l_iY0 = std::min(l_oA.m_iY, l_oB.m_iY);
    const int l_iX1 = std::max(l_oA.f_Right(), l_oB.f_Right()), l_iY1 = std::max(l_oA.f_Bottom(), l_oB.f_Bottom());
    return {l_iX0, l_iY0, l_iX1 - l_iX0, l_iY1 - l_iY0};
}

static bool f_ReadFile(const std::string& l_strPath, std::vector<uint8_t>& l_vOut) {
    std::ifstream l_oF(f_U8path(l_strPath), std::ios::binary);
    if (!l_oF) return false;
    l_oF.seekg(0, std::ios::end);
    const std::streamoff l_i64N = l_oF.tellg();
    if (l_i64N < 0) return false;
    l_vOut.resize((size_t)l_i64N);
    l_oF.seekg(0);
    if (l_i64N > 0) l_oF.read((char*)l_vOut.data(), l_i64N);
    return (bool)l_oF;
}

bool f_LoadImageFromMemory(const void* l_pData, size_t l_nSize, Image& l_oOut, std::string* l_pErr) {
    int l_iW = 0, l_iH = 0, l_iCh = 0;
    stbi_uc* l_pD = stbi_load_from_memory((const stbi_uc*)l_pData, (int)l_nSize, &l_iW, &l_iH, &l_iCh, 4);
    if (!l_pD) {
        if (l_pErr) *l_pErr = stbi_failure_reason() ? stbi_failure_reason() : "decode failed";
        return false;
    }
    l_oOut.m_iW = l_iW; l_oOut.m_iH = l_iH;
    l_oOut.m_vPx.assign(l_pD, l_pD + (size_t)l_iW * l_iH * 4);
    stbi_image_free(l_pD);
    return true;
}

bool f_LoadImage(const std::string& l_strPath, Image& l_oOut, std::string* l_pErr) {
    std::vector<uint8_t> l_vBytes;
    if (!f_ReadFile(l_strPath, l_vBytes)) {
        if (l_pErr) *l_pErr = "cannot read " + l_strPath;
        return false;
    }
    if (!f_LoadImageFromMemory(l_vBytes.data(), l_vBytes.size(), l_oOut, l_pErr)) {
        if (l_pErr) *l_pErr = l_strPath + ": " + *l_pErr;
        return false;
    }
    return true;
}

static void f_AppendBytes(void* l_pCtx, void* l_pData, int l_iSize) {
    auto* l_pV = (std::vector<uint8_t>*)l_pCtx;
    l_pV->insert(l_pV->end(), (uint8_t*)l_pData, (uint8_t*)l_pData + l_iSize);
}

std::vector<uint8_t> f_EncodePng(const Image& l_oImg) {
    std::vector<uint8_t> l_vOut;
    if (l_oImg.f_Valid()) stbi_write_png_to_func(f_AppendBytes, &l_vOut, l_oImg.m_iW, l_oImg.m_iH, 4, l_oImg.m_vPx.data(), l_oImg.m_iW * 4);
    return l_vOut;
}

bool f_SavePng(const std::string& l_strPath, const Image& l_oImg, std::string* l_pErr) {
    const std::vector<uint8_t> l_vBytes = f_EncodePng(l_oImg);
    if (l_vBytes.empty()) {
        if (l_pErr) *l_pErr = "PNG encode failed for " + l_strPath;
        return false;
    }
    std::ofstream l_oF(f_U8path(l_strPath), std::ios::binary);
    if (!l_oF || !l_oF.write((const char*)l_vBytes.data(), (std::streamsize)l_vBytes.size())) {
        if (l_pErr) *l_pErr = "cannot write " + l_strPath;
        return false;
    }
    return true;
}

IRect f_OpaqueBounds(const Image& l_oImg, int l_iAlphaThreshold) {
    int l_iX0 = l_oImg.m_iW, l_iY0 = l_oImg.m_iH, l_iX1 = -1, l_iY1 = -1;
    for (int l_iY = 0; l_iY < l_oImg.m_iH; l_iY++)
        for (int l_iX = 0; l_iX < l_oImg.m_iW; l_iX++)
            if (l_oImg.f_At(l_iX, l_iY)[3] > l_iAlphaThreshold) {
                l_iX0 = std::min(l_iX0, l_iX); l_iX1 = std::max(l_iX1, l_iX);
                l_iY0 = std::min(l_iY0, l_iY); l_iY1 = std::max(l_iY1, l_iY);
            }
    if (l_iX1 < 0) return {};
    return {l_iX0, l_iY0, l_iX1 - l_iX0 + 1, l_iY1 - l_iY0 + 1};
}

Image f_Crop(const Image& l_oSrc, const IRect& l_oR) {
    Image l_oOut(std::max(l_oR.m_iW, 0), std::max(l_oR.m_iH, 0));
    f_Blit(l_oOut, l_oSrc, l_oR, 0, 0);
    return l_oOut;
}

void f_Blit(Image& l_oDst, const Image& l_oSrc, const IRect& l_oSrcRect, int l_iDx, int l_iDy) {
    for (int l_iY = 0; l_iY < l_oSrcRect.m_iH; l_iY++) {
        const int l_iSy = l_oSrcRect.m_iY + l_iY, l_iTy = l_iDy + l_iY;
        if (l_iSy < 0 || l_iSy >= l_oSrc.m_iH || l_iTy < 0 || l_iTy >= l_oDst.m_iH) continue;
        for (int l_iX = 0; l_iX < l_oSrcRect.m_iW; l_iX++) {
            const int l_iSx = l_oSrcRect.m_iX + l_iX, l_iTx = l_iDx + l_iX;
            if (l_iSx < 0 || l_iSx >= l_oSrc.m_iW || l_iTx < 0 || l_iTx >= l_oDst.m_iW) continue;
            std::memcpy(l_oDst.f_At(l_iTx, l_iTy), l_oSrc.f_At(l_iSx, l_iSy), 4);
        }
    }
}

void f_Extrude(Image& l_oDst, const IRect& l_oR, int l_iN) {
    if (l_iN <= 0 || l_oR.f_Empty()) return;
    auto l_fnClampX = [&](int l_iX) { return std::min(std::max(l_iX, l_oR.m_iX), l_oR.f_Right() - 1); };
    auto l_fnClampY = [&](int l_iY) { return std::min(std::max(l_iY, l_oR.m_iY), l_oR.f_Bottom() - 1); };
    for (int l_iY = l_oR.m_iY - l_iN; l_iY < l_oR.f_Bottom() + l_iN; l_iY++) {
        if (l_iY < 0 || l_iY >= l_oDst.m_iH) continue;
        for (int l_iX = l_oR.m_iX - l_iN; l_iX < l_oR.f_Right() + l_iN; l_iX++) {
            if (l_iX < 0 || l_iX >= l_oDst.m_iW) continue;
            if (l_iX >= l_oR.m_iX && l_iX < l_oR.f_Right() && l_iY >= l_oR.m_iY && l_iY < l_oR.f_Bottom()) continue;
            std::memcpy(l_oDst.f_At(l_iX, l_iY), l_oDst.f_At(l_fnClampX(l_iX), l_fnClampY(l_iY)), 4);
        }
    }
}

void f_Premultiply(Image& l_oImg) {
    for (size_t l_nI = 0; l_nI + 3 < l_oImg.m_vPx.size(); l_nI += 4) {
        const unsigned l_uA = l_oImg.m_vPx[l_nI + 3];
        for (int l_iC = 0; l_iC < 3; l_iC++) l_oImg.m_vPx[l_nI + l_iC] = (uint8_t)((l_oImg.m_vPx[l_nI + l_iC] * l_uA + 127) / 255);
    }
}

void f_Unpremultiply(Image& l_oImg) {
    for (size_t l_nI = 0; l_nI + 3 < l_oImg.m_vPx.size(); l_nI += 4) {
        const unsigned l_uA = l_oImg.m_vPx[l_nI + 3];
        if (l_uA == 0 || l_uA == 255) continue;
        for (int l_iC = 0; l_iC < 3; l_iC++) l_oImg.m_vPx[l_nI + l_iC] = (uint8_t)std::min(255u, (l_oImg.m_vPx[l_nI + l_iC] * 255 + l_uA / 2) / l_uA);
    }
}

Image f_ScaleNearest(const Image& l_oSrc, int l_iW, int l_iH) {
    if (l_iW == l_oSrc.m_iW && l_iH == l_oSrc.m_iH) return l_oSrc;
    Image l_oOut(l_iW, l_iH);
    for (int l_iY = 0; l_iY < l_iH; l_iY++)
        for (int l_iX = 0; l_iX < l_iW; l_iX++)
            std::memcpy(l_oOut.f_At(l_iX, l_iY), l_oSrc.f_At((int)((int64_t)l_iX * l_oSrc.m_iW / l_iW), (int)((int64_t)l_iY * l_oSrc.m_iH / l_iH)), 4);
    return l_oOut;
}

uint64_t f_HashPixels(const Image& l_oImg, const IRect& l_oR) {
    uint64_t l_u64H = 1469598103934665603ull;
    auto l_fnMix = [&](uint8_t l_u8B) { l_u64H ^= l_u8B; l_u64H *= 1099511628211ull; };
    for (int l_iV : {l_oR.m_iW, l_oR.m_iH})
        for (int l_iI = 0; l_iI < 4; l_iI++) l_fnMix((uint8_t)(l_iV >> (l_iI * 8)));
    for (int l_iY = l_oR.m_iY; l_iY < l_oR.f_Bottom(); l_iY++)
        for (int l_iX = l_oR.m_iX; l_iX < l_oR.f_Right(); l_iX++) {
            const uint8_t* l_pP = l_oImg.f_At(l_iX, l_iY);
            for (int l_iC = 0; l_iC < 4; l_iC++) l_fnMix(l_pP[l_iC]);
        }
    return l_u64H;
}

bool f_SamePixels(const Image& l_oA, const IRect& l_oRa, const Image& l_oB, const IRect& l_oRb) {
    if (l_oRa.m_iW != l_oRb.m_iW || l_oRa.m_iH != l_oRb.m_iH) return false;
    for (int l_iY = 0; l_iY < l_oRa.m_iH; l_iY++)
        if (std::memcmp(l_oA.f_At(l_oRa.m_iX, l_oRa.m_iY + l_iY), l_oB.f_At(l_oRb.m_iX, l_oRb.m_iY + l_iY), (size_t)l_oRa.m_iW * 4) != 0) return false;
    return true;
}

}  // namespace atlas
