// gltf_anim_texture.cpp -- see gltf_anim_texture.h.
#include "gltf_anim_texture.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace toms::gltf {

namespace {

constexpr char g_pszMagic[8] = {'T', 'O', 'M', 'S', 'A', 'N', 'I', 'M'};
constexpr uint32_t g_uVersion = 1;

uint64_t f_Fnv(uint64_t l_u64H, const void* l_pData, size_t l_nN) {
    const auto* l_pP = static_cast<const uint8_t*>(l_pData);
    for (size_t l_nI = 0; l_nI < l_nN; l_nI++) l_u64H = (l_u64H ^ l_pP[l_nI]) * 1099511628211ull;
    return l_u64H;
}

// The rows of one clip a time falls between, and how far (the last segment may be shorter than 1 / fps).
FramePair f_PairFor(const AnimClip& l_oC, float l_fFps, float l_fT, bool l_bLoop) {
    FramePair l_oOut;
    l_oOut.m_fRow0 = l_oOut.m_fRow1 = (float)l_oC.m_iFirstRow;
    if (l_oC.m_iFrames <= 1 || l_oC.m_fDuration <= 0.0f) return l_oOut;
    if (l_bLoop) {
        l_fT = std::fmod(l_fT, l_oC.m_fDuration);
        if (l_fT < 0.0f) l_fT += l_oC.m_fDuration;
    } else {
        l_fT = std::clamp(l_fT, 0.0f, l_oC.m_fDuration);
    }
    const float l_fStep = 1.0f / l_fFps;
    int l_iI0 = std::min((int)std::floor(l_fT * l_fFps), l_oC.m_iFrames - 1);
    const int l_iI1 = std::min(l_iI0 + 1, l_oC.m_iFrames - 1);
    const float l_fT0 = (float)l_iI0 * l_fStep, l_fT1 = std::min((float)l_iI1 * l_fStep, l_oC.m_fDuration);
    l_oOut.m_fRow0 = (float)(l_oC.m_iFirstRow + l_iI0);
    l_oOut.m_fRow1 = (float)(l_oC.m_iFirstRow + l_iI1);
    l_oOut.m_fBlend = l_fT1 > l_fT0 ? std::clamp((l_fT - l_fT0) / (l_fT1 - l_fT0), 0.0f, 1.0f) : 0.0f;
    return l_oOut;
}

glm::vec4 f_Texel(const AnimTexture& l_oTex, int l_iRow, int l_iX) {
    l_iRow = std::clamp(l_iRow, 0, std::max(0, l_oTex.m_iRows - 1));
    const size_t l_nI = ((size_t)l_iRow * l_oTex.m_iWidth + l_iX) * 4;
    if (l_nI + 3 >= l_oTex.m_vTexels.size()) return glm::vec4(0, 0, 0, 1);
    return glm::vec4(l_oTex.m_vTexels[l_nI], l_oTex.m_vTexels[l_nI + 1], l_oTex.m_vTexels[l_nI + 2], l_oTex.m_vTexels[l_nI + 3]);
}

}  // namespace

int AnimTexture::f_ClipIndex(const std::string& l_strName) const {
    for (size_t l_nI = 0; l_nI < m_vClips.size(); l_nI++)
        if (m_vClips[l_nI].m_strName == l_strName) return (int)l_nI;
    return -1;
}

uint64_t f_ModelHash(const Model& l_oModel, int l_iSkin) {
    uint64_t l_u64H = 1469598103934665603ull;
    if (l_iSkin < 0 || l_iSkin >= (int)l_oModel.m_vSkins.size()) return l_u64H;
    const Skin& l_oS = l_oModel.m_vSkins[(size_t)l_iSkin];
    for (size_t l_nJ = 0; l_nJ < l_oS.m_vJoints.size(); l_nJ++) {
        const int l_iN = l_oS.m_vJoints[l_nJ];
        if (l_iN >= 0 && l_iN < (int)l_oModel.m_vNodes.size()) l_u64H = f_Fnv(l_u64H, l_oModel.m_vNodes[(size_t)l_iN].m_strName.data(), l_oModel.m_vNodes[(size_t)l_iN].m_strName.size());
        if (l_nJ < l_oS.m_vInverseBind.size()) l_u64H = f_Fnv(l_u64H, &l_oS.m_vInverseBind[l_nJ], sizeof(glm::mat4));
    }
    for (const Animation& l_oA : l_oModel.m_vAnimations) {
        l_u64H = f_Fnv(l_u64H, l_oA.m_strName.data(), l_oA.m_strName.size());
        l_u64H = f_Fnv(l_u64H, &l_oA.m_fDuration, sizeof l_oA.m_fDuration);
        const size_t l_nCh = l_oA.m_vChannels.size();
        l_u64H = f_Fnv(l_u64H, &l_nCh, sizeof l_nCh);
    }
    return l_u64H;
}

bool f_BakeAnimTexture(const Model& l_oModel, int l_iSkin, float l_fFps, AnimTexture& l_oOut, std::string* l_pError) {
    auto l_fnFail = [&](const std::string& l_strE) { if (l_pError) *l_pError = l_strE; return false; };
    if (l_iSkin < 0 || l_iSkin >= (int)l_oModel.m_vSkins.size()) return l_fnFail("no skin " + std::to_string(l_iSkin));
    const Skin& l_oS = l_oModel.m_vSkins[(size_t)l_iSkin];
    if (l_oS.m_vJoints.empty()) return l_fnFail("the skin has no joints");
    if ((int)l_oS.m_vJoints.size() > g_iMaxJoints) return l_fnFail("the skin has " + std::to_string(l_oS.m_vJoints.size()) + " joints (at most " +
                                                       std::to_string(g_iMaxJoints) + ")");
    l_fFps = std::clamp(l_fFps, 1.0f, 240.0f);
    l_oOut = AnimTexture();
    l_oOut.m_iSkin = l_iSkin;
    l_oOut.m_iJoints = (int)l_oS.m_vJoints.size();
    l_oOut.m_fFps = l_fFps;
    l_oOut.m_iWidth = l_oOut.m_iJoints * 2;
    l_oOut.m_u64SourceHash = f_ModelHash(l_oModel, l_iSkin);

    Pose l_oPose;
    l_oPose.f_Bind(&l_oModel);
    std::vector<glm::mat4> l_vMats;
    std::vector<glm::quat> l_vPrevious((size_t)l_oOut.m_iJoints);
    auto l_fnBakeFrame = [&](int l_iAnim, float l_fT, bool l_bFirst) {
        l_oPose.f_Reset();
        if (l_iAnim >= 0) l_oPose.f_Apply(l_iAnim, l_fT);
        l_oPose.f_UpdateWorld();
        l_oPose.f_JointMatrices(l_iSkin, l_vMats);
        for (int l_iJ = 0; l_iJ < l_oOut.m_iJoints; l_iJ++) {
            const glm::mat4& l_mat4M = l_vMats[(size_t)l_iJ];
            const glm::vec3 l_v3C0(l_mat4M[0]), l_v3C1(l_mat4M[1]), l_v3C2(l_mat4M[2]);
            const float l_fSx = glm::length(l_v3C0), l_fSy = glm::length(l_v3C1), l_fSz = glm::length(l_v3C2);
            const float l_fSu = (l_fSx + l_fSy + l_fSz) / 3.0f;
            if (l_fSu > 1e-8f)
                l_oOut.m_fMaxScaleError = std::max(l_oOut.m_fMaxScaleError, std::max({std::fabs(l_fSx - l_fSu), std::fabs(l_fSy - l_fSu), std::fabs(l_fSz - l_fSu)}) / l_fSu);
            glm::mat3 l_mat3R(l_fSx > 1e-8f ? l_v3C0 / l_fSx : l_v3C0, l_fSy > 1e-8f ? l_v3C1 / l_fSy : l_v3C1, l_fSz > 1e-8f ? l_v3C2 / l_fSz : l_v3C2);
            if (glm::determinant(l_mat3R) < 0.0f) l_oOut.m_bMirroredJoints = true;
            glm::quat l_qQ = glm::normalize(glm::quat_cast(l_mat3R));
            // Keep each joint's quaternion on the same side as its previous frame, so two frames always blend
            // the short way round (the shader also aligns signs, this keeps neighbouring frames consistent).
            if (!l_bFirst && glm::dot(l_qQ, l_vPrevious[(size_t)l_iJ]) < 0.0f) l_qQ = -l_qQ;
            l_vPrevious[(size_t)l_iJ] = l_qQ;
            l_oOut.m_vTexels.insert(l_oOut.m_vTexels.end(), {l_qQ.x, l_qQ.y, l_qQ.z, l_qQ.w, l_mat4M[3].x, l_mat4M[3].y, l_mat4M[3].z, l_fSu});
        }
        l_oOut.m_iRows++;
    };

    for (size_t l_nA = 0; l_nA < l_oModel.m_vAnimations.size(); l_nA++) {
        AnimClip l_oC;
        l_oC.m_strName = l_oModel.m_vAnimations[l_nA].m_strName.empty() ? "animation_" + std::to_string(l_nA) : l_oModel.m_vAnimations[l_nA].m_strName;
        l_oC.m_iAnimation = (int)l_nA;
        l_oC.m_iFirstRow = l_oOut.m_iRows;
        l_oC.m_fDuration = std::max(0.0f, l_oModel.m_vAnimations[l_nA].m_fDuration);
        l_oC.m_iFrames = l_oC.m_fDuration > 0.0f ? (int)std::ceil(l_oC.m_fDuration * l_fFps - 1e-4f) + 1 : 1;
        for (int l_iF = 0; l_iF < l_oC.m_iFrames; l_iF++) l_fnBakeFrame((int)l_nA, std::min((float)l_iF / l_fFps, l_oC.m_fDuration), l_iF == 0);
        l_oOut.m_vClips.push_back(l_oC);
    }
    if (l_oOut.m_vClips.empty()) {   // no animation: one row, the rest pose, so the model still draws
        AnimClip l_oC;
        l_oC.m_strName = "rest";
        l_oC.m_iFirstRow = 0;
        l_oOut.m_vClips.push_back(l_oC);
        l_fnBakeFrame(-1, 0.0f, true);
    }
    return true;
}

bool f_SaveAnimTexture(const AnimTexture& l_oTex, const std::string& l_strPath, std::string* l_pError) {
    std::ofstream l_oF(l_strPath, std::ios::binary);
    if (!l_oF) { if (l_pError) *l_pError = "cannot write " + l_strPath; return false; }
    auto l_fnPut = [&](const void* l_pP, size_t l_nN) { l_oF.write(static_cast<const char*>(l_pP), (std::streamsize)l_nN); };
    auto l_fnPut32 = [&](uint32_t l_uV) { l_fnPut(&l_uV, 4); };
    l_fnPut(g_pszMagic, 8);
    l_fnPut32(g_uVersion);
    l_fnPut(&l_oTex.m_u64SourceHash, 8);
    l_fnPut32((uint32_t)l_oTex.m_iSkin);
    l_fnPut32((uint32_t)l_oTex.m_iJoints);
    l_fnPut(&l_oTex.m_fFps, 4);
    l_fnPut32((uint32_t)l_oTex.m_iWidth);
    l_fnPut32((uint32_t)l_oTex.m_iRows);
    l_fnPut(&l_oTex.m_fMaxScaleError, 4);
    l_fnPut32(l_oTex.m_bMirroredJoints ? 1u : 0u);
    l_fnPut32((uint32_t)l_oTex.m_vClips.size());
    for (const AnimClip& l_oC : l_oTex.m_vClips) {
        l_fnPut32((uint32_t)l_oC.m_strName.size());
        l_fnPut(l_oC.m_strName.data(), l_oC.m_strName.size());
        l_fnPut32((uint32_t)l_oC.m_iAnimation);
        l_fnPut32((uint32_t)l_oC.m_iFirstRow);
        l_fnPut32((uint32_t)l_oC.m_iFrames);
        l_fnPut(&l_oC.m_fDuration, 4);
    }
    l_fnPut(l_oTex.m_vTexels.data(), l_oTex.f_Bytes());
    if (!l_oF) { if (l_pError) *l_pError = "write failed: " + l_strPath; return false; }
    return true;
}

bool f_LoadAnimTexture(const std::string& l_strPath, AnimTexture& l_oOut, const Model* l_pCheckAgainst, int l_iSkin, std::string* l_pError) {
    auto l_fnFail = [&](const std::string& l_strE) { if (l_pError) *l_pError = l_strPath + ": " + l_strE; return false; };
    std::ifstream l_oF(l_strPath, std::ios::binary);
    if (!l_oF) return l_fnFail("cannot open");
    auto l_fnGet = [&](void* l_pP, size_t l_nN) { l_oF.read(static_cast<char*>(l_pP), (std::streamsize)l_nN); return (bool)l_oF; };
    auto l_fnGet32 = [&](uint32_t& l_uV) { return l_fnGet(&l_uV, 4); };
    char l_pszMagic[8];
    uint32_t l_uVersion = 0, l_uU = 0;
    if (!l_fnGet(l_pszMagic, 8) || std::memcmp(l_pszMagic, g_pszMagic, 8) != 0) return l_fnFail("not an animation texture");
    if (!l_fnGet32(l_uVersion) || l_uVersion != g_uVersion) return l_fnFail("version " + std::to_string(l_uVersion) + " (expected " + std::to_string(g_uVersion) + ")");
    AnimTexture l_oT;
    if (!l_fnGet(&l_oT.m_u64SourceHash, 8)) return l_fnFail("truncated");
    l_fnGet32(l_uU); l_oT.m_iSkin = (int)l_uU;
    l_fnGet32(l_uU); l_oT.m_iJoints = (int)l_uU;
    l_fnGet(&l_oT.m_fFps, 4);
    l_fnGet32(l_uU); l_oT.m_iWidth = (int)l_uU;
    l_fnGet32(l_uU); l_oT.m_iRows = (int)l_uU;
    l_fnGet(&l_oT.m_fMaxScaleError, 4);
    l_fnGet32(l_uU); l_oT.m_bMirroredJoints = l_uU != 0;
    uint32_t l_uClips = 0;
    if (!l_fnGet32(l_uClips) || l_uClips > 100000 || l_oT.m_iJoints <= 0 || l_oT.m_iJoints > g_iMaxJoints || l_oT.m_iWidth != l_oT.m_iJoints * 2 || l_oT.m_iRows <= 0)
        return l_fnFail("bad header");
    for (uint32_t l_uI = 0; l_uI < l_uClips; l_uI++) {
        AnimClip l_oC;
        uint32_t l_uLen = 0;
        if (!l_fnGet32(l_uLen) || l_uLen > 4096) return l_fnFail("bad clip table");
        l_oC.m_strName.resize(l_uLen);
        if (l_uLen) l_fnGet(l_oC.m_strName.data(), l_uLen);
        l_fnGet32(l_uU); l_oC.m_iAnimation = (int)l_uU;
        l_fnGet32(l_uU); l_oC.m_iFirstRow = (int)l_uU;
        l_fnGet32(l_uU); l_oC.m_iFrames = (int)l_uU;
        if (!l_fnGet(&l_oC.m_fDuration, 4)) return l_fnFail("truncated clip table");
        if (l_oC.m_iFirstRow < 0 || l_oC.m_iFrames < 1 || l_oC.m_iFirstRow + l_oC.m_iFrames > l_oT.m_iRows) return l_fnFail("clip '" + l_oC.m_strName + "' is outside the texture");
        l_oT.m_vClips.push_back(l_oC);
    }
    l_oT.m_vTexels.resize((size_t)l_oT.m_iRows * l_oT.m_iWidth * 4);
    if (!l_fnGet(l_oT.m_vTexels.data(), l_oT.f_Bytes())) return l_fnFail("truncated texels");
    if (l_pCheckAgainst && l_oT.m_u64SourceHash != f_ModelHash(*l_pCheckAgainst, l_iSkin >= 0 ? l_iSkin : l_oT.m_iSkin))
        return l_fnFail("stale: the model or its animations changed since it was baked (bake it again)");
    l_oOut = std::move(l_oT);
    return true;
}

FramePair f_FramePair(const AnimTexture& l_oTex, int l_iClip, float l_fT, bool l_bLoop) {
    if (l_iClip < 0 || l_iClip >= (int)l_oTex.m_vClips.size()) return FramePair{};
    return f_PairFor(l_oTex.m_vClips[(size_t)l_iClip], l_oTex.m_fFps, l_fT, l_bLoop);
}

glm::mat4 f_SampleJoint(const AnimTexture& l_oTex, int l_iJoint, const FramePair& l_oA, const FramePair* l_pB, float l_fW) {
    // The same steps as vs_mesh_animtex.sc: nlerp the rotations (signs aligned), lerp translation and scale.
    auto l_fnClipPose = [&](const FramePair& l_oP, glm::vec4& l_v4Q, glm::vec4& l_v4Ts) {
        const glm::vec4 l_v4Q0 = f_Texel(l_oTex, (int)l_oP.m_fRow0, l_iJoint * 2), l_v4Q1 = f_Texel(l_oTex, (int)l_oP.m_fRow1, l_iJoint * 2);
        l_v4Q = l_v4Q0 * (1.0f - l_oP.m_fBlend) + l_v4Q1 * (glm::dot(l_v4Q0, l_v4Q1) < 0.0f ? -l_oP.m_fBlend : l_oP.m_fBlend);
        l_v4Ts = glm::mix(f_Texel(l_oTex, (int)l_oP.m_fRow0, l_iJoint * 2 + 1), f_Texel(l_oTex, (int)l_oP.m_fRow1, l_iJoint * 2 + 1), l_oP.m_fBlend);
    };
    glm::vec4 l_v4Q, l_v4Ts;
    l_fnClipPose(l_oA, l_v4Q, l_v4Ts);
    if (l_pB && l_fW > 0.0f) {
        glm::vec4 l_v4Qb, l_v4Tsb;
        l_fnClipPose(*l_pB, l_v4Qb, l_v4Tsb);
        l_v4Q = l_v4Q * (1.0f - l_fW) + l_v4Qb * (glm::dot(l_v4Q, l_v4Qb) < 0.0f ? -l_fW : l_fW);
        l_v4Ts = glm::mix(l_v4Ts, l_v4Tsb, l_fW);
    }
    const float l_fLen = glm::length(l_v4Q);
    l_v4Q = l_fLen > 1e-8f ? l_v4Q / l_fLen : glm::vec4(0, 0, 0, 1);
    const glm::mat3 l_mat3R = glm::mat3_cast(glm::quat(l_v4Q.w, l_v4Q.x, l_v4Q.y, l_v4Q.z)) * l_v4Ts.w;
    glm::mat4 l_mat4M(l_mat3R);
    l_mat4M[3] = glm::vec4(l_v4Ts.x, l_v4Ts.y, l_v4Ts.z, 1.0f);
    return l_mat4M;
}

// ---- Crowd ----

int Crowd::f_Add(const glm::mat4& l_mat4Placement, int l_iClip, float l_fTime, bool l_bLoop) {
    CrowdInstance l_oC;
    l_oC.m_mat4Placement = l_mat4Placement;
    l_oC.m_oA.m_iClip = l_iClip;
    l_oC.m_oA.m_fTime = l_fTime;
    l_oC.m_oA.m_bLoop = l_bLoop;
    m_vInstances.push_back(l_oC);
    return (int)m_vInstances.size() - 1;
}

void Crowd::f_Remove(int l_iI) {
    if (l_iI >= 0 && l_iI < size()) m_vInstances.erase(m_vInstances.begin() + l_iI);
}

void Crowd::f_Play(int l_iI, int l_iClip, bool l_bLoop, float l_fTime) {
    if (l_iI < 0 || l_iI >= size()) return;
    CrowdInstance& l_oC = m_vInstances[(size_t)l_iI];
    l_oC.m_oA = CrowdTrack{l_iClip, l_fTime, l_oC.m_oA.m_fSpeed, l_bLoop};
    l_oC.m_fWeight = 0.0f;
    l_oC.m_fFadeSeconds = l_oC.m_fFadeElapsed = 0.0f;
}

void Crowd::f_CrossFade(int l_iI, int l_iClip, float l_fSeconds, bool l_bLoop) {
    if (l_iI < 0 || l_iI >= size()) return;
    CrowdInstance& l_oC = m_vInstances[(size_t)l_iI];
    if (l_fSeconds <= 0.0f) { f_Play(l_iI, l_iClip, l_bLoop); return; }
    if (l_oC.m_fWeight > 0.5f) l_oC.m_oA = l_oC.m_oB;   // already mostly B (a fade or blend in progress): fade on from B
    l_oC.m_oB = CrowdTrack{l_iClip, 0.0f, l_oC.m_oA.m_fSpeed, l_bLoop};
    l_oC.m_fWeight = 0.0f;
    l_oC.m_fFadeSeconds = l_fSeconds;
    l_oC.m_fFadeElapsed = 0.0f;
}

void Crowd::f_SetBlend(int l_iI, int l_iClipA, int l_iClipB, float l_fWeight) {
    if (l_iI < 0 || l_iI >= size()) return;
    CrowdInstance& l_oC = m_vInstances[(size_t)l_iI];
    if (l_oC.m_oA.m_iClip != l_iClipA) l_oC.m_oA = CrowdTrack{l_iClipA, 0.0f, l_oC.m_oA.m_fSpeed, true};
    if (l_oC.m_oB.m_iClip != l_iClipB || l_oC.m_fWeight <= 0.0f) l_oC.m_oB = CrowdTrack{l_iClipB, l_oC.m_oB.m_iClip == l_iClipB ? l_oC.m_oB.m_fTime : 0.0f, l_oC.m_oA.m_fSpeed, true};
    l_oC.m_fWeight = std::clamp(l_fWeight, 0.0f, 1.0f);
    l_oC.m_fFadeSeconds = l_oC.m_fFadeElapsed = 0.0f;
}

void Crowd::f_Update(float l_fDt) {
    for (CrowdInstance& l_oC : m_vInstances) {
        l_oC.m_oA.m_fTime += l_fDt * l_oC.m_oA.m_fSpeed;
        if (l_oC.m_fWeight > 0.0f || l_oC.m_fFadeSeconds > 0.0f) l_oC.m_oB.m_fTime += l_fDt * l_oC.m_oB.m_fSpeed;
        if (l_oC.m_fFadeSeconds > 0.0f) {
            l_oC.m_fFadeElapsed += l_fDt;
            l_oC.m_fWeight = std::clamp(l_oC.m_fFadeElapsed / l_oC.m_fFadeSeconds, 0.0f, 1.0f);
            if (l_oC.m_fFadeElapsed >= l_oC.m_fFadeSeconds) {   // done: B is what it plays now
                l_oC.m_oA = l_oC.m_oB;
                l_oC.m_fWeight = 0.0f;
                l_oC.m_fFadeSeconds = l_oC.m_fFadeElapsed = 0.0f;
            }
        }
    }
}

void Crowd::f_GpuInstances(std::vector<CrowdGpuInstance>& l_vOut) const {
    l_vOut.clear();
    if (!m_spTex) return;
    l_vOut.reserve(m_vInstances.size());
    for (const CrowdInstance& l_oC : m_vInstances) {
        if (l_oC.m_bHidden) continue;
        CrowdGpuInstance l_oG;
        for (int l_iR = 0; l_iR < 3; l_iR++) l_oG.m_arrRows[l_iR] = glm::vec4(l_oC.m_mat4Placement[0][l_iR], l_oC.m_mat4Placement[1][l_iR], l_oC.m_mat4Placement[2][l_iR], l_oC.m_mat4Placement[3][l_iR]);
        const FramePair l_oA = f_FramePair(*m_spTex, l_oC.m_oA.m_iClip, l_oC.m_oA.m_fTime, l_oC.m_oA.m_bLoop);
        l_oG.m_v4A = glm::vec4(l_oA.m_fRow0, l_oA.m_fRow1, l_oA.m_fBlend, 0.0f);
        if (l_oC.m_fWeight > 0.0f) {
            const FramePair l_oB = f_FramePair(*m_spTex, l_oC.m_oB.m_iClip, l_oC.m_oB.m_fTime, l_oC.m_oB.m_bLoop);
            l_oG.m_v4B = glm::vec4(l_oB.m_fRow0, l_oB.m_fRow1, l_oB.m_fBlend, l_oC.m_fWeight);
        } else {
            l_oG.m_v4B = glm::vec4(l_oA.m_fRow0, l_oA.m_fRow1, l_oA.m_fBlend, 0.0f);
        }
        l_vOut.push_back(l_oG);
    }
}

}  // namespace toms::gltf
