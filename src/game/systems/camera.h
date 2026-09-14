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
    static constexpr float kTopMarginPx    = 60.0f;
    static constexpr float kBottomMarginPx = 50.0f;
    static constexpr int   kMinViewCols    = 4;      // guard against a corrupt persisted value
    static constexpr float kSmoothingMs    = 120.0f; // ease: deviation shrinks this fast per frame
    static constexpr float kSettleEps      = 0.01f;  // snap the last fraction so it really settles

    static Mode modeFromIndex(int index) { return index == 1 ? Mode::Rooms : Mode::Follow; }

    Mode mode() const { return mode_; }
    void setMode(Mode m) { mode_ = m; }
    int  modeIndex() const { return (int)mode_; }

    int  viewCols() const { return viewCols_; }
    void setViewCols(int cols) { viewCols_ = std::max(kMinViewCols, cols); }

    // Tile size in pixels plus the viewport size in tiles, for the current zoom and the pixel size
    // of the drawing area. A viewport with no usable height (<= margins) still yields rows >= 1.
    void viewportTiles(float viewportW, float viewportH,
                       float& tileSize, int& cols, int& rows) const;

    // Recomputes the target origin (in tile units, may be fractional or negative) for the focus
    // tile, clamped so the viewport never scrolls past the grid edges -- and *centred* when the
    // grid is smaller than the viewport, instead of pinning to the top-left corner.
    void retarget(int focusTileX, int focusTileY, int gridW, int gridH,
                  float viewportW, float viewportH);

    // Frame-rate-independent exponential smoothing toward the target: the remaining deviation
    // shrinks by a constant fraction per kSmoothingMs, however the frame time is chopped up.
    // dtSeconds <= 0 is a no-op. The last fraction of a tile is closed instantly so the camera
    // actually settles instead of asymptotically crawling forever.
    void update(float dtSeconds);

    // Jump straight to the target (no pan). Used on a floor change, so the new floor never
    // visibly scrolls in from the previous floor's camera position.
    void snap() { x_ = targetX_; y_ = targetY_; }

    float x() const { return x_; }
    float y() const { return y_; }
    float targetX() const { return targetX_; }
    float targetY() const { return targetY_; }

    // Direct placement, for restoring a saved position.
    void setPosition(float x, float y) { x_ = x; y_ = y; }

private:
    Mode  mode_ = Mode::Follow;
    int   viewCols_ = 13;    // tiles across the (1024px-wide) design canvas
    float x_ = 0.0f, y_ = 0.0f;             // current viewport origin, tile units (fractional so a
                                            // slide can sit mid-tile between frames)
    float targetX_ = 0.0f, targetY_ = 0.0f; // where x_/y_ are easing toward
};

} // namespace toms
