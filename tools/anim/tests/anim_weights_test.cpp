// Weight painting (tools/anim/core/anim_weights.h): every dab keeps each vertex summing to 1 with at most
// g_iSkinInfluences bones; Add, Subtract and Smooth move the shares the way the editor's brush promises.
#include "anim_weights.h"

#include <cmath>
#include <cstdio>

using animed::WeightBrush;
using toms::anim::g_iSkinInfluences;

namespace {

int g_iFailed = 0;

void f_Check(bool l_bOk, const char* l_pszWhat)
{
    if (!l_bOk) {
        std::printf("FAIL: %s\n", l_pszWhat);
        g_iFailed++;
    }
}

bool f_Near(float l_fA, float l_fB) { return std::fabs(l_fA - l_fB) < 1e-4f; }

bool f_SumsToOne(const std::vector<float>& l_vW, size_t l_nVerts)
{
    for (size_t l_nV = 0; l_nV < l_nVerts; l_nV++) {
        float l_fS = 0;
        for (int l_iK = 0; l_iK < g_iSkinInfluences; l_iK++) l_fS += l_vW[l_nV * 2 * g_iSkinInfluences + 2 * size_t(l_iK) + 1];
        if (!f_Near(l_fS, 1.0f)) return false;
    }
    return true;
}

}  // namespace

int main()
{
    // falloff: full at the centre, none at the edge, between in the middle
    f_Check(f_Near(animed::f_BrushFalloff(0, 10), 1.0f), "falloff 1 at the centre");
    f_Check(animed::f_BrushFalloff(10, 10) == 0.0f && animed::f_BrushFalloff(20, 10) == 0.0f, "falloff 0 at and past the edge");
    const float l_fMid = animed::f_BrushFalloff(5, 10);
    f_Check(l_fMid > 0.4f && l_fMid < 0.7f, "falloff about half way at half the radius");

    // a strip of 3 vertices: 0 and 1 share a triangle with 2
    const std::vector<uint16_t> l_vTris = {0, 1, 2};
    std::vector<float> l_vW;
    // no weights yet: all on bone 0; Add bone 1 fully on vertex 1 only
    animed::f_PaintWeights(l_vW, 3, 1, WeightBrush::Add, {0, 1, 0}, l_vTris);
    f_Check(l_vW.size() == 3 * 2 * g_iSkinInfluences, "weights made for every vertex");
    f_Check(f_Near(animed::f_BoneWeight(l_vW, 0, 0), 1) && f_Near(animed::f_BoneWeight(l_vW, 1, 1), 1), "Add 1: the vertex follows only that bone");
    f_Check(f_SumsToOne(l_vW, 3), "Add keeps the sums at 1");

    // Add half of bone 2 on vertex 0 (was all bone 0): 0.5 / 0.5
    animed::f_PaintWeights(l_vW, 3, 2, WeightBrush::Add, {0.5f, 0, 0}, l_vTris);
    f_Check(f_Near(animed::f_BoneWeight(l_vW, 0, 2), 0.5f) && f_Near(animed::f_BoneWeight(l_vW, 0, 0), 0.5f), "Add 0.5: half and half");

    // Subtract half of bone 2 on vertex 0: 0.25, bone 0 takes the rest
    animed::f_PaintWeights(l_vW, 3, 2, WeightBrush::Subtract, {0.5f, 0, 0}, l_vTris);
    f_Check(f_Near(animed::f_BoneWeight(l_vW, 0, 2), 0.25f) && f_Near(animed::f_BoneWeight(l_vW, 0, 0), 0.75f), "Subtract scales the others up");
    f_Check(f_SumsToOne(l_vW, 3), "Subtract keeps the sums at 1");

    // Subtract on a vertex with one bone: nothing without a fallback; with one, the fallback's bone takes the share
    animed::f_PaintWeights(l_vW, 3, 1, WeightBrush::Subtract, {0, 1, 0}, l_vTris);
    f_Check(f_Near(animed::f_BoneWeight(l_vW, 1, 1), 1), "Subtract with no other bone and no fallback leaves the vertex");
    std::vector<float> l_vFb(3 * 2 * g_iSkinInfluences, 0.0f);
    for (size_t l_nV = 0; l_nV < 3; l_nV++) { l_vFb[l_nV * 2 * g_iSkinInfluences] = 2; l_vFb[l_nV * 2 * g_iSkinInfluences + 1] = 1; }   // bone 2 nearest
    animed::f_PaintWeights(l_vW, 3, 1, WeightBrush::Subtract, {0, 0.5f, 0}, l_vTris, &l_vFb);
    f_Check(f_Near(animed::f_BoneWeight(l_vW, 1, 1), 0.5f) && f_Near(animed::f_BoneWeight(l_vW, 1, 2), 0.5f), "Subtract gives the share to the fallback's bone");

    // Smooth vertex 2 (all bone 0) fully: it becomes the average of 0 and 1
    animed::f_PaintWeights(l_vW, 3, 0, WeightBrush::Smooth, {0, 0, 1}, l_vTris);
    const float l_fB2 = (animed::f_BoneWeight(l_vW, 0, 2) + animed::f_BoneWeight(l_vW, 1, 2)) / 2;
    f_Check(f_Near(animed::f_BoneWeight(l_vW, 2, 2), l_fB2), "Smooth 1: the average of the neighbours");
    f_Check(f_SumsToOne(l_vW, 3), "Smooth keeps the sums at 1");

    // at most g_iSkinInfluences bones: add 6 bones in turn on one vertex
    std::vector<float> l_vMany;
    for (int l_iB = 0; l_iB < 6; l_iB++) animed::f_PaintWeights(l_vMany, 6, l_iB, WeightBrush::Add, {0.3f}, {});
    int l_iUsed = 0;
    for (int l_iB = 0; l_iB < 6; l_iB++) l_iUsed += animed::f_BoneWeight(l_vMany, 0, l_iB) > 0 ? 1 : 0;
    f_Check(l_iUsed <= g_iSkinInfluences, "never more than g_iSkinInfluences bones");
    f_Check(f_SumsToOne(l_vMany, 1), "the strongest kept, scaled to 1");
    f_Check(animed::f_BoneWeight(l_vMany, 0, 5) > 0, "the bone just painted is kept");

    if (g_iFailed == 0) std::printf("anim_weights_test: all passed\n");
    return g_iFailed == 0 ? 0 : 1;
}
