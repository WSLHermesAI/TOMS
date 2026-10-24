#include "anim_weights.h"

#include <algorithm>
#include <numeric>

using toms::anim::kSkinInfluences;

namespace animed {

namespace {

constexpr size_t kStride = 2 * kSkinInfluences;

// Vertex v's weights as one share per bone.
void expand(const std::vector<float>& weights, size_t v, int boneCount, std::vector<float>& dense)
{
    dense.assign(size_t(boneCount), 0.0f);
    for (int k = 0; k < kSkinInfluences; k++) {
        const size_t at = v * kStride + 2 * size_t(k);
        if (at + 1 >= weights.size()) break;
        const int b = int(weights[at]);
        const float w = weights[at + 1];
        if (w > 0 && b >= 0 && b < boneCount) dense[size_t(b)] += w;
    }
}

// Back to (bone, weight) pairs: the strongest kSkinInfluences, scaled to sum to 1; none left = bone 0.
void compact(const std::vector<float>& dense, size_t v, std::vector<float>& weights)
{
    std::vector<int> order(dense.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return dense[size_t(a)] > dense[size_t(b)]; });
    float sum = 0;
    int kept = 0;
    for (int b : order) {
        if (kept == kSkinInfluences || dense[size_t(b)] <= 1e-4f) break;
        sum += dense[size_t(b)];
        kept++;
    }
    for (int k = 0; k < kSkinInfluences; k++) {
        const size_t at = v * kStride + 2 * size_t(k);
        if (k < kept) {
            weights[at] = float(order[size_t(k)]);
            weights[at + 1] = dense[size_t(order[size_t(k)])] / sum;
        } else {
            weights[at] = 0.0f;
            weights[at + 1] = (kept == 0 && k == 0) ? 1.0f : 0.0f;
        }
    }
}

// The bone's share set to `target`, the others scaled to fill the rest (keeping their proportions). False when the
// others have nothing to scale and the target is below 1 (Subtract with no other bone).
bool setShare(std::vector<float>& dense, int bone, float target)
{
    float others = 0;
    for (size_t b = 0; b < dense.size(); b++)
        if (int(b) != bone) others += dense[b];
    if (others <= 1e-6f) {
        if (target < 1.0f - 1e-6f) return false;
        dense[size_t(bone)] = 1.0f;
        return true;
    }
    const float f = (1.0f - target) / others;
    for (size_t b = 0; b < dense.size(); b++)
        if (int(b) != bone) dense[b] *= f;
    dense[size_t(bone)] = target;
    return true;
}

}  // namespace

float brushFalloff(float dist, float radius)
{
    if (radius <= 0 || dist >= radius) return 0.0f;
    const float q = 1.0f - (dist / radius) * (dist / radius);
    return q * q;
}

float boneWeight(const std::vector<float>& weights, size_t v, int bone)
{
    float w = 0;
    for (int k = 0; k < kSkinInfluences; k++) {
        const size_t at = v * kStride + 2 * size_t(k);
        if (at + 1 >= weights.size()) break;
        if (int(weights[at]) == bone) w += weights[at + 1];
    }
    return w;
}

void paintWeights(std::vector<float>& weights, int boneCount, int bone, WeightBrush mode, const std::vector<float>& amount,
                  const std::vector<uint16_t>& triangles, const std::vector<float>* fallback)
{
    const size_t verts = amount.size();
    if (boneCount <= 0 || bone < 0 || bone >= boneCount) return;
    if (weights.size() != verts * kStride) {   // none yet: every vertex on bone 0
        weights.assign(verts * kStride, 0.0f);
        for (size_t v = 0; v < verts; v++) weights[v * kStride + 1] = 1.0f;
    }

    std::vector<std::vector<uint16_t>> neighbours;
    if (mode == WeightBrush::Smooth) {
        neighbours.resize(verts);
        for (size_t t = 0; t + 2 < triangles.size(); t += 3)
            for (int e = 0; e < 3; e++) {
                const uint16_t a = triangles[t + size_t(e)], b = triangles[t + size_t((e + 1) % 3)];
                if (a >= verts || b >= verts) continue;
                neighbours[a].push_back(b);
                neighbours[b].push_back(a);
            }
    }
    const std::vector<float> before = weights;   // Smooth averages the weights as they were before this dab

    std::vector<float> dense, other;
    for (size_t v = 0; v < verts; v++) {
        const float a = std::clamp(amount[v], 0.0f, 1.0f);
        if (a <= 0) continue;
        expand(before, v, boneCount, dense);
        const float w = dense[size_t(bone)];
        switch (mode) {
        case WeightBrush::Add:
            setShare(dense, bone, w + (1.0f - w) * a);
            break;
        case WeightBrush::Subtract: {
            const float target = w * (1.0f - a);
            if (!setShare(dense, bone, target) && fallback) {
                // Only this bone moves the vertex: the removed share goes to the fallback's other bones.
                expand(*fallback, v, boneCount, other);
                other[size_t(bone)] = 0.0f;
                const float sum = std::accumulate(other.begin(), other.end(), 0.0f);
                if (sum > 1e-6f) {
                    for (size_t b = 0; b < dense.size(); b++) dense[b] = other[b] / sum * (1.0f - target);
                    dense[size_t(bone)] = target;
                }
            }
            break;
        }
        case WeightBrush::Smooth: {
            if (neighbours[v].empty()) break;
            std::vector<float> avg(size_t(boneCount), 0.0f);
            for (uint16_t n : neighbours[v]) {
                expand(before, n, boneCount, other);
                for (size_t b = 0; b < avg.size(); b++) avg[b] += other[b];
            }
            for (size_t b = 0; b < avg.size(); b++) dense[b] += (avg[b] / float(neighbours[v].size()) - dense[b]) * a;
            break;
        }
        }
        compact(dense, v, weights);
    }
}

}  // namespace animed
