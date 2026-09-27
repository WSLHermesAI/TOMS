// ui_layout.h — the one place that turns the fixed design size into on-screen geometry (S3.6 / "C").
//
// Why this exists: the maze, the on-canvas pad, the dialogue box and the battle screen each laid
// themselves out with literals authored against a 1024x768 design. That is why shrinking the design
// for phones put the pad off-screen and clipped the battle's HP text (owner reports, 2026-09-13).
// Anything positioned through this struct follows the design size and the UI scale automatically, so
// a phone can use a smaller design with a larger scale and every screen still fits.
//
// It is pure arithmetic -- no Renderer, no Game, no drawing -- so ui_layout_test.cpp can prove the
// invariants (nothing leaves the screen, the pad keeps its distance from the bottom edge, a long
// battle log wraps into the width it is given) without a window.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace toms {

struct UiRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    bool inside(float W, float H, float eps = 0.5f) const {
        return x >= -eps && y >= -eps && x + w <= W + eps && y + h <= H + eps;
    }
};

// The design the AUTHORED numbers were written against (the pad's table, the battle's literals).
constexpr float kAuthoredW = 1024.0f;
constexpr float kAuthoredH = 768.0f;

struct UiLayout {
    float W = kAuthoredW;      // current design size (1024x768 desktop, 768x576 phone)
    float H = kAuthoredH;
    float scale = 1.0f;        // UI scale on top of it (1.0 desktop, 1.25 phone)

    float s(float v) const { return v * scale; }                     // a size
    float xAt(float x) const { return x * (W / kAuthoredW); }         // an authored x -> this design
    float yAt(float y) const { return y * (H / kAuthoredH); }         // an authored y -> this design
    float cx() const { return W * 0.5f; }
    float cy() const { return H * 0.5f; }

    // An authored y for something anchored to the BOTTOM edge (the pad's buttons). On a shorter
    // design the authored value would sit below the screen, so it keeps its distance from the edge
    // instead of its distance from the top.
    // Anchored to the BOTTOM edge, height and all: the authored rect's distance from the bottom is
    // what survives a different design size and a UI scale (the test pins it). Anchoring only the
    // top edge would let the button's scaled height eat into that gap.
    float yFromBottom(float authoredY, float authoredH) const {
        return H - (kAuthoredH - (authoredY + authoredH)) - s(authoredH);
    }

    // Centre-relative scaling for a screen that keeps its shape (the battle modal): positions spread
    // out from the middle, sizes grow.
    float midX(float x) const { return cx() + (x - cx()) * scale; }
    float midY(float y) const { return cy() + (y - cy()) * scale; }

    // The battle modal's content box: everything on that screen is positioned inside this, so it can
    // never leave the design no matter how the scale grows (760x620 authored, which fits a 768x576
    // design at scale 1 and a 1024x768 one at 1.25).
    UiRect battleBox() const {
        float w = std::min(s(760.0f), W - 16.0f);
        float h = std::min(s(620.0f), H - 16.0f);
        return UiRect{(W - w) * 0.5f, (H - h) * 0.5f, w, h};
    }

    UiRect centred(float wAuth, float hAuth, float yFrac = 0.5f) const {
        UiRect r;
        r.w = s(wAuth);
        r.h = s(hAuth);
        r.x = (W - r.w) * 0.5f;
        r.y = (H - r.h) * yFrac;
        return r;
    }

    // Wrap to a pixel width using a caller-supplied measure (the game passes Game::measureText, so
    // wrapping and drawing can never disagree). Breaks on spaces, and between CJK characters, which
    // is what a battle log and a dialogue line actually need.
    template <class Measure>
    std::vector<std::string> wrap(const std::string& text, float maxW, Measure measure) const {
        std::vector<std::string> lines;
        std::string line;
        size_t i = 0;
        auto flush = [&]() { if (!line.empty()) { lines.push_back(line); line.clear(); } };
        while (i < text.size()) {
            size_t len = 1;
            unsigned char c = (unsigned char)text[i];
            if ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            std::string ch = text.substr(i, len);
            i += len;
            if (ch == "\n") { flush(); continue; }
            std::string cand = line + ch;
            if (!line.empty() && measure(cand) > maxW) {
                // prefer breaking at the last space for latin text
                size_t sp = line.find_last_of(' ');
                if (len == 1 && sp != std::string::npos && sp + 1 < line.size()) {
                    lines.push_back(line.substr(0, sp));
                    line = line.substr(sp + 1) + ch;
                } else {
                    lines.push_back(line);
                    line = ch;
                }
            } else {
                line = cand;
            }
        }
        flush();
        if (lines.empty()) lines.push_back(std::string());
        return lines;
    }
};

} // namespace toms
