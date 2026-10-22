// anim_player.h -- plays an anim_clip.h clip and draws it from the packed sprite atlas(es).
//
//   toms::anim::AnimPlayer p;
//   p.play(file.find("slime_attack"));
//   each frame:  p.update(dtMs);
//                for (const std::string& e : p.takeEvents()) ...     // "hit", "sfx_swing", ...
//                p.draw(ren, atlases, toms::anim::placement(x, y, scale));
//
// A sprite is drawn around its pivot (the node's, else the atlas's x_pivot, else the centre) with
// the atlas's trim offsets, so it sits exactly where the untrimmed image would. The sprites come
// from one or more atlases (AtlasSet, the .anim's "atlases" in order); sprites from the same atlas
// page share a texture, so a whole clip -- rotated, scaled, any depth of nodes -- still draws in a
// few batches (a new draw call only where the texture or additive blending changes).
#pragma once
#include "anim_clip.h"
#include "atlas_file.h"
#include "render_iface.h"
#include "sprite_mesh.h"

#include <string>
#include <vector>

namespace toms::anim {

// Clip space -> screen: the clip's origin at (x, y), uniformly scaled (and optionally rotated).
glm::mat3 placement(float x, float y, float scale = 1.0f, float rotationDegrees = 0.0f);

// The atlases a clip draws from (the .anim's "atlases"), each with its id. A sprite reference
// "id:name" (anim_clip.h SpriteRef) is looked up in the atlas with that id; a bare "name" in each
// atlas in turn, the first that has it winning. Each atlas page has the Quad::texture it is drawn
// with -- kSpriteAtlasTexture for the game's own sprite atlas, else an IRenderer::loadTexture id.
struct AtlasSet {
    struct Entry {
        const toms::AtlasFile* atlas = nullptr;
        std::string id;
        std::vector<uint16_t> pageTextures;   // per page; empty = every page is the sprite atlas texture
        // Its sprites' meshes (<atlas>.mesh.json beside it, by meshKey; written by the anim tools); nullptr = none.
        const toms::SpriteMeshFile* meshes = nullptr;
    };
    std::vector<Entry> entries;

    AtlasSet() = default;
    AtlasSet(const toms::AtlasFile& single) { entries.push_back({&single, std::string(), {}}); }   // the common case
    void add(const toms::AtlasFile& atlas, std::string id = std::string(), std::vector<uint16_t> pageTextures = {}) {
        entries.push_back({&atlas, std::move(id), std::move(pageTextures)});
    }
    // The region a sprite reference names (nullptr: not found) and the texture to draw it with.
    // `entry` (optional) gets the index of the atlas it came from.
    const toms::AtlasRegion* find(const std::string& ref, uint16_t* texture = nullptr, int* entry = nullptr) const;
    // The mesh a node bends: its sprite reference `ref` (the one at that time) cut on the node's mesh grid, from the
    // mesh file of the atlas the sprite comes from (nullptr: none there, or no atlas has the sprite).
    const toms::SpriteMesh* findMesh(const std::string& ref, const Node& node) const;
    // The key that mesh has in its atlas's mesh file (meshKey), "" when no atlas has the sprite; `entry` (optional)
    // gets the atlas.
    std::string meshKeyFor(const std::string& ref, const Node& node, int* entry = nullptr) const;
};

// A mesh's key in its atlas's mesh file: the sprite name and the grid, "@CxR" for a node's meshGrid, nothing for 8-pixel
// cells: "golem", "golem@6x8". Every clip using that atlas shares the mesh.
std::string meshKey(const std::string& sprite, int cols = 0, int rows = 0);

// A mesh a clip file needs: a skinned or deformed node's sprite (its rest sprite and its sprite keys) on its grid.
struct MeshNeed {
    std::string key;                          // meshKey, in the mesh file of atlas `entry`
    int entry = -1;                           // the AtlasSet entry the sprite comes from
    const toms::AtlasRegion* region = nullptr;
    int cols = 0, rows = 0;                   // the node's meshGrid; 0 = 8-pixel cells
};
// Every mesh the file's clips need, each once (by atlas, then key). Sprites no atlas has are left out.
std::vector<MeshNeed> meshesNeeded(const AnimFile& file, const AtlasSet& atlases);
std::vector<MeshNeed> meshesNeeded(const Clip& clip, const AtlasSet& atlases);
// The mesh a need asks for, cut from its sprite's alpha: `rgba` points at the region's packed pixels (its top-left),
// rows `stride` pixels apart. On the node's grid, or on 8-pixel cells. A rotated or child region, or an empty sprite,
// gives an empty mesh (the node draws as a quad, or a deform on a plain grid).
toms::SpriteMesh cutMesh(const MeshNeed& need, const uint8_t* rgba, int stride);

// What a clip draws, in draw order: quads, and the meshes of skinned nodes (docs/34_MESH_ANIMATION.md).
struct DrawList {
    std::vector<Quad> quads;
    struct MeshItem {
        uint32_t firstVertex = 0, vertexCount = 0, firstIndex = 0, indexCount = 0;
        float tint[4] = {1, 1, 1, 1};
        bool additive = false;
        uint16_t texture = kSpriteAtlasTexture;
    };
    std::vector<MeshItem> meshes;
    std::vector<MeshVertex> vertices;       // every mesh's, in turn
    std::vector<uint16_t> indices;          // local to each mesh's first vertex
    struct Item { bool mesh; uint32_t index; };
    std::vector<Item> order;

    void clear() { quads.clear(); meshes.clear(); vertices.clear(); indices.clear(); order.clear(); }
    bool empty() const { return order.empty(); }
    void addQuad(const Quad& q) { order.push_back({false, (uint32_t)quads.size()}); quads.push_back(q); }
    void submit(IRenderer* ren) const;      // drawSprite / drawMesh each, in order
};

// The skin weights a skinned pose's mesh uses (kSkinInfluences (bone, weight) pairs per vertex): the node's own
// when they fit the mesh, else automatic -- each vertex weighted by its distance to each bone in the rest pose (a
// bone is the segment from its origin to the first of its children that is also a bone, else its origin).
// restVerts: the mesh's vertices in clip space at rest (x, y pairs); boneRest: each bone's rest world transform;
// boneEnd: each bone's segment end (= its origin when it has no bone child).
void autoSkinWeights(const std::vector<float>& restVerts, const std::vector<glm::mat4>& boneRest,
                     const std::vector<glm::vec2>& boneEnd, std::vector<float>& out);
// Every node's rest world transform (rest pos / rot / scale, keys ignored), by node.
void restWorlds(const Clip& clip, std::vector<std::pair<const Node*, glm::mat4>>& out);


// The quads for one evaluated pose list (anim_clip.h evaluate), in draw order. tint multiplies
// every colour (nullptr = white). Sprites no atlas has are skipped and, with `missing`, reported
// once each.
void appendQuads(const std::vector<NodePose>& poses, const AtlasSet& atlases, const glm::mat3& placement,
                 const float tint[4], std::vector<Quad>& out, std::vector<std::string>* missing = nullptr);
// The quad of one pose; false = nothing to draw (hidden, transparent, no sprite or not found).
bool makeQuad(const NodePose& pose, const AtlasSet& atlases, const glm::mat3& placement, const glm::vec4& tint,
              Quad& out, const toms::AtlasRegion** region = nullptr);
// As appendQuads, but a skinned node whose sprite has a mesh is a bent mesh (the others are quads, as there). The
// rest pose comes from `clip` (the clip the poses were evaluated from).
void appendDraws(const Clip& clip, const std::vector<NodePose>& poses, const AtlasSet& atlases, const glm::mat3& placement,
                 const float tint[4], DrawList& out, std::vector<std::string>* missing = nullptr);
// A skinned node's sprite that has no mesh in its atlas's mesh file (docs/34_MESH_ANIMATION.md): it is drawn as a
// plain quad, not bent by its bones. Sprites no atlas has are left out (that is another problem).
struct MissingMesh {
    std::string node;     // "root/child/node"
    std::string sprite;   // the reference, as the clip writes it
};
// Every skinned node's sprites (its rest sprite and its sprite keys) without a mesh, each once per node.
std::vector<MissingMesh> skinnedWithoutMesh(const Clip& clip, const AtlasSet& atlases);

// A skinned pose as the editor's skin view shows it: the bent mesh with its weights, and the bones -- each the segment
// the automatic weights measure against, where its bone has carried it now. Everything placed (like the draw).
struct SkinView {
    std::vector<glm::vec2> vertices;              // bent, placed
    std::vector<uint16_t> triangles;
    std::vector<float> weights;                   // kSkinInfluences (bone, weight) pairs per vertex
    bool autoWeights = true;                      // false: the node's painted weights
    std::vector<std::string> bones;               // the node's skinBones
    std::vector<bool> boneFound;                  // a node of the clip has that name
    std::vector<glm::vec2> boneStart, boneEnd;    // per bone, placed
};
enum class SkinState { NotSkinned, NoSprite, Rotated, NoMesh, Ok };
// The skin of pose i of `poses` (evaluated from `clip`): Ok and *out filled, or why there is none to show (the node
// then draws as its quad). Hidden poses are shown too.
SkinState skinView(const Clip& clip, const std::vector<NodePose>& poses, size_t i, const AtlasSet& atlases,
                   const glm::mat3& placement, SkinView* out);
// The bent mesh of pose i (skinned, with a mesh); false = draw its quad instead.
bool makeSkinnedMesh(const std::vector<NodePose>& poses, size_t i, const std::vector<std::pair<const Node*, glm::mat4>>& rest,
                     const AtlasSet& atlases, const glm::mat3& placement, const glm::vec4& tint, DrawList& out);

class AnimPlayer {
public:
    void play(const Clip* clip);       // from the start; nullptr = stop
    void stop() { play(nullptr); }
    void update(int dtMs);             // advances; events passed go to takeEvents()
    std::vector<std::string> takeEvents();

    const Clip* clip() const { return clip_; }
    float time() const;                // clip time now (0..duration)
    // The time the poses are drawn at: time(), except after the last play of a clip with looping
    // nodes, where it keeps running past the duration so those nodes keep playing (the others hold
    // their last keys there).
    float poseTime() const;
    bool finished() const;             // played playCount times (never, for a looping clip)
    bool showing() const;              // something to draw: running, or finished with stayAtLastFrame

    // Draws the clip at time(). The poses are a PoseCache (only the key tracks that have keys move)
    // and each quad is kept until its pose, the placement, the tint or the AtlasSet changes, so a
    // still part of a clip costs nothing but its drawSprite call. A skinned node's mesh is bent
    // again every frame (its bones may move).
    void draw(IRenderer* ren, const AtlasSet& atlases, const glm::mat3& placement,
              const float tint[4] = nullptr) const;
    // The atlases' contents changed under the same AtlasSet (a reload): rebuild every quad.
    void refreshQuads() { quadAtlases_ = nullptr; }
    const PoseCache& poses() const { return cache_; }

private:
    const Clip* clip_ = nullptr;
    double elapsed_ = 0;               // seconds since play()
    bool started_ = false;             // events at time 0 have been sent
    bool loops_ = false;               // the clip has looping nodes (poseTime() runs on after the end)
    std::vector<std::string> events_;
    mutable PoseCache cache_;
    mutable std::vector<Quad> quads_;                // per pose
    mutable std::vector<unsigned char> hasQuad_;     // per pose: quads_[i] is drawn
    mutable const AtlasSet* quadAtlases_ = nullptr;  // what quads_ were made with
    mutable glm::mat3 quadPlace_{1.0f};
    mutable glm::vec4 quadTint_{1.0f};
    mutable std::vector<std::pair<const Node*, glm::mat4>> rest_;   // the clip's rest pose (skins)
    mutable DrawList meshScratch_;

    double total() const;              // playCount * duration; < 0 = forever
};

// The deform lattice's offset at a point of the sprite's original image (bilinear in the cell it lies in): offsets
// from deformAt (cols x rows, row major), the point and the original size in pixels.
glm::vec2 latticeOffset(const std::vector<glm::vec2>& offsets, int cols, int rows, float x, float y, float origW, float origH);
// A sprite node's lattice as the editor shows and drags it: every point placed (with the deform at the node's time),
// where it is at rest in the sprite's pixels, its offset now, and the map from sprite pixels to placed coordinates
// (the node's own transform; a skinned node's bones are not applied). False: no sprite.
struct LatticeView {
    int cols = 0, rows = 0;
    std::vector<glm::vec2> points, rest, offsets;   // row major
    glm::mat3 spriteToPlaced{1.0f};
};
bool latticeView(const std::vector<NodePose>& poses, size_t i, const AtlasSet& atlases, const glm::mat3& placement, LatticeView& out);

// Where a thing held by a node is drawn: `place` times that node's world transform in `poses` (its position, turn and
// scale at that frame). False: no node of that name.
bool attachPlacement(const std::vector<NodePose>& poses, const std::string& node, const glm::mat3& place, glm::mat3& out);

// A character clip and a held thing's clip played together (docs/15_ANIMATION.md "Characters"): the character clip
// (archer_bow.anim: body, hand) and the held clip (weapon_bow.anim) are separate files, bound here. Both start
// together; every frame the held clip is drawn at the character's `hand` node, in front of the character, so it moves
// with the body's lean, recoil and shake. Events come from both. Either clip may be missing (only the other plays).
class CharacterRig {
public:
    void play(const Clip* character, const Clip* held);   // both from the start; nullptrs = stop
    void stop() { play(nullptr, nullptr); }
    void update(int dtMs);
    std::vector<std::string> takeEvents();
    const Clip* character() const { return body_.clip(); }
    const Clip* held() const { return held_.clip(); }
    bool finished() const;   // both played out (a looping clip never is)
    float time() const { return body_.clip() ? body_.time() : held_.time(); }
    // The character drawn at `place` from characterAtlases, then the held clip at its hand from heldAtlases.
    void draw(IRenderer* ren, const AtlasSet& characterAtlases, const AtlasSet& heldAtlases, const glm::mat3& place,
              const float tint[4] = nullptr) const;
    const AnimPlayer& characterPlayer() const { return body_; }

private:
    AnimPlayer body_, held_;
};

}  // namespace toms::anim
