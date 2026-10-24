// anim_recipes.cpp -- see anim_recipes.h.
#include "anim_recipes.h"
#include "anim_check.h"
#include "anim_keys.h"
#include "anim_player.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>

namespace animed {

namespace fs = std::filesystem;
using toms::anim::Clip;
using toms::anim::Deform;
using toms::anim::Ease;
using toms::anim::Key;
using toms::anim::Node;

const std::vector<MotionRecipe>& motionRecipes() {
    static const std::vector<MotionRecipe> all = {
        {"breathe", "Breathe", "the top rises and widens a little, then settles (loop it for an idle)", false, 2.0f},
        {"sway", "Sway", "the top leans left and right, the feet stay (wind, a slow idle)", false, 2.0f},
        {"squash", "Squash", "squashed down and wide, then a stretch and a settle (a hit, a landing, a jump start)", false, 0.6f},
        {"jelly", "Jelly", "a wobble of squash and stretch that dies out (a slime, a hit on something soft)", false, 1.0f},
        {"bone_sway", "Bone sway", "every bone of the skin turns left and right, more toward the tip (a tail, a plant, a body)", true, 2.0f},
        {"nod", "Nod", "the last bone of the skin dips and comes back (a head)", true, 0.8f},
        {"swing", "Swing", "the bones wind up back, swing through, and return (an attack)", true, 0.7f},
    };
    return all;
}

const MotionRecipe* findMotionRecipe(const std::string& id) {
    for (const MotionRecipe& r : motionRecipes())
        if (r.id == id) return &r;
    return nullptr;
}

namespace {

Ease ease(const char* name) {
    Ease e;
    toms::anim::parseEase(name, e);
    return e;
}

// The keys between t0 and t1 replaced by `add` (sorted by time).
template <class T> void putKeys(std::vector<Key<T>>& keys, float t0, float t1, std::vector<Key<T>> add) {
    constexpr float eps = 1e-4f;
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const Key<T>& k) { return k.t >= t0 - eps && k.t <= t1 + eps; }),
               keys.end());
    keys.insert(keys.end(), add.begin(), add.end());
    std::sort(keys.begin(), keys.end(), [](const Key<T>& a, const Key<T>& b) { return a.t < b.t; });
}

// A lattice pose: every point's offset for "amount" v of the recipe's shape (0 = at rest). Points that do not move are
// left out of the key (a deform key lists only moved points).
using Shape = glm::vec2 (*)(float across, float up, float v, float w, float h);
//   across: -1 (left edge) .. 1 (right edge); up: 0 (bottom row) .. 1 (top row)

glm::vec2 breatheShape(float across, float up, float v, float w, float h) {
    return {across * 0.04f * w * up * v, -0.06f * h * up * v};   // the top rises 6 %, widening 4 % with height
}
glm::vec2 swayShape(float across, float up, float v, float w, float) {
    (void)across;
    return {0.15f * w * std::pow(up, 1.5f) * v, 0.0f};   // the top leans 15 % of the width
}
glm::vec2 squashShape(float across, float up, float v, float w, float h) {
    return {across * 0.12f * w * up * v, 0.2f * h * up * v};   // down 20 %, wide 12 % (v < 0: a stretch)
}

Deform latticePose(int cols, int rows, Shape shape, float v, float w, float h) {
    Deform d;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            const float across = cols > 1 ? 2.0f * c / (cols - 1) - 1.0f : 0.0f;
            const float up = rows > 1 ? 1.0f - float(r) / (rows - 1) : 1.0f;
            glm::vec2 o = shape(across, up, v, w, h);
            o = glm::round(o * 4.0f) / 4.0f;   // quarter pixels: tidy numbers in the file
            if (o != glm::vec2(0.0f)) d[r * cols + c] = o;
        }
    return d;
}

// (time 0..1 of the period, amount) pairs per lattice recipe.
std::vector<std::pair<float, float>> latticeCurve(const std::string& id) {
    if (id == "breathe") return {{0, 0}, {0.5f, 1}, {1, 0}};
    if (id == "sway") return {{0, 0}, {0.25f, 1}, {0.75f, -1}, {1, 0}};
    if (id == "squash") return {{0, 0}, {0.12f, 1}, {0.3f, -0.45f}, {0.5f, 0.15f}, {0.7f, -0.05f}, {1, 0}};
    if (id == "jelly") return {{0, 0}, {0.12f, 1}, {0.27f, -0.7f}, {0.42f, 0.45f}, {0.57f, -0.25f}, {0.75f, 0.1f}, {1, 0}};
    return {};
}

// (time 0..1, degrees at strength 1) per bone recipe; `tipOnly`: only the last bone moves.
std::vector<std::pair<float, float>> boneCurve(const std::string& id, bool& tipOnly) {
    tipOnly = id == "nod";
    if (id == "bone_sway") return {{0, 0}, {0.25f, 8}, {0.75f, -8}, {1, 0}};
    if (id == "nod") return {{0, 0}, {0.35f, 12}, {0.65f, -3}, {1, 0}};
    if (id == "swing") return {{0, 0}, {0.35f, -20}, {0.5f, 30}, {0.75f, 6}, {1, 0}};
    return {};
}

Node* findNodeMut(Node& n, const std::string& name) {
    if (n.name == name) return &n;
    for (Node& c : n.children)
        if (Node* f = findNodeMut(c, name)) return f;
    return nullptr;
}

}  // namespace

bool applyMotionRecipe(Clip& clip, const std::vector<int>& nodePath, const std::string& id, const RecipeParams& params,
                       std::string* err) {
    const MotionRecipe* recipe = findMotionRecipe(id);
    if (!recipe) {
        if (err) *err = "no recipe '" + id + "'";
        return false;
    }
    Node* node = nodeAt(clip.root, nodePath);
    if (!node) {
        if (err) *err = "no such node";
        return false;
    }
    const float period = params.period > 0 ? params.period : recipe->period;
    const float k = params.strength, t0 = std::max(0.0f, params.start), t1 = t0 + period;
    const Ease smooth = ease("sinusoidalInOut");
    if (!recipe->bones) {
        if (params.spriteW <= 0 || params.spriteH <= 0) {
            if (err) *err = "the node's sprite size is not known (no sprite, or it is in no atlas)";
            return false;
        }
        Shape shape = id == "breathe" ? breatheShape : id == "sway" ? swayShape : squashShape;   // squash and jelly share it
        std::vector<Key<Deform>> add;
        for (const auto& [f, v] : latticeCurve(id)) {
            Key<Deform> key;
            key.t = t0 + f * period;
            key.v = latticePose(node->latticeCols(), node->latticeRows(), shape, v * k, params.spriteW, params.spriteH);
            key.ease = smooth;
            add.push_back(std::move(key));
        }
        putKeys(node->deformKeys, t0, t1, std::move(add));
    } else {
        if (!node->skinned()) {
            if (err) *err = "'" + recipe->title + "' needs a skinned node (Properties > Skin)";
            return false;
        }
        std::vector<Node*> bones;
        for (const std::string& b : node->skinBones)
            if (Node* bn = findNodeMut(clip.root, b)) bones.push_back(bn);
        if (bones.empty()) {
            if (err) *err = "none of the skin's bones is a node of this clip";
            return false;
        }
        bool tipOnly = false;
        const auto curve = boneCurve(id, tipOnly);
        for (size_t i = 0; i < bones.size(); i++) {
            if (tipOnly && i + 1 != bones.size()) continue;
            const float share = tipOnly ? 1.0f : float(i + 1) / float(bones.size());   // more toward the tip
            Node& b = *bones[i];
            std::vector<Key<glm::vec3>> add;
            for (const auto& [f, deg] : curve) {
                Key<glm::vec3> key;
                key.t = t0 + f * period;
                key.v = b.rot + glm::vec3(0, 0, std::round(deg * k * share * 10.0f) / 10.0f);
                key.ease = smooth;
                add.push_back(key);
            }
            putKeys(b.rotKeys, t0, t1, std::move(add));
        }
    }
    if (clip.length > 0 && clip.length < t1) clip.length = t1;
    return true;
}

int headlessRecipe(const std::vector<std::string>& args) {
    // args: "recipe" ...
    if (args.size() >= 2 && args[1] == "--list") {
        for (const MotionRecipe& r : motionRecipes())
            std::printf("%-10s %-7s %4.1f s  %s\n", r.id.c_str(), r.bones ? "bones" : "lattice", r.period, r.description.c_str());
        return 0;
    }
    std::string file, out, clipName, nodeName, id;
    RecipeParams p;
    for (size_t i = 1; i < args.size(); i++) {
        const bool more = i + 1 < args.size();
        if (args[i] == "--node" && more) nodeName = args[++i];
        else if (args[i] == "--recipe" && more) id = args[++i];
        else if (args[i] == "--clip" && more) clipName = args[++i];
        else if (args[i] == "--start" && more) p.start = std::strtof(args[++i].c_str(), nullptr);
        else if (args[i] == "--period" && more) p.period = std::strtof(args[++i].c_str(), nullptr);
        else if (args[i] == "--strength" && more) p.strength = std::strtof(args[++i].c_str(), nullptr);
        else if (args[i] == "--out" && more) out = args[++i];
        else if (args[i].rfind("--", 0) != 0 && file.empty()) file = args[i];
        else { std::fprintf(stderr, "unknown argument: %s\n", args[i].c_str()); return 3; }
    }
    if (file.empty() || nodeName.empty() || id.empty()) {
        std::fputs("usage: anim_editor --headless recipe <file.anim> --node <name> --recipe <id> [--clip <name>] [--start s]\n"
                   "       [--period s] [--strength k] [--out <file.anim>]\n"
                   "       anim_editor --headless recipe --list\n"
                   "  writes a motion recipe's keys on the node (docs/engine/06_ANIMATION_RECIPES.md section 7)\n",
                   stderr);
        return 3;
    }
    std::string text, err;
    toms::anim::AnimFile f;
    if (!readTextFile(file, text) || !toms::anim::parseAnim(text, f, &err)) {
        std::fprintf(stderr, "%s: error: %s\n", file.c_str(), err.empty() ? "cannot read the file" : err.c_str());
        return 2;
    }
    Clip* clip = nullptr;
    for (Clip& c : f.clips)
        if (clipName.empty() ? !clip : c.name == clipName) clip = &c;
    if (!clip) { std::fprintf(stderr, "%s: error: no clip '%s'\n", file.c_str(), clipName.c_str()); return 2; }
    // The node's path, and its sprite's original size from the file's atlases.
    std::vector<int> path;
    std::function<bool(const Node&)> find = [&](const Node& n) {
        if (n.name == nodeName) return true;
        for (size_t i = 0; i < n.children.size(); i++) {
            path.push_back((int)i);
            if (find(n.children[i])) return true;
            path.pop_back();
        }
        return false;
    };
    if (!find(clip->root)) { std::fprintf(stderr, "%s: error: clip '%s' has no node '%s'\n", file.c_str(), clip->name.c_str(), nodeName.c_str()); return 2; }
    const Node* node = nodeAt(clip->root, path);
    std::vector<toms::AtlasFile> atlases(f.atlases.size());
    toms::anim::AtlasSet set;
    for (size_t i = 0; i < f.atlases.size(); i++) {
        const fs::path ap = isAbsoluteAtlasPath(f.atlases[i].path) ? fs::u8path(f.atlases[i].path)
                                                                   : fs::u8path(file).parent_path() / fs::u8path(f.atlases[i].path);
        std::string atext;
        if (readTextFile(ap.u8string(), atext) && toms::parseAtlas(atext, atlases[i], nullptr)) set.add(atlases[i], f.atlases[i].id);
    }
    const std::string sprite = toms::anim::spriteAt(*node, p.start);
    if (const toms::AtlasRegion* r = sprite.empty() ? nullptr : set.find(sprite)) {
        p.spriteW = (float)r->origW;
        p.spriteH = (float)r->origH;
    }
    if (!applyMotionRecipe(*clip, path, id, p, &err)) {
        std::fprintf(stderr, "%s: error: %s\n", file.c_str(), err.c_str());
        return 2;
    }
    const std::string target = out.empty() ? file : out;
    std::ofstream o(fs::u8path(target), std::ios::binary);
    o << toms::anim::animToJson(f);
    if (!o) { std::fprintf(stderr, "error: cannot write %s\n", target.c_str()); return 2; }
    std::printf("%s: '%s' written on %s/%s from %.2f s\n", target.c_str(), id.c_str(), clip->name.c_str(), nodeName.c_str(), p.start);
    return 0;
}

}  // namespace animed
