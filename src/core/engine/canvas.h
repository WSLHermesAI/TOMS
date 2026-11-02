// canvas.h -- the size of the design canvas for a screen (docs/gameplay/12_UI_LAYOUT.md; the owner 2026-10-25: landscape
// and portrait, fill the screen with no black bars, switch live when the screen turns).
//
// Everything the game and the UI draw is laid out on a canvas of design pixels, scaled to the screen. Its short side is
// always kShortSide (768), so text and buttons keep their size in either orientation; the long side follows the
// screen's shape, from kMinLongSide (1024: the 4:3 canvas the layouts were made for, so a 4:3 window is unchanged) up
// to kMaxLongSide. A screen squarer than 4:3 / 3:4 gets the 4:3 / 3:4 canvas and thin bars.
//
//   toms::Canvas c = toms::canvasFor(deviceW, deviceH);   // 1920 x 1080 -> 1365 x 768, landscape
//                                                          // 1080 x 2400 -> 768 x 1707, portrait
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace toms {

struct Canvas {
    uint32_t m_uW = 1024, m_uH = 768;
    bool f_Portrait() const { return m_uH > m_uW; }
    bool operator==(const Canvas& l_oO) const { return m_uW == l_oO.m_uW && m_uH == l_oO.m_uH; }
    bool operator!=(const Canvas& l_oO) const { return !(*this == l_oO); }
};

constexpr uint32_t g_uShortSide = 768, g_uMinLongSide = 1024, g_uMaxLongSide = 2048;

inline Canvas f_CanvasFor(uint32_t l_uDeviceW, uint32_t l_uDeviceH) {
    Canvas l_oC;
    if (l_uDeviceW == 0 || l_uDeviceH == 0) return l_oC;   // no screen yet: the landscape default
    const bool l_bPortrait = l_uDeviceH > l_uDeviceW;
    const double l_dRatio = l_bPortrait ? double(l_uDeviceH) / double(l_uDeviceW) : double(l_uDeviceW) / double(l_uDeviceH);
    const uint32_t l_uLongSide =
        (uint32_t)std::clamp<long>(std::lround(g_uShortSide * l_dRatio), (long)g_uMinLongSide, (long)g_uMaxLongSide);
    l_oC.m_uW = l_bPortrait ? g_uShortSide : l_uLongSide;
    l_oC.m_uH = l_bPortrait ? l_uLongSide : g_uShortSide;
    return l_oC;
}

}  // namespace toms
