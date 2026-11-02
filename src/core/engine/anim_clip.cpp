// anim_clip.cpp -- see anim_clip.h.
#include "anim_clip.h"
#include "sprite_mesh.h"

#include <tweeny/easing.h>
#include <json.hpp>

#include <algorithm>
#include <cmath>

using nlohmann::json;

namespace toms::anim {

// ---- easing ------------------------------------------------------------------------------------

namespace {

using EaseFn = float (*)(float);
// In the order of tweeny::easing::enumerated, so Ease::kind indexes both tables.
#define TOMS_EASE(name) [](float l_fP) { return tweeny::easing::name.run(l_fP, 0.0f, 1.0f); }
const EaseFn g_arrEaseFns[] = {
    TOMS_EASE(linear),            // def: Tweeny's default is linear for floats
    TOMS_EASE(linear),
    [](float) { return 0.0f; },   // stepped: hold the key's value until the next key
    TOMS_EASE(quadraticIn),  TOMS_EASE(quadraticOut),  TOMS_EASE(quadraticInOut),
    TOMS_EASE(cubicIn),      TOMS_EASE(cubicOut),      TOMS_EASE(cubicInOut),
    TOMS_EASE(quarticIn),    TOMS_EASE(quarticOut),    TOMS_EASE(quarticInOut),
    TOMS_EASE(quinticIn),    TOMS_EASE(quinticOut),    TOMS_EASE(quinticInOut),
    TOMS_EASE(sinusoidalIn), TOMS_EASE(sinusoidalOut), TOMS_EASE(sinusoidalInOut),
    TOMS_EASE(exponentialIn), TOMS_EASE(exponentialOut), TOMS_EASE(exponentialInOut),
    TOMS_EASE(circularIn),   TOMS_EASE(circularOut),   TOMS_EASE(circularInOut),
    TOMS_EASE(bounceIn),     TOMS_EASE(bounceOut),     TOMS_EASE(bounceInOut),
    TOMS_EASE(elasticIn),    TOMS_EASE(elasticOut),    TOMS_EASE(elasticInOut),
    TOMS_EASE(backIn),       TOMS_EASE(backOut),       TOMS_EASE(backInOut),
};
#undef TOMS_EASE
constexpr int g_iEaseCount = (int)(sizeof(g_arrEaseFns) / sizeof(g_arrEaseFns[0]));
static_assert(g_iEaseCount == (int)tweeny::easing::enumerated::backInOut + 1, "easing table out of step with Tweeny");

// cubic-bezier(x1, y1, x2, y2) with P0 = (0,0), P3 = (1,1): find s with x(s) = p, return y(s).
float f_BezierEase(const float l_arrB[4], float l_fP) {
    auto l_fnCoord = [](float l_fA1, float l_fA2, float l_fS) {   // one coordinate of the curve at s
        const float l_fU = 1 - l_fS;
        return 3 * l_fU * l_fU * l_fS * l_fA1 + 3 * l_fU * l_fS * l_fS * l_fA2 + l_fS * l_fS * l_fS;
    };
    float l_fLo = 0, l_fHi = 1, l_fS = l_fP;
    for (int l_iI = 0; l_iI < 8; l_iI++) {   // Newton, falling back to bisection when it stalls or leaves [0,1]
        const float l_fX = l_fnCoord(l_arrB[0], l_arrB[2], l_fS) - l_fP;
        if (std::fabs(l_fX) < 1e-6f) return l_fnCoord(l_arrB[1], l_arrB[3], l_fS);
        const float l_fU = 1 - l_fS;
        const float l_fDx = 3 * l_fU * l_fU * l_arrB[0] + 6 * l_fU * l_fS * (l_arrB[2] - l_arrB[0]) + 3 * l_fS * l_fS * (1 - l_arrB[2]);
        if (std::fabs(l_fDx) < 1e-6f) break;
        const float l_fNext = l_fS - l_fX / l_fDx;
        if (l_fNext < 0 || l_fNext > 1) break;
        l_fS = l_fNext;
    }
    for (int l_iI = 0; l_iI < 40; l_iI++) {
        l_fS = (l_fLo + l_fHi) / 2;
        if (l_fnCoord(l_arrB[0], l_arrB[2], l_fS) < l_fP) l_fLo = l_fS; else l_fHi = l_fS;
    }
    return l_fnCoord(l_arrB[1], l_arrB[3], (l_fLo + l_fHi) / 2);
}

}  // namespace

const std::vector<std::string>& f_EaseNames() {
    static const std::vector<std::string> l_vNames = {
        "def", "linear", "stepped",
        "quadraticIn", "quadraticOut", "quadraticInOut", "cubicIn", "cubicOut", "cubicInOut",
        "quarticIn", "quarticOut", "quarticInOut", "quinticIn", "quinticOut", "quinticInOut",
        "sinusoidalIn", "sinusoidalOut", "sinusoidalInOut", "exponentialIn", "exponentialOut", "exponentialInOut",
        "circularIn", "circularOut", "circularInOut", "bounceIn", "bounceOut", "bounceInOut",
        "elasticIn", "elasticOut", "elasticInOut", "backIn", "backOut", "backInOut"};
    return l_vNames;
}

bool Ease::operator==(const Ease& l_oO) const {
    if (m_iKind != l_oO.m_iKind) return false;
    return m_iKind != m_iBezier || std::equal(m_arrBezier, m_arrBezier + 4, l_oO.m_arrBezier);
}

float Ease::f_Apply(float l_fP) const {
    l_fP = std::min(1.0f, std::max(0.0f, l_fP));
    if (m_iKind == m_iBezier) return f_BezierEase(m_arrBezier, l_fP);
    if (m_iKind < 0 || m_iKind >= g_iEaseCount) return l_fP;
    return g_arrEaseFns[m_iKind](l_fP);
}

bool f_ParseEase(const std::string& l_strName, Ease& l_oOut) {
    const auto& l_vN = f_EaseNames();
    for (size_t l_nI = 0; l_nI < l_vN.size(); l_nI++)
        if (l_vN[l_nI] == l_strName) { l_oOut.m_iKind = (int)l_nI; return true; }
    return false;
}

std::string f_EaseName(const Ease& l_oE) {
    if (l_oE.m_iKind == Ease::m_iBezier || l_oE.m_iKind < 0 || l_oE.m_iKind >= (int)f_EaseNames().size()) return std::string();
    return f_EaseNames()[(size_t)l_oE.m_iKind];
}

// ---- sampling ----------------------------------------------------------------------------------

namespace {

template <class T> T f_LerpValue(const T& l_oA, const T& l_oB, float l_fF) { return l_oA + (l_oB - l_oA) * l_fF; }

// Interpolated channel: the key segment around t, eased by the key that starts it.
template <class T> T f_Sample(const std::vector<Key<T>>& l_vKeys, float l_fT, const T& l_oRest) {
    if (l_vKeys.empty()) return l_oRest;
    if (l_fT <= l_vKeys.front().m_fT) return l_vKeys.front().m_oV;
    if (l_fT >= l_vKeys.back().m_fT) return l_vKeys.back().m_oV;
    auto l_oIt = std::upper_bound(l_vKeys.begin(), l_vKeys.end(), l_fT, [](float l_fX, const Key<T>& l_oK) { return l_fX < l_oK.m_fT; });
    const Key<T>& l_oB = *l_oIt;
    const Key<T>& l_oA = *(l_oIt - 1);
    const float l_fSpan = l_oB.m_fT - l_oA.m_fT;
    if (l_fSpan <= 0 || l_oA.m_oEase.f_IsStepped()) return l_oA.m_oV;
    return f_LerpValue(l_oA.m_oV, l_oB.m_oV, l_oA.m_oEase.f_Apply((l_fT - l_oA.m_fT) / l_fSpan));
}

// Stepped channel: the last key at or before t (the first key before it starts).
template <class T> T f_SampleStep(const std::vector<Key<T>>& l_vKeys, float l_fT, const T& l_oRest) {
    if (l_vKeys.empty()) return l_oRest;
    auto l_oIt = std::upper_bound(l_vKeys.begin(), l_vKeys.end(), l_fT, [](float l_fX, const Key<T>& l_oK) { return l_fX < l_oK.m_fT; });
    return l_oIt == l_vKeys.begin() ? l_vKeys.front().m_oV : (l_oIt - 1)->m_oV;
}

template <class T> float f_LastTime(const std::vector<Key<T>>& l_vKeys) { return l_vKeys.empty() ? 0.0f : l_vKeys.back().m_fT; }
template <class T> void f_FirstTime(const std::vector<Key<T>>& l_vKeys, float& l_fM, bool& l_bAny) {
    if (l_vKeys.empty()) return;
    l_fM = l_bAny ? std::min(l_fM, l_vKeys.front().m_fT) : l_vKeys.front().m_fT;
    l_bAny = true;
}
void f_FirstKey(const Node& l_oN, float& l_fM, bool& l_bAny) {
    f_FirstTime(l_oN.m_vPosKeys, l_fM, l_bAny);
    f_FirstTime(l_oN.m_vScaleKeys, l_fM, l_bAny);
    f_FirstTime(l_oN.m_vRotKeys, l_fM, l_bAny);
    f_FirstTime(l_oN.m_vColorKeys, l_fM, l_bAny);
    f_FirstTime(l_oN.m_vSpriteKeys, l_fM, l_bAny);
    f_FirstTime(l_oN.m_vEventKeys, l_fM, l_bAny);
    f_FirstTime(l_oN.m_vVisibleKeys, l_fM, l_bAny);
    f_FirstTime(l_oN.m_vDeformKeys, l_fM, l_bAny);
    f_FirstTime(l_oN.m_vFxKeys, l_fM, l_bAny);
    f_FirstTime(l_oN.m_vTrailKeys, l_fM, l_bAny);
    for (const Node& l_oC : l_oN.m_vChildren) f_FirstKey(l_oC, l_fM, l_bAny);
}

}  // namespace

glm::vec3 f_PositionAt(const Node& l_oN, float l_fT) { return f_Sample(l_oN.m_vPosKeys, l_fT, l_oN.m_v3Pos); }
glm::vec3 f_RotationAt(const Node& l_oN, float l_fT) { return f_Sample(l_oN.m_vRotKeys, l_fT, l_oN.m_v3Rot); }
glm::vec3 f_ScaleAt(const Node& l_oN, float l_fT) { return f_Sample(l_oN.m_vScaleKeys, l_fT, l_oN.m_v3Scale); }
glm::vec4 f_ColorAt(const Node& l_oN, float l_fT) { return glm::clamp(f_Sample(l_oN.m_vColorKeys, l_fT, l_oN.m_v4Color), 0.0f, 1.0f); }
std::string f_SpriteAt(const Node& l_oN, float l_fT) { return f_SampleStep(l_oN.m_vSpriteKeys, l_fT, l_oN.m_strSprite); }
bool f_VisibleAt(const Node& l_oN, float l_fT) { return f_SampleStep(l_oN.m_vVisibleKeys, l_fT, l_oN.m_bVisible); }
glm::vec4 f_FxParamsAt(const Node& l_oN, float l_fT) { return f_Sample(l_oN.m_vFxKeys, l_fT, l_oN.m_oFx.m_v4Params); }
float f_TrailAt(const Node& l_oN, float l_fT) { return std::clamp(f_Sample(l_oN.m_vTrailKeys, l_fT, 1.0f), 0.0f, 1.0f); }

namespace {
bool f_AnyEffect(const Node& l_oN) {
    if (l_oN.f_HasFx() || l_oN.f_HasTrail()) return true;
    for (const Node& l_oC : l_oN.m_vChildren) if (f_AnyEffect(l_oC)) return true;
    return false;
}
}  // namespace
bool f_HasEffects(const Clip& l_oC) { return f_AnyEffect(l_oC.m_oRoot); }

const char* f_FxShaderName(FxShader l_eS) {
    switch (l_eS) {
    case FxShader::Group: return "group";
    case FxShader::MotionBlur: return "motionBlur";
    case FxShader::Glow: return "glow";
    case FxShader::Flash: return "flash";
    case FxShader::Outline: return "outline";
    case FxShader::None: break;
    }
    return "";
}

bool f_ParseFxShader(const std::string& l_strName, FxShader& l_eOut) {
    for (FxShader l_eS : {FxShader::None, FxShader::Group, FxShader::MotionBlur, FxShader::Glow, FxShader::Flash, FxShader::Outline})
        if (l_strName == f_FxShaderName(l_eS)) { l_eOut = l_eS; return true; }
    return false;
}

float Node::f_LastKeyTime() const {
    float l_fM = std::max({f_LastTime(m_vPosKeys), f_LastTime(m_vScaleKeys), f_LastTime(m_vRotKeys), f_LastTime(m_vColorKeys),
                        f_LastTime(m_vSpriteKeys), f_LastTime(m_vEventKeys), f_LastTime(m_vVisibleKeys), f_LastTime(m_vDeformKeys),
                        f_LastTime(m_vFxKeys), f_LastTime(m_vTrailKeys)});
    for (const Node& l_oC : m_vChildren) l_fM = std::max(l_fM, l_oC.f_LastKeyTime());
    return l_fM;
}

float Node::f_FirstKeyTime() const {
    float l_fM = 0;
    bool l_bAny = false;
    f_FirstKey(*this, l_fM, l_bAny);
    return l_fM;
}

bool Node::f_HasKeys() const {
    float l_fM = 0;
    bool l_bAny = false;
    f_FirstKey(*this, l_fM, l_bAny);
    return l_bAny;
}

bool Node::operator==(const Node& l_oO) const {
    return m_strName == l_oO.m_strName && m_strSprite == l_oO.m_strSprite && m_v3Pos == l_oO.m_v3Pos && m_v3Rot == l_oO.m_v3Rot && m_v3Scale == l_oO.m_v3Scale && m_v4Color == l_oO.m_v4Color &&
           m_bVisible == l_oO.m_bVisible && m_bHasPivot == l_oO.m_bHasPivot && m_v2Pivot == l_oO.m_v2Pivot && m_iOrder == l_oO.m_iOrder && m_eBlend == l_oO.m_eBlend &&
           m_bInheritColor == l_oO.m_bInheritColor && m_bLoop == l_oO.m_bLoop && m_bStayAtLastFrame == l_oO.m_bStayAtLastFrame &&
           m_vSkinBones == l_oO.m_vSkinBones && m_vSkinWeights == l_oO.m_vSkinWeights && m_arrLattice[0] == l_oO.m_arrLattice[0] &&
           m_arrLattice[1] == l_oO.m_arrLattice[1] && m_vDeformKeys == l_oO.m_vDeformKeys && m_arrMeshGrid[0] == l_oO.m_arrMeshGrid[0] &&
           m_arrMeshGrid[1] == l_oO.m_arrMeshGrid[1] && m_oFx == l_oO.m_oFx && m_vFxKeys == l_oO.m_vFxKeys && m_oTrail == l_oO.m_oTrail && m_vTrailKeys == l_oO.m_vTrailKeys &&
           m_vPosKeys == l_oO.m_vPosKeys && m_vRotKeys == l_oO.m_vRotKeys && m_vScaleKeys == l_oO.m_vScaleKeys && m_vColorKeys == l_oO.m_vColorKeys &&
           m_vSpriteKeys == l_oO.m_vSpriteKeys && m_vEventKeys == l_oO.m_vEventKeys && m_vVisibleKeys == l_oO.m_vVisibleKeys && m_vChildren == l_oO.m_vChildren;
}

float Clip::f_Duration() const { return m_fLength > 0 ? m_fLength : m_oRoot.f_LastKeyTime(); }

float f_NodeTime(const Node& l_oN, float l_fT, float l_fFirst, float l_fLast, bool* l_pEnded) {
    if (l_pEnded) *l_pEnded = false;
    if (!(l_fT > l_fLast)) return l_fT;
    if (l_oN.m_bLoop) {
        const float l_fLen = l_fLast - l_fFirst;
        if (l_fLen <= 1e-6f) return l_fT;   // keys at one time only: nothing to repeat
        return l_fFirst + std::fmod(l_fT - l_fFirst, l_fLen);
    }
    if (!l_oN.m_bStayAtLastFrame && l_pEnded) *l_pEnded = true;
    return l_fT;
}

namespace {
bool f_AnyLoop(const Node& l_oN) {
    if (l_oN.m_bLoop) return true;
    for (const Node& l_oC : l_oN.m_vChildren)
        if (f_AnyLoop(l_oC)) return true;
    return false;
}
}  // namespace

bool f_HasLoopingNodes(const Clip& l_oC) { return f_AnyLoop(l_oC.m_oRoot); }

std::string f_DefaultAtlasId(const std::string& l_strPath) {
    const size_t l_nSlash = l_strPath.find_last_of("/\\");
    std::string l_strName = l_nSlash == std::string::npos ? l_strPath : l_strPath.substr(l_nSlash + 1);
    if (l_strName.size() > 6 && l_strName.compare(l_strName.size() - 6, 6, ".atlas") == 0) l_strName.resize(l_strName.size() - 6);
    return l_strName;
}

SpriteRef f_ParseSpriteRef(const std::string& l_strRef) {
    const size_t l_nColon = l_strRef.find(':');
    if (l_nColon == std::string::npos) return {std::string(), l_strRef};
    return {l_strRef.substr(0, l_nColon), l_strRef.substr(l_nColon + 1)};
}

std::string f_SpriteRef(const std::string& l_strAtlasId, const std::string& l_strName) {
    return l_strAtlasId.empty() ? l_strName : l_strAtlasId + ":" + l_strName;
}

const Clip* AnimFile::f_Find(const std::string& l_strN) const {
    for (const Clip& l_oC : m_vClips)
        if (l_oC.m_strName == l_strN) return &l_oC;
    return nullptr;
}

// ---- evaluation --------------------------------------------------------------------------------

glm::mat4 f_Compose(const glm::vec3& l_v3P, const glm::vec3& l_v3RotDegrees, const glm::vec3& l_v3S) {
    // Column-major: T * Rx * Ry * Rz * S (z applied first). With y down, a positive z angle turns
    // clockwise on screen.
    glm::mat3 l_mat3R(1.0f);
    if (l_v3RotDegrees.z != 0) {
        const float l_fA = glm::radians(l_v3RotDegrees.z), l_fC = std::cos(l_fA), l_fSn = std::sin(l_fA);
        l_mat3R = glm::mat3(l_fC, l_fSn, 0, -l_fSn, l_fC, 0, 0, 0, 1);
    }
    if (l_v3RotDegrees.y != 0) {
        const float l_fA = glm::radians(l_v3RotDegrees.y), l_fC = std::cos(l_fA), l_fSn = std::sin(l_fA);
        l_mat3R = glm::mat3(l_fC, 0, -l_fSn, 0, 1, 0, l_fSn, 0, l_fC) * l_mat3R;
    }
    if (l_v3RotDegrees.x != 0) {
        const float l_fA = glm::radians(l_v3RotDegrees.x), l_fC = std::cos(l_fA), l_fSn = std::sin(l_fA);
        l_mat3R = glm::mat3(1, 0, 0, 0, l_fC, l_fSn, 0, -l_fSn, l_fC) * l_mat3R;
    }
    glm::mat4 l_mat4M(1.0f);
    l_mat4M[0] = glm::vec4(l_mat3R[0] * l_v3S.x, 0);
    l_mat4M[1] = glm::vec4(l_mat3R[1] * l_v3S.y, 0);
    l_mat4M[2] = glm::vec4(l_mat3R[2] * l_v3S.z, 0);
    l_mat4M[3] = glm::vec4(l_v3P, 1);
    return l_mat4M;
}

glm::mat3 f_Flatten(const glm::mat4& l_mat4M) {
    glm::mat3 l_mat3F(1.0f);
    l_mat3F[0] = glm::vec3(l_mat4M[0].x, l_mat4M[0].y, 0);
    l_mat3F[1] = glm::vec3(l_mat4M[1].x, l_mat4M[1].y, 0);
    l_mat3F[2] = glm::vec3(l_mat4M[3].x, l_mat4M[3].y, 1);
    return l_mat3F;
}

std::vector<glm::vec2> f_DeformAt(const Node& l_oN, float l_fT) {
    const size_t l_nCount = (size_t)l_oN.f_LatticeCols() * (size_t)l_oN.f_LatticeRows();
    std::vector<glm::vec2> l_vOut(l_nCount, glm::vec2(0.0f));
    const auto& l_vKeys = l_oN.m_vDeformKeys;
    if (l_vKeys.empty()) return l_vOut;
    auto l_fnPut = [&](const Deform& l_mapD, float l_fW) {
        for (const auto& [l_iI, l_v2V] : l_mapD)
            if (l_iI >= 0 && (size_t)l_iI < l_nCount) l_vOut[(size_t)l_iI] += l_fW * l_v2V;
    };
    if (l_fT <= l_vKeys.front().m_fT) { l_fnPut(l_vKeys.front().m_oV, 1.0f); return l_vOut; }
    if (l_fT >= l_vKeys.back().m_fT) { l_fnPut(l_vKeys.back().m_oV, 1.0f); return l_vOut; }
    auto l_itIt = std::upper_bound(l_vKeys.begin(), l_vKeys.end(), l_fT, [](float l_fX, const Key<Deform>& l_oK) { return l_fX < l_oK.m_fT; });
    const Key<Deform>& l_oB = *l_itIt;
    const Key<Deform>& l_oA = *(l_itIt - 1);
    const float l_fSpan = l_oB.m_fT - l_oA.m_fT;
    const float l_fF = l_fSpan <= 0 || l_oA.m_oEase.f_IsStepped() ? 0.0f : l_oA.m_oEase.f_Apply((l_fT - l_oA.m_fT) / l_fSpan);
    l_fnPut(l_oA.m_oV, 1.0f - l_fF);   // a point one key leaves out is at 0 there
    l_fnPut(l_oB.m_oV, l_fF);
    return l_vOut;
}

glm::mat4 f_LocalTransform(const Node& l_oN, float l_fT) { return f_Compose(f_PositionAt(l_oN, l_fT), f_RotationAt(l_oN, l_fT), f_ScaleAt(l_oN, l_fT)); }

namespace {

void f_Walk(const Node& l_oN, float l_fT, const glm::mat4& l_mat4ParentWorld, const glm::vec4& l_v4ParentColor, bool l_bParentVisible,
          int l_iDepth, float l_fClipTime, std::vector<NodePose>& l_vOut) {
    const size_t l_nFirst = l_vOut.size();
    bool l_bEnded = false;
    if (l_oN.f_Timed()) l_fT = f_NodeTime(l_oN, l_fT, l_oN.f_FirstKeyTime(), l_oN.f_LastKeyTime(), &l_bEnded);
    NodePose l_oMe;
    l_oMe.m_pNode = &l_oN;
    l_oMe.m_iDepth = l_iDepth;
    l_oMe.m_mat4World = l_mat4ParentWorld * f_LocalTransform(l_oN, l_fT);
    const glm::vec4 l_v4Own = f_ColorAt(l_oN, l_fT);
    l_oMe.m_v4Color = l_oN.m_bInheritColor ? l_v4ParentColor * l_v4Own : l_v4Own;
    l_oMe.m_bVisible = l_bParentVisible && f_VisibleAt(l_oN, l_fT) && !l_bEnded;
    l_oMe.m_strSprite = f_SpriteAt(l_oN, l_fT);
    l_oMe.m_fTime = l_fT;
    l_oMe.m_fClipTime = l_fClipTime;
    // Children in order (stable: equal orders keep their list order). Most nodes' children all have order 0: in list
    // order, with no sorting (and no allocation -- evaluate runs for every afterimage, every frame).
    const bool l_bOrdered = std::all_of(l_oN.m_vChildren.begin(), l_oN.m_vChildren.end(), [](const Node& l_oC) { return l_oC.m_iOrder == 0; });
    std::vector<const Node*> l_vKids;
    if (!l_bOrdered) {
        for (const Node& l_oC : l_oN.m_vChildren) l_vKids.push_back(&l_oC);
        std::stable_sort(l_vKids.begin(), l_vKids.end(), [](const Node* l_pA, const Node* l_pB) { return l_pA->m_iOrder < l_pB->m_iOrder; });
    }
    const size_t l_nCount = l_oN.m_vChildren.size();
    auto l_fnKid = [&](size_t l_nI) -> const Node& { return l_bOrdered ? l_oN.m_vChildren[l_nI] : *l_vKids[l_nI]; };
    size_t l_nK = 0;
    for (; l_nK < l_nCount && l_fnKid(l_nK).m_iOrder < 0; l_nK++) f_Walk(l_fnKid(l_nK), l_fT, l_oMe.m_mat4World, l_oMe.m_v4Color, l_oMe.m_bVisible, l_iDepth + 1, l_fClipTime, l_vOut);
    const size_t l_nAt = l_vOut.size();
    l_vOut.push_back(l_oMe);
    for (; l_nK < l_nCount; l_nK++) f_Walk(l_fnKid(l_nK), l_fT, l_oMe.m_mat4World, l_oMe.m_v4Color, l_oMe.m_bVisible, l_iDepth + 1, l_fClipTime, l_vOut);
    l_vOut[l_nAt].m_uFirst = (uint32_t)l_nFirst;
    l_vOut[l_nAt].m_uEnd = (uint32_t)l_vOut.size();
}

void f_CollectEvents(const Node& l_oN, float l_fT0, float l_fT1, std::vector<std::pair<float, std::string>>& l_vOut) {
    for (const Key<std::string>& l_oK : l_oN.m_vEventKeys)
        if (l_oK.m_fT > l_fT0 && l_oK.m_fT <= l_fT1) l_vOut.emplace_back(l_oK.m_fT, l_oK.m_oV);
    for (const Node& l_oC : l_oN.m_vChildren) f_CollectEvents(l_oC, l_fT0, l_fT1, l_vOut);
}

}  // namespace

void f_Evaluate(const Clip& l_oC, float l_fT, std::vector<NodePose>& l_vOut) {
    l_vOut.clear();
    f_Walk(l_oC.m_oRoot, l_fT, glm::mat4(1.0f), glm::vec4(1.0f), true, 0, l_fT, l_vOut);
}

// ---- PoseCache ---------------------------------------------------------------------------------

namespace {

// The last key at or before t (-1: before the first), starting from the previous answer: playback
// moves forward a little each frame, so it is nearly always the same key or the next one.
template <class T> int f_Locate(const std::vector<Key<T>>& l_vKeys, float l_fT, int l_iHint) {
    const int l_iN = (int)l_vKeys.size();
    auto l_oFits = [&](int l_iI) { return (l_iI < 0 || l_vKeys[(size_t)l_iI].m_fT <= l_fT) && (l_iI + 1 >= l_iN || l_fT < l_vKeys[(size_t)l_iI + 1].m_fT); };
    if (l_iHint >= -1 && l_iHint < l_iN && l_oFits(l_iHint)) return l_iHint;
    if (l_iHint >= -1 && l_iHint + 1 < l_iN && l_oFits(l_iHint + 1)) return l_iHint + 1;
    return int(std::upper_bound(l_vKeys.begin(), l_vKeys.end(), l_fT, [](float l_fX, const Key<T>& l_oK) { return l_fX < l_oK.m_fT; }) -
               l_vKeys.begin()) - 1;
}

// Interpolated track: the key whose value it holds at t (before the first key, after the last, a
// stepped or empty segment), or -1 while it moves between two keys. Same cases as sample().
template <class T> int f_HeldKey(const std::vector<Key<T>>& l_vKeys, float l_fT, int l_iI) {
    if (l_fT <= l_vKeys.front().m_fT) return 0;
    if (l_fT >= l_vKeys.back().m_fT) return (int)l_vKeys.size() - 1;
    const Key<T>& l_oA = l_vKeys[(size_t)l_iI];
    return l_oA.m_oEase.f_IsStepped() || l_vKeys[(size_t)l_iI + 1].m_fT - l_oA.m_fT <= 0 ? l_iI : -1;
}

// Moves an interpolated track to t. True when its value changed.
template <class T> bool f_Advance(const std::vector<Key<T>>& l_vKeys, float l_fT, int& l_iCursor, int& l_iHeld, T& l_oValue, bool l_bForce) {
    if (l_vKeys.empty()) return false;
    l_iCursor = f_Locate(l_vKeys, l_fT, l_iCursor);
    const int l_iH = f_HeldKey(l_vKeys, l_fT, l_iCursor);
    if (!l_bForce && l_iH >= 0 && l_iH == l_iHeld) return false;   // still holding the same key: nothing to do
    l_iHeld = l_iH;
    T l_oV;
    if (l_iH >= 0) {
        l_oV = l_vKeys[(size_t)l_iH].m_oV;
    } else {
        const Key<T>& l_oA = l_vKeys[(size_t)l_iCursor];
        const Key<T>& l_oB = l_vKeys[(size_t)l_iCursor + 1];
        l_oV = f_LerpValue(l_oA.m_oV, l_oB.m_oV, l_oA.m_oEase.f_Apply((l_fT - l_oA.m_fT) / (l_oB.m_fT - l_oA.m_fT)));
    }
    if (!l_bForce && l_oV == l_oValue) return false;
    l_oValue = l_oV;
    return true;
}

// Moves a stepped track to t. True when its value changed.
template <class T> bool f_AdvanceStep(const std::vector<Key<T>>& l_vKeys, float l_fT, int& l_iCursor, int& l_iHeld, T& l_oValue, bool l_bForce) {
    if (l_vKeys.empty()) return false;
    l_iCursor = f_Locate(l_vKeys, l_fT, l_iCursor);
    const int l_iH = std::max(l_iCursor, 0);
    if (!l_bForce && l_iH == l_iHeld) return false;
    l_iHeld = l_iH;
    if (!l_bForce && l_vKeys[(size_t)l_iH].m_oV == l_oValue) return false;
    l_oValue = l_vKeys[(size_t)l_iH].m_oV;
    return true;
}

}  // namespace

void PoseCache::f_Bind(const Clip* l_pClip) {
    m_pClip = l_pClip;
    m_vItems.clear();
    m_vPoses.clear();
    m_iAnimated = 0;
    m_iChangedCount = 0;
    m_bFresh = true;
    if (l_pClip) f_AddItems(l_pClip->m_oRoot, -1, 0);
    m_vChanged.assign(m_vPoses.size(), 1);
}

int PoseCache::f_AddItems(const Node& l_oN, int l_iParent, int l_iDepth) {
    const int l_iMe = (int)m_vItems.size();
    const size_t l_nFirst = m_vPoses.size();
    Item l_oIt;
    l_oIt.m_pNode = &l_oN;
    l_oIt.m_iParent = l_iParent;
    l_oIt.m_bAnimated = !(l_oN.m_vPosKeys.empty() && l_oN.m_vRotKeys.empty() && l_oN.m_vScaleKeys.empty() && l_oN.m_vColorKeys.empty() &&
                    l_oN.m_vSpriteKeys.empty() && l_oN.m_vVisibleKeys.empty());
    for (int l_iI = 0; l_iI < kTracks; l_iI++) {
        l_oIt.m_arrCursor[l_iI] = -1;
        l_oIt.m_arrHeld[l_iI] = -2;
    }
    l_oIt.m_v3Pos = l_oN.m_v3Pos;
    l_oIt.m_v3Rot = l_oN.m_v3Rot;
    l_oIt.m_v3Scale = l_oN.m_v3Scale;
    l_oIt.m_v4Color = l_oN.m_v4Color;
    l_oIt.m_bVisible = l_oN.m_bVisible;
    l_oIt.m_bTimed = l_oN.f_Timed();
    if (l_oIt.m_bTimed) {
        l_oIt.m_fFirst = l_oN.f_FirstKeyTime();
        l_oIt.m_fLast = l_oN.f_LastKeyTime();
    }
    m_iAnimated += l_oIt.m_bAnimated;
    m_vItems.push_back(l_oIt);   // parents before their children: seek() updates in this order
    // Draw order as walk(): children with order < 0, the node itself, the rest.
    std::vector<const Node*> l_vKids;
    for (const Node& l_oC : l_oN.m_vChildren) l_vKids.push_back(&l_oC);
    std::stable_sort(l_vKids.begin(), l_vKids.end(), [](const Node* l_pA, const Node* l_pB) { return l_pA->m_iOrder < l_pB->m_iOrder; });
    size_t l_nK = 0;
    for (; l_nK < l_vKids.size() && l_vKids[l_nK]->m_iOrder < 0; l_nK++) f_AddItems(*l_vKids[l_nK], l_iMe, l_iDepth + 1);
    m_vItems[(size_t)l_iMe].m_nPose = m_vPoses.size();
    NodePose l_oP;
    l_oP.m_pNode = &l_oN;
    l_oP.m_iDepth = l_iDepth;
    l_oP.m_strSprite = l_oN.m_strSprite;
    m_vPoses.push_back(l_oP);
    for (; l_nK < l_vKids.size(); l_nK++) f_AddItems(*l_vKids[l_nK], l_iMe, l_iDepth + 1);
    NodePose& l_oMine = m_vPoses[m_vItems[(size_t)l_iMe].m_nPose];
    l_oMine.m_uFirst = (uint32_t)l_nFirst;
    l_oMine.m_uEnd = (uint32_t)m_vPoses.size();
    return l_iMe;
}

void PoseCache::f_Seek(float l_fT) {
    m_iChangedCount = 0;
    if (!m_pClip) return;
    const bool l_bAll = m_bFresh;
    m_bFresh = false;
    for (Item& l_oIt : m_vItems) {
        NodePose& l_oP = m_vPoses[l_oIt.m_nPose];
        bool l_bLocal = l_bAll, l_bColour = l_bAll, l_bVis = l_bAll, l_bSprite = false;
        const Item* l_pParent = l_oIt.m_iParent >= 0 ? &m_vItems[(size_t)l_oIt.m_iParent] : nullptr;
        l_oIt.m_fTime = l_pParent ? l_pParent->m_fTime : l_fT;   // a looping ancestor's time passes down
        if (l_oIt.m_bTimed) {
            bool l_bEnded = false;
            l_oIt.m_fTime = f_NodeTime(*l_oIt.m_pNode, l_oIt.m_fTime, l_oIt.m_fFirst, l_oIt.m_fLast, &l_bEnded);
            l_bVis |= l_bEnded != l_oIt.m_bEnded;
            l_oIt.m_bEnded = l_bEnded;
        }
        l_oP.m_fTime = l_oIt.m_fTime;
        l_oP.m_fClipTime = l_fT;
        if (l_oIt.m_bAnimated) {   // only the tracks that have keys; a node without keys skips all of this
            const Node& l_oN = *l_oIt.m_pNode;
            const float l_fNt = l_oIt.m_fTime;
            l_bLocal |= f_Advance(l_oN.m_vPosKeys, l_fNt, l_oIt.m_arrCursor[kPos], l_oIt.m_arrHeld[kPos], l_oIt.m_v3Pos, l_bAll);
            l_bLocal |= f_Advance(l_oN.m_vRotKeys, l_fNt, l_oIt.m_arrCursor[kRot], l_oIt.m_arrHeld[kRot], l_oIt.m_v3Rot, l_bAll);
            l_bLocal |= f_Advance(l_oN.m_vScaleKeys, l_fNt, l_oIt.m_arrCursor[kScale], l_oIt.m_arrHeld[kScale], l_oIt.m_v3Scale, l_bAll);
            l_bColour |= f_Advance(l_oN.m_vColorKeys, l_fNt, l_oIt.m_arrCursor[kColor], l_oIt.m_arrHeld[kColor], l_oIt.m_v4Color, l_bAll);
            l_bVis |= f_AdvanceStep(l_oN.m_vVisibleKeys, l_fNt, l_oIt.m_arrCursor[kVisible], l_oIt.m_arrHeld[kVisible], l_oIt.m_bVisible, l_bAll);
            l_bSprite = f_AdvanceStep(l_oN.m_vSpriteKeys, l_fNt, l_oIt.m_arrCursor[kSprite], l_oIt.m_arrHeld[kSprite], l_oP.m_strSprite, l_bAll);
        }
        const NodePose* l_pPp = l_pParent ? &m_vPoses[l_pParent->m_nPose] : nullptr;
        if (l_bLocal) l_oIt.m_mat4Local = f_Compose(l_oIt.m_v3Pos, l_oIt.m_v3Rot, l_oIt.m_v3Scale);
        l_oIt.m_bWorldChanged = l_bLocal || (l_pParent && l_pParent->m_bWorldChanged);
        if (l_oIt.m_bWorldChanged) l_oP.m_mat4World = (l_pPp ? l_pPp->m_mat4World : glm::mat4(1.0f)) * l_oIt.m_mat4Local;
        l_oIt.m_bColorChanged = l_bColour || (l_pParent && l_pParent->m_bColorChanged && l_oIt.m_pNode->m_bInheritColor);
        if (l_oIt.m_bColorChanged) {
            const glm::vec4 l_v4Own = glm::clamp(l_oIt.m_v4Color, 0.0f, 1.0f);
            l_oP.m_v4Color = l_oIt.m_pNode->m_bInheritColor ? (l_pPp ? l_pPp->m_v4Color : glm::vec4(1.0f)) * l_v4Own : l_v4Own;
        }
        l_oIt.m_bVisibleChanged = l_bVis || (l_pParent && l_pParent->m_bVisibleChanged);
        if (l_oIt.m_bVisibleChanged) l_oP.m_bVisible = (l_pPp ? l_pPp->m_bVisible : true) && l_oIt.m_bVisible && !l_oIt.m_bEnded;
        const bool l_bChanged = l_oIt.m_bWorldChanged || l_oIt.m_bColorChanged || l_oIt.m_bVisibleChanged || l_bSprite;
        m_vChanged[l_oIt.m_nPose] = l_bChanged;
        m_iChangedCount += l_bChanged;
    }
}

void f_EventsBetween(const Clip& l_oC, float l_fT0, float l_fT1, std::vector<std::string>& l_vOut) {
    std::vector<std::pair<float, std::string>> l_vFound;
    f_CollectEvents(l_oC.m_oRoot, l_fT0, l_fT1, l_vFound);
    std::stable_sort(l_vFound.begin(), l_vFound.end(), [](const auto& l_oA, const auto& l_oB) { return l_oA.first < l_oB.first; });
    for (auto& l_pairF : l_vFound) l_vOut.push_back(std::move(l_pairF.second));
}

// ---- JSON --------------------------------------------------------------------------------------

namespace {

json f_Vec(const glm::vec2& l_v2V) { return json::array({l_v2V.x, l_v2V.y}); }
json f_Vec(const glm::vec3& l_v3V) { return json::array({l_v3V.x, l_v3V.y, l_v3V.z}); }
json f_Vec(const glm::vec4& l_v4V) { return json::array({l_v4V.x, l_v4V.y, l_v4V.z, l_v4V.w}); }
json f_EaseToJson(const Ease& l_oE) {
    if (l_oE.m_iKind == Ease::m_iBezier) return json::array({l_oE.m_arrBezier[0], l_oE.m_arrBezier[1], l_oE.m_arrBezier[2], l_oE.m_arrBezier[3]});
    return f_EaseName(l_oE);
}

template <class T, class F> json f_KeysToJson(const std::vector<Key<T>>& l_vKeys, F l_oValue, bool l_bEased) {
    json l_jA = json::array();
    for (const Key<T>& l_oK : l_vKeys) {
        json l_jO{{"t", l_oK.m_fT}, {"v", l_oValue(l_oK.m_oV)}};
        if (l_bEased && !l_oK.m_oEase.f_IsLinear()) l_jO["ease"] = f_EaseToJson(l_oK.m_oEase);
        l_jA.push_back(l_jO);
    }
    return l_jA;
}

json f_NodeToJson(const Node& l_oN) {
    json l_jJ;
    l_jJ["name"] = l_oN.m_strName;
    if (!l_oN.m_strSprite.empty()) l_jJ["sprite"] = l_oN.m_strSprite;
    if (l_oN.m_v3Pos != glm::vec3(0)) l_jJ["pos"] = f_Vec(l_oN.m_v3Pos);
    if (l_oN.m_v3Rot != glm::vec3(0)) l_jJ["rot"] = f_Vec(l_oN.m_v3Rot);
    if (l_oN.m_v3Scale != glm::vec3(1)) l_jJ["scale"] = f_Vec(l_oN.m_v3Scale);
    if (l_oN.m_v4Color != glm::vec4(1)) l_jJ["color"] = f_Vec(l_oN.m_v4Color);
    if (!l_oN.m_bVisible) l_jJ["visible"] = false;
    if (l_oN.m_bHasPivot) l_jJ["pivot"] = f_Vec(l_oN.m_v2Pivot);
    if (l_oN.m_iOrder != 0) l_jJ["order"] = l_oN.m_iOrder;
    if (l_oN.m_eBlend == Blend::Add) l_jJ["blend"] = "add";
    if (!l_oN.m_bInheritColor) l_jJ["inheritColor"] = false;
    if (l_oN.m_bLoop) l_jJ["loop"] = true;
    if (!l_oN.m_bStayAtLastFrame) l_jJ["stayAtLastFrame"] = false;
    if (l_oN.m_arrLattice[0] >= 2 || l_oN.m_arrLattice[1] >= 2) l_jJ["lattice"] = {l_oN.f_LatticeCols(), l_oN.f_LatticeRows()};
    if (l_oN.f_HasMeshGrid()) l_jJ["meshGrid"] = {l_oN.m_arrMeshGrid[0], l_oN.m_arrMeshGrid[1]};
    if (l_oN.f_HasFx()) {
        json l_jFx{{"shader", f_FxShaderName(l_oN.m_oFx.m_eShader)}};
        if (l_oN.m_oFx.m_v4Params != glm::vec4(0)) l_jFx["params"] = f_Vec(l_oN.m_oFx.m_v4Params);
        if (l_oN.m_oFx.m_v4Color != glm::vec4(1)) l_jFx["color"] = f_Vec(l_oN.m_oFx.m_v4Color);
        l_jJ["fx"] = l_jFx;
    }
    if (l_oN.f_HasTrail()) {
        json l_jTr{{"count", l_oN.m_oTrail.m_iCount}, {"gap", l_oN.m_oTrail.m_fGap}, {"color", f_Vec(l_oN.m_oTrail.m_v4Color)}};
        if (l_oN.m_oTrail.m_bAdditive) l_jTr["blend"] = "add";
        l_jJ["trail"] = l_jTr;
    }
    if (l_oN.f_Skinned()) {
        json l_jSkin{{"bones", l_oN.m_vSkinBones}};
        if (l_oN.m_vSkinWeights.empty()) {
            l_jSkin["weights"] = "auto";
        } else {   // per vertex [bone, weight, ...], the zero weights left out
            json l_jW = json::array();
            for (size_t l_nV = 0; l_nV + 2 * g_iSkinInfluences <= l_oN.m_vSkinWeights.size(); l_nV += 2 * g_iSkinInfluences) {
                json l_jOne = json::array();
                for (int l_iK = 0; l_iK < g_iSkinInfluences; l_iK++)
                    if (l_oN.m_vSkinWeights[l_nV + 2 * l_iK + 1] != 0.0f) { l_jOne.push_back((int)l_oN.m_vSkinWeights[l_nV + 2 * l_iK]); l_jOne.push_back(l_oN.m_vSkinWeights[l_nV + 2 * l_iK + 1]); }
                l_jW.push_back(l_jOne);
            }
            l_jSkin["weights"] = l_jW;
        }
        l_jJ["skin"] = l_jSkin;
    }
    json l_jTr = json::object();
    auto l_fnV3 = [](const glm::vec3& l_v3V) { return f_Vec(l_v3V); };
    auto l_fnV4 = [](const glm::vec4& l_v4V) { return f_Vec(l_v4V); };
    auto l_fnS = [](const std::string& l_strV) { return json(l_strV); };
    auto l_fnB = [](bool l_bV) { return json(l_bV); };
    if (!l_oN.m_vPosKeys.empty()) l_jTr["pos"] = f_KeysToJson(l_oN.m_vPosKeys, l_fnV3, true);
    if (!l_oN.m_vRotKeys.empty()) l_jTr["rot"] = f_KeysToJson(l_oN.m_vRotKeys, l_fnV3, true);
    if (!l_oN.m_vScaleKeys.empty()) l_jTr["scale"] = f_KeysToJson(l_oN.m_vScaleKeys, l_fnV3, true);
    if (!l_oN.m_vColorKeys.empty()) l_jTr["color"] = f_KeysToJson(l_oN.m_vColorKeys, l_fnV4, true);
    if (!l_oN.m_vSpriteKeys.empty()) l_jTr["sprite"] = f_KeysToJson(l_oN.m_vSpriteKeys, l_fnS, false);
    if (!l_oN.m_vVisibleKeys.empty()) l_jTr["visible"] = f_KeysToJson(l_oN.m_vVisibleKeys, l_fnB, false);
    if (!l_oN.m_vEventKeys.empty()) l_jTr["event"] = f_KeysToJson(l_oN.m_vEventKeys, l_fnS, false);
    if (!l_oN.m_vFxKeys.empty()) l_jTr["fx"] = f_KeysToJson(l_oN.m_vFxKeys, l_fnV4, true);
    if (!l_oN.m_vTrailKeys.empty()) l_jTr["trail"] = f_KeysToJson(l_oN.m_vTrailKeys, [](float l_fV) { return json(l_fV); }, true);
    if (!l_oN.m_vDeformKeys.empty()) {   // only the points that move: {"5": [0, -2], ...}
        auto l_fnD = [](const Deform& l_mapV) {
            json l_jO = json::object();
            for (const auto& [l_iI, l_v2Off] : l_mapV) l_jO[std::to_string(l_iI)] = f_Vec(l_v2Off);
            return l_jO;
        };
        l_jTr["deform"] = f_KeysToJson(l_oN.m_vDeformKeys, l_fnD, true);
    }
    if (!l_jTr.empty()) l_jJ["tracks"] = l_jTr;
    if (!l_oN.m_vChildren.empty()) {
        l_jJ["children"] = json::array();
        for (const Node& l_oC : l_oN.m_vChildren) l_jJ["children"].push_back(f_NodeToJson(l_oC));
    }
    return l_jJ;
}

glm::vec2 f_ToVec2(const json& l_jJ) { return {l_jJ.at(0).get<float>(), l_jJ.at(1).get<float>()}; }
// [x, y, z]; a version 1 [x, y] gets z = `z`.
glm::vec3 f_ToVec3(const json& l_jJ, float l_fZ) {
    return {l_jJ.at(0).get<float>(), l_jJ.at(1).get<float>(), l_jJ.size() > 2 ? l_jJ.at(2).get<float>() : l_fZ};
}
glm::vec3 f_ToPos(const json& l_jJ) { return f_ToVec3(l_jJ, 0.0f); }
glm::vec3 f_ToScale(const json& l_jJ) { return f_ToVec3(l_jJ, 1.0f); }
// [x, y, z] degrees; a version 1 number is the z (screen-plane) rotation.
glm::vec3 f_ToRot(const json& l_jJ) { return l_jJ.is_number() ? glm::vec3(0, 0, l_jJ.get<float>()) : f_ToVec3(l_jJ, 0.0f); }
glm::vec4 f_ToVec4(const json& l_jJ) { return {l_jJ.at(0).get<float>(), l_jJ.at(1).get<float>(), l_jJ.at(2).get<float>(), l_jJ.at(3).get<float>()}; }

void f_EaseFromJson(const json& l_jJ, Ease& l_oE, const std::string& l_strWhere) {
    if (l_jJ.is_string()) {
        if (!f_ParseEase(l_jJ.get<std::string>(), l_oE)) throw std::runtime_error(l_strWhere + ": unknown ease '" + l_jJ.get<std::string>() + "'");
    } else if (l_jJ.is_array() && l_jJ.size() == 4) {
        l_oE.m_iKind = Ease::m_iBezier;
        for (int l_iI = 0; l_iI < 4; l_iI++) l_oE.m_arrBezier[l_iI] = l_jJ[l_iI].get<float>();
    } else {
        throw std::runtime_error(l_strWhere + ": ease must be a name or [x1, y1, x2, y2]");
    }
}

template <class T, class F>
void f_KeysFromJson(const json& l_jTracks, const char* l_pszChannel, std::vector<Key<T>>& l_vOut, F l_oValue, const std::string& l_strWhere) {
    if (!l_jTracks.contains(l_pszChannel)) return;
    for (const json& l_jK : l_jTracks[l_pszChannel]) {
        Key<T> l_oKey;
        l_oKey.m_fT = l_jK.at("t").get<float>();
        l_oKey.m_oV = l_oValue(l_jK.at("v"));
        if (l_jK.contains("ease")) f_EaseFromJson(l_jK["ease"], l_oKey.m_oEase, l_strWhere + "." + l_pszChannel);
        l_vOut.push_back(l_oKey);
    }
    std::stable_sort(l_vOut.begin(), l_vOut.end(), [](const Key<T>& l_oA, const Key<T>& l_oB) { return l_oA.m_fT < l_oB.m_fT; });
}

void f_NodeFromJson(const json& l_jJ, Node& l_oN, const std::string& l_strWhere) {
    l_oN.m_strName = l_jJ.value("name", std::string());
    const std::string l_strHere = l_strWhere + "/" + l_oN.m_strName;
    l_oN.m_strSprite = l_jJ.value("sprite", std::string());
    if (l_jJ.contains("pos")) l_oN.m_v3Pos = f_ToPos(l_jJ["pos"]);
    if (l_jJ.contains("rot")) l_oN.m_v3Rot = f_ToRot(l_jJ["rot"]);
    if (l_jJ.contains("scale")) l_oN.m_v3Scale = f_ToScale(l_jJ["scale"]);
    if (l_jJ.contains("color")) l_oN.m_v4Color = f_ToVec4(l_jJ["color"]);
    l_oN.m_bVisible = l_jJ.value("visible", true);
    if (l_jJ.contains("pivot")) { l_oN.m_bHasPivot = true; l_oN.m_v2Pivot = f_ToVec2(l_jJ["pivot"]); }
    l_oN.m_iOrder = l_jJ.value("order", 0);
    const std::string l_strBlend = l_jJ.value("blend", std::string("normal"));
    if (l_strBlend != "normal" && l_strBlend != "add") throw std::runtime_error(l_strHere + ": blend must be normal or add");
    l_oN.m_eBlend = l_strBlend == "add" ? Blend::Add : Blend::Normal;
    l_oN.m_bInheritColor = l_jJ.value("inheritColor", true);
    l_oN.m_bLoop = l_jJ.value("loop", false);
    l_oN.m_bStayAtLastFrame = l_jJ.value("stayAtLastFrame", true);
    if (l_jJ.contains("lattice")) {
        const json& l_jL = l_jJ["lattice"];
        if (!l_jL.is_array() || l_jL.size() != 2 || !l_jL[0].is_number_integer() || !l_jL[1].is_number_integer() ||
            l_jL[0].get<int>() < 2 || l_jL[1].get<int>() < 2 || l_jL[0].get<int>() > g_iMaxLattice || l_jL[1].get<int>() > g_iMaxLattice)
            throw std::runtime_error(l_strHere + ": lattice must be [cols, rows], 2 to " + std::to_string(g_iMaxLattice) + " each");
        l_oN.m_arrLattice[0] = l_jL[0].get<int>();
        l_oN.m_arrLattice[1] = l_jL[1].get<int>();
    }
    if (l_jJ.contains("meshGrid")) {
        const json& l_jG = l_jJ["meshGrid"];
        if (!l_jG.is_array() || l_jG.size() != 2 || !l_jG[0].is_number_integer() || !l_jG[1].is_number_integer() ||
            l_jG[0].get<int>() < 1 || l_jG[1].get<int>() < 1 || l_jG[0].get<int>() > g_iMaxMeshGrid || l_jG[1].get<int>() > g_iMaxMeshGrid)
            throw std::runtime_error(l_strHere + ": meshGrid must be [cols, rows], 1 to " + std::to_string(g_iMaxMeshGrid) + " each");
        l_oN.m_arrMeshGrid[0] = l_jG[0].get<int>();
        l_oN.m_arrMeshGrid[1] = l_jG[1].get<int>();
    }
    if (l_jJ.contains("fx")) {
        const json& l_jFx = l_jJ["fx"];
        const std::string l_strName = l_jFx.value("shader", std::string());
        if (!f_ParseFxShader(l_strName, l_oN.m_oFx.m_eShader) || l_oN.m_oFx.m_eShader == FxShader::None)
            throw std::runtime_error(l_strHere + ": fx shader must be group, motionBlur, glow, flash or outline");
        if (l_jFx.contains("params")) l_oN.m_oFx.m_v4Params = f_ToVec4(l_jFx["params"]);
        if (l_jFx.contains("color")) l_oN.m_oFx.m_v4Color = f_ToVec4(l_jFx["color"]);
    }
    if (l_jJ.contains("trail")) {
        const json& l_jTr = l_jJ["trail"];
        l_oN.m_oTrail.m_iCount = l_jTr.value("count", 0);
        if (l_oN.m_oTrail.m_iCount < 0 || l_oN.m_oTrail.m_iCount > g_iMaxTrail)
            throw std::runtime_error(l_strHere + ": trail count must be 0 to " + std::to_string(g_iMaxTrail));
        l_oN.m_oTrail.m_fGap = l_jTr.value("gap", 0.04f);
        if (!(l_oN.m_oTrail.m_fGap > 0)) throw std::runtime_error(l_strHere + ": trail gap must be above 0");
        if (l_jTr.contains("color")) l_oN.m_oTrail.m_v4Color = f_ToVec4(l_jTr["color"]);
        const std::string l_strBlend = l_jTr.value("blend", std::string("normal"));
        if (l_strBlend != "normal" && l_strBlend != "add") throw std::runtime_error(l_strHere + ": trail blend must be normal or add");
        l_oN.m_oTrail.m_bAdditive = l_strBlend == "add";
    }
    if (l_jJ.contains("skin")) {
        const json& l_jSkin = l_jJ["skin"];
        l_oN.m_vSkinBones = l_jSkin.value("bones", std::vector<std::string>());
        if (l_oN.m_vSkinBones.empty()) throw std::runtime_error(l_strHere + ": a skin needs bones");
        const json l_jW = l_jSkin.value("weights", json("auto"));
        if (l_jW.is_array()) {
            for (const json& l_jOne : l_jW) {
                if (!l_jOne.is_array() || l_jOne.size() % 2 || l_jOne.size() > 2 * g_iSkinInfluences)
                    throw std::runtime_error(l_strHere + ": skin weights are [bone, weight, ...] per vertex, at most " +
                                             std::to_string(g_iSkinInfluences) + " bones");
                for (size_t l_nK = 0; l_nK < (size_t)g_iSkinInfluences; l_nK++) {
                    const bool l_bHas = 2 * l_nK < l_jOne.size();
                    const int l_iBone = l_bHas ? l_jOne[2 * l_nK].get<int>() : 0;
                    if (l_iBone < 0 || l_iBone >= (int)l_oN.m_vSkinBones.size()) throw std::runtime_error(l_strHere + ": skin weight names bone " + std::to_string(l_iBone));
                    l_oN.m_vSkinWeights.push_back((float)l_iBone);
                    l_oN.m_vSkinWeights.push_back(l_bHas ? l_jOne[2 * l_nK + 1].get<float>() : 0.0f);
                }
            }
        } else if (!l_jW.is_string() || l_jW.get<std::string>() != "auto") {
            throw std::runtime_error(l_strHere + ": skin weights must be \"auto\" or a list per vertex");
        }
    }
    if (l_jJ.contains("tracks")) {
        const json& l_jTr = l_jJ["tracks"];
        f_KeysFromJson(l_jTr, "pos", l_oN.m_vPosKeys, f_ToPos, l_strHere);
        f_KeysFromJson(l_jTr, "rot", l_oN.m_vRotKeys, f_ToRot, l_strHere);
        f_KeysFromJson(l_jTr, "scale", l_oN.m_vScaleKeys, f_ToScale, l_strHere);
        f_KeysFromJson(l_jTr, "color", l_oN.m_vColorKeys, f_ToVec4, l_strHere);
        f_KeysFromJson(l_jTr, "sprite", l_oN.m_vSpriteKeys, [](const json& l_jV) { return l_jV.get<std::string>(); }, l_strHere);
        f_KeysFromJson(l_jTr, "visible", l_oN.m_vVisibleKeys, [](const json& l_jV) { return l_jV.get<bool>(); }, l_strHere);
        f_KeysFromJson(l_jTr, "event", l_oN.m_vEventKeys, [](const json& l_jV) { return l_jV.get<std::string>(); }, l_strHere);
        f_KeysFromJson(l_jTr, "fx", l_oN.m_vFxKeys, f_ToVec4, l_strHere);
        f_KeysFromJson(l_jTr, "trail", l_oN.m_vTrailKeys, [](const json& l_jV) { return l_jV.get<float>(); }, l_strHere);
        const int l_iPoints = l_oN.f_LatticeCols() * l_oN.f_LatticeRows();
        f_KeysFromJson(l_jTr, "deform", l_oN.m_vDeformKeys, [&](const json& l_jV) {
            if (!l_jV.is_object()) throw std::runtime_error(l_strHere + ".deform: a key's value is {\"point\": [dx, dy], ...}");
            Deform l_mapD;
            for (auto l_oIt = l_jV.begin(); l_oIt != l_jV.end(); ++l_oIt) {
                int l_iI = -1;
                try { l_iI = std::stoi(l_oIt.key()); } catch (...) {}
                if (l_iI < 0 || l_iI >= l_iPoints)
                    throw std::runtime_error(l_strHere + ".deform: no lattice point " + l_oIt.key() + " (" + std::to_string(l_iPoints) + " points)");
                l_mapD[l_iI] = f_ToVec2(l_oIt.value());
            }
            return l_mapD;
        }, l_strHere);
    }
    for (const json& l_jC : l_jJ.value("children", json::array())) {
        l_oN.m_vChildren.emplace_back();
        f_NodeFromJson(l_jC, l_oN.m_vChildren.back(), l_strHere);
    }
}

}  // namespace

bool f_ParseAnim(const std::string& l_strText, AnimFile& l_oOut, std::string* l_pErr) {
    try {
        const json l_jJ = json::parse(l_strText);
        AnimFile l_oF;
        l_oF.m_iVersion = l_jJ.value("version", 1);
        if (l_oF.m_iVersion > g_iAnimVersion)
            throw std::runtime_error("version " + std::to_string(l_oF.m_iVersion) + " is newer than this build reads (" +
                                     std::to_string(g_iAnimVersion) + ")");
        if (l_jJ.contains("atlases")) {
            for (const json& l_jA : l_jJ["atlases"]) {
                AtlasRef l_oR;
                if (l_jA.is_string()) l_oR.m_strPath = l_jA.get<std::string>();
                else { l_oR.m_strPath = l_jA.at("path").get<std::string>(); l_oR.m_strId = l_jA.value("id", std::string()); }
                if (l_oR.m_strId.empty()) l_oR.m_strId = f_DefaultAtlasId(l_oR.m_strPath);
                for (const AtlasRef& l_oO : l_oF.m_vAtlases)
                    if (l_oO.m_strId == l_oR.m_strId) throw std::runtime_error("two atlases have the id '" + l_oR.m_strId + "': give one an \"id\"");
                l_oF.m_vAtlases.push_back(l_oR);
            }
        } else if (l_jJ.contains("atlas")) {   // older files: one atlas
            const std::string l_strPath = l_jJ["atlas"].get<std::string>();
            l_oF.m_vAtlases.push_back({f_DefaultAtlasId(l_strPath), l_strPath});
        }
        for (const json& l_jC : l_jJ.value("clips", json::array())) {
            Clip l_oClip;
            l_oClip.m_strName = l_jC.value("name", std::string());
            l_oClip.m_fLength = l_jC.value("length", 0.0f);
            l_oClip.m_iPlayCount = l_jC.value("playCount", 1);
            l_oClip.m_bStayAtLastFrame = l_jC.value("stayAtLastFrame", true);
            if (l_jC.contains("root")) f_NodeFromJson(l_jC["root"], l_oClip.m_oRoot, l_oClip.m_strName);
            l_oF.m_vClips.push_back(std::move(l_oClip));
        }
        l_oOut = std::move(l_oF);
        return true;
    } catch (const std::exception& l_oE) {
        if (l_pErr) *l_pErr = l_oE.what();
        return false;
    }
}

// ---- characters ----

const Node* f_FindNode(const Node& l_oRoot, const std::string& l_strName) {
    if (l_oRoot.m_strName == l_strName) return &l_oRoot;
    for (const Node& l_oC : l_oRoot.m_vChildren)
        if (const Node* l_pF = f_FindNode(l_oC, l_strName)) return l_pF;
    return nullptr;
}

glm::vec2 f_CharacterHandOffset(const glm::vec2& l_v2Hand, const toms::SpriteBox& l_oFrom, const toms::SpriteBox& l_oTo) {
    if (l_oFrom.f_Empty() || l_oTo.f_Empty()) return glm::vec2(0.0f);
    // Both bodies pivoted at the bottom centre: body space x from the centre, y up from the feet (negative).
    auto l_fnTop = [](const toms::SpriteBox& l_oB) { return l_oB.m_fTop - l_oB.m_fOrigH; };
    auto l_fnBottom = [](const toms::SpriteBox& l_oB) { return l_oB.m_fBottom - l_oB.m_fOrigH; };
    auto l_fnRight = [](const toms::SpriteBox& l_oB) { return l_oB.m_fRight - l_oB.m_fOrigW * 0.5f; };
    const float l_fShare = (l_v2Hand.y - l_fnTop(l_oFrom)) / (l_fnBottom(l_oFrom) - l_fnTop(l_oFrom));   // 0 = top of the head, 1 = the feet
    const float l_fY = l_fnTop(l_oTo) + l_fShare * (l_fnBottom(l_oTo) - l_fnTop(l_oTo));
    const float l_fX = l_v2Hand.x + (l_fnRight(l_oTo) - l_fnRight(l_oFrom));
    return glm::vec2(l_fX - l_v2Hand.x, l_fY - l_v2Hand.y);
}

namespace {
void f_FitWalk(Node& l_oN, const std::string& l_strSprite, const glm::vec2& l_v2Offset) {
    if (l_oN.m_strName == "body") {
        l_oN.m_strSprite = l_strSprite;
        for (auto& l_oK : l_oN.m_vSpriteKeys)
            if (!l_oK.m_oV.empty()) l_oK.m_oV = l_strSprite;
    }
    if (l_oN.m_strName == "hand") {
        l_oN.m_v3Pos += glm::vec3(l_v2Offset, 0.0f);
        for (auto& l_oK : l_oN.m_vPosKeys) l_oK.m_oV += glm::vec3(l_v2Offset, 0.0f);
    }
    for (Node& l_oC : l_oN.m_vChildren) f_FitWalk(l_oC, l_strSprite, l_v2Offset);
}
}  // namespace

Clip f_FitCharacter(const Clip& l_oC, const std::string& l_strSprite,
                  const std::function<bool(const std::string&, toms::SpriteBox&)>& l_fnBoxOf, const glm::vec2& l_v2Extra) {
    Clip l_oOut = l_oC;
    const Node* l_pBody = f_FindNode(l_oC.m_oRoot, "body");
    const Node* l_pHand = f_FindNode(l_oC.m_oRoot, "hand");
    glm::vec2 l_v2Offset = l_v2Extra;
    toms::SpriteBox l_oFrom, l_oTo;
    if (l_pBody && l_pHand && l_fnBoxOf && l_fnBoxOf(l_pBody->m_strSprite, l_oFrom) && l_fnBoxOf(l_strSprite, l_oTo))
        l_v2Offset += f_CharacterHandOffset(glm::vec2(l_pHand->m_v3Pos), l_oFrom, l_oTo);
    // A bare name keeps the body's atlas: "player_mage" for a body "game:player" is "game:player_mage".
    std::string l_strRef = l_strSprite;
    if (l_pBody && l_strSprite.find(':') == std::string::npos) {
        const SpriteRef l_oWas = f_ParseSpriteRef(l_pBody->m_strSprite);
        if (!l_oWas.m_strAtlas.empty()) l_strRef = f_SpriteRef(l_oWas.m_strAtlas, l_strSprite);
    }
    f_FitWalk(l_oOut.m_oRoot, l_strRef, l_v2Offset);
    return l_oOut;
}

std::string f_NodeToJsonText(const Node& l_oN) { return f_NodeToJson(l_oN).dump(2) + "\n"; }

bool f_ParseNodeJson(const std::string& l_strText, Node& l_oOut, std::string* l_pErr) {
    try {
        const json l_jJ = json::parse(l_strText);
        if (!l_jJ.is_object()) throw std::runtime_error("not a node");
        Node l_oN;
        f_NodeFromJson(l_jJ, l_oN, std::string());
        l_oOut = std::move(l_oN);
        return true;
    } catch (const std::exception& l_oE) {
        if (l_pErr) *l_pErr = l_oE.what();
        return false;
    }
}

namespace {
bool f_AnySkin(const Node& l_oN) {   // version 3's fields: a skin or a lattice
    if (l_oN.f_Skinned() || l_oN.f_Deformed() || l_oN.m_arrLattice[0] >= 2 || l_oN.m_arrLattice[1] >= 2 || l_oN.f_HasMeshGrid()) return true;
    for (const Node& l_oC : l_oN.m_vChildren) if (f_AnySkin(l_oC)) return true;
    return false;
}
}  // namespace

std::string f_AnimToJson(const AnimFile& l_oF) {
    json l_jJ;
    const bool l_bEffects = std::any_of(l_oF.m_vClips.begin(), l_oF.m_vClips.end(), [](const Clip& l_oC) { return f_HasEffects(l_oC); });
    l_jJ["version"] = l_bEffects ? 4 : std::any_of(l_oF.m_vClips.begin(), l_oF.m_vClips.end(), [](const Clip& l_oC) { return f_AnySkin(l_oC.m_oRoot); }) ? 3 : 2;
    if (!l_oF.m_vAtlases.empty()) {
        json l_jA = json::array();
        for (const AtlasRef& l_oR : l_oF.m_vAtlases) {   // the short form when the id is the default one
            if (l_oR.m_strId == f_DefaultAtlasId(l_oR.m_strPath)) l_jA.push_back(l_oR.m_strPath);
            else l_jA.push_back({{"id", l_oR.m_strId}, {"path", l_oR.m_strPath}});
        }
        l_jJ["atlases"] = l_jA;
    }
    l_jJ["clips"] = json::array();
    for (const Clip& l_oC : l_oF.m_vClips) {
        json l_jCj{{"name", l_oC.m_strName}};
        if (l_oC.m_fLength > 0) l_jCj["length"] = l_oC.m_fLength;
        if (l_oC.m_iPlayCount != 1) l_jCj["playCount"] = l_oC.m_iPlayCount;
        if (!l_oC.m_bStayAtLastFrame) l_jCj["stayAtLastFrame"] = false;
        l_jCj["root"] = f_NodeToJson(l_oC.m_oRoot);
        l_jJ["clips"].push_back(l_jCj);
    }
    return l_jJ.dump(2) + "\n";
}

}  // namespace toms::anim
