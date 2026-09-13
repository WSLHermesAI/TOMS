// camera_test.cpp — headless verification of the maze camera (its own class as of the refactor).
// Exits 0 on success, 1 on any CHECK failure. Run: ./camera_test
//
// These are the properties the walking scene depends on; they are pure math, so they need no
// renderer/GPU -- which is exactly why the camera was pulled out of game.cpp into its own class.
#include "camera.h"
#include <cstdio>
#include <cmath>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)
static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

int main() {
    // Design canvas the walking scene uses, and a typical floor grid (stage10 is 27x22).
    const float W = 1024.0f, H = 768.0f;
    const int   gridW = 27, gridH = 22;

    // 1. Zoom knob -> tile size: viewCols tiles always span the full width, whatever the height.
    {
        Camera cam;
        cam.setViewCols(16);
        float ts; int cols, rows;
        cam.viewportTiles(W, H, ts, cols, rows);
        CHECK(near(ts, W / 16.0f), "tile size = viewport width / viewCols");
        CHECK(cols == 16, "cols equals the zoom level");
        CHECK(rows == (int)((H - Camera::kTopMarginPx - Camera::kBottomMarginPx) / ts),
              "rows come from the usable height (top/bottom HUD margins removed)");
    }

    // 2. A corrupt/absurd persisted zoom can never produce a degenerate viewport.
    {
        Camera cam;
        cam.setViewCols(0);
        float ts; int cols, rows;
        cam.viewportTiles(W, H, ts, cols, rows);
        CHECK(cols >= Camera::kMinViewCols, "viewCols is clamped to kMinViewCols");
        CHECK(rows >= 1, "rows is at least 1 even for a tiny drawing area");
        Camera tiny;
        float ts2; int c2, r2;
        tiny.viewportTiles(W, 40.0f, ts2, c2, r2);   // height smaller than the margins
        CHECK(r2 >= 1, "rows stays >= 1 when the height is smaller than the HUD margins");
    }

    // 3. Follow mode centres the player and clamps at the grid edges.
    {
        Camera cam;                                  // Follow is the default mode
        cam.retarget(10, 10, gridW, gridH, W, H);
        float ts; int cols, rows;
        cam.viewportTiles(W, H, ts, cols, rows);
        CHECK(near(cam.targetX(), 10 - cols * 0.5f), "Follow centres the player horizontally");
        CHECK(near(cam.targetY(), 10 - rows * 0.5f), "Follow centres the player vertically");

        cam.retarget(0, 0, gridW, gridH, W, H);       // top-left corner -> clamp to 0
        CHECK(near(cam.targetX(), 0.0f) && near(cam.targetY(), 0.0f),
              "Follow clamps to the grid's top-left corner");
        cam.retarget(gridW - 1, gridH - 1, gridW, gridH, W, H);
        CHECK(cam.targetX() <= (float)(gridW - cols) + 0.01f, "Follow clamps at the right edge");
        CHECK(cam.targetY() <= (float)(gridH - rows) + 0.01f, "Follow clamps at the bottom edge");
    }

    // 4. A grid smaller than the viewport is centred, not pinned to the corner.
    {
        Camera cam;
        cam.setViewCols(40);
        cam.retarget(2, 2, 8, 6, W, H);
        float ts; int cols, rows;
        cam.viewportTiles(W, H, ts, cols, rows);
        CHECK(cols > 8, "test premise: the viewport is wider than this grid");
        CHECK(near(cam.targetX(), -(cols - 8) * 0.5f), "a narrow grid is centred horizontally");
        CHECK(near(cam.targetY(), -(rows - 6) * 0.5f), "a short grid is centred vertically");
    }

    // 5. Rooms mode: the target only moves when the player crosses a section boundary, and lands
    //    on section-aligned coordinates.
    {
        Camera cam;
        cam.setMode(Camera::Mode::Rooms);
        float ts; int cols, rows;
        cam.viewportTiles(W, H, ts, cols, rows);

        cam.retarget(1, 1, gridW, gridH, W, H);
        CHECK(near(cam.targetX(), 0.0f) && near(cam.targetY(), 0.0f), "Rooms: first section is 0,0");
        const float keepX = cam.targetX(), keepY = cam.targetY();
        cam.retarget(std::min(gridW - 1, cols - 1), std::min(gridH - 1, rows - 1), gridW, gridH, W, H);
        CHECK(near(cam.targetX(), keepX) && near(cam.targetY(), keepY),
              "Rooms: moving inside the same section does not move the camera");

        cam.retarget(std::min(gridW - 1, cols), std::min(gridH - 1, rows), gridW, gridH, W, H);
        CHECK(cam.targetX() > keepX || cam.targetY() > keepY,
              "Rooms: crossing into the next section moves the camera");
        CHECK(near(std::fmod(cam.targetX(), (float)cols), 0.0f), "Rooms: target is column-aligned");
    }

    // 6. Mode round-trip through the persisted integer (game_settings.json stores 0/1).
    {
        for (int i = 0; i <= 1; i++) {
            Camera cam;
            cam.setMode(Camera::modeFromIndex(i));
            CHECK(cam.modeIndex() == i, "camera mode survives an int round-trip");
        }
    }

    // 7. snap() jumps; update() eases, never overshoots, settles exactly, and is a no-op for dt<=0.
    {
        Camera cam;
        cam.retarget(20, 20, gridW, gridH, W, H);
        const float tx = cam.targetX(), ty = cam.targetY();
        cam.update(0.0f);
        CHECK(cam.x() == 0.0f && cam.y() == 0.0f, "update(0) does not move the camera");

        float prevDist = std::fabs(cam.x() - tx);
        for (int i = 0; i < 200; i++) {
            cam.update(1.0f / 60.0f);
            const float d = std::fabs(cam.x() - tx);
            CHECK(d <= prevDist + 0.0001f, "ease never overshoots / never moves away from target");
            prevDist = d;
        }
        CHECK(cam.x() == tx && cam.y() == ty, "the ease settles exactly on the target");

        cam.snap();
        CHECK(cam.x() == cam.targetX() && cam.y() == cam.targetY(), "snap() jumps to the target");
    }

    // 8. Frame-rate independence: the same total time gives the same result whether it arrives as
    //    one big step or many small ones (exponential smoothing composes multiplicatively).
    {
        Camera a, b;
        a.retarget(20, 20, gridW, gridH, W, H);
        b.retarget(20, 20, gridW, gridH, W, H);
        a.update(0.1f);
        for (int i = 0; i < 10; i++) b.update(0.01f);
        CHECK(near(a.x(), b.x(), 0.001f) && near(a.y(), b.y(), 0.001f),
              "ease is frame-rate independent for the same elapsed time");
    }

    if (g_fail == 0) printf("camera_test: ALL PASS\n");
    else             printf("camera_test: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
