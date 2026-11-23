// image_decode.h -- one decoder for the pictures the game ships: PNG / JPG / ... through stb_image, WebP through libwebp
// (the web build packs the atlas pages as WebP: tools/web/pack_webp.py). The format is told by the file's first bytes,
// not its name, so a page named "game.png" in its .atlas may be WebP (vfs.h f_VfsWebpTwin).
#pragma once
#include <cstddef>

namespace toms {

// True when the bytes are a WebP file ("RIFF" .... "WEBP").
bool f_IsWebp(const void* l_pData, size_t l_nSize);

// Decodes to RGBA, 8 bits a channel, rows top to bottom. Returns nullptr when it cannot (f_ImageError says why).
// Free the pixels with f_ImageFree (or stbi_image_free: both are malloc'd).
unsigned char* f_DecodeImageRgba(const void* l_pData, size_t l_nSize, int* l_pW, int* l_pH);
void f_ImageFree(unsigned char* l_pPixels);
const char* f_ImageError();

}  // namespace toms
