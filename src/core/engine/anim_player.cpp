// anim_player.cpp -- see anim_player.h.
#include "anim_player.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace toms::anim {

glm::mat3 f_Placement(float l_fX, float l_fY, float l_fScale, float l_fRotationDegrees) {
    return f_Flatten(f_Compose({l_fX, l_fY, 0}, {0, 0, l_fRotationDegrees}, {l_fScale, l_fScale, 1}));
}

const toms::AtlasRegion* AtlasSet::f_Find(const std::string& l_strRef, uint16_t* l_pTexture, int* l_pEntry) const {
    SpriteRef l_oS = f_ParseSpriteRef(l_strRef);
    // An atlas id this set does not have (e.g. the game draws a clip with only its own atlas): look
    // the name up by order instead. The editor's check reports such references.
    if (!l_oS.m_strAtlas.empty() &&
        std::none_of(m_vEntries.begin(), m_vEntries.end(), [&](const Entry& l_oE) { return l_oE.m_strId == l_oS.m_strAtlas; }))
        l_oS.m_strAtlas.clear();
    for (size_t l_nI = 0; l_nI < m_vEntries.size(); l_nI++) {
        const Entry& l_oE = m_vEntries[l_nI];
        if (!l_oE.m_pAtlas || (!l_oS.m_strAtlas.empty() && l_oE.m_strId != l_oS.m_strAtlas)) continue;
        const toms::AtlasRegion* l_pR = l_oE.m_pAtlas->f_Find(l_oS.m_strName);
        if (!l_pR) {
            if (!l_oS.m_strAtlas.empty()) return nullptr;   // the named atlas does not have it: no fallback
            continue;
        }
        if (l_pEntry) *l_pEntry = (int)l_nI;
        if (l_pTexture) {
            const bool l_bOwn = l_pR->m_iPage >= 0 && l_pR->m_iPage < (int)l_oE.m_vPageTextures.size();
            *l_pTexture = l_bOwn ? l_oE.m_vPageTextures[(size_t)l_pR->m_iPage] : g_u16SpriteAtlasTexture;
        }
        return l_pR;
    }
    return nullptr;
}

std::string f_MeshKey(const std::string& l_strSprite, int l_iCols, int l_iRows) {
    return l_iCols > 0 && l_iRows > 0 ? l_strSprite + "@" + std::to_string(l_iCols) + "x" + std::to_string(l_iRows) : l_strSprite;
}

std::string AtlasSet::f_MeshKeyFor(const std::string& l_strRef, const Node& l_oNode, int* l_pEntry) const {
    int l_iE = -1;
    const toms::AtlasRegion* l_pR = f_Find(l_strRef, nullptr, &l_iE);
    if (l_pEntry) *l_pEntry = l_iE;
    if (!l_pR || l_iE < 0) return std::string();
    return l_oNode.f_HasMeshGrid() ? f_MeshKey(l_pR->m_strName, l_oNode.m_arrMeshGrid[0], l_oNode.m_arrMeshGrid[1]) : f_MeshKey(l_pR->m_strName);
}

const toms::SpriteMesh* AtlasSet::f_FindMesh(const std::string& l_strRef, const Node& l_oNode) const {
    int l_iEntry = -1;
    const std::string l_strKey = f_MeshKeyFor(l_strRef, l_oNode, &l_iEntry);
    if (l_strKey.empty() || !m_vEntries[(size_t)l_iEntry].m_pMeshes) return nullptr;
    return m_vEntries[(size_t)l_iEntry].m_pMeshes->f_Find(l_strKey);
}

namespace {
void f_CollectNeeds(const Node& l_oN, const AtlasSet& l_oAtlases, std::map<std::string, MeshNeed>& l_mapOut) {
    if (l_oN.f_UsesMesh()) {
        std::vector<std::string> l_vSprites;
        if (!l_oN.m_strSprite.empty()) l_vSprites.push_back(l_oN.m_strSprite);
        for (const auto& l_oK : l_oN.m_vSpriteKeys)
            if (!l_oK.m_oV.empty()) l_vSprites.push_back(l_oK.m_oV);
        for (const std::string& l_strS : l_vSprites) {
            MeshNeed l_oNeed;
            l_oNeed.m_pRegion = l_oAtlases.f_Find(l_strS, nullptr, &l_oNeed.m_iEntry);
            if (!l_oNeed.m_pRegion || l_oNeed.m_iEntry < 0) continue;
            if (l_oN.f_HasMeshGrid()) { l_oNeed.m_iCols = l_oN.m_arrMeshGrid[0]; l_oNeed.m_iRows = l_oN.m_arrMeshGrid[1]; }
            l_oNeed.m_strKey = l_oAtlases.f_MeshKeyFor(l_strS, l_oN);
            char l_pszAt[16];
            std::snprintf(l_pszAt, sizeof l_pszAt, "%04d|", l_oNeed.m_iEntry);   // by atlas, then key
            l_mapOut.emplace(l_pszAt + l_oNeed.m_strKey, l_oNeed);
        }
    }
    for (const Node& l_oC : l_oN.m_vChildren) f_CollectNeeds(l_oC, l_oAtlases, l_mapOut);
}
std::vector<MeshNeed> f_Sorted(std::map<std::string, MeshNeed>& l_mapM) {
    std::vector<MeshNeed> l_vV;
    for (auto& l_pairKv : l_mapM) l_vV.push_back(l_pairKv.second);
    return l_vV;
}
}  // namespace

std::vector<MeshNeed> f_MeshesNeeded(const Clip& l_oClip, const AtlasSet& l_oAtlases) {
    std::map<std::string, MeshNeed> l_mapM;
    f_CollectNeeds(l_oClip.m_oRoot, l_oAtlases, l_mapM);
    return f_Sorted(l_mapM);
}

toms::SpriteMesh f_CutMesh(const MeshNeed& l_oNeed, const uint8_t* l_pRgba, int l_iStride) {
    const toms::AtlasRegion* l_pR = l_oNeed.m_pRegion;
    if (!l_pR || !l_pRgba || l_pR->m_bRotated || l_pR->m_bChild || l_pR->m_iW <= 0 || l_pR->m_iH <= 0) return toms::SpriteMesh();
    if (l_oNeed.m_iCols > 0 && l_oNeed.m_iRows > 0)
        return toms::f_MeshFromAlphaGrid(l_pRgba, l_iStride, l_pR->m_iW, l_pR->m_iH, l_pR->m_iOffX, l_pR->m_iOffY, l_oNeed.m_iCols, l_oNeed.m_iRows);
    return toms::f_MeshFromAlpha(l_pRgba, l_iStride, l_pR->m_iW, l_pR->m_iH, l_pR->m_iOffX, l_pR->m_iOffY, g_iMeshCell);
}

std::vector<MeshNeed> f_MeshesNeeded(const AnimFile& l_oFile, const AtlasSet& l_oAtlases) {
    std::map<std::string, MeshNeed> l_mapM;
    for (const Clip& l_oC : l_oFile.m_vClips) f_CollectNeeds(l_oC.m_oRoot, l_oAtlases, l_mapM);
    return f_Sorted(l_mapM);
}

void DrawList::f_Submit(IRenderer* l_pRen) const {
    if (!l_pRen) return;
    // the batched run's vertices, reused from call to call
    static thread_local std::vector<MeshVertex> l_vRunVerts;
    static thread_local std::vector<float> l_vRunColors;
    static thread_local std::vector<uint16_t> l_vRunIdx;
    auto l_fnTexOf = [&](const Item& l_oIt) { return l_oIt.m_eKind == QuadItem ? m_vQuads[l_oIt.m_uIndex].m_u16Texture : m_vMeshes[l_oIt.m_uIndex].m_u16Texture; };
    auto l_fnAddOf = [&](const Item& l_oIt) { return l_oIt.m_eKind == QuadItem ? m_vQuads[l_oIt.m_uIndex].m_bAdditive : m_vMeshes[l_oIt.m_uIndex].m_bAdditive; };
    auto l_fnVertsOf = [&](const Item& l_oIt) { return l_oIt.m_eKind == QuadItem ? 4u : m_vMeshes[l_oIt.m_uIndex].m_uVertexCount; };
    size_t l_nI = 0;
    while (l_nI < m_vOrder.size()) {
        const Item& l_oIt = m_vOrder[l_nI];
        if (l_oIt.m_eKind == BeginLayer) { l_pRen->f_BeginLayer(m_vLayers[l_oIt.m_uIndex]); l_nI++; continue; }
        if (l_oIt.m_eKind == EndLayer) { l_pRen->f_EndLayer(); l_nI++; continue; }
        if (l_oIt.m_eKind == QuadItem && m_vQuads[l_oIt.m_uIndex].m_bSolid) { l_pRen->f_DrawSprite(m_vQuads[l_oIt.m_uIndex]); l_nI++; continue; }
        // the run: quads (not solid) and meshes with this texture and blending, at most 65536 vertices (16-bit indices)
        const uint16_t l_u16Tex = l_fnTexOf(l_oIt);
        const bool l_bAdd = l_fnAddOf(l_oIt);
        size_t l_nJ = l_nI;
        uint32_t l_uVerts = 0;
        bool l_bMesh = false;
        while (l_nJ < m_vOrder.size()) {
            const Item& l_oO = m_vOrder[l_nJ];
            if (l_oO.m_eKind == BeginLayer || l_oO.m_eKind == EndLayer || (l_oO.m_eKind == QuadItem && m_vQuads[l_oO.m_uIndex].m_bSolid)) break;
            if (l_fnTexOf(l_oO) != l_u16Tex || l_fnAddOf(l_oO) != l_bAdd || l_uVerts + l_fnVertsOf(l_oO) > 65536u) break;
            l_uVerts += l_fnVertsOf(l_oO);
            l_bMesh = l_bMesh || l_oO.m_eKind == MeshItemKind;
            l_nJ++;
        }
        if (!l_bMesh) {   // quads only: the sprite batch
            for (; l_nI < l_nJ; l_nI++) l_pRen->f_DrawSprite(m_vQuads[m_vOrder[l_nI].m_uIndex]);
            continue;
        }
        l_vRunVerts.clear();
        l_vRunColors.clear();
        l_vRunIdx.clear();
        for (; l_nI < l_nJ; l_nI++) {
            const Item& l_oO = m_vOrder[l_nI];
            const uint16_t l_u16Base = uint16_t(l_vRunVerts.size());
            if (l_oO.m_eKind == QuadItem) {
                const Quad& l_oQ = m_vQuads[l_oO.m_uIndex];
                const float l_fX0 = l_oQ.m_arrRect[0], l_fY0 = l_oQ.m_arrRect[1], l_fX1 = l_oQ.m_arrRect[0] + l_oQ.m_arrRect[2], l_fY1 = l_oQ.m_arrRect[1] + l_oQ.m_arrRect[3];
                const float l_arrRc[8] = {l_fX0, l_fY0, l_fX1, l_fY0, l_fX1, l_fY1, l_fX0, l_fY1};
                const float* l_pC = l_oQ.m_bHasCorners ? l_oQ.m_arrCorners : l_arrRc;
                l_vRunVerts.push_back({l_pC[0], l_pC[1], l_oQ.m_arrUv[0], l_oQ.m_arrUv[1]});
                l_vRunVerts.push_back({l_pC[2], l_pC[3], l_oQ.m_arrUv[2], l_oQ.m_arrUv[1]});
                l_vRunVerts.push_back({l_pC[4], l_pC[5], l_oQ.m_arrUv[2], l_oQ.m_arrUv[3]});
                l_vRunVerts.push_back({l_pC[6], l_pC[7], l_oQ.m_arrUv[0], l_oQ.m_arrUv[3]});
                for (int l_iK = 0; l_iK < 4; l_iK++) l_vRunColors.insert(l_vRunColors.end(), l_oQ.m_arrTint, l_oQ.m_arrTint + 4);
                for (uint16_t l_u16K : {0, 1, 2, 0, 2, 3}) l_vRunIdx.push_back(uint16_t(l_u16Base + l_u16K));
            } else {
                const MeshItem& l_oM = m_vMeshes[l_oO.m_uIndex];
                l_vRunVerts.insert(l_vRunVerts.end(), m_vVertices.begin() + l_oM.m_uFirstVertex, m_vVertices.begin() + l_oM.m_uFirstVertex + l_oM.m_uVertexCount);
                for (uint32_t l_uK = 0; l_uK < l_oM.m_uVertexCount; l_uK++) l_vRunColors.insert(l_vRunColors.end(), l_oM.m_arrTint, l_oM.m_arrTint + 4);
                for (uint32_t l_uK = 0; l_uK < l_oM.m_uIndexCount; l_uK++) l_vRunIdx.push_back(uint16_t(l_u16Base + m_vIndices[l_oM.m_uFirstIndex + l_uK]));
            }
        }
        Mesh l_oBatch;
        l_oBatch.m_pVertices = l_vRunVerts.data();
        l_oBatch.m_uVertexCount = uint32_t(l_vRunVerts.size());
        l_oBatch.m_pIndices = l_vRunIdx.data();
        l_oBatch.m_uIndexCount = uint32_t(l_vRunIdx.size());
        l_oBatch.m_pColors = l_vRunColors.data();
        l_oBatch.m_bAdditive = l_bAdd;
        l_oBatch.m_u16Texture = l_u16Tex;
        l_pRen->f_DrawMesh(l_oBatch);
    }
}

namespace {
void f_RestWalk(const Node& l_oN, const glm::mat4& l_mat4Parent, std::vector<std::pair<const Node*, glm::mat4>>& l_vOut) {
    const glm::mat4 l_mat4W = l_mat4Parent * f_Compose(l_oN.m_v3Pos, l_oN.m_v3Rot, l_oN.m_v3Scale);
    l_vOut.push_back({&l_oN, l_mat4W});
    for (const Node& l_oC : l_oN.m_vChildren) f_RestWalk(l_oC, l_mat4W, l_vOut);
}
const glm::mat4* f_RestOf(const std::vector<std::pair<const Node*, glm::mat4>>& l_vRest, const Node* l_pN) {
    for (const auto& l_pairR : l_vRest) if (l_pairR.first == l_pN) return &l_pairR.second;
    return nullptr;
}
const std::pair<const Node*, glm::mat4>* f_RestNamed(const std::vector<std::pair<const Node*, glm::mat4>>& l_vRest, const std::string& l_strName) {
    for (const auto& l_pairR : l_vRest) if (l_pairR.first->m_strName == l_strName) return &l_pairR;
    return nullptr;
}
float f_SegmentDistance(const glm::vec2& l_v2P, const glm::vec2& l_v2A, const glm::vec2& l_v2B) {
    const glm::vec2 l_v2Ab = l_v2B - l_v2A;
    const float l_fLen2 = glm::dot(l_v2Ab, l_v2Ab);
    const float l_fT = l_fLen2 > 1e-9f ? std::clamp(glm::dot(l_v2P - l_v2A, l_v2Ab) / l_fLen2, 0.0f, 1.0f) : 0.0f;
    return glm::length(l_v2P - (l_v2A + l_fT * l_v2Ab));
}
}  // namespace

void f_RestWorlds(const Clip& l_oClip, std::vector<std::pair<const Node*, glm::mat4>>& l_vOut) {
    l_vOut.clear();
    f_RestWalk(l_oClip.m_oRoot, glm::mat4(1.0f), l_vOut);
}

void f_AutoSkinWeights(const std::vector<float>& l_vRestVerts, const std::vector<glm::mat4>& l_vBoneRest,
                     const std::vector<glm::vec2>& l_vBoneEnd, std::vector<float>& l_vOut) {
    const size_t l_nVerts = l_vRestVerts.size() / 2, l_nBones = l_vBoneRest.size();
    l_vOut.assign(l_nVerts * 2 * g_iSkinInfluences, 0.0f);
    std::vector<std::pair<float, int>> l_vW(l_nBones);
    for (size_t l_nV = 0; l_nV < l_nVerts; l_nV++) {
        const glm::vec2 l_v2P(l_vRestVerts[2 * l_nV], l_vRestVerts[2 * l_nV + 1]);
        for (size_t l_nB = 0; l_nB < l_nBones; l_nB++) {
            const float l_fD = f_SegmentDistance(l_v2P, glm::vec2(l_vBoneRest[l_nB][3]), l_vBoneEnd[l_nB]);
            l_vW[l_nB] = {1.0f / (l_fD * l_fD + 1.0f), (int)l_nB};   // inverse square: the nearest bone leads, joints blend
        }
        std::sort(l_vW.begin(), l_vW.end(), [](const std::pair<float, int>& l_pairX, const std::pair<float, int>& l_pairY) {
            return l_pairX.first > l_pairY.first || (l_pairX.first == l_pairY.first && l_pairX.second < l_pairY.second);
        });
        const size_t l_nK = std::min<size_t>(l_nBones, g_iSkinInfluences);
        float l_fSum = 0;
        for (size_t l_nI = 0; l_nI < l_nK; l_nI++) l_fSum += l_vW[l_nI].first;
        for (size_t l_nI = 0; l_nI < l_nK; l_nI++) {
            l_vOut[l_nV * 2 * g_iSkinInfluences + 2 * l_nI] = (float)l_vW[l_nI].second;
            l_vOut[l_nV * 2 * g_iSkinInfluences + 2 * l_nI + 1] = l_fSum > 0 ? l_vW[l_nI].first / l_fSum : 0.0f;
        }
    }
}

namespace {
// A skinned pose's mesh bent for this frame, and how (what makeSkinnedMesh draws and skinView shows).
struct Bent {
    const toms::AtlasRegion* m_pRegion = nullptr;
    uint16_t m_u16Texture = g_u16SpriteAtlasTexture;
    const toms::SpriteMesh* m_pMesh = nullptr;
    toms::SpriteMesh m_oOwn;              // a deformed sprite without a mesh file: a plain grid over it
    SkinView m_oView;                     // placed vertices, weights, bones
    // what a SkinRig keeps (a skinned node): the mesh at rest in clip space, each bone's rest transform and pose (-1: none)
    std::vector<float> m_vRestV;
    std::vector<glm::mat4> m_vBoneRest;
    std::vector<int> m_vBonePose;
    std::vector<const Node*> m_vBoneNode;
};

// A plain grid of cols x rows cells over a region's packed pixels (every cell, transparent or not), in original pixels.
toms::SpriteMesh f_GridMesh(const toms::AtlasRegion& l_oR, int l_iCols, int l_iRows) {
    toms::SpriteMesh l_oM;
    for (int l_iY = 0; l_iY <= l_iRows; l_iY++)
        for (int l_iX = 0; l_iX <= l_iCols; l_iX++) {
            l_oM.m_vVertices.push_back((float)l_oR.m_iOffX + (float)l_oR.m_iW * l_iX / l_iCols);
            l_oM.m_vVertices.push_back((float)l_oR.m_iOffY + (float)l_oR.m_iH * l_iY / l_iRows);
        }
    for (int l_iY = 0; l_iY < l_iRows; l_iY++)
        for (int l_iX = 0; l_iX < l_iCols; l_iX++) {
            const uint16_t l_u16A = (uint16_t)(l_iY * (l_iCols + 1) + l_iX), l_u16B = (uint16_t)(l_u16A + 1), l_u16C = (uint16_t)(l_u16A + l_iCols + 2), l_u16D = (uint16_t)(l_u16A + l_iCols + 1);
            l_oM.m_vTriangles.insert(l_oM.m_vTriangles.end(), {l_u16A, l_u16B, l_u16C, l_u16A, l_u16C, l_u16D});
        }
    return l_oM;
}
}  // namespace

glm::vec2 f_LatticeOffset(const std::vector<glm::vec2>& l_vOffsets, int l_iCols, int l_iRows, float l_fX, float l_fY, float l_fOrigW, float l_fOrigH) {
    if (l_iCols < 2 || l_iRows < 2 || l_vOffsets.size() < (size_t)l_iCols * l_iRows || l_fOrigW <= 0 || l_fOrigH <= 0) return glm::vec2(0.0f);
    const float l_fU = std::clamp(l_fX / l_fOrigW, 0.0f, 1.0f) * (l_iCols - 1), l_fV = std::clamp(l_fY / l_fOrigH, 0.0f, 1.0f) * (l_iRows - 1);
    const int l_iC0 = std::min((int)l_fU, l_iCols - 2), l_iR0 = std::min((int)l_fV, l_iRows - 2);
    const float l_fFu = l_fU - l_iC0, l_fFv = l_fV - l_iR0;
    auto l_fnAt = [&](int l_iC, int l_iR) { return l_vOffsets[(size_t)l_iR * l_iCols + l_iC]; };
    return (l_fnAt(l_iC0, l_iR0) * (1 - l_fFu) + l_fnAt(l_iC0 + 1, l_iR0) * l_fFu) * (1 - l_fFv) + (l_fnAt(l_iC0, l_iR0 + 1) * (1 - l_fFu) + l_fnAt(l_iC0 + 1, l_iR0 + 1) * l_fFu) * l_fFv;
}

namespace {

// Automatic weights depend only on what they are made from -- the mesh at rest and the bones' rest segments -- so they
// are made once and kept by a hash of exactly that (an edit of the rest pose, another mesh: other weights). Without
// this, every skinned node made them again every frame.
const std::vector<float>& f_CachedAutoWeights(const std::vector<float>& l_vRestV, const std::vector<glm::mat4>& l_vBoneRest,
                                            const std::vector<glm::vec2>& l_vBoneEnd) {
    uint64_t l_u64H = 1469598103934665603ull;
    auto l_fnMix = [&](const void* l_pP, size_t l_nN) {
        const auto* l_pB = static_cast<const unsigned char*>(l_pP);
        for (size_t l_nI = 0; l_nI < l_nN; l_nI++) l_u64H = (l_u64H ^ l_pB[l_nI]) * 1099511628211ull;
    };
    l_fnMix(l_vRestV.data(), l_vRestV.size() * sizeof(float));
    for (const glm::mat4& l_mat4M : l_vBoneRest) l_fnMix(&l_mat4M[3][0], 2 * sizeof(float));
    l_fnMix(l_vBoneEnd.data(), l_vBoneEnd.size() * sizeof(glm::vec2));
    const size_t l_nN = l_vBoneRest.size();
    l_fnMix(&l_nN, sizeof l_nN);
    static thread_local std::unordered_map<uint64_t, std::vector<float>> l_mapCache;
    auto l_itIt = l_mapCache.find(l_u64H);
    if (l_itIt != l_mapCache.end()) return l_itIt->second;
    if (l_mapCache.size() > 512) l_mapCache.clear();   // an editor dragging bones at rest makes new ones: keep it bounded
    std::vector<float>& l_vW = l_mapCache[l_u64H];
    f_AutoSkinWeights(l_vRestV, l_vBoneRest, l_vBoneEnd, l_vW);
    return l_vW;
}

SkinState f_Bend(const std::vector<NodePose>& l_vPoses, size_t l_nI, const std::vector<std::pair<const Node*, glm::mat4>>& l_vRest,
               const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Place, Bent& l_oOut) {
    const NodePose& l_oP = l_vPoses[l_nI];
    if (!l_oP.m_pNode || !l_oP.m_pNode->f_UsesMesh()) return SkinState::NotSkinned;
    const Node& l_oN = *l_oP.m_pNode;
    const toms::AtlasRegion* l_pR = l_oP.m_strSprite.empty() ? nullptr : l_oAtlases.f_Find(l_oP.m_strSprite, &l_oOut.m_u16Texture);
    if (!l_pR || l_pR->m_iW <= 0 || l_pR->m_iH <= 0) return SkinState::NoSprite;
    if (l_pR->m_bRotated) return SkinState::Rotated;   // meshes are cut from unrotated pixels: drawn as a quad
    const toms::SpriteMesh* l_pMesh = l_oAtlases.f_FindMesh(l_oP.m_strSprite, l_oN);
    if ((!l_pMesh || l_pMesh->m_vTriangles.empty()) && !l_oN.f_Skinned()) {   // a deform only: a plain grid, twice the lattice
        l_oOut.m_oOwn = f_GridMesh(*l_pR, (l_oN.f_LatticeCols() - 1) * 2, (l_oN.f_LatticeRows() - 1) * 2);
        l_pMesh = &l_oOut.m_oOwn;
    }
    const glm::mat4* l_pNodeRest = f_RestOf(l_vRest, &l_oN);
    if (!l_pMesh || l_pMesh->m_vTriangles.empty() || (l_oN.f_Skinned() && !l_pNodeRest)) return SkinState::NoMesh;
    l_oOut.m_pRegion = l_pR;
    l_oOut.m_pMesh = l_pMesh;
    SkinView& l_oV = l_oOut.m_oView;
    auto l_fnPlaced = [&](const glm::vec2& l_v2C) { const glm::vec3 l_v3S = l_mat3Place * glm::vec3(l_v2C, 1.0f); return glm::vec2(l_v3S); };
    // Each vertex around the pivot, moved by the lattice (at the node's own time): the shape the rest works on.
    const glm::vec2 l_v2Pivot = l_oN.m_bHasPivot ? l_oN.m_v2Pivot : glm::vec2(l_pR->m_arrPivot[0], l_pR->m_arrPivot[1]);
    const uint32_t l_uVerts = l_pMesh->f_VertexCount();
    const std::vector<glm::vec2> l_vLattice = l_oN.f_Deformed() ? f_DeformAt(l_oN, l_oP.m_fTime) : std::vector<glm::vec2>();
    auto l_fnLocal = [&](uint32_t l_uK) {
        const float l_fX = l_pMesh->m_vVertices[2 * l_uK], l_fY = l_pMesh->m_vVertices[2 * l_uK + 1];
        glm::vec2 l_v2L(l_fX - l_v2Pivot.x * l_pR->m_iOrigW, l_fY - l_v2Pivot.y * l_pR->m_iOrigH);
        if (!l_vLattice.empty()) l_v2L += f_LatticeOffset(l_vLattice, l_oN.f_LatticeCols(), l_oN.f_LatticeRows(), l_fX, l_fY, (float)l_pR->m_iOrigW, (float)l_pR->m_iOrigH);
        return l_v2L;
    };
    if (!l_oN.f_Skinned()) {   // a deform only: the bent shape, carried by the node itself
        l_oV.m_vVertices.resize(l_uVerts);
        for (uint32_t l_uK = 0; l_uK < l_uVerts; l_uK++) l_oV.m_vVertices[l_uK] = l_fnPlaced(glm::vec2(l_oP.m_mat4World * glm::vec4(l_fnLocal(l_uK), 0, 1)));
        l_oV.m_vTriangles = l_pMesh->m_vTriangles;
        return SkinState::Ok;
    }

    // The bones: rest transform, where they are now (a bone left out of the poses, e.g. by an editor's solo,
    // stays at rest), and the segment the automatic weights measure against. A bone name no node has is the
    // skinned node itself.
    const size_t l_nBones = l_oN.m_vSkinBones.size();
    std::vector<glm::mat4> l_vBoneRest(l_nBones), l_vBoneSkin(l_nBones);
    std::vector<glm::vec2> l_vBoneEnd(l_nBones);
    l_oV.m_vBones = l_oN.m_vSkinBones;
    l_oV.m_vBoneFound.assign(l_nBones, false);
    for (size_t l_nB = 0; l_nB < l_nBones; l_nB++) {
        const auto* l_pNamed = f_RestNamed(l_vRest, l_oN.m_vSkinBones[l_nB]);
        l_oV.m_vBoneFound[l_nB] = l_pNamed != nullptr;
        const Node* l_pBn = l_pNamed ? l_pNamed->first : &l_oN;
        l_vBoneRest[l_nB] = l_pNamed ? l_pNamed->second : *l_pNodeRest;
        glm::mat4 l_mat4Now = l_vBoneRest[l_nB];
        int l_iAt = -1;
        for (size_t l_nQi = 0; l_nQi < l_vPoses.size(); l_nQi++)
            if (l_vPoses[l_nQi].m_pNode == l_pBn) { l_mat4Now = l_vPoses[l_nQi].m_mat4World; l_iAt = (int)l_nQi; break; }
        l_oOut.m_vBonePose.push_back(l_iAt);
        l_oOut.m_vBoneNode.push_back(l_pBn);
        l_vBoneSkin[l_nB] = std::fabs(glm::determinant(l_vBoneRest[l_nB])) > 1e-12f ? l_mat4Now * glm::inverse(l_vBoneRest[l_nB]) : glm::mat4(1.0f);
        // A bone reaches to its first child that is a bone; a last bone (a head, a hand) reaches on past its origin as
        // far as from its parent bone to it, so what lies beyond it follows it.
        l_vBoneEnd[l_nB] = glm::vec2(l_vBoneRest[l_nB][3]);
        bool l_bReaches = false;
        for (const Node& l_oC : l_pBn->m_vChildren)
            if (std::find(l_oN.m_vSkinBones.begin(), l_oN.m_vSkinBones.end(), l_oC.m_strName) != l_oN.m_vSkinBones.end())
                if (const glm::mat4* l_pCr = f_RestOf(l_vRest, &l_oC)) { l_vBoneEnd[l_nB] = glm::vec2((*l_pCr)[3]); l_bReaches = true; break; }
        if (!l_bReaches)
            for (const auto& l_pairRr : l_vRest)
                if (std::find(l_oN.m_vSkinBones.begin(), l_oN.m_vSkinBones.end(), l_pairRr.first->m_strName) != l_oN.m_vSkinBones.end() &&
                    std::any_of(l_pairRr.first->m_vChildren.begin(), l_pairRr.first->m_vChildren.end(), [&](const Node& l_oC) { return &l_oC == l_pBn; })) {
                    l_vBoneEnd[l_nB] = 2.0f * glm::vec2(l_vBoneRest[l_nB][3]) - glm::vec2(l_pairRr.second[3]);
                    break;
                }
        // The segment as it is now: carried by its own bone.
        l_oV.m_vBoneStart.push_back(l_fnPlaced(glm::vec2(l_vBoneSkin[l_nB] * glm::vec4(glm::vec2(l_vBoneRest[l_nB][3]), 0, 1))));
        l_oV.m_vBoneEnd.push_back(l_fnPlaced(glm::vec2(l_vBoneSkin[l_nB] * glm::vec4(l_vBoneEnd[l_nB], 0, 1))));
    }

    // The mesh at rest in clip space: its (deformed) vertices through the node's rest transform; the bones carry it.
    std::vector<float> l_vRestV((size_t)l_uVerts * 2);
    for (uint32_t l_uK = 0; l_uK < l_uVerts; l_uK++) {
        const glm::vec4 l_v4C = *l_pNodeRest * glm::vec4(l_fnLocal(l_uK), 0, 1);
        l_vRestV[2 * l_uK] = l_v4C.x;
        l_vRestV[2 * l_uK + 1] = l_v4C.y;
    }
    l_oOut.m_vBoneRest = l_vBoneRest;
    l_oV.m_bAutoWeights = l_oN.m_vSkinWeights.size() != (size_t)l_uVerts * 2 * g_iSkinInfluences;   // automatic, or painted for another mesh
    if (l_oV.m_bAutoWeights) l_oV.m_vWeights = f_CachedAutoWeights(l_vRestV, l_vBoneRest, l_vBoneEnd);
    else l_oV.m_vWeights = l_oN.m_vSkinWeights;

    l_oV.m_vVertices.resize(l_uVerts);
    for (uint32_t l_uK = 0; l_uK < l_uVerts; l_uK++) {
        const glm::vec4 l_v4RestPos(l_vRestV[2 * l_uK], l_vRestV[2 * l_uK + 1], 0, 1);
        glm::vec4 l_v4Acc(0.0f);
        float l_fSum = 0;
        for (int l_iJ = 0; l_iJ < g_iSkinInfluences; l_iJ++) {
            const float l_fW = l_oV.m_vWeights[(size_t)l_uK * 2 * g_iSkinInfluences + 2 * l_iJ + 1];
            const int l_iB = (int)l_oV.m_vWeights[(size_t)l_uK * 2 * g_iSkinInfluences + 2 * l_iJ];
            if (l_fW == 0.0f || l_iB < 0 || l_iB >= (int)l_nBones) continue;
            l_v4Acc += l_fW * (l_vBoneSkin[(size_t)l_iB] * l_v4RestPos);
            l_fSum += l_fW;
        }
        l_v4Acc = l_fSum > 0 ? l_v4Acc / l_fSum : l_v4RestPos;
        l_oV.m_vVertices[l_uK] = l_fnPlaced(glm::vec2(l_v4Acc));
    }
    l_oV.m_vTriangles = l_pMesh->m_vTriangles;
    l_oOut.m_vRestV = std::move(l_vRestV);
    return SkinState::Ok;
}
}  // namespace

struct SkinRigCache {
    struct Rig {
        std::string m_strSprite;                       // the sprite it was made for (sprite keys switch meshes)
        const toms::SpriteMesh* m_pMesh = nullptr;
        uint16_t m_u16Texture = g_u16SpriteAtlasTexture;
        std::vector<float> m_vRestV, m_vUv, m_vWeights;    // per vertex: at rest (clip space), texture, kSkinInfluences pairs
        std::vector<int> m_vBonePose;                // per bone: its pose, -1 = stays at rest
        std::vector<const Node*> m_vBoneNode;
        std::vector<glm::mat4> m_vRestInv;           // per bone: its rest transform inverted (identity when degenerate)
    };
    std::unordered_map<const Node*, std::vector<Rig>> m_mapRigs;
};

namespace {
// The shared caches, by clip and atlas set, held weakly: the players keep them alive. fresh: a new one replaces the
// shared one (the atlases were reloaded under the same AtlasSet; players still on the old one keep it until they ask
// again).
std::shared_ptr<SkinRigCache> f_SharedRigs(const Clip* l_pClip, const AtlasSet* l_pAtlases, bool l_bFresh) {
    static std::map<std::pair<const Clip*, const AtlasSet*>, std::weak_ptr<SkinRigCache>> l_mapShared;
    static int l_iCalls = 0;
    if (++l_iCalls % 256 == 0)   // now and then: forget the ones nobody plays any more
        for (auto l_itIt = l_mapShared.begin(); l_itIt != l_mapShared.end();) l_itIt = l_itIt->second.expired() ? l_mapShared.erase(l_itIt) : std::next(l_itIt);
    std::weak_ptr<SkinRigCache>& l_wpSlot = l_mapShared[{l_pClip, l_pAtlases}];
    std::shared_ptr<SkinRigCache> l_spC = l_bFresh ? nullptr : l_wpSlot.lock();
    if (!l_spC) {
        l_spC = std::make_shared<SkinRigCache>();
        l_wpSlot = l_spC;
    }
    return l_spC;
}
}  // namespace

bool f_MakeSkinnedMesh(const std::vector<NodePose>& l_vPoses, size_t l_nI, const std::vector<std::pair<const Node*, glm::mat4>>& l_vRest,
                     const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Place, const glm::vec4& l_v4T, DrawList& l_oOut, SkinRigCache* l_pRigs) {
    const NodePose& l_oP = l_vPoses[l_nI];
    if (!l_oP.m_bVisible || l_oP.m_v4Color.a <= 0.0f) return false;
    // A skinned node without a lattice, its rig made: only its bones are new this frame.
    const bool l_bCachable = l_pRigs && l_oP.m_pNode->f_Skinned() && !l_oP.m_pNode->f_Deformed();
    if (l_bCachable) {
        for (const SkinRigCache::Rig& l_oG : l_pRigs->m_mapRigs[l_oP.m_pNode]) {
            if (l_oG.m_strSprite != l_oP.m_strSprite) continue;
            const size_t l_nBones = l_oG.m_vBonePose.size();
            glm::mat4 l_arrSkin[16];
            for (size_t l_nB = 0; l_nB < l_nBones && l_nB < 16; l_nB++) {
                const int l_iAt = l_oG.m_vBonePose[l_nB];
                l_arrSkin[l_nB] = l_iAt >= 0 && (size_t)l_iAt < l_vPoses.size() && l_vPoses[(size_t)l_iAt].m_pNode == l_oG.m_vBoneNode[l_nB] ? l_vPoses[(size_t)l_iAt].m_mat4World * l_oG.m_vRestInv[l_nB]
                                                                                                          : glm::mat4(1.0f);
            }
            DrawList::MeshItem l_oItem;
            l_oItem.m_uFirstVertex = (uint32_t)l_oOut.m_vVertices.size();
            l_oItem.m_uVertexCount = l_oG.m_pMesh->f_VertexCount();
            l_oItem.m_uFirstIndex = (uint32_t)l_oOut.m_vIndices.size();
            l_oItem.m_uIndexCount = (uint32_t)l_oG.m_pMesh->m_vTriangles.size();
            for (uint32_t l_uV = 0; l_uV < l_oItem.m_uVertexCount; l_uV++) {
                const glm::vec4 l_v4RestPos(l_oG.m_vRestV[2 * l_uV], l_oG.m_vRestV[2 * l_uV + 1], 0, 1);
                glm::vec4 l_v4Acc(0.0f);
                float l_fSum = 0;
                for (int l_iJ = 0; l_iJ < g_iSkinInfluences; l_iJ++) {
                    const float l_fW = l_oG.m_vWeights[(size_t)l_uV * 2 * g_iSkinInfluences + 2 * l_iJ + 1];
                    const int l_iB = (int)l_oG.m_vWeights[(size_t)l_uV * 2 * g_iSkinInfluences + 2 * l_iJ];
                    if (l_fW == 0.0f || l_iB < 0 || l_iB >= (int)l_nBones || l_iB >= 16) continue;
                    l_v4Acc += l_fW * (l_arrSkin[l_iB] * l_v4RestPos);
                    l_fSum += l_fW;
                }
                l_v4Acc = l_fSum > 0 ? l_v4Acc / l_fSum : l_v4RestPos;
                const glm::vec3 l_v3S = l_mat3Place * glm::vec3(l_v4Acc.x, l_v4Acc.y, 1.0f);
                l_oOut.m_vVertices.push_back({l_v3S.x, l_v3S.y, l_oG.m_vUv[2 * l_uV], l_oG.m_vUv[2 * l_uV + 1]});
            }
            l_oOut.m_vIndices.insert(l_oOut.m_vIndices.end(), l_oG.m_pMesh->m_vTriangles.begin(), l_oG.m_pMesh->m_vTriangles.end());
            const glm::vec4 l_v4Col = l_oP.m_v4Color * l_v4T;
            l_oItem.m_arrTint[0] = l_v4Col.r; l_oItem.m_arrTint[1] = l_v4Col.g; l_oItem.m_arrTint[2] = l_v4Col.b; l_oItem.m_arrTint[3] = l_v4Col.a;
            l_oItem.m_bAdditive = l_oP.m_pNode->m_eBlend == Blend::Add;
            l_oItem.m_u16Texture = l_oG.m_u16Texture;
            l_oOut.m_vOrder.push_back({DrawList::MeshItemKind, (uint32_t)l_oOut.m_vMeshes.size()});
            l_oOut.m_vMeshes.push_back(l_oItem);
            return true;
        }
    }
    Bent l_oB;
    if (f_Bend(l_vPoses, l_nI, l_vRest, l_oAtlases, l_mat3Place, l_oB) != SkinState::Ok) return false;
    const toms::AtlasRegion* l_pR = l_oB.m_pRegion;
    const toms::SpriteMesh* l_pMesh = l_oB.m_pMesh;
    DrawList::MeshItem l_oItem;
    l_oItem.m_uFirstVertex = (uint32_t)l_oOut.m_vVertices.size();
    l_oItem.m_uVertexCount = l_pMesh->f_VertexCount();
    l_oItem.m_uFirstIndex = (uint32_t)l_oOut.m_vIndices.size();
    l_oItem.m_uIndexCount = (uint32_t)l_pMesh->m_vTriangles.size();
    // The UVs stay inside the region: a mesh cut from another packing or art style (the game's sprite atlas follows the
    // chosen style, a clip file has one mesh file) must not sample the neighbouring sprites.
    const float l_fDu = (l_pR->m_arrUv[2] - l_pR->m_arrUv[0]) / l_pR->m_iW, l_fDv = (l_pR->m_arrUv[3] - l_pR->m_arrUv[1]) / l_pR->m_iH;
    SkinRigCache::Rig* l_pRig = nullptr;
    if (l_bCachable && l_oB.m_vBoneRest.size() <= 16) {   // keep what the next frames need
        l_pRigs->m_mapRigs[l_oP.m_pNode].push_back(SkinRigCache::Rig());
        l_pRig = &l_pRigs->m_mapRigs[l_oP.m_pNode].back();
        l_pRig->m_strSprite = l_oP.m_strSprite;
        l_pRig->m_pMesh = l_pMesh;
        l_pRig->m_u16Texture = l_oB.m_u16Texture;
        l_pRig->m_vRestV = l_oB.m_vRestV;
        l_pRig->m_vWeights = l_oB.m_oView.m_vWeights;
        l_pRig->m_vBonePose = l_oB.m_vBonePose;
        l_pRig->m_vBoneNode = l_oB.m_vBoneNode;
        for (const glm::mat4& l_mat4M : l_oB.m_vBoneRest)
            l_pRig->m_vRestInv.push_back(std::fabs(glm::determinant(l_mat4M)) > 1e-12f ? glm::inverse(l_mat4M) : glm::mat4(1.0f));
    }
    for (uint32_t l_uV = 0; l_uV < l_oItem.m_uVertexCount; l_uV++) {
        const float l_fPx = std::clamp(l_pMesh->m_vVertices[2 * l_uV] - l_pR->m_iOffX, 0.0f, (float)l_pR->m_iW);
        const float l_fPy = std::clamp(l_pMesh->m_vVertices[2 * l_uV + 1] - l_pR->m_iOffY, 0.0f, (float)l_pR->m_iH);
        l_oOut.m_vVertices.push_back({l_oB.m_oView.m_vVertices[l_uV].x, l_oB.m_oView.m_vVertices[l_uV].y, l_pR->m_arrUv[0] + l_fPx * l_fDu, l_pR->m_arrUv[1] + l_fPy * l_fDv});
        if (l_pRig) { l_pRig->m_vUv.push_back(l_pR->m_arrUv[0] + l_fPx * l_fDu); l_pRig->m_vUv.push_back(l_pR->m_arrUv[1] + l_fPy * l_fDv); }
    }
    l_oOut.m_vIndices.insert(l_oOut.m_vIndices.end(), l_pMesh->m_vTriangles.begin(), l_pMesh->m_vTriangles.end());
    const glm::vec4 l_v4Col = l_oP.m_v4Color * l_v4T;
    l_oItem.m_arrTint[0] = l_v4Col.r; l_oItem.m_arrTint[1] = l_v4Col.g; l_oItem.m_arrTint[2] = l_v4Col.b; l_oItem.m_arrTint[3] = l_v4Col.a;
    l_oItem.m_bAdditive = l_oP.m_pNode->m_eBlend == Blend::Add;
    l_oItem.m_u16Texture = l_oB.m_u16Texture;
    l_oOut.m_vOrder.push_back({DrawList::MeshItemKind, (uint32_t)l_oOut.m_vMeshes.size()});
    l_oOut.m_vMeshes.push_back(l_oItem);
    return true;
}

SkinState f_SkinView(const Clip& l_oClip, const std::vector<NodePose>& l_vPoses, size_t l_nI, const AtlasSet& l_oAtlases,
                   const glm::mat3& l_mat3Place, SkinView* l_pOut) {
    if (l_nI >= l_vPoses.size()) return SkinState::NotSkinned;
    std::vector<std::pair<const Node*, glm::mat4>> l_vRest;
    f_RestWorlds(l_oClip, l_vRest);
    Bent l_oB;
    const SkinState l_eS = f_Bend(l_vPoses, l_nI, l_vRest, l_oAtlases, l_mat3Place, l_oB);
    if (l_pOut) *l_pOut = l_eS == SkinState::Ok ? std::move(l_oB.m_oView) : SkinView();
    return l_eS;
}

namespace {
void f_FindMissingMeshes(const Node& l_oN, const std::string& l_strParent, const AtlasSet& l_oAtlases, std::vector<MissingMesh>& l_vOut) {
    const std::string l_strPath = l_strParent.empty() ? l_oN.m_strName : l_strParent + "/" + l_oN.m_strName;
    if (l_oN.f_Skinned()) {
        std::vector<std::string> l_vSprites;
        if (!l_oN.m_strSprite.empty()) l_vSprites.push_back(l_oN.m_strSprite);
        for (const auto& l_oK : l_oN.m_vSpriteKeys)
            if (!l_oK.m_oV.empty()) l_vSprites.push_back(l_oK.m_oV);
        std::sort(l_vSprites.begin(), l_vSprites.end());
        l_vSprites.erase(std::unique(l_vSprites.begin(), l_vSprites.end()), l_vSprites.end());
        for (const std::string& l_strS : l_vSprites) {
            if (!l_oAtlases.f_Find(l_strS)) continue;
            const toms::SpriteMesh* l_pM = l_oAtlases.f_FindMesh(l_strS, l_oN);
            if (!l_pM || l_pM->m_vTriangles.empty()) l_vOut.push_back({l_strPath, l_strS});
        }
    }
    for (const Node& l_oC : l_oN.m_vChildren) f_FindMissingMeshes(l_oC, l_strPath, l_oAtlases, l_vOut);
}
}  // namespace

std::vector<MissingMesh> f_SkinnedWithoutMesh(const Clip& l_oClip, const AtlasSet& l_oAtlases) {
    std::vector<MissingMesh> l_vOut;
    f_FindMissingMeshes(l_oClip.m_oRoot, std::string(), l_oAtlases, l_vOut);
    return l_vOut;
}

// ---- effects (docs/engine/18_NODE_EFFECTS.md) ---------------------------------------------------

namespace {
void f_DrawPose(const Clip& l_oClip, const std::vector<NodePose>& l_vPoses, size_t l_nI, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Place,
              const glm::vec4& l_v4T, std::vector<std::pair<const Node*, glm::mat4>>& l_vRest, bool& l_bRestMade, DrawList& l_oOut,
              std::vector<std::string>* l_pMissing);

float f_PlacementScale(const glm::mat3& l_mat3M) { return std::sqrt(std::fabs(l_mat3M[0][0] * l_mat3M[1][1] - l_mat3M[1][0] * l_mat3M[0][1])); }

// Effects around subtrees: before pose j, the afterimages and layers of every node whose subtree starts at j (outer
// nodes first); after pose j, the layers whose subtree ends there (inner first). A subtree's poses are contiguous
// (NodePose::first / end), so its layer holds exactly it.
struct EffectWalk {
    const Clip& m_oClip;
    const std::vector<NodePose>& m_vPoses;
    bool m_bAny = false;
    std::vector<size_t> m_vNodes;      // poses with an effect or a trail that shows, by first pose, outer first
    size_t m_nNext = 0;
    std::vector<uint32_t> m_vOpen;     // the open layers' ends

    EffectWalk(const Clip& l_oC, const std::vector<NodePose>& l_vP, bool l_bEffects) : m_oClip(l_oC), m_vPoses(l_vP) {
        if (!l_bEffects) return;
        for (size_t l_nI = 0; l_nI < l_vP.size(); l_nI++) {
            const NodePose& l_oQ = l_vP[l_nI];
            if (!l_oQ.m_pNode || !l_oQ.m_bVisible || l_oQ.m_v4Color.a <= 0.0f) continue;   // hidden: so is its subtree
            if (l_oQ.m_pNode->f_HasFx() || (l_oQ.m_pNode->f_HasTrail() && f_TrailAt(*l_oQ.m_pNode, l_oQ.m_fTime) > 0.0f)) m_vNodes.push_back(l_nI);
        }
        std::stable_sort(m_vNodes.begin(), m_vNodes.end(), [&](size_t l_nA, size_t l_nB) {
            return l_vP[l_nA].m_uFirst != l_vP[l_nB].m_uFirst ? l_vP[l_nA].m_uFirst < l_vP[l_nB].m_uFirst : l_vP[l_nA].m_iDepth < l_vP[l_nB].m_iDepth;
        });
        m_bAny = !m_vNodes.empty();
    }
    void f_Before(size_t l_nJ, const AtlasSet& l_oAtlases, const std::vector<std::pair<const Node*, glm::mat4>>& l_vRest,
                const glm::mat3& l_mat3Place, const glm::vec4& l_v4T, DrawList& l_oOut) {
        for (; m_nNext < m_vNodes.size() && m_vPoses[m_vNodes[m_nNext]].m_uFirst == l_nJ; m_nNext++) {
            const size_t l_nI = m_vNodes[m_nNext];
            const Node& l_oN = *m_vPoses[l_nI].m_pNode;
            if (l_oN.f_HasTrail()) f_AppendTrail(m_oClip, m_vPoses, l_nI, l_oAtlases, l_vRest, l_mat3Place, l_v4T, l_oOut);   // behind it, outside its layer
            LayerEffect l_oFx;
            if (l_oN.f_HasFx() && f_NodeLayerEffect(m_oClip, m_vPoses, l_nI, l_mat3Place, l_oFx)) {
                l_oOut.f_BeginLayer(l_oFx);
                m_vOpen.push_back(m_vPoses[l_nI].m_uEnd);
            }
        }
    }
    void f_After(size_t l_nJ, DrawList& l_oOut) {
        while (!m_vOpen.empty() && m_vOpen.back() <= l_nJ + 1) {
            l_oOut.f_EndLayer();
            m_vOpen.pop_back();
        }
    }
};
}  // namespace

bool f_NodeLayerEffect(const Clip& l_oClip, const std::vector<NodePose>& l_vPoses, size_t l_nI, const glm::mat3& l_mat3Place, LayerEffect& l_oOut) {
    if (l_nI >= l_vPoses.size() || !l_vPoses[l_nI].m_pNode || !l_vPoses[l_nI].m_pNode->f_HasFx()) return false;
    const NodePose& l_oP = l_vPoses[l_nI];
    if (!l_oP.m_bVisible || l_oP.m_v4Color.a <= 0.0f) return false;
    const Node& l_oN = *l_oP.m_pNode;
    const glm::vec4 l_v4V = f_FxParamsAt(l_oN, l_oP.m_fTime);
    const float l_fScale = f_PlacementScale(l_mat3Place);
    l_oOut = LayerEffect();
    for (int l_iK = 0; l_iK < 4; l_iK++) l_oOut.m_arrColor[l_iK] = l_oN.m_oFx.m_v4Color[l_iK];
    switch (l_oN.m_oFx.m_eShader) {
    case FxShader::None: return false;
    case FxShader::Group: l_oOut.m_eKind = LayerEffect::None; break;
    case FxShader::MotionBlur: {
        l_oOut.m_eKind = LayerEffect::MotionBlur;
        glm::vec2 l_v2D(l_mat3Place * glm::vec3(l_v4V.x, l_v4V.y, 0.0f));   // a vector: the placement's turn and scale, not its move
        if (l_v4V.z > 0.0f) {   // + its own movement over the last 1/30 s
            static thread_local std::vector<NodePose> l_vWas;   // reused: no allocation a frame
            f_Evaluate(l_oClip, std::max(0.0f, l_oP.m_fClipTime - 1.0f / 30.0f), l_vWas);
            if (l_nI < l_vWas.size() && l_vWas[l_nI].m_pNode == l_oP.m_pNode) {
                const glm::vec2 l_v2A(l_mat3Place * f_Flatten(l_vWas[l_nI].m_mat4World) * glm::vec3(0, 0, 1)), l_v2B(l_mat3Place * f_Flatten(l_oP.m_mat4World) * glm::vec3(0, 0, 1));
                l_v2D += l_v4V.z * (l_v2B - l_v2A);
            }
        }
        if (glm::length(l_v2D) < 0.5f) return false;   // nothing to smear: drawn as it is
        l_oOut.m_arrParams[0] = l_v2D.x;
        l_oOut.m_arrParams[1] = l_v2D.y;
        break;
    }
    case FxShader::Glow:
        l_oOut.m_eKind = LayerEffect::Glow;
        l_oOut.m_arrParams[0] = l_v4V.x;
        l_oOut.m_arrParams[1] = l_v4V.y * l_fScale;
        l_oOut.m_arrParams[2] = l_v4V.z;
        l_oOut.m_arrParams[3] = l_v4V.w;
        break;
    case FxShader::Flash:
        if (l_v4V.x <= 0.0f) return false;
        l_oOut.m_eKind = LayerEffect::Flash;
        l_oOut.m_arrParams[0] = l_v4V.x;
        break;
    case FxShader::Outline:
        if (l_v4V.x <= 0.0f || l_oN.m_oFx.m_v4Color.a <= 0.0f) return false;
        l_oOut.m_eKind = LayerEffect::Outline;
        l_oOut.m_arrParams[0] = l_v4V.x * l_fScale;
        l_oOut.m_arrParams[1] = l_v4V.y;
        break;
    }
    return true;
}

void f_AppendTrail(const Clip& l_oClip, const std::vector<NodePose>& l_vPoses, size_t l_nI, const AtlasSet& l_oAtlases,
                 const std::vector<std::pair<const Node*, glm::mat4>>& l_vRest, const glm::mat3& l_mat3Place, const glm::vec4& l_v4T,
                 DrawList& l_oOut) {
    if (l_nI >= l_vPoses.size() || !l_vPoses[l_nI].m_pNode) return;
    const NodePose& l_oP = l_vPoses[l_nI];
    const NodeTrail& l_oTr = l_oP.m_pNode->m_oTrail;
    const float l_fStrength = f_TrailAt(*l_oP.m_pNode, l_oP.m_fTime);
    if (l_oTr.m_iCount <= 0 || l_oTr.m_fGap <= 0.0f || l_fStrength <= 0.0f) return;
    static thread_local std::vector<NodePose> l_vGhost;   // reused for every afterimage: no allocation a frame
    for (int l_iK = std::min(l_oTr.m_iCount, g_iMaxTrail); l_iK >= 1; l_iK--) {   // the oldest first: the newer ones over it
        const float l_fWhen = l_oP.m_fClipTime - (float)l_iK * l_oTr.m_fGap;
        if (l_fWhen < 0.0f) continue;
        f_Evaluate(l_oClip, l_fWhen, l_vGhost);
        if (l_nI >= l_vGhost.size() || l_vGhost[l_nI].m_pNode != l_oP.m_pNode) continue;
        const float l_fFade = (float)(l_oTr.m_iCount - l_iK + 1) / (float)l_oTr.m_iCount;
        const glm::vec4 l_v4Tint = l_v4T * glm::vec4(l_oTr.m_v4Color.r, l_oTr.m_v4Color.g, l_oTr.m_v4Color.b, l_oTr.m_v4Color.a * l_fStrength * l_fFade);
        for (uint32_t l_uG = l_vGhost[l_nI].m_uFirst; l_uG < l_vGhost[l_nI].m_uEnd && l_uG < l_vGhost.size(); l_uG++) {
            const size_t l_nQuads = l_oOut.m_vQuads.size(), l_nMeshes = l_oOut.m_vMeshes.size();
            if (!(l_vGhost[l_uG].m_pNode && l_vGhost[l_uG].m_pNode->f_UsesMesh() && f_MakeSkinnedMesh(l_vGhost, l_uG, l_vRest, l_oAtlases, l_mat3Place, l_v4Tint, l_oOut))) {
                Quad l_oQ;
                if (f_MakeQuad(l_vGhost[l_uG], l_oAtlases, l_mat3Place, l_v4Tint, l_oQ)) l_oOut.f_AddQuad(l_oQ);
            }
            if (l_oTr.m_bAdditive) {
                for (size_t l_n2 = l_nQuads; l_n2 < l_oOut.m_vQuads.size(); l_n2++) l_oOut.m_vQuads[l_n2].m_bAdditive = true;
                for (size_t l_n2 = l_nMeshes; l_n2 < l_oOut.m_vMeshes.size(); l_n2++) l_oOut.m_vMeshes[l_n2].m_bAdditive = true;
            }
        }
    }
}

void f_AppendDraws(const Clip& l_oClip, const std::vector<NodePose>& l_vPoses, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Place,
                 const float l_arrTint[4], DrawList& l_oOut, std::vector<std::string>* l_pMissing) {
    const glm::vec4 l_v4T = l_arrTint ? glm::vec4(l_arrTint[0], l_arrTint[1], l_arrTint[2], l_arrTint[3]) : glm::vec4(1.0f);
    std::vector<std::pair<const Node*, glm::mat4>> l_vRest;
    bool l_bRestMade = false;
    EffectWalk l_oFx(l_oClip, l_vPoses, f_HasEffects(l_oClip));
    for (size_t l_nI = 0; l_nI < l_vPoses.size(); l_nI++) {
        if (l_oFx.m_bAny) {
            if (!l_bRestMade) { f_RestWorlds(l_oClip, l_vRest); l_bRestMade = true; }
            l_oFx.f_Before(l_nI, l_oAtlases, l_vRest, l_mat3Place, l_v4T, l_oOut);
        }
        f_DrawPose(l_oClip, l_vPoses, l_nI, l_oAtlases, l_mat3Place, l_v4T, l_vRest, l_bRestMade, l_oOut, l_pMissing);
        if (l_oFx.m_bAny) l_oFx.f_After(l_nI, l_oOut);
    }
}

namespace {
void f_DrawPose(const Clip& l_oClip, const std::vector<NodePose>& l_vPoses, size_t l_nI, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Place,
              const glm::vec4& l_v4T, std::vector<std::pair<const Node*, glm::mat4>>& l_vRest, bool& l_bRestMade, DrawList& l_oOut,
              std::vector<std::string>* l_pMissing) {
    Quad l_oQ;
    {
        const NodePose& l_oP = l_vPoses[l_nI];
        if (l_oP.m_pNode && l_oP.m_pNode->f_UsesMesh()) {
            if (!l_bRestMade) { f_RestWorlds(l_oClip, l_vRest); l_bRestMade = true; }
            if (f_MakeSkinnedMesh(l_vPoses, l_nI, l_vRest, l_oAtlases, l_mat3Place, l_v4T, l_oOut)) return;
        }
        const toms::AtlasRegion* l_pR = nullptr;
        if (f_MakeQuad(l_oP, l_oAtlases, l_mat3Place, l_v4T, l_oQ, &l_pR)) {
            l_oOut.f_AddQuad(l_oQ);
        } else if (!l_pR && l_pMissing && l_oP.m_bVisible && !l_oP.m_strSprite.empty() && l_oP.m_v4Color.a > 0.0f &&
                   std::find(l_pMissing->begin(), l_pMissing->end(), l_oP.m_strSprite) == l_pMissing->end()) {
            l_pMissing->push_back(l_oP.m_strSprite);
        }
    }
}
}  // namespace

bool f_MakeQuad(const NodePose& l_oP, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Place, const glm::vec4& l_v4T, Quad& l_oQ,
              const toms::AtlasRegion** l_pRegion) {
    if (l_pRegion) *l_pRegion = nullptr;
    if (!l_oP.m_bVisible || l_oP.m_strSprite.empty() || l_oP.m_v4Color.a <= 0.0f) return false;
    uint16_t l_u16Texture = g_u16SpriteAtlasTexture;
    const toms::AtlasRegion* l_pR = l_oAtlases.f_Find(l_oP.m_strSprite, &l_u16Texture);
    if (l_pRegion) *l_pRegion = l_pR;
    if (!l_pR) return false;
    // The packed (trimmed) pixels, placed inside the original image, relative to the pivot.
    const glm::vec2 l_v2Pivot = l_oP.m_pNode->m_bHasPivot ? l_oP.m_pNode->m_v2Pivot : glm::vec2(l_pR->m_arrPivot[0], l_pR->m_arrPivot[1]);
    const float l_fX0 = l_pR->m_iOffX - l_v2Pivot.x * l_pR->m_iOrigW, l_fY0 = l_pR->m_iOffY - l_v2Pivot.y * l_pR->m_iOrigH;
    const float l_fX1 = l_fX0 + l_pR->m_iW, l_fY1 = l_fY0 + l_pR->m_iH;
    const glm::mat3 l_mat3M = l_mat3Place * f_Flatten(l_oP.m_mat4World);   // the orthographic screen camera
    const glm::vec3 l_arrC[4] = {l_mat3M * glm::vec3(l_fX0, l_fY0, 1), l_mat3M * glm::vec3(l_fX1, l_fY0, 1), l_mat3M * glm::vec3(l_fX1, l_fY1, 1),
                            l_mat3M * glm::vec3(l_fX0, l_fY1, 1)};
    l_oQ = Quad();
    l_oQ.m_bHasCorners = true;
    float l_fMinX = l_arrC[0].x, l_fMinY = l_arrC[0].y, l_fMaxX = l_arrC[0].x, l_fMaxY = l_arrC[0].y;
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        l_oQ.m_arrCorners[l_iI * 2] = l_arrC[l_iI].x;
        l_oQ.m_arrCorners[l_iI * 2 + 1] = l_arrC[l_iI].y;
        l_fMinX = std::min(l_fMinX, l_arrC[l_iI].x); l_fMaxX = std::max(l_fMaxX, l_arrC[l_iI].x);
        l_fMinY = std::min(l_fMinY, l_arrC[l_iI].y); l_fMaxY = std::max(l_fMaxY, l_arrC[l_iI].y);
    }
    l_oQ.m_arrRect[0] = l_fMinX; l_oQ.m_arrRect[1] = l_fMinY; l_oQ.m_arrRect[2] = l_fMaxX - l_fMinX; l_oQ.m_arrRect[3] = l_fMaxY - l_fMinY;   // bounds
    std::copy(l_pR->m_arrUv, l_pR->m_arrUv + 4, l_oQ.m_arrUv);
    const glm::vec4 l_v4Col = l_oP.m_v4Color * l_v4T;
    l_oQ.m_arrTint[0] = l_v4Col.r; l_oQ.m_arrTint[1] = l_v4Col.g; l_oQ.m_arrTint[2] = l_v4Col.b; l_oQ.m_arrTint[3] = l_v4Col.a;
    l_oQ.m_bAdditive = l_oP.m_pNode->m_eBlend == Blend::Add;
    l_oQ.m_u16Texture = l_u16Texture;
    return true;
}

void f_AppendQuads(const std::vector<NodePose>& l_vPoses, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Place,
                 const float l_arrTint[4], std::vector<Quad>& l_vOut, std::vector<std::string>* l_pMissing) {
    const glm::vec4 l_v4T = l_arrTint ? glm::vec4(l_arrTint[0], l_arrTint[1], l_arrTint[2], l_arrTint[3]) : glm::vec4(1.0f);
    Quad l_oQ;
    for (const NodePose& l_oP : l_vPoses) {
        const toms::AtlasRegion* l_pR = nullptr;
        if (f_MakeQuad(l_oP, l_oAtlases, l_mat3Place, l_v4T, l_oQ, &l_pR)) {
            l_vOut.push_back(l_oQ);
        } else if (!l_pR && l_pMissing && l_oP.m_bVisible && !l_oP.m_strSprite.empty() && l_oP.m_v4Color.a > 0.0f &&
                   std::find(l_pMissing->begin(), l_pMissing->end(), l_oP.m_strSprite) == l_pMissing->end()) {
            l_pMissing->push_back(l_oP.m_strSprite);
        }
    }
}

// ---- AnimPlayer --------------------------------------------------------------------------------

void AnimPlayer::f_Play(const Clip* l_pClip) {
    m_pClip = l_pClip;
    m_oCache.f_Bind(l_pClip);
    m_vRest.clear();
    if (l_pClip) f_RestWorlds(*l_pClip, m_vRest);
    m_bLoops = l_pClip && f_HasLoopingNodes(*l_pClip);
    m_bEffects = l_pClip && f_HasEffects(*l_pClip);
    m_spRigs.reset();   // the shared one is asked for at the first draw (it needs the atlas set)
    m_pRigAtlases = nullptr;
    m_bRigsStale = false;
    m_pQuadAtlases = nullptr;
    m_dElapsed = 0;
    m_bStarted = false;
    m_vEvents.clear();
}

double AnimPlayer::f_Total() const {
    if (!m_pClip || m_pClip->m_iPlayCount < 0) return -1;
    return (double)m_pClip->m_iPlayCount * m_pClip->f_Duration();
}

bool AnimPlayer::f_Finished() const {
    const double l_dTot = f_Total();
    return m_pClip && l_dTot >= 0 && m_dElapsed >= l_dTot;
}

bool AnimPlayer::f_Showing() const { return m_pClip && (!f_Finished() || m_pClip->m_bStayAtLastFrame); }

float AnimPlayer::f_Time() const {
    if (!m_pClip) return 0;
    const double l_dD = m_pClip->f_Duration();
    if (l_dD <= 0) return 0;
    if (f_Finished()) return (float)l_dD;
    return (float)std::fmod(m_dElapsed, l_dD);
}

float AnimPlayer::f_PoseTime() const {
    if (!m_pClip || !f_Finished() || !m_bLoops) return f_Time();
    const double l_dD = m_pClip->f_Duration();
    return (float)(m_dElapsed - (double)(m_pClip->m_iPlayCount - 1) * l_dD);   // into the last play, past its end
}

void AnimPlayer::f_Seek(double l_dSeconds) {
    if (!m_pClip) return;
    m_dElapsed = std::max(0.0, l_dSeconds);
    m_bStarted = true;
    m_vEvents.clear();
}

void AnimPlayer::f_Update(int l_iDtMs) {
    if (!m_pClip) return;
    const double l_dD = m_pClip->f_Duration();
    const double l_dTot = f_Total();
    if (!m_bStarted) {   // keys at time 0 belong to the first update
        f_EventsBetween(*m_pClip, -1.0f, 0.0f, m_vEvents);
        m_bStarted = true;
    }
    double l_dA = m_dElapsed;
    m_dElapsed += std::max(0, l_iDtMs) / 1000.0;
    const double l_dB = l_dTot >= 0 ? std::min(m_dElapsed, l_dTot) : m_dElapsed;
    if (l_dD <= 0) return;
    // Walk the passed time one play at a time, so every event in it is sent once, in order.
    while (l_dA < l_dB) {
        double l_dLoopStart = std::floor(l_dA / l_dD) * l_dD;
        if (l_dLoopStart + l_dD <= l_dA) l_dLoopStart += l_dD;   // rounding put `a` on a boundary: that is the next play
        const double l_dEnd = std::min(l_dB, l_dLoopStart + l_dD);
        const float l_fT0 = l_dA == l_dLoopStart && l_dA > 0 ? -1.0f : (float)(l_dA - l_dLoopStart);   // a new play: include time 0
        f_EventsBetween(*m_pClip, l_fT0, (float)(l_dEnd - l_dLoopStart), m_vEvents);
        l_dA = l_dEnd;
    }
}

std::vector<std::string> AnimPlayer::f_TakeEvents() {
    std::vector<std::string> l_vOut;
    l_vOut.swap(m_vEvents);
    return l_vOut;
}

void AnimPlayer::f_Draw(IRenderer* l_pRen, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Place, const float l_arrTint[4]) const {
    if (!l_pRen || !f_Showing()) return;
    m_oCache.f_Seek(f_PoseTime());
    const std::vector<NodePose>& l_vPoses = m_oCache.f_Poses();
    const glm::vec4 l_v4T = l_arrTint ? glm::vec4(l_arrTint[0], l_arrTint[1], l_arrTint[2], l_arrTint[3]) : glm::vec4(1.0f);
    const bool l_bAll = m_vQuads.size() != l_vPoses.size() || m_pQuadAtlases != &l_oAtlases || m_mat3QuadPlace != l_mat3Place || m_v4QuadTint != l_v4T;
    if (l_bAll) {
        m_vQuads.assign(l_vPoses.size(), Quad());
        m_vHasQuad.assign(l_vPoses.size(), 0);
        m_pQuadAtlases = &l_oAtlases;
        m_mat3QuadPlace = l_mat3Place;
        m_v4QuadTint = l_v4T;
    }
    // The frame's draws in order -- the cached quads, the bent meshes, effect layers and afterimages -- then submitted
    // together, so the parts of one texture go as one batch.
    DrawList& l_oFrame = m_oMeshScratch;
    l_oFrame.f_Clear();
    if (m_pRigAtlases != &l_oAtlases || !m_spRigs) {   // the clip's shared cache for these atlases (a reload: a new one)
        m_spRigs = f_SharedRigs(m_pClip, &l_oAtlases, m_bRigsStale);
        m_pRigAtlases = &l_oAtlases;
        m_bRigsStale = false;
    }
    EffectWalk l_oFx(*m_pClip, l_vPoses, m_bEffects);
    for (size_t l_nI = 0; l_nI < l_vPoses.size(); l_nI++) {
        if (l_oFx.m_bAny) l_oFx.f_Before(l_nI, l_oAtlases, m_vRest, l_mat3Place, l_v4T, l_oFrame);   // afterimages and effect layers that start here
        bool l_bDrawn = false;
        if (l_vPoses[l_nI].m_pNode && l_vPoses[l_nI].m_pNode->f_UsesMesh())   // bent again every frame: its bones / lattice may move
            l_bDrawn = f_MakeSkinnedMesh(l_vPoses, l_nI, m_vRest, l_oAtlases, l_mat3Place, l_v4T, l_oFrame, m_spRigs.get());
        if (!l_bDrawn) {
            if (l_bAll || m_oCache.f_Changed(l_nI)) m_vHasQuad[l_nI] = f_MakeQuad(l_vPoses[l_nI], l_oAtlases, l_mat3Place, l_v4T, m_vQuads[l_nI]);
            if (m_vHasQuad[l_nI]) l_oFrame.f_AddQuad(m_vQuads[l_nI]);
        }
        if (l_oFx.m_bAny) l_oFx.f_After(l_nI, l_oFrame);
    }
    l_oFrame.f_Submit(l_pRen);
}

// ---- the lattice --------------------------------------------------------------------------------

bool f_LatticeView(const std::vector<NodePose>& l_vPoses, size_t l_nI, const AtlasSet& l_oAtlases, const glm::mat3& l_mat3Place, LatticeView& l_oOut) {
    if (l_nI >= l_vPoses.size() || !l_vPoses[l_nI].m_pNode || l_vPoses[l_nI].m_strSprite.empty()) return false;
    const NodePose& l_oP = l_vPoses[l_nI];
    const Node& l_oN = *l_oP.m_pNode;
    const toms::AtlasRegion* l_pR = l_oAtlases.f_Find(l_oP.m_strSprite);
    if (!l_pR || l_pR->m_iOrigW <= 0 || l_pR->m_iOrigH <= 0) return false;
    l_oOut.m_iCols = l_oN.f_LatticeCols();
    l_oOut.m_iRows = l_oN.f_LatticeRows();
    const glm::vec2 l_v2Pivot = l_oN.m_bHasPivot ? l_oN.m_v2Pivot : glm::vec2(l_pR->m_arrPivot[0], l_pR->m_arrPivot[1]);
    // sprite pixels -> placed: around the pivot, through the node's world transform, placed
    glm::mat3 l_mat3ToLocal(1.0f);
    l_mat3ToLocal[2] = glm::vec3(-l_v2Pivot.x * l_pR->m_iOrigW, -l_v2Pivot.y * l_pR->m_iOrigH, 1.0f);
    l_oOut.m_mat3SpriteToPlaced = l_mat3Place * f_Flatten(l_oP.m_mat4World) * l_mat3ToLocal;
    const std::vector<glm::vec2> l_vOffsets = f_DeformAt(l_oN, l_oP.m_fTime);
    l_oOut.m_vRest.clear();
    l_oOut.m_vPoints.clear();
    for (int l_iRow = 0; l_iRow < l_oOut.m_iRows; l_iRow++)
        for (int l_iCol = 0; l_iCol < l_oOut.m_iCols; l_iCol++) {
            const glm::vec2 l_v2At((float)l_pR->m_iOrigW * l_iCol / (l_oOut.m_iCols - 1), (float)l_pR->m_iOrigH * l_iRow / (l_oOut.m_iRows - 1));
            l_oOut.m_vRest.push_back(l_v2At);
            l_oOut.m_vPoints.push_back(glm::vec2(l_oOut.m_mat3SpriteToPlaced * glm::vec3(l_v2At + l_vOffsets[(size_t)l_iRow * l_oOut.m_iCols + l_iCol], 1.0f)));
        }
    l_oOut.m_vOffsets = l_vOffsets;
    return true;
}

// ---- CharacterRig ------------------------------------------------------------------------------

bool f_AttachPlacement(const std::vector<NodePose>& l_vPoses, const std::string& l_strNode, const glm::mat3& l_mat3Place, glm::mat3& l_mat3Out) {
    for (const NodePose& l_oP : l_vPoses)
        if (l_oP.m_pNode && l_oP.m_pNode->m_strName == l_strNode) {
            l_mat3Out = l_mat3Place * f_Flatten(l_oP.m_mat4World);
            return true;
        }
    return false;
}

void CharacterRig::f_Play(const Clip* l_pCharacter, const Clip* l_pHeld) {
    m_oBody.f_Play(l_pCharacter);
    m_oHeld.f_Play(l_pHeld);
}

void CharacterRig::f_Update(int l_iDtMs) {
    m_oBody.f_Update(l_iDtMs);
    m_oHeld.f_Update(l_iDtMs);
}

std::vector<std::string> CharacterRig::f_TakeEvents() {
    std::vector<std::string> l_vOut = m_oBody.f_TakeEvents();
    for (std::string& l_strE : m_oHeld.f_TakeEvents()) l_vOut.push_back(std::move(l_strE));
    return l_vOut;
}

bool CharacterRig::f_Finished() const {
    return (!m_oBody.f_Clip() || m_oBody.f_Finished()) && (!m_oHeld.f_Clip() || m_oHeld.f_Finished());
}

void CharacterRig::f_Draw(IRenderer* l_pRen, const AtlasSet& l_oCharacterAtlases, const AtlasSet& l_oHeldAtlases, const glm::mat3& l_mat3Place,
                        const float l_arrTint[4]) const {
    if (!l_pRen) return;
    glm::mat3 l_mat3Hand = l_mat3Place;
    if (m_oBody.f_Clip()) {
        m_oBody.f_Draw(l_pRen, l_oCharacterAtlases, l_mat3Place, l_arrTint);   // also brings its poses to this frame
        if (!f_AttachPlacement(m_oBody.f_Poses().f_Poses(), "hand", l_mat3Place, l_mat3Hand)) l_mat3Hand = l_mat3Place;
    }
    if (m_oHeld.f_Clip()) m_oHeld.f_Draw(l_pRen, l_oHeldAtlases, l_mat3Hand, l_arrTint);
}

}  // namespace toms::anim
