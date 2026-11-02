// anim_recipes.h -- motion recipes for meshes (docs/engine/06_ANIMATION_RECIPES.md section 7, docs/engine/16_MESH_ANIMATION.md step 5):
// short named motions that write the keys of a deformed or skinned node, so a new sprite or character gets decent
// motion without hand-keying, from the editor (Key > Motion Recipe) or a script / AI (`anim_editor --headless recipe`).
//
//   lattice recipes (any sprite; its deform lattice): breathe, sway, squash, jelly
//   bone recipes (a skinned node; the bones of its skin, rotated): bone_sway, nod, swing
//
// A recipe replaces the keys it writes between `start` and `start + period` and leaves the rest of the clip alone. No Qt.
#pragma once
#include "anim_clip.h"

#include <string>
#include <vector>

namespace animed {

struct MotionRecipe {
    std::string m_strId;            // "breathe"
    std::string m_strTitle;         // "Breathe"
    std::string m_strDescription;   // one line: what it does
    bool m_bBones = false;        // true: needs a skinned node (rotates its bones); false: the lattice
    float m_fPeriod = 1.0f;       // default length, seconds
};
// Every recipe, lattice ones first.
const std::vector<MotionRecipe>& f_MotionRecipes();
const MotionRecipe* f_FindMotionRecipe(const std::string& l_strId);

struct RecipeParams {
    float m_fStart = 0;           // seconds
    float m_fPeriod = 0;          // seconds; 0 = the recipe's default
    float m_fStrength = 1;        // 1 = the recipe's normal size; 0.5 subtle, 2 strong
    float m_fSpriteW = 0, m_fSpriteH = 0;   // the node's sprite, original pixels (lattice recipes size their offsets by it)
};

// Writes the recipe's keys: deform keys on `node` (lattice recipes), or rotation keys on the bones of node's skin, found
// in `clip` by name (bone recipes). The clip's length grows to fit when it has one. False and `err` when the node does
// not fit the recipe (no skin, no bone of the skin in the clip, no sprite size) or the id is unknown.
bool f_ApplyMotionRecipe(toms::anim::Clip& l_oClip, const std::vector<int>& l_vNodePath, const std::string& l_strId,
                       const RecipeParams& l_oParams, std::string* l_pErr);

// `anim_editor --headless recipe <file.anim> --node <name> --recipe <id> [--clip <name>] [--start s] [--period s]
// [--strength k] [--out <file.anim>]` writes the recipe into the file (or --out); `--headless recipe --list` lists them.
// The node's sprite size comes from the file's atlases. Exit 0 / 2 (the file, the node or the recipe did not work) / 3.
int f_HeadlessRecipe(const std::vector<std::string>& l_vArgs);

}  // namespace animed
