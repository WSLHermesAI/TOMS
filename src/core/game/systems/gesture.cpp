// gesture.cpp -- see gesture.h.
#include "gesture.h"

#include <algorithm>
#include <cmath>

namespace toms {

namespace {
constexpr float kPi = 3.14159265f;

std::vector<glm::vec2> ring(int n, float startDeg, int step) {   // n points on a circle, every `step`th, closed
    std::vector<glm::vec2> v;
    for (int k = 0; k <= n; k++) {
        const float a = (startDeg + 360.0f * (float)((k * step) % n) / (float)n) * kPi / 180.0f;
        v.push_back({0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)});
    }
    return v;
}
}  // namespace

std::vector<glm::vec2> shapeCorners(const std::string& name) {
    if (name == "pentagram") return ring(5, -90.0f, 2);   // the top, then every second point: a star in one stroke
    if (name == "triangle") return {{0.5f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {0.5f, 0.0f}};
    if (name == "square") return {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}};
    if (name == "circle") return ring(8, -90.0f, 1);
    if (name == "line") return {{0.0f, 0.5f}, {1.0f, 0.5f}};
    return {};
}

bool shapeClosed(const std::string& name) { return name == "pentagram" || name == "triangle" || name == "square" || name == "circle"; }

glm::vec2 shapePoint(const std::string& name, glm::vec2 centre, float size, float t) {
    const std::vector<glm::vec2> c = shapeCorners(name);
    if (c.empty()) return centre;
    auto at = [&](glm::vec2 p) { return centre + (p - glm::vec2(0.5f)) * size; };
    if (c.size() == 1) return at(c[0]);
    float total = 0.0f;
    for (size_t k = 1; k < c.size(); k++) total += glm::length(c[k] - c[k - 1]);
    float want = std::clamp(t, 0.0f, 1.0f) * total;
    for (size_t k = 1; k < c.size(); k++) {
        const float len = glm::length(c[k] - c[k - 1]);
        if (want <= len || k + 1 == c.size()) return at(c[k - 1] + (c[k] - c[k - 1]) * (len > 0.0f ? std::min(1.0f, want / len) : 1.0f));
        want -= len;
    }
    return at(c.back());
}

void ShapeTrace::start(const std::string& shape, glm::vec2 centre, float size, float cornerRadius) {
    *this = ShapeTrace{};
    for (glm::vec2 p : shapeCorners(shape)) pts_.push_back(centre + (p - glm::vec2(0.5f)) * size);
    radius_ = cornerRadius;
}

int ShapeTrace::pointer(int phase, float x, float y) {
    if (phase == 0) down_ = true;
    if (!down_) return 0;
    if (phase == 2) down_ = false;
    last_ = {x, y};
    const int before = steps();
    while (!done() && glm::length(last_ - pts_[(size_t)reached_]) <= radius_) reached_++;   // (a closed shape's end is its start again)
    return steps() - before;
}

bool matchStroke(const std::vector<glm::vec2>& stroke, const std::string& shape, float tolerance) {
    std::vector<glm::vec2> c = shapeCorners(shape);
    if (c.empty() || stroke.size() < 2) return false;
    glm::vec2 lo = stroke[0], hi = stroke[0];
    for (glm::vec2 p : stroke) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    const float side = std::max(hi.x - lo.x, hi.y - lo.y);
    if (side < 1.0f) return false;
    // the stroke in its own box: the longer side 1, the shorter kept in proportion and centred (a thin line stays thin)
    const glm::vec2 off = (glm::vec2(side) - (hi - lo)) * 0.5f;
    std::vector<glm::vec2> s;
    for (glm::vec2 p : stroke) s.push_back((p - lo + off) / side);
    auto passes = [&](const std::vector<glm::vec2>& corners) {   // each corner, in order, near a later point
        size_t from = 0;
        for (glm::vec2 k : corners) {
            size_t hit = s.size();
            for (size_t i = from; i < s.size(); i++)
                if (glm::length(s[i] - k) <= tolerance) { hit = i; break; }
            if (hit == s.size()) return false;
            from = hit;
        }
        return true;
    };
    if (shape == "circle")   // round: every point about as far from the middle (a triangle's corners stick out)
        for (glm::vec2 p : s)
            if (glm::length(p - glm::vec2(0.5f)) > 0.5f + tolerance || glm::length(p - glm::vec2(0.5f)) < 0.5f - 1.5f * tolerance) return false;
    if (!shapeClosed(shape)) {
        std::vector<glm::vec2> back(c.rbegin(), c.rend());
        return passes(c) || passes(back);
    }
    c.pop_back();   // a closed shape: try each corner as the start, both ways round
    const size_t n = c.size();
    for (size_t first = 0; first < n; first++)
        for (int dir : {1, -1}) {
            std::vector<glm::vec2> order;
            for (size_t k = 0; k <= n; k++) {
                const int idx = (((int)first + dir * (int)k) % (int)n + (int)n) % (int)n;
                order.push_back(c[(size_t)idx]);
            }
            if (passes(order)) return true;
        }
    return false;
}

} // namespace toms
