// roamer.cpp — see roamer.h. S1's floor-wanderer AI.
#include "roamer.h"

namespace toms {

bool Roamer::canStand(const GridQuery& g, int nx, int ny, int gw, int gh) const {
    if (nx < 0 || ny < 0 || nx + fp_.w > gw || ny + fp_.h > gh) return false;   // stay inside the maze
    for (int dy = 0; dy < fp_.h; ++dy)
        for (int dx = 0; dx < fp_.w; ++dx)
            if (!g.walkable(nx + dx, ny + dy)) return false;   // never phase through walls (F3/F4)
    return true;
}

bool Roamer::tryMove(const GridQuery& g, int nx, int ny, int gw, int gh) {
    if (!canStand(g, nx, ny, gw, gh)) return false;
    x_ = nx; y_ = ny;
    return true;
}

bool Roamer::chaseStep(const GridQuery& g, int px, int py, int gw, int gh) {
    const int dx = px - x_, dy = py - y_;
    // Greedy: close the larger gap first, then the smaller one. If the preferred axis is blocked
    // (a wall, or the whole footprint doesn't fit) fall back to the other axis -- that is what lets
    // a chase slide along a corridor instead of getting stuck head-butting a corner.
    const bool xFirst = (dx > 0 ? dx : -dx) >= (dy > 0 ? dy : -dy);
    const int stepX = dx ? (dx > 0 ? 1 : -1) : 0;
    const int stepY = dy ? (dy > 0 ? 1 : -1) : 0;
    if (xFirst) {
        if (stepX && tryMove(g, x_ + stepX, y_, gw, gh)) return true;
        if (stepY && tryMove(g, x_, y_ + stepY, gw, gh)) return true;
    } else {
        if (stepY && tryMove(g, x_, y_ + stepY, gw, gh)) return true;
        if (stepX && tryMove(g, x_ + stepX, y_, gw, gh)) return true;
    }
    // Cornered: keep moving rather than freezing in place, so it can path around the obstacle.
    return wanderStep(g, gw, gh);
}

bool Roamer::wanderStep(const GridQuery& g, int gw, int gh) {
    static const int kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    // Prefer going straight: a roamer that keeps its heading reads as patrolling a corridor instead
    // of twitching, and it makes a chase predictable enough for the player to break line of sight.
    if (lastDir_ >= 0) {
        const int* d = kDirs[lastDir_];
        if (tryMove(g, x_ + d[0], y_ + d[1], gw, gh)) return true;
    }
    // Otherwise pick a random legal direction, starting from a rotating random offset so it doesn't
    // always favour +x. Deterministic per seed (rng_), so tests reproduce exactly.
    const int start = (int)(rnd() % 4u);
    for (int i = 0; i < 4; ++i) {
        const int idx = (start + i) % 4;
        const int* d = kDirs[idx];
        if (tryMove(g, x_ + d[0], y_ + d[1], gw, gh)) { lastDir_ = idx; return true; }
    }
    lastDir_ = -1;   // dead end: forget the heading so the next turn picks fresh
    return false;
}

bool Roamer::step(const GridQuery& grid, int playerX, int playerY, int gridW, int gridH) {
    ++turns_;
    const int dist = manhattan(x_, y_, playerX, playerY);
    // Hysteresis: start chasing inside the detect radius, but only give up past the lose radius, so
    // a player skating along the edge of the radius isn't treated to on/off toggling every turn.
    if (mode_ == Mode::Wander && dist <= kDetectRadius) mode_ = Mode::Chase;
    else if (mode_ == Mode::Chase && dist > kLoseRadius) mode_ = Mode::Wander;

    if (mode_ == Mode::Chase) {
        if (chaseStep(grid, playerX, playerY, gridW, gridH)) return true;
        return false;
    }
    return wanderStep(grid, gridW, gridH);
}

} // namespace toms
