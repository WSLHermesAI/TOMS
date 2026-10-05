// anim_clip_test.cpp — the node animation runtime (anim_clip.h, anim_player.h), headless.
//
//   easing     Tweeny's Penner easings by name, stepped, cubic Bezier, overshoot
//   tracks     linear / eased / stepped segments, before-first and after-last, unwrapped rotation
//   tree       world = parent * T R S, draw order by `order`, colour inheritance, hidden subtrees
//   cache      PoseCache == evaluate() (any seek order); only keyed tracks / changed poses update
//   child      a child's keys outlive its parent's: it keeps animating, the clip lasts to its last key
//   playback   per node: once + stay, once + hide, loop (its subtree's key range, also after the end)
//   player     play count, looping, stay-at-last-frame, events (time 0, loop wrap, exactly once)
//   quads      corners from pivot + trim offsets + world transform, uv, tint, additive, missing sprites
//   json       round trip and errors
//
// Run: ./anim_clip_test
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "anim_player.h"

static int g_checks = 0, g_fails = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            ++g_fails;                                                            \
            fprintf(stderr, "  FAIL: %s:%d  ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fprintf(stderr, "\n");                                                \
        }                                                                         \
    } while (0)

using namespace toms::anim;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near2(const glm::vec2& a, float x, float y, float eps = 1e-4f) { return near(a.x, x, eps) && near(a.y, y, eps); }
glm::vec2 apply(const glm::mat3& m, float x, float y) { const glm::vec3 v = m * glm::vec3(x, y, 1); return {v.x, v.y}; }

Ease ease(const char* name) { Ease e; parseEase(name, e); return e; }

void testEasing() {
    CHECK(near(ease("linear").apply(0.5f), 0.5f), "linear");
    CHECK(near(ease("quadraticIn").apply(0.5f), 0.25f), "quadraticIn(0.5) = 0.25");
    CHECK(near(ease("quadraticOut").apply(0.5f), 0.75f), "quadraticOut(0.5) = 0.75");
    CHECK(ease("stepped").apply(0.9f) == 0.0f, "stepped holds");
    for (const char* n : {"cubicInOut", "sinusoidalIn", "exponentialOut", "circularInOut", "bounceOut", "elasticIn", "backInOut"}) {
        const Ease e = ease(n);
        CHECK(near(e.apply(0.0f), 0.0f, 1e-3f) && near(e.apply(1.0f), 1.0f, 1e-3f), "%s starts at 0 and ends at 1", n);
    }
    float maxBack = 0;
    for (int i = 0; i <= 100; i++) maxBack = std::max(maxBack, ease("backOut").apply(i / 100.0f));
    CHECK(maxBack > 1.05f, "backOut overshoots (%f)", maxBack);
    // Every name round-trips; unknown names are rejected.
    for (const std::string& n : easeNames()) { Ease e; CHECK(parseEase(n, e) && easeName(e) == n, "ease name %s", n.c_str()); }
    Ease bad;
    CHECK(!parseEase("wobbly", bad), "unknown ease rejected");
    // Bezier: (0,0,1,1) is linear; CSS ease-in (0.42,0,1,1) is below the diagonal.
    Ease bz; bz.kind = Ease::kBezier;
    for (int i = 0; i <= 10; i++) CHECK(near(bz.apply(i / 10.0f), i / 10.0f, 1e-3f), "bezier(0,0,1,1) linear at %d", i);
    Ease easeIn; easeIn.kind = Ease::kBezier; easeIn.bezier[0] = 0.42f; easeIn.bezier[1] = 0; easeIn.bezier[2] = 1; easeIn.bezier[3] = 1;
    CHECK(easeIn.apply(0.5f) < 0.4f && easeIn.apply(0.5f) > 0.2f, "ease-in bezier at 0.5 = %f", easeIn.apply(0.5f));
}

void testTracks() {
    Node n;
    n.pos = {7, 7};
    CHECK(near2(positionAt(n, 3), 7, 7), "no keys: rest value");
    n.posKeys = {{0.0f, {0, 0}, {}}, {1.0f, {10, 20}, {}}, {2.0f, {10, 0}, ease("stepped")}, {3.0f, {0, 0}, {}}};
    CHECK(near2(positionAt(n, -1), 0, 0), "before the first key");
    CHECK(near2(positionAt(n, 0.5f), 5, 10), "linear midpoint");
    CHECK(near2(positionAt(n, 1.5f), 10, 10), "second segment");
    CHECK(near2(positionAt(n, 2.9f), 10, 0), "stepped key holds");
    CHECK(near2(positionAt(n, 3.0f), 0, 0), "on the last key");
    CHECK(near2(positionAt(n, 9.0f), 0, 0), "after the last key");
    n.posKeys[0].ease = ease("quadraticIn");
    CHECK(near2(positionAt(n, 0.5f), 2.5f, 5), "the key's ease shapes its segment");
    n.rotKeys = {{0.0f, 0.0f, {}}, {1.0f, 720.0f, {}}};
    CHECK(near(rotationAt(n, 0.5f), 360.0f), "rotation is not wrapped");
    n.spriteKeys = {{0.0f, "a", {}}, {0.25f, "b", {}}};
    CHECK(spriteAt(n, 0.1f) == "a" && spriteAt(n, 0.25f) == "b" && spriteAt(n, 5) == "b", "sprite steps");
    n.colorKeys = {{0.0f, {1, 1, 1, 1}, ease("backIn")}, {1.0f, {1, 1, 1, 0}, {}}};
    for (int i = 0; i <= 10; i++) { const glm::vec4 c = colorAt(n, i / 10.0f); CHECK(c.a >= 0 && c.a <= 1, "colour clamped at %d", i); }
    CHECK(near(n.lastKeyTime(), 3.0f), "last key time");
}

// root (no sprite) at (100,0) turned 90 degrees, with children "shadow" (order -1), "body",
// "fx" (order 2) and "hat" (order 0, child of body).
Clip treeClip() {
    Clip c;
    c.name = "tree";
    c.root.name = "root";
    c.root.pos = {100, 0};
    c.root.rot = 90;
    c.root.color = {1, 0.5f, 1, 1};
    Node body; body.name = "body"; body.sprite = "s"; body.pos = {10, 0};
    Node hat; hat.name = "hat"; hat.sprite = "s"; hat.pos = {0, -5};
    body.children.push_back(hat);
    Node fx; fx.name = "fx"; fx.order = 2; fx.inheritColor = false; fx.blend = Blend::Add; fx.sprite = "s";
    Node shadow; shadow.name = "shadow"; shadow.order = -1; shadow.sprite = "s";
    c.root.children = {fx, body, shadow};
    return c;
}

void testTree() {
    Clip c = treeClip();
    std::vector<NodePose> poses;
    evaluate(c, 0, poses);
    std::vector<std::string> order;
    for (const NodePose& p : poses) order.push_back(p.node->name);
    CHECK((order == std::vector<std::string>{"shadow", "root", "body", "hat", "fx"}), "draw order");
    const NodePose& body = poses[2];
    // (10,0) turned 90 degrees clockwise (y down) is (0,10); plus the root's (100,0).
    CHECK(near2(apply(body.world, 0, 0), 100, 10), "child position through a rotated parent");
    const NodePose& hat = poses[3];
    CHECK(near2(apply(hat.world, 0, 0), 105, 10), "grandchild: (0,-5) turned is (5,0)");
    CHECK(near(body.color.g, 0.5f) && near(poses[4].color.g, 1.0f), "colour inherited unless inheritColor is false");
    c.root.children[1].visibleKeys = {{0.0f, false, {}}};
    evaluate(c, 0, poses);
    CHECK(!poses[2].visible && !poses[3].visible && poses[4].visible, "a hidden node hides its subtree only");
    // Scale and mirror.
    Node m; m.scale = {-2, 1};
    CHECK(near2(apply(localTransform(m, 0), 3, 4), -6, 4), "negative scale mirrors");
}

bool samePoses(const std::vector<NodePose>& a, const std::vector<NodePose>& b, float t) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        bool same = a[i].node == b[i].node && a[i].depth == b[i].depth && a[i].visible == b[i].visible &&
                    a[i].sprite == b[i].sprite;
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++) same &= near(a[i].world[c][r], b[i].world[c][r], 1e-4f);
        for (int k = 0; k < 4; k++) same &= near(a[i].color[k], b[i].color[k], 1e-6f);
        if (!same) {
            fprintf(stderr, "  pose %zu (%s) differs at t=%f\n", i, a[i].node ? a[i].node->name.c_str() : "?", t);
            return false;
        }
    }
    return true;
}

void testPoseCache() {
    // Each node animates only what it needs: the root turns, body only switches sprites, fx only
    // fades (with a stepped key) and blinks, shadow only scales, hat has no keys at all.
    Clip c = treeClip();
    c.root.rotKeys = {{0.0f, 0.0f, ease("quadraticInOut")}, {1.0f, 360.0f, {}}};
    Node& fx = c.root.children[0];
    Node& body = c.root.children[1];
    Node& shadow = c.root.children[2];
    body.spriteKeys = {{0.0f, "s", {}}, {0.3f, "p", {}}, {0.6f, "s", {}}};
    fx.colorKeys = {{0.2f, {1, 1, 1, 0}, {}}, {0.5f, {1, 1, 1, 1}, ease("stepped")}, {0.8f, {1, 0, 0, 1.5f}, {}}};
    fx.visibleKeys = {{0.0f, true, {}}, {0.4f, false, {}}, {0.45f, true, {}}};
    shadow.scaleKeys = {{0.1f, {1, 1}, ease("backOut")}, {0.9f, {2, 0.5f}, {}}};

    PoseCache cache;
    cache.bind(&c);
    CHECK(cache.animatedNodes() == 4, "4 animated nodes (hat has no keys): %d", cache.animatedNodes());
    std::vector<NodePose> ref;
    bool same = true;
    auto at = [&](float t) {
        cache.seek(t);
        evaluate(c, t, ref);
        same = same && samePoses(cache.poses(), ref, t);
    };
    for (int f = 0; f <= 150; f++) at(f / 60.0f);                                   // forward, past the end
    for (float t : {0.1f, 0.0f, 0.3f, 0.2999f, 0.5f, 0.45f, 0.4f, 0.8f, 0.9f, 1.0f}) at(t);   // jumps, exactly on keys
    unsigned seed = 7;
    for (int i = 0; i < 300; i++) {                                                  // anywhere, either way
        seed = seed * 1103515245u + 12345u;
        at(float((seed >> 8) % 1300) / 1000.0f - 0.1f);
    }
    CHECK(same, "PoseCache == evaluate() forward, backward, on keys, at random times");

    // Only what has keys moves.
    Clip s;
    s.root.name = "root";
    Node a; a.name = "a"; a.sprite = "s"; a.spriteKeys = {{0.0f, "s", {}}, {0.5f, "p", {}}};
    Node b; b.name = "b"; b.sprite = "s";
    Node kid; kid.name = "kid"; kid.sprite = "s";
    Node mover; mover.name = "mover"; mover.posKeys = {{0.0f, {0, 0}, {}}, {1.0f, {10, 0}, {}}};
    mover.children.push_back(kid);
    s.root.children = {a, b, mover};
    cache.bind(&s);
    cache.seek(0);
    CHECK(cache.changedCount() == 5, "first seek: every pose (%d)", cache.changedCount());
    const NodePose& pa = cache.poses()[1];
    const glm::mat3 aWorld = pa.world;
    cache.seek(0.25f);
    CHECK(cache.changedCount() == 2 && cache.changed(3) && cache.changed(4),
          "mid-way: only mover and its child (%d)", cache.changedCount());
    cache.seek(0.6f);
    CHECK(cache.changed(1) && pa.sprite == "p" && pa.world == aWorld && !cache.changed(2) && cache.changedCount() == 3,
          "a's sprite key: a changes (sprite only), b does not");
    cache.seek(0.7f);
    CHECK(!cache.changed(1) && cache.changedCount() == 2, "a holds its sprite: not touched");
    cache.seek(1.5f);
    cache.seek(2.0f);
    CHECK(cache.changedCount() == 0, "after every last key nothing changes");
    evaluate(s, 2.0f, ref);
    CHECK(samePoses(cache.poses(), ref, 2.0f), "and the poses are still right");
}

// A child's keys can start and end at other times than its parent's: it keeps animating after the
// parent's last key (and before its own first key it holds that key's value).
void testChildOutlivesParent() {
    Clip c;
    c.name = "c";
    c.root.name = "root";
    c.root.posKeys = {{0.0f, {0, 0}, {}}, {0.5f, {100, 0}, {}}};   // the parent stops at 0.5
    Node kid;
    kid.name = "kid";
    kid.sprite = "s";
    kid.posKeys = {{0.3f, {0, 0}, {}}, {1.5f, {0, 120}, {}}};        // the child runs 0.3 .. 1.5
    kid.rotKeys = {{1.0f, 0.0f, {}}, {2.0f, 90.0f, {}}};            // and turns 1.0 .. 2.0
    c.root.children.push_back(kid);
    CHECK(near(c.duration(), 2.0f), "duration = the child's last key: %f", c.duration());

    AnimPlayer p;   // its time, and a PoseCache seeked to it each frame (as AnimPlayer::draw does)
    PoseCache cache;
    cache.bind(&c);
    p.play(&c);
    p.update(0);
    cache.seek(p.time());
    std::vector<NodePose> ref;
    bool same = true, moved = true;
    glm::vec2 last = apply(cache.poses()[1].world, 0, 0);
    for (int f = 1; f <= 120; f++) {   // 60 fps to 2.0 s
        p.update(f * 1000 / 60 - (f - 1) * 1000 / 60);
        const float t = p.time();
        cache.seek(t);
        evaluate(c, t, ref);
        same = same && samePoses(cache.poses(), ref, t);
        const glm::vec2 at = apply(cache.poses()[1].world, 0, 0);
        if (t > 0.55f && t < 1.45f) moved = moved && at.y > last.y;   // still moving after the parent stopped
        last = at;
    }
    CHECK(same, "the player's poses == evaluate() every frame");
    CHECK(moved, "the child keeps moving after the parent's last key");
    CHECK(near2(apply(cache.poses()[1].world, 0, 0), 100, 120, 1e-3f), "child at the end: parent (100,0) + own (0,120)");
    p.update(50);   // (the 60 fps steps sum to a hair under 2 s)
    CHECK(near(p.time(), 2.0f, 1e-3f) && p.finished(), "the clip ends at the child's last key, not the parent's");
}

// Per-node playback: a node plays once and stays (default), plays once and hides, or loops its
// subtree's key range -- also after the clip's own timeline has ended.
void testNodePlayback() {
    Clip c;
    c.name = "popup";
    c.root.name = "root";
    Node pop; pop.name = "pop"; pop.sprite = "s";                      // once, stays
    pop.scaleKeys = {{0.0f, {0, 0}, {}}, {0.3f, {1, 1}, {}}};
    Node flash; flash.name = "flash"; flash.sprite = "s"; flash.stayAtLastFrame = false;   // once, then hides
    flash.colorKeys = {{0.0f, {1, 1, 1, 1}, {}}, {0.2f, {1, 1, 1, 0.5f}, {}}};
    Node spark; spark.name = "spark"; spark.sprite = "s"; spark.loop = true;              // loops 0.5..1.0
    spark.posKeys = {{0.5f, {0, 0}, {}}, {1.0f, {10, 0}, {}}};
    Node orbit; orbit.name = "orbit"; orbit.loop = true;                                 // a looping group
    orbit.rotKeys = {{0.0f, 0.0f, {}}, {1.0f, 360.0f, {}}};
    Node dot; dot.name = "dot"; dot.sprite = "s";                                        // runs in orbit's time
    dot.posKeys = {{0.0f, {20, 0}, {}}, {0.5f, {40, 0}, {}}};
    orbit.children.push_back(dot);
    c.root.children = {pop, flash, spark, orbit};
    CHECK(near(c.duration(), 1.0f) && hasLoopingNodes(c), "duration 1.0, has looping nodes");

    std::vector<NodePose> ps;
    auto find = [&](const char* name) -> const NodePose& {
        for (const NodePose& p : ps)
            if (p.node->name == name) return p;
        return ps.front();
    };
    evaluate(c, 0.75f, ps);
    CHECK(near2(apply(find("spark").world, 0, 0), 5, 0), "spark mid-range at 0.75");
    evaluate(c, 1.25f, ps);
    CHECK(near2(apply(find("spark").world, 0, 0), 5, 0), "spark looped: 1.25 plays as 0.75");
    CHECK(near2(apply(find("dot").world, 0, 0), 0, 30, 1e-3f), "orbit looped to 0.25 (90 deg), dot in orbit's time (x 30): %f %f",
          apply(find("dot").world, 0, 0).x, apply(find("dot").world, 0, 0).y);
    evaluate(c, 0.1f, ps);
    CHECK(find("flash").visible, "flash shows during its keys");
    evaluate(c, 0.3f, ps);
    CHECK(!find("flash").visible && find("pop").visible, "flash hidden after its last key; pop stays");
    evaluate(c, 7.6f, ps);
    CHECK(near(find("pop").world[0][0], 1.0f) && !find("flash").visible && near2(apply(find("spark").world, 0, 0), 2, 0, 1e-3f),
          "long after the end: pop holds, flash hidden, spark still looping (7.6 -> 0.6)");

    PoseCache cache;
    cache.bind(&c);
    std::vector<NodePose> ref;
    bool same = true;
    auto at = [&](float t) {
        cache.seek(t);
        evaluate(c, t, ref);
        same = same && samePoses(cache.poses(), ref, t);
    };
    for (int f = 0; f <= 300; f++) at(f / 60.0f);   // 0 .. 5 s
    for (float t : {0.2f, 0.19f, 1.0f, 1.5f, 0.3f, 4.0f, 0.0f}) at(t);
    unsigned seed = 3;
    for (int i = 0; i < 300; i++) {
        seed = seed * 1103515245u + 12345u;
        at(float((seed >> 8) % 5000) / 1000.0f);
    }
    CHECK(same, "PoseCache == evaluate() with looping / hiding nodes, any seek order");

    AnimPlayer p;
    p.play(&c);
    p.update(0);
    p.update(1750);
    CHECK(p.finished() && near(p.time(), 1.0f) && near(p.poseTime(), 1.75f, 1e-3f),
          "after the clip's end: time() holds 1.0, poseTime() runs on (%f)", p.poseTime());
    Clip still = c;
    still.root.children[2].loop = false;
    still.root.children[3].loop = false;
    AnimPlayer q;
    q.play(&still);
    q.update(0);
    q.update(1750);
    CHECK(near(q.poseTime(), 1.0f), "without looping nodes poseTime() stops at the end");

    AnimFile f;
    f.clips.push_back(c);
    AnimFile back;
    std::string err;
    CHECK(parseAnim(animToJson(f), back, &err) && back.clips[0].root.children[1].stayAtLastFrame == false &&
              back.clips[0].root.children[2].loop && back.clips[0].root.children[0].stayAtLastFrame &&
              !back.clips[0].root.children[0].loop,
          "loop / stayAtLastFrame round-trip through JSON");
}

void testPlayer() {
    Clip c;
    c.name = "c";
    c.root.posKeys = {{0.0f, {0, 0}, {}}, {1.0f, {10, 0}, {}}};
    c.root.eventKeys = {{0.0f, "start", {}}, {0.5f, "hit", {}}, {1.0f, "end", {}}};
    c.playCount = 2;
    AnimPlayer p;
    p.play(&c);
    p.update(0);
    CHECK((p.takeEvents() == std::vector<std::string>{"start"}), "time-0 event on the first update");
    p.update(600);
    CHECK((p.takeEvents() == std::vector<std::string>{"hit"}), "event at 0.5");
    CHECK(near(p.time(), 0.6f, 1e-3f), "time 0.6");
    p.update(900);   // 1.5: crosses the end of play 1 and the start of play 2
    CHECK((p.takeEvents() == std::vector<std::string>{"end", "start", "hit"}), "events across the wrap, once each");
    CHECK(near(p.time(), 0.5f, 1e-3f) && !p.finished(), "second play at 0.5");
    p.update(5000);
    CHECK((p.takeEvents() == std::vector<std::string>{"end"}), "only up to the last play");
    CHECK(p.finished() && p.showing() && near(p.time(), 1.0f), "finished, stays at the last frame");
    c.stayAtLastFrame = false;
    CHECK(!p.showing(), "hidden after the end without stayAtLastFrame");
    c.playCount = -1;
    p.play(&c);
    p.update(0);
    p.takeEvents();
    p.update(10250);   // ten plays and a quarter
    int hits = 0;
    for (const std::string& e : p.takeEvents()) hits += e == "hit";
    CHECK(hits == 10 && !p.finished() && near(p.time(), 0.25f, 1e-3f), "looping: %d hits", hits);
}

struct CaptureRenderer : IRenderer {
    std::vector<Quad> quads;
    void init(uint32_t, uint32_t) override {}
    void loadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void loadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    void begin() override {}
    void drawSprite(const Quad& q) override { quads.push_back(q); }
    void end() override {}
    uint32_t width() const override { return 1024; }
    uint32_t height() const override { return 768; }
};

void testQuads() {
    // A 100x100 page: "s" is a 20x10 image trimmed to 16x8 at offset (2,1); "p" has a top-left pivot.
    toms::AtlasFile atlas;
    std::string err;
    CHECK(toms::parseAtlas("page.png\nsize: 100, 100\n"
                           "s\n  bounds: 10, 20, 16, 8\n  offsets: 2, 1, 20, 10\n"
                           "p\n  bounds: 50, 50, 10, 10\n  x_pivot: 0, 0\n", atlas, &err), "atlas: %s", err.c_str());
    Clip c;
    c.root.sprite = "s";
    c.root.pos = {100, 100};
    std::vector<NodePose> poses;
    evaluate(c, 0, poses);
    std::vector<Quad> q;
    appendQuads(poses, atlas, glm::mat3(1.0f), nullptr, q);
    CHECK(q.size() == 1, "one quad");
    // Centre pivot (10,5) of the 20x10 original; trimmed pixels start at (2,1): x 92..108, y 96..104.
    CHECK(q.size() == 1 && near(q[0].corners[0], 92) && near(q[0].corners[1], 96) && near(q[0].corners[4], 108) &&
              near(q[0].corners[5], 104), "corners from pivot and trim offset");
    CHECK(q.size() == 1 && near(q[0].uv[0], 0.10f) && near(q[0].uv[3], 0.28f), "uv from the atlas");
    // Rotated 90 degrees and placed at (10,10) scaled x2 by the placement.
    c.root.rot = 90;
    c.root.blend = Blend::Add;
    c.root.color = {1, 1, 1, 0.5f};
    evaluate(c, 0, poses);
    q.clear();
    const float tint[4] = {1, 0, 1, 1};
    appendQuads(poses, atlas, placement(10, 10, 2), tint, q);
    // Top-left corner (-8,-4) from the pivot, turned 90 -> (4,-8), at (100,100) -> (104,92), x2 +10 -> (218,194).
    CHECK(q.size() == 1 && near(q[0].corners[0], 218) && near(q[0].corners[1], 194), "rotated corner (%f,%f)",
          q.empty() ? 0 : q[0].corners[0], q.empty() ? 0 : q[0].corners[1]);
    CHECK(q.size() == 1 && q[0].additive && near(q[0].tint[1], 0) && near(q[0].tint[3], 0.5f), "blend and tint");
    // The atlas's own pivot, and a missing sprite reported once.
    c.root = Node();
    c.root.sprite = "p";
    Node a; a.sprite = "nope"; Node b; b.sprite = "nope";
    c.root.children = {a, b};
    evaluate(c, 0, poses);
    q.clear();
    std::vector<std::string> missing;
    appendQuads(poses, atlas, glm::mat3(1.0f), nullptr, q, &missing);
    CHECK(q.size() == 1 && near(q[0].corners[0], 0) && near(q[0].corners[1], 0), "x_pivot 0,0 = top-left");
    CHECK(missing == std::vector<std::string>{"nope"}, "missing sprite reported once");
    // The player draws through the renderer.
    CaptureRenderer ren;
    AnimPlayer p;
    p.play(&c);
    p.draw(&ren, atlas, glm::mat3(1.0f));
    CHECK(ren.quads.size() == 1 && ren.quads[0].texture == kSpriteAtlasTexture, "player drew %zu quad(s) from the sprite atlas",
          ren.quads.size());

    // Several atlases: looked up in order (first match wins), each page with its own texture.
    toms::AtlasFile second;
    CHECK(toms::parseAtlas("a.png\nsize: 10, 10\nonly2\n  bounds: 0, 0, 4, 4\n"
                           "\nb.png\nsize: 20, 20\ns\n  bounds: 0, 0, 2, 2\n", second, &err), "second atlas: %s", err.c_str());
    AtlasSet set;
    set.add(atlas, "game");             // the sprite atlas: kSpriteAtlasTexture
    set.add(second, "fx", {7, 9});      // its two pages: textures 7 and 9
    uint16_t tex = 0;
    const toms::AtlasRegion* r = set.find("s", &tex);
    CHECK(r && r->w == 16 && tex == kSpriteAtlasTexture, "'s' from the first atlas that has it");
    r = set.find("only2", &tex);
    CHECK(r && tex == 7, "'only2' from the second atlas, page 0 -> texture 7");
    CHECK(!set.find("nope"), "in no atlas");
    // "atlas:sprite" picks the atlas, like MPDI's PIName + ImageName: the same name from either.
    int entry = -1;
    r = set.find("fx:s", &tex, &entry);
    CHECK(r && r->w == 2 && tex == 9 && entry == 1, "'fx:s' is the second atlas's s (page 1 -> texture 9)");
    r = set.find("game:s", &tex, &entry);
    CHECK(r && r->w == 16 && tex == kSpriteAtlasTexture && entry == 0, "'game:s' is the first atlas's s");
    CHECK(!set.find("game:only2"), "a named atlas without the sprite: not found, no fallback");
    CHECK(set.find("other:only2", &tex) && tex == 7, "an atlas id the set does not have: looked up by order");
    c.root = Node();
    c.root.sprite = "only2";
    c.root.spriteKeys = {{0.0f, "only2", {}}, {0.5f, "fx:s", {}}};
    evaluate(c, 0, poses);
    q.clear();
    appendQuads(poses, set, glm::mat3(1.0f), nullptr, q);
    CHECK(q.size() == 1 && q[0].texture == 7 && near(q[0].uv[2], 0.4f), "quad carries its atlas page's texture and uv");
    evaluate(c, 0.6f, poses);   // the sprite key switched atlas and page
    q.clear();
    appendQuads(poses, set, glm::mat3(1.0f), nullptr, q);
    CHECK(q.size() == 1 && q[0].texture == 9 && near(q[0].uv[2], 0.1f), "a sprite key from another atlas page");

    // The player keeps each quad until its pose changes: frame by frame it draws what
    // appendQuads(evaluate()) would, also when the placement or tint changes.
    Node still; still.name = "still"; still.sprite = "game:s"; still.pos = {30, 0};
    Node spin; spin.name = "spin"; spin.sprite = "s"; spin.rotKeys = {{0.0f, 0.0f, {}}, {1.0f, 90.0f, {}}};
    c.root.children = {still, spin};
    c.root.colorKeys = {{0.2f, {1, 1, 1, 1}, {}}, {0.4f, {1, 1, 1, 0}, {}}, {0.7f, {1, 1, 1, 1}, {}}};
    c.length = 1.0f;
    c.playCount = -1;
    p.play(&c);
    bool same = true;
    for (int f = 0; f < 140; f++) {
        const glm::mat3 place = placement(f < 70 ? 0.0f : 5.0f, 0);
        const float half[4] = {1, 1, 1, 0.5f};
        const float* tint = f >= 100 ? half : nullptr;
        ren.quads.clear();
        p.draw(&ren, set, place, tint);
        evaluate(c, p.time(), poses);
        q.clear();
        appendQuads(poses, set, place, tint, q);
        bool frame = ren.quads.size() == q.size();
        for (size_t i = 0; frame && i < q.size(); i++) {
            frame = q[i].texture == ren.quads[i].texture && q[i].additive == ren.quads[i].additive;
            for (int k = 0; k < 8; k++) frame = frame && near(q[i].corners[k], ren.quads[i].corners[k], 1e-3f);
            for (int k = 0; k < 4; k++) frame = frame && near(q[i].tint[k], ren.quads[i].tint[k], 1e-5f) && q[i].uv[k] == ren.quads[i].uv[k];
        }
        if (!frame) fprintf(stderr, "  frame %d (t=%f): player %zu quads, reference %zu\n", f, p.time(), ren.quads.size(), q.size());
        same = same && frame;
        p.update(16);
    }
    CHECK(same, "player's cached quads == appendQuads(evaluate()) every frame");
}

void testJson() {
    AnimFile f;
    f.atlases = {{"game", "../media/atlas/game.atlas"}, {"dark16", "../styles/dark16/atlas/game.atlas"}, {"fx", "../fx/fx.atlas"}};
    Clip c = treeClip();
    c.playCount = -1;
    c.length = 2;
    c.root.children[1].posKeys = {{0.0f, {1, 2}, ease("backOut")}, {0.5f, {3, 4}, {}}};
    Ease bz; bz.kind = Ease::kBezier; bz.bezier[0] = 0.1f; bz.bezier[3] = 0.9f;
    c.root.children[1].rotKeys = {{0.0f, 0.0f, bz}, {1.0f, 90.0f, {}}};
    c.root.children[1].spriteKeys = {{0.0f, "s", {}}, {0.2f, "t", {}}};
    c.root.children[1].eventKeys = {{0.2f, "hit", {}}};
    c.root.children[1].visibleKeys = {{0.0f, true, {}}, {0.9f, false, {}}};
    f.clips.push_back(c);
    const std::string text = animToJson(f);
    AnimFile back;
    std::string err;
    CHECK(parseAnim(text, back, &err), "parse: %s", err.c_str());
    CHECK(animToJson(back) == text, "round trip is exact");
    CHECK(back.find("tree") && back.find("tree")->playCount == -1 && !back.find("other"), "find");
    CHECK(back.atlases == f.atlases, "atlases and their ids kept in order");
    CHECK(text.find("\"../fx/fx.atlas\"") != std::string::npos && text.find("\"id\": \"dark16\"") != std::string::npos,
          "default ids written as plain paths, others as {id, path}");
    AnimFile legacy;
    CHECK(parseAnim(R"({"atlas":"old/game.atlas","clips":[]})", legacy, &err) && legacy.atlases.size() == 1 &&
              legacy.atlases[0].id == "game" && legacy.atlases[0].path == "old/game.atlas", "an older single \"atlas\" reads as a list");
    CHECK(!parseAnim(R"({"atlases":["a/game.atlas","b/game.atlas"],"clips":[]})", legacy, &err) && err.find("game") != std::string::npos,
          "two atlases with the same id are an error: %s", err.c_str());
    const SpriteRef sr = parseSpriteRef("fx:slash_0");
    CHECK(sr.atlas == "fx" && sr.name == "slash_0" && parseSpriteRef("coin").atlas.empty() && spriteRef("fx", "a") == "fx:a" &&
              spriteRef("", "a") == "a" && defaultAtlasId("x/y/fx.atlas") == "fx", "sprite references");
    const Node& body = back.clips[0].root.children[1];
    CHECK(body.posKeys.size() == 2 && easeName(body.posKeys[0].ease) == "backOut", "ease kept");
    CHECK(body.rotKeys.size() == 2 && body.rotKeys[0].ease.kind == Ease::kBezier && near(body.rotKeys[0].ease.bezier[0], 0.1f), "bezier kept");
    // Keys come back sorted; bad input is an error, not a crash.
    AnimFile sorted;
    CHECK(parseAnim(R"({"clips":[{"name":"x","root":{"name":"r","tracks":{"pos":[{"t":1,"v":[1,1]},{"t":0,"v":[0,0]}]}}}]})", sorted, &err) &&
              sorted.clips[0].root.posKeys[0].t == 0, "keys sorted by time");
    CHECK(!parseAnim(R"({"clips":[{"name":"x","root":{"tracks":{"pos":[{"t":0,"v":[0,0],"ease":"wobbly"}]}}}]})", sorted, &err) &&
              err.find("wobbly") != std::string::npos, "unknown ease: %s", err.c_str());
    CHECK(!parseAnim("{not json", sorted, &err), "invalid JSON");
}

}  // namespace

int main() {
    testEasing();
    testTracks();
    testTree();
    testPoseCache();
    testPlayer();
    testChildOutlivesParent();
    testNodePlayback();
    testQuads();
    testJson();
    std::printf("anim_clip_test: %d check(s), %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
