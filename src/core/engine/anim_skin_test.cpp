// anim_skin_test.cpp -- skinned nodes (anim_clip.h "skin", anim_player.h makeSkinnedMesh / appendDraws;
// docs/engine/16_MESH_ANIMATION.md): the skin in the .anim file (version 3 only when used), a mesh at rest lying exactly where
// the sprite's quad does, a turning bone moving the vertices near it and not the far ones, the automatic weights, and
// the fallbacks (no mesh, a rotated region: the quad). No GPU: a renderer that records quads and meshes.
#include "anim_player.h"

#include <cmath>
#include <cstdio>

using namespace toms::anim;

namespace {
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

struct Recorder : IRenderer {
    std::vector<char> order;   // 'q' quad, 'm' mesh
    std::vector<MeshVertex> lastMesh;
    void init(uint32_t, uint32_t) override {}
    void loadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void loadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    void begin() override {}
    void drawSprite(const Quad&) override { order.push_back('q'); }
    void drawMesh(const Mesh& m) override { order.push_back('m'); lastMesh.assign(m.vertices, m.vertices + m.vertexCount); }
    void end() override {}
    uint32_t width() const override { return 1024; }
    uint32_t height() const override { return 768; }
};

// A 100 x 100 page; "bar" is 8 x 32 at (10, 10), pivot at the bottom centre. Its mesh: a column of 4 cells, 8 px each.
struct Fixture {
    toms::AtlasFile atlas;
    toms::SpriteMeshFile meshes;
    AtlasSet set;
    Fixture() {
        std::string err;
        toms::parseAtlas("page.png\nsize: 100, 100\nbar\n  bounds: 10, 10, 8, 32\n  x_pivot: 0.5, 1\n"
                         "tilted\n  bounds: 30, 10, 32, 8\n  rotate: 90\n", atlas, &err);
        std::vector<uint8_t> px(8 * 32 * 4, 255);
        meshes.meshes["bar"] = toms::meshFromAlpha(px.data(), 8, 8, 32, 0, 0, 8);
        meshes.meshes["tilted"] = meshes.meshes["bar"];
        set.add(atlas);
        set.entries.back().meshes = &meshes;   // the atlas's mesh file, by meshKey ("bar": 8-pixel cells)
    }
};

// bar skinned to "low" (at the feet) and "high" (16 px up, a child of low).
Clip skinnedBar() {
    Clip c;
    c.name = "bend";
    c.root.name = "root";
    Node bar;
    bar.name = "bar";
    bar.sprite = "bar";
    bar.skinBones = {"low", "high"};
    Node low;
    low.name = "low";
    Node high;
    high.name = "high";
    high.pos = {0, -16, 0};
    low.children.push_back(high);
    c.root.children.push_back(bar);
    c.root.children.push_back(low);
    return c;
}

void testFile() {
    AnimFile f;
    f.clips.push_back(skinnedBar());
    std::string json = animToJson(f);
    CHECK(json.find("\"version\": 3") != std::string::npos, "a file with a skin is version 3");
    CHECK(json.find("\"weights\": \"auto\"") != std::string::npos, "automatic weights are written as \"auto\"");
    AnimFile back;
    std::string err;
    CHECK(parseAnim(json, back, &err) && back == f, "the skin reads back");

    f.clips[0].root.children[0].skinWeights = {0, 1, 0, 0, 0, 0, 0, 0,   1, 0.25f, 0, 0.75f, 0, 0, 0, 0};
    json = animToJson(f);
    CHECK(parseAnim(json, back, &err) && back == f, "painted weights read back (zero weights left out, refilled)");

    AnimFile plain;
    plain.clips.push_back(Clip());
    CHECK(animToJson(plain).find("\"version\": 2") != std::string::npos, "a file without skins stays version 2 (older builds read it)");

    CHECK(!parseAnim(R"({"version":3,"clips":[{"name":"c","root":{"name":"n","skin":{"bones":[]}}}]})", back, &err), "a skin without bones");
    CHECK(!parseAnim(R"({"version":3,"clips":[{"name":"c","root":{"name":"n","skin":{"bones":["a"],"weights":[[1,1]]}}}]})", back, &err),
          "a weight naming a bone the skin does not have");
    CHECK(!parseAnim(R"({"version":4,"clips":[]})", back, &err), "a newer version");
}

void testRest() {
    Fixture fx;
    const Clip c = skinnedBar();
    std::vector<NodePose> poses;
    evaluate(c, 0, poses);
    DrawList d;
    appendDraws(c, poses, fx.set, placement(100, 200), nullptr, d);
    CHECK(d.meshes.size() == 1 && d.quads.empty(), "the skinned bar is a mesh");
    if (d.meshes.size() != 1) return;
    // At rest the mesh lies exactly on the quad: x 96..104, y 168..200 (pivot bottom centre at (100, 200)).
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minU = 1e9f, maxV = -1e9f;
    for (const MeshVertex& v : d.vertices) {
        minX = std::min(minX, v.x); maxX = std::max(maxX, v.x); minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
        minU = std::min(minU, v.u); maxV = std::max(maxV, v.v);
    }
    CHECK(near(minX, 96) && near(maxX, 104) && near(minY, 168) && near(maxY, 200), "at rest the mesh covers the quad's rect");
    CHECK(near(minU, 0.10f) && near(maxV, 0.42f), "uv from the region");
    CHECK(d.indices.size() == fx.meshes.meshes["bar"].triangles.size(), "the mesh's triangles");
}

void testBend() {
    Fixture fx;
    Clip c = skinnedBar();
    // "high" turns 90 degrees at t = 1.
    Key<glm::vec3> k0, k1;
    k1.t = 1;
    k1.v = {0, 0, 90};
    c.root.children[1].children[0].rotKeys = {k0, k1};
    std::vector<NodePose> poses;
    evaluate(c, 1, poses);
    DrawList d;
    appendDraws(c, poses, fx.set, glm::mat3(1.0f), nullptr, d);
    CHECK(d.meshes.size() == 1, "bent: still a mesh");
    if (d.meshes.size() != 1) return;
    // The bottom row (at low's origin, y 0) hardly moves; the top row (y -32, 16 px past high: high reaches on as far
    // as from low to it) swings round high's origin.
    float bottomMove = 0, topX = 0;
    int bottoms = 0, tops = 0;
    const toms::SpriteMesh& m = fx.meshes.meshes["bar"];
    for (uint32_t v = 0; v < m.vertexCount(); v++) {
        const float restX = m.vertices[2 * v] - 4, restY = m.vertices[2 * v + 1] - 32;
        if (near(restY, 0)) { bottomMove += std::fabs(d.vertices[v].x - restX) + std::fabs(d.vertices[v].y - restY); bottoms++; }
        if (near(restY, -32)) { topX += d.vertices[v].x; tops++; }
    }
    CHECK(bottoms > 0 && bottomMove / bottoms < 2.0f, "the vertices at the still bone hardly move");
    // Turned 90 degrees clockwise on screen about (0, -16): the top (16 px above it) goes 16 px to the right.
    CHECK(tops > 0 && topX / tops > 12.0f, "the vertices at the turning bone swing with it");

    // Automatic weights: two influences per vertex here, summing to 1, the nearer bone leading.
    std::vector<float> w;
    autoSkinWeights({0, 0, 0, -32}, {glm::mat4(1.0f), compose({0, -16, 0}, glm::vec3(0), glm::vec3(1))},
                    {glm::vec2(0, -16), glm::vec2(0, -32)}, w);
    CHECK(w.size() == 2 * 2 * kSkinInfluences, "kSkinInfluences pairs per vertex");
    CHECK(near(w[1] + w[3], 1.0f) && near(w[9] + w[11], 1.0f), "the weights sum to 1");
    CHECK(w[0] == 0.0f && w[1] > 0.5f, "a vertex on a bone's segment follows that bone most");
    CHECK(w[8] == 1.0f && w[9] > 0.9f, "a vertex far past one bone and near the other follows the near one");
}

void testFallbacks() {
    Fixture fx;
    Clip c = skinnedBar();
    std::vector<NodePose> poses;
    // No mesh for the sprite: its quad.
    toms::SpriteMeshFile empty;
    AtlasSet noMeshes;
    noMeshes.add(fx.atlas);
    evaluate(c, 0, poses);
    DrawList d;
    appendDraws(c, poses, noMeshes, glm::mat3(1.0f), nullptr, d);
    CHECK(d.meshes.empty() && d.quads.size() == 1, "a skinned sprite without a mesh draws as its quad");
    // ... and is named, so the check and the game can say so; with its mesh, nothing is.
    const std::vector<MissingMesh> missing = skinnedWithoutMesh(c, noMeshes);
    CHECK(missing.size() == 1 && missing[0].node == "root/bar" && missing[0].sprite == "bar",
          "skinnedWithoutMesh: the skinned bar, by its node path and sprite");
    CHECK(skinnedWithoutMesh(c, fx.set).empty(), "skinnedWithoutMesh: none when the mesh is there");
    c.root.children[0].sprite = "nowhere";
    CHECK(skinnedWithoutMesh(c, noMeshes).empty(), "skinnedWithoutMesh: a sprite no atlas has is another problem");
    c.root.children[0].sprite = "bar";
    evaluate(c, 0, poses);
    // A rotated region: its quad (meshes are cut from unrotated pixels).
    c.root.children[0].sprite = "tilted";
    evaluate(c, 0, poses);
    d.clear();
    appendDraws(c, poses, fx.set, glm::mat3(1.0f), nullptr, d);
    CHECK(d.meshes.empty() && d.quads.size() == 1, "a rotated region draws as its quad");
    // Not skinned: a quad, as appendQuads.
    c = skinnedBar();
    c.root.children[0].skinBones.clear();
    evaluate(c, 0, poses);
    d.clear();
    appendDraws(c, poses, fx.set, glm::mat3(1.0f), nullptr, d);
    CHECK(d.meshes.empty() && d.quads.size() == 1, "a node without a skin is a quad");
}

void testSkinView() {
    // What the editor's View > Show Skin draws: the bent mesh as drawn, the weights, the bones' segments now.
    Fixture fx;
    Clip c = skinnedBar();
    c.root.children[0].skinBones = {"low", "high", "nobody"};
    Key<glm::vec3> k0, k1;
    k1.t = 1;
    k1.v = {0, 0, 90};
    c.root.children[1].children[0].rotKeys = {k0, k1};
    std::vector<NodePose> poses;
    evaluate(c, 1, poses);
    size_t bar = 0;
    while (bar < poses.size() && poses[bar].node->name != "bar") bar++;
    SkinView sv;
    CHECK(skinView(c, poses, bar, fx.set, glm::mat3(1.0f), &sv) == SkinState::Ok, "the bar's skin can be shown");
    DrawList d;
    appendDraws(c, poses, fx.set, glm::mat3(1.0f), nullptr, d);
    bool same = d.meshes.size() == 1 && sv.vertices.size() == d.vertices.size();
    for (size_t v = 0; same && v < sv.vertices.size(); v++) same = near(sv.vertices[v].x, d.vertices[v].x) && near(sv.vertices[v].y, d.vertices[v].y);
    CHECK(same, "the view's points are where the mesh is drawn");
    CHECK(sv.triangles == fx.meshes.meshes["bar"].triangles && sv.autoWeights, "its triangles; automatic weights");
    CHECK(sv.bones.size() == 3 && sv.boneFound[0] && sv.boneFound[1] && !sv.boneFound[2], "a bone name no node has is marked");
    // low: (0, 0) -> high's origin (0, -16), not moved. high: (0, -16) on to (0, -32), turned 90 degrees round its origin.
    CHECK(near(sv.boneStart[0].x, 0) && near(sv.boneStart[0].y, 0) && near(sv.boneEnd[0].y, -16), "the still bone's segment");
    CHECK(near(sv.boneStart[1].x, 0) && near(sv.boneStart[1].y, -16) && near(sv.boneEnd[1].x, 16) && near(sv.boneEnd[1].y, -16),
          "the turned bone's segment, carried by it");
    bool sums = true;
    for (size_t v = 0; v < sv.vertices.size(); v++) {
        float sum = 0;
        for (int k = 0; k < kSkinInfluences; k++) sum += sv.weights[v * 2 * kSkinInfluences + 2 * k + 1];
        sums = sums && near(sum, 1.0f);
    }
    CHECK(sums, "every point's weights sum to 1");

    // Why there is nothing to show.
    CHECK(skinView(c, poses, poses.size(), fx.set, glm::mat3(1.0f), nullptr) == SkinState::NotSkinned, "no such pose");
    AtlasSet noMeshes;
    noMeshes.add(fx.atlas);
    CHECK(skinView(c, poses, bar, noMeshes, glm::mat3(1.0f), &sv) == SkinState::NoMesh && sv.vertices.empty(), "no mesh");
    c.root.children[0].sprite = "tilted";
    evaluate(c, 1, poses);
    CHECK(skinView(c, poses, bar, fx.set, glm::mat3(1.0f), nullptr) == SkinState::Rotated, "a rotated region");
    c.root.children[0].sprite = "nothere";
    evaluate(c, 1, poses);
    CHECK(skinView(c, poses, bar, fx.set, glm::mat3(1.0f), nullptr) == SkinState::NoSprite, "a sprite no atlas has");
}

void testPlayer() {
    // AnimPlayer::draw: the skinned node goes to drawMesh, in draw order with the quads; the bend follows the time.
    Fixture fx;
    Clip c = skinnedBar();
    Node plain;
    plain.name = "plain";
    plain.sprite = "bar";
    c.root.children.push_back(plain);
    Key<glm::vec3> k0, k1;
    k1.t = 1;
    k1.v = {0, 0, 90};
    c.root.children[1].children[0].rotKeys = {k0, k1};
    AnimPlayer p;
    p.play(&c);
    Recorder r;
    p.draw(&r, fx.set, glm::mat3(1.0f));
    CHECK(r.order == std::vector<char>({'m', 'q'}), "the mesh, then the plain sprite's quad, in draw order");
    const std::vector<MeshVertex> atRest = r.lastMesh;
    p.update(1000);
    r.order.clear();
    p.draw(&r, fx.set, glm::mat3(1.0f));
    CHECK(r.order == std::vector<char>({'m', 'q'}) && r.lastMesh.size() == atRest.size(), "drawn again the next frame");
    bool moved = false;
    for (size_t i = 0; i < atRest.size() && i < r.lastMesh.size(); i++) moved = moved || !near(atRest[i].x, r.lastMesh[i].x);
    CHECK(moved, "the mesh is bent again as the bones move");
}
void testMeshKeys() {
    // An atlas's mesh file (docs/engine/16_MESH_ANIMATION.md): keyed by sprite and the node's grid, shared by every clip using
    // the atlas; what a clip file needs; cut from alpha on the node's grid.
    CHECK(meshKey("golem") == "golem" && meshKey("golem", 6, 8) == "golem@6x8", "meshKey");
    Fixture fx;
    Clip c = skinnedBar();
    Node& bar = c.root.children[0];
    CHECK(fx.set.meshKeyFor("bar", bar) == "bar" && fx.set.findMesh("bar", bar) != nullptr, "no grid: the 8-pixel mesh");
    bar.meshGrid[0] = 1;
    bar.meshGrid[1] = 2;
    CHECK(fx.set.meshKeyFor("bar", bar) == "bar@1x2" && fx.set.findMesh("bar", bar) == nullptr, "a grid: its own key, not cut yet");
    Key<std::string> swap;
    swap.t = 1;
    swap.v = "tilted";
    bar.spriteKeys = {swap};
    AnimFile f;
    f.clips.push_back(c);
    f.clips.push_back(c);   // a second clip using the same: shared
    const std::vector<MeshNeed> needs = meshesNeeded(f, fx.set);
    CHECK(needs.size() == 2 && needs[0].key == "bar@1x2" && needs[1].key == "tilted@1x2" && needs[0].cols == 1 && needs[0].rows == 2,
          "meshesNeeded: the skinned node's rest sprite and its sprite key, on its grid, once for both clips");
    std::vector<uint8_t> px(8 * 32 * 4, 255);
    const toms::SpriteMesh cut = cutMesh(needs[0], px.data(), 8);
    CHECK(cut.vertexCount() == 6 && cut.triangles.size() == 12, "cutMesh: 1 x 2 cells: 6 points, 4 triangles");
    CHECK(cutMesh(needs[1], px.data(), 8).triangles.empty(), "cutMesh: a rotated region has none");
    toms::SpriteMeshFile own;
    own.meshes["bar@1x2"] = cut;
    AtlasSet withOwn = fx.set;
    withOwn.entries[0].meshes = &own;
    CHECK(withOwn.findMesh("bar", bar) == &own.meshes["bar@1x2"], "findMesh: the node's grid's mesh");
}
}  // namespace

int main() {
    testMeshKeys();
    testFile();
    testRest();
    testBend();
    testFallbacks();
    testSkinView();
    testPlayer();
    if (g_fail) { std::printf("anim_skin_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("anim_skin_test: OK\n");
    return 0;
}
