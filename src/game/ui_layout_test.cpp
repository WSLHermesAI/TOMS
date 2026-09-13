// ui_layout_test.cpp — the invariants the mobile layout work depends on (S3.6 / "C").
// Run: ./ui_layout_test
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ui_layout.h"

using toms::UiLayout;   // the test reads best unqualified
using toms::UiRect;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) { ++g_fails; fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__); \
                       fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); }     \
    } while (0)

// a stand-in for Game::measureText: 16 px per CJK glyph, 8 px per ASCII one
static float measure(const std::string& s) {
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        size_t len = (c < 0x80) ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : 4));
        i += len;
        w += (len > 1) ? 16.0f : 8.0f;
    }
    return w;
}

int main() {
    // ---- the two designs the game actually runs: desktop and the phone candidate ----
    for (UiLayout L : { toms::UiLayout{1024, 768, 1.0f}, toms::UiLayout{768, 576, 1.0f},
                        toms::UiLayout{768, 576, 1.25f} }) {
        char tag[64];
        snprintf(tag, sizeof(tag), "design %.0fx%.0f scale %.2f", L.W, L.H, L.scale);
        // the pad: authored y=626 (up button) must stay on screen and keep its bottom distance
        float padUpY = L.yFromBottom(626, 72);
        CHECK(padUpY >= 0 && padUpY + L.s(72) <= L.H + 0.5f,
              "%s: pad up button leaves the screen (y=%.1f h=%.1f H=%.0f)", tag, padUpY, L.s(72), L.H);
        CHECK(std::abs((L.H - (padUpY + L.s(72))) - (768.0f - (626.0f + 72))) < 1.0f,
              "%s: pad lost its distance from the bottom edge", tag);
        // a centred panel must never exceed the design
        toms::UiRect panel = L.centred(560, 210);
        CHECK(panel.inside(L.W, L.H), "%s: centred panel leaves the design", tag);
        // The rule the battle relayout must follow: its content lives inside one centred box, so no
        // element can leave the design however the scale grows. (Today the enemy HP label is drawn at
        // an absolute cx+560 and DOES overflow a 768-wide design at scale 1.25 -- clamping it to this
        // box is the wiring step, and this check is what will hold it there.)
        UiRect box = L.battleBox();
        CHECK(box.inside(L.W, L.H), "%s: battle content box leaves the design", tag);
        CHECK(box.w <= L.W - 16.0f, "%s: battle box is wider than the design", tag);
        // sizes grow with the scale, positions do not drift at scale 1
        CHECK(std::abs(L.s(96) - 96 * L.scale) < 0.01f, "%s: s() is not a straight multiply", tag);
        if (L.scale == 1.0f) CHECK(std::abs(L.midX(300) - 300) < 0.01f, "%s: scale 1 must not move anything", tag);
    }

    // ---- wrapping: a battle log line must break inside the width it is given ----
    UiLayout L{1024, 768, 1.0f};
    const std::string cjk = "\u5be9\u5224\u4e4b\u5370\u88ab\u6230\u9b25\u7684\u56de\u97ff\u9707\u788e\u4e86\u4e00\u89d2";   // 15 CJK glyphs
    auto lines = L.wrap(cjk, 96.0f, measure);      // room for 6 glyphs per line
    CHECK(lines.size() >= 3, "15 CJK glyphs in a 96 px column should need >= 3 lines, got %d", (int)lines.size());
    for (const auto& ln : lines) CHECK(measure(ln) <= 96.0f + 0.01f, "wrapped line is too wide (%.0f)", measure(ln));
    std::string joined;
    for (const auto& ln : lines) joined += ln;
    CHECK(joined == cjk, "wrapping must not lose or duplicate characters");
    auto latin = L.wrap("Vorkath strikes your guard", 80.0f, measure);
    for (const auto& ln : latin) CHECK(measure(ln) <= 80.0f + 0.01f, "latin line too wide (%.0f)", measure(ln));
    CHECK(latin.size() >= 3, "latin text should wrap too, got %d lines", (int)latin.size());
    // a real newline forces a break
    auto forced = L.wrap("a\nb", 1000.0f, measure);
    CHECK(forced.size() == 2 && forced[0] == "a" && forced[1] == "b", "explicit newline must break");

    printf("ui_layout_test: %s (%d checks)\n", g_fails == 0 ? "ALL PASS" : "FAILED", g_checks);
    return g_fails == 0 ? 0 : 1;
}
