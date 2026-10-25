// color_space.h -- where the game blends its colours: one switch for every place that depends on it (the backbuffer,
// the sprite and UI textures, the UI shader, the title scene's output).
//
// Desktop: an sRGB backbuffer. Textures are sampled as sRGB (linear values), blended linear, and the GPU encodes on
// write; RCSS colours are linearized by the UI shader.
// Web and Android (TOMS_GAMMA_SPACE): no sRGB backbuffer. WebGL has none; Android's GL ES drivers (the emulator's among
// them) may ignore the request, and then everything is drawn dark (the owner saw it on 2026-10-25), while a Vulkan
// phone would not -- so Android, like the web, blends in gamma space on every device: textures and colours as they
// are, the colours on screen exactly the art's. Only half-transparent blends differ a little from desktop.
#pragma once

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#define TOMS_GAMMA_SPACE 1
#else
#define TOMS_GAMMA_SPACE 0
#endif
