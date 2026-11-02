// sprite_mesh.cpp -- see sprite_mesh.h.
#include "sprite_mesh.h"

#include <json.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace toms {

using json = nlohmann::json;

std::string f_SpriteMeshPath(const std::string& l_strAtlasPath) {
    const size_t l_nSlash = l_strAtlasPath.find_last_of("/\\");
    const size_t l_nDot = l_strAtlasPath.find_last_of('.');
    const bool l_bHasExt = l_nDot != std::string::npos && (l_nSlash == std::string::npos || l_nDot > l_nSlash);
    return (l_bHasExt ? l_strAtlasPath.substr(0, l_nDot) : l_strAtlasPath) + ".mesh.json";
}

namespace {

void f_CheckMesh(const SpriteMesh& l_oM, const std::string& l_strName) {
    if (l_oM.m_vVertices.size() % 2) throw std::runtime_error(l_strName + ": vertices must be x, y pairs");
    if (l_oM.f_VertexCount() > 65536u) throw std::runtime_error(l_strName + ": more than 65536 vertices");
    if (l_oM.m_vTriangles.size() % 3) throw std::runtime_error(l_strName + ": triangles must be three indices each");
    for (uint16_t l_u16I : l_oM.m_vTriangles)
        if (l_u16I >= l_oM.f_VertexCount()) throw std::runtime_error(l_strName + ": index " + std::to_string(l_u16I) + " past the last vertex");
}

// count values of T at `view` ({"buffer", "byteOffset"}) in the loaded buffers.
template <class T>
std::vector<T> f_FromBuffer(const json& l_jView, size_t l_nCount, const std::vector<std::string>& l_vBuffers, const std::string& l_strWhere) {
    const size_t l_nB = l_jView.at("buffer").get<size_t>(), l_nOff = l_jView.value("byteOffset", (size_t)0);
    if (l_nB >= l_vBuffers.size()) throw std::runtime_error(l_strWhere + ": no buffer " + std::to_string(l_nB));
    if (l_nOff + l_nCount * sizeof(T) > l_vBuffers[l_nB].size()) throw std::runtime_error(l_strWhere + ": past the end of buffer " + std::to_string(l_nB));
    std::vector<T> l_vOut(l_nCount);
    if (l_nCount) std::memcpy(l_vOut.data(), l_vBuffers[l_nB].data() + l_nOff, l_nCount * sizeof(T));   // little-endian, as every target
    return l_vOut;
}

}  // namespace

bool f_ParseSpriteMeshes(const std::string& l_strText, const std::function<bool(const std::string&, std::string&)>& l_fnReadBuffer,
                       SpriteMeshFile& l_oOut, std::string* l_pErr) {
    try {
        const json l_jJ = json::parse(l_strText);
        if (!l_jJ.is_object() || !l_jJ.contains("version") || !l_jJ["version"].is_number_integer())
            throw std::runtime_error("no \"version\" (the file's format version; this build reads " + std::to_string(g_iSpriteMeshVersion) + ")");
        const int l_iVersion = l_jJ["version"].get<int>();
        if (l_iVersion < 1 || l_iVersion > g_iSpriteMeshVersion)
            throw std::runtime_error("version " + std::to_string(l_iVersion) + " is not one this build reads (1 .. " +
                                     std::to_string(g_iSpriteMeshVersion) + ")");
        std::vector<std::string> l_vBuffers;
        for (const json& l_jB : l_jJ.value("buffers", json::array())) {
            const std::string l_strUri = l_jB.at("uri").get<std::string>();
            std::string l_strBytes;
            if (!l_fnReadBuffer || !l_fnReadBuffer(l_strUri, l_strBytes)) throw std::runtime_error("cannot read the buffer " + l_strUri);
            if (l_jB.contains("byteLength") && l_strBytes.size() < l_jB["byteLength"].get<size_t>()) throw std::runtime_error(l_strUri + " is shorter than its byteLength");
            l_vBuffers.push_back(std::move(l_strBytes));
        }
        SpriteMeshFile l_oF;
        const json l_jMeshes = l_jJ.value("meshes", json::object());
        for (auto l_oIt = l_jMeshes.begin(); l_oIt != l_jMeshes.end(); ++l_oIt) {
            const std::string& l_strName = l_oIt.key();
            const json& l_jM = l_oIt.value();
            SpriteMesh l_oMesh;
            l_oMesh.m_strSource = l_jM.value("source", std::string("alpha"));
            if (l_jM.contains("cells")) {
                const json& l_jC = l_jM["cells"];
                if (!l_jC.is_array() || l_jC.size() != 2 || l_jC[0].get<int>() < 1 || l_jC[1].get<int>() < 1)
                    throw std::runtime_error(l_strName + ": cells must be [across, down], at least 1 each");
                l_oMesh.m_arrCells[0] = l_jC[0].get<int>();
                l_oMesh.m_arrCells[1] = l_jC[1].get<int>();
            }
            const json& l_jV = l_jM.at("vertices");
            const json& l_jT = l_jM.at("triangles");
            if (l_jV.is_array()) l_oMesh.m_vVertices = l_jV.get<std::vector<float>>();
            else l_oMesh.m_vVertices = f_FromBuffer<float>(l_jV, l_jM.at("vertexCount").get<size_t>() * 2, l_vBuffers, l_strName);
            if (l_jT.is_array()) l_oMesh.m_vTriangles = l_jT.get<std::vector<uint16_t>>();
            else l_oMesh.m_vTriangles = f_FromBuffer<uint16_t>(l_jT, l_jM.at("indexCount").get<size_t>(), l_vBuffers, l_strName);
            f_CheckMesh(l_oMesh, l_strName);
            l_oF.m_mapMeshes[l_strName] = std::move(l_oMesh);
        }
        l_oOut = std::move(l_oF);
        return true;
    } catch (const std::exception& l_oE) {
        if (l_pErr) *l_pErr = l_oE.what();
        return false;
    }
}

std::string f_SpriteMeshesToJson(const SpriteMeshFile& l_oF, const std::string& l_strBinaryUri, std::string* l_pBinary) {
    using ojson = nlohmann::ordered_json;   // keys in the order written: the version first, as a header
    const bool l_bBin = !l_strBinaryUri.empty() && l_pBinary;
    if (l_bBin) l_pBinary->clear();
    ojson l_jJ;
    l_jJ["version"] = g_iSpriteMeshVersion;
    ojson l_jMeshes = ojson::object();
    for (const auto& [l_strName, l_oM] : l_oF.m_mapMeshes) {
        ojson l_jMj;
        l_jMj["source"] = l_oM.m_strSource;
        if (l_oM.m_arrCells[0] > 0 && l_oM.m_arrCells[1] > 0) l_jMj["cells"] = {l_oM.m_arrCells[0], l_oM.m_arrCells[1]};
        if (l_bBin) {
            // Each array starts on a 4-byte boundary (glTF's rule), so the floats are aligned.
            auto l_fnPut = [&](const void* l_pData, size_t l_nBytes) {
                while (l_pBinary->size() % 4) l_pBinary->push_back('\0');
                const size_t l_nAt = l_pBinary->size();
                l_pBinary->append((const char*)l_pData, l_nBytes);
                return ojson{{"buffer", 0}, {"byteOffset", l_nAt}};
            };
            l_jMj["vertexCount"] = l_oM.f_VertexCount();
            l_jMj["vertices"] = l_fnPut(l_oM.m_vVertices.data(), l_oM.m_vVertices.size() * sizeof(float));
            l_jMj["indexCount"] = l_oM.m_vTriangles.size();
            l_jMj["triangles"] = l_fnPut(l_oM.m_vTriangles.data(), l_oM.m_vTriangles.size() * sizeof(uint16_t));
        } else {
            ojson l_jV = ojson::array();   // whole pixels (meshes cut from alpha) as integers: "8", not "8.0"
            for (float l_fX : l_oM.m_vVertices)
                if (l_fX == (float)(long long)l_fX) l_jV.push_back((long long)l_fX);
                else l_jV.push_back(l_fX);
            l_jMj["vertices"] = l_jV;
            l_jMj["triangles"] = l_oM.m_vTriangles;
        }
        l_jMeshes[l_strName] = l_jMj;
    }
    if (l_bBin) {
        while (l_pBinary->size() % 4) l_pBinary->push_back('\0');
        l_jJ["buffers"] = ojson::array({{{"uri", l_strBinaryUri}, {"byteLength", l_pBinary->size()}}});
    }
    l_jJ["meshes"] = l_jMeshes;
    return l_jJ.dump() + "\n";   // compact: it is data (the atlas tool and the anim editor show it)
}

namespace {
// The mesh on the grid whose column edges are xs (0 .. w) and row edges ys (0 .. h), in packed pixels.
SpriteMesh f_CutOnGrid(const uint8_t* l_pRgba, int l_iStride, const std::vector<int>& l_vXs, const std::vector<int>& l_vYs, int l_iOffX, int l_iOffY,
                     int l_iAlphaMin) {
    SpriteMesh l_oM;
    const int l_iCols = (int)l_vXs.size() - 1, l_iRows = (int)l_vYs.size() - 1;
    auto l_fnFilled = [&](int l_iCx, int l_iCy) {
        for (int l_iY = l_vYs[(size_t)l_iCy]; l_iY < l_vYs[(size_t)l_iCy + 1]; l_iY++)
            for (int l_iX = l_vXs[(size_t)l_iCx]; l_iX < l_vXs[(size_t)l_iCx + 1]; l_iX++)
                if (l_pRgba[((size_t)l_iY * l_iStride + l_iX) * 4 + 3] >= l_iAlphaMin) return true;
        return false;
    };
    // Grid corners are shared: corner (gx, gy) -> its vertex, made the first time a cell uses it.
    std::unordered_map<int, uint16_t> l_mapCorner;
    auto l_fnVertex = [&](int l_iGx, int l_iGy) -> uint16_t {
        const int l_iKey = l_iGy * (l_iCols + 1) + l_iGx;
        auto l_itIt = l_mapCorner.find(l_iKey);
        if (l_itIt != l_mapCorner.end()) return l_itIt->second;
        const uint16_t l_u16I = (uint16_t)l_oM.f_VertexCount();
        l_oM.m_vVertices.push_back((float)(l_iOffX + l_vXs[(size_t)l_iGx]));
        l_oM.m_vVertices.push_back((float)(l_iOffY + l_vYs[(size_t)l_iGy]));
        l_mapCorner.emplace(l_iKey, l_u16I);
        return l_u16I;
    };
    for (int l_iCy = 0; l_iCy < l_iRows; l_iCy++)
        for (int l_iCx = 0; l_iCx < l_iCols; l_iCx++) {
            if (!l_fnFilled(l_iCx, l_iCy)) continue;
            if (l_oM.f_VertexCount() > 65532u) return l_oM;   // 16-bit indices
            const uint16_t l_u16A = l_fnVertex(l_iCx, l_iCy), l_u16B = l_fnVertex(l_iCx + 1, l_iCy), l_u16C = l_fnVertex(l_iCx + 1, l_iCy + 1), l_u16D = l_fnVertex(l_iCx, l_iCy + 1);
            l_oM.m_vTriangles.insert(l_oM.m_vTriangles.end(), {l_u16A, l_u16B, l_u16C, l_u16A, l_u16C, l_u16D});
        }
    return l_oM;
}
}  // namespace

SpriteMesh f_MeshFromAlpha(const uint8_t* l_pRgba, int l_iStride, int l_iW, int l_iH, int l_iOffX, int l_iOffY, int l_iCell, int l_iAlphaMin) {
    if (!l_pRgba || l_iW <= 0 || l_iH <= 0) return SpriteMesh();
    if (l_iCell < 1) l_iCell = 1;
    std::vector<int> l_vXs, l_vYs;
    for (int l_iX = 0; l_iX < l_iW; l_iX += l_iCell) l_vXs.push_back(l_iX);
    l_vXs.push_back(l_iW);
    for (int l_iY = 0; l_iY < l_iH; l_iY += l_iCell) l_vYs.push_back(l_iY);
    l_vYs.push_back(l_iH);
    return f_CutOnGrid(l_pRgba, l_iStride, l_vXs, l_vYs, l_iOffX, l_iOffY, l_iAlphaMin);
}

SpriteBox f_VisibleBox(const uint8_t* l_pRgba, int l_iStride, int l_iW, int l_iH, int l_iOffX, int l_iOffY, int l_iOrigW, int l_iOrigH, int l_iAlphaMin) {
    SpriteBox l_oB;
    l_oB.m_fOrigW = (float)l_iOrigW;
    l_oB.m_fOrigH = (float)l_iOrigH;
    if (!l_pRgba || l_iW <= 0 || l_iH <= 0) return l_oB;
    int l_iL = l_iW, l_iT = l_iH, l_iR = -1, l_iBt = -1;
    for (int l_iY = 0; l_iY < l_iH; l_iY++)
        for (int l_iX = 0; l_iX < l_iW; l_iX++)
            if (l_pRgba[((size_t)l_iY * l_iStride + l_iX) * 4 + 3] >= l_iAlphaMin) {
                l_iL = std::min(l_iL, l_iX); l_iR = std::max(l_iR, l_iX);
                l_iT = std::min(l_iT, l_iY); l_iBt = std::max(l_iBt, l_iY);
            }
    if (l_iR < 0) return l_oB;
    l_oB.m_fLeft = (float)(l_iOffX + l_iL);
    l_oB.m_fTop = (float)(l_iOffY + l_iT);
    l_oB.m_fRight = (float)(l_iOffX + l_iR + 1);
    l_oB.m_fBottom = (float)(l_iOffY + l_iBt + 1);
    return l_oB;
}

SpriteMesh f_MeshFromAlphaGrid(const uint8_t* l_pRgba, int l_iStride, int l_iW, int l_iH, int l_iOffX, int l_iOffY, int l_iCols, int l_iRows, int l_iAlphaMin) {
    if (!l_pRgba || l_iW <= 0 || l_iH <= 0) return SpriteMesh();
    l_iCols = std::clamp(l_iCols, 1, l_iW);
    l_iRows = std::clamp(l_iRows, 1, l_iH);
    std::vector<int> l_vXs, l_vYs;
    for (int l_iI = 0; l_iI <= l_iCols; l_iI++) l_vXs.push_back((int)((long long)l_iI * l_iW / l_iCols));
    for (int l_iI = 0; l_iI <= l_iRows; l_iI++) l_vYs.push_back((int)((long long)l_iI * l_iH / l_iRows));
    SpriteMesh l_oM = f_CutOnGrid(l_pRgba, l_iStride, l_vXs, l_vYs, l_iOffX, l_iOffY, l_iAlphaMin);
    l_oM.m_arrCells[0] = l_iCols;
    l_oM.m_arrCells[1] = l_iRows;
    return l_oM;
}

}  // namespace toms
