// anim_recipes.cpp -- see anim_recipes.h.
#include "anim_recipes.h"
#include "anim_check.h"
#include "anim_keys.h"
#include "anim_player.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>

namespace animed {

namespace fs = std::filesystem;
using toms::anim::Clip;
using toms::anim::Deform;
using toms::anim::Ease;
using toms::anim::Key;
using toms::anim::Node;

const std::vector<MotionRecipe>& f_MotionRecipes() {
    static const std::vector<MotionRecipe> l_vAll = {
        {"breathe", "Breathe", "the top rises and widens a little, then settles (loop it for an idle)", false, 2.0f},
        {"sway", "Sway", "the top leans left and right, the feet stay (wind, a slow idle)", false, 2.0f},
        {"squash", "Squash", "squashed down and wide, then a stretch and a settle (a hit, a landing, a jump start)", false, 0.6f},
        {"jelly", "Jelly", "a wobble of squash and stretch that dies out (a slime, a hit on something soft)", false, 1.0f},
        {"bone_sway", "Bone sway", "every bone of the skin turns left and right, more toward the tip (a tail, a plant, a body)", true, 2.0f},
        {"nod", "Nod", "the last bone of the skin dips and comes back (a head)", true, 0.8f},
        {"swing", "Swing", "the bones wind up back, swing through, and return (an attack)", true, 0.7f},
    };
    return l_vAll;
}

const MotionRecipe* f_FindMotionRecipe(const std::string& l_strId) {
    for (const MotionRecipe& l_oR : f_MotionRecipes())
        if (l_oR.m_strId == l_strId) return &l_oR;
    return nullptr;
}

namespace {

Ease f_Ease(const char* l_pszName) {
    Ease l_oE;
    toms::anim::f_ParseEase(l_pszName, l_oE);
    return l_oE;
}

// The keys between t0 and t1 replaced by `add` (sorted by time).
template <class T> void f_PutKeys(std::vector<Key<T>>& l_vKeys, float l_fT0, float l_fT1, std::vector<Key<T>> l_vAdd) {
    constexpr float l_fEps = 1e-4f;
    l_vKeys.erase(std::remove_if(l_vKeys.begin(), l_vKeys.end(), [&](const Key<T>& l_oK) { return l_oK.m_fT >= l_fT0 - l_fEps && l_oK.m_fT <= l_fT1 + l_fEps; }),
               l_vKeys.end());
    l_vKeys.insert(l_vKeys.end(), l_vAdd.begin(), l_vAdd.end());
    std::sort(l_vKeys.begin(), l_vKeys.end(), [](const Key<T>& l_oA, const Key<T>& l_oB) { return l_oA.m_fT < l_oB.m_fT; });
}

// A lattice pose: every point's offset for "amount" v of the recipe's shape (0 = at rest). Points that do not move are
// left out of the key (a deform key lists only moved points).
using Shape = glm::vec2 (*)(float l_fAcross, float l_fUp, float l_fV, float l_fW, float l_fH);
//   across: -1 (left edge) .. 1 (right edge); up: 0 (bottom row) .. 1 (top row)

glm::vec2 f_BreatheShape(float l_fAcross, float l_fUp, float l_fV, float l_fW, float l_fH) {
    return {l_fAcross * 0.04f * l_fW * l_fUp * l_fV, -0.06f * l_fH * l_fUp * l_fV};   // the top rises 6 %, widening 4 % with height
}
glm::vec2 f_SwayShape(float l_fAcross, float l_fUp, float l_fV, float l_fW, float) {
    (void)l_fAcross;
    return {0.15f * l_fW * std::pow(l_fUp, 1.5f) * l_fV, 0.0f};   // the top leans 15 % of the width
}
glm::vec2 f_SquashShape(float l_fAcross, float l_fUp, float l_fV, float l_fW, float l_fH) {
    return {l_fAcross * 0.12f * l_fW * l_fUp * l_fV, 0.2f * l_fH * l_fUp * l_fV};   // down 20 %, wide 12 % (v < 0: a stretch)
}

Deform f_LatticePose(int l_iCols, int l_iRows, Shape l_fnShape, float l_fV, float l_fW, float l_fH) {
    Deform l_mapD;
    for (int l_iR = 0; l_iR < l_iRows; l_iR++)
        for (int l_iC = 0; l_iC < l_iCols; l_iC++) {
            const float l_fAcross = l_iCols > 1 ? 2.0f * l_iC / (l_iCols - 1) - 1.0f : 0.0f;
            const float l_fUp = l_iRows > 1 ? 1.0f - float(l_iR) / (l_iRows - 1) : 1.0f;
            glm::vec2 l_v2O = l_fnShape(l_fAcross, l_fUp, l_fV, l_fW, l_fH);
            l_v2O = glm::round(l_v2O * 4.0f) / 4.0f;   // quarter pixels: tidy numbers in the file
            if (l_v2O != glm::vec2(0.0f)) l_mapD[l_iR * l_iCols + l_iC] = l_v2O;
        }
    return l_mapD;
}

// (time 0..1 of the period, amount) pairs per lattice recipe.
std::vector<std::pair<float, float>> f_LatticeCurve(const std::string& l_strId) {
    if (l_strId == "breathe") return {{0, 0}, {0.5f, 1}, {1, 0}};
    if (l_strId == "sway") return {{0, 0}, {0.25f, 1}, {0.75f, -1}, {1, 0}};
    if (l_strId == "squash") return {{0, 0}, {0.12f, 1}, {0.3f, -0.45f}, {0.5f, 0.15f}, {0.7f, -0.05f}, {1, 0}};
    if (l_strId == "jelly") return {{0, 0}, {0.12f, 1}, {0.27f, -0.7f}, {0.42f, 0.45f}, {0.57f, -0.25f}, {0.75f, 0.1f}, {1, 0}};
    return {};
}

// (time 0..1, degrees at strength 1) per bone recipe; `tipOnly`: only the last bone moves.
std::vector<std::pair<float, float>> f_BoneCurve(const std::string& l_strId, bool& l_bTipOnly) {
    l_bTipOnly = l_strId == "nod";
    if (l_strId == "bone_sway") return {{0, 0}, {0.25f, 8}, {0.75f, -8}, {1, 0}};
    if (l_strId == "nod") return {{0, 0}, {0.35f, 12}, {0.65f, -3}, {1, 0}};
    if (l_strId == "swing") return {{0, 0}, {0.35f, -20}, {0.5f, 30}, {0.75f, 6}, {1, 0}};
    return {};
}

Node* f_FindNodeMut(Node& l_oN, const std::string& l_strName) {
    if (l_oN.m_strName == l_strName) return &l_oN;
    for (Node& l_oC : l_oN.m_vChildren)
        if (Node* l_pF = f_FindNodeMut(l_oC, l_strName)) return l_pF;
    return nullptr;
}

}  // namespace

bool f_ApplyMotionRecipe(Clip& l_oClip, const std::vector<int>& l_vNodePath, const std::string& l_strId, const RecipeParams& l_oParams,
                       std::string* l_pErr) {
    const MotionRecipe* l_pRecipe = f_FindMotionRecipe(l_strId);
    if (!l_pRecipe) {
        if (l_pErr) *l_pErr = "no recipe '" + l_strId + "'";
        return false;
    }
    Node* l_pNode = f_NodeAt(l_oClip.m_oRoot, l_vNodePath);
    if (!l_pNode) {
        if (l_pErr) *l_pErr = "no such node";
        return false;
    }
    const float l_fPeriod = l_oParams.m_fPeriod > 0 ? l_oParams.m_fPeriod : l_pRecipe->m_fPeriod;
    const float l_fK = l_oParams.m_fStrength, l_fT0 = std::max(0.0f, l_oParams.m_fStart), l_fT1 = l_fT0 + l_fPeriod;
    const Ease l_oSmooth = f_Ease("sinusoidalInOut");
    if (!l_pRecipe->m_bBones) {
        if (l_oParams.m_fSpriteW <= 0 || l_oParams.m_fSpriteH <= 0) {
            if (l_pErr) *l_pErr = "the node's sprite size is not known (no sprite, or it is in no atlas)";
            return false;
        }
        Shape l_fnShape = l_strId == "breathe" ? f_BreatheShape : l_strId == "sway" ? f_SwayShape : f_SquashShape;   // squash and jelly share it
        std::vector<Key<Deform>> l_vAdd;
        for (const auto& [l_fF, l_fV] : f_LatticeCurve(l_strId)) {
            Key<Deform> l_oKey;
            l_oKey.m_fT = l_fT0 + l_fF * l_fPeriod;
            l_oKey.m_oV = f_LatticePose(l_pNode->f_LatticeCols(), l_pNode->f_LatticeRows(), l_fnShape, l_fV * l_fK, l_oParams.m_fSpriteW, l_oParams.m_fSpriteH);
            l_oKey.m_oEase = l_oSmooth;
            l_vAdd.push_back(std::move(l_oKey));
        }
        f_PutKeys(l_pNode->m_vDeformKeys, l_fT0, l_fT1, std::move(l_vAdd));
    } else {
        if (!l_pNode->f_Skinned()) {
            if (l_pErr) *l_pErr = "'" + l_pRecipe->m_strTitle + "' needs a skinned node (Properties > Skin)";
            return false;
        }
        std::vector<Node*> l_vBones;
        for (const std::string& l_strB : l_pNode->m_vSkinBones)
            if (Node* l_pBn = f_FindNodeMut(l_oClip.m_oRoot, l_strB)) l_vBones.push_back(l_pBn);
        if (l_vBones.empty()) {
            if (l_pErr) *l_pErr = "none of the skin's bones is a node of this clip";
            return false;
        }
        bool l_bTipOnly = false;
        const auto l_vCurve = f_BoneCurve(l_strId, l_bTipOnly);
        for (size_t l_nI = 0; l_nI < l_vBones.size(); l_nI++) {
            if (l_bTipOnly && l_nI + 1 != l_vBones.size()) continue;
            const float l_fShare = l_bTipOnly ? 1.0f : float(l_nI + 1) / float(l_vBones.size());   // more toward the tip
            Node& l_oB = *l_vBones[l_nI];
            std::vector<Key<glm::vec3>> l_vAdd;
            for (const auto& [l_fF, l_fDeg] : l_vCurve) {
                Key<glm::vec3> l_oKey;
                l_oKey.m_fT = l_fT0 + l_fF * l_fPeriod;
                l_oKey.m_oV = l_oB.m_v3Rot + glm::vec3(0, 0, std::round(l_fDeg * l_fK * l_fShare * 10.0f) / 10.0f);
                l_oKey.m_oEase = l_oSmooth;
                l_vAdd.push_back(l_oKey);
            }
            f_PutKeys(l_oB.m_vRotKeys, l_fT0, l_fT1, std::move(l_vAdd));
        }
    }
    if (l_oClip.m_fLength > 0 && l_oClip.m_fLength < l_fT1) l_oClip.m_fLength = l_fT1;
    return true;
}

int f_HeadlessRecipe(const std::vector<std::string>& l_vArgs) {
    // args: "recipe" ...
    if (l_vArgs.size() >= 2 && l_vArgs[1] == "--list") {
        for (const MotionRecipe& l_oR : f_MotionRecipes())
            std::printf("%-10s %-7s %4.1f s  %s\n", l_oR.m_strId.c_str(), l_oR.m_bBones ? "bones" : "lattice", l_oR.m_fPeriod, l_oR.m_strDescription.c_str());
        return 0;
    }
    std::string l_strFile, l_strOut, l_strClipName, l_strNodeName, l_strId;
    RecipeParams l_oP;
    for (size_t l_nI = 1; l_nI < l_vArgs.size(); l_nI++) {
        const bool l_bMore = l_nI + 1 < l_vArgs.size();
        if (l_vArgs[l_nI] == "--node" && l_bMore) l_strNodeName = l_vArgs[++l_nI];
        else if (l_vArgs[l_nI] == "--recipe" && l_bMore) l_strId = l_vArgs[++l_nI];
        else if (l_vArgs[l_nI] == "--clip" && l_bMore) l_strClipName = l_vArgs[++l_nI];
        else if (l_vArgs[l_nI] == "--start" && l_bMore) l_oP.m_fStart = std::strtof(l_vArgs[++l_nI].c_str(), nullptr);
        else if (l_vArgs[l_nI] == "--period" && l_bMore) l_oP.m_fPeriod = std::strtof(l_vArgs[++l_nI].c_str(), nullptr);
        else if (l_vArgs[l_nI] == "--strength" && l_bMore) l_oP.m_fStrength = std::strtof(l_vArgs[++l_nI].c_str(), nullptr);
        else if (l_vArgs[l_nI] == "--out" && l_bMore) l_strOut = l_vArgs[++l_nI];
        else if (l_vArgs[l_nI].rfind("--", 0) != 0 && l_strFile.empty()) l_strFile = l_vArgs[l_nI];
        else { std::fprintf(stderr, "unknown argument: %s\n", l_vArgs[l_nI].c_str()); return 3; }
    }
    if (l_strFile.empty() || l_strNodeName.empty() || l_strId.empty()) {
        std::fputs("usage: anim_editor --headless recipe <file.anim> --node <name> --recipe <id> [--clip <name>] [--start s]\n"
                   "       [--period s] [--strength k] [--out <file.anim>]\n"
                   "       anim_editor --headless recipe --list\n"
                   "  writes a motion recipe's keys on the node (docs/engine/06_ANIMATION_RECIPES.md section 7)\n",
                   stderr);
        return 3;
    }
    std::string l_strText, l_strErr;
    toms::anim::AnimFile l_oF;
    if (!f_ReadTextFile(l_strFile, l_strText) || !toms::anim::f_ParseAnim(l_strText, l_oF, &l_strErr)) {
        std::fprintf(stderr, "%s: error: %s\n", l_strFile.c_str(), l_strErr.empty() ? "cannot read the file" : l_strErr.c_str());
        return 2;
    }
    Clip* l_pClip = nullptr;
    for (Clip& l_oC : l_oF.m_vClips)
        if (l_strClipName.empty() ? !l_pClip : l_oC.m_strName == l_strClipName) l_pClip = &l_oC;
    if (!l_pClip) { std::fprintf(stderr, "%s: error: no clip '%s'\n", l_strFile.c_str(), l_strClipName.c_str()); return 2; }
    // The node's path, and its sprite's original size from the file's atlases.
    std::vector<int> l_vPath;
    std::function<bool(const Node&)> l_fnFind = [&](const Node& l_oN) {
        if (l_oN.m_strName == l_strNodeName) return true;
        for (size_t l_nI = 0; l_nI < l_oN.m_vChildren.size(); l_nI++) {
            l_vPath.push_back((int)l_nI);
            if (l_fnFind(l_oN.m_vChildren[l_nI])) return true;
            l_vPath.pop_back();
        }
        return false;
    };
    if (!l_fnFind(l_pClip->m_oRoot)) { std::fprintf(stderr, "%s: error: clip '%s' has no node '%s'\n", l_strFile.c_str(), l_pClip->m_strName.c_str(), l_strNodeName.c_str()); return 2; }
    const Node* l_pNode = f_NodeAt(l_pClip->m_oRoot, l_vPath);
    std::vector<toms::AtlasFile> l_vAtlases(l_oF.m_vAtlases.size());
    toms::anim::AtlasSet l_oSet;
    for (size_t l_nI = 0; l_nI < l_oF.m_vAtlases.size(); l_nI++) {
        const fs::path l_oAp = f_IsAbsoluteAtlasPath(l_oF.m_vAtlases[l_nI].m_strPath) ? fs::u8path(l_oF.m_vAtlases[l_nI].m_strPath)
                                                                   : fs::u8path(l_strFile).parent_path() / fs::u8path(l_oF.m_vAtlases[l_nI].m_strPath);
        std::string l_strAtext;
        if (f_ReadTextFile(l_oAp.u8string(), l_strAtext) && toms::f_ParseAtlas(l_strAtext, l_vAtlases[l_nI], nullptr)) l_oSet.f_Add(l_vAtlases[l_nI], l_oF.m_vAtlases[l_nI].m_strId);
    }
    const std::string l_strSprite = toms::anim::f_SpriteAt(*l_pNode, l_oP.m_fStart);
    if (const toms::AtlasRegion* l_pR = l_strSprite.empty() ? nullptr : l_oSet.f_Find(l_strSprite)) {
        l_oP.m_fSpriteW = (float)l_pR->m_iOrigW;
        l_oP.m_fSpriteH = (float)l_pR->m_iOrigH;
    }
    if (!f_ApplyMotionRecipe(*l_pClip, l_vPath, l_strId, l_oP, &l_strErr)) {
        std::fprintf(stderr, "%s: error: %s\n", l_strFile.c_str(), l_strErr.c_str());
        return 2;
    }
    const std::string l_strTarget = l_strOut.empty() ? l_strFile : l_strOut;
    std::ofstream l_oO(fs::u8path(l_strTarget), std::ios::binary);
    l_oO << toms::anim::f_AnimToJson(l_oF);
    if (!l_oO) { std::fprintf(stderr, "error: cannot write %s\n", l_strTarget.c_str()); return 2; }
    std::printf("%s: '%s' written on %s/%s from %.2f s\n", l_strTarget.c_str(), l_strId.c_str(), l_pClip->m_strName.c_str(), l_strNodeName.c_str(), l_oP.m_fStart);
    return 0;
}

}  // namespace animed
