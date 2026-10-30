// anim_player.cpp -- see anim_player.h.
#include "anim_player.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace toms::anim {

glm::mat3 placement(float x, float y, float scale, float rotationDegrees) {
    return flatten(compose({x, y, 0}, {0, 0, rotationDegrees}, {scale, scale, 1}));
}

const toms::AtlasRegion* AtlasSet::find(const std::string& ref, uint16_t* texture, int* entry) const {
    SpriteRef s = parseSpriteRef(ref);
    // An atlas id this set does not have (e.g. the game draws a clip with only its own atlas): look
    // the name up by order instead. The editor's check reports such references.
    if (!s.atlas.empty() &&
        std::none_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.id == s.atlas; }))
        s.atlas.clear();
    for (size_t i = 0; i < entries.size(); i++) {
        const Entry& e = entries[i];
        if (!e.atlas || (!s.atlas.empty() && e.id != s.atlas)) continue;
        const toms::AtlasRegion* r = e.atlas->find(s.name);
        if (!r) {
            if (!s.atlas.empty()) return nullptr;   // the named atlas does not have it: no fallback
            continue;
        }
        if (entry) *entry = (int)i;
        if (texture) {
            const bool own = r->page >= 0 && r->page < (int)e.pageTextures.size();
            *texture = own ? e.pageTextures[(size_t)r->page] : kSpriteAtlasTexture;
        }
        return r;
    }
    return nullptr;
}

std::string meshKey(const std::string& sprite, int cols, int rows) {
    return cols > 0 && rows > 0 ? sprite + "@" + std::to_string(cols) + "x" + std::to_string(rows) : sprite;
}

std::string AtlasSet::meshKeyFor(const std::string& ref, const Node& node, int* entry) const {
    int e = -1;
    const toms::AtlasRegion* r = find(ref, nullptr, &e);
    if (entry) *entry = e;
    if (!r || e < 0) return std::string();
    return node.hasMeshGrid() ? meshKey(r->name, node.meshGrid[0], node.meshGrid[1]) : meshKey(r->name);
}

const toms::SpriteMesh* AtlasSet::findMesh(const std::string& ref, const Node& node) const {
    int entry = -1;
    const std::string key = meshKeyFor(ref, node, &entry);
    if (key.empty() || !entries[(size_t)entry].meshes) return nullptr;
    return entries[(size_t)entry].meshes->find(key);
}

namespace {
void collectNeeds(const Node& n, const AtlasSet& atlases, std::map<std::string, MeshNeed>& out) {
    if (n.usesMesh()) {
        std::vector<std::string> sprites;
        if (!n.sprite.empty()) sprites.push_back(n.sprite);
        for (const auto& k : n.spriteKeys)
            if (!k.v.empty()) sprites.push_back(k.v);
        for (const std::string& s : sprites) {
            MeshNeed need;
            need.region = atlases.find(s, nullptr, &need.entry);
            if (!need.region || need.entry < 0) continue;
            if (n.hasMeshGrid()) { need.cols = n.meshGrid[0]; need.rows = n.meshGrid[1]; }
            need.key = atlases.meshKeyFor(s, n);
            char at[16];
            std::snprintf(at, sizeof at, "%04d|", need.entry);   // by atlas, then key
            out.emplace(at + need.key, need);
        }
    }
    for (const Node& c : n.children) collectNeeds(c, atlases, out);
}
std::vector<MeshNeed> sorted(std::map<std::string, MeshNeed>& m) {
    std::vector<MeshNeed> v;
    for (auto& kv : m) v.push_back(kv.second);
    return v;
}
}  // namespace

std::vector<MeshNeed> meshesNeeded(const Clip& clip, const AtlasSet& atlases) {
    std::map<std::string, MeshNeed> m;
    collectNeeds(clip.root, atlases, m);
    return sorted(m);
}

toms::SpriteMesh cutMesh(const MeshNeed& need, const uint8_t* rgba, int stride) {
    const toms::AtlasRegion* r = need.region;
    if (!r || !rgba || r->rotated || r->child || r->w <= 0 || r->h <= 0) return toms::SpriteMesh();
    if (need.cols > 0 && need.rows > 0)
        return toms::meshFromAlphaGrid(rgba, stride, r->w, r->h, r->offX, r->offY, need.cols, need.rows);
    return toms::meshFromAlpha(rgba, stride, r->w, r->h, r->offX, r->offY, kMeshCell);
}

std::vector<MeshNeed> meshesNeeded(const AnimFile& file, const AtlasSet& atlases) {
    std::map<std::string, MeshNeed> m;
    for (const Clip& c : file.clips) collectNeeds(c.root, atlases, m);
    return sorted(m);
}

void DrawList::submit(IRenderer* ren) const {
    if (!ren) return;
    // the batched run's vertices, reused from call to call
    static thread_local std::vector<MeshVertex> runVerts;
    static thread_local std::vector<float> runColors;
    static thread_local std::vector<uint16_t> runIdx;
    auto texOf = [&](const Item& it) { return it.kind == QuadItem ? quads[it.index].texture : meshes[it.index].texture; };
    auto addOf = [&](const Item& it) { return it.kind == QuadItem ? quads[it.index].additive : meshes[it.index].additive; };
    auto vertsOf = [&](const Item& it) { return it.kind == QuadItem ? 4u : meshes[it.index].vertexCount; };
    size_t i = 0;
    while (i < order.size()) {
        const Item& it = order[i];
        if (it.kind == BeginLayer) { ren->beginLayer(layers[it.index]); i++; continue; }
        if (it.kind == EndLayer) { ren->endLayer(); i++; continue; }
        if (it.kind == QuadItem && quads[it.index].solid) { ren->drawSprite(quads[it.index]); i++; continue; }
        // the run: quads (not solid) and meshes with this texture and blending, at most 65536 vertices (16-bit indices)
        const uint16_t tex = texOf(it);
        const bool add = addOf(it);
        size_t j = i;
        uint32_t verts = 0;
        bool mesh = false;
        while (j < order.size()) {
            const Item& o = order[j];
            if (o.kind == BeginLayer || o.kind == EndLayer || (o.kind == QuadItem && quads[o.index].solid)) break;
            if (texOf(o) != tex || addOf(o) != add || verts + vertsOf(o) > 65536u) break;
            verts += vertsOf(o);
            mesh = mesh || o.kind == MeshItemKind;
            j++;
        }
        if (!mesh) {   // quads only: the sprite batch
            for (; i < j; i++) ren->drawSprite(quads[order[i].index]);
            continue;
        }
        runVerts.clear();
        runColors.clear();
        runIdx.clear();
        for (; i < j; i++) {
            const Item& o = order[i];
            const uint16_t base = uint16_t(runVerts.size());
            if (o.kind == QuadItem) {
                const Quad& q = quads[o.index];
                const float x0 = q.rect[0], y0 = q.rect[1], x1 = q.rect[0] + q.rect[2], y1 = q.rect[1] + q.rect[3];
                const float rc[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
                const float* c = q.hasCorners ? q.corners : rc;
                runVerts.push_back({c[0], c[1], q.uv[0], q.uv[1]});
                runVerts.push_back({c[2], c[3], q.uv[2], q.uv[1]});
                runVerts.push_back({c[4], c[5], q.uv[2], q.uv[3]});
                runVerts.push_back({c[6], c[7], q.uv[0], q.uv[3]});
                for (int k = 0; k < 4; k++) runColors.insert(runColors.end(), q.tint, q.tint + 4);
                for (uint16_t k : {0, 1, 2, 0, 2, 3}) runIdx.push_back(uint16_t(base + k));
            } else {
                const MeshItem& m = meshes[o.index];
                runVerts.insert(runVerts.end(), vertices.begin() + m.firstVertex, vertices.begin() + m.firstVertex + m.vertexCount);
                for (uint32_t k = 0; k < m.vertexCount; k++) runColors.insert(runColors.end(), m.tint, m.tint + 4);
                for (uint32_t k = 0; k < m.indexCount; k++) runIdx.push_back(uint16_t(base + indices[m.firstIndex + k]));
            }
        }
        Mesh batch;
        batch.vertices = runVerts.data();
        batch.vertexCount = uint32_t(runVerts.size());
        batch.indices = runIdx.data();
        batch.indexCount = uint32_t(runIdx.size());
        batch.colors = runColors.data();
        batch.additive = add;
        batch.texture = tex;
        ren->drawMesh(batch);
    }
}

namespace {
void restWalk(const Node& n, const glm::mat4& parent, std::vector<std::pair<const Node*, glm::mat4>>& out) {
    const glm::mat4 w = parent * compose(n.pos, n.rot, n.scale);
    out.push_back({&n, w});
    for (const Node& c : n.children) restWalk(c, w, out);
}
const glm::mat4* restOf(const std::vector<std::pair<const Node*, glm::mat4>>& rest, const Node* n) {
    for (const auto& r : rest) if (r.first == n) return &r.second;
    return nullptr;
}
const std::pair<const Node*, glm::mat4>* restNamed(const std::vector<std::pair<const Node*, glm::mat4>>& rest, const std::string& name) {
    for (const auto& r : rest) if (r.first->name == name) return &r;
    return nullptr;
}
float segmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    const float t = len2 > 1e-9f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + t * ab));
}
}  // namespace

void restWorlds(const Clip& clip, std::vector<std::pair<const Node*, glm::mat4>>& out) {
    out.clear();
    restWalk(clip.root, glm::mat4(1.0f), out);
}

void autoSkinWeights(const std::vector<float>& restVerts, const std::vector<glm::mat4>& boneRest,
                     const std::vector<glm::vec2>& boneEnd, std::vector<float>& out) {
    const size_t verts = restVerts.size() / 2, bones = boneRest.size();
    out.assign(verts * 2 * kSkinInfluences, 0.0f);
    std::vector<std::pair<float, int>> w(bones);
    for (size_t v = 0; v < verts; v++) {
        const glm::vec2 p(restVerts[2 * v], restVerts[2 * v + 1]);
        for (size_t b = 0; b < bones; b++) {
            const float d = segmentDistance(p, glm::vec2(boneRest[b][3]), boneEnd[b]);
            w[b] = {1.0f / (d * d + 1.0f), (int)b};   // inverse square: the nearest bone leads, joints blend
        }
        std::sort(w.begin(), w.end(), [](const std::pair<float, int>& x, const std::pair<float, int>& y) {
            return x.first > y.first || (x.first == y.first && x.second < y.second);
        });
        const size_t k = std::min<size_t>(bones, kSkinInfluences);
        float sum = 0;
        for (size_t i = 0; i < k; i++) sum += w[i].first;
        for (size_t i = 0; i < k; i++) {
            out[v * 2 * kSkinInfluences + 2 * i] = (float)w[i].second;
            out[v * 2 * kSkinInfluences + 2 * i + 1] = sum > 0 ? w[i].first / sum : 0.0f;
        }
    }
}

namespace {
// A skinned pose's mesh bent for this frame, and how (what makeSkinnedMesh draws and skinView shows).
struct Bent {
    const toms::AtlasRegion* region = nullptr;
    uint16_t texture = kSpriteAtlasTexture;
    const toms::SpriteMesh* mesh = nullptr;
    toms::SpriteMesh own;              // a deformed sprite without a mesh file: a plain grid over it
    SkinView view;                     // placed vertices, weights, bones
    // what a SkinRig keeps (a skinned node): the mesh at rest in clip space, each bone's rest transform and pose (-1: none)
    std::vector<float> restV;
    std::vector<glm::mat4> boneRest;
    std::vector<int> bonePose;
    std::vector<const Node*> boneNode;
};

// A plain grid of cols x rows cells over a region's packed pixels (every cell, transparent or not), in original pixels.
toms::SpriteMesh gridMesh(const toms::AtlasRegion& r, int cols, int rows) {
    toms::SpriteMesh m;
    for (int y = 0; y <= rows; y++)
        for (int x = 0; x <= cols; x++) {
            m.vertices.push_back((float)r.offX + (float)r.w * x / cols);
            m.vertices.push_back((float)r.offY + (float)r.h * y / rows);
        }
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++) {
            const uint16_t a = (uint16_t)(y * (cols + 1) + x), b = (uint16_t)(a + 1), c = (uint16_t)(a + cols + 2), d = (uint16_t)(a + cols + 1);
            m.triangles.insert(m.triangles.end(), {a, b, c, a, c, d});
        }
    return m;
}
}  // namespace

glm::vec2 latticeOffset(const std::vector<glm::vec2>& offsets, int cols, int rows, float x, float y, float origW, float origH) {
    if (cols < 2 || rows < 2 || offsets.size() < (size_t)cols * rows || origW <= 0 || origH <= 0) return glm::vec2(0.0f);
    const float u = std::clamp(x / origW, 0.0f, 1.0f) * (cols - 1), v = std::clamp(y / origH, 0.0f, 1.0f) * (rows - 1);
    const int c0 = std::min((int)u, cols - 2), r0 = std::min((int)v, rows - 2);
    const float fu = u - c0, fv = v - r0;
    auto at = [&](int c, int r) { return offsets[(size_t)r * cols + c]; };
    return (at(c0, r0) * (1 - fu) + at(c0 + 1, r0) * fu) * (1 - fv) + (at(c0, r0 + 1) * (1 - fu) + at(c0 + 1, r0 + 1) * fu) * fv;
}

namespace {

// Automatic weights depend only on what they are made from -- the mesh at rest and the bones' rest segments -- so they
// are made once and kept by a hash of exactly that (an edit of the rest pose, another mesh: other weights). Without
// this, every skinned node made them again every frame.
const std::vector<float>& cachedAutoWeights(const std::vector<float>& restV, const std::vector<glm::mat4>& boneRest,
                                            const std::vector<glm::vec2>& boneEnd) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    };
    mix(restV.data(), restV.size() * sizeof(float));
    for (const glm::mat4& m : boneRest) mix(&m[3][0], 2 * sizeof(float));
    mix(boneEnd.data(), boneEnd.size() * sizeof(glm::vec2));
    const size_t n = boneRest.size();
    mix(&n, sizeof n);
    static thread_local std::unordered_map<uint64_t, std::vector<float>> cache;
    auto it = cache.find(h);
    if (it != cache.end()) return it->second;
    if (cache.size() > 512) cache.clear();   // an editor dragging bones at rest makes new ones: keep it bounded
    std::vector<float>& w = cache[h];
    autoSkinWeights(restV, boneRest, boneEnd, w);
    return w;
}

SkinState bend(const std::vector<NodePose>& poses, size_t i, const std::vector<std::pair<const Node*, glm::mat4>>& rest,
               const AtlasSet& atlases, const glm::mat3& place, Bent& out) {
    const NodePose& p = poses[i];
    if (!p.node || !p.node->usesMesh()) return SkinState::NotSkinned;
    const Node& n = *p.node;
    const toms::AtlasRegion* r = p.sprite.empty() ? nullptr : atlases.find(p.sprite, &out.texture);
    if (!r || r->w <= 0 || r->h <= 0) return SkinState::NoSprite;
    if (r->rotated) return SkinState::Rotated;   // meshes are cut from unrotated pixels: drawn as a quad
    const toms::SpriteMesh* mesh = atlases.findMesh(p.sprite, n);
    if ((!mesh || mesh->triangles.empty()) && !n.skinned()) {   // a deform only: a plain grid, twice the lattice
        out.own = gridMesh(*r, (n.latticeCols() - 1) * 2, (n.latticeRows() - 1) * 2);
        mesh = &out.own;
    }
    const glm::mat4* nodeRest = restOf(rest, &n);
    if (!mesh || mesh->triangles.empty() || (n.skinned() && !nodeRest)) return SkinState::NoMesh;
    out.region = r;
    out.mesh = mesh;
    SkinView& v = out.view;
    auto placed = [&](const glm::vec2& c) { const glm::vec3 s = place * glm::vec3(c, 1.0f); return glm::vec2(s); };
    // Each vertex around the pivot, moved by the lattice (at the node's own time): the shape the rest works on.
    const glm::vec2 pivot = n.hasPivot ? n.pivot : glm::vec2(r->pivot[0], r->pivot[1]);
    const uint32_t verts = mesh->vertexCount();
    const std::vector<glm::vec2> lattice = n.deformed() ? deformAt(n, p.time) : std::vector<glm::vec2>();
    auto local = [&](uint32_t k) {
        const float x = mesh->vertices[2 * k], y = mesh->vertices[2 * k + 1];
        glm::vec2 l(x - pivot.x * r->origW, y - pivot.y * r->origH);
        if (!lattice.empty()) l += latticeOffset(lattice, n.latticeCols(), n.latticeRows(), x, y, (float)r->origW, (float)r->origH);
        return l;
    };
    if (!n.skinned()) {   // a deform only: the bent shape, carried by the node itself
        v.vertices.resize(verts);
        for (uint32_t k = 0; k < verts; k++) v.vertices[k] = placed(glm::vec2(p.world * glm::vec4(local(k), 0, 1)));
        v.triangles = mesh->triangles;
        return SkinState::Ok;
    }

    // The bones: rest transform, where they are now (a bone left out of the poses, e.g. by an editor's solo,
    // stays at rest), and the segment the automatic weights measure against. A bone name no node has is the
    // skinned node itself.
    const size_t bones = n.skinBones.size();
    std::vector<glm::mat4> boneRest(bones), boneSkin(bones);
    std::vector<glm::vec2> boneEnd(bones);
    v.bones = n.skinBones;
    v.boneFound.assign(bones, false);
    for (size_t b = 0; b < bones; b++) {
        const auto* named = restNamed(rest, n.skinBones[b]);
        v.boneFound[b] = named != nullptr;
        const Node* bn = named ? named->first : &n;
        boneRest[b] = named ? named->second : *nodeRest;
        glm::mat4 now = boneRest[b];
        int at = -1;
        for (size_t qi = 0; qi < poses.size(); qi++)
            if (poses[qi].node == bn) { now = poses[qi].world; at = (int)qi; break; }
        out.bonePose.push_back(at);
        out.boneNode.push_back(bn);
        boneSkin[b] = std::fabs(glm::determinant(boneRest[b])) > 1e-12f ? now * glm::inverse(boneRest[b]) : glm::mat4(1.0f);
        // A bone reaches to its first child that is a bone; a last bone (a head, a hand) reaches on past its origin as
        // far as from its parent bone to it, so what lies beyond it follows it.
        boneEnd[b] = glm::vec2(boneRest[b][3]);
        bool reaches = false;
        for (const Node& c : bn->children)
            if (std::find(n.skinBones.begin(), n.skinBones.end(), c.name) != n.skinBones.end())
                if (const glm::mat4* cr = restOf(rest, &c)) { boneEnd[b] = glm::vec2((*cr)[3]); reaches = true; break; }
        if (!reaches)
            for (const auto& rr : rest)
                if (std::find(n.skinBones.begin(), n.skinBones.end(), rr.first->name) != n.skinBones.end() &&
                    std::any_of(rr.first->children.begin(), rr.first->children.end(), [&](const Node& c) { return &c == bn; })) {
                    boneEnd[b] = 2.0f * glm::vec2(boneRest[b][3]) - glm::vec2(rr.second[3]);
                    break;
                }
        // The segment as it is now: carried by its own bone.
        v.boneStart.push_back(placed(glm::vec2(boneSkin[b] * glm::vec4(glm::vec2(boneRest[b][3]), 0, 1))));
        v.boneEnd.push_back(placed(glm::vec2(boneSkin[b] * glm::vec4(boneEnd[b], 0, 1))));
    }

    // The mesh at rest in clip space: its (deformed) vertices through the node's rest transform; the bones carry it.
    std::vector<float> restV((size_t)verts * 2);
    for (uint32_t k = 0; k < verts; k++) {
        const glm::vec4 c = *nodeRest * glm::vec4(local(k), 0, 1);
        restV[2 * k] = c.x;
        restV[2 * k + 1] = c.y;
    }
    out.boneRest = boneRest;
    v.autoWeights = n.skinWeights.size() != (size_t)verts * 2 * kSkinInfluences;   // automatic, or painted for another mesh
    if (v.autoWeights) v.weights = cachedAutoWeights(restV, boneRest, boneEnd);
    else v.weights = n.skinWeights;

    v.vertices.resize(verts);
    for (uint32_t k = 0; k < verts; k++) {
        const glm::vec4 restPos(restV[2 * k], restV[2 * k + 1], 0, 1);
        glm::vec4 acc(0.0f);
        float sum = 0;
        for (int j = 0; j < kSkinInfluences; j++) {
            const float w = v.weights[(size_t)k * 2 * kSkinInfluences + 2 * j + 1];
            const int b = (int)v.weights[(size_t)k * 2 * kSkinInfluences + 2 * j];
            if (w == 0.0f || b < 0 || b >= (int)bones) continue;
            acc += w * (boneSkin[(size_t)b] * restPos);
            sum += w;
        }
        acc = sum > 0 ? acc / sum : restPos;
        v.vertices[k] = placed(glm::vec2(acc));
    }
    v.triangles = mesh->triangles;
    out.restV = std::move(restV);
    return SkinState::Ok;
}
}  // namespace

struct SkinRigCache {
    struct Rig {
        std::string sprite;                       // the sprite it was made for (sprite keys switch meshes)
        const toms::SpriteMesh* mesh = nullptr;
        uint16_t texture = kSpriteAtlasTexture;
        std::vector<float> restV, uv, weights;    // per vertex: at rest (clip space), texture, kSkinInfluences pairs
        std::vector<int> bonePose;                // per bone: its pose, -1 = stays at rest
        std::vector<const Node*> boneNode;
        std::vector<glm::mat4> restInv;           // per bone: its rest transform inverted (identity when degenerate)
    };
    std::unordered_map<const Node*, std::vector<Rig>> rigs;
};

namespace {
// The shared caches, by clip and atlas set, held weakly: the players keep them alive. fresh: a new one replaces the
// shared one (the atlases were reloaded under the same AtlasSet; players still on the old one keep it until they ask
// again).
std::shared_ptr<SkinRigCache> sharedRigs(const Clip* clip, const AtlasSet* atlases, bool fresh) {
    static std::map<std::pair<const Clip*, const AtlasSet*>, std::weak_ptr<SkinRigCache>> shared;
    static int calls = 0;
    if (++calls % 256 == 0)   // now and then: forget the ones nobody plays any more
        for (auto it = shared.begin(); it != shared.end();) it = it->second.expired() ? shared.erase(it) : std::next(it);
    std::weak_ptr<SkinRigCache>& slot = shared[{clip, atlases}];
    std::shared_ptr<SkinRigCache> c = fresh ? nullptr : slot.lock();
    if (!c) {
        c = std::make_shared<SkinRigCache>();
        slot = c;
    }
    return c;
}
}  // namespace

bool makeSkinnedMesh(const std::vector<NodePose>& poses, size_t i, const std::vector<std::pair<const Node*, glm::mat4>>& rest,
                     const AtlasSet& atlases, const glm::mat3& place, const glm::vec4& t, DrawList& out, SkinRigCache* rigs) {
    const NodePose& p = poses[i];
    if (!p.visible || p.color.a <= 0.0f) return false;
    // A skinned node without a lattice, its rig made: only its bones are new this frame.
    const bool cachable = rigs && p.node->skinned() && !p.node->deformed();
    if (cachable) {
        for (const SkinRigCache::Rig& g : rigs->rigs[p.node]) {
            if (g.sprite != p.sprite) continue;
            const size_t bones = g.bonePose.size();
            glm::mat4 skin[16];
            for (size_t b = 0; b < bones && b < 16; b++) {
                const int at = g.bonePose[b];
                skin[b] = at >= 0 && (size_t)at < poses.size() && poses[(size_t)at].node == g.boneNode[b] ? poses[(size_t)at].world * g.restInv[b]
                                                                                                          : glm::mat4(1.0f);
            }
            DrawList::MeshItem item;
            item.firstVertex = (uint32_t)out.vertices.size();
            item.vertexCount = g.mesh->vertexCount();
            item.firstIndex = (uint32_t)out.indices.size();
            item.indexCount = (uint32_t)g.mesh->triangles.size();
            for (uint32_t v = 0; v < item.vertexCount; v++) {
                const glm::vec4 restPos(g.restV[2 * v], g.restV[2 * v + 1], 0, 1);
                glm::vec4 acc(0.0f);
                float sum = 0;
                for (int j = 0; j < kSkinInfluences; j++) {
                    const float w = g.weights[(size_t)v * 2 * kSkinInfluences + 2 * j + 1];
                    const int b = (int)g.weights[(size_t)v * 2 * kSkinInfluences + 2 * j];
                    if (w == 0.0f || b < 0 || b >= (int)bones || b >= 16) continue;
                    acc += w * (skin[b] * restPos);
                    sum += w;
                }
                acc = sum > 0 ? acc / sum : restPos;
                const glm::vec3 s = place * glm::vec3(acc.x, acc.y, 1.0f);
                out.vertices.push_back({s.x, s.y, g.uv[2 * v], g.uv[2 * v + 1]});
            }
            out.indices.insert(out.indices.end(), g.mesh->triangles.begin(), g.mesh->triangles.end());
            const glm::vec4 col = p.color * t;
            item.tint[0] = col.r; item.tint[1] = col.g; item.tint[2] = col.b; item.tint[3] = col.a;
            item.additive = p.node->blend == Blend::Add;
            item.texture = g.texture;
            out.order.push_back({DrawList::MeshItemKind, (uint32_t)out.meshes.size()});
            out.meshes.push_back(item);
            return true;
        }
    }
    Bent b;
    if (bend(poses, i, rest, atlases, place, b) != SkinState::Ok) return false;
    const toms::AtlasRegion* r = b.region;
    const toms::SpriteMesh* mesh = b.mesh;
    DrawList::MeshItem item;
    item.firstVertex = (uint32_t)out.vertices.size();
    item.vertexCount = mesh->vertexCount();
    item.firstIndex = (uint32_t)out.indices.size();
    item.indexCount = (uint32_t)mesh->triangles.size();
    // The UVs stay inside the region: a mesh cut from another packing or art style (the game's sprite atlas follows the
    // chosen style, a clip file has one mesh file) must not sample the neighbouring sprites.
    const float du = (r->uv[2] - r->uv[0]) / r->w, dv = (r->uv[3] - r->uv[1]) / r->h;
    SkinRigCache::Rig* rig = nullptr;
    if (cachable && b.boneRest.size() <= 16) {   // keep what the next frames need
        rigs->rigs[p.node].push_back(SkinRigCache::Rig());
        rig = &rigs->rigs[p.node].back();
        rig->sprite = p.sprite;
        rig->mesh = mesh;
        rig->texture = b.texture;
        rig->restV = b.restV;
        rig->weights = b.view.weights;
        rig->bonePose = b.bonePose;
        rig->boneNode = b.boneNode;
        for (const glm::mat4& m : b.boneRest)
            rig->restInv.push_back(std::fabs(glm::determinant(m)) > 1e-12f ? glm::inverse(m) : glm::mat4(1.0f));
    }
    for (uint32_t v = 0; v < item.vertexCount; v++) {
        const float px = std::clamp(mesh->vertices[2 * v] - r->offX, 0.0f, (float)r->w);
        const float py = std::clamp(mesh->vertices[2 * v + 1] - r->offY, 0.0f, (float)r->h);
        out.vertices.push_back({b.view.vertices[v].x, b.view.vertices[v].y, r->uv[0] + px * du, r->uv[1] + py * dv});
        if (rig) { rig->uv.push_back(r->uv[0] + px * du); rig->uv.push_back(r->uv[1] + py * dv); }
    }
    out.indices.insert(out.indices.end(), mesh->triangles.begin(), mesh->triangles.end());
    const glm::vec4 col = p.color * t;
    item.tint[0] = col.r; item.tint[1] = col.g; item.tint[2] = col.b; item.tint[3] = col.a;
    item.additive = p.node->blend == Blend::Add;
    item.texture = b.texture;
    out.order.push_back({DrawList::MeshItemKind, (uint32_t)out.meshes.size()});
    out.meshes.push_back(item);
    return true;
}

SkinState skinView(const Clip& clip, const std::vector<NodePose>& poses, size_t i, const AtlasSet& atlases,
                   const glm::mat3& place, SkinView* out) {
    if (i >= poses.size()) return SkinState::NotSkinned;
    std::vector<std::pair<const Node*, glm::mat4>> rest;
    restWorlds(clip, rest);
    Bent b;
    const SkinState s = bend(poses, i, rest, atlases, place, b);
    if (out) *out = s == SkinState::Ok ? std::move(b.view) : SkinView();
    return s;
}

namespace {
void findMissingMeshes(const Node& n, const std::string& parent, const AtlasSet& atlases, std::vector<MissingMesh>& out) {
    const std::string path = parent.empty() ? n.name : parent + "/" + n.name;
    if (n.skinned()) {
        std::vector<std::string> sprites;
        if (!n.sprite.empty()) sprites.push_back(n.sprite);
        for (const auto& k : n.spriteKeys)
            if (!k.v.empty()) sprites.push_back(k.v);
        std::sort(sprites.begin(), sprites.end());
        sprites.erase(std::unique(sprites.begin(), sprites.end()), sprites.end());
        for (const std::string& s : sprites) {
            if (!atlases.find(s)) continue;
            const toms::SpriteMesh* m = atlases.findMesh(s, n);
            if (!m || m->triangles.empty()) out.push_back({path, s});
        }
    }
    for (const Node& c : n.children) findMissingMeshes(c, path, atlases, out);
}
}  // namespace

std::vector<MissingMesh> skinnedWithoutMesh(const Clip& clip, const AtlasSet& atlases) {
    std::vector<MissingMesh> out;
    findMissingMeshes(clip.root, std::string(), atlases, out);
    return out;
}

// ---- effects (docs/engine/18_NODE_EFFECTS.md) ---------------------------------------------------

namespace {
void drawPose(const Clip& clip, const std::vector<NodePose>& poses, size_t i, const AtlasSet& atlases, const glm::mat3& place,
              const glm::vec4& t, std::vector<std::pair<const Node*, glm::mat4>>& rest, bool& restMade, DrawList& out,
              std::vector<std::string>* missing);

float placementScale(const glm::mat3& m) { return std::sqrt(std::fabs(m[0][0] * m[1][1] - m[1][0] * m[0][1])); }

// Effects around subtrees: before pose j, the afterimages and layers of every node whose subtree starts at j (outer
// nodes first); after pose j, the layers whose subtree ends there (inner first). A subtree's poses are contiguous
// (NodePose::first / end), so its layer holds exactly it.
struct EffectWalk {
    const Clip& clip;
    const std::vector<NodePose>& poses;
    bool any = false;
    std::vector<size_t> nodes;      // poses with an effect or a trail that shows, by first pose, outer first
    size_t next = 0;
    std::vector<uint32_t> open;     // the open layers' ends

    EffectWalk(const Clip& c, const std::vector<NodePose>& p, bool effects) : clip(c), poses(p) {
        if (!effects) return;
        for (size_t i = 0; i < p.size(); i++) {
            const NodePose& q = p[i];
            if (!q.node || !q.visible || q.color.a <= 0.0f) continue;   // hidden: so is its subtree
            if (q.node->hasFx() || (q.node->hasTrail() && trailAt(*q.node, q.time) > 0.0f)) nodes.push_back(i);
        }
        std::stable_sort(nodes.begin(), nodes.end(), [&](size_t a, size_t b) {
            return p[a].first != p[b].first ? p[a].first < p[b].first : p[a].depth < p[b].depth;
        });
        any = !nodes.empty();
    }
    void before(size_t j, const AtlasSet& atlases, const std::vector<std::pair<const Node*, glm::mat4>>& rest,
                const glm::mat3& place, const glm::vec4& t, DrawList& out) {
        for (; next < nodes.size() && poses[nodes[next]].first == j; next++) {
            const size_t i = nodes[next];
            const Node& n = *poses[i].node;
            if (n.hasTrail()) appendTrail(clip, poses, i, atlases, rest, place, t, out);   // behind it, outside its layer
            LayerEffect fx;
            if (n.hasFx() && nodeLayerEffect(clip, poses, i, place, fx)) {
                out.beginLayer(fx);
                open.push_back(poses[i].end);
            }
        }
    }
    void after(size_t j, DrawList& out) {
        while (!open.empty() && open.back() <= j + 1) {
            out.endLayer();
            open.pop_back();
        }
    }
};
}  // namespace

bool nodeLayerEffect(const Clip& clip, const std::vector<NodePose>& poses, size_t i, const glm::mat3& place, LayerEffect& out) {
    if (i >= poses.size() || !poses[i].node || !poses[i].node->hasFx()) return false;
    const NodePose& p = poses[i];
    if (!p.visible || p.color.a <= 0.0f) return false;
    const Node& n = *p.node;
    const glm::vec4 v = fxParamsAt(n, p.time);
    const float scale = placementScale(place);
    out = LayerEffect();
    for (int k = 0; k < 4; k++) out.color[k] = n.fx.color[k];
    switch (n.fx.shader) {
    case FxShader::None: return false;
    case FxShader::Group: out.kind = LayerEffect::None; break;
    case FxShader::MotionBlur: {
        out.kind = LayerEffect::MotionBlur;
        glm::vec2 d(place * glm::vec3(v.x, v.y, 0.0f));   // a vector: the placement's turn and scale, not its move
        if (v.z > 0.0f) {   // + its own movement over the last 1/30 s
            static thread_local std::vector<NodePose> was;   // reused: no allocation a frame
            evaluate(clip, std::max(0.0f, p.clipTime - 1.0f / 30.0f), was);
            if (i < was.size() && was[i].node == p.node) {
                const glm::vec2 a(place * flatten(was[i].world) * glm::vec3(0, 0, 1)), b(place * flatten(p.world) * glm::vec3(0, 0, 1));
                d += v.z * (b - a);
            }
        }
        if (glm::length(d) < 0.5f) return false;   // nothing to smear: drawn as it is
        out.params[0] = d.x;
        out.params[1] = d.y;
        break;
    }
    case FxShader::Glow:
        out.kind = LayerEffect::Glow;
        out.params[0] = v.x;
        out.params[1] = v.y * scale;
        out.params[2] = v.z;
        out.params[3] = v.w;
        break;
    case FxShader::Flash:
        if (v.x <= 0.0f) return false;
        out.kind = LayerEffect::Flash;
        out.params[0] = v.x;
        break;
    case FxShader::Outline:
        if (v.x <= 0.0f || n.fx.color.a <= 0.0f) return false;
        out.kind = LayerEffect::Outline;
        out.params[0] = v.x * scale;
        out.params[1] = v.y;
        break;
    }
    return true;
}

void appendTrail(const Clip& clip, const std::vector<NodePose>& poses, size_t i, const AtlasSet& atlases,
                 const std::vector<std::pair<const Node*, glm::mat4>>& rest, const glm::mat3& place, const glm::vec4& t,
                 DrawList& out) {
    if (i >= poses.size() || !poses[i].node) return;
    const NodePose& p = poses[i];
    const NodeTrail& tr = p.node->trail;
    const float strength = trailAt(*p.node, p.time);
    if (tr.count <= 0 || tr.gap <= 0.0f || strength <= 0.0f) return;
    static thread_local std::vector<NodePose> ghost;   // reused for every afterimage: no allocation a frame
    for (int k = std::min(tr.count, kMaxTrail); k >= 1; k--) {   // the oldest first: the newer ones over it
        const float when = p.clipTime - (float)k * tr.gap;
        if (when < 0.0f) continue;
        evaluate(clip, when, ghost);
        if (i >= ghost.size() || ghost[i].node != p.node) continue;
        const float fade = (float)(tr.count - k + 1) / (float)tr.count;
        const glm::vec4 tint = t * glm::vec4(tr.color.r, tr.color.g, tr.color.b, tr.color.a * strength * fade);
        for (uint32_t g = ghost[i].first; g < ghost[i].end && g < ghost.size(); g++) {
            const size_t quads = out.quads.size(), meshes = out.meshes.size();
            if (!(ghost[g].node && ghost[g].node->usesMesh() && makeSkinnedMesh(ghost, g, rest, atlases, place, tint, out))) {
                Quad q;
                if (makeQuad(ghost[g], atlases, place, tint, q)) out.addQuad(q);
            }
            if (tr.additive) {
                for (size_t k2 = quads; k2 < out.quads.size(); k2++) out.quads[k2].additive = true;
                for (size_t k2 = meshes; k2 < out.meshes.size(); k2++) out.meshes[k2].additive = true;
            }
        }
    }
}

void appendDraws(const Clip& clip, const std::vector<NodePose>& poses, const AtlasSet& atlases, const glm::mat3& place,
                 const float tint[4], DrawList& out, std::vector<std::string>* missing) {
    const glm::vec4 t = tint ? glm::vec4(tint[0], tint[1], tint[2], tint[3]) : glm::vec4(1.0f);
    std::vector<std::pair<const Node*, glm::mat4>> rest;
    bool restMade = false;
    EffectWalk fx(clip, poses, hasEffects(clip));
    for (size_t i = 0; i < poses.size(); i++) {
        if (fx.any) {
            if (!restMade) { restWorlds(clip, rest); restMade = true; }
            fx.before(i, atlases, rest, place, t, out);
        }
        drawPose(clip, poses, i, atlases, place, t, rest, restMade, out, missing);
        if (fx.any) fx.after(i, out);
    }
}

namespace {
void drawPose(const Clip& clip, const std::vector<NodePose>& poses, size_t i, const AtlasSet& atlases, const glm::mat3& place,
              const glm::vec4& t, std::vector<std::pair<const Node*, glm::mat4>>& rest, bool& restMade, DrawList& out,
              std::vector<std::string>* missing) {
    Quad q;
    {
        const NodePose& p = poses[i];
        if (p.node && p.node->usesMesh()) {
            if (!restMade) { restWorlds(clip, rest); restMade = true; }
            if (makeSkinnedMesh(poses, i, rest, atlases, place, t, out)) return;
        }
        const toms::AtlasRegion* r = nullptr;
        if (makeQuad(p, atlases, place, t, q, &r)) {
            out.addQuad(q);
        } else if (!r && missing && p.visible && !p.sprite.empty() && p.color.a > 0.0f &&
                   std::find(missing->begin(), missing->end(), p.sprite) == missing->end()) {
            missing->push_back(p.sprite);
        }
    }
}
}  // namespace

bool makeQuad(const NodePose& p, const AtlasSet& atlases, const glm::mat3& place, const glm::vec4& t, Quad& q,
              const toms::AtlasRegion** region) {
    if (region) *region = nullptr;
    if (!p.visible || p.sprite.empty() || p.color.a <= 0.0f) return false;
    uint16_t texture = kSpriteAtlasTexture;
    const toms::AtlasRegion* r = atlases.find(p.sprite, &texture);
    if (region) *region = r;
    if (!r) return false;
    // The packed (trimmed) pixels, placed inside the original image, relative to the pivot.
    const glm::vec2 pivot = p.node->hasPivot ? p.node->pivot : glm::vec2(r->pivot[0], r->pivot[1]);
    const float x0 = r->offX - pivot.x * r->origW, y0 = r->offY - pivot.y * r->origH;
    const float x1 = x0 + r->w, y1 = y0 + r->h;
    const glm::mat3 m = place * flatten(p.world);   // the orthographic screen camera
    const glm::vec3 c[4] = {m * glm::vec3(x0, y0, 1), m * glm::vec3(x1, y0, 1), m * glm::vec3(x1, y1, 1),
                            m * glm::vec3(x0, y1, 1)};
    q = Quad();
    q.hasCorners = true;
    float minX = c[0].x, minY = c[0].y, maxX = c[0].x, maxY = c[0].y;
    for (int i = 0; i < 4; i++) {
        q.corners[i * 2] = c[i].x;
        q.corners[i * 2 + 1] = c[i].y;
        minX = std::min(minX, c[i].x); maxX = std::max(maxX, c[i].x);
        minY = std::min(minY, c[i].y); maxY = std::max(maxY, c[i].y);
    }
    q.rect[0] = minX; q.rect[1] = minY; q.rect[2] = maxX - minX; q.rect[3] = maxY - minY;   // bounds
    std::copy(r->uv, r->uv + 4, q.uv);
    const glm::vec4 col = p.color * t;
    q.tint[0] = col.r; q.tint[1] = col.g; q.tint[2] = col.b; q.tint[3] = col.a;
    q.additive = p.node->blend == Blend::Add;
    q.texture = texture;
    return true;
}

void appendQuads(const std::vector<NodePose>& poses, const AtlasSet& atlases, const glm::mat3& place,
                 const float tint[4], std::vector<Quad>& out, std::vector<std::string>* missing) {
    const glm::vec4 t = tint ? glm::vec4(tint[0], tint[1], tint[2], tint[3]) : glm::vec4(1.0f);
    Quad q;
    for (const NodePose& p : poses) {
        const toms::AtlasRegion* r = nullptr;
        if (makeQuad(p, atlases, place, t, q, &r)) {
            out.push_back(q);
        } else if (!r && missing && p.visible && !p.sprite.empty() && p.color.a > 0.0f &&
                   std::find(missing->begin(), missing->end(), p.sprite) == missing->end()) {
            missing->push_back(p.sprite);
        }
    }
}

// ---- AnimPlayer --------------------------------------------------------------------------------

void AnimPlayer::play(const Clip* clip) {
    clip_ = clip;
    cache_.bind(clip);
    rest_.clear();
    if (clip) restWorlds(*clip, rest_);
    loops_ = clip && hasLoopingNodes(*clip);
    effects_ = clip && hasEffects(*clip);
    rigs_.reset();   // the shared one is asked for at the first draw (it needs the atlas set)
    rigAtlases_ = nullptr;
    rigsStale_ = false;
    quadAtlases_ = nullptr;
    elapsed_ = 0;
    started_ = false;
    events_.clear();
}

double AnimPlayer::total() const {
    if (!clip_ || clip_->playCount < 0) return -1;
    return (double)clip_->playCount * clip_->duration();
}

bool AnimPlayer::finished() const {
    const double tot = total();
    return clip_ && tot >= 0 && elapsed_ >= tot;
}

bool AnimPlayer::showing() const { return clip_ && (!finished() || clip_->stayAtLastFrame); }

float AnimPlayer::time() const {
    if (!clip_) return 0;
    const double d = clip_->duration();
    if (d <= 0) return 0;
    if (finished()) return (float)d;
    return (float)std::fmod(elapsed_, d);
}

float AnimPlayer::poseTime() const {
    if (!clip_ || !finished() || !loops_) return time();
    const double d = clip_->duration();
    return (float)(elapsed_ - (double)(clip_->playCount - 1) * d);   // into the last play, past its end
}

void AnimPlayer::seek(double seconds) {
    if (!clip_) return;
    elapsed_ = std::max(0.0, seconds);
    started_ = true;
    events_.clear();
}

void AnimPlayer::update(int dtMs) {
    if (!clip_) return;
    const double d = clip_->duration();
    const double tot = total();
    if (!started_) {   // keys at time 0 belong to the first update
        eventsBetween(*clip_, -1.0f, 0.0f, events_);
        started_ = true;
    }
    double a = elapsed_;
    elapsed_ += std::max(0, dtMs) / 1000.0;
    const double b = tot >= 0 ? std::min(elapsed_, tot) : elapsed_;
    if (d <= 0) return;
    // Walk the passed time one play at a time, so every event in it is sent once, in order.
    while (a < b) {
        double loopStart = std::floor(a / d) * d;
        if (loopStart + d <= a) loopStart += d;   // rounding put `a` on a boundary: that is the next play
        const double end = std::min(b, loopStart + d);
        const float t0 = a == loopStart && a > 0 ? -1.0f : (float)(a - loopStart);   // a new play: include time 0
        eventsBetween(*clip_, t0, (float)(end - loopStart), events_);
        a = end;
    }
}

std::vector<std::string> AnimPlayer::takeEvents() {
    std::vector<std::string> out;
    out.swap(events_);
    return out;
}

void AnimPlayer::draw(IRenderer* ren, const AtlasSet& atlases, const glm::mat3& place, const float tint[4]) const {
    if (!ren || !showing()) return;
    cache_.seek(poseTime());
    const std::vector<NodePose>& poses = cache_.poses();
    const glm::vec4 t = tint ? glm::vec4(tint[0], tint[1], tint[2], tint[3]) : glm::vec4(1.0f);
    const bool all = quads_.size() != poses.size() || quadAtlases_ != &atlases || quadPlace_ != place || quadTint_ != t;
    if (all) {
        quads_.assign(poses.size(), Quad());
        hasQuad_.assign(poses.size(), 0);
        quadAtlases_ = &atlases;
        quadPlace_ = place;
        quadTint_ = t;
    }
    // The frame's draws in order -- the cached quads, the bent meshes, effect layers and afterimages -- then submitted
    // together, so the parts of one texture go as one batch.
    DrawList& frame = meshScratch_;
    frame.clear();
    if (rigAtlases_ != &atlases || !rigs_) {   // the clip's shared cache for these atlases (a reload: a new one)
        rigs_ = sharedRigs(clip_, &atlases, rigsStale_);
        rigAtlases_ = &atlases;
        rigsStale_ = false;
    }
    EffectWalk fx(*clip_, poses, effects_);
    for (size_t i = 0; i < poses.size(); i++) {
        if (fx.any) fx.before(i, atlases, rest_, place, t, frame);   // afterimages and effect layers that start here
        bool drawn = false;
        if (poses[i].node && poses[i].node->usesMesh())   // bent again every frame: its bones / lattice may move
            drawn = makeSkinnedMesh(poses, i, rest_, atlases, place, t, frame, rigs_.get());
        if (!drawn) {
            if (all || cache_.changed(i)) hasQuad_[i] = makeQuad(poses[i], atlases, place, t, quads_[i]);
            if (hasQuad_[i]) frame.addQuad(quads_[i]);
        }
        if (fx.any) fx.after(i, frame);
    }
    frame.submit(ren);
}

// ---- the lattice --------------------------------------------------------------------------------

bool latticeView(const std::vector<NodePose>& poses, size_t i, const AtlasSet& atlases, const glm::mat3& place, LatticeView& out) {
    if (i >= poses.size() || !poses[i].node || poses[i].sprite.empty()) return false;
    const NodePose& p = poses[i];
    const Node& n = *p.node;
    const toms::AtlasRegion* r = atlases.find(p.sprite);
    if (!r || r->origW <= 0 || r->origH <= 0) return false;
    out.cols = n.latticeCols();
    out.rows = n.latticeRows();
    const glm::vec2 pivot = n.hasPivot ? n.pivot : glm::vec2(r->pivot[0], r->pivot[1]);
    // sprite pixels -> placed: around the pivot, through the node's world transform, placed
    glm::mat3 toLocal(1.0f);
    toLocal[2] = glm::vec3(-pivot.x * r->origW, -pivot.y * r->origH, 1.0f);
    out.spriteToPlaced = place * flatten(p.world) * toLocal;
    const std::vector<glm::vec2> offsets = deformAt(n, p.time);
    out.rest.clear();
    out.points.clear();
    for (int row = 0; row < out.rows; row++)
        for (int col = 0; col < out.cols; col++) {
            const glm::vec2 at((float)r->origW * col / (out.cols - 1), (float)r->origH * row / (out.rows - 1));
            out.rest.push_back(at);
            out.points.push_back(glm::vec2(out.spriteToPlaced * glm::vec3(at + offsets[(size_t)row * out.cols + col], 1.0f)));
        }
    out.offsets = offsets;
    return true;
}

// ---- CharacterRig ------------------------------------------------------------------------------

bool attachPlacement(const std::vector<NodePose>& poses, const std::string& node, const glm::mat3& place, glm::mat3& out) {
    for (const NodePose& p : poses)
        if (p.node && p.node->name == node) {
            out = place * flatten(p.world);
            return true;
        }
    return false;
}

void CharacterRig::play(const Clip* character, const Clip* held) {
    body_.play(character);
    held_.play(held);
}

void CharacterRig::update(int dtMs) {
    body_.update(dtMs);
    held_.update(dtMs);
}

std::vector<std::string> CharacterRig::takeEvents() {
    std::vector<std::string> out = body_.takeEvents();
    for (std::string& e : held_.takeEvents()) out.push_back(std::move(e));
    return out;
}

bool CharacterRig::finished() const {
    return (!body_.clip() || body_.finished()) && (!held_.clip() || held_.finished());
}

void CharacterRig::draw(IRenderer* ren, const AtlasSet& characterAtlases, const AtlasSet& heldAtlases, const glm::mat3& place,
                        const float tint[4]) const {
    if (!ren) return;
    glm::mat3 hand = place;
    if (body_.clip()) {
        body_.draw(ren, characterAtlases, place, tint);   // also brings its poses to this frame
        if (!attachPlacement(body_.poses().poses(), "hand", place, hand)) hand = place;
    }
    if (held_.clip()) held_.draw(ren, heldAtlases, hand, tint);
}

}  // namespace toms::anim
