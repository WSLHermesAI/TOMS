// camera.cpp — see camera.h. Extracted verbatim (math unchanged) from Game::cameraViewportTiles /
// Game::updateCameraTarget / the camera block of Game::update, so behaviour on screen is identical.
#include "camera.h"

#include <cmath>

namespace toms {

void Camera::f_ViewportTiles(float l_fViewportW, float l_fViewportH,
                           float& l_fTileSize, int& l_iCols, int& l_iRows) const {
    const int l_iVc = std::max(m_iMinViewCols, m_iViewCols);
    // The zoom is "viewCols tiles across a 1024-wide canvas": the tile size is the same on every canvas, and the columns
    // and rows are what fits (a 1024-wide canvas: exactly viewCols; wider: more; portrait: fewer, and more rows).
    l_fTileSize = m_fZoomWidth / (float)l_iVc;
    l_iCols = std::max(1, (int)(l_fViewportW / l_fTileSize + 0.001f));
    const float l_fUsableH = l_fViewportH - m_fTopMarginPx - m_fBottomMarginPx;
    l_iRows = std::max(1, (int)(l_fUsableH / (l_fTileSize > 0.0f ? l_fTileSize : 1.0f)));
}

void Camera::f_Retarget(int l_iFocusTileX, int l_iFocusTileY, int l_iGridW, int l_iGridH,
                      float l_fViewportW, float l_fViewportH) {
    float l_fTs; int l_iCols, l_iRows;
    f_ViewportTiles(l_fViewportW, l_fViewportH, l_fTs, l_iCols, l_iRows);
    // The visible width in tiles: a canvas that is not a whole number of tiles wide shows part of one more, and the view
    // clamps to the grid's real edge (a portrait canvas: 768 / 78.8 = 9.75 tiles). On the 1024 canvas it is viewCols.
    const float l_fColsF = l_fTs > 0.0f ? std::round(l_fViewportW / l_fTs * 1000.0f) / 1000.0f : (float)l_iCols;

    float l_fTx, l_fTy;
    if (m_eMode == Mode::Rooms) {
        // Divide the grid into fixed viewport-sized sections, aligned from the origin; the target
        // is whichever section's top-left corner currently contains the focus tile.
        l_fTx = (float)(std::max(0, l_iFocusTileX / l_iCols) * l_iCols);
        l_fTy = (float)(std::max(0, l_iFocusTileY / l_iRows) * l_iRows);
    } else {
        // Follow: keep the focus tile centred in the viewport.
        l_fTx = (float)l_iFocusTileX - l_fColsF * 0.5f;
        l_fTy = (float)l_iFocusTileY - l_iRows * 0.5f;
    }
    // Clamp so the viewport never scrolls past the grid's edges -- and centre a grid that is
    // smaller than the viewport (small floors are narrower than a 21-tile-wide viewport).
    const float l_fMaxX = std::max(0.0f, (float)l_iGridW - l_fColsF);
    const float l_fMaxY = (float)std::max(0, l_iGridH - l_iRows);
    if ((float)l_iGridW <= l_fColsF) l_fTx = -(l_fColsF - (float)l_iGridW) * 0.5f;
    else               l_fTx = std::max(0.0f, std::min(l_fTx, l_fMaxX));
    if (l_iGridH <= l_iRows) l_fTy = -(l_iRows - l_iGridH) * 0.5f;
    else               l_fTy = std::max(0.0f, std::min(l_fTy, l_fMaxY));
    m_fTargetX = l_fTx; m_fTargetY = l_fTy;
}

void Camera::f_Update(float l_fDtSeconds) {
    if (!(l_fDtSeconds > 0.0f)) return;
    const float l_fDecay = std::exp(-(l_fDtSeconds * 1000.0f) / m_fSmoothingMs);
    m_fX = m_fTargetX + (m_fX - m_fTargetX) * l_fDecay;
    m_fY = m_fTargetY + (m_fY - m_fTargetY) * l_fDecay;
    if (std::fabs(m_fX - m_fTargetX) < m_fSettleEps) m_fX = m_fTargetX;
    if (std::fabs(m_fY - m_fTargetY) < m_fSettleEps) m_fY = m_fTargetY;
}

} // namespace toms
