#pragma once
// DialogueLayout -- the dialogue screen geometry, in ONE place, for both drawing and hit-testing.
//
// Why this exists: the dialogue already shares two functions with its own hit-test (game_helpers.h
// dialogueBoxRect / dialogueRowY), which is the right instinct, but the hit-test then re-derives its own
// zone by hand (py within +-22 of the baseline, px within [56, W-56]) while the draw path draws a
// different height than that zone. Two numbers for one thing is how "it looks fine but the tap misses"
// happens. This struct is the single description: the draw path asks it for rects, the input path asks
// it which row a point is on, and both go through UiRoot so the answer stays true at any UI scale.
//
// Layering: lives in src/game/ui/ and takes its numbers as PARAMETERS -- it does not include
// game_helpers.h (core/), so ui/ still does not depend on core/. The caller passes the values it
// already gets from dialogueBoxRect/dialogueRowY, so today's framing is preserved, not re-authored.

#include <glm/glm.hpp>

#include "ui_root.h"

namespace toms {

struct DialogueLayout {
    // All rects are in DESIGN space (1024x768), the space every authored rect in the code uses.
    glm::vec4 box       = glm::vec4(0, 0, 0, 0);
    float     firstRowY = 0.0f;    // baseline y of row 0
    float     pitch     = 0.0f;    // baseline-to-baseline distance between rows
    float     halfZone  = 22.0f;   // a row tap zone reaches this far above/below its baseline
    float     rowLeft   = 56.0f;   // tap zone / text inset
    float     rowRight  = 0.0f;    // where the zone ends (W - 56 today); 0 = derive from the box
    float     textScale = 1.0f;
    int       rowCount  = 0;

    float right() const { return rowRight > 0.0f ? rowRight : box.x + box.w - rowLeft; }

    // The visible row band: what the row own text occupies, for a highlight or row background.
    glm::vec4 rowRect(int i) const {
        const float top = firstRowY + (float)i * pitch - pitch * 0.5f;
        return glm::vec4(rowLeft, top, right() - rowLeft, pitch);
    }

    // The tap target: the visible band, at least minScreen px tall ON SCREEN.
    glm::vec4 rowTouchRect(const UiRoot& ui, int i, float minScreen = 44.0f) const {
        return ui.TouchRect(rowRect(i), minScreen);
    }

    // Which row a screen point is on, or -1. Overlapping zones resolve to the NEAREST baseline, so a
    // tap between two rows picks the one it is really closer to instead of whichever the loop saw
    // first (the old hand-written scan returned the first row whose +-22 window matched).
    int rowAt(const UiRoot& ui, const glm::vec2& screenPoint) const {
        if (rowCount <= 0) return -1;
        const glm::vec2 p = ui.ScreenToLocal(screenPoint);
        if (p.x < rowLeft || p.x > right()) return -1;
        int best = -1;
        float bestDist = halfZone;
        for (int i = 0; i < rowCount; ++i) {
            const float base = firstRowY + (float)i * pitch;
            const float d = (p.y > base) ? p.y - base : base - p.y;
            if (d <= bestDist) { bestDist = d; best = i; }
        }
        return best;
    }

    // Is the point on the box at all (so a tap that hits no row can still mean advance)?
    bool onBox(const UiRoot& ui, const glm::vec2& screenPoint) const { return ui.Hit(box, screenPoint); }
};

}  // namespace toms
