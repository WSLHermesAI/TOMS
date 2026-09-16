// Tests for DialogueLayout -- the dialogue one description of its own geometry.
// The point: the row a tap selects must be the row the drawing put there, at any UI scale, and the
// numbers must be the ones the screen already uses today.
#include "dialogue_layout.h"

#include <cstdio>

using namespace toms;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static bool near(float a, float b, float eps = 0.01f) { return (a > b ? a - b : b - a) <= eps; }

// Today framing, as game_helpers.h produces it: box bottom-anchored, rows from box.y+60, 24px pitch,
// tap zone within x [56, W-56].
static DialogueLayout todayRows(int n) {
    DialogueLayout L;
    L.box = glm::vec4(0.0f, 468.0f, 1024.0f, 300.0f);
    L.firstRowY = 468.0f + 60.0f;
    L.pitch = 24.0f;
    L.rowLeft = 56.0f;
    L.rowRight = 1024.0f - 56.0f;
    L.textScale = 1.0f;
    L.rowCount = n;
    return L;
}

int main() {
    UiRoot ui;                     // scale 1.0: the desktop framing must be unchanged
    DialogueLayout L = todayRows(4);

    // --- row rects follow the baselines the draw path already uses ---
    glm::vec4 r0 = L.rowRect(0);
    CHECK(near(r0.x, 56.0f) && near(r0.z, 1024.0f - 112.0f), "row spans the authored inset (%.1f..%.1f)", r0.x, r0.x + r0.z);
    CHECK(near(r0.y + r0.w * 0.5f, L.firstRowY), "row 0 band is centred on row 0 baseline (%.1f)", r0.y + r0.w * 0.5f);
    glm::vec4 r2 = L.rowRect(2);
    CHECK(near(r2.y + r2.w * 0.5f, L.firstRowY + 2.0f * L.pitch), "row 2 sits two pitches down (%.1f)", r2.y + r2.w * 0.5f);

    // --- the tap that matters: on row i baseline, row i is selected ---
    for (int i = 0; i < 4; ++i) {
        const float y = L.firstRowY + (float)i * L.pitch;
        CHECK(L.rowAt(ui, glm::vec2(200.0f, y)) == i, "a tap on row %d baseline selects row %d", i, i);
    }

    // --- a tap between two rows goes to the nearer one (not whichever was scanned first) ---
    const float mid = L.firstRowY + 1.5f * L.pitch;
    CHECK(L.rowAt(ui, glm::vec2(200.0f, mid)) == 2, "a tie goes to the lower row (%.1f)", mid);

    // --- outside the rows: no row, but still on the box ---
    CHECK(L.rowAt(ui, glm::vec2(200.0f, L.box.y + 5.0f)) == -1, "above the first row: no row");
    CHECK(L.rowAt(ui, glm::vec2(10.0f, L.firstRowY)) == -1, "left of the inset: no row");
    CHECK(L.rowAt(ui, glm::vec2(1020.0f, L.firstRowY)) == -1, "right of the inset: no row");
    CHECK(L.onBox(ui, glm::vec2(500.0f, L.box.y + 10.0f)), "a tap on the box itself is on the box");

    // --- the touch target is finger-sized even where the visible row is not ---
    glm::vec4 t2 = L.rowTouchRect(ui, 2);
    CHECK(t2.w >= 44.0f, "row touch band is at least 44px tall, got %.1f", t2.w);
    CHECK(near(t2.y + t2.w * 0.5f, r2.y + r2.w * 0.5f), "and it stays centred on the drawn row");

    // --- and it all still holds at 1.5x: bigger objects, same selection ---
    ui.SetScale(1.5f);
    glm::vec4 drawn3 = ui.ScreenRect(L.rowRect(3));
    CHECK(L.rowAt(ui, glm::vec2(300.0f, drawn3.y + drawn3.w * 0.5f)) == 3, "at 1.5x a tap on row 3 drawn band selects row 3");
    glm::vec4 t15 = L.rowTouchRect(ui, 3);
    CHECK(t15.w * 1.5f >= 44.0f, "the touch band is still >=44px ON SCREEN at 1.5x (%.1f)", t15.w * 1.5f);
    glm::vec4 boxLo = ui.ScreenRect(L.box);
    CHECK(near(boxLo.z, L.box.z * 1.5f), "the box is 1.5x wider on screen (%.1f)", boxLo.z);
    CHECK(near(UiRoot::DesignSize().x, 1024.0f), "and the design is still 1024x768");

    // --- degenerate: no rows means no row is ever hit ---
    DialogueLayout empty = todayRows(0);
    CHECK(empty.rowAt(ui, glm::vec2(200.0f, 528.0f)) == -1, "a node with no choices hits no row");

    if (g_failed == 0) std::printf("dialogue_layout_test: ALL PASS (%d checks)\n", g_checks);
    else               std::printf("dialogue_layout_test: FAILED (%d checks, %d failed)\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
