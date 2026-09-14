// camera.cpp — see camera.h. Extracted verbatim (math unchanged) from Game::cameraViewportTiles /
// Game::updateCameraTarget / the camera block of Game::update, so behaviour on screen is identical.
#include "camera.h"

#include <cmath>

namespace toms {

void Camera::viewportTiles(float viewportW, float viewportH,
                           float& tileSize, int& cols, int& rows) const {
    const int vc = std::max(kMinViewCols, viewCols_);
    // Tile size comes from the width only, so the horizontal zoom stays exactly "viewCols tiles
    // across" no matter how the window is shaped; rows then follow from the usable height.
    tileSize = viewportW / (float)vc;
    cols = vc;
    const float usableH = viewportH - kTopMarginPx - kBottomMarginPx;
    rows = std::max(1, (int)(usableH / (tileSize > 0.0f ? tileSize : 1.0f)));
}

void Camera::retarget(int focusTileX, int focusTileY, int gridW, int gridH,
                      float viewportW, float viewportH) {
    float ts; int cols, rows;
    viewportTiles(viewportW, viewportH, ts, cols, rows);

    float tx, ty;
    if (mode_ == Mode::Rooms) {
        // Divide the grid into fixed viewport-sized sections, aligned from the origin; the target
        // is whichever section's top-left corner currently contains the focus tile.
        tx = (float)(std::max(0, focusTileX / cols) * cols);
        ty = (float)(std::max(0, focusTileY / rows) * rows);
    } else {
        // Follow: keep the focus tile centred in the viewport.
        tx = (float)focusTileX - cols * 0.5f;
        ty = (float)focusTileY - rows * 0.5f;
    }
    // Clamp so the viewport never scrolls past the grid's edges -- and centre a grid that is
    // smaller than the viewport (small floors are narrower than a 21-tile-wide viewport).
    const float maxX = (float)std::max(0, gridW - cols);
    const float maxY = (float)std::max(0, gridH - rows);
    if (gridW <= cols) tx = -(cols - gridW) * 0.5f;
    else               tx = std::max(0.0f, std::min(tx, maxX));
    if (gridH <= rows) ty = -(rows - gridH) * 0.5f;
    else               ty = std::max(0.0f, std::min(ty, maxY));
    targetX_ = tx; targetY_ = ty;
}

void Camera::update(float dtSeconds) {
    if (!(dtSeconds > 0.0f)) return;
    const float decay = std::exp(-(dtSeconds * 1000.0f) / kSmoothingMs);
    x_ = targetX_ + (x_ - targetX_) * decay;
    y_ = targetY_ + (y_ - targetY_) * decay;
    if (std::fabs(x_ - targetX_) < kSettleEps) x_ = targetX_;
    if (std::fabs(y_ - targetY_) < kSettleEps) y_ = targetY_;
}

} // namespace toms
