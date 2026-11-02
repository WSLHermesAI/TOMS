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

#include <memory>
#include <string>
#include <vector>

namespace toms::anim {

// What a playing clip's skinned nodes need every frame but which does not change while it plays: per node and sprite,
// the region, texture, mesh, the mesh at rest with its UVs and weights, each bone's pose index and inverse rest transform.
// A frame then only multiplies bones and sums weights. One per clip and atlas set, shared by every AnimPlayer playing that
// clip (a crowd of 64 holds one copy); it goes when the last of them stops. Defined in anim_player.cpp.
struct SkinRigCache;

// Clip space -> screen: the clip's origin at (x, y), uniformly scaled (and optionally rotated).
glm::mat3 f_Placement(float l_fX, float l_fY, float l_fScale = 1.0f, float l_fRotationDegrees = 0.0f);

// The atlases a clip draws from (the .anim's "atlases"), each with its id. A sprite reference
// "id:name" (anim_clip.h SpriteRef) is looked up in the atlas with that id; a bare "name" in each
// atlas in turn, the first that has it winning. Each atlas page has the Quad::texture it is drawn
// with -- kSpriteAtlasTexture for the game's own sprite atlas, else an IRenderer::loadTexture id.
struct AtlasSet {
    struct Entry {
        const toms::AtlasFile* m_pAtlas = nullptr;
        std::string m_strId;
        std::vector<uint16_t> m_vPageTextures;   // per page; empty = every page is the sprite atlas texture
        // Its sprites' meshes (<atlas>.mesh.json beside it, by meshKey; written by the anim tools); nullptr = none.
        const toms::SpriteMeshFile* m_pMeshes = nullptr;
    };
    std::vector<Entry> m_vEntries;

    AtlasSet() = default;
    AtlasSet(const toms::AtlasFile& l_oSingle) { m_vEntries.push_back({&l_oSingle, std::string(), {}}); }   // the common case
    void f_Add(const toms::AtlasFile& l_oAtlas, std::string l_strId = std::string(), std::vector<uint16_t> l_vPageTextures = {}) {
        m_vEntries.push_back({&l_oAtlas, std::move(l_strId), std::move(l_vPageTextures)});
    }
    // The region a sprite reference names (nullptr: not found) and the texture to draw it with.
    // `entry` (optional) gets the index of the atlas it came from.
    const toms::AtlasRegion* f_Find(const std::string& l_strRef, uint16_t* l_pTexture = nullptr, int* l_pEntry = nullptr) const;
    // The mesh a node bends: its sprite reference `ref` (the one at that time) cut on the node's mesh grid, from the
    // mesh file of the atlas the sprite comes from (nullptr: none there, or no atlas has the sprite).
    const toms::SpriteMesh* f_FindMesh(const std::string& l_strRef, const Node& l_oNode) const;
    // The key that mesh has in its atlas's mesh file (meshKey), "" when no atlas has the sprite; `entry` (optional)
    // gets the atlas.
    std::string f_MeshKeyFor(const std::string& l_strRef, const Node& l_oNode, int* l_pEntry = nullptr) const;
};

// A mesh's key in its atlas's mesh file: the sprite name and the grid, "@CxR" for a node's meshGrid, nothing for 8-pixel
// cells: "golem", "golem@6x8". Every clip using that atlas shares the mesh.
std::string f_MeshKey(const std::string& l_strSprite, int l_iCols = 0, int l_iRows = 0);

// A mesh a clip file needs: a skinned or deformed node's sprite (its rest sprite and its sprite keys) on its grid.
struct MeshNeed {
    std::string m_strKey;                          // meshKey, in the mesh file of atlas `entry`
    int m_iEntry = -1;                           // the AtlasSet entry the sprite comes from
    const toms::AtlasRegion* m_pRegion = nullptr;
    int m_iCols = 0, m_iRows = 0;                   // the node's meshGrid; 0 = 8-pixel cells
};
// Every mesh the file's clips need, each once (by atlas, then key). Sprites no atlas has are left out.
std::vector<MeshNeed> f_MeshesNeeded(const AnimFile& l_oFile, const AtlasSet& l_oAtlases);
std::vector<MeshNeed> f_MeshesNeeded(const Clip& l_oClip, const AtlasSet& l_oAtlases);
// The mesh a need asks for, cut from its sprite's alpha: `rgba` points at the region's packed pixels (its top-left),
// rows `stride` pixels apart. On the node's grid, or on 8-pixel cells. A rotated or child region, or an empty sprite,
// gives an empty mesh (the node draws as a quad, or a deform on a plain grid).
toms::SpriteMesh f_CutMesh(const MeshNeed& l_oNeed, const uint8_t* l_pRgba, int l_iStride);

// What a clip draws, in draw order: quads, the meshes of skinned nodes (docs/engine/16_MESH_ANIMATION.md), and where a
// node's effect layer begins and ends (docs/engine/18_NODE_EFFECTS.md: IRenderer::beginLayer / endLayer).
struct DrawList {
    std::vector<Quad> m_vQuads;
    struct MeshItem {
        uint32_t m_uFirstVertex = 0, m_uVertexCount = 0, m_uFirstIndex = 0, m_uIndexCount = 0;
        float m_arrTint[4] = {1, 1, 1, 1};
        bool m_bAdditive = false;
        uint16_t m_u16Texture = g_u16SpriteAtlasTexture;
    };
    std::vector<MeshItem> m_vMeshes;
    std::vector<MeshVertex> m_vVertices;       // every mesh's, in turn
    std::vector<uint16_t> m_vIndices;          // local to each mesh's first vertex
    std::vector<LayerEffect> m_vLayers;        // BeginLayer items' effects
    enum Kind : uint8_t { QuadItem, MeshItemKind, BeginLayer, EndLayer };
    struct Item { Kind m_eKind; uint32_t m_uIndex; };   // index into quads, meshes or layers
    std::vector<Item> m_vOrder;

    void f_Clear() { m_vQuads.clear(); m_vMeshes.clear(); m_vVertices.clear(); m_vIndices.clear(); m_vLayers.clear(); m_vOrder.clear(); }
    bool f_Empty() const { return m_vOrder.empty(); }
    void f_AddQuad(const Quad& l_oQ) { m_vOrder.push_back({QuadItem, (uint32_t)m_vQuads.size()}); m_vQuads.push_back(l_oQ); }
    void f_BeginLayer(const LayerEffect& l_oFx) { m_vOrder.push_back({BeginLayer, (uint32_t)m_vLayers.size()}); m_vLayers.push_back(l_oFx); }
    void f_EndLayer() { m_vOrder.push_back({EndLayer, 0}); }
    // To the renderer, in order. A run of quads and meshes with one texture and blending that has a mesh in it goes as
    // ONE mesh (each quad two triangles with its own colour): a skinned character is a draw or two, not one per part. A
    // run of quads only stays quads (the sprite batch already joins those, also across clips).
    void f_Submit(IRenderer* l_pRen) const;
};

// The skin weights a skinned pose's mesh uses (kSkinInfluences (bone, weight) pairs per vertex): the node's own
// when they fit the mesh, else automatic -- each vertex weighted by its distance to each bone in the rest pose (a
// bone is the segment from its origin to the first of its children that is also a bone, else its origin).
// restVerts: the mesh's vertices in clip space at rest (x, y pairs); boneRest: each bone's rest world transform;
// boneEnd: each bone's segment end (= its origin when it has no bone child).
void f_AutoSkinWeights(const std::vector<float>& l_vRestVerts, const std::vector<glm::mat4>& l_vBoneRest,
                     const std::vector<glm::vec2>& l_vBoneEnd, std::vector<float>& l_vOut);
// Every node's rest world transform (rest pos / rot / scale, keys ignored), by node.
void f_RestWorlds(const Clip& l_oClip, std::vector<std::pair<const Node*, glm::mat4>>& l_vOut);


// The layer effect of pose i's node at its time (its fx, its fx keys), lengths placed (times the placement's scale, the
// blur vector through it). `clip` (the poses' clip) gives a motion blur's automatic part: the node's movement over the
// last 1/30 s. False: no effect, or nothing of it shows (hidden, transparent).
bool f_NodeLayerEffect(const Clip& l_oClip, const std::vector<NodePose>& l_vPoses, size_t l_nI, const glm::mat3& l_mat3Placement, LayerEffect& l_oOut);
// The afterimages of pose i's node (its trail) into `out`: its subtree evaluated gap, 2 gap, ... seconds earlier, oldest
// first, tinted. `rest`: the clip's rest worlds (skinned nodes).
void f_AppendTrail(const Clip& l_oClip, const std::vector<NodePose>& l_vPoses, size_t l_nI, const AtlasSet& l_oAtlases,
                 const std::vector<std::pair<const Node*, glm::mat4>>& l_vRest, const glm::mat3& l_mat3Placement, const glm::vec4& l_v4Tint,
                 DrawList& l_oOut);

// The quads for one evaluated pose list (anim_clip.h evaluate), in draw order. tint multiplies
// every colour (nullptr = white). Sprites no atlas has are skipped and, with `missing`, reported
// once each.
void f_AppendQuads(const std::vector<NodePose>& l_vPoses, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement,
                 const float l_arrTint[4], std::vector<Quad>& l_vOut, std::vector<std::string>* l_pMissing = nullptr);
// The quad of one pose; false = nothing to draw (hidden, transparent, no sprite or not found).
bool f_MakeQuad(const NodePose& l_oPose, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement, const glm::vec4& l_v4Tint,
              Quad& l_oOut, const toms::AtlasRegion** l_pRegion = nullptr);
// As appendQuads, but a skinned node whose sprite has a mesh is a bent mesh (the others are quads, as there). The
// rest pose comes from `clip` (the clip the poses were evaluated from).
void f_AppendDraws(const Clip& l_oClip, const std::vector<NodePose>& l_vPoses, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement,
                 const float l_arrTint[4], DrawList& l_oOut, std::vector<std::string>* l_pMissing = nullptr);
// A skinned node's sprite that has no mesh in its atlas's mesh file (docs/engine/16_MESH_ANIMATION.md): it is drawn as a
// plain quad, not bent by its bones. Sprites no atlas has are left out (that is another problem).
struct MissingMesh {
    std::string m_strNode;     // "root/child/node"
    std::string m_strSprite;   // the reference, as the clip writes it
};
// Every skinned node's sprites (its rest sprite and its sprite keys) without a mesh, each once per node.
std::vector<MissingMesh> f_SkinnedWithoutMesh(const Clip& l_oClip, const AtlasSet& l_oAtlases);

// A skinned pose as the editor's skin view shows it: the bent mesh with its weights, and the bones -- each the segment
// the automatic weights measure against, where its bone has carried it now. Everything placed (like the draw).
struct SkinView {
    std::vector<glm::vec2> m_vVertices;              // bent, placed
    std::vector<uint16_t> m_vTriangles;
    std::vector<float> m_vWeights;                   // kSkinInfluences (bone, weight) pairs per vertex
    bool m_bAutoWeights = true;                      // false: the node's painted weights
    std::vector<std::string> m_vBones;               // the node's skinBones
    std::vector<bool> m_vBoneFound;                  // a node of the clip has that name
    std::vector<glm::vec2> m_vBoneStart, m_vBoneEnd;    // per bone, placed
};
enum class SkinState { NotSkinned, NoSprite, Rotated, NoMesh, Ok };
// The skin of pose i of `poses` (evaluated from `clip`): Ok and *out filled, or why there is none to show (the node
// then draws as its quad). Hidden poses are shown too.
SkinState f_SkinView(const Clip& l_oClip, const std::vector<NodePose>& l_vPoses, size_t l_nI, const AtlasSet& l_oAtlases,
                   const glm::mat3& l_mat3Placement, SkinView* l_pOut);
// The bent mesh of pose i (skinned, with a mesh); false = draw its quad instead.
// rigs (optional): a clip's cache -- poses from that clip, in its order (PoseCache / evaluate), the atlases the same.
bool f_MakeSkinnedMesh(const std::vector<NodePose>& l_vPoses, size_t l_nI, const std::vector<std::pair<const Node*, glm::mat4>>& l_vRest,
                     const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement, const glm::vec4& l_v4Tint, DrawList& l_oOut,
                     SkinRigCache* l_pRigs = nullptr);

class AnimPlayer {
public:
    void f_Play(const Clip* l_pClip);       // from the start; nullptr = stop
    void f_Stop() { f_Play(nullptr); }
    void f_Update(int l_iDtMs);             // advances; events passed go to takeEvents()
    // Jumps to this time since play() (seconds; past the duration = a later play), sending no events -- for a clip
    // driven by an outside clock (the floor's idle clock: render_object.h ClipObject::setTime).
    void f_Seek(double l_dSeconds);
    std::vector<std::string> f_TakeEvents();

    const Clip* f_Clip() const { return m_pClip; }
    float f_Time() const;                // clip time now (0..duration)
    // The time the poses are drawn at: time(), except after the last play of a clip with looping
    // nodes, where it keeps running past the duration so those nodes keep playing (the others hold
    // their last keys there).
    float f_PoseTime() const;
    bool f_Finished() const;             // played playCount times (never, for a looping clip)
    bool f_Showing() const;              // something to draw: running, or finished with stayAtLastFrame

    // Draws the clip at time(). The poses are a PoseCache (only the key tracks that have keys move)
    // and each quad is kept until its pose, the placement, the tint or the AtlasSet changes, so a
    // still part of a clip costs nothing but its drawSprite call. A skinned node's mesh is bent
    // again every frame (its bones may move).
    void f_Draw(IRenderer* l_pRen, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement,
              const float l_arrTint[4] = nullptr) const;
    // The atlases' contents changed under the same AtlasSet (a reload): rebuild every quad.
    void f_RefreshQuads() { m_pQuadAtlases = nullptr; m_pRigAtlases = nullptr; m_bRigsStale = true; }
    const PoseCache& f_Poses() const { return m_oCache; }

private:
    const Clip* m_pClip = nullptr;
    double m_dElapsed = 0;               // seconds since play()
    bool m_bStarted = false;             // events at time 0 have been sent
    bool m_bLoops = false;               // the clip has looping nodes (poseTime() runs on after the end)
    std::vector<std::string> m_vEvents;
    mutable PoseCache m_oCache;
    mutable std::vector<Quad> m_vQuads;                // per pose
    mutable std::vector<unsigned char> m_vHasQuad;     // per pose: quads_[i] is drawn
    mutable const AtlasSet* m_pQuadAtlases = nullptr;  // what quads_ were made with
    mutable glm::mat3 m_mat3QuadPlace{1.0f};
    mutable glm::vec4 m_v4QuadTint{1.0f};
    mutable std::vector<std::pair<const Node*, glm::mat4>> m_vRest;   // the clip's rest pose (skins)
    mutable DrawList m_oMeshScratch;     // this frame's draws (the cached quads and the bent meshes), submitted batched
    mutable std::shared_ptr<SkinRigCache> m_spRigs;   // the clip's skinned nodes at rest: shared per clip and atlas set
    mutable const AtlasSet* m_pRigAtlases = nullptr;
    mutable bool m_bRigsStale = false;   // refreshQuads: the atlases' contents changed -- a new shared cache, not the old one
    bool m_bEffects = false;             // the clip has effects or trails (hasEffects): layers and afterimages around subtrees

    double f_Total() const;              // playCount * duration; < 0 = forever
};

// The deform lattice's offset at a point of the sprite's original image (bilinear in the cell it lies in): offsets
// from deformAt (cols x rows, row major), the point and the original size in pixels.
glm::vec2 f_LatticeOffset(const std::vector<glm::vec2>& l_vOffsets, int l_iCols, int l_iRows, float l_fX, float l_fY, float l_fOrigW, float l_fOrigH);
// A sprite node's lattice as the editor shows and drags it: every point placed (with the deform at the node's time),
// where it is at rest in the sprite's pixels, its offset now, and the map from sprite pixels to placed coordinates
// (the node's own transform; a skinned node's bones are not applied). False: no sprite.
struct LatticeView {
    int m_iCols = 0, m_iRows = 0;
    std::vector<glm::vec2> m_vPoints, m_vRest, m_vOffsets;   // row major
    glm::mat3 m_mat3SpriteToPlaced{1.0f};
};
bool f_LatticeView(const std::vector<NodePose>& l_vPoses, size_t l_nI, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement, LatticeView& l_oOut);

// Where a thing held by a node is drawn: `place` times that node's world transform in `poses` (its position, turn and
// scale at that frame). False: no node of that name.
bool f_AttachPlacement(const std::vector<NodePose>& l_vPoses, const std::string& l_strNode, const glm::mat3& l_mat3Place, glm::mat3& l_mat3Out);

// A character clip and a held thing's clip played together (docs/engine/05_ANIMATION.md "Characters"): the character clip
// (archer_bow.anim: body, hand) and the held clip (weapon_bow.anim) are separate files, bound here. Both start
// together; every frame the held clip is drawn at the character's `hand` node, in front of the character, so it moves
// with the body's lean, recoil and shake. Events come from both. Either clip may be missing (only the other plays).
class CharacterRig {
public:
    void f_Play(const Clip* l_pCharacter, const Clip* l_pHeld);   // both from the start; nullptrs = stop
    void f_Stop() { f_Play(nullptr, nullptr); }
    void f_Update(int l_iDtMs);
    std::vector<std::string> f_TakeEvents();
    const Clip* f_Character() const { return m_oBody.f_Clip(); }
    const Clip* f_Held() const { return m_oHeld.f_Clip(); }
    bool f_Finished() const;   // both played out (a looping clip never is)
    float f_Time() const { return m_oBody.f_Clip() ? m_oBody.f_Time() : m_oHeld.f_Time(); }
    // The character drawn at `place` from characterAtlases, then the held clip at its hand from heldAtlases.
    void f_Draw(IRenderer* l_pRen, const AtlasSet& l_oCharacterAtlases, const AtlasSet& l_oHeldAtlases, const glm::mat3& l_mat3Place,
              const float l_arrTint[4] = nullptr) const;
    const AnimPlayer& f_CharacterPlayer() const { return m_oBody; }

private:
    AnimPlayer m_oBody, m_oHeld;
};

}  // namespace toms::anim
