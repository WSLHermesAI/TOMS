// image_decode.cpp -- see image_decode.h.
#include "image_decode.h"

#include <stb_image.h>
#include <webp/decode.h>

#include <cstdlib>
#include <cstring>

namespace toms {

namespace {
const char* g_pszImageError = "";
}

bool f_IsWebp(const void* l_pData, size_t l_nSize) {
    const unsigned char* l_pB = (const unsigned char*)l_pData;
    return l_nSize >= 12 && std::memcmp(l_pB, "RIFF", 4) == 0 && std::memcmp(l_pB + 8, "WEBP", 4) == 0;
}

unsigned char* f_DecodeImageRgba(const void* l_pData, size_t l_nSize, int* l_pW, int* l_pH) {
    g_pszImageError = "";
    if (!l_pData || l_nSize == 0) { g_pszImageError = "no data"; return nullptr; }
    if (f_IsWebp(l_pData, l_nSize)) {
        int l_iW = 0, l_iH = 0;
        if (!WebPGetInfo((const uint8_t*)l_pData, l_nSize, &l_iW, &l_iH) || l_iW <= 0 || l_iH <= 0) {
            g_pszImageError = "not a readable WebP";
            return nullptr;
        }
        const size_t l_nBytes = (size_t)l_iW * l_iH * 4;
        unsigned char* l_pPx = (unsigned char*)std::malloc(l_nBytes);   // malloc: freed like stb_image's pixels
        if (!l_pPx) { g_pszImageError = "out of memory"; return nullptr; }
        if (!WebPDecodeRGBAInto((const uint8_t*)l_pData, l_nSize, l_pPx, l_nBytes, l_iW * 4)) {
            std::free(l_pPx);
            g_pszImageError = "WebP decoding failed";
            return nullptr;
        }
        *l_pW = l_iW;
        *l_pH = l_iH;
        return l_pPx;
    }
    int l_iCh = 0;
    unsigned char* l_pPx = stbi_load_from_memory((const stbi_uc*)l_pData, (int)l_nSize, l_pW, l_pH, &l_iCh, 4);
    if (!l_pPx) g_pszImageError = stbi_failure_reason() ? stbi_failure_reason() : "cannot decode";
    return l_pPx;
}

void f_ImageFree(unsigned char* l_pPixels) { std::free(l_pPixels); }

const char* f_ImageError() { return g_pszImageError; }

}  // namespace toms
