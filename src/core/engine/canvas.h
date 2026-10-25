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
    uint32_t w = 1024, h = 768;
    bool portrait() const { return h > w; }
    bool operator==(const Canvas& o) const { return w == o.w && h == o.h; }
    bool operator!=(const Canvas& o) const { return !(*this == o); }
};

constexpr uint32_t kShortSide = 768, kMinLongSide = 1024, kMaxLongSide = 2048;

inline Canvas canvasFor(uint32_t deviceW, uint32_t deviceH) {
    Canvas c;
    if (deviceW == 0 || deviceH == 0) return c;   // no screen yet: the landscape default
    const bool portrait = deviceH > deviceW;
    const double ratio = portrait ? double(deviceH) / double(deviceW) : double(deviceW) / double(deviceH);
    const uint32_t longSide =
        (uint32_t)std::clamp<long>(std::lround(kShortSide * ratio), (long)kMinLongSide, (long)kMaxLongSide);
    c.w = portrait ? kShortSide : longSide;
    c.h = portrait ? longSide : kShortSide;
    return c;
}

}  // namespace toms
