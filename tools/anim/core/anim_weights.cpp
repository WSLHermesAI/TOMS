#include "anim_weights.h"

#include <algorithm>
#include <numeric>

using toms::anim::g_iSkinInfluences;

namespace animed {

namespace {

constexpr size_t g_nStride = 2 * g_iSkinInfluences;

// Vertex v's weights as one share per bone.
void f_Expand(const std::vector<float>& l_vWeights, size_t l_nV, int l_iBoneCount, std::vector<float>& l_vDense)
{
    l_vDense.assign(size_t(l_iBoneCount), 0.0f);
    for (int l_iK = 0; l_iK < g_iSkinInfluences; l_iK++) {
        const size_t l_nAt = l_nV * g_nStride + 2 * size_t(l_iK);
        if (l_nAt + 1 >= l_vWeights.size()) break;
        const int l_iB = int(l_vWeights[l_nAt]);
        const float l_fW = l_vWeights[l_nAt + 1];
        if (l_fW > 0 && l_iB >= 0 && l_iB < l_iBoneCount) l_vDense[size_t(l_iB)] += l_fW;
    }
}

// Back to (bone, weight) pairs: the strongest g_iSkinInfluences, scaled to sum to 1; none left = bone 0.
void f_Compact(const std::vector<float>& l_vDense, size_t l_nV, std::vector<float>& l_vWeights)
{
    std::vector<int> l_vOrder(l_vDense.size());
    std::iota(l_vOrder.begin(), l_vOrder.end(), 0);
    std::stable_sort(l_vOrder.begin(), l_vOrder.end(), [&](int l_iA, int l_iB) { return l_vDense[size_t(l_iA)] > l_vDense[size_t(l_iB)]; });
    float l_fSum = 0;
    int l_iKept = 0;
    for (int l_iB : l_vOrder) {
        if (l_iKept == g_iSkinInfluences || l_vDense[size_t(l_iB)] <= 1e-4f) break;
        l_fSum += l_vDense[size_t(l_iB)];
        l_iKept++;
    }
    for (int l_iK = 0; l_iK < g_iSkinInfluences; l_iK++) {
        const size_t l_nAt = l_nV * g_nStride + 2 * size_t(l_iK);
        if (l_iK < l_iKept) {
            l_vWeights[l_nAt] = float(l_vOrder[size_t(l_iK)]);
            l_vWeights[l_nAt + 1] = l_vDense[size_t(l_vOrder[size_t(l_iK)])] / l_fSum;
        } else {
            l_vWeights[l_nAt] = 0.0f;
            l_vWeights[l_nAt + 1] = (l_iKept == 0 && l_iK == 0) ? 1.0f : 0.0f;
        }
    }
}

// The bone's share set to `target`, the others scaled to fill the rest (keeping their proportions). False when the
// others have nothing to scale and the target is below 1 (Subtract with no other bone).
bool f_SetShare(std::vector<float>& l_vDense, int l_iBone, float l_fTarget)
{
    float l_fOthers = 0;
    for (size_t l_nB = 0; l_nB < l_vDense.size(); l_nB++)
        if (int(l_nB) != l_iBone) l_fOthers += l_vDense[l_nB];
    if (l_fOthers <= 1e-6f) {
        if (l_fTarget < 1.0f - 1e-6f) return false;
        l_vDense[size_t(l_iBone)] = 1.0f;
        return true;
    }
    const float l_fF = (1.0f - l_fTarget) / l_fOthers;
    for (size_t l_nB = 0; l_nB < l_vDense.size(); l_nB++)
        if (int(l_nB) != l_iBone) l_vDense[l_nB] *= l_fF;
    l_vDense[size_t(l_iBone)] = l_fTarget;
    return true;
}

}  // namespace

float f_BrushFalloff(float l_fDist, float l_fRadius)
{
    if (l_fRadius <= 0 || l_fDist >= l_fRadius) return 0.0f;
    const float l_fQ = 1.0f - (l_fDist / l_fRadius) * (l_fDist / l_fRadius);
    return l_fQ * l_fQ;
}

float f_BoneWeight(const std::vector<float>& l_vWeights, size_t l_nV, int l_iBone)
{
    float l_fW = 0;
    for (int l_iK = 0; l_iK < g_iSkinInfluences; l_iK++) {
        const size_t l_nAt = l_nV * g_nStride + 2 * size_t(l_iK);
        if (l_nAt + 1 >= l_vWeights.size()) break;
        if (int(l_vWeights[l_nAt]) == l_iBone) l_fW += l_vWeights[l_nAt + 1];
    }
    return l_fW;
}

void f_PaintWeights(std::vector<float>& l_vWeights, int l_iBoneCount, int l_iBone, WeightBrush l_eMode, const std::vector<float>& l_vAmount,
                  const std::vector<uint16_t>& l_vTriangles, const std::vector<float>* l_pFallback)
{
    const size_t l_nVerts = l_vAmount.size();
    if (l_iBoneCount <= 0 || l_iBone < 0 || l_iBone >= l_iBoneCount) return;
    if (l_vWeights.size() != l_nVerts * g_nStride) {   // none yet: every vertex on bone 0
        l_vWeights.assign(l_nVerts * g_nStride, 0.0f);
        for (size_t l_nV = 0; l_nV < l_nVerts; l_nV++) l_vWeights[l_nV * g_nStride + 1] = 1.0f;
    }

    std::vector<std::vector<uint16_t>> l_vNeighbours;
    if (l_eMode == WeightBrush::Smooth) {
        l_vNeighbours.resize(l_nVerts);
        for (size_t l_nT = 0; l_nT + 2 < l_vTriangles.size(); l_nT += 3)
            for (int l_iE = 0; l_iE < 3; l_iE++) {
                const uint16_t l_u16A = l_vTriangles[l_nT + size_t(l_iE)], l_u16B = l_vTriangles[l_nT + size_t((l_iE + 1) % 3)];
                if (l_u16A >= l_nVerts || l_u16B >= l_nVerts) continue;
                l_vNeighbours[l_u16A].push_back(l_u16B);
                l_vNeighbours[l_u16B].push_back(l_u16A);
            }
    }
    const std::vector<float> l_vBefore = l_vWeights;   // Smooth averages the weights as they were before this dab

    std::vector<float> l_vDense, l_vOther;
    for (size_t l_nV = 0; l_nV < l_nVerts; l_nV++) {
        const float l_fA = std::clamp(l_vAmount[l_nV], 0.0f, 1.0f);
        if (l_fA <= 0) continue;
        f_Expand(l_vBefore, l_nV, l_iBoneCount, l_vDense);
        const float l_fW = l_vDense[size_t(l_iBone)];
        switch (l_eMode) {
        case WeightBrush::Add:
            f_SetShare(l_vDense, l_iBone, l_fW + (1.0f - l_fW) * l_fA);
            break;
        case WeightBrush::Subtract: {
            const float l_fTarget = l_fW * (1.0f - l_fA);
            if (!f_SetShare(l_vDense, l_iBone, l_fTarget) && l_pFallback) {
                // Only this bone moves the vertex: the removed share goes to the fallback's other bones.
                f_Expand(*l_pFallback, l_nV, l_iBoneCount, l_vOther);
                l_vOther[size_t(l_iBone)] = 0.0f;
                const float l_fSum = std::accumulate(l_vOther.begin(), l_vOther.end(), 0.0f);
                if (l_fSum > 1e-6f) {
                    for (size_t l_nB = 0; l_nB < l_vDense.size(); l_nB++) l_vDense[l_nB] = l_vOther[l_nB] / l_fSum * (1.0f - l_fTarget);
                    l_vDense[size_t(l_iBone)] = l_fTarget;
                }
            }
            break;
        }
        case WeightBrush::Smooth: {
            if (l_vNeighbours[l_nV].empty()) break;
            std::vector<float> l_vAvg(size_t(l_iBoneCount), 0.0f);
            for (uint16_t l_u16N : l_vNeighbours[l_nV]) {
                f_Expand(l_vBefore, l_u16N, l_iBoneCount, l_vOther);
                for (size_t l_nB = 0; l_nB < l_vAvg.size(); l_nB++) l_vAvg[l_nB] += l_vOther[l_nB];
            }
            for (size_t l_nB = 0; l_nB < l_vAvg.size(); l_nB++) l_vDense[l_nB] += (l_vAvg[l_nB] / float(l_vNeighbours[l_nV].size()) - l_vDense[l_nB]) * l_fA;
            break;
        }
        }
        f_Compact(l_vDense, l_nV, l_vWeights);
    }
}

}  // namespace animed
