// anim_clip.h -- node animations on the packed sprite atlas: data, .anim files, evaluation.
//
// An .anim file (JSON) holds clips. A clip is a tree of nodes; each node has a rest transform
// (position, rotation, scale -- each x, y, z), a colour, optionally a sprite from the atlas, and one key track per
// channel. A node's world transform is its parent's world transform times its own, so moving a
// group moves everything in it; a node without a sprite is just a group.
//
//   channels   pos (x,y,z px)  rot (x,y,z degrees)  scale (x,y,z; negative = mirrored)
//              color (r,g,b,a 0..1)  -- interpolated, one easing per key for the segment it starts
//              sprite ("atlas:name", see SpriteRef)  visible  event  -- stepped (holds until the next key)
//
// Each node can play its keys its own way (Node::loop, Node::stayAtLastFrame). A node's key range is
// first..last key over its subtree (its own tracks and every descendant's):
//   default            plays once; after its last key it holds that pose (stays on screen, nothing updates)
//   stayAtLastFrame=0  plays once; after its last key it and its subtree are hidden
//   loop               after its last key it plays first..last again, forever -- also after the clip's
//                      own timeline has ended (AnimPlayer keeps time running for it: a popup whose
//                      parts stop and stay while a sparkle keeps flying around)
// The time a node gets passes down: a looping group loops its whole subtree, and a looping child
// inside it loops in the group's time. Event keys follow the clip's timeline only (they do not repeat
// with a looping node).
//
// Coordinates are screen pixels, y down, z into the screen. Rotation is Euler angles in degrees,
// NOT wrapped (0 -> 720 spins twice): z turns in the screen plane (clockwise on screen), x and y
// tilt the node out of it; applied z first, then y, then x (R = Rx * Ry * Rz, the FM79979 engine's
// cMatrix44::RotationMatrix, so MPDI angles carry over as they are). The
// world transform is 3D (NodePose::world) so a 3D camera can show depth; today the sprites are drawn
// with the orthographic screen camera (flatten()): z does not move them, x / y tilts foreshorten
// them. A channel without keys keeps the node's rest value; before its first key it holds the first
// key's value, after its last key the last.
//
// Files: version 2 stores pos / rot / scale as [x, y, z]. Version 1 files ([x, y] and a single rot
// number, the z rotation) still load; they are saved as version 2. Version 3 adds a node's "skin"
// (docs/engine/16_MESH_ANIMATION.md); a file is saved as version 3 only when a node has one, so files without
// skins still open in older builds.
//
// Deform (a lattice; version 3 too): a node's `lattice` is a grid of points over its sprite's original image, cols x
// rows (default 4 x 4, 2 .. 16 each), and its `deform` keys move some of those points, in the sprite's pixels. Every
// mesh vertex follows the grid cell it lies in (bilinear), so the sprite squashes, stretches, breathes, sways. The
// lattice belongs to the image, not to the mesh: a sprite re-meshed after a reskin bends the same way. A sprite without
// a mesh file is bent on a grid of its own. Keys are eased like the other interpolated channels; a point a key does
// not list is at 0 there. The deform comes before skinning (it changes the shape the bones then carry).
//
// Meshes: the triangles a skinned or deformed node bends are in its atlas's mesh file (`game.atlas` ->
// `game.mesh.json` beside it, sprite_mesh.h), keyed by sprite and grid (meshKey), so every clip using that atlas shares
// them. Each node says the grid its sprite is cut on (`meshGrid`, cells across and down; none = 8-pixel cells). The anim
// tools add the meshes their clips need when a clip is saved (anim editor, `anim_editor --headless meshes`); the atlas
// tool never touches them.
//
// Skinning: a node whose sprite has a mesh and a skin bends that mesh
// with other nodes of the clip, its bones. Each mesh vertex follows up to kSkinInfluences bones, weighted;
// the weights are the file's, or made automatically from how near each vertex is to each bone in the rest
// pose (the nodes' rest values, keys ignored: Spine's setup pose). Without a mesh it draws as a quad.
//
// Draw order: depth first. A node draws its children with order < 0 (sorted by order, then by
// position in the list), then its own sprite, then the children with order >= 0. Colour multiplies
// down the tree unless a node sets inheritColor false; an invisible node hides its subtree.
//
// Effects (version 4, docs/engine/18_NODE_EFFECTS.md): a node's `fx` draws it and its whole subtree into a picture of
// their own (IRenderer::beginLayer) and that picture through a shader -- motion blur, glow (HDR + bloom), flash,
// outline -- so nodes outside the subtree are not touched; effects nest. Its `fx` keys animate the shader's params. A
// node's `trail` draws afterimages of its subtree: the same nodes as they were a little earlier, fading.
//
// The game plays clips with AnimPlayer (anim_player.h); the editor evaluates them with the same
// code, so its preview is what the game draws.
#pragma once
#include <glm/glm.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace toms { struct SpriteBox; }

namespace toms::anim {

// Easing of the segment that starts at a key: Tweeny's (src/third_party/tweeny) Penner easings by
// name ("linear", "quadraticIn", ..., "backInOut", "stepped"), or a cubic Bezier
// [x1, y1, x2, y2] like CSS cubic-bezier().
struct Ease {
    int m_iKind = 1;                      // tweeny::easing::enumerated as int; kBezier = Bezier
    float m_arrBezier[4] = {0, 0, 1, 1};
    static constexpr int m_iBezier = 1000;

    bool operator==(const Ease& l_oO) const;
    bool operator!=(const Ease& l_oO) const { return !(*this == l_oO); }

    bool f_IsLinear() const { return m_iKind == 1; }
    bool f_IsStepped() const { return m_iKind == 2; }
    float f_Apply(float l_fProgress) const;  // 0..1 -> eased 0..1 (may overshoot for back/elastic)
};
const std::vector<std::string>& f_EaseNames();   // index = Ease::kind
bool f_ParseEase(const std::string& l_strName, Ease& l_oOut);
std::string f_EaseName(const Ease& l_oE);           // "" for a Bezier

template <class T> struct Key {
    float m_fT = 0;
    T m_oV{};
    Ease m_oEase;                         // interpolated channels only

    bool operator==(const Key& l_oO) const { return m_fT == l_oO.m_fT && m_oV == l_oO.m_oV && m_oEase == l_oO.m_oEase; }
    bool operator!=(const Key& l_oO) const { return !(*this == l_oO); }
};

enum class Blend { Normal, Add };

// A node's effect (see the top). params by shader, lengths in clip pixels:
//   group       -                                   the subtree drawn as one picture (one alpha over what is under it)
//   motionBlur  dx, dy, auto, -                     smeared along (dx, dy); auto > 0 adds the node's own movement over
//                                                   the last 1/30 s, times auto
//   glow        intensity, radius, threshold, bloom colours x intensity (> 1 = HDR); light above the threshold blooms
//                                                   `radius` past the outline, `bloom` strong, in `color`
//   flash       amount, -, -, -                     towards `color` (0..1), the alpha kept
//   outline     width, intensity, -, -              a band of `color` around the subtree's alpha
enum class FxShader { None, Group, MotionBlur, Glow, Flash, Outline };
const char* f_FxShaderName(FxShader l_eS);           // "" for None
bool f_ParseFxShader(const std::string& l_strName, FxShader& l_eOut);
struct NodeFx {
    FxShader m_eShader = FxShader::None;            // None: no picture of its own
    glm::vec4 m_v4Params{0, 0, 0, 0};                // at rest (fx keys animate them)
    glm::vec4 m_v4Color{1, 1, 1, 1};
    bool operator==(const NodeFx& l_oO) const { return m_eShader == l_oO.m_eShader && m_v4Params == l_oO.m_v4Params && m_v4Color == l_oO.m_v4Color; }
    bool operator!=(const NodeFx& l_oO) const { return !(*this == l_oO); }
};
// A node's afterimages (see the top): `count` copies of its subtree as it was gap, 2 gap, ... seconds earlier, tinted by
// `color` (the nearest with color.a, fading to the oldest), drawn behind it; trail keys (0..1) scale them, no keys = 1.
struct NodeTrail {
    int m_iCount = 0;                               // 0 = none
    float m_fGap = 0.04f;
    glm::vec4 m_v4Color{1, 1, 1, 0.5f};
    bool m_bAdditive = false;
    bool operator==(const NodeTrail& l_oO) const { return m_iCount == l_oO.m_iCount && m_fGap == l_oO.m_fGap && m_v4Color == l_oO.m_v4Color && m_bAdditive == l_oO.m_bAdditive; }
    bool operator!=(const NodeTrail& l_oO) const { return !(*this == l_oO); }
};
constexpr int g_iMaxTrail = 16;

// One deform key's value: lattice point index (row major: row * cols + col) -> its offset, sprite pixels.
using Deform = std::map<int, glm::vec2>;
constexpr int g_iDefaultLattice = 4;   // points across and down when a node does not set its lattice
constexpr int g_iMaxLattice = 16;
constexpr int g_iMaxMeshGrid = 32;    // a node's meshGrid, cells across or down
constexpr int g_iMeshCell = 8;        // without a meshGrid: square cells of this many pixels

struct Node {
    std::string m_strName;
    std::string m_strSprite;                // rest sprite ("atlas:name" or "name"); "" = a group node
    glm::vec3 m_v3Pos{0, 0, 0};
    glm::vec3 m_v3Rot{0, 0, 0};            // Euler degrees (see the top); z = the screen-plane turn
    glm::vec3 m_v3Scale{1, 1, 1};
    glm::vec4 m_v4Color{1, 1, 1, 1};
    bool m_bVisible = true;
    bool m_bHasPivot = false;             // false: the sprite's pivot from the atlas (x_pivot, default centre)
    glm::vec2 m_v2Pivot{0.5f, 0.5f};       // 0..1 of the sprite's original size, from the top-left
    int m_iOrder = 0;                     // among siblings; < 0 draws before the parent's own sprite
    Blend m_eBlend = Blend::Normal;
    bool m_bInheritColor = true;
    bool m_bLoop = false;                 // after its key range: play it again (see the top)
    bool m_bStayAtLastFrame = true;       // after its key range (not looping): false = hide the subtree
    // The skin (see the top): bone node names; empty = not skinned. skinWeights: kSkinInfluences
    // (bone index into skinBones, weight) pairs per mesh vertex; empty = automatic.
    std::vector<std::string> m_vSkinBones;
    std::vector<float> m_vSkinWeights;
    bool f_Skinned() const { return !m_vSkinBones.empty(); }
    // The deform lattice (see the top): points across and down, 0 = the default; and its keys.
    int m_arrLattice[2] = {0, 0};
    std::vector<Key<Deform>> m_vDeformKeys;
    bool f_Deformed() const { return !m_vDeformKeys.empty(); }
    int f_LatticeCols() const { return m_arrLattice[0] >= 2 ? m_arrLattice[0] : g_iDefaultLattice; }
    int f_LatticeRows() const { return m_arrLattice[1] >= 2 ? m_arrLattice[1] : g_iDefaultLattice; }
    // The grid its sprite's mesh is cut on, cells across and down (1 .. kMaxMeshGrid); 0 = 8-pixel cells.
    int m_arrMeshGrid[2] = {0, 0};
    bool f_HasMeshGrid() const { return m_arrMeshGrid[0] > 0 && m_arrMeshGrid[1] > 0; }
    // Drawn as a bent mesh (a skin or a deform), not a quad.
    bool f_UsesMesh() const { return f_Skinned() || f_Deformed(); }

    NodeFx m_oFx;                         // see the top: effects
    std::vector<Key<glm::vec4>> m_vFxKeys;
    NodeTrail m_oTrail;
    std::vector<Key<float>> m_vTrailKeys;
    bool f_HasFx() const { return m_oFx.m_eShader != FxShader::None; }
    bool f_HasTrail() const { return m_oTrail.m_iCount > 0; }

    std::vector<Key<glm::vec3>> m_vPosKeys, m_vRotKeys, m_vScaleKeys;
    std::vector<Key<glm::vec4>> m_vColorKeys;
    std::vector<Key<std::string>> m_vSpriteKeys, m_vEventKeys;
    std::vector<Key<bool>> m_vVisibleKeys;

    std::vector<Node> m_vChildren;

    float f_LastKeyTime() const;         // over this node's tracks and its subtree
    float f_FirstKeyTime() const;        // over this node's tracks and its subtree (0 without keys)
    bool f_HasKeys() const;              // any key in this node's tracks or its subtree
    bool f_Timed() const { return m_bLoop || !m_bStayAtLastFrame; }   // plays its range its own way

    // Every field, keys and children included (what the editor's undo compares; exact floats).
    bool operator==(const Node& l_oO) const;
    bool operator!=(const Node& l_oO) const { return !(*this == l_oO); }
};

struct Clip {
    std::string m_strName;
    float m_fLength = 0;                  // seconds; 0 = up to the last key
    int m_iPlayCount = 1;                 // times to play; -1 = loop forever
    bool m_bStayAtLastFrame = true;       // after the last play: keep showing the end (else hide)
    Node m_oRoot;

    float f_Duration() const;            // length, or the last key time when length is 0

    bool operator==(const Clip& l_oO) const {
        return m_strName == l_oO.m_strName && m_fLength == l_oO.m_fLength && m_iPlayCount == l_oO.m_iPlayCount && m_bStayAtLastFrame == l_oO.m_bStayAtLastFrame &&
               m_oRoot == l_oO.m_oRoot;
    }
    bool operator!=(const Clip& l_oO) const { return !(*this == l_oO); }
};

// One atlas a file draws from. `path` is relative to the .anim. `id` names it in sprite
// references; by default it is the file name without ".atlas", and a file sets another one when two
// atlases share a file name (an art style's game.atlas next to the original's).
struct AtlasRef {
    std::string m_strId;
    std::string m_strPath;
    bool operator==(const AtlasRef& l_oO) const { return m_strId == l_oO.m_strId && m_strPath == l_oO.m_strPath; }
};
std::string f_DefaultAtlasId(const std::string& l_strPath);   // "../fx/fx.atlas" -> "fx"

// A sprite reference -- a node's sprite or a sprite key -- is "atlas:sprite" (the sprite in the
// atlas with that id, like MPDI's PIName + ImageName), or a bare "sprite": the first atlas that
// has it.
struct SpriteRef {
    std::string m_strAtlas;   // "" = the first atlas that has the sprite
    std::string m_strName;
};
SpriteRef f_ParseSpriteRef(const std::string& l_strRef);
std::string f_SpriteRef(const std::string& l_strAtlasId, const std::string& l_strName);   // "id:name", or "name" when id is ""

constexpr int g_iAnimVersion = 4;       // the newest this build reads: 3 = skins, 4 = effects (each written only when used)
constexpr int g_iSkinInfluences = 4;    // bones per skinned vertex

struct AnimFile {
    int m_iVersion = g_iAnimVersion;        // as read; saving writes 4 with an effect, 3 with a skin, else 2
    // The .atlas files its sprites come from (anim_player.h AtlasSet), in lookup order. JSON:
    // "atlases": ["path", {"id": "...", "path": "..."}, ...] -- a plain path's id is its file name
    // without ".atlas"; an older file's single "atlas": "path" reads as a one-entry list.
    std::vector<AtlasRef> m_vAtlases;
    std::vector<Clip> m_vClips;

    const Clip* f_Find(const std::string& l_strName) const;

    bool operator==(const AnimFile& l_oO) const { return m_vAtlases == l_oO.m_vAtlases && m_vClips == l_oO.m_vClips; }   // not the version
    bool operator!=(const AnimFile& l_oO) const { return !(*this == l_oO); }
};

bool f_ParseAnim(const std::string& l_strJson, AnimFile& l_oOut, std::string* l_pErr = nullptr);
std::string f_AnimToJson(const AnimFile& l_oF);
// One node and its subtree in the file's node format (what the editor's Copy puts on the clipboard).
std::string f_NodeToJsonText(const Node& l_oN);
bool f_ParseNodeJson(const std::string& l_strJson, Node& l_oOut, std::string* l_pErr = nullptr);

// ---- characters ---------------------------------------------------------------------------------
//
// A character clip (docs/engine/05_ANIMATION.md, "Characters"; tools/make_archer_anim.py) is made with one character and
// played by any other. Its contract is two node names:
//   body   the character's sprite, pivot at the bottom centre (feet), unscaled
//   hand   where a held thing (a bow) sits, a sibling of the body's chain under a node at the feet
// fitCharacter gives every `body` node (rest sprite and sprite keys) the new sprite, and moves every `hand` node (rest
// position and position keys) so the hand stays at the same place on the new body: its height keeps its share of the
// visible body (head to feet) and its x its distance from the body's right edge -- both measured from the sprites'
// visible pixels (boxOf: a sprite reference -> its SpriteBox; false = unknown, the hand is not moved). `extra` is added
// on top (a per-character nudge, assets/data/character_fit.json). The clip itself is not changed.
glm::vec2 f_CharacterHandOffset(const glm::vec2& l_v2HandRest, const toms::SpriteBox& l_oFrom, const toms::SpriteBox& l_oTo);
Clip f_FitCharacter(const Clip& l_oC, const std::string& l_strSprite,
                  const std::function<bool(const std::string& l_strRef, toms::SpriteBox& l_oOut)>& l_fnBoxOf,
                  const glm::vec2& l_v2Extra = glm::vec2(0.0f));
// The first node with this name (depth first), or nullptr.
const Node* f_FindNode(const Node& l_oRoot, const std::string& l_strName);

// ---- evaluation --------------------------------------------------------------------------------

// One node at one time, in draw order.
struct NodePose {
    const Node* m_pNode = nullptr;
    int m_iDepth = 0;
    glm::mat4 m_mat4World{1.0f};             // node space -> clip space (pixels, y down, z into the screen)
    glm::vec4 m_v4Color{1, 1, 1, 1};       // with the parents' colour when inherited
    std::string m_strSprite;                // "" = nothing to draw
    bool m_bVisible = true;               // false: hidden (itself or a parent)
    float m_fTime = 0;                    // the time its own tracks see (a looping ancestor's wraps it): deformAt
    float m_fClipTime = 0;                // the clip's time these poses are for
    uint32_t m_uFirst = 0, m_uEnd = 0;       // its subtree's poses: [first, end), itself among them (effects draw them)
};

// The time a node's tracks (and its children) see when its parent's time is t, with its key range
// first..last: wraps into the range for a looping node. `ended` (optional) is set when a
// stayAtLastFrame=false node is past its range (hidden).
float f_NodeTime(const Node& l_oN, float l_fT, float l_fFirst, float l_fLast, bool* l_pEnded = nullptr);
// Any node in the clip loops (the clip keeps moving after its timeline ends).
bool f_HasLoopingNodes(const Clip& l_oC);

// The local transform of a node at time t: T(pos) * R(rot) * S(scale), R = Rx * Ry * Rz.
glm::mat4 f_LocalTransform(const Node& l_oN, float l_fT);
glm::mat4 f_Compose(const glm::vec3& l_v3Pos, const glm::vec3& l_v3RotDegrees, const glm::vec3& l_v3Scale);
// A 3D transform seen by the orthographic screen camera (looking down +z): the 2D affine map of the
// xy plane (z = 0) onto the screen. What the sprites are drawn with today.
glm::mat3 f_Flatten(const glm::mat4& l_mat4M);
// Every node of the clip at time t (clip time, 0..duration), in draw order (see the top). Hidden
// nodes are included with visible = false so an editor can still show them.
void f_Evaluate(const Clip& l_oC, float l_fT, std::vector<NodePose>& l_vOut);
// The poses of one clip kept from frame to frame (what AnimPlayer uses; evaluate() is the stateless
// reference with the same results). Each node holds its current position, rotation, scale, colour,
// sprite and visibility, starting at its rest values, and each of its key tracks a cursor. A seek
// only looks at the tracks that have keys, each on its own: a node with only sprite keys never
// touches its transform, a node without keys is never updated after the first seek, a track holding
// a value (before its first key, after its last, a stepped segment) costs a compare, and a world
// transform / colour is only rebuilt when its own values or a parent's changed.
class PoseCache {
public:
    void f_Bind(const Clip* l_pClip);       // the rest pose; nullptr = nothing. The clip must outlive the binding.
    const Clip* f_Clip() const { return m_pClip; }
    void f_Seek(float l_fT);                // clip time (0..duration); any direction, any step
    const std::vector<NodePose>& f_Poses() const { return m_vPoses; }   // draw order, as evaluate()
    // Pose i changed (world, colour, visibility or sprite) in the last seek.
    bool f_Changed(size_t l_nI) const { return l_nI < m_vChanged.size() && m_vChanged[l_nI]; }
    int f_ChangedCount() const { return m_iChangedCount; }
    int f_AnimatedNodes() const { return m_iAnimated; }   // nodes with at least one key track

private:
    enum { kPos, kRot, kScale, kColor, kSprite, kVisible, kTracks };
    struct Item {
        const Node* m_pNode = nullptr;
        int m_iParent = -1;               // index in items_ (parents come first)
        size_t m_nPose = 0;               // index in poses_
        bool m_bAnimated = false;
        int m_arrCursor[kTracks];           // last key at or before the time, -1 = before the first
        int m_arrHeld[kTracks];             // the key whose value the track holds now, -1 = between keys
        glm::vec3 m_v3Pos{0}, m_v3Rot{0}, m_v3Scale{1};
        glm::vec4 m_v4Color{1};            // own, unclamped (before the parents' colour)
        bool m_bVisible = true;           // own
        bool m_bTimed = false;            // loop or !stayAtLastFrame: its time is remapped
        float m_fFirst = 0, m_fLast = 0;     // its subtree's key range (timed nodes)
        float m_fTime = 0;                // the time its tracks saw in the last seek
        bool m_bEnded = false;            // stayAtLastFrame=false and past its range: hidden
        glm::mat4 m_mat4Local{1.0f};
        bool m_bWorldChanged = false, m_bColorChanged = false, m_bVisibleChanged = false;   // in this seek
    };
    int f_AddItems(const Node& l_oN, int l_iParent, int l_iDepth);

    const Clip* m_pClip = nullptr;
    std::vector<Item> m_vItems;
    std::vector<NodePose> m_vPoses;
    std::vector<unsigned char> m_vChanged;
    int m_iChangedCount = 0;
    int m_iAnimated = 0;
    bool m_bFresh = true;                // the first seek computes everything
};

// Event names whose key time lies in (t0, t1], in time order. For a clip that wrapped around,
// call it for each part.
void f_EventsBetween(const Clip& l_oC, float l_fT0, float l_fT1, std::vector<std::string>& l_vOut);

// Channel values at time t (rest value when the track is empty).
glm::vec3 f_PositionAt(const Node& l_oN, float l_fT);
glm::vec3 f_RotationAt(const Node& l_oN, float l_fT);
glm::vec3 f_ScaleAt(const Node& l_oN, float l_fT);
glm::vec4 f_ColorAt(const Node& l_oN, float l_fT);
std::string f_SpriteAt(const Node& l_oN, float l_fT);
bool f_VisibleAt(const Node& l_oN, float l_fT);
// An effect's params at time t (fx keys; the rest params without keys) and a trail's strength (0..1; 1 without keys).
glm::vec4 f_FxParamsAt(const Node& l_oN, float l_fT);
float f_TrailAt(const Node& l_oN, float l_fT);
// Any node of the clip has an effect or a trail.
bool f_HasEffects(const Clip& l_oC);
// The lattice's offsets at time t: every point (latticeCols() * latticeRows(), row major), eased between keys.
std::vector<glm::vec2> f_DeformAt(const Node& l_oN, float l_fT);

}  // namespace toms::anim
