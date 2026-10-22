// anim_recipes_test.cpp -- the motion recipes (anim_recipes.h; docs/16_ANIMATION_RECIPES.md section 7): the lattice
// recipes' keys and shapes, the bone recipes on a skin's bones, what they replace and keep, and what they refuse.
#include "anim_recipes.h"

#include <cmath>
#include <cstdio>

using namespace toms::anim;

namespace {
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)

// root > slime (a sprite, 32 x 32) and root > body (skinned to hip > chest > head).
Clip sample() {
    Clip c;
    c.name = "idle";
    c.length = 1;
    c.root.name = "root";
    Node slime;
    slime.name = "slime";
    slime.sprite = "slime";
    Node body;
    body.name = "body";
    body.sprite = "golem";
    body.skinBones = {"hip", "chest", "head"};
    Node hip, chest, head;
    hip.name = "hip";
    chest.name = "chest";
    head.name = "head";
    head.rot = {0, 0, 5};
    chest.children.push_back(head);
    hip.children.push_back(chest);
    body.children.push_back(hip);
    c.root.children = {slime, body};
    return c;
}
}  // namespace

int main() {
    CHECK(animed::motionRecipes().size() == 7 && animed::findMotionRecipe("breathe") && !animed::findMotionRecipe("nope"),
          "seven recipes, found by id");
    animed::RecipeParams p;
    p.spriteW = 32;
    p.spriteH = 32;
    std::string err;

    // ---- breathe on the lattice ----
    Clip c = sample();
    Key<Deform> before, after;
    before.t = 0.5f;   // inside the range: replaced
    after.t = 5;       // outside: kept
    c.root.children[0].deformKeys = {before, after};
    p.period = 2;
    CHECK(animed::applyMotionRecipe(c, {0}, "breathe", p, &err), "breathe on the slime");
    const auto& dk = c.root.children[0].deformKeys;
    CHECK(dk.size() == 4 && dk[0].t == 0 && dk[1].t == 1 && dk[2].t == 2 && dk[3].t == 5, "keys at 0, 1, 2 s; the key at 5 s kept");
    CHECK(dk[0].v.empty() && dk[2].v.empty(), "at rest at the start and the end");
    // 4 x 4: point 1 (top row) rises 6 % of 32 = 1.92 px (rounded to a quarter); the bottom row (12 .. 15) stays.
    CHECK(dk[1].v.count(1) && dk[1].v.at(1).y < -1.5f && !dk[1].v.count(12) && !dk[1].v.count(15), "the top rises, the feet stay");
    CHECK(dk[1].v.at(0).x < 0 && dk[1].v.at(3).x > 0, "and widens");
    CHECK(c.length == 2, "the clip's length grows to fit");

    // ---- squash and jelly: a squash then a stretch ----
    c = sample();
    p.period = 0;   // the recipe's own (0.6 s)
    CHECK(animed::applyMotionRecipe(c, {0}, "squash", p, &err), "squash");
    const auto& sk = c.root.children[0].deformKeys;
    CHECK(sk.size() == 6 && std::fabs(sk.back().t - 0.6f) < 1e-4f, "squash: six keys over its own 0.6 s");
    CHECK(sk[1].v.at(1).y > 0 && sk[2].v.at(1).y < 0, "down first, then a stretch up");
    p.strength = 2;
    CHECK(animed::applyMotionRecipe(c, {0}, "jelly", p, &err) && c.root.children[0].deformKeys.size() == 7, "jelly replaces it");
    p.strength = 1;

    // ---- sway: the top leans, both ways ----
    c = sample();
    CHECK(animed::applyMotionRecipe(c, {0}, "sway", p, &err), "sway");
    const auto& wk = c.root.children[0].deformKeys;
    CHECK(wk.size() == 4 && wk[1].v.at(0).x > 0 && wk[2].v.at(0).x < 0 && wk[1].v.at(0).y == 0, "right, then left, only across");

    // ---- bone recipes ----
    c = sample();
    CHECK(animed::applyMotionRecipe(c, {1}, "bone_sway", p, &err), "bone_sway on the skinned body");
    const Node* hip = findNode(c.root, "hip");
    const Node* head = findNode(c.root, "head");
    CHECK(hip && head && hip->rotKeys.size() == 4 && head->rotKeys.size() == 4, "every bone keyed");
    CHECK(std::fabs(head->rotKeys[1].v.z - (5 + 8)) < 0.05f && std::fabs(hip->rotKeys[1].v.z - 8.0f / 3) < 0.05f,
          "the tip turns the most; from the bone's own rest angle");
    c = sample();
    CHECK(animed::applyMotionRecipe(c, {1}, "nod", p, &err) && findNode(c.root, "head")->rotKeys.size() == 4 &&
              findNode(c.root, "hip")->rotKeys.empty(),
          "nod: only the last bone");

    // ---- refused ----
    c = sample();
    CHECK(!animed::applyMotionRecipe(c, {0}, "swing", p, &err) && err.find("skinned") != std::string::npos, "a bone recipe needs a skin");
    animed::RecipeParams noSize;
    CHECK(!animed::applyMotionRecipe(c, {0}, "breathe", noSize, &err), "a lattice recipe needs the sprite's size");
    CHECK(!animed::applyMotionRecipe(c, {0}, "dance", p, &err), "an unknown recipe");
    CHECK(!animed::applyMotionRecipe(c, {7}, "breathe", p, &err), "no such node");

    // ---- the file it makes reads back ----
    c = sample();
    animed::applyMotionRecipe(c, {0}, "jelly", p, &err);
    animed::applyMotionRecipe(c, {1}, "swing", p, &err);
    AnimFile f;
    f.clips.push_back(c);
    AnimFile back;
    CHECK(parseAnim(animToJson(f), back, &err) && back == f, "the keys written read back the same");

    if (g_fail) { std::printf("anim_recipes_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("anim_recipes_test: OK\n");
    return 0;
}
