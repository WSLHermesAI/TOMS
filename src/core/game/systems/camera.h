// camera.h — maze camera for the walking scene (its own file + class: this was ~120 lines of
// camera state and math embedded in game.cpp/game.h, mixed in with unrelated game logic).
//
// The maze camera exists because stage grids (19x16 up to 34x31 tiles) are far too big to shrink
// onto one fixed-size screen legibly: instead the tile size is derived from a single zoom knob
// ("how many tile columns are visible") and the viewport scrolls over the grid.
//
// Two modes:
//   Follow — the viewport pans continuously to keep the focus tile (the player) centred.
//   Rooms  — the grid is divided into fixed viewport-sized sections; the camera slides to
//            whichever section currently contains the focus tile, so it only moves when the
//            player crosses a boundary (classic room-by-room maze feel).
//
// Deliberately free of renderer and game coupling: the owner feeds in the viewport size in pixels,
// the grid size in tiles and the focus tile, and gets back a scroll origin. That keeps every rule
// here pure math, unit-testable without a GPU (see camera_test.cpp), and single-sourced -- draw()
// and update() both go through this class, which is what guarantees the rendered scroll offset and
// the position being eased toward can never disagree.
#pragma once

#include <algorithm>

namespace toms {

class Camera {
public:
    enum class Mode { Follow = 0, Rooms = 1 };

    // HUD layout the walking scene draws around the grid: a 60px text band on top, and a 50px
    // story_note line at the bottom. The camera owns these because the viewport it computes must
    // match the area the scene actually draws into.
    static constexpr float m_fTopMarginPx    = 60.0f;
    static constexpr float m_fBottomMarginPx = 50.0f;
    static constexpr int   m_iMinViewCols    = 4;      // guard against a corrupt persisted value
    // The canvas width the zoom (viewCols) is measured on: tiles are kZoomWidth / viewCols big on any canvas, so a wide
    // or tall screen (canvas.h) shows more tiles, not bigger ones (the owner 2026-10-25: fill the screen).
    static constexpr float m_fZoomWidth      = 1024.0f;
    static constexpr float m_fSmoothingMs    = 120.0f; // ease: deviation shrinks this fast per frame
    static constexpr float m_fSettleEps      = 0.01f;  // snap the last fraction so it really settles

    static Mode f_ModeFromIndex(int l_iIndex) { return l_iIndex == 1 ? Mode::Rooms : Mode::Follow; }

    Mode f_Mode() const { return m_eMode; }
    void f_SetMode(Mode l_eM) { m_eMode = l_eM; }
    int  f_ModeIndex() const { return (int)m_eMode; }

    int  f_ViewCols() const { return m_iViewCols; }
    void f_SetViewCols(int l_iCols) { m_iViewCols = std::max(m_iMinViewCols, l_iCols); }

    // Tile size in pixels plus the viewport size in tiles, for the current zoom and the pixel size
    // of the drawing area. A viewport with no usable height (<= margins) still yields rows >= 1.
    void f_ViewportTiles(float l_fViewportW, float l_fViewportH,
                       float& l_fTileSize, int& l_iCols, int& l_iRows) const;

    // Recomputes the target origin (in tile units, may be fractional or negative) for the focus
    // tile, clamped so the viewport never scrolls past the grid edges -- and *centred* when the
    // grid is smaller than the viewport, instead of pinning to the top-left corner.
    void f_Retarget(int l_iFocusTileX, int l_iFocusTileY, int l_iGridW, int l_iGridH,
                  float l_fViewportW, float l_fViewportH);

    // Frame-rate-independent exponential smoothing toward the target: the remaining deviation
    // shrinks by a constant fraction per kSmoothingMs, however the frame time is chopped up.
    // dtSeconds <= 0 is a no-op. The last fraction of a tile is closed instantly so the camera
    // actually settles instead of asymptotically crawling forever.
    void f_Update(float l_fDtSeconds);

    // Jump straight to the target (no pan). Used on a floor change, so the new floor never
    // visibly scrolls in from the previous floor's camera position.
    void f_Snap() { m_fX = m_fTargetX; m_fY = m_fTargetY; }

    float f_X() const { return m_fX; }
    float f_Y() const { return m_fY; }
    float f_TargetX() const { return m_fTargetX; }
    float f_TargetY() const { return m_fTargetY; }

    // Direct placement, for restoring a saved position.
    void f_SetPosition(float l_fX, float l_fY) { m_fX = l_fX; m_fY = l_fY; }

private:
    Mode  m_eMode = Mode::Follow;
    int   m_iViewCols = 13;    // tiles across the (1024px-wide) design canvas
    float m_fX = 0.0f, m_fY = 0.0f;             // current viewport origin, tile units (fractional so a
                                            // slide can sit mid-tile between frames)
    float m_fTargetX = 0.0f, m_fTargetY = 0.0f; // where x_/y_ are easing toward
};

} // namespace toms
