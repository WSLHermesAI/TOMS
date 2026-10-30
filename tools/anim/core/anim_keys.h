// anim_keys.h -- channel-generic key editing on toms::anim nodes, for the anim editor (no Qt).
//
// The runtime (src/core/engine/anim_clip.h) stores one typed key vector per channel. The editor
// works on "channels" and "key times" generically: Value carries any channel's value (pos / rot /
// scale in xyz, color in xyzw, sprite/event in s, visible in b, deform in d). Key times are compared with
// kTimeEps, so 0.3 typed into a spin box finds the key at 0.3f.
#pragma once
#include "anim_clip.h"

#include <functional>
#include <string>
#include <vector>

namespace animed {

using toms::anim::Ease;
using toms::anim::Node;

// Deform: the lattice's offsets (anim_clip.h; docs/engine/16_MESH_ANIMATION.md). Fx: an effect's four params; Trail: the
// afterimages' strength in v.x (docs/engine/18_NODE_EFFECTS.md). Interpolated (eased) but not one of kInterpolated:
// "key all channels together" keys the transform and colour only.
enum class Channel { Pos, Rot, Scale, Color, Sprite, Visible, Event, Deform, Fx, Trail };
constexpr int kChannelCount = 10;
constexpr Channel kAllChannels[kChannelCount] = {Channel::Pos,    Channel::Rot,     Channel::Scale, Channel::Color,
                                                 Channel::Sprite, Channel::Visible, Channel::Event, Channel::Deform,
                                                 Channel::Fx,     Channel::Trail};
constexpr Channel kInterpolated[4] = {Channel::Pos, Channel::Rot, Channel::Scale, Channel::Color};

constexpr float kTimeEps = 1e-4f;
inline bool sameTime(float a, float b) { return (a > b ? a - b : b - a) < kTimeEps; }

bool isInterpolated(Channel c);
const char* channelName(Channel c);   // "pos", "rot", ... (the .anim track names)

struct Value {
    glm::vec4 v{0, 0, 0, 0};
    std::string s;
    bool b = false;
    toms::anim::Deform d;   // the lattice points that move ("5: 0, -2; 6: 0, -2" as text)
};

// ---- reading ----
int keyCount(const Node& n, Channel c);
std::vector<float> keyTimes(const Node& n, Channel c);
// Every key time over the node's channels (merged within kTimeEps), ascending.
std::vector<float> unionKeyTimes(const Node& n);
bool hasKeyAt(const Node& n, Channel c, float t);
bool keyAt(const Node& n, Channel c, float t, Value& out);   // the key's value, exactly at t
Value valueAt(const Node& n, Channel c, float t);            // sampled (event: the key at t or "")
Value restValue(const Node& n, Channel c);                   // event: ""
// The ease of the key at t / of the last key at or before t (the segment t is in). False when
// the channel is stepped or has no such key.
bool easeAt(const Node& n, Channel c, float t, Ease& out);
bool easeAtOrBefore(const Node& n, Channel c, float t, Ease& out, float* keyTime = nullptr);
// Pairs of keys of one channel closer than kTimeEps (the runtime would never reach the second).
int overlappingKeys(const Node& n, Channel c);

// ---- writing ----
// Inserts a key or updates the one at t (keeping its ease); keys stay sorted.
void setKey(Node& n, Channel c, float t, const Value& v);
bool removeKey(Node& n, Channel c, float t);
void setRest(Node& n, Channel c, const Value& v);   // event: ignored
bool setEase(Node& n, Channel c, float t, const Ease& e);   // the key at t; false if none
// Moves the keys of every channel at times from[i] to to[i] (all at once, so rows can swap
// past each other). Fails without changing anything when a moved key would land on a key of
// the same channel that is not moved, two moved keys collide, or a time is negative; `conflict`
// then names the channel and time.
bool moveKeyTimes(Node& n, const std::vector<float>& from, const std::vector<float>& to, std::string* conflict);
// Multiplies every key time of the node (and with `recursive` its subtree) by `factor`.
void scaleKeyTimes(Node& n, float factor, bool recursive);

// ---- text (the key list's cells) ----
std::string formatValue(Channel c, const Value& v);
bool parseValue(Channel c, const std::string& text, Value& out, std::string* err);
std::string formatEase(const Ease& e);           // a name, or "bezier(x1, y1, x2, y2)"
bool parseEaseText(const std::string& text, Ease& out);

// ---- tree ----
// A node by child-index path from the clip root (empty = the root); nullptr when out of range.
const Node* nodeAt(const Node& root, const std::vector<int>& path);
Node* nodeAt(Node& root, const std::vector<int>& path);
bool findPath(const Node& root, const Node* target, std::vector<int>& path);   // by address
void collectNames(const Node& root, std::vector<std::string>& out);
std::string uniqueNodeName(const Node& root, const std::string& base);
// A copied node (Edit > Copy): {"toms": "anim-node", "version": 3, "node": {...}}, the .anim's node format.
std::string nodeClipboardText(const Node& n);
// The node of a copied text; false when it is not one (other text on the clipboard).
bool nodeFromClipboardText(const std::string& text, Node& out, std::string* err = nullptr);
// Makes a subtree about to be put into the clip `root` fit it: every node whose name the clip (or an earlier node of
// the subtree) already has gets name_2, name_3, ...; a skin inside the subtree that names a renamed node is pointed at
// the new name, so a copied rig bends with its own bones. Returns how many nodes were renamed.
int renameForPaste(const Node& root, Node& pasted);
// A skin's bones that are nodes of the clip but not inside the skinned node (not it, not under it): moving the node
// does not carry them, so its mesh slides off them. Names in the skin's order.
std::vector<std::string> bonesOutside(const Node& root, const Node& skinned);

// ---- sprite references (anim_clip.h SpriteRef: "atlas:name" or a bare "name") ----
// Every non-empty sprite reference of the node and its subtree: rest sprites and sprite keys.
void collectSpriteRefs(const Node& root, std::vector<std::string>& out);
// Replaces each non-empty reference r of the subtree with f(r); returns how many changed.
int rewriteSpriteRefs(Node& root, const std::function<std::string(const std::string&)>& f);

}  // namespace animed
