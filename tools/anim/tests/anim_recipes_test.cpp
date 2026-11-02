// anim_recipes_test.cpp -- the motion recipes (anim_recipes.h; docs/engine/06_ANIMATION_RECIPES.md section 7): the lattice
// recipes' keys and shapes, the bone recipes on a skin's bones, what they replace and keep, and what they refuse.
#include "anim_recipes.h"

#include <cmath>
#include <cstdio>

using namespace toms::anim;

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_iFail; } } while (0)

// root > slime (a sprite, 32 x 32) and root > body (skinned to hip > chest > head).
Clip f_Sample() {
    Clip l_oC;
    l_oC.m_strName = "idle";
    l_oC.m_fLength = 1;
    l_oC.m_oRoot.m_strName = "root";
    Node l_oSlime;
    l_oSlime.m_strName = "slime";
    l_oSlime.m_strSprite = "slime";
    Node l_oBody;
    l_oBody.m_strName = "body";
    l_oBody.m_strSprite = "golem";
    l_oBody.m_vSkinBones = {"hip", "chest", "head"};
    Node l_oHip, l_oChest, l_oHead;
    l_oHip.m_strName = "hip";
    l_oChest.m_strName = "chest";
    l_oHead.m_strName = "head";
    l_oHead.m_v3Rot = {0, 0, 5};
    l_oChest.m_vChildren.push_back(l_oHead);
    l_oHip.m_vChildren.push_back(l_oChest);
    l_oBody.m_vChildren.push_back(l_oHip);
    l_oC.m_oRoot.m_vChildren = {l_oSlime, l_oBody};
    return l_oC;
}
}  // namespace

int main() {
    CHECK(animed::f_MotionRecipes().size() == 7 && animed::f_FindMotionRecipe("breathe") && !animed::f_FindMotionRecipe("nope"),
          "seven recipes, found by id");
    animed::RecipeParams l_oP;
    l_oP.m_fSpriteW = 32;
    l_oP.m_fSpriteH = 32;
    std::string l_strErr;

    // ---- breathe on the lattice ----
    Clip l_oC = f_Sample();
    Key<Deform> l_oBefore, l_oAfter;
    l_oBefore.m_fT = 0.5f;   // inside the range: replaced
    l_oAfter.m_fT = 5;       // outside: kept
    l_oC.m_oRoot.m_vChildren[0].m_vDeformKeys = {l_oBefore, l_oAfter};
    l_oP.m_fPeriod = 2;
    CHECK(animed::f_ApplyMotionRecipe(l_oC, {0}, "breathe", l_oP, &l_strErr), "breathe on the slime");
    const auto& l_vDk = l_oC.m_oRoot.m_vChildren[0].m_vDeformKeys;
    CHECK(l_vDk.size() == 4 && l_vDk[0].m_fT == 0 && l_vDk[1].m_fT == 1 && l_vDk[2].m_fT == 2 && l_vDk[3].m_fT == 5, "keys at 0, 1, 2 s; the key at 5 s kept");
    CHECK(l_vDk[0].m_oV.empty() && l_vDk[2].m_oV.empty(), "at rest at the start and the end");
    // 4 x 4: point 1 (top row) rises 6 % of 32 = 1.92 px (rounded to a quarter); the bottom row (12 .. 15) stays.
    CHECK(l_vDk[1].m_oV.count(1) && l_vDk[1].m_oV.at(1).y < -1.5f && !l_vDk[1].m_oV.count(12) && !l_vDk[1].m_oV.count(15), "the top rises, the feet stay");
    CHECK(l_vDk[1].m_oV.at(0).x < 0 && l_vDk[1].m_oV.at(3).x > 0, "and widens");
    CHECK(l_oC.m_fLength == 2, "the clip's length grows to fit");

    // ---- squash and jelly: a squash then a stretch ----
    l_oC = f_Sample();
    l_oP.m_fPeriod = 0;   // the recipe's own (0.6 s)
    CHECK(animed::f_ApplyMotionRecipe(l_oC, {0}, "squash", l_oP, &l_strErr), "squash");
    const auto& l_vSk = l_oC.m_oRoot.m_vChildren[0].m_vDeformKeys;
    CHECK(l_vSk.size() == 6 && std::fabs(l_vSk.back().m_fT - 0.6f) < 1e-4f, "squash: six keys over its own 0.6 s");
    CHECK(l_vSk[1].m_oV.at(1).y > 0 && l_vSk[2].m_oV.at(1).y < 0, "down first, then a stretch up");
    l_oP.m_fStrength = 2;
    CHECK(animed::f_ApplyMotionRecipe(l_oC, {0}, "jelly", l_oP, &l_strErr) && l_oC.m_oRoot.m_vChildren[0].m_vDeformKeys.size() == 7, "jelly replaces it");
    l_oP.m_fStrength = 1;

    // ---- sway: the top leans, both ways ----
    l_oC = f_Sample();
    CHECK(animed::f_ApplyMotionRecipe(l_oC, {0}, "sway", l_oP, &l_strErr), "sway");
    const auto& l_vWk = l_oC.m_oRoot.m_vChildren[0].m_vDeformKeys;
    CHECK(l_vWk.size() == 4 && l_vWk[1].m_oV.at(0).x > 0 && l_vWk[2].m_oV.at(0).x < 0 && l_vWk[1].m_oV.at(0).y == 0, "right, then left, only across");

    // ---- bone recipes ----
    l_oC = f_Sample();
    CHECK(animed::f_ApplyMotionRecipe(l_oC, {1}, "bone_sway", l_oP, &l_strErr), "bone_sway on the skinned body");
    const Node* l_pHip = f_FindNode(l_oC.m_oRoot, "hip");
    const Node* l_pHead = f_FindNode(l_oC.m_oRoot, "head");
    CHECK(l_pHip && l_pHead && l_pHip->m_vRotKeys.size() == 4 && l_pHead->m_vRotKeys.size() == 4, "every bone keyed");
    CHECK(std::fabs(l_pHead->m_vRotKeys[1].m_oV.z - (5 + 8)) < 0.05f && std::fabs(l_pHip->m_vRotKeys[1].m_oV.z - 8.0f / 3) < 0.05f,
          "the tip turns the most; from the bone's own rest angle");
    l_oC = f_Sample();
    CHECK(animed::f_ApplyMotionRecipe(l_oC, {1}, "nod", l_oP, &l_strErr) && f_FindNode(l_oC.m_oRoot, "head")->m_vRotKeys.size() == 4 &&
              f_FindNode(l_oC.m_oRoot, "hip")->m_vRotKeys.empty(),
          "nod: only the last bone");

    // ---- refused ----
    l_oC = f_Sample();
    CHECK(!animed::f_ApplyMotionRecipe(l_oC, {0}, "swing", l_oP, &l_strErr) && l_strErr.find("skinned") != std::string::npos, "a bone recipe needs a skin");
    animed::RecipeParams l_oNoSize;
    CHECK(!animed::f_ApplyMotionRecipe(l_oC, {0}, "breathe", l_oNoSize, &l_strErr), "a lattice recipe needs the sprite's size");
    CHECK(!animed::f_ApplyMotionRecipe(l_oC, {0}, "dance", l_oP, &l_strErr), "an unknown recipe");
    CHECK(!animed::f_ApplyMotionRecipe(l_oC, {7}, "breathe", l_oP, &l_strErr), "no such node");

    // ---- the file it makes reads back ----
    l_oC = f_Sample();
    animed::f_ApplyMotionRecipe(l_oC, {0}, "jelly", l_oP, &l_strErr);
    animed::f_ApplyMotionRecipe(l_oC, {1}, "swing", l_oP, &l_strErr);
    AnimFile l_oF;
    l_oF.m_vClips.push_back(l_oC);
    AnimFile l_oBack;
    CHECK(f_ParseAnim(f_AnimToJson(l_oF), l_oBack, &l_strErr) && l_oBack == l_oF, "the keys written read back the same");

    if (g_iFail) { std::printf("anim_recipes_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("anim_recipes_test: OK\n");
    return 0;
}
