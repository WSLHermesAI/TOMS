// roamer.cpp — see roamer.h. S1's floor-wanderer AI.
#include "roamer.h"

namespace toms {

bool Roamer::f_CanStand(const GridQuery& l_oG, int l_iNx, int l_iNy, int l_iGw, int l_iGh) const {
    if (l_iNx < 0 || l_iNy < 0 || l_iNx + m_oFp.m_iW > l_iGw || l_iNy + m_oFp.m_iH > l_iGh) return false;   // stay inside the maze
    for (int l_iDy = 0; l_iDy < m_oFp.m_iH; ++l_iDy)
        for (int l_iDx = 0; l_iDx < m_oFp.m_iW; ++l_iDx)
            if (!l_oG.f_Walkable(l_iNx + l_iDx, l_iNy + l_iDy)) return false;   // never phase through walls (F3/F4)
    return true;
}

bool Roamer::f_TryMove(const GridQuery& l_oG, int l_iNx, int l_iNy, int l_iGw, int l_iGh) {
    if (!f_CanStand(l_oG, l_iNx, l_iNy, l_iGw, l_iGh)) return false;
    m_iX = l_iNx; m_iY = l_iNy;
    return true;
}

bool Roamer::f_ChaseStep(const GridQuery& l_oG, int l_iPx, int l_iPy, int l_iGw, int l_iGh) {
    const int l_iDx = l_iPx - m_iX, l_iDy = l_iPy - m_iY;
    // Greedy: close the larger gap first, then the smaller one. If the preferred axis is blocked
    // (a wall, or the whole footprint doesn't fit) fall back to the other axis -- that is what lets
    // a chase slide along a corridor instead of getting stuck head-butting a corner.
    const bool l_bXFirst = (l_iDx > 0 ? l_iDx : -l_iDx) >= (l_iDy > 0 ? l_iDy : -l_iDy);
    const int l_iStepX = l_iDx ? (l_iDx > 0 ? 1 : -1) : 0;
    const int l_iStepY = l_iDy ? (l_iDy > 0 ? 1 : -1) : 0;
    if (l_bXFirst) {
        if (l_iStepX && f_TryMove(l_oG, m_iX + l_iStepX, m_iY, l_iGw, l_iGh)) return true;
        if (l_iStepY && f_TryMove(l_oG, m_iX, m_iY + l_iStepY, l_iGw, l_iGh)) return true;
    } else {
        if (l_iStepY && f_TryMove(l_oG, m_iX, m_iY + l_iStepY, l_iGw, l_iGh)) return true;
        if (l_iStepX && f_TryMove(l_oG, m_iX + l_iStepX, m_iY, l_iGw, l_iGh)) return true;
    }
    // Cornered: keep moving rather than freezing in place, so it can path around the obstacle.
    return f_WanderStep(l_oG, l_iGw, l_iGh);
}

bool Roamer::f_WanderStep(const GridQuery& l_oG, int l_iGw, int l_iGh) {
    static const int l_arrDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    // Prefer going straight: a roamer that keeps its heading reads as patrolling a corridor instead
    // of twitching, and it makes a chase predictable enough for the player to break line of sight.
    if (m_iLastDir >= 0) {
        const int* l_pD = l_arrDirs[m_iLastDir];
        if (f_TryMove(l_oG, m_iX + l_pD[0], m_iY + l_pD[1], l_iGw, l_iGh)) return true;
    }
    // Otherwise pick a random legal direction, starting from a rotating random offset so it doesn't
    // always favour +x. Deterministic per seed (rng_), so tests reproduce exactly.
    const int l_iStart = (int)(f_Rnd() % 4u);
    for (int l_iI = 0; l_iI < 4; ++l_iI) {
        const int l_iIdx = (l_iStart + l_iI) % 4;
        const int* l_pD = l_arrDirs[l_iIdx];
        if (f_TryMove(l_oG, m_iX + l_pD[0], m_iY + l_pD[1], l_iGw, l_iGh)) { m_iLastDir = l_iIdx; return true; }
    }
    m_iLastDir = -1;   // dead end: forget the heading so the next turn picks fresh
    return false;
}

bool Roamer::f_Step(const GridQuery& l_oGrid, int l_iPlayerX, int l_iPlayerY, int l_iGridW, int l_iGridH) {
    ++m_iTurns;
    const int l_iDist = f_Manhattan(m_iX, m_iY, l_iPlayerX, l_iPlayerY);
    // Hysteresis: start chasing inside the detect radius, but only give up past the lose radius, so
    // a player skating along the edge of the radius isn't treated to on/off toggling every turn.
    if (m_eMode == Mode::Wander && l_iDist <= m_iDetectRadius) m_eMode = Mode::Chase;
    else if (m_eMode == Mode::Chase && l_iDist > m_iLoseRadius) m_eMode = Mode::Wander;

    if (m_eMode == Mode::Chase) {
        if (f_ChaseStep(l_oGrid, l_iPlayerX, l_iPlayerY, l_iGridW, l_iGridH)) return true;
        return false;
    }
    return f_WanderStep(l_oGrid, l_iGridW, l_iGridH);
}

} // namespace toms
