// camera_test.cpp — headless verification of the maze camera (its own class as of the refactor).
// Exits 0 on success, 1 on any CHECK failure. Run: ./camera_test
//
// These are the properties the walking scene depends on; they are pure math, so they need no
// renderer/GPU -- which is exactly why the camera was pulled out of game.cpp into its own class.
#include "camera.h"
#include <cstdio>
#include <cmath>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)
static bool f_Near(float l_fA, float l_fB, float l_fEps = 0.01f) { return std::fabs(l_fA - l_fB) < l_fEps; }

int main() {
    // Design canvas the walking scene uses, and a typical floor grid (stage10 is 27x22).
    const float l_fW = 1024.0f, l_fH = 768.0f;
    const int   l_iGridW = 27, l_iGridH = 22;

    // 1. Zoom knob -> tile size: viewCols tiles always span the full width, whatever the height.
    {
        Camera l_oCam;
        l_oCam.f_SetViewCols(16);
        float l_fTs; int l_iCols, l_iRows;
        l_oCam.f_ViewportTiles(l_fW, l_fH, l_fTs, l_iCols, l_iRows);
        CHECK(f_Near(l_fTs, l_fW / 16.0f), "tile size = viewport width / viewCols");
        CHECK(l_iCols == 16, "cols equals the zoom level");
        CHECK(l_iRows == (int)((l_fH - Camera::m_fTopMarginPx - Camera::m_fBottomMarginPx) / l_fTs),
              "rows come from the usable height (top/bottom HUD margins removed)");
    }

    // 1b. Other canvases (canvas.h): the same tile size, more columns on a wide one, fewer and more rows in portrait.
    {
        Camera l_oCam;
        l_oCam.f_SetViewCols(16);
        float l_fTs; int l_iCols, l_iRows;
        l_oCam.f_ViewportTiles(1365.0f, 768.0f, l_fTs, l_iCols, l_iRows);
        CHECK(f_Near(l_fTs, 64.0f) && l_iCols == 21, "16:9: the same tiles, 21 columns");
        float l_fTs2; int l_iCols2, l_iRows2;
        l_oCam.f_ViewportTiles(768.0f, 1707.0f, l_fTs2, l_iCols2, l_iRows2);
        CHECK(f_Near(l_fTs2, 64.0f) && l_iCols2 == 12 && l_iRows2 > 20, "portrait: 12 columns, many rows");
        Camera l_oZ;   // the default zoom: 13 across 1024, so a 768 canvas holds 9.75 tiles
        l_oZ.f_SetViewCols(13);
        l_oZ.f_Retarget(18, 5, 19, 30, 768.0f, 1707.0f);   // at the grid's right edge
        CHECK(f_Near(l_oZ.f_TargetX(), 19.0f - 9.75f), "portrait: the view stops at the grid's real right edge (part of a tile shows)");
    }

    // 2. A corrupt/absurd persisted zoom can never produce a degenerate viewport.
    {
        Camera l_oCam;
        l_oCam.f_SetViewCols(0);
        float l_fTs; int l_iCols, l_iRows;
        l_oCam.f_ViewportTiles(l_fW, l_fH, l_fTs, l_iCols, l_iRows);
        CHECK(l_iCols >= Camera::m_iMinViewCols, "viewCols is clamped to kMinViewCols");
        CHECK(l_iRows >= 1, "rows is at least 1 even for a tiny drawing area");
        Camera l_oTiny;
        float l_fTs2; int l_iC2, l_iR2;
        l_oTiny.f_ViewportTiles(l_fW, 40.0f, l_fTs2, l_iC2, l_iR2);   // height smaller than the margins
        CHECK(l_iR2 >= 1, "rows stays >= 1 when the height is smaller than the HUD margins");
    }

    // 3. Follow mode centres the player and clamps at the grid edges.
    {
        Camera l_oCam;                                  // Follow is the default mode
        l_oCam.f_Retarget(10, 10, l_iGridW, l_iGridH, l_fW, l_fH);
        float l_fTs; int l_iCols, l_iRows;
        l_oCam.f_ViewportTiles(l_fW, l_fH, l_fTs, l_iCols, l_iRows);
        CHECK(f_Near(l_oCam.f_TargetX(), 10 - l_iCols * 0.5f), "Follow centres the player horizontally");
        CHECK(f_Near(l_oCam.f_TargetY(), 10 - l_iRows * 0.5f), "Follow centres the player vertically");

        l_oCam.f_Retarget(0, 0, l_iGridW, l_iGridH, l_fW, l_fH);       // top-left corner -> clamp to 0
        CHECK(f_Near(l_oCam.f_TargetX(), 0.0f) && f_Near(l_oCam.f_TargetY(), 0.0f),
              "Follow clamps to the grid's top-left corner");
        l_oCam.f_Retarget(l_iGridW - 1, l_iGridH - 1, l_iGridW, l_iGridH, l_fW, l_fH);
        CHECK(l_oCam.f_TargetX() <= (float)(l_iGridW - l_iCols) + 0.01f, "Follow clamps at the right edge");
        CHECK(l_oCam.f_TargetY() <= (float)(l_iGridH - l_iRows) + 0.01f, "Follow clamps at the bottom edge");
    }

    // 4. A grid smaller than the viewport is centred, not pinned to the corner.
    {
        Camera l_oCam;
        l_oCam.f_SetViewCols(40);
        l_oCam.f_Retarget(2, 2, 8, 6, l_fW, l_fH);
        float l_fTs; int l_iCols, l_iRows;
        l_oCam.f_ViewportTiles(l_fW, l_fH, l_fTs, l_iCols, l_iRows);
        CHECK(l_iCols > 8, "test premise: the viewport is wider than this grid");
        CHECK(f_Near(l_oCam.f_TargetX(), -(l_iCols - 8) * 0.5f), "a narrow grid is centred horizontally");
        CHECK(f_Near(l_oCam.f_TargetY(), -(l_iRows - 6) * 0.5f), "a short grid is centred vertically");
    }

    // 5. Rooms mode: the target only moves when the player crosses a section boundary, and lands
    //    on section-aligned coordinates.
    {
        Camera l_oCam;
        l_oCam.f_SetMode(Camera::Mode::Rooms);
        float l_fTs; int l_iCols, l_iRows;
        l_oCam.f_ViewportTiles(l_fW, l_fH, l_fTs, l_iCols, l_iRows);

        l_oCam.f_Retarget(1, 1, l_iGridW, l_iGridH, l_fW, l_fH);
        CHECK(f_Near(l_oCam.f_TargetX(), 0.0f) && f_Near(l_oCam.f_TargetY(), 0.0f), "Rooms: first section is 0,0");
        const float l_fKeepX = l_oCam.f_TargetX(), l_fKeepY = l_oCam.f_TargetY();
        l_oCam.f_Retarget(std::min(l_iGridW - 1, l_iCols - 1), std::min(l_iGridH - 1, l_iRows - 1), l_iGridW, l_iGridH, l_fW, l_fH);
        CHECK(f_Near(l_oCam.f_TargetX(), l_fKeepX) && f_Near(l_oCam.f_TargetY(), l_fKeepY),
              "Rooms: moving inside the same section does not move the camera");

        l_oCam.f_Retarget(std::min(l_iGridW - 1, l_iCols), std::min(l_iGridH - 1, l_iRows), l_iGridW, l_iGridH, l_fW, l_fH);
        CHECK(l_oCam.f_TargetX() > l_fKeepX || l_oCam.f_TargetY() > l_fKeepY,
              "Rooms: crossing into the next section moves the camera");
        CHECK(f_Near(std::fmod(l_oCam.f_TargetX(), (float)l_iCols), 0.0f), "Rooms: target is column-aligned");
    }

    // 6. Mode round-trip through the persisted integer (game_settings.json stores 0/1).
    {
        for (int l_iI = 0; l_iI <= 1; l_iI++) {
            Camera l_oCam;
            l_oCam.f_SetMode(Camera::f_ModeFromIndex(l_iI));
            CHECK(l_oCam.f_ModeIndex() == l_iI, "camera mode survives an int round-trip");
        }
    }

    // 7. snap() jumps; update() eases, never overshoots, settles exactly, and is a no-op for dt<=0.
    {
        Camera l_oCam;
        l_oCam.f_Retarget(20, 20, l_iGridW, l_iGridH, l_fW, l_fH);
        const float l_fTx = l_oCam.f_TargetX(), l_fTy = l_oCam.f_TargetY();
        l_oCam.f_Update(0.0f);
        CHECK(l_oCam.f_X() == 0.0f && l_oCam.f_Y() == 0.0f, "update(0) does not move the camera");

        float l_fPrevDist = std::fabs(l_oCam.f_X() - l_fTx);
        for (int l_iI = 0; l_iI < 200; l_iI++) {
            l_oCam.f_Update(1.0f / 60.0f);
            const float l_fD = std::fabs(l_oCam.f_X() - l_fTx);
            CHECK(l_fD <= l_fPrevDist + 0.0001f, "ease never overshoots / never moves away from target");
            l_fPrevDist = l_fD;
        }
        CHECK(l_oCam.f_X() == l_fTx && l_oCam.f_Y() == l_fTy, "the ease settles exactly on the target");

        l_oCam.f_Snap();
        CHECK(l_oCam.f_X() == l_oCam.f_TargetX() && l_oCam.f_Y() == l_oCam.f_TargetY(), "snap() jumps to the target");
    }

    // 8. Frame-rate independence: the same total time gives the same result whether it arrives as
    //    one big step or many small ones (exponential smoothing composes multiplicatively).
    {
        Camera l_oA, l_oB;
        l_oA.f_Retarget(20, 20, l_iGridW, l_iGridH, l_fW, l_fH);
        l_oB.f_Retarget(20, 20, l_iGridW, l_iGridH, l_fW, l_fH);
        l_oA.f_Update(0.1f);
        for (int l_iI = 0; l_iI < 10; l_iI++) l_oB.f_Update(0.01f);
        CHECK(f_Near(l_oA.f_X(), l_oB.f_X(), 0.001f) && f_Near(l_oA.f_Y(), l_oB.f_Y(), 0.001f),
              "ease is frame-rate independent for the same elapsed time");
    }

    if (g_iFail == 0) printf("camera_test: ALL PASS\n");
    else             printf("camera_test: %d FAILED\n", g_iFail);
    return g_iFail == 0 ? 0 : 1;
}
