// game_text_draw.cpp — text/bar primitives: UTF-8 -> code point decode, glyph draw,
// width measurement, HP bars and the Power Bar. Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

// Proper UTF-8 decode -> code points, then draw each glyph from font atlas.
static std::u32string utf8_to_utf32(const std::string& s) {
    std::u32string out; size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { i++; continue; }
        for (int k = 1; k < len && i + k < s.size(); k++)
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(cp); i += len;
    }
    return out;
}

// B (mobile): one multiplier for every on-screen text size. Read by drawText/measureText so text
// and its measurement can never disagree -- the two must use the same scale or centred/right-
// aligned strings drift. The comment this replaced claimed "the browser sets 1.25 on small
// screens" -- checked while raising the base value below, and nothing anywhere actually assigns
// this at runtime (a real, still-open gap, not touched here: small-screen detection was never
// wired to it). 1.15 (up from 1.0, 2026-09-17 art/UI polish pass) is a flat, always-on bump so
// every screen's text reads a bit bigger by default, independent of that still-missing feature.
namespace toms { float g_uiScale = 1.15f; int g_padShiftY = 0; }

void Game::drawText(const std::string& s, float x, float y, float size, const float tint[4]) {
    size *= toms::g_uiScale;
    float cx = x;
    std::u32string cps = utf8_to_utf32(s);
    bool fontChanged = false;
    // First glyph's top bearing establishes the baseline reference (FM79979
    // FreetypeGlypth.cpp L145: YOffset = -firstGlyph->Offset.y). All glyphs are
    // then placed top-aligned to that, at their NATURAL size — no cell-centering,
    // no shrink. This is exactly how the reference lays text out.
    float firstTop = 0;
    const std::array<float,4>* m0 = nullptr;
    if (font_ && !cps.empty()) {
        m0 = font_->glyphMetrics(cps[0]);
        if (m0) firstTop = (*m0)[1];   // topBearing
    }
    for (uint32_t cp : cps) {
        auto it = fontMap.find(cp);
        if (it == fontMap.end()) {
            // realtime fallback: try to pull the glyph from the primary font or a
            // system font and bake it into the atlas on the fly.
            if (font_ && font_->ensure(cp)) {
                const std::array<float,4>* uv = font_->uv(cp);
                if (uv) {
                    fontMap[cp] = *uv; it = fontMap.find(cp); fontChanged = true;
                    if (cp == cps[0]) { m0 = font_->glyphMetrics(cp); if (m0) firstTop = (*m0)[1]; }
                }
            }
        }
        if (it == fontMap.end()) { cx += size; continue; }   // truly unknown glyph
        auto& uv = it->second;
        // Design metrics from the font are stored in ATLAS pixels (relative to the
        // bake cell, cell_). To place the quad at the requested `size` we must
        // scale them up to display pixels: scale = size / cell_. This is exactly
        // what the reference does (its Size.x/Size.y are at m_iFontSize, then the
        // whole vertex buffer is scaled by m_fScale). Without this, every glyph
        // renders at cell_ px instead of `size`, and the per-glyph topBearing (which
        // differs for e.g. 'e' vs 'h') shifts glyphs by the wrong amount -> they
        // fall off the baseline and clip.
        float leftB = 0, topB = 0, sx = size, sy = size;
        float s = (font_ ? size / (float)font_->cellSize() : 1.0f);
        if (font_) {
            if (auto* m = font_->glyphMetrics(cp)) {
                leftB = (*m)[0]*s; topB = (*m)[1]*s; sx = (*m)[2]*s; sy = (*m)[3]*s;
            }
        }
        float qw = sx, qh = sy;                 // natural glyph size (display px)
        // Vertical placement matches FM79979 RenderFont: vertex.y = YOffset +
        // glyphOffset.y = -firstOffset.y + glyphOffset.y. In our stored metrics
        // (glyphM_[1] = -top_stb = top_freetype = -Offset.y_ref) this is
        //   qy = y + (glyphM_[first][1] - glyphM_[cp][1]) * s
        // (NOT y - firstTop*s + glyphM_[cp][1]*s, which would negate the bearing).
        float qx = cx + leftB;                  // advance then place by bearing
        float qy = y + (firstTop*s) - topB;     // baseline-aligned (ref sign)
        Quad q; q.rect[0]=qx; q.rect[1]=qy; q.rect[2]=qw; q.rect[3]=qh;
        q.uv[0]=uv[0]; q.uv[1]=uv[1]; q.uv[2]=uv[2]; q.uv[3]=uv[3];
        q.tint[0]=tint[0]; q.tint[1]=tint[1]; q.tint[2]=tint[2]; q.tint[3]=tint[3];
        ren->drawText(q);
        cx += leftB + sx;                       // reference advance = Offset.x + Size.x
    }
    // If any glyph was added to the atlas this frame, re-upload it so it shows up.
    if (fontChanged && font_) {
        ren->updateFont(font_->atlas(), font_->atlasW(), font_->atlasH());
    }
}

// Public wrapper so TextNode (render-bound text) can draw through Game's font.
void Game::drawTextPublic(const std::string& s, float x, float y, float sz, const float* t) {
    drawText(s, x, y, sz, t);
}

float Game::measureText(const std::string& s, float size) const {
    size *= toms::g_uiScale;      // must match drawText (B)
    float w = 0;
    std::u32string cps = utf8_to_utf32(s);
    float s2 = (font_ ? size / (float)font_->cellSize() : 1.0f);
    for (uint32_t cp : cps) {
        auto it = fontMap.find(cp);
        float leftB = 0, sx = (font_ ? (float)font_->cellSize() : 32.0f);
        if (font_ && it != fontMap.end()) {
            if (auto* m = font_->glyphMetrics(cp)) { leftB = (*m)[0]; sx = (*m)[2]; }
        }
        w += (leftB + sx) * s2;   // matches drawText advance (Offset.x + Size.x), scaled to display px
    }
    return w;
}

void Game::drawBar(float x, float y, float w, float h, float frac, const float col[4]) {
    // background
    Quad bg; bg.rect[0]=x; bg.rect[1]=y; bg.rect[2]=w; bg.rect[3]=h;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1; bg.solid=true;
    bg.tint[0]=0.2f;bg.tint[1]=0.2f;bg.tint[2]=0.25f;bg.tint[3]=1; ren->drawSprite(bg);
    float fw = w * std::max(0.0f, std::min(1.0f, frac));
    if (fw > 0) {
        Quad fg; fg.rect[0]=x; fg.rect[1]=y; fg.rect[2]=fw; fg.rect[3]=h;
        fg.uv[0]=0;fg.uv[1]=0;fg.uv[2]=1;fg.uv[3]=1; fg.solid=true;
        fg.tint[0]=col[0];fg.tint[1]=col[1];fg.tint[2]=col[2];fg.tint[3]=1; ren->drawSprite(fg);
    }
}

// Milestone 6: renders the Attack/Defense Power Bar -- five solid-colour zones (red/blue/green/
// blue/red, matching docs/design/FIGHT_SCENE_DESIGN.md §2's symmetric layout) scaled to fit [x, x+w], plus a
// thin marker at `position` (in the bar's own [0, 2*redOuter] units). Same raw-Quad technique as
// drawBar()/the dialogue box background -- deliberately NOT ImGui, so the marker's frame timing
// is never at the mercy of ImGui's own frame pacing (architecture-doc §2.3 Phase 5).
void Game::drawPowerBar(float x, float y, float w, float h, const toms::PowerBarParams& bar, float position) {
    float span = 2.0f * bar.redOuter;
    if (span <= 0.0f) return;
    float c = bar.redOuter;
    auto seg = [&](float fromUnits, float toUnits, float r, float g, float b) {
        float x0 = x + std::max(0.0f, fromUnits) / span * w;
        float x1 = x + std::min(span, toUnits) / span * w;
        if (x1 <= x0) return;
        Quad q; q.rect[0]=x0; q.rect[1]=y; q.rect[2]=x1-x0; q.rect[3]=h;
        q.uv[0]=0;q.uv[1]=0;q.uv[2]=1;q.uv[3]=1; q.solid=true;
        q.tint[0]=r; q.tint[1]=g; q.tint[2]=b; q.tint[3]=1.0f;
        ren->drawSprite(q);
    };
    seg(0.0f,            c - bar.blueOuter, 0.75f, 0.25f, 0.25f);   // red (left)
    seg(c - bar.blueOuter, c - bar.greenHalf, 0.3f, 0.45f, 0.85f);  // blue (left)
    seg(c - bar.greenHalf, c + bar.greenHalf, 0.3f, 0.8f, 0.4f);    // green (center)
    seg(c + bar.greenHalf, c + bar.blueOuter, 0.3f, 0.45f, 0.85f);  // blue (right)
    seg(c + bar.blueOuter, span,              0.75f, 0.25f, 0.25f); // red (right)

    float mx = x + std::max(0.0f, std::min(span, position)) / span * w;
    Quad marker; marker.rect[0]=mx-2.0f; marker.rect[1]=y-6.0f; marker.rect[2]=4.0f; marker.rect[3]=h+12.0f;
    marker.uv[0]=0;marker.uv[1]=0;marker.uv[2]=1;marker.uv[3]=1; marker.solid=true;
    marker.tint[0]=1;marker.tint[1]=1;marker.tint[2]=1;marker.tint[3]=1;
    ren->drawSprite(marker);
}

void Game::drawFocusSplash() {
    if (std::getenv("TOMS_NOSPLASH")) return;  // TEST: toggle splash off to isolate bug
    float W = (float)ren->width(), H = (float)ren->height();
    // full-screen black splash (transparent 50%) so the player focuses on the
    // active modal scene (combat / dialogue / inventory).
    Quad dim; dim.rect[0]=0; dim.rect[1]=0; dim.rect[2]=W; dim.rect[3]=H;
    dim.uv[0]=0;dim.uv[1]=0;dim.uv[2]=1;dim.uv[3]=1; dim.solid=true;
    dim.tint[0]=0;dim.tint[1]=0;dim.tint[2]=0;dim.tint[3]=0.8f; ren->drawSprite(dim);
}

// store modal uses a full-screen black splash at alpha 0.5 (per design) and is the
// topmost layer, drawn last in Game::draw(); so it sits above all other overlays.
void Game::drawStoreSplash() {
    if (std::getenv("TOMS_NOSPLASH")) return;
    float W = (float)ren->width(), H = (float)ren->height();
    Quad dim; dim.rect[0]=0; dim.rect[1]=0; dim.rect[2]=W; dim.rect[3]=H;
    dim.uv[0]=0;dim.uv[1]=0;dim.uv[2]=1;dim.uv[3]=1; dim.solid=true;
    dim.tint[0]=0;dim.tint[1]=0;dim.tint[2]=0;dim.tint[3]=0.5f; ren->drawSprite(dim);
}
