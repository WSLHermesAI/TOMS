// Weight painting (tools/anim/core/anim_weights.h): every dab keeps each vertex summing to 1 with at most
// kSkinInfluences bones; Add, Subtract and Smooth move the shares the way the editor's brush promises.
#include "anim_weights.h"

#include <cmath>
#include <cstdio>

using animed::WeightBrush;
using toms::anim::kSkinInfluences;

namespace {

int g_failed = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_failed++;
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

bool sumsToOne(const std::vector<float>& w, size_t verts)
{
    for (size_t v = 0; v < verts; v++) {
        float s = 0;
        for (int k = 0; k < kSkinInfluences; k++) s += w[v * 2 * kSkinInfluences + 2 * size_t(k) + 1];
        if (!near(s, 1.0f)) return false;
    }
    return true;
}

}  // namespace

int main()
{
    // falloff: full at the centre, none at the edge, between in the middle
    check(near(animed::brushFalloff(0, 10), 1.0f), "falloff 1 at the centre");
    check(animed::brushFalloff(10, 10) == 0.0f && animed::brushFalloff(20, 10) == 0.0f, "falloff 0 at and past the edge");
    const float mid = animed::brushFalloff(5, 10);
    check(mid > 0.4f && mid < 0.7f, "falloff about half way at half the radius");

    // a strip of 3 vertices: 0 and 1 share a triangle with 2
    const std::vector<uint16_t> tris = {0, 1, 2};
    std::vector<float> w;
    // no weights yet: all on bone 0; Add bone 1 fully on vertex 1 only
    animed::paintWeights(w, 3, 1, WeightBrush::Add, {0, 1, 0}, tris);
    check(w.size() == 3 * 2 * kSkinInfluences, "weights made for every vertex");
    check(near(animed::boneWeight(w, 0, 0), 1) && near(animed::boneWeight(w, 1, 1), 1), "Add 1: the vertex follows only that bone");
    check(sumsToOne(w, 3), "Add keeps the sums at 1");

    // Add half of bone 2 on vertex 0 (was all bone 0): 0.5 / 0.5
    animed::paintWeights(w, 3, 2, WeightBrush::Add, {0.5f, 0, 0}, tris);
    check(near(animed::boneWeight(w, 0, 2), 0.5f) && near(animed::boneWeight(w, 0, 0), 0.5f), "Add 0.5: half and half");

    // Subtract half of bone 2 on vertex 0: 0.25, bone 0 takes the rest
    animed::paintWeights(w, 3, 2, WeightBrush::Subtract, {0.5f, 0, 0}, tris);
    check(near(animed::boneWeight(w, 0, 2), 0.25f) && near(animed::boneWeight(w, 0, 0), 0.75f), "Subtract scales the others up");
    check(sumsToOne(w, 3), "Subtract keeps the sums at 1");

    // Subtract on a vertex with one bone: nothing without a fallback; with one, the fallback's bone takes the share
    animed::paintWeights(w, 3, 1, WeightBrush::Subtract, {0, 1, 0}, tris);
    check(near(animed::boneWeight(w, 1, 1), 1), "Subtract with no other bone and no fallback leaves the vertex");
    std::vector<float> fb(3 * 2 * kSkinInfluences, 0.0f);
    for (size_t v = 0; v < 3; v++) { fb[v * 2 * kSkinInfluences] = 2; fb[v * 2 * kSkinInfluences + 1] = 1; }   // bone 2 nearest
    animed::paintWeights(w, 3, 1, WeightBrush::Subtract, {0, 0.5f, 0}, tris, &fb);
    check(near(animed::boneWeight(w, 1, 1), 0.5f) && near(animed::boneWeight(w, 1, 2), 0.5f), "Subtract gives the share to the fallback's bone");

    // Smooth vertex 2 (all bone 0) fully: it becomes the average of 0 and 1
    animed::paintWeights(w, 3, 0, WeightBrush::Smooth, {0, 0, 1}, tris);
    const float b2 = (animed::boneWeight(w, 0, 2) + animed::boneWeight(w, 1, 2)) / 2;
    check(near(animed::boneWeight(w, 2, 2), b2), "Smooth 1: the average of the neighbours");
    check(sumsToOne(w, 3), "Smooth keeps the sums at 1");

    // at most kSkinInfluences bones: add 6 bones in turn on one vertex
    std::vector<float> many;
    for (int b = 0; b < 6; b++) animed::paintWeights(many, 6, b, WeightBrush::Add, {0.3f}, {});
    int used = 0;
    for (int b = 0; b < 6; b++) used += animed::boneWeight(many, 0, b) > 0 ? 1 : 0;
    check(used <= kSkinInfluences, "never more than kSkinInfluences bones");
    check(sumsToOne(many, 1), "the strongest kept, scaled to 1");
    check(animed::boneWeight(many, 0, 5) > 0, "the bone just painted is kept");

    if (g_failed == 0) std::printf("anim_weights_test: all passed\n");
    return g_failed == 0 ? 0 : 1;
}
