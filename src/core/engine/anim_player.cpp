// anim_player.cpp -- see anim_player.h.
#include "anim_player.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

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
    for (const Item& it : order) {
        if (!it.mesh) { ren->drawSprite(quads[it.index]); continue; }
        const MeshItem& m = meshes[it.index];
        Mesh mesh;
        mesh.vertices = vertices.data() + m.firstVertex;
        mesh.vertexCount = m.vertexCount;
        mesh.indices = indices.data() + m.firstIndex;
        mesh.indexCount = m.indexCount;
        std::copy(m.tint, m.tint + 4, mesh.tint);
        mesh.additive = m.additive;
        mesh.texture = m.texture;
        ren->drawMesh(mesh);
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
        for (const NodePose& q : poses) if (q.node == bn) { now = q.world; break; }
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
    v.autoWeights = n.skinWeights.size() != (size_t)verts * 2 * kSkinInfluences;   // automatic, or painted for another mesh
    if (v.autoWeights) autoSkinWeights(restV, boneRest, boneEnd, v.weights);
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
    return SkinState::Ok;
}
}  // namespace

bool makeSkinnedMesh(const std::vector<NodePose>& poses, size_t i, const std::vector<std::pair<const Node*, glm::mat4>>& rest,
                     const AtlasSet& atlases, const glm::mat3& place, const glm::vec4& t, DrawList& out) {
    const NodePose& p = poses[i];
    if (!p.visible || p.color.a <= 0.0f) return false;
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
    for (uint32_t v = 0; v < item.vertexCount; v++) {
        const float px = std::clamp(mesh->vertices[2 * v] - r->offX, 0.0f, (float)r->w);
        const float py = std::clamp(mesh->vertices[2 * v + 1] - r->offY, 0.0f, (float)r->h);
        out.vertices.push_back({b.view.vertices[v].x, b.view.vertices[v].y, r->uv[0] + px * du, r->uv[1] + py * dv});
    }
    out.indices.insert(out.indices.end(), mesh->triangles.begin(), mesh->triangles.end());
    const glm::vec4 col = p.color * t;
    item.tint[0] = col.r; item.tint[1] = col.g; item.tint[2] = col.b; item.tint[3] = col.a;
    item.additive = p.node->blend == Blend::Add;
    item.texture = b.texture;
    out.order.push_back({true, (uint32_t)out.meshes.size()});
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

void appendDraws(const Clip& clip, const std::vector<NodePose>& poses, const AtlasSet& atlases, const glm::mat3& place,
                 const float tint[4], DrawList& out, std::vector<std::string>* missing) {
    const glm::vec4 t = tint ? glm::vec4(tint[0], tint[1], tint[2], tint[3]) : glm::vec4(1.0f);
    std::vector<std::pair<const Node*, glm::mat4>> rest;
    bool restMade = false;
    Quad q;
    for (size_t i = 0; i < poses.size(); i++) {
        const NodePose& p = poses[i];
        if (p.node && p.node->usesMesh()) {
            if (!restMade) { restWorlds(clip, rest); restMade = true; }
            if (makeSkinnedMesh(poses, i, rest, atlases, place, t, out)) continue;
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
    for (size_t i = 0; i < poses.size(); i++) {
        if (poses[i].node && poses[i].node->usesMesh()) {   // bent again every frame: its bones / lattice may move
            meshScratch_.clear();
            if (makeSkinnedMesh(poses, i, rest_, atlases, place, t, meshScratch_)) {
                meshScratch_.submit(ren);
                continue;
            }
        }
        if (all || cache_.changed(i)) hasQuad_[i] = makeQuad(poses[i], atlases, place, t, quads_[i]);
        if (hasQuad_[i]) ren->drawSprite(quads_[i]);
    }
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
