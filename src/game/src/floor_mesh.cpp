// floor_mesh.cpp -- see floor_mesh.h.
#include "field_keys.h"    // toms::field: every data field by its constant
#include "floor_mesh.h"

#include <glm/glm.hpp>
#include <json.hpp>

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace toms::next {
namespace {

struct Part {
    std::vector<glm::vec3> m_vPos, m_vNor;
    std::vector<glm::vec4> m_vTan;
    std::vector<glm::vec2> m_vUv;
    std::vector<uint32_t> m_vIdx;

    // Four corners round a quad facing `normal` (turned so its triangles face it: glTF fronts are counter-clockwise).
    void f_Quad(glm::vec3 l_arrP[4], const glm::vec3& l_v3N, const glm::vec3& l_v3T, glm::vec2 l_arrUv[4]) {
        if (glm::dot(glm::cross(l_arrP[1] - l_arrP[0], l_arrP[2] - l_arrP[0]), l_v3N) < 0.0f) {
            std::swap(l_arrP[1], l_arrP[3]);
            std::swap(l_arrUv[1], l_arrUv[3]);
        }
        const uint32_t l_uBase = (uint32_t)m_vPos.size();
        for (int l_iI = 0; l_iI < 4; l_iI++) {
            m_vPos.push_back(l_arrP[l_iI]);
            m_vNor.push_back(l_v3N);
            m_vTan.push_back(glm::vec4(l_v3T, 1.0f));
            m_vUv.push_back(l_arrUv[l_iI]);
        }
        for (uint32_t l_uI : {0u, 1u, 2u, 0u, 2u, 3u}) m_vIdx.push_back(l_uBase + l_uI);
    }
};

}  // namespace

std::string f_MakeFloorGlb(const std::vector<std::string>& l_vWalls, const std::string& l_strLook, float l_fWallHeight, float l_fGroundRepeat,
                           float l_fStairShade) {
    const int l_iH = (int)l_vWalls.size();
    int l_iW = 0;
    for (const std::string& l_strRow : l_vWalls) l_iW = std::max(l_iW, (int)l_strRow.size());
    auto l_fnWall = [&](int l_iX, int l_iZ) {
        return l_iZ >= 0 && l_iZ < l_iH && l_iX >= 0 && l_iX < (int)l_vWalls[(size_t)l_iZ].size() && l_vWalls[(size_t)l_iZ][(size_t)l_iX] == '#';
    };
    Part l_oGround, l_oSide, l_oTop, l_oStair;
    const float l_fW = (float)l_iW, l_fD = (float)l_iH, l_fR = std::max(0.1f, l_fGroundRepeat), l_fWh = l_fWallHeight;
    auto l_fnCell = [&](int l_iX, int l_iZ) {
        return l_iZ >= 0 && l_iZ < l_iH && l_iX >= 0 && l_iX < (int)l_vWalls[(size_t)l_iZ].size() ? l_vWalls[(size_t)l_iZ][(size_t)l_iX] : '#';
    };
    // the ground: a square a cell, but not under the walls nor where stairs go down (their pit)
    (void)l_fW; (void)l_fD;
    for (int l_iZ = 0; l_iZ < l_iH; l_iZ++)
        for (int l_iX = 0; l_iX < l_iW; l_iX++) {
            const char l_cC = l_fnCell(l_iX, l_iZ);
            if (l_cC == '#' || l_cC == 'D') continue;
            const float l_fX = (float)l_iX, l_fZ = (float)l_iZ;
            glm::vec3 l_arrP[4] = {{l_fX, 0, l_fZ}, {l_fX + 1, 0, l_fZ}, {l_fX + 1, 0, l_fZ + 1}, {l_fX, 0, l_fZ + 1}};
            glm::vec2 l_arrUv[4] = {{l_fX / l_fR, l_fZ / l_fR}, {(l_fX + 1) / l_fR, l_fZ / l_fR}, {(l_fX + 1) / l_fR, (l_fZ + 1) / l_fR}, {l_fX / l_fR, (l_fZ + 1) / l_fR}};
            l_oGround.f_Quad(l_arrP, {0, 1, 0}, {1, 0, 0}, l_arrUv);
        }
    // the stairs ('U' up, 'D' down): four steps, walked onto from an open side (south first: it faces the camera); up
    // climbs to the walls' top, down goes into a pit as deep. Treads in the look's ground, risers and the pit in its wall.
    const int l_iSteps = 4;
    auto l_fnStairs = [&](int l_iX, int l_iZ, bool l_bUp) {
        static const int l_arrDir[4][2] = {{0, 1}, {-1, 0}, {1, 0}, {0, -1}};   // south, west, east, north
        int l_iDx = 0, l_iDz = 1;
        for (const auto& l_arrD : l_arrDir) {
            const char l_cN = l_fnCell(l_iX + l_arrD[0], l_iZ + l_arrD[1]);
            if (l_cN != '#' && l_cN != 'U' && l_cN != 'D') { l_iDx = l_arrD[0]; l_iDz = l_arrD[1]; break; }
        }
        const glm::vec3 l_v3In((float)l_iDx, 0.0f, (float)l_iDz);        // toward the side it is walked onto from
        const glm::vec3 l_v3Side(-l_v3In.z, 0.0f, l_v3In.x);              // across the steps
        const glm::vec3 l_v3Mid((float)l_iX + 0.5f, 0.0f, (float)l_iZ + 0.5f);
        // a point of the cell: u from its open edge (0) to its far edge (1), v across (-0.5..0.5), y up
        auto l_fnP = [&](float l_fU, float l_fV, float l_fY) { return l_v3Mid + l_v3In * (0.5f - l_fU) + l_v3Side * l_fV + glm::vec3(0.0f, l_fY, 0.0f); };
        const float l_fStepD = 1.0f / (float)l_iSteps, l_fStepH = l_fWh / (float)l_iSteps;
        for (int l_iK = 0; l_iK < l_iSteps; l_iK++) {
            const float l_fU0 = l_iK * l_fStepD, l_fU1 = l_fU0 + l_fStepD;
            const float l_fTop = l_bUp ? (l_iK + 1) * l_fStepH : -(l_iK + 1) * l_fStepH;   // this step's tread
            const float l_fPrev = l_bUp ? l_iK * l_fStepH : -l_iK * l_fStepH;              // the one before it
            {   // the tread
                glm::vec3 l_arrP[4] = {l_fnP(l_fU0, -0.5f, l_fTop), l_fnP(l_fU0, 0.5f, l_fTop), l_fnP(l_fU1, 0.5f, l_fTop), l_fnP(l_fU1, -0.5f, l_fTop)};
                glm::vec2 l_arrUv[4];   // the floor's own boards / slabs, darkened (stairShade): they stand out from the ground
                for (int l_iC = 0; l_iC < 4; l_iC++) l_arrUv[l_iC] = glm::vec2(l_arrP[l_iC].x, l_arrP[l_iC].z) / l_fR;
                l_oStair.f_Quad(l_arrP, {0, 1, 0}, l_v3Side, l_arrUv);
            }
            {   // the riser at its front edge: from the step before up to it (up), or down to it (down)
                glm::vec3 l_arrP[4] = {l_fnP(l_fU0, -0.5f, l_fPrev), l_fnP(l_fU0, 0.5f, l_fPrev), l_fnP(l_fU0, 0.5f, l_fTop), l_fnP(l_fU0, -0.5f, l_fTop)};
                glm::vec2 l_arrUv[4] = {{-0.5f, -l_fPrev}, {0.5f, -l_fPrev}, {0.5f, -l_fTop}, {-0.5f, -l_fTop}};
                l_oSide.f_Quad(l_arrP, l_bUp ? l_v3In : -l_v3In, l_v3Side, l_arrUv);
            }
            if (l_bUp)   // its two sides, where the cell beside is open (a wall beside hides them)
                for (int l_iS = -1; l_iS <= 1; l_iS += 2) {
                    const glm::vec3 l_v3Out = l_v3Side * (float)l_iS;
                    const glm::vec3 l_v3N = l_v3Mid + l_v3Out;
                    if (l_fnCell((int)std::floor(l_v3N.x), (int)std::floor(l_v3N.z)) == '#') continue;
                    const float l_fV = 0.5f * (float)l_iS;
                    glm::vec3 l_arrP[4] = {l_fnP(l_fU0, l_fV, 0.0f), l_fnP(l_fU1, l_fV, 0.0f), l_fnP(l_fU1, l_fV, l_fTop), l_fnP(l_fU0, l_fV, l_fTop)};
                    glm::vec2 l_arrUv[4] = {{l_fU0, 0.0f}, {l_fU1, 0.0f}, {l_fU1, -l_fTop}, {l_fU0, -l_fTop}};
                    l_oSide.f_Quad(l_arrP, l_v3Out, -l_v3In, l_arrUv);
                }
        }
        if (l_bUp) {   // its back, where the cell behind is open
            const glm::vec3 l_v3B = l_v3Mid - l_v3In;
            if (l_fnCell((int)std::floor(l_v3B.x), (int)std::floor(l_v3B.z)) != '#') {
                glm::vec3 l_arrP[4] = {l_fnP(1.0f, -0.5f, 0.0f), l_fnP(1.0f, 0.5f, 0.0f), l_fnP(1.0f, 0.5f, l_fWh), l_fnP(1.0f, -0.5f, l_fWh)};
                glm::vec2 l_arrUv[4] = {{-0.5f, 0.0f}, {0.5f, 0.0f}, {0.5f, -l_fWh}, {-0.5f, -l_fWh}};
                l_oSide.f_Quad(l_arrP, -l_v3In, l_v3Side, l_arrUv);
            }
        } else {   // the pit's three walls (the sides and the back), from the ground down to its bottom
            const float l_fBottom = -l_fWh;
            for (int l_iS = -1; l_iS <= 1; l_iS += 2) {
                const float l_fV = 0.5f * (float)l_iS;
                glm::vec3 l_arrP[4] = {l_fnP(0.0f, l_fV, 0.0f), l_fnP(1.0f, l_fV, 0.0f), l_fnP(1.0f, l_fV, l_fBottom), l_fnP(0.0f, l_fV, l_fBottom)};
                glm::vec2 l_arrUv[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, -l_fBottom}, {0.0f, -l_fBottom}};
                l_oSide.f_Quad(l_arrP, -l_v3Side * (float)l_iS, l_v3In, l_arrUv);
            }
            glm::vec3 l_arrP[4] = {l_fnP(1.0f, -0.5f, 0.0f), l_fnP(1.0f, 0.5f, 0.0f), l_fnP(1.0f, 0.5f, l_fBottom), l_fnP(1.0f, -0.5f, l_fBottom)};
            glm::vec2 l_arrUv[4] = {{-0.5f, 0.0f}, {0.5f, 0.0f}, {0.5f, -l_fBottom}, {-0.5f, -l_fBottom}};
            l_oSide.f_Quad(l_arrP, l_v3In, l_v3Side, l_arrUv);
        }
    };
    for (int l_iZ = 0; l_iZ < l_iH; l_iZ++)
        for (int l_iX = 0; l_iX < l_iW; l_iX++) {
            const char l_cC = l_fnCell(l_iX, l_iZ);
            if (l_cC == 'U' || l_cC == 'D') l_fnStairs(l_iX, l_iZ, l_cC == 'U');
        }
    for (int l_iZ = 0; l_iZ < l_iH; l_iZ++)
        for (int l_iX = 0; l_iX < l_iW; l_iX++) {
            if (!l_fnWall(l_iX, l_iZ)) continue;
            const float l_fX = (float)l_iX, l_fZ = (float)l_iZ;
            {
                glm::vec3 l_arrP[4] = {{l_fX, l_fWh, l_fZ}, {l_fX + 1, l_fWh, l_fZ}, {l_fX + 1, l_fWh, l_fZ + 1}, {l_fX, l_fWh, l_fZ + 1}};
                glm::vec2 l_arrUv[4] = {{l_fX, l_fZ}, {l_fX + 1, l_fZ}, {l_fX + 1, l_fZ + 1}, {l_fX, l_fZ + 1}};
                l_oTop.f_Quad(l_arrP, {0, 1, 0}, {1, 0, 0}, l_arrUv);
            }
            if (!l_fnWall(l_iX, l_iZ - 1)) {   // north, facing -z
                glm::vec3 l_arrP[4] = {{l_fX + 1, 0, l_fZ}, {l_fX, 0, l_fZ}, {l_fX, l_fWh, l_fZ}, {l_fX + 1, l_fWh, l_fZ}};
                glm::vec2 l_arrUv[4] = {{-(l_fX + 1), l_fWh}, {-l_fX, l_fWh}, {-l_fX, 0}, {-(l_fX + 1), 0}};
                l_oSide.f_Quad(l_arrP, {0, 0, -1}, {-1, 0, 0}, l_arrUv);
            }
            if (!l_fnWall(l_iX, l_iZ + 1)) {   // south, facing +z (toward the camera)
                glm::vec3 l_arrP[4] = {{l_fX, 0, l_fZ + 1}, {l_fX + 1, 0, l_fZ + 1}, {l_fX + 1, l_fWh, l_fZ + 1}, {l_fX, l_fWh, l_fZ + 1}};
                glm::vec2 l_arrUv[4] = {{l_fX, l_fWh}, {l_fX + 1, l_fWh}, {l_fX + 1, 0}, {l_fX, 0}};
                l_oSide.f_Quad(l_arrP, {0, 0, 1}, {1, 0, 0}, l_arrUv);
            }
            if (!l_fnWall(l_iX - 1, l_iZ)) {   // west, facing -x
                glm::vec3 l_arrP[4] = {{l_fX, 0, l_fZ}, {l_fX, 0, l_fZ + 1}, {l_fX, l_fWh, l_fZ + 1}, {l_fX, l_fWh, l_fZ}};
                glm::vec2 l_arrUv[4] = {{l_fZ, l_fWh}, {l_fZ + 1, l_fWh}, {l_fZ + 1, 0}, {l_fZ, 0}};
                l_oSide.f_Quad(l_arrP, {-1, 0, 0}, {0, 0, 1}, l_arrUv);
            }
            if (!l_fnWall(l_iX + 1, l_iZ)) {   // east, facing +x
                glm::vec3 l_arrP[4] = {{l_fX + 1, 0, l_fZ + 1}, {l_fX + 1, 0, l_fZ}, {l_fX + 1, l_fWh, l_fZ}, {l_fX + 1, l_fWh, l_fZ + 1}};
                glm::vec2 l_arrUv[4] = {{-(l_fZ + 1), l_fWh}, {-l_fZ, l_fWh}, {-l_fZ, 0}, {-(l_fZ + 1), 0}};
                l_oSide.f_Quad(l_arrP, {1, 0, 0}, {0, 0, -1}, l_arrUv);
            }
        }

    // ---- the glTF: one buffer, a primitive per part, the look's textures by file ----
    std::string l_strBin;
    nlohmann::json l_jViews = nlohmann::json::array(), l_jAcc = nlohmann::json::array(), l_jPrims = nlohmann::json::array(),
                   l_jMats = nlohmann::json::array(), l_jImages = nlohmann::json::array(), l_jTex = nlohmann::json::array();
    auto l_fnAdd = [&](const void* l_pData, size_t l_nBytes, int l_iTarget, size_t l_nCount, const char* l_pszType, int l_iComp) {
        while (l_strBin.size() % 4) l_strBin.push_back('\0');
        l_jViews.push_back({{toms::field::g_pszBuffer, 0}, {toms::field::g_pszByteOffset, l_strBin.size()}, {toms::field::g_pszByteLength, l_nBytes}, {toms::field::g_pszTarget, l_iTarget}});
        l_strBin.append((const char*)l_pData, l_nBytes);
        l_jAcc.push_back({{toms::field::g_pszBufferView, l_jViews.size() - 1}, {toms::field::g_pszComponentType, l_iComp}, {toms::field::g_pszCount, l_nCount}, {toms::field::g_pszType, l_pszType}});
        return (int)l_jAcc.size() - 1;
    };
    // (name, its texture, its part, its colour's shade): the stairs' treads are the ground's boards, darkened
    struct PartOut { const char* m_pszName; const char* m_pszTex; const Part* m_pPart; float m_fShade; };
    const PartOut l_arrParts[4] = {{toms::field::g_pszGround, "ground", &l_oGround, 1.0f}, {toms::field::g_pszWall, "wall", &l_oSide, 1.0f}, {toms::field::g_pszTop, "top", &l_oTop, 1.0f},
                                   {toms::field::g_pszStair, "ground", &l_oStair, std::clamp(l_fStairShade, 0.05f, 1.0f)}};
    for (const PartOut& l_oPart : l_arrParts) {
        const char* const l_pszName = l_oPart.m_pszName;
        const Part* const l_pPart = l_oPart.m_pPart;
        if (l_pPart->m_vIdx.empty()) continue;
        glm::vec3 l_v3Min(1e30f), l_v3Max(-1e30f);
        for (const glm::vec3& l_v3P : l_pPart->m_vPos) { l_v3Min = glm::min(l_v3Min, l_v3P); l_v3Max = glm::max(l_v3Max, l_v3P); }
        const size_t l_nN = l_pPart->m_vPos.size();
        const int l_iPos = l_fnAdd(l_pPart->m_vPos.data(), l_nN * sizeof(glm::vec3), 34962, l_nN, "VEC3", 5126);
        l_jAcc[(size_t)l_iPos][toms::field::g_pszMin] = {l_v3Min.x, l_v3Min.y, l_v3Min.z};
        l_jAcc[(size_t)l_iPos][toms::field::g_pszMax] = {l_v3Max.x, l_v3Max.y, l_v3Max.z};
        const int l_iNor = l_fnAdd(l_pPart->m_vNor.data(), l_nN * sizeof(glm::vec3), 34962, l_nN, "VEC3", 5126);
        const int l_iTan = l_fnAdd(l_pPart->m_vTan.data(), l_nN * sizeof(glm::vec4), 34962, l_nN, "VEC4", 5126);
        const int l_iUv = l_fnAdd(l_pPart->m_vUv.data(), l_nN * sizeof(glm::vec2), 34962, l_nN, "VEC2", 5126);
        const int l_iIdx = l_fnAdd(l_pPart->m_vIdx.data(), l_pPart->m_vIdx.size() * 4, 34963, l_pPart->m_vIdx.size(), "SCALAR", 5125);
        const size_t l_nImg = l_jImages.size();
        l_jImages.push_back({{toms::field::g_pszUri, "looks/" + l_strLook + "/" + l_oPart.m_pszTex + "_c.jpg"}});
        l_jImages.push_back({{toms::field::g_pszUri, "looks/" + l_strLook + "/" + l_oPart.m_pszTex + "_n.jpg"}});
        l_jTex.push_back({{toms::field::g_pszSource, l_nImg}, {toms::field::g_pszSampler, 0}});
        l_jTex.push_back({{toms::field::g_pszSource, l_nImg + 1}, {toms::field::g_pszSampler, 0}});
        const float l_fS = l_oPart.m_fShade;
        l_jMats.push_back({{toms::field::g_pszName, l_strLook + "_" + l_pszName},
                           {toms::field::g_pszPbrMetallicRoughness, {{toms::field::g_pszBaseColorTexture, {{toms::field::g_pszIndex, l_nImg}}}, {toms::field::g_pszBaseColorFactor, {l_fS, l_fS, l_fS, 1.0f}}, {toms::field::g_pszMetallicFactor, 0.0},
                                                     {toms::field::g_pszRoughnessFactor, std::strcmp(l_pszName, "top") == 0 ? 0.95 : 0.85}}},
                           {toms::field::g_pszNormalTexture, {{toms::field::g_pszIndex, l_nImg + 1}}}});
        l_jPrims.push_back({{toms::field::g_pszAttributes, {{toms::field::g_pszPOSITION, l_iPos}, {toms::field::g_pszNORMAL, l_iNor}, {toms::field::g_pszTANGENT, l_iTan}, {toms::field::g_pszTEXCOORD0, l_iUv}}},
                            {toms::field::g_pszIndices, l_iIdx}, {toms::field::g_pszMaterial, l_jMats.size() - 1}});
    }
    while (l_strBin.size() % 4) l_strBin.push_back('\0');
    const nlohmann::json l_jG = {
        {toms::field::g_pszAsset, {{toms::field::g_pszVersion, "2.0"}, {toms::field::g_pszGenerator, "TOMS floor_mesh.cpp"}}},
        {toms::field::g_pszScene, 0}, {toms::field::g_pszScenes, {{{toms::field::g_pszNodes, {0}}}}}, {toms::field::g_pszNodes, {{{toms::field::g_pszMesh, 0}, {toms::field::g_pszName, "floor"}}}},
        {toms::field::g_pszMeshes, {{{toms::field::g_pszName, "floor"}, {toms::field::g_pszPrimitives, l_jPrims}}}},
        {toms::field::g_pszBuffers, {{{toms::field::g_pszByteLength, l_strBin.size()}}}},
        {toms::field::g_pszBufferViews, l_jViews}, {toms::field::g_pszAccessors, l_jAcc}, {toms::field::g_pszMaterials, l_jMats},
        {toms::field::g_pszSamplers, {{{toms::field::g_pszMagFilter, 9729}, {toms::field::g_pszMinFilter, 9987}, {toms::field::g_pszWrapS, 10497}, {toms::field::g_pszWrapT, 10497}}}},
        {toms::field::g_pszImages, l_jImages}, {toms::field::g_pszTextures, l_jTex}};
    std::string l_strJson = l_jG.dump();
    while (l_strJson.size() % 4) l_strJson.push_back(' ');
    // .glb: the header, the JSON chunk, the BIN chunk (little-endian)
    std::string l_strOut;
    auto l_fnU32 = [&](uint32_t l_uV) { l_strOut.append((const char*)&l_uV, 4); };
    l_fnU32(0x46546C67u);   // "glTF"
    l_fnU32(2u);
    l_fnU32((uint32_t)(12 + 8 + l_strJson.size() + 8 + l_strBin.size()));
    l_fnU32((uint32_t)l_strJson.size());
    l_fnU32(0x4E4F534Au);   // "JSON"
    l_strOut += l_strJson;
    l_fnU32((uint32_t)l_strBin.size());
    l_fnU32(0x004E4942u);   // "BIN"
    l_strOut += l_strBin;
    return l_strOut;
}

}  // namespace toms::next
