// Tests for UiRoot -- the one scaled root the game's UI hangs off (owner's "grow the UI objects, keep
// the game resolution" rule). The invariant this file exists to protect: the mapping the DRAWING uses
// and the mapping the HIT-TESTING uses are the same function, so a control that looks bigger is still
// exactly as tappable as it looks.
#include "ui_root.h"

#include <cstdio>

using namespace toms;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static bool near(float a, float b, float eps = 0.01f) { return (a > b ? a - b : b - a) <= eps; }

int main() {
    UiRoot ui;
    const glm::vec4 row(400.0f, 640.0f, 300.0f, 24.0f);   // a dialogue-row-shaped rect

    // --- 1.0 is the identity: the desktop framing must not change at all ---
    CHECK(near(ui.Scale(), 1.0f), "default scale is 1.0");
    glm::vec4 s1 = ui.ScreenRect(row);
    CHECK(near(s1.x, row.x) && near(s1.y, row.y) && near(s1.z, row.z) && near(s1.w, row.w),
          "at 1.0 a rect maps to itself, got (%.1f,%.1f,%.1f,%.1f)", s1.x, s1.y, s1.z, s1.w);
    CHECK(ui.Hit(row, glm::vec2(row.x + 1.0f, row.y + 1.0f)), "a point inside hits at 1.0");
    CHECK(!ui.Hit(row, glm::vec2(row.x - 1.0f, row.y + 1.0f)), "a point outside misses at 1.0");

    // --- 1.5: the OBJECT grows, the design does not ---
    ui.SetScale(1.5f);
    CHECK(near(UiRoot::DesignSize().x, 1024.0f) && near(UiRoot::DesignSize().y, 768.0f),
          "the design stays 1024x768 no matter the scale");
    glm::vec4 s15 = ui.ScreenRect(row);
    CHECK(near(s15.z, row.z * 1.5f) && near(s15.w, row.w * 1.5f),
          "a 300x24 rect becomes 450x36 on screen, got %.1fx%.1f", s15.z, s15.w);

    // scaling is about the pivot, so a rect on the centre line keeps its centre
    const glm::vec4 centred(512.0f - 50.0f, 384.0f - 10.0f, 100.0f, 20.0f);
    glm::vec4 sc = ui.ScreenRect(centred);
    CHECK(near(sc.x + sc.z * 0.5f, 512.0f) && near(sc.y + sc.w * 0.5f, 384.0f),
          "a centred rect stays centred when scaled, got centre (%.1f,%.1f)", sc.x + sc.z * 0.5f, sc.y + sc.w * 0.5f);
    // ...and nothing is pushed off the design by the growth alone: the composition grows outward
    CHECK(sc.x < centred.x && sc.y < centred.y, "growth is outward from the pivot, not one-directional");

    // --- the inverse: what an on-screen tap means in design space ---
    glm::vec2 back = ui.ScreenToLocal(glm::vec2(s15.x, s15.y));
    CHECK(near(back.x, row.x) && near(back.y, row.y),
          "screen -> local inverts local -> screen, got (%.1f,%.1f)", back.x, back.y);
    glm::vec2 rt = ui.ScreenToLocal(ui.ScreenRect(row).x > 0 ? glm::vec2(s15.x + s15.z, s15.y + s15.w) : glm::vec2(0, 0));
    CHECK(near(rt.x, row.x + row.z) && near(rt.y, row.y + row.w),
          "all four corners round-trip, got (%.1f,%.1f)", rt.x, rt.y);

    // --- drawing and hit-testing cannot disagree ---
    // A tap on the SCREEN rect the caller drew must hit; a tap just outside it must not.
    const glm::vec2 centreOfRow(s15.x + s15.z * 0.5f, s15.y + s15.w * 0.5f);
    CHECK(ui.Hit(row, centreOfRow), "a tap on the drawn rect hits");
    CHECK(!ui.Hit(row, glm::vec2(s15.x + s15.z + 2.0f, centreOfRow.y)), "a tap just outside misses");
    CHECK(ui.Hit(row, glm::vec2(s15.x + 0.5f, s15.y + 0.5f)), "the drawn rect's own corner hits");

    // --- touch targets: a small authored row still gets a finger-sized target ---
    glm::vec4 t1 = ui.TouchRect(row, 44.0f);
    CHECK(near(t1.w * 1.5f, 44.0f), "at 1.5 a 24px row is expanded to a 44px-tall target on screen (%.1f)", t1.w * 1.5f);
    ui.SetScale(1.0f);
    glm::vec4 t2 = ui.TouchRect(row, 44.0f);
    CHECK(near(t2.w, 44.0f), "at 1.0 a 24px row is expanded to 44px on screen, got %.1f", t2.w);
    // NOTE: a glm::vec4 rect here is (x, y, w, h) == (.x, .y, .z, .w) -- .w is the HEIGHT. Using .w as
    // a width silently compares against half the height; that mistake is exactly what this check failed on
    // the first run, and the code was right.
    CHECK(near(t2.x + t2.z * 0.5f, row.x + row.z * 0.5f), "expansion is about the row's centre, so it does not drift");
    CHECK(near(t2.z, row.z), "the touch rect never narrows a wide element");

    // --- degenerate input fails safe ---
    ui.SetScale(0.0f);
    CHECK(near(ui.Scale(), 1.0f), "a zero scale is refused, not applied");
    ui.SetScale(1.0f);
    ui.SetPivot(glm::vec2(0.0f, 0.0f));
    ui.SetScale(2.0f);
    glm::vec4 tl = ui.ScreenRect(glm::vec4(10.0f, 10.0f, 5.0f, 5.0f));
    CHECK(near(tl.x, 20.0f) && near(tl.z, 10.0f), "with the pivot at the origin, scale doubles the position too (%.1f)", tl.x);

    if (g_failed == 0) std::printf("ui_root_test: ALL PASS (%d checks)\n", g_checks);
    else               std::printf("ui_root_test: FAILED (%d checks, %d failed)\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
