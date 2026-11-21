// floor_mesh.cpp -- see floor_mesh.h.
#include "floor_mesh.h"

#include <glm/glm.hpp>
#include <json.hpp>

#include <cstdint>
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

std::string f_MakeFloorGlb(const std::vector<std::string>& l_vWalls, const std::string& l_strLook, float l_fWallHeight, float l_fGroundRepeat) {
    const int l_iH = (int)l_vWalls.size();
    int l_iW = 0;
    for (const std::string& l_strRow : l_vWalls) l_iW = std::max(l_iW, (int)l_strRow.size());
    auto l_fnWall = [&](int l_iX, int l_iZ) {
        return l_iZ >= 0 && l_iZ < l_iH && l_iX >= 0 && l_iX < (int)l_vWalls[(size_t)l_iZ].size() && l_vWalls[(size_t)l_iZ][(size_t)l_iX] == '#';
    };
    Part l_oGround, l_oSide, l_oTop;
    const float l_fW = (float)l_iW, l_fD = (float)l_iH, l_fR = std::max(0.1f, l_fGroundRepeat), l_fWh = l_fWallHeight;
    {
        glm::vec3 l_arrP[4] = {{0, 0, 0}, {l_fW, 0, 0}, {l_fW, 0, l_fD}, {0, 0, l_fD}};
        glm::vec2 l_arrUv[4] = {{0, 0}, {l_fW / l_fR, 0}, {l_fW / l_fR, l_fD / l_fR}, {0, l_fD / l_fR}};
        l_oGround.f_Quad(l_arrP, {0, 1, 0}, {1, 0, 0}, l_arrUv);
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
        l_jViews.push_back({{"buffer", 0}, {"byteOffset", l_strBin.size()}, {"byteLength", l_nBytes}, {"target", l_iTarget}});
        l_strBin.append((const char*)l_pData, l_nBytes);
        l_jAcc.push_back({{"bufferView", l_jViews.size() - 1}, {"componentType", l_iComp}, {"count", l_nCount}, {"type", l_pszType}});
        return (int)l_jAcc.size() - 1;
    };
    const std::pair<const char*, const Part*> l_arrParts[3] = {{"ground", &l_oGround}, {"wall", &l_oSide}, {"top", &l_oTop}};
    for (const auto& [l_pszName, l_pPart] : l_arrParts) {
        if (l_pPart->m_vIdx.empty()) continue;
        glm::vec3 l_v3Min(1e30f), l_v3Max(-1e30f);
        for (const glm::vec3& l_v3P : l_pPart->m_vPos) { l_v3Min = glm::min(l_v3Min, l_v3P); l_v3Max = glm::max(l_v3Max, l_v3P); }
        const size_t l_nN = l_pPart->m_vPos.size();
        const int l_iPos = l_fnAdd(l_pPart->m_vPos.data(), l_nN * sizeof(glm::vec3), 34962, l_nN, "VEC3", 5126);
        l_jAcc[(size_t)l_iPos]["min"] = {l_v3Min.x, l_v3Min.y, l_v3Min.z};
        l_jAcc[(size_t)l_iPos]["max"] = {l_v3Max.x, l_v3Max.y, l_v3Max.z};
        const int l_iNor = l_fnAdd(l_pPart->m_vNor.data(), l_nN * sizeof(glm::vec3), 34962, l_nN, "VEC3", 5126);
        const int l_iTan = l_fnAdd(l_pPart->m_vTan.data(), l_nN * sizeof(glm::vec4), 34962, l_nN, "VEC4", 5126);
        const int l_iUv = l_fnAdd(l_pPart->m_vUv.data(), l_nN * sizeof(glm::vec2), 34962, l_nN, "VEC2", 5126);
        const int l_iIdx = l_fnAdd(l_pPart->m_vIdx.data(), l_pPart->m_vIdx.size() * 4, 34963, l_pPart->m_vIdx.size(), "SCALAR", 5125);
        const size_t l_nImg = l_jImages.size();
        l_jImages.push_back({{"uri", "looks/" + l_strLook + "/" + l_pszName + "_c.jpg"}});
        l_jImages.push_back({{"uri", "looks/" + l_strLook + "/" + l_pszName + "_n.png"}});
        l_jTex.push_back({{"source", l_nImg}, {"sampler", 0}});
        l_jTex.push_back({{"source", l_nImg + 1}, {"sampler", 0}});
        l_jMats.push_back({{"name", l_strLook + "_" + l_pszName},
                           {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", l_nImg}}}, {"metallicFactor", 0.0},
                                                     {"roughnessFactor", std::strcmp(l_pszName, "top") == 0 ? 0.95 : 0.85}}},
                           {"normalTexture", {{"index", l_nImg + 1}}}});
        l_jPrims.push_back({{"attributes", {{"POSITION", l_iPos}, {"NORMAL", l_iNor}, {"TANGENT", l_iTan}, {"TEXCOORD_0", l_iUv}}},
                            {"indices", l_iIdx}, {"material", l_jMats.size() - 1}});
    }
    while (l_strBin.size() % 4) l_strBin.push_back('\0');
    const nlohmann::json l_jG = {
        {"asset", {{"version", "2.0"}, {"generator", "TOMS floor_mesh.cpp"}}},
        {"scene", 0}, {"scenes", {{{"nodes", {0}}}}}, {"nodes", {{{"mesh", 0}, {"name", "floor"}}}},
        {"meshes", {{{"name", "floor"}, {"primitives", l_jPrims}}}},
        {"buffers", {{{"byteLength", l_strBin.size()}}}},
        {"bufferViews", l_jViews}, {"accessors", l_jAcc}, {"materials", l_jMats},
        {"samplers", {{{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", 10497}, {"wrapT", 10497}}}},
        {"images", l_jImages}, {"textures", l_jTex}};
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
