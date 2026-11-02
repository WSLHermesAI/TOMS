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
constexpr int g_iChannelCount = 10;
constexpr Channel g_arrAllChannels[g_iChannelCount] = {Channel::Pos,    Channel::Rot,     Channel::Scale, Channel::Color,
                                                 Channel::Sprite, Channel::Visible, Channel::Event, Channel::Deform,
                                                 Channel::Fx,     Channel::Trail};
constexpr Channel g_arrInterpolated[4] = {Channel::Pos, Channel::Rot, Channel::Scale, Channel::Color};

constexpr float g_fTimeEps = 1e-4f;
inline bool f_SameTime(float l_fA, float l_fB) { return (l_fA > l_fB ? l_fA - l_fB : l_fB - l_fA) < g_fTimeEps; }

bool f_IsInterpolated(Channel l_eC);
const char* f_ChannelName(Channel l_eC);   // "pos", "rot", ... (the .anim track names)

struct Value {
    glm::vec4 m_v4V{0, 0, 0, 0};
    std::string m_strS;
    bool m_bB = false;
    toms::anim::Deform m_mapD;   // the lattice points that move ("5: 0, -2; 6: 0, -2" as text)
};

// ---- reading ----
int f_KeyCount(const Node& l_oN, Channel l_eC);
std::vector<float> f_KeyTimes(const Node& l_oN, Channel l_eC);
// Every key time over the node's channels (merged within kTimeEps), ascending.
std::vector<float> f_UnionKeyTimes(const Node& l_oN);
bool f_HasKeyAt(const Node& l_oN, Channel l_eC, float l_fT);
bool f_KeyAt(const Node& l_oN, Channel l_eC, float l_fT, Value& l_oOut);   // the key's value, exactly at t
Value f_ValueAt(const Node& l_oN, Channel l_eC, float l_fT);            // sampled (event: the key at t or "")
Value f_RestValue(const Node& l_oN, Channel l_eC);                   // event: ""
// The ease of the key at t / of the last key at or before t (the segment t is in). False when
// the channel is stepped or has no such key.
bool f_EaseAt(const Node& l_oN, Channel l_eC, float l_fT, Ease& l_oOut);
bool f_EaseAtOrBefore(const Node& l_oN, Channel l_eC, float l_fT, Ease& l_oOut, float* l_pKeyTime = nullptr);
// Pairs of keys of one channel closer than kTimeEps (the runtime would never reach the second).
int f_OverlappingKeys(const Node& l_oN, Channel l_eC);

// ---- writing ----
// Inserts a key or updates the one at t (keeping its ease); keys stay sorted.
void f_SetKey(Node& l_oN, Channel l_eC, float l_fT, const Value& l_oV);
bool f_RemoveKey(Node& l_oN, Channel l_eC, float l_fT);
void f_SetRest(Node& l_oN, Channel l_eC, const Value& l_oV);   // event: ignored
bool f_SetEase(Node& l_oN, Channel l_eC, float l_fT, const Ease& l_oE);   // the key at t; false if none
// Moves the keys of every channel at times from[i] to to[i] (all at once, so rows can swap
// past each other). Fails without changing anything when a moved key would land on a key of
// the same channel that is not moved, two moved keys collide, or a time is negative; `conflict`
// then names the channel and time.
bool f_MoveKeyTimes(Node& l_oN, const std::vector<float>& l_vFrom, const std::vector<float>& l_vTo, std::string* l_pConflict);
// Multiplies every key time of the node (and with `recursive` its subtree) by `factor`.
void f_ScaleKeyTimes(Node& l_oN, float l_fFactor, bool l_bRecursive);

// ---- text (the key list's cells) ----
std::string f_FormatValue(Channel l_eC, const Value& l_oV);
bool f_ParseValue(Channel l_eC, const std::string& l_strText, Value& l_oOut, std::string* l_pErr);
std::string f_FormatEase(const Ease& l_oE);           // a name, or "bezier(x1, y1, x2, y2)"
bool f_ParseEaseText(const std::string& l_strText, Ease& l_oOut);

// ---- tree ----
// A node by child-index path from the clip root (empty = the root); nullptr when out of range.
const Node* f_NodeAt(const Node& l_oRoot, const std::vector<int>& l_vPath);
Node* f_NodeAt(Node& l_oRoot, const std::vector<int>& l_vPath);
bool f_FindPath(const Node& l_oRoot, const Node* l_pTarget, std::vector<int>& l_vPath);   // by address
void f_CollectNames(const Node& l_oRoot, std::vector<std::string>& l_vOut);
std::string f_UniqueNodeName(const Node& l_oRoot, const std::string& l_strBase);
// A copied node (Edit > Copy): {"toms": "anim-node", "version": 3, "node": {...}}, the .anim's node format.
std::string f_NodeClipboardText(const Node& l_oN);
// The node of a copied text; false when it is not one (other text on the clipboard).
bool f_NodeFromClipboardText(const std::string& l_strText, Node& l_oOut, std::string* l_pErr = nullptr);
// Makes a subtree about to be put into the clip `root` fit it: every node whose name the clip (or an earlier node of
// the subtree) already has gets name_2, name_3, ...; a skin inside the subtree that names a renamed node is pointed at
// the new name, so a copied rig bends with its own bones. Returns how many nodes were renamed.
int f_RenameForPaste(const Node& l_oRoot, Node& l_oPasted);
// A skin's bones that are nodes of the clip but not inside the skinned node (not it, not under it): moving the node
// does not carry them, so its mesh slides off them. Names in the skin's order.
std::vector<std::string> f_BonesOutside(const Node& l_oRoot, const Node& l_oSkinned);

// ---- sprite references (anim_clip.h SpriteRef: "atlas:name" or a bare "name") ----
// Every non-empty sprite reference of the node and its subtree: rest sprites and sprite keys.
void f_CollectSpriteRefs(const Node& l_oRoot, std::vector<std::string>& l_vOut);
// Replaces each non-empty reference r of the subtree with f(r); returns how many changed.
int f_RewriteSpriteRefs(Node& l_oRoot, const std::function<std::string(const std::string&)>& l_fnF);

}  // namespace animed
