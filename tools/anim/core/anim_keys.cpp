// anim_keys.cpp -- see anim_keys.h.
#include "anim_keys.h"
#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace animed {

using toms::anim::Key;

namespace {

// Calls f(keys) with the channel's key vector (const or not, following NodeT).
template <class NodeT, class F> auto f_Visit(NodeT& l_oN, Channel l_eC, F&& l_oF) {
    switch (l_eC) {
    case Channel::Pos: return l_oF(l_oN.m_vPosKeys);
    case Channel::Rot: return l_oF(l_oN.m_vRotKeys);
    case Channel::Scale: return l_oF(l_oN.m_vScaleKeys);
    case Channel::Color: return l_oF(l_oN.m_vColorKeys);
    case Channel::Sprite: return l_oF(l_oN.m_vSpriteKeys);
    case Channel::Visible: return l_oF(l_oN.m_vVisibleKeys);
    case Channel::Deform: return l_oF(l_oN.m_vDeformKeys);
    case Channel::Fx: return l_oF(l_oN.m_vFxKeys);
    case Channel::Trail: return l_oF(l_oN.m_vTrailKeys);
    case Channel::Event: break;
    }
    return l_oF(l_oN.m_vEventKeys);
}

template <class T> struct Conv;
template <> struct Conv<glm::vec3> {
    static glm::vec3 f_From(const Value& l_oV) { return {l_oV.m_v4V.x, l_oV.m_v4V.y, l_oV.m_v4V.z}; }
    static Value f_To(const glm::vec3& l_v3X) { Value l_oV; l_oV.m_v4V = {l_v3X.x, l_v3X.y, l_v3X.z, 0}; return l_oV; }
};
template <> struct Conv<glm::vec4> {
    static glm::vec4 f_From(const Value& l_oV) { return l_oV.m_v4V; }
    static Value f_To(const glm::vec4& l_v4X) { Value l_oV; l_oV.m_v4V = l_v4X; return l_oV; }
};
template <> struct Conv<std::string> {
    static std::string f_From(const Value& l_oV) { return l_oV.m_strS; }
    static Value f_To(const std::string& l_strX) { Value l_oV; l_oV.m_strS = l_strX; return l_oV; }
};
template <> struct Conv<bool> {
    static bool f_From(const Value& l_oV) { return l_oV.m_bB; }
    static Value f_To(bool l_bX) { Value l_oV; l_oV.m_bB = l_bX; return l_oV; }
};
template <> struct Conv<float> {
    static float f_From(const Value& l_oV) { return l_oV.m_v4V.x; }
    static Value f_To(float l_fX) { Value l_oV; l_oV.m_v4V = {l_fX, 0, 0, 0}; return l_oV; }
};
template <> struct Conv<toms::anim::Deform> {
    static toms::anim::Deform f_From(const Value& l_oV) { return l_oV.m_mapD; }
    static Value f_To(const toms::anim::Deform& l_mapX) { Value l_oV; l_oV.m_mapD = l_mapX; return l_oV; }
};

template <class T> int f_FindKey(const std::vector<Key<T>>& l_vKeys, float l_fT) {
    for (size_t l_nI = 0; l_nI < l_vKeys.size(); l_nI++)
        if (f_SameTime(l_vKeys[l_nI].m_fT, l_fT)) return (int)l_nI;
    return -1;
}

template <class T> void f_SortKeys(std::vector<Key<T>>& l_vKeys) {
    std::stable_sort(l_vKeys.begin(), l_vKeys.end(), [](const Key<T>& l_oA, const Key<T>& l_oB) { return l_oA.m_fT < l_oB.m_fT; });
}

std::string f_Num(float l_fF) {
    char l_pszBuf[32];
    std::snprintf(l_pszBuf, sizeof l_pszBuf, "%.3f", l_fF);
    std::string l_strS = l_pszBuf;
    while (!l_strS.empty() && l_strS.back() == '0') l_strS.pop_back();
    if (!l_strS.empty() && l_strS.back() == '.') l_strS.pop_back();
    if (l_strS == "-0") l_strS = "0";
    return l_strS;
}

std::string f_Trim(const std::string& l_strS) {
    size_t l_nA = 0, l_nB = l_strS.size();
    while (l_nA < l_nB && std::isspace((unsigned char)l_strS[l_nA])) l_nA++;
    while (l_nB > l_nA && std::isspace((unsigned char)l_strS[l_nB - 1])) l_nB--;
    return l_strS.substr(l_nA, l_nB - l_nA);
}

// "1, 2" / "1 2" / "(1; 2)" -> numbers.
bool f_ParseNumbers(const std::string& l_strText, std::vector<float>& l_vOut) {
    std::string l_strT;
    for (char l_cCh : l_strText) l_strT += (l_cCh == ',' || l_cCh == ';' || l_cCh == '(' || l_cCh == ')' || l_cCh == '[' || l_cCh == ']') ? ' ' : l_cCh;
    std::istringstream l_oIn(l_strT);
    std::string l_strTok;
    l_vOut.clear();
    while (l_oIn >> l_strTok) {
        char* l_pszEnd = nullptr;
        const float l_fF = std::strtof(l_strTok.c_str(), &l_pszEnd);
        if (!l_pszEnd || *l_pszEnd != '\0' || !std::isfinite(l_fF)) return false;
        l_vOut.push_back(l_fF);
    }
    return true;
}

}  // namespace

bool f_IsInterpolated(Channel l_eC) {
    return l_eC == Channel::Pos || l_eC == Channel::Rot || l_eC == Channel::Scale || l_eC == Channel::Color || l_eC == Channel::Deform ||
           l_eC == Channel::Fx || l_eC == Channel::Trail;
}

const char* f_ChannelName(Channel l_eC) {
    switch (l_eC) {
    case Channel::Pos: return "pos";
    case Channel::Rot: return "rot";
    case Channel::Scale: return "scale";
    case Channel::Color: return "color";
    case Channel::Sprite: return "sprite";
    case Channel::Visible: return "visible";
    case Channel::Deform: return "deform";
    case Channel::Fx: return "fx";
    case Channel::Trail: return "trail";
    case Channel::Event: break;
    }
    return "event";
}

int f_KeyCount(const Node& l_oN, Channel l_eC) {
    return f_Visit(l_oN, l_eC, [](const auto& l_oKeys) { return (int)l_oKeys.size(); });
}

std::vector<float> f_KeyTimes(const Node& l_oN, Channel l_eC) {
    return f_Visit(l_oN, l_eC, [](const auto& l_oKeys) {
        std::vector<float> l_vOut;
        for (const auto& l_oK : l_oKeys) l_vOut.push_back(l_oK.m_fT);
        return l_vOut;
    });
}

std::vector<float> f_UnionKeyTimes(const Node& l_oN) {
    std::vector<float> l_vAll;
    for (Channel l_eC : g_arrAllChannels) {
        const std::vector<float> l_vT = f_KeyTimes(l_oN, l_eC);
        l_vAll.insert(l_vAll.end(), l_vT.begin(), l_vT.end());
    }
    std::sort(l_vAll.begin(), l_vAll.end());
    std::vector<float> l_vOut;
    for (float l_fT : l_vAll)
        if (l_vOut.empty() || !f_SameTime(l_vOut.back(), l_fT)) l_vOut.push_back(l_fT);
    return l_vOut;
}

bool f_HasKeyAt(const Node& l_oN, Channel l_eC, float l_fT) {
    return f_Visit(l_oN, l_eC, [l_fT](const auto& l_oKeys) { return f_FindKey(l_oKeys, l_fT) >= 0; });
}

bool f_KeyAt(const Node& l_oN, Channel l_eC, float l_fT, Value& l_oOut) {
    return f_Visit(l_oN, l_eC, [&](const auto& l_oKeys) {
        using T = decltype(l_oKeys.front().m_oV);
        const int l_iI = f_FindKey(l_oKeys, l_fT);
        if (l_iI < 0) return false;
        l_oOut = Conv<std::decay_t<T>>::f_To(l_oKeys[(size_t)l_iI].m_oV);
        return true;
    });
}

Value f_RestValue(const Node& l_oN, Channel l_eC) {
    switch (l_eC) {
    case Channel::Pos: return Conv<glm::vec3>::f_To(l_oN.m_v3Pos);
    case Channel::Rot: return Conv<glm::vec3>::f_To(l_oN.m_v3Rot);
    case Channel::Scale: return Conv<glm::vec3>::f_To(l_oN.m_v3Scale);
    case Channel::Color: return Conv<glm::vec4>::f_To(l_oN.m_v4Color);
    case Channel::Sprite: return Conv<std::string>::f_To(l_oN.m_strSprite);
    case Channel::Visible: return Conv<bool>::f_To(l_oN.m_bVisible);
    case Channel::Fx: return Conv<glm::vec4>::f_To(l_oN.m_oFx.m_v4Params);
    case Channel::Trail: return Conv<float>::f_To(1.0f);   // no keys: full strength
    case Channel::Deform:   // no rest: the lattice's points sit on the image
    case Channel::Event: break;
    }
    return Value();
}

Value f_ValueAt(const Node& l_oN, Channel l_eC, float l_fT) {
    using namespace toms::anim;
    switch (l_eC) {
    case Channel::Pos: return Conv<glm::vec3>::f_To(f_PositionAt(l_oN, l_fT));
    case Channel::Rot: return Conv<glm::vec3>::f_To(f_RotationAt(l_oN, l_fT));
    case Channel::Scale: return Conv<glm::vec3>::f_To(f_ScaleAt(l_oN, l_fT));
    case Channel::Color: {
        // Unclamped would be the raw track; the runtime clamps, and so does what the user sees.
        return Conv<glm::vec4>::f_To(f_ColorAt(l_oN, l_fT));
    }
    case Channel::Sprite: return Conv<std::string>::f_To(f_SpriteAt(l_oN, l_fT));
    case Channel::Visible: return Conv<bool>::f_To(f_VisibleAt(l_oN, l_fT));
    case Channel::Fx: return Conv<glm::vec4>::f_To(f_FxParamsAt(l_oN, l_fT));
    case Channel::Trail: return Conv<float>::f_To(f_TrailAt(l_oN, l_fT));
    case Channel::Deform: {   // the points that are off their place at t
        Value l_oV;
        const std::vector<glm::vec2> l_vAll = f_DeformAt(l_oN, l_fT);
        for (size_t l_nI = 0; l_nI < l_vAll.size(); l_nI++)
            if (l_vAll[l_nI] != glm::vec2(0.0f)) l_oV.m_mapD[(int)l_nI] = l_vAll[l_nI];
        return l_oV;
    }
    case Channel::Event: break;
    }
    Value l_oV;
    f_KeyAt(l_oN, l_eC, l_fT, l_oV);
    return l_oV;
}

bool f_EaseAt(const Node& l_oN, Channel l_eC, float l_fT, Ease& l_oOut) {
    if (!f_IsInterpolated(l_eC)) return false;
    return f_Visit(l_oN, l_eC, [&](const auto& l_oKeys) {
        const int l_iI = f_FindKey(l_oKeys, l_fT);
        if (l_iI < 0) return false;
        l_oOut = l_oKeys[(size_t)l_iI].m_oEase;
        return true;
    });
}

bool f_EaseAtOrBefore(const Node& l_oN, Channel l_eC, float l_fT, Ease& l_oOut, float* l_pKeyTime) {
    if (!f_IsInterpolated(l_eC)) return false;
    return f_Visit(l_oN, l_eC, [&](const auto& l_oKeys) {
        int l_iBest = -1;
        for (size_t l_nI = 0; l_nI < l_oKeys.size(); l_nI++)
            if (l_oKeys[l_nI].m_fT <= l_fT + g_fTimeEps) l_iBest = (int)l_nI;
        if (l_iBest < 0) return false;
        l_oOut = l_oKeys[(size_t)l_iBest].m_oEase;
        if (l_pKeyTime) *l_pKeyTime = l_oKeys[(size_t)l_iBest].m_fT;
        return true;
    });
}

int f_OverlappingKeys(const Node& l_oN, Channel l_eC) {
    return f_Visit(l_oN, l_eC, [](const auto& l_oKeys) {
        int l_iCount = 0;
        for (size_t l_nI = 1; l_nI < l_oKeys.size(); l_nI++)
            if (f_SameTime(l_oKeys[l_nI - 1].m_fT, l_oKeys[l_nI].m_fT)) l_iCount++;
        return l_iCount;
    });
}

void f_SetKey(Node& l_oN, Channel l_eC, float l_fT, const Value& l_oV) {
    f_Visit(l_oN, l_eC, [&](auto& l_oKeys) {
        using T = std::decay_t<decltype(l_oKeys.front().m_oV)>;
        const int l_iI = f_FindKey(l_oKeys, l_fT);
        if (l_iI >= 0) {
            l_oKeys[(size_t)l_iI].m_oV = Conv<T>::f_From(l_oV);
            return 0;
        }
        Key<T> l_oK;
        l_oK.m_fT = std::max(0.0f, l_fT);
        l_oK.m_oV = Conv<T>::f_From(l_oV);
        // A key dropped inside a segment keeps that segment's easing on both halves.
        for (const auto& l_oOther : l_oKeys)
            if (l_oOther.m_fT < l_fT) l_oK.m_oEase = l_oOther.m_oEase;
        l_oKeys.push_back(l_oK);
        f_SortKeys(l_oKeys);
        return 0;
    });
}

bool f_RemoveKey(Node& l_oN, Channel l_eC, float l_fT) {
    return f_Visit(l_oN, l_eC, [&](auto& l_oKeys) {
        const int l_iI = f_FindKey(l_oKeys, l_fT);
        if (l_iI < 0) return false;
        l_oKeys.erase(l_oKeys.begin() + l_iI);
        return true;
    });
}

void f_SetRest(Node& l_oN, Channel l_eC, const Value& l_oV) {
    switch (l_eC) {
    case Channel::Pos: l_oN.m_v3Pos = Conv<glm::vec3>::f_From(l_oV); break;
    case Channel::Rot: l_oN.m_v3Rot = Conv<glm::vec3>::f_From(l_oV); break;
    case Channel::Scale: l_oN.m_v3Scale = Conv<glm::vec3>::f_From(l_oV); break;
    case Channel::Color: l_oN.m_v4Color = l_oV.m_v4V; break;
    case Channel::Sprite: l_oN.m_strSprite = l_oV.m_strS; break;
    case Channel::Visible: l_oN.m_bVisible = l_oV.m_bB; break;
    case Channel::Fx: l_oN.m_oFx.m_v4Params = l_oV.m_v4V; break;
    case Channel::Trail:    // keys only (no keys = 1)
    case Channel::Deform:   // keys only
    case Channel::Event: break;
    }
}

bool f_SetEase(Node& l_oN, Channel l_eC, float l_fT, const Ease& l_oE) {
    if (!f_IsInterpolated(l_eC)) return false;
    return f_Visit(l_oN, l_eC, [&](auto& l_oKeys) {
        const int l_iI = f_FindKey(l_oKeys, l_fT);
        if (l_iI < 0) return false;
        l_oKeys[(size_t)l_iI].m_oEase = l_oE;
        return true;
    });
}

bool f_MoveKeyTimes(Node& l_oN, const std::vector<float>& l_vFrom, const std::vector<float>& l_vTo, std::string* l_pConflict) {
    if (l_vFrom.size() != l_vTo.size()) return false;
    for (float l_fT : l_vTo)
        if (l_fT < -g_fTimeEps) {
            if (l_pConflict) *l_pConflict = "time " + f_Num(l_fT) + " is negative";
            return false;
        }
    Node l_oWork = l_oN;
    for (Channel l_eC : g_arrAllChannels) {
        const bool l_bOk = f_Visit(l_oWork, l_eC, [&](auto& l_oKeys) {
            std::vector<bool> l_vMoved(l_oKeys.size(), false);
            for (size_t l_nR = 0; l_nR < l_vFrom.size(); l_nR++) {
                const int l_iI = f_FindKey(l_oKeys, l_vFrom[l_nR]);
                if (l_iI >= 0 && !l_vMoved[(size_t)l_iI]) {
                    l_oKeys[(size_t)l_iI].m_fT = std::max(0.0f, l_vTo[l_nR]);
                    l_vMoved[(size_t)l_iI] = true;
                }
            }
            for (size_t l_nI = 0; l_nI < l_oKeys.size(); l_nI++)
                for (size_t l_nJ = l_nI + 1; l_nJ < l_oKeys.size(); l_nJ++)
                    if ((l_vMoved[l_nI] || l_vMoved[l_nJ]) && f_SameTime(l_oKeys[l_nI].m_fT, l_oKeys[l_nJ].m_fT)) {
                        if (l_pConflict) *l_pConflict = std::string(f_ChannelName(l_eC)) + " already has a key at " + f_Num(l_oKeys[l_nI].m_fT);
                        return false;
                    }
            f_SortKeys(l_oKeys);
            return true;
        });
        if (!l_bOk) return false;
    }
    l_oN = std::move(l_oWork);
    return true;
}

void f_ScaleKeyTimes(Node& l_oN, float l_fFactor, bool l_bRecursive) {
    for (Channel l_eC : g_arrAllChannels)
        f_Visit(l_oN, l_eC, [&](auto& l_oKeys) {
            for (auto& l_oK : l_oKeys) l_oK.m_fT *= l_fFactor;
            f_SortKeys(l_oKeys);
            return 0;
        });
    if (l_bRecursive)
        for (Node& l_oCh : l_oN.m_vChildren) f_ScaleKeyTimes(l_oCh, l_fFactor, true);
}

std::string f_FormatValue(Channel l_eC, const Value& l_oV) {
    switch (l_eC) {
    case Channel::Pos:
    case Channel::Rot:
    case Channel::Scale: return f_Num(l_oV.m_v4V.x) + ", " + f_Num(l_oV.m_v4V.y) + ", " + f_Num(l_oV.m_v4V.z);
    case Channel::Color:
    case Channel::Fx: return f_Num(l_oV.m_v4V.x) + ", " + f_Num(l_oV.m_v4V.y) + ", " + f_Num(l_oV.m_v4V.z) + ", " + f_Num(l_oV.m_v4V.w);
    case Channel::Trail: return f_Num(l_oV.m_v4V.x);
    case Channel::Sprite: return l_oV.m_strS.empty() ? std::string("(none)") : l_oV.m_strS;
    case Channel::Visible: return l_oV.m_bB ? "true" : "false";
    case Channel::Deform: {   // "5: 0, -2; 6: 0, -2"
        if (l_oV.m_mapD.empty()) return "(none)";
        std::string l_strS;
        for (const auto& [l_iI, l_v2Off] : l_oV.m_mapD) l_strS += (l_strS.empty() ? "" : "; ") + std::to_string(l_iI) + ": " + f_Num(l_v2Off.x) + ", " + f_Num(l_v2Off.y);
        return l_strS;
    }
    case Channel::Event: break;
    }
    return l_oV.m_strS;
}

bool f_ParseValue(Channel l_eC, const std::string& l_strText, Value& l_oOut, std::string* l_pErr) {
    const std::string l_strT = f_Trim(l_strText);
    l_oOut = Value();
    std::vector<float> l_vF;
    auto l_fnFail = [&](const std::string& l_strM) {
        if (l_pErr) *l_pErr = l_strM;
        return false;
    };
    switch (l_eC) {
    case Channel::Pos:
        if (!f_ParseNumbers(l_strT, l_vF) || (l_vF.size() != 2 && l_vF.size() != 3)) return l_fnFail("expected x, y[, z]");
        l_oOut.m_v4V = {l_vF[0], l_vF[1], l_vF.size() == 3 ? l_vF[2] : 0.0f, 0};
        return true;
    case Channel::Scale:
        if (!f_ParseNumbers(l_strT, l_vF) || l_vF.empty() || l_vF.size() > 3) return l_fnFail("expected x, y[, z] (one number = uniform)");
        if (l_vF.size() == 1) l_oOut.m_v4V = {l_vF[0], l_vF[0], l_vF[0], 0};   // uniform
        else l_oOut.m_v4V = {l_vF[0], l_vF[1], l_vF.size() == 3 ? l_vF[2] : 1.0f, 0};
        return true;
    case Channel::Rot:
        if (!f_ParseNumbers(l_strT, l_vF) || (l_vF.size() != 1 && l_vF.size() != 3)) return l_fnFail("expected x, y, z degrees (one number = z)");
        if (l_vF.size() == 1) l_oOut.m_v4V = {0, 0, l_vF[0], 0};   // the screen-plane turn
        else l_oOut.m_v4V = {l_vF[0], l_vF[1], l_vF[2], 0};
        return true;
    case Channel::Color:
        if (!f_ParseNumbers(l_strT, l_vF) || (l_vF.size() != 3 && l_vF.size() != 4)) return l_fnFail("expected r, g, b[, a] (0..1)");
        l_oOut.m_v4V = {l_vF[0], l_vF[1], l_vF[2], l_vF.size() == 4 ? l_vF[3] : 1.0f};
        return true;
    case Channel::Fx:
        if (!f_ParseNumbers(l_strT, l_vF) || l_vF.empty() || l_vF.size() > 4) return l_fnFail("expected up to four numbers (the effect's params)");
        l_vF.resize(4, 0.0f);
        l_oOut.m_v4V = {l_vF[0], l_vF[1], l_vF[2], l_vF[3]};
        return true;
    case Channel::Trail:
        if (!f_ParseNumbers(l_strT, l_vF) || l_vF.size() != 1) return l_fnFail("expected one number, 0..1 (the afterimages' strength)");
        l_oOut.m_v4V = {std::clamp(l_vF[0], 0.0f, 1.0f), 0, 0, 0};
        return true;
    case Channel::Sprite:
        l_oOut.m_strS = l_strT == "(none)" ? std::string() : l_strT;
        return true;
    case Channel::Visible:
        if (l_strT == "true" || l_strT == "1" || l_strT == "yes" || l_strT == "on") l_oOut.m_bB = true;
        else if (l_strT == "false" || l_strT == "0" || l_strT == "no" || l_strT == "off") l_oOut.m_bB = false;
        else return l_fnFail("expected true or false");
        return true;
    case Channel::Deform: {   // "point: dx, dy; ..." ; "(none)" or nothing = every point in place
        if (l_strT.empty() || l_strT == "(none)") return true;
        std::istringstream l_oParts(l_strT);
        std::string l_strPart;
        while (std::getline(l_oParts, l_strPart, ';')) {
            l_strPart = f_Trim(l_strPart);
            if (l_strPart.empty()) continue;
            const size_t l_nColon = l_strPart.find(':');
            char* l_pszEnd = nullptr;
            const long l_iI = l_nColon == std::string::npos ? -1 : std::strtol(l_strPart.c_str(), &l_pszEnd, 10);
            if (l_nColon == std::string::npos || !l_pszEnd || f_Trim(std::string(static_cast<const char*>(l_pszEnd), l_strPart.c_str() + l_nColon)).size() || l_iI < 0 ||
                !f_ParseNumbers(l_strPart.substr(l_nColon + 1), l_vF) || l_vF.size() != 2)
                return l_fnFail("expected point: dx, dy; ... (e.g. 5: 0, -2; 6: 0, -2)");
            l_oOut.m_mapD[(int)l_iI] = {l_vF[0], l_vF[1]};
        }
        return true;
    }
    case Channel::Event: break;
    }
    if (l_strT.empty()) return l_fnFail("an event needs a name");
    l_oOut.m_strS = l_strT;
    return true;
}

std::string f_FormatEase(const Ease& l_oE) {
    if (l_oE.m_iKind == Ease::m_iBezier)
        return "bezier(" + f_Num(l_oE.m_arrBezier[0]) + ", " + f_Num(l_oE.m_arrBezier[1]) + ", " + f_Num(l_oE.m_arrBezier[2]) + ", " + f_Num(l_oE.m_arrBezier[3]) + ")";
    return toms::anim::f_EaseName(l_oE);
}

bool f_ParseEaseText(const std::string& l_strText, Ease& l_oOut) {
    const std::string l_strT = f_Trim(l_strText);
    if (toms::anim::f_ParseEase(l_strT, l_oOut)) return true;
    std::string l_strBody = l_strT;
    if (l_strBody.rfind("bezier", 0) == 0) l_strBody = l_strBody.substr(6);
    std::vector<float> l_vF;
    if (f_ParseNumbers(l_strBody, l_vF) && l_vF.size() == 4) {
        l_oOut.m_iKind = Ease::m_iBezier;
        for (int l_iI = 0; l_iI < 4; l_iI++) l_oOut.m_arrBezier[l_iI] = l_vF[(size_t)l_iI];
        l_oOut.m_arrBezier[0] = std::min(1.0f, std::max(0.0f, l_oOut.m_arrBezier[0]));   // x must stay in 0..1
        l_oOut.m_arrBezier[2] = std::min(1.0f, std::max(0.0f, l_oOut.m_arrBezier[2]));
        return true;
    }
    return false;
}

const Node* f_NodeAt(const Node& l_oRoot, const std::vector<int>& l_vPath) {
    const Node* l_pN = &l_oRoot;
    for (int l_iI : l_vPath) {
        if (l_iI < 0 || l_iI >= (int)l_pN->m_vChildren.size()) return nullptr;
        l_pN = &l_pN->m_vChildren[(size_t)l_iI];
    }
    return l_pN;
}

Node* f_NodeAt(Node& l_oRoot, const std::vector<int>& l_vPath) {
    return const_cast<Node*>(f_NodeAt(static_cast<const Node&>(l_oRoot), l_vPath));
}

bool f_FindPath(const Node& l_oRoot, const Node* l_pTarget, std::vector<int>& l_vPath) {
    if (&l_oRoot == l_pTarget) return true;
    for (size_t l_nI = 0; l_nI < l_oRoot.m_vChildren.size(); l_nI++) {
        l_vPath.push_back((int)l_nI);
        if (f_FindPath(l_oRoot.m_vChildren[l_nI], l_pTarget, l_vPath)) return true;
        l_vPath.pop_back();
    }
    return false;
}

void f_CollectNames(const Node& l_oRoot, std::vector<std::string>& l_vOut) {
    l_vOut.push_back(l_oRoot.m_strName);
    for (const Node& l_oC : l_oRoot.m_vChildren) f_CollectNames(l_oC, l_vOut);
}

std::string f_UniqueNodeName(const Node& l_oRoot, const std::string& l_strBase) {
    std::vector<std::string> l_vNames;
    f_CollectNames(l_oRoot, l_vNames);
    const std::set<std::string> l_setTaken(l_vNames.begin(), l_vNames.end());
    const std::string l_strB = l_strBase.empty() ? std::string("node") : l_strBase;
    if (!l_setTaken.count(l_strB)) return l_strB;
    for (int l_iI = 2;; l_iI++) {
        const std::string l_strS = l_strB + "_" + std::to_string(l_iI);
        if (!l_setTaken.count(l_strS)) return l_strS;
    }
}

std::string f_NodeClipboardText(const Node& l_oN) {
    // The node's own JSON, wrapped so a paste can tell it from any other text.
    std::string l_strNode = toms::anim::f_NodeToJsonText(l_oN);
    while (!l_strNode.empty() && (l_strNode.back() == '\n' || l_strNode.back() == ' ')) l_strNode.pop_back();
    return "{\"toms\": \"anim-node\", \"version\": 3, \"node\": " + l_strNode + "}\n";
}

bool f_NodeFromClipboardText(const std::string& l_strText, Node& l_oOut, std::string* l_pErr) {
    try {
        const nlohmann::json l_jJ = nlohmann::json::parse(l_strText);
        if (!l_jJ.is_object() || l_jJ.value("toms", std::string()) != "anim-node" || !l_jJ.contains("node"))
            throw std::runtime_error("the clipboard holds no copied node");
        return toms::anim::f_ParseNodeJson(l_jJ["node"].dump(), l_oOut, l_pErr);
    } catch (const std::exception& l_oE) {
        if (l_pErr) *l_pErr = dynamic_cast<const nlohmann::json::exception*>(&l_oE) ? "the clipboard holds no copied node" : l_oE.what();
        return false;
    }
}

namespace {
void f_RenameWalk(Node& l_oN, std::set<std::string>& l_setTaken, std::map<std::string, std::string>& l_mapRenamed, int& l_iCount) {
    const std::string l_strBase = l_oN.m_strName.empty() ? std::string("node") : l_oN.m_strName;
    std::string l_strName = l_strBase;
    for (int l_iI = 2; l_setTaken.count(l_strName); l_iI++) l_strName = l_strBase + "_" + std::to_string(l_iI);
    if (l_strName != l_oN.m_strName) {
        if (!l_mapRenamed.count(l_oN.m_strName)) l_mapRenamed[l_oN.m_strName] = l_strName;   // a skin names the first node with a name
        l_oN.m_strName = l_strName;
        l_iCount++;
    }
    l_setTaken.insert(l_strName);
    for (Node& l_oC : l_oN.m_vChildren) f_RenameWalk(l_oC, l_setTaken, l_mapRenamed, l_iCount);
}
void f_RemapSkins(Node& l_oN, const std::map<std::string, std::string>& l_mapRenamed) {
    for (std::string& l_strB : l_oN.m_vSkinBones) {
        auto l_itIt = l_mapRenamed.find(l_strB);
        if (l_itIt != l_mapRenamed.end()) l_strB = l_itIt->second;
    }
    for (Node& l_oC : l_oN.m_vChildren) f_RemapSkins(l_oC, l_mapRenamed);
}
}  // namespace

int f_RenameForPaste(const Node& l_oRoot, Node& l_oPasted) {
    std::vector<std::string> l_vNames;
    f_CollectNames(l_oRoot, l_vNames);
    std::set<std::string> l_setTaken(l_vNames.begin(), l_vNames.end());
    std::map<std::string, std::string> l_mapRenamed;
    int l_iCount = 0;
    f_RenameWalk(l_oPasted, l_setTaken, l_mapRenamed, l_iCount);
    f_RemapSkins(l_oPasted, l_mapRenamed);
    return l_iCount;
}

namespace {
const Node* f_FirstNamed(const Node& l_oN, const std::string& l_strName) {   // depth first, as the skin's lookup
    if (l_oN.m_strName == l_strName) return &l_oN;
    for (const Node& l_oC : l_oN.m_vChildren)
        if (const Node* l_pF = f_FirstNamed(l_oC, l_strName)) return l_pF;
    return nullptr;
}
bool f_Inside(const Node& l_oTop, const Node* l_pN) {
    if (&l_oTop == l_pN) return true;
    for (const Node& l_oC : l_oTop.m_vChildren) if (f_Inside(l_oC, l_pN)) return true;
    return false;
}
}  // namespace

std::vector<std::string> f_BonesOutside(const Node& l_oRoot, const Node& l_oSkinned) {
    std::vector<std::string> l_vOut;
    for (const std::string& l_strB : l_oSkinned.m_vSkinBones)
        if (const Node* l_pBn = f_FirstNamed(l_oRoot, l_strB))
            if (!f_Inside(l_oSkinned, l_pBn)) l_vOut.push_back(l_strB);
    return l_vOut;
}

void f_CollectSpriteRefs(const Node& l_oRoot, std::vector<std::string>& l_vOut) {
    if (!l_oRoot.m_strSprite.empty()) l_vOut.push_back(l_oRoot.m_strSprite);
    for (const auto& l_oK : l_oRoot.m_vSpriteKeys)
        if (!l_oK.m_oV.empty()) l_vOut.push_back(l_oK.m_oV);
    for (const Node& l_oC : l_oRoot.m_vChildren) f_CollectSpriteRefs(l_oC, l_vOut);
}

int f_RewriteSpriteRefs(Node& l_oRoot, const std::function<std::string(const std::string&)>& l_fnF) {
    int l_iChanged = 0;
    auto l_fnOne = [&](std::string& l_strS) {
        if (l_strS.empty()) return;
        std::string l_strR = l_fnF(l_strS);
        if (l_strR != l_strS) {
            l_strS = std::move(l_strR);
            l_iChanged++;
        }
    };
    l_fnOne(l_oRoot.m_strSprite);
    for (auto& l_oK : l_oRoot.m_vSpriteKeys) l_fnOne(l_oK.m_oV);
    for (Node& l_oC : l_oRoot.m_vChildren) l_iChanged += f_RewriteSpriteRefs(l_oC, l_fnF);
    return l_iChanged;
}

}  // namespace animed
