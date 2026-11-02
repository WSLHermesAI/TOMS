// anim_weights.h -- painting a skin's weights (docs/engine/16_MESH_ANIMATION.md step 5 part 3): what the anim editor's Weights
// mode (P) does to the weights of the vertices under its brush. No Qt.
//
// Weights are laid out as Node::skinWeights and SkinView::weights: kSkinInfluences (bone, weight) pairs per vertex, the
// bone an index into the node's skinBones. A dab changes one bone's share of each vertex by that vertex's `amount`
// (0 = untouched, 1 = all the way); every vertex it touches still sums to 1 and follows at most kSkinInfluences bones
// (the strongest are kept).
#pragma once
#include "anim_clip.h"

#include <cstdint>
#include <vector>

namespace animed {

enum class WeightBrush {
    Add,        // the bone's share grows towards 1; the others shrink in proportion
    Subtract,   // the bone's share shrinks towards 0; the others grow in proportion (or `fallback`'s bones take it)
    Smooth,     // every bone's share moves towards the average of the vertex's neighbours (the mesh's triangles)
};

// How much of a dab reaches a point `dist` from the brush centre: 1 at the centre, 0 at `radius` and beyond, smooth
// in between ((1 - (d/r)^2)^2).
float f_BrushFalloff(float l_fDist, float l_fRadius);

// Bone `bone`'s weight on vertex v (0 when the vertex does not follow it).
float f_BoneWeight(const std::vector<float>& l_vWeights, size_t l_nV, int l_iBone);

// One dab. weights: the skin's weights (resized to fit `amount` when they do not); boneCount: the skin's bones;
// bone: the painted one (Add / Subtract); amount: per vertex; triangles: the mesh (Smooth's neighbours). fallback
// (optional, same layout): the weights a vertex takes the removed share from when Subtract finds no other bone on it
// (the editor passes the nearest other bone). A vertex left with no weight at all goes to bone 0.
void f_PaintWeights(std::vector<float>& l_vWeights, int l_iBoneCount, int l_iBone, WeightBrush l_eMode, const std::vector<float>& l_vAmount,
                  const std::vector<uint16_t>& l_vTriangles, const std::vector<float>* l_pFallback = nullptr);

}  // namespace animed
