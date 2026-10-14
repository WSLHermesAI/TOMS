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
// number, the z rotation) still load; they are saved as version 2.
//
// Draw order: depth first. A node draws its children with order < 0 (sorted by order, then by
// position in the list), then its own sprite, then the children with order >= 0. Colour multiplies
// down the tree unless a node sets inheritColor false; an invisible node hides its subtree.
//
// The game plays clips with AnimPlayer (anim_player.h); the editor evaluates them with the same
// code, so its preview is what the game draws.
#pragma once
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace toms::anim {

// Easing of the segment that starts at a key: Tweeny's (src/third_party/tweeny) Penner easings by
// name ("linear", "quadraticIn", ..., "backInOut", "stepped"), or a cubic Bezier
// [x1, y1, x2, y2] like CSS cubic-bezier().
struct Ease {
    int kind = 1;                      // tweeny::easing::enumerated as int; kBezier = Bezier
    float bezier[4] = {0, 0, 1, 1};
    static constexpr int kBezier = 1000;

    bool operator==(const Ease& o) const;
    bool operator!=(const Ease& o) const { return !(*this == o); }

    bool isLinear() const { return kind == 1; }
    bool isStepped() const { return kind == 2; }
    float apply(float progress) const;  // 0..1 -> eased 0..1 (may overshoot for back/elastic)
};
const std::vector<std::string>& easeNames();   // index = Ease::kind
bool parseEase(const std::string& name, Ease& out);
std::string easeName(const Ease& e);           // "" for a Bezier

template <class T> struct Key {
    float t = 0;
    T v{};
    Ease ease;                         // interpolated channels only

    bool operator==(const Key& o) const { return t == o.t && v == o.v && ease == o.ease; }
    bool operator!=(const Key& o) const { return !(*this == o); }
};

enum class Blend { Normal, Add };

struct Node {
    std::string name;
    std::string sprite;                // rest sprite ("atlas:name" or "name"); "" = a group node
    glm::vec3 pos{0, 0, 0};
    glm::vec3 rot{0, 0, 0};            // Euler degrees (see the top); z = the screen-plane turn
    glm::vec3 scale{1, 1, 1};
    glm::vec4 color{1, 1, 1, 1};
    bool visible = true;
    bool hasPivot = false;             // false: the sprite's pivot from the atlas (x_pivot, default centre)
    glm::vec2 pivot{0.5f, 0.5f};       // 0..1 of the sprite's original size, from the top-left
    int order = 0;                     // among siblings; < 0 draws before the parent's own sprite
    Blend blend = Blend::Normal;
    bool inheritColor = true;
    bool loop = false;                 // after its key range: play it again (see the top)
    bool stayAtLastFrame = true;       // after its key range (not looping): false = hide the subtree

    std::vector<Key<glm::vec3>> posKeys, rotKeys, scaleKeys;
    std::vector<Key<glm::vec4>> colorKeys;
    std::vector<Key<std::string>> spriteKeys, eventKeys;
    std::vector<Key<bool>> visibleKeys;

    std::vector<Node> children;

    float lastKeyTime() const;         // over this node's tracks and its subtree
    float firstKeyTime() const;        // over this node's tracks and its subtree (0 without keys)
    bool hasKeys() const;              // any key in this node's tracks or its subtree
    bool timed() const { return loop || !stayAtLastFrame; }   // plays its range its own way

    // Every field, keys and children included (what the editor's undo compares; exact floats).
    bool operator==(const Node& o) const;
    bool operator!=(const Node& o) const { return !(*this == o); }
};

struct Clip {
    std::string name;
    float length = 0;                  // seconds; 0 = up to the last key
    int playCount = 1;                 // times to play; -1 = loop forever
    bool stayAtLastFrame = true;       // after the last play: keep showing the end (else hide)
    Node root;

    float duration() const;            // length, or the last key time when length is 0

    bool operator==(const Clip& o) const {
        return name == o.name && length == o.length && playCount == o.playCount && stayAtLastFrame == o.stayAtLastFrame &&
               root == o.root;
    }
    bool operator!=(const Clip& o) const { return !(*this == o); }
};

// One atlas a file draws from. `path` is relative to the .anim. `id` names it in sprite
// references; by default it is the file name without ".atlas", and a file sets another one when two
// atlases share a file name (an art style's game.atlas next to the original's).
struct AtlasRef {
    std::string id;
    std::string path;
    bool operator==(const AtlasRef& o) const { return id == o.id && path == o.path; }
};
std::string defaultAtlasId(const std::string& path);   // "../fx/fx.atlas" -> "fx"

// A sprite reference -- a node's sprite or a sprite key -- is "atlas:sprite" (the sprite in the
// atlas with that id, like MPDI's PIName + ImageName), or a bare "sprite": the first atlas that
// has it.
struct SpriteRef {
    std::string atlas;   // "" = the first atlas that has the sprite
    std::string name;
};
SpriteRef parseSpriteRef(const std::string& ref);
std::string spriteRef(const std::string& atlasId, const std::string& name);   // "id:name", or "name" when id is ""

constexpr int kAnimVersion = 2;       // pos / rot / scale as [x, y, z]

struct AnimFile {
    int version = kAnimVersion;        // as read; saving always writes kAnimVersion
    // The .atlas files its sprites come from (anim_player.h AtlasSet), in lookup order. JSON:
    // "atlases": ["path", {"id": "...", "path": "..."}, ...] -- a plain path's id is its file name
    // without ".atlas"; an older file's single "atlas": "path" reads as a one-entry list.
    std::vector<AtlasRef> atlases;
    std::vector<Clip> clips;

    const Clip* find(const std::string& name) const;

    bool operator==(const AnimFile& o) const { return atlases == o.atlases && clips == o.clips; }   // not the version
    bool operator!=(const AnimFile& o) const { return !(*this == o); }
};

bool parseAnim(const std::string& json, AnimFile& out, std::string* err = nullptr);
std::string animToJson(const AnimFile& f);

// ---- evaluation --------------------------------------------------------------------------------

// One node at one time, in draw order.
struct NodePose {
    const Node* node = nullptr;
    int depth = 0;
    glm::mat4 world{1.0f};             // node space -> clip space (pixels, y down, z into the screen)
    glm::vec4 color{1, 1, 1, 1};       // with the parents' colour when inherited
    std::string sprite;                // "" = nothing to draw
    bool visible = true;               // false: hidden (itself or a parent)
};

// The time a node's tracks (and its children) see when its parent's time is t, with its key range
// first..last: wraps into the range for a looping node. `ended` (optional) is set when a
// stayAtLastFrame=false node is past its range (hidden).
float nodeTime(const Node& n, float t, float first, float last, bool* ended = nullptr);
// Any node in the clip loops (the clip keeps moving after its timeline ends).
bool hasLoopingNodes(const Clip& c);

// The local transform of a node at time t: T(pos) * R(rot) * S(scale), R = Rx * Ry * Rz.
glm::mat4 localTransform(const Node& n, float t);
glm::mat4 compose(const glm::vec3& pos, const glm::vec3& rotDegrees, const glm::vec3& scale);
// A 3D transform seen by the orthographic screen camera (looking down +z): the 2D affine map of the
// xy plane (z = 0) onto the screen. What the sprites are drawn with today.
glm::mat3 flatten(const glm::mat4& m);
// Every node of the clip at time t (clip time, 0..duration), in draw order (see the top). Hidden
// nodes are included with visible = false so an editor can still show them.
void evaluate(const Clip& c, float t, std::vector<NodePose>& out);
// The poses of one clip kept from frame to frame (what AnimPlayer uses; evaluate() is the stateless
// reference with the same results). Each node holds its current position, rotation, scale, colour,
// sprite and visibility, starting at its rest values, and each of its key tracks a cursor. A seek
// only looks at the tracks that have keys, each on its own: a node with only sprite keys never
// touches its transform, a node without keys is never updated after the first seek, a track holding
// a value (before its first key, after its last, a stepped segment) costs a compare, and a world
// transform / colour is only rebuilt when its own values or a parent's changed.
class PoseCache {
public:
    void bind(const Clip* clip);       // the rest pose; nullptr = nothing. The clip must outlive the binding.
    const Clip* clip() const { return clip_; }
    void seek(float t);                // clip time (0..duration); any direction, any step
    const std::vector<NodePose>& poses() const { return poses_; }   // draw order, as evaluate()
    // Pose i changed (world, colour, visibility or sprite) in the last seek.
    bool changed(size_t i) const { return i < changed_.size() && changed_[i]; }
    int changedCount() const { return changedCount_; }
    int animatedNodes() const { return animated_; }   // nodes with at least one key track

private:
    enum { kPos, kRot, kScale, kColor, kSprite, kVisible, kTracks };
    struct Item {
        const Node* node = nullptr;
        int parent = -1;               // index in items_ (parents come first)
        size_t pose = 0;               // index in poses_
        bool animated = false;
        int cursor[kTracks];           // last key at or before the time, -1 = before the first
        int held[kTracks];             // the key whose value the track holds now, -1 = between keys
        glm::vec3 pos{0}, rot{0}, scale{1};
        glm::vec4 color{1};            // own, unclamped (before the parents' colour)
        bool visible = true;           // own
        bool timed = false;            // loop or !stayAtLastFrame: its time is remapped
        float first = 0, last = 0;     // its subtree's key range (timed nodes)
        float time = 0;                // the time its tracks saw in the last seek
        bool ended = false;            // stayAtLastFrame=false and past its range: hidden
        glm::mat4 local{1.0f};
        bool worldChanged = false, colorChanged = false, visibleChanged = false;   // in this seek
    };
    int addItems(const Node& n, int parent, int depth);

    const Clip* clip_ = nullptr;
    std::vector<Item> items_;
    std::vector<NodePose> poses_;
    std::vector<unsigned char> changed_;
    int changedCount_ = 0;
    int animated_ = 0;
    bool fresh_ = true;                // the first seek computes everything
};

// Event names whose key time lies in (t0, t1], in time order. For a clip that wrapped around,
// call it for each part.
void eventsBetween(const Clip& c, float t0, float t1, std::vector<std::string>& out);

// Channel values at time t (rest value when the track is empty).
glm::vec3 positionAt(const Node& n, float t);
glm::vec3 rotationAt(const Node& n, float t);
glm::vec3 scaleAt(const Node& n, float t);
glm::vec4 colorAt(const Node& n, float t);
std::string spriteAt(const Node& n, float t);
bool visibleAt(const Node& n, float t);

}  // namespace toms::anim
