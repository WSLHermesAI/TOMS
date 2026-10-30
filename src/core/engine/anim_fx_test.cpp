// anim_fx_test.cpp -- node effects and trails (anim_clip.h fx / trail, anim_player.h nodeLayerEffect / appendTrail;
// docs/engine/18_NODE_EFFECTS.md): the file (version 4 only when used, the errors), each pose's subtree range, a layer
// opened before a node's subtree and closed after it (nested ones inside), no layer when the effect shows nothing, the
// params at the node's time, and the afterimages. No GPU.
#include "anim_player.h"

#include <cmath>
#include <cstdio>

using namespace toms::anim;

namespace {
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

Node sprite(const char* name, float x = 0) {
    Node n;
    n.name = name;
    n.sprite = "box";
    n.pos = {x, 0, 0};
    return n;
}

// root ( back(order -1), arm [fx] ( upper, hand [fx] ( fist ) ), front )
Clip rig() {
    Clip c;
    c.name = "rig";
    c.root.name = "root";
    Node back = sprite("back");
    back.order = -1;
    Node arm;
    arm.name = "arm";
    arm.fx.shader = FxShader::MotionBlur;
    arm.fx.params = {0, 0, 0, 0};
    Key<glm::vec4> k0, k1;
    k0.v = {0, 0, 0, 0};
    k1.t = 1;
    k1.v = {40, 0, 0, 0};
    arm.fxKeys = {k0, k1};
    Node hand;
    hand.name = "hand";
    hand.fx.shader = FxShader::Glow;
    hand.fx.params = {2, 10, 0.5f, 1};
    hand.fx.color = {1, 0.5f, 0.2f, 1};
    hand.children.push_back(sprite("fist", 20));
    arm.children.push_back(sprite("upper", 10));
    arm.children.push_back(hand);
    c.root.children = {back, arm, sprite("front", 30)};
    return c;
}

size_t poseOf(const std::vector<NodePose>& p, const char* name) {
    for (size_t i = 0; i < p.size(); i++)
        if (p[i].node->name == name) return i;
    return SIZE_MAX;
}

// The draw list as a string: q = a quad, [ = a layer begins (its kind), ] = it ends.
std::string shape(const DrawList& d) {
    std::string s;
    for (const DrawList::Item& it : d.order) {
        if (it.kind == DrawList::QuadItem) s += 'q';
        else if (it.kind == DrawList::MeshItemKind) s += 'm';
        else if (it.kind == DrawList::BeginLayer) s += "[" + std::to_string(int(d.layers[it.index].kind));
        else s += ']';
    }
    return s;
}
}  // namespace

int main() {
    // ---- the file ----
    AnimFile f;
    f.clips.push_back(rig());
    f.clips[0].root.children[2].trail.count = 3;
    f.clips[0].root.children[2].trail.gap = 0.1f;
    f.clips[0].root.children[2].trail.additive = true;
    Key<float> tk;
    tk.t = 0.5f;
    tk.v = 0.25f;
    f.clips[0].root.children[2].trailKeys = {tk};
    std::string json = animToJson(f), err;
    CHECK(json.find("\"version\": 4") != std::string::npos && json.find("\"motionBlur\"") != std::string::npos &&
              json.find("\"trail\"") != std::string::npos, "effects and trails: version 4");
    AnimFile back;
    CHECK(parseAnim(json, back, &err) && back == f, "they read back the same");
    AnimFile none;
    none.clips.push_back(Clip());
    CHECK(animToJson(none).find("\"version\": 2") != std::string::npos, "a file without them stays version 2");
    CHECK(!parseAnim(R"({"version":4,"clips":[{"name":"c","root":{"name":"n","fx":{"shader":"sparkle"}}}]})", back, &err),
          "an unknown shader");
    CHECK(!parseAnim(R"({"version":4,"clips":[{"name":"c","root":{"name":"n","trail":{"count":17}}}]})", back, &err),
          "a trail over 16 afterimages");
    CHECK(!parseAnim(R"({"version":5,"clips":[]})", back, &err), "a newer version");

    // ---- subtree ranges ----
    const Clip c = rig();
    std::vector<NodePose> poses;
    evaluate(c, 0.5f, poses);
    const size_t arm = poseOf(poses, "arm"), hand = poseOf(poses, "hand"), root = poseOf(poses, "root");
    CHECK(poses.size() == 7 && poses[root].first == 0 && poses[root].end == 7, "the root's subtree: every pose");
    CHECK(poses[arm].end - poses[arm].first == 4 && poses[poses[arm].first].node->name == "arm", "the arm: itself, upper, hand, fist");
    CHECK(poses[hand].first == hand && poses[hand].end == hand + 2, "the hand: itself and the fist");
    CHECK(near(poses[arm].clipTime, 0.5f), "the clip's time in every pose");
    PoseCache cache;
    cache.bind(&c);
    cache.seek(0.5f);
    bool same = cache.poses().size() == poses.size();
    for (size_t i = 0; same && i < poses.size(); i++)
        same = cache.poses()[i].first == poses[i].first && cache.poses()[i].end == poses[i].end;
    CHECK(same, "PoseCache gives the same ranges");

    // ---- layers around subtrees ----
    toms::AtlasFile atlas;
    toms::parseAtlas("page.png\nsize: 64, 64\nbox\n  bounds: 0, 0, 16, 16\n", atlas, &err);
    AtlasSet set(atlas);
    DrawList d;
    appendDraws(c, poses, set, glm::mat3(1.0f), nullptr, d);
    // back, [blur: arm's upper, [glow: hand's fist] ], front -- the arm and the hand have no sprites
    CHECK(shape(d) == "q[1q[2q]]q", "the blur holds the arm's subtree, the glow the hand's inside it, the rest outside");
    CHECK(d.layers.size() == 2 && near(d.layers[0].params[0], 20), "the blur vector at the node's time (half way: 20)");
    CHECK(near(d.layers[1].params[0], 2) && near(d.layers[1].params[1], 10) && near(d.layers[1].color[1], 0.5f),
          "the glow's params and colour");
    evaluate(c, 0, poses);
    d.clear();
    appendDraws(c, poses, set, placement(0, 0, 2.0f), nullptr, d);
    CHECK(shape(d) == "qq[2q]q", "no blur at t = 0 (a zero vector): no layer, the glow still");
    CHECK(near(d.layers[0].params[1], 20), "lengths scale with the placement (radius 10 at scale 2)");
    evaluate(c, 1, poses);
    d.clear();
    appendDraws(c, poses, set, placement(0, 0, 1.0f, 90.0f), nullptr, d);
    CHECK(near(d.layers[0].params[0], 0) && near(d.layers[0].params[1], 40), "the blur vector turns with the placement");
    Clip hidden = rig();
    hidden.root.children[1].visible = false;
    evaluate(hidden, 1, poses);
    d.clear();
    appendDraws(hidden, poses, set, glm::mat3(1.0f), nullptr, d);
    CHECK(d.layers.empty(), "a hidden subtree: no layers");
    Clip flash = rig();
    flash.root.children[1].fx.shader = FxShader::Flash;
    flash.root.children[1].fxKeys.clear();
    evaluate(flash, 1, poses);
    d.clear();
    appendDraws(flash, poses, set, glm::mat3(1.0f), nullptr, d);
    CHECK(shape(d) == "qq[2q]q", "a flash of amount 0 shows nothing: no layer");

    // ---- afterimages ----
    Clip trail = rig();
    Node& front = trail.root.children[2];
    front.trail.count = 3;
    front.trail.gap = 0.1f;
    front.trail.color = {1, 1, 1, 0.6f};
    Key<glm::vec3> p0, p1;
    p1.t = 1;
    p1.v = {100, 0, 0};
    front.posKeys = {p0, p1};
    evaluate(trail, 0.5f, poses);
    d.clear();
    appendDraws(trail, poses, set, glm::mat3(1.0f), nullptr, d);
    CHECK(d.quads.size() == 7, "three afterimages of the front before it (4 sprites + 3)");
    if (d.quads.size() == 7) {
        // the oldest (0.2 s) first, faintest; the newest (0.4 s) last, at the trail's alpha
        CHECK(near(d.quads[3].tint[3], 0.2f) && near(d.quads[5].tint[3], 0.6f), "they fade, oldest first");
        CHECK(near(d.quads[3].corners[0], 20 - 8) && near(d.quads[5].corners[0], 40 - 8), "each where the node was then");
    }
    evaluate(trail, 0.15f, poses);
    d.clear();
    appendDraws(trail, poses, set, glm::mat3(1.0f), nullptr, d);
    CHECK(d.quads.size() == 5, "none from before the clip began (only 0.05 s back)");

    if (g_fail) { std::printf("anim_fx_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("anim_fx_test: OK\n");
    return 0;
}
