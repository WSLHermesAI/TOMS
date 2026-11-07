// miniaudio_vorbis_impl.h -- miniaudio's implementation with Ogg Vorbis (stb_vorbis). Include it in ONE source file of a
// program. The order is miniaudio's own rule: stb_vorbis's declarations, then miniaudio, then stb_vorbis's code.
#pragma once
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
// stb_vorbis leaves these defined, and they break any later header
#undef L
#undef C
#undef R
