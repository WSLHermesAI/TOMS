// gltf_model.cpp -- see gltf_model.h.
#define CGLTF_IMPLEMENTATION
#include "gltf_model.h"

#include <cgltf.h>
#include <json.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>   // the implementation is in stb_image_impl.cpp

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace toms::gltf {
namespace {

// ---- small helpers ------------------------------------------------------------------------------

template <class T> int f_IndexOf(const T* l_pItem, const T* l_pFirst) { return l_pItem ? int(l_pItem - l_pFirst) : -1; }

bool f_Contains(const std::vector<std::string>& l_vV, const std::string& l_strS) { return std::find(l_vV.begin(), l_vV.end(), l_strS) != l_vV.end(); }

// The extensions we draw. Anything else the file uses is "ignored" (still loads), unless required.
const char* const g_arrSupported[] = {"KHR_materials_unlit", "KHR_texture_transform", "KHR_materials_emissive_strength",
                                  "KHR_mesh_quantization", "EXT_mesh_gpu_instancing", "KHR_materials_pbrSpecularGlossiness",
                                  "KHR_materials_transmission", "KHR_materials_volume", "KHR_lights_punctual",
                                  "KHR_materials_specular", "KHR_materials_ior",
                                  "KHR_implicit_shapes", "KHR_physics_rigid_bodies"};
bool f_Supported(const std::string& l_strE) {
    for (const char* l_pszS : g_arrSupported)
        if (l_strE == l_pszS) return true;
    return false;
}

std::vector<float> f_Floats(const cgltf_accessor* l_pA) {
    std::vector<float> l_vOut;
    if (!l_pA) return l_vOut;
    const size_t l_nN = l_pA->count * cgltf_num_components(l_pA->type);
    l_vOut.resize(l_nN);
    cgltf_accessor_unpack_floats(l_pA, l_vOut.data(), l_nN);   // normalized / quantized ints, sparse accessors
    return l_vOut;
}

template <class V> std::vector<V> f_Vectors(const cgltf_accessor* l_pA, int l_iComps, float l_fFill = 0.0f) {
    std::vector<V> l_vOut;
    if (!l_pA) return l_vOut;
    const int l_iHave = (int)cgltf_num_components(l_pA->type);
    const std::vector<float> l_vF = f_Floats(l_pA);
    l_vOut.resize(l_pA->count);
    for (size_t l_nI = 0; l_nI < l_pA->count; l_nI++) {
        V l_oV(l_fFill);
        for (int l_iC = 0; l_iC < l_iComps && l_iC < l_iHave; l_iC++) l_oV[l_iC] = l_vF[l_nI * (size_t)l_iHave + (size_t)l_iC];
        l_vOut[l_nI] = l_oV;
    }
    return l_vOut;
}

std::vector<uint8_t> f_DecodeBase64(const char* l_pszS, size_t l_nLen) {
    auto l_fnVal = [](char l_cC) -> int {
        if (l_cC >= 'A' && l_cC <= 'Z') return l_cC - 'A';
        if (l_cC >= 'a' && l_cC <= 'z') return l_cC - 'a' + 26;
        if (l_cC >= '0' && l_cC <= '9') return l_cC - '0' + 52;
        if (l_cC == '+' || l_cC == '-') return 62;
        if (l_cC == '/' || l_cC == '_') return 63;
        return -1;
    };
    std::vector<uint8_t> l_vOut;
    l_vOut.reserve(l_nLen * 3 / 4);
    int l_iBuf = 0, l_iBits = 0;
    for (size_t l_nI = 0; l_nI < l_nLen; l_nI++) {
        const int l_iV = l_fnVal(l_pszS[l_nI]);
        if (l_iV < 0) continue;   // '=' padding, whitespace
        l_iBuf = (l_iBuf << 6) | l_iV;
        l_iBits += 6;
        if (l_iBits >= 8) {
            l_iBits -= 8;
            l_vOut.push_back(uint8_t((l_iBuf >> l_iBits) & 0xff));
        }
    }
    return l_vOut;
}

// Paths are UTF-8 (the viewer's command line, drag-and-drop, the open dialog). A plain std::ifstream(string) on
// Windows reads them in the ANSI code page, so a name like "Unicode❤♻Test.glb" was not found: open through
// std::filesystem's UTF-8 path instead (a wide path on Windows, the same bytes elsewhere).
bool f_ReadFile(const std::string& l_strPath, std::vector<uint8_t>& l_vOut) {
    std::ifstream l_oF(std::filesystem::u8path(l_strPath), std::ios::binary);
    if (!l_oF) return false;
    l_vOut.assign(std::istreambuf_iterator<char>(l_oF), std::istreambuf_iterator<char>());
    return true;
}

// cgltf's own file reads (the .gltf / .glb, external .bin buffers) through readFile, for the same reason.
cgltf_result f_CgltfRead(const cgltf_memory_options*, const cgltf_file_options*, const char* l_pszPath, cgltf_size* l_pSize, void** l_pData) {
    std::vector<uint8_t> l_vBytes;
    if (!l_pszPath || !f_ReadFile(l_pszPath, l_vBytes)) return cgltf_result_file_not_found;
    void* l_pP = std::malloc(l_vBytes.empty() ? 1 : l_vBytes.size());
    if (!l_pP) return cgltf_result_out_of_memory;
    if (!l_vBytes.empty()) std::memcpy(l_pP, l_vBytes.data(), l_vBytes.size());
    *l_pSize = l_vBytes.size();
    *l_pData = l_pP;
    return cgltf_result_success;
}
void f_CgltfRelease(const cgltf_memory_options*, const cgltf_file_options*, void* l_pData) { std::free(l_pData); }

cgltf_options f_CgltfOptions() {
    cgltf_options l_oOpt{};
    l_oOpt.file.read = f_CgltfRead;
    l_oOpt.file.release = f_CgltfRelease;
    return l_oOpt;
}

std::string f_DirOf(const std::string& l_strPath) {
    const size_t l_nS = l_strPath.find_last_of("/\\");
    return l_nS == std::string::npos ? std::string(".") : l_strPath.substr(0, l_nS);
}

void f_DecodeImage(const cgltf_image& l_oSrc, const std::string& l_strBaseDir, Image& l_oOut, std::vector<std::string>& l_vWarnings) {
    l_oOut.m_strName = l_oSrc.name ? l_oSrc.name : "";
    std::vector<uint8_t> l_vBytes;
    if (l_oSrc.buffer_view && l_oSrc.buffer_view->buffer && l_oSrc.buffer_view->buffer->data) {
        const uint8_t* l_pP = (const uint8_t*)l_oSrc.buffer_view->buffer->data + l_oSrc.buffer_view->offset;
        l_vBytes.assign(l_pP, l_pP + l_oSrc.buffer_view->size);
    } else if (l_oSrc.uri && std::strncmp(l_oSrc.uri, "data:", 5) == 0) {
        const char* l_pszComma = std::strchr(l_oSrc.uri, ',');
        if (l_pszComma) l_vBytes = f_DecodeBase64(l_pszComma + 1, std::strlen(l_pszComma + 1));
    } else if (l_oSrc.uri) {
        std::string l_strUri = l_oSrc.uri;
        cgltf_decode_uri(&l_strUri[0]);
        l_strUri.resize(std::strlen(l_strUri.c_str()));
        if (!f_ReadFile(l_strBaseDir + "/" + l_strUri, l_vBytes)) l_vWarnings.push_back("image not found: " + l_strUri);
    }
    if (l_vBytes.empty()) return;
    int l_iW = 0, l_iH = 0, l_iN = 0;
    stbi_uc* l_pPx = stbi_load_from_memory(l_vBytes.data(), (int)l_vBytes.size(), &l_iW, &l_iH, &l_iN, 4);
    if (!l_pPx) {
        l_vWarnings.push_back("image could not be decoded (" + std::string(stbi_failure_reason() ? stbi_failure_reason() : "?") +
                           "): " + (l_oSrc.uri && std::strncmp(l_oSrc.uri, "data:", 5) != 0 ? l_oSrc.uri : l_oOut.m_strName));
        return;
    }
    l_oOut.m_iWidth = l_iW;
    l_oOut.m_iHeight = l_iH;
    l_oOut.m_vRgba.assign(l_pPx, l_pPx + (size_t)l_iW * (size_t)l_iH * 4);
    stbi_image_free(l_pPx);
}

TextureRef f_TextureRef(const cgltf_data* l_pD, const cgltf_texture_view& l_oV) {
    TextureRef l_oR;
    l_oR.m_iTexture = f_IndexOf(l_oV.texture, l_pD->textures);
    l_oR.m_iUvSet = (int)l_oV.texcoord;
    if (l_oV.has_transform) {
        l_oR.m_v2Offset = {l_oV.transform.offset[0], l_oV.transform.offset[1]};
        l_oR.m_v2Scale = {l_oV.transform.scale[0], l_oV.transform.scale[1]};
        l_oR.m_fRotation = l_oV.transform.rotation;
        if (l_oV.transform.has_texcoord) l_oR.m_iUvSet = (int)l_oV.transform.texcoord;
    }
    return l_oR;
}

// Strips and fans to lists.
std::vector<uint32_t> f_ToList(cgltf_primitive_type l_eType, const std::vector<uint32_t>& l_vIn, Topology& l_eTopo) {
    std::vector<uint32_t> l_vOut;
    switch (l_eType) {
    case cgltf_primitive_type_triangle_strip:
        l_eTopo = Topology::Triangles;
        for (size_t l_nI = 2; l_nI < l_vIn.size(); l_nI++) {
            if (l_nI % 2 == 0) l_vOut.insert(l_vOut.end(), {l_vIn[l_nI - 2], l_vIn[l_nI - 1], l_vIn[l_nI]});
            else l_vOut.insert(l_vOut.end(), {l_vIn[l_nI - 1], l_vIn[l_nI - 2], l_vIn[l_nI]});
        }
        return l_vOut;
    case cgltf_primitive_type_triangle_fan:
        l_eTopo = Topology::Triangles;
        for (size_t l_nI = 2; l_nI < l_vIn.size(); l_nI++) l_vOut.insert(l_vOut.end(), {l_vIn[0], l_vIn[l_nI - 1], l_vIn[l_nI]});
        return l_vOut;
    case cgltf_primitive_type_line_strip:
    case cgltf_primitive_type_line_loop:
        l_eTopo = Topology::Lines;
        for (size_t l_nI = 1; l_nI < l_vIn.size(); l_nI++) l_vOut.insert(l_vOut.end(), {l_vIn[l_nI - 1], l_vIn[l_nI]});
        if (l_eType == cgltf_primitive_type_line_loop && l_vIn.size() > 2) l_vOut.insert(l_vOut.end(), {l_vIn.back(), l_vIn.front()});
        return l_vOut;
    case cgltf_primitive_type_lines: l_eTopo = Topology::Lines; return l_vIn;
    case cgltf_primitive_type_points: l_eTopo = Topology::Points; return l_vIn;
    default: l_eTopo = Topology::Triangles; return l_vIn;
    }
}

// No normals in the file: flat normals (the spec), so every triangle gets its own vertices.
template <class T> void f_Expand(std::vector<T>& l_vV, const std::vector<uint32_t>& l_vIdx) {
    if (l_vV.empty()) return;
    std::vector<T> l_vOut(l_vIdx.size());
    for (size_t l_nI = 0; l_nI < l_vIdx.size(); l_nI++) l_vOut[l_nI] = l_vV[l_vIdx[l_nI]];
    l_vV.swap(l_vOut);
}
void f_FlatNormals(Primitive& l_oP) {
    f_Expand(l_oP.m_vPosition, l_oP.m_vIndices);
    f_Expand(l_oP.m_vTangent, l_oP.m_vIndices);
    f_Expand(l_oP.m_vUv0, l_oP.m_vIndices);
    f_Expand(l_oP.m_vUv1, l_oP.m_vIndices);
    f_Expand(l_oP.m_vColor, l_oP.m_vIndices);
    f_Expand(l_oP.m_vJoints, l_oP.m_vIndices);
    f_Expand(l_oP.m_vWeights, l_oP.m_vIndices);
    for (MorphTarget& l_oT : l_oP.m_vTargets) {
        f_Expand(l_oT.m_vPosition, l_oP.m_vIndices);
        f_Expand(l_oT.m_vNormal, l_oP.m_vIndices);
        f_Expand(l_oT.m_vTangent, l_oP.m_vIndices);
    }
    l_oP.m_vNormal.assign(l_oP.m_vPosition.size(), glm::vec3(0, 0, 1));
    for (size_t l_nI = 0; l_nI + 2 < l_oP.m_vPosition.size(); l_nI += 3) {
        const glm::vec3 l_v3N = glm::cross(l_oP.m_vPosition[l_nI + 1] - l_oP.m_vPosition[l_nI], l_oP.m_vPosition[l_nI + 2] - l_oP.m_vPosition[l_nI]);
        const float l_fL = glm::length(l_v3N);
        const glm::vec3 l_v3Nn = l_fL > 0 ? l_v3N / l_fL : glm::vec3(0, 0, 1);
        l_oP.m_vNormal[l_nI] = l_oP.m_vNormal[l_nI + 1] = l_oP.m_vNormal[l_nI + 2] = l_v3Nn;
    }
    for (uint32_t l_uI = 0; l_uI < (uint32_t)l_oP.m_vIndices.size(); l_uI++) l_oP.m_vIndices[l_uI] = l_uI;
}

void f_ReadAttribute(const cgltf_attribute& l_oA, Primitive& l_oP) {
    switch (l_oA.type) {
    case cgltf_attribute_type_position: l_oP.m_vPosition = f_Vectors<glm::vec3>(l_oA.data, 3); break;
    case cgltf_attribute_type_normal: l_oP.m_vNormal = f_Vectors<glm::vec3>(l_oA.data, 3); break;
    case cgltf_attribute_type_tangent: l_oP.m_vTangent = f_Vectors<glm::vec4>(l_oA.data, 4, 1.0f); break;
    case cgltf_attribute_type_texcoord:
        if (l_oA.index == 0) l_oP.m_vUv0 = f_Vectors<glm::vec2>(l_oA.data, 2);
        else if (l_oA.index == 1) l_oP.m_vUv1 = f_Vectors<glm::vec2>(l_oA.data, 2);
        break;
    case cgltf_attribute_type_color:
        if (l_oA.index == 0) l_oP.m_vColor = f_Vectors<glm::vec4>(l_oA.data, 4, 1.0f);   // rgb: alpha stays 1
        break;
    case cgltf_attribute_type_joints:
        if (l_oA.index == 0) l_oP.m_vJoints = f_Vectors<glm::vec4>(l_oA.data, 4);
        break;
    case cgltf_attribute_type_weights:
        if (l_oA.index == 0) l_oP.m_vWeights = f_Vectors<glm::vec4>(l_oA.data, 4);
        break;
    default: break;
    }
}

// ---- physics: KHR_implicit_shapes + KHR_physics_rigid_bodies (cgltf keeps them as raw JSON) ------------

nlohmann::json f_ExtensionJson(const cgltf_extension* l_pExt, size_t l_nCount, const char* l_pszName) {
    for (size_t l_nI = 0; l_nI < l_nCount; l_nI++)
        if (l_pExt[l_nI].name && std::strcmp(l_pExt[l_nI].name, l_pszName) == 0 && l_pExt[l_nI].data)
            return nlohmann::json::parse(l_pExt[l_nI].data, nullptr, false);
    return nlohmann::json();
}

glm::vec3 f_Vec3Of(const nlohmann::json& l_jJ, const char* l_pszKey, glm::vec3 l_v3Def) {
    if (!l_jJ.contains(l_pszKey) || !l_jJ[l_pszKey].is_array() || l_jJ[l_pszKey].size() < 3) return l_v3Def;
    return {l_jJ[l_pszKey][0].get<float>(), l_jJ[l_pszKey][1].get<float>(), l_jJ[l_pszKey][2].get<float>()};
}

PhysicsCombine f_CombineOf(const nlohmann::json& l_jJ, const char* l_pszKey) {
    const std::string l_strV = l_jJ.value(l_pszKey, std::string());
    return l_strV == "average" ? PhysicsCombine::Average : l_strV == "minimum" ? PhysicsCombine::Minimum
         : l_strV == "maximum" ? PhysicsCombine::Maximum : l_strV == "multiply" ? PhysicsCombine::Multiply : PhysicsCombine::Unset;
}

std::vector<std::string> f_StringsOf(const nlohmann::json& l_jJ, const char* l_pszKey) {
    std::vector<std::string> l_vOut;
    if (l_jJ.contains(l_pszKey) && l_jJ[l_pszKey].is_array())
        for (const auto& l_jV : l_jJ[l_pszKey]) if (l_jV.is_string()) l_vOut.push_back(l_jV.get<std::string>());
    return l_vOut;
}

std::vector<int> f_IntsOf(const nlohmann::json& l_jJ, const char* l_pszKey) {
    std::vector<int> l_vOut;
    if (l_jJ.contains(l_pszKey) && l_jJ[l_pszKey].is_array())
        for (const auto& l_jV : l_jJ[l_pszKey]) if (l_jV.is_number_integer()) l_vOut.push_back(l_jV.get<int>());
    return l_vOut;
}

PhysicsGeometry f_GeometryOf(const nlohmann::json& l_jJ) {
    PhysicsGeometry l_oG;
    if (!l_jJ.is_object()) return l_oG;
    l_oG.m_iShape = l_jJ.value("shape", -1);
    l_oG.m_iMesh = l_jJ.value("mesh", -1);
    l_oG.m_bConvexHull = l_jJ.value("convexHull", false);
    return l_oG;
}

// Reads the two extensions into m.physics. Indices are checked here, so the simulation can trust them.
void f_ReadPhysics(const cgltf_data* l_pD, Model& l_oM) {
    const nlohmann::json l_jShapes = f_ExtensionJson(l_pD->data_extensions, l_pD->data_extensions_count, "KHR_implicit_shapes");
    const nlohmann::json l_jDoc = f_ExtensionJson(l_pD->data_extensions, l_pD->data_extensions_count, "KHR_physics_rigid_bodies");
    Physics& l_oPh = l_oM.m_oPhysics;
    if (l_jShapes.is_object() && l_jShapes.contains("shapes") && l_jShapes["shapes"].is_array())
        for (const auto& l_jJ : l_jShapes["shapes"]) {
            PhysicsShape l_oS;
            const std::string l_strType = l_jJ.value("type", std::string());
            const nlohmann::json l_jBody = l_jJ.contains(l_strType) ? l_jJ[l_strType] : nlohmann::json::object();
            if (l_strType == "sphere") { l_oS.m_eType = PhysicsShape::Type::Sphere; l_oS.m_fRadius = l_jBody.value("radius", 0.5f); }
            else if (l_strType == "box") { l_oS.m_eType = PhysicsShape::Type::Box; l_oS.m_v3Size = f_Vec3Of(l_jBody, "size", glm::vec3(1.0f)); }
            else if (l_strType == "capsule" || l_strType == "cylinder") {
                l_oS.m_eType = l_strType == "capsule" ? PhysicsShape::Type::Capsule : PhysicsShape::Type::Cylinder;
                l_oS.m_fHeight = l_jBody.value("height", 0.5f);
                l_oS.m_fRadiusTop = l_jBody.value("radiusTop", 0.25f);
                l_oS.m_fRadiusBottom = l_jBody.value("radiusBottom", 0.25f);
            } else if (l_strType == "plane") {
                l_oS.m_eType = PhysicsShape::Type::Plane;
                l_oS.m_fSizeX = l_jBody.value("sizeX", 0.0f);
                l_oS.m_fSizeZ = l_jBody.value("sizeZ", 0.0f);
                l_oS.m_bDoubleSided = l_jBody.value("doubleSided", false);
            } else {
                l_oM.m_vWarnings.push_back("physics: unknown implicit shape type '" + l_strType + "' (read as a 1 m box)");
            }
            l_oPh.m_vShapes.push_back(l_oS);
        }
    if (l_jDoc.is_object()) {
        if (l_jDoc.contains("physicsMaterials") && l_jDoc["physicsMaterials"].is_array())
            for (const auto& l_jJ : l_jDoc["physicsMaterials"]) {
                PhysicsMaterial l_oPm;
                l_oPm.m_fStaticFriction = l_jJ.value("staticFriction", 0.6f);
                l_oPm.m_fDynamicFriction = l_jJ.value("dynamicFriction", 0.6f);
                l_oPm.m_fRestitution = l_jJ.value("restitution", 0.0f);
                l_oPm.m_eFrictionCombine = f_CombineOf(l_jJ, "frictionCombine");
                l_oPm.m_eRestitutionCombine = f_CombineOf(l_jJ, "restitutionCombine");
                l_oPh.m_vMaterials.push_back(l_oPm);
            }
        if (l_jDoc.contains("collisionFilters") && l_jDoc["collisionFilters"].is_array())
            for (const auto& l_jJ : l_jDoc["collisionFilters"]) {
                PhysicsFilter l_oF;
                l_oF.m_vSystems = f_StringsOf(l_jJ, "collisionSystems");
                l_oF.m_vCollideWith = f_StringsOf(l_jJ, "collideWithSystems");
                l_oF.m_vNotCollideWith = f_StringsOf(l_jJ, "notCollideWithSystems");
                l_oPh.m_vFilters.push_back(l_oF);
            }
        if (l_jDoc.contains("physicsJoints") && l_jDoc["physicsJoints"].is_array())
            for (const auto& l_jJ : l_jDoc["physicsJoints"]) {
                PhysicsJoint l_oPj;
                if (l_jJ.contains("limits") && l_jJ["limits"].is_array())
                    for (const auto& l_jL : l_jJ["limits"]) {
                        PhysicsJointLimit l_oO;
                        l_oO.m_vLinearAxes = f_IntsOf(l_jL, "linearAxes");
                        l_oO.m_vAngularAxes = f_IntsOf(l_jL, "angularAxes");
                        l_oO.m_bHasMin = l_jL.contains("min");
                        l_oO.m_bHasMax = l_jL.contains("max");
                        l_oO.m_fMin = l_jL.value("min", 0.0f);
                        l_oO.m_fMax = l_jL.value("max", 0.0f);
                        l_oO.m_fStiffness = l_jL.contains("stiffness") ? l_jL.value("stiffness", 0.0f) : -1.0f;
                        l_oO.m_fDamping = l_jL.value("damping", 0.0f);
                        l_oPj.m_vLimits.push_back(l_oO);
                    }
                if (l_jJ.contains("drives") && l_jJ["drives"].is_array())
                    for (const auto& l_jDr : l_jJ["drives"]) {
                        PhysicsJointDrive l_oO;
                        l_oO.m_bAngular = l_jDr.value("type", std::string()) == "angular";
                        l_oO.m_bAcceleration = l_jDr.value("mode", std::string()) == "acceleration";
                        l_oO.m_iAxis = std::clamp(l_jDr.value("axis", 0), 0, 2);
                        l_oO.m_fMaxForce = l_jDr.contains("maxForce") ? l_jDr.value("maxForce", 0.0f) : -1.0f;
                        l_oO.m_bHasPositionTarget = l_jDr.contains("positionTarget");
                        l_oO.m_bHasVelocityTarget = l_jDr.contains("velocityTarget");
                        l_oO.m_fPositionTarget = l_jDr.value("positionTarget", 0.0f);
                        l_oO.m_fVelocityTarget = l_jDr.value("velocityTarget", 0.0f);
                        l_oO.m_fStiffness = l_jDr.value("stiffness", 0.0f);
                        l_oO.m_fDamping = l_jDr.value("damping", 0.0f);
                        l_oPj.m_vDrives.push_back(l_oO);
                    }
                l_oPh.m_vJoints.push_back(l_oPj);
            }
    }
    bool l_bAnyNode = false;
    std::vector<NodePhysics> l_vNodes(l_pD->nodes_count);
    auto l_fnCheckGeometry = [&](PhysicsGeometry& l_oG, const std::string& l_strWhere) {
        if (l_oG.m_iShape >= (int)l_oPh.m_vShapes.size()) { l_oM.m_vWarnings.push_back(l_strWhere + ": shape " + std::to_string(l_oG.m_iShape) + " does not exist"); l_oG.m_iShape = -1; }
        if (l_oG.m_iMesh >= (int)l_oM.m_vMeshes.size()) { l_oM.m_vWarnings.push_back(l_strWhere + ": mesh " + std::to_string(l_oG.m_iMesh) + " does not exist"); l_oG.m_iMesh = -1; }
    };
    for (size_t l_nI = 0; l_nI < l_pD->nodes_count; l_nI++) {
        const nlohmann::json l_jJ = f_ExtensionJson(l_pD->nodes[l_nI].extensions, l_pD->nodes[l_nI].extensions_count, "KHR_physics_rigid_bodies");
        if (!l_jJ.is_object()) continue;
        l_bAnyNode = true;
        NodePhysics& l_oN = l_vNodes[l_nI];
        const std::string l_strWhere = "physics, node '" + l_oM.m_vNodes[l_nI].m_strName + "'";
        if (l_jJ.contains("motion") && l_jJ["motion"].is_object()) {
            const auto& l_jMo = l_jJ["motion"];
            l_oN.m_bHasMotion = true;
            l_oN.m_bKinematic = l_jMo.value("isKinematic", false);
            l_oN.m_fMass = l_jMo.contains("mass") ? l_jMo.value("mass", 1.0f) : -1.0f;
            l_oN.m_bHasCenterOfMass = l_jMo.contains("centerOfMass");
            l_oN.m_v3CenterOfMass = f_Vec3Of(l_jMo, "centerOfMass", glm::vec3(0.0f));
            l_oN.m_bHasInertia = l_jMo.contains("inertiaDiagonal");
            l_oN.m_v3InertiaDiagonal = f_Vec3Of(l_jMo, "inertiaDiagonal", glm::vec3(0.0f));
            if (l_jMo.contains("inertiaOrientation") && l_jMo["inertiaOrientation"].is_array() && l_jMo["inertiaOrientation"].size() == 4) {
                const auto& l_jQ = l_jMo["inertiaOrientation"];
                l_oN.m_qInertiaOrientation = glm::normalize(glm::quat(l_jQ[3].get<float>(), l_jQ[0].get<float>(), l_jQ[1].get<float>(), l_jQ[2].get<float>()));
            }
            l_oN.m_v3LinearVelocity = f_Vec3Of(l_jMo, "linearVelocity", glm::vec3(0.0f));
            l_oN.m_v3AngularVelocity = f_Vec3Of(l_jMo, "angularVelocity", glm::vec3(0.0f));
            l_oN.m_fGravityFactor = l_jMo.value("gravityFactor", 1.0f);
        }
        if (l_jJ.contains("collider") && l_jJ["collider"].is_object()) {
            const auto& l_jC = l_jJ["collider"];
            l_oN.m_oCollider = f_GeometryOf(l_jC.value("geometry", nlohmann::json::object()));
            l_fnCheckGeometry(l_oN.m_oCollider, l_strWhere);
            l_oN.m_bHasCollider = l_oN.m_oCollider.f_Valid();
            l_oN.m_iMaterial = l_jC.value("physicsMaterial", -1);
            l_oN.m_iFilter = l_jC.value("collisionFilter", -1);
            if (l_oN.m_iMaterial >= (int)l_oPh.m_vMaterials.size()) l_oN.m_iMaterial = -1;
            if (l_oN.m_iFilter >= (int)l_oPh.m_vFilters.size()) l_oN.m_iFilter = -1;
        }
        if (l_jJ.contains("trigger") && l_jJ["trigger"].is_object()) {
            const auto& l_jT = l_jJ["trigger"];
            l_oN.m_oTrigger = f_GeometryOf(l_jT.value("geometry", nlohmann::json::object()));
            l_fnCheckGeometry(l_oN.m_oTrigger, l_strWhere);
            for (int l_iK : f_IntsOf(l_jT, "nodes")) if (l_iK >= 0 && l_iK < (int)l_pD->nodes_count) l_oN.m_vTriggerNodes.push_back(l_iK);
            l_oN.m_bHasTrigger = l_oN.m_oTrigger.f_Valid() || !l_oN.m_vTriggerNodes.empty();
            l_oN.m_iTriggerFilter = l_jT.value("collisionFilter", -1);
            if (l_oN.m_iTriggerFilter >= (int)l_oPh.m_vFilters.size()) l_oN.m_iTriggerFilter = -1;
        }
        if (l_jJ.contains("joint") && l_jJ["joint"].is_object()) {
            const auto& l_jJo = l_jJ["joint"];
            l_oN.m_iConnectedNode = l_jJo.value("connectedNode", -1);
            l_oN.m_iJoint = l_jJo.value("joint", -1);
            l_oN.m_bEnableCollision = l_jJo.value("enableCollision", false);
            l_oN.m_bHasJoint = l_oN.m_iConnectedNode >= 0 && l_oN.m_iConnectedNode < (int)l_pD->nodes_count && l_oN.m_iJoint >= 0 && l_oN.m_iJoint < (int)l_oPh.m_vJoints.size();
            if (!l_oN.m_bHasJoint) l_oM.m_vWarnings.push_back(l_strWhere + ": a joint with a bad connectedNode or joint index (ignored)");
        }
    }
    if (l_bAnyNode) l_oPh.m_vNodes = std::move(l_vNodes);
}

bool f_Convert(const cgltf_data* l_pD, const std::string& l_strBaseDir, Model& l_oM, std::string* l_pError) {
    l_oM.m_strGenerator = l_pD->asset.generator ? l_pD->asset.generator : "";
    for (size_t l_nI = 0; l_nI < l_pD->extensions_used_count; l_nI++) {
        const std::string l_strE = l_pD->extensions_used[l_nI];
        l_oM.m_vExtensionsUsed.push_back(l_strE);
        if (!f_Supported(l_strE)) l_oM.m_vIgnoredExtensions.push_back(l_strE);
    }
    for (size_t l_nI = 0; l_nI < l_pD->extensions_required_count; l_nI++) {
        const std::string l_strE = l_pD->extensions_required[l_nI];
        if (!f_Supported(l_strE)) {
            if (l_pError) *l_pError = "the file requires the extension " + l_strE + ", which this engine cannot read yet "
                               "(re-export without it, e.g. gltf-transform: no Draco / meshopt / KTX2)";
            return false;
        }
    }

    // Images and textures.
    l_oM.m_vImages.resize(l_pD->images_count);
    for (size_t l_nI = 0; l_nI < l_pD->images_count; l_nI++) f_DecodeImage(l_pD->images[l_nI], l_strBaseDir, l_oM.m_vImages[l_nI], l_oM.m_vWarnings);
    l_oM.m_vTextures.resize(l_pD->textures_count);
    for (size_t l_nI = 0; l_nI < l_pD->textures_count; l_nI++) {
        const cgltf_texture& l_oT = l_pD->textures[l_nI];
        Texture& l_oO = l_oM.m_vTextures[l_nI];
        l_oO.m_iImage = f_IndexOf(l_oT.image, l_pD->images);
        if (const cgltf_sampler* l_pS = l_oT.sampler) {
            l_oO.m_bRepeatS = (int)l_pS->wrap_s == 10497;
            l_oO.m_bRepeatT = (int)l_pS->wrap_t == 10497;
            l_oO.m_bMirrorS = (int)l_pS->wrap_s == 33648;
            l_oO.m_bMirrorT = (int)l_pS->wrap_t == 33648;
            l_oO.m_bNearest = (int)l_pS->mag_filter == 9728;
        }
    }

    // Materials.
    l_oM.m_vMaterials.resize(l_pD->materials_count);
    for (size_t l_nI = 0; l_nI < l_pD->materials_count; l_nI++) {
        const cgltf_material& l_oS = l_pD->materials[l_nI];
        Material& l_oO = l_oM.m_vMaterials[l_nI];
        l_oO.m_strName = l_oS.name ? l_oS.name : "";
        if (l_oS.has_pbr_metallic_roughness) {
            const auto& l_oPbr = l_oS.pbr_metallic_roughness;
            l_oO.m_v4BaseColor = glm::make_vec4(l_oPbr.base_color_factor);
            l_oO.m_fMetallic = l_oPbr.metallic_factor;
            l_oO.m_fRoughness = l_oPbr.roughness_factor;
            l_oO.m_oBaseColorTex = f_TextureRef(l_pD, l_oPbr.base_color_texture);
            l_oO.m_oMetalRoughTex = f_TextureRef(l_pD, l_oPbr.metallic_roughness_texture);
        } else if (l_oS.has_pbr_specular_glossiness) {   // older files: diffuse as base colour, no metal
            const auto& l_oSg = l_oS.pbr_specular_glossiness;
            l_oO.m_v4BaseColor = glm::make_vec4(l_oSg.diffuse_factor);
            l_oO.m_fMetallic = 0.0f;
            l_oO.m_fRoughness = 1.0f - l_oSg.glossiness_factor;
            l_oO.m_oBaseColorTex = f_TextureRef(l_pD, l_oSg.diffuse_texture);
        }
        l_oO.m_oNormalTex = f_TextureRef(l_pD, l_oS.normal_texture);
        l_oO.m_fNormalScale = l_oS.normal_texture.texture ? l_oS.normal_texture.scale : 1.0f;
        l_oO.m_oOcclusionTex = f_TextureRef(l_pD, l_oS.occlusion_texture);
        l_oO.m_fOcclusionStrength = l_oS.occlusion_texture.texture ? l_oS.occlusion_texture.scale : 1.0f;
        l_oO.m_oEmissiveTex = f_TextureRef(l_pD, l_oS.emissive_texture);
        l_oO.m_v3Emissive = glm::make_vec3(l_oS.emissive_factor) * (l_oS.has_emissive_strength ? l_oS.emissive_strength.emissive_strength : 1.0f);
        l_oO.m_eAlphaMode = l_oS.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask
                    : l_oS.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend : AlphaMode::Opaque;
        l_oO.m_fAlphaCutoff = l_oS.alpha_cutoff;
        l_oO.m_bDoubleSided = l_oS.double_sided;
        l_oO.m_bUnlit = l_oS.unlit;
        if (l_oS.has_transmission) l_oO.m_fTransmission = l_oS.transmission.transmission_factor;
        if (l_oS.has_volume) l_oO.m_v3AttenuationColor = glm::make_vec3(l_oS.volume.attenuation_color);
        if (l_oS.has_specular) {
            l_oO.m_fSpecular = l_oS.specular.specular_factor;
            l_oO.m_v3SpecularColor = glm::make_vec3(l_oS.specular.specular_color_factor);
            l_oO.m_oSpecularTex = f_TextureRef(l_pD, l_oS.specular.specular_texture);
            l_oO.m_oSpecularColorTex = f_TextureRef(l_pD, l_oS.specular.specular_color_texture);
        }
        if (l_oS.has_ior) l_oO.m_fIor = l_oS.ior.ior;
        for (const TextureRef* l_pR : {&l_oO.m_oBaseColorTex, &l_oO.m_oEmissiveTex, &l_oO.m_oSpecularColorTex})   // colour data: sRGB
            if (l_pR->f_Valid()) l_oM.m_vTextures[(size_t)l_pR->m_iTexture].m_bSrgb = true;
    }

    // Meshes.
    l_oM.m_vMeshes.resize(l_pD->meshes_count);
    for (size_t l_nI = 0; l_nI < l_pD->meshes_count; l_nI++) {
        const cgltf_mesh& l_oS = l_pD->meshes[l_nI];
        Mesh& l_oO = l_oM.m_vMeshes[l_nI];
        l_oO.m_strName = l_oS.name ? l_oS.name : "";
        l_oO.m_vWeights.assign(l_oS.weights, l_oS.weights + l_oS.weights_count);
        for (size_t l_nT = 0; l_nT < l_oS.target_names_count; l_nT++) l_oO.m_vTargetNames.push_back(l_oS.target_names[l_nT]);
        for (size_t l_nPi = 0; l_nPi < l_oS.primitives_count; l_nPi++) {
            const cgltf_primitive& l_oSp = l_oS.primitives[l_nPi];
            if (l_oSp.has_draco_mesh_compression) {
                l_oM.m_vWarnings.push_back("mesh '" + l_oO.m_strName + "': a Draco-compressed primitive is skipped");
                continue;
            }
            Primitive l_oP;
            l_oP.m_iMaterial = f_IndexOf(l_oSp.material, l_pD->materials);
            for (size_t l_nA = 0; l_nA < l_oSp.attributes_count; l_nA++) f_ReadAttribute(l_oSp.attributes[l_nA], l_oP);
            if (l_oP.m_vPosition.empty()) continue;
            std::vector<uint32_t> l_vIdx;
            if (l_oSp.indices) {
                l_vIdx.resize(l_oSp.indices->count);
                for (size_t l_nK = 0; l_nK < l_vIdx.size(); l_nK++) l_vIdx[l_nK] = (uint32_t)cgltf_accessor_read_index(l_oSp.indices, l_nK);
            } else {
                l_vIdx.resize(l_oP.m_vPosition.size());
                for (uint32_t l_uK = 0; l_uK < (uint32_t)l_vIdx.size(); l_uK++) l_vIdx[l_uK] = l_uK;
            }
            l_oP.m_vIndices = f_ToList(l_oSp.type, l_vIdx, l_oP.m_eTopology);
            for (size_t l_nT = 0; l_nT < l_oSp.targets_count; l_nT++) {
                MorphTarget l_oMt;
                for (size_t l_nA = 0; l_nA < l_oSp.targets[l_nT].attributes_count; l_nA++) {
                    const cgltf_attribute& l_oTa = l_oSp.targets[l_nT].attributes[l_nA];
                    if (l_oTa.type == cgltf_attribute_type_position) l_oMt.m_vPosition = f_Vectors<glm::vec3>(l_oTa.data, 3);
                    else if (l_oTa.type == cgltf_attribute_type_normal) l_oMt.m_vNormal = f_Vectors<glm::vec3>(l_oTa.data, 3);
                    else if (l_oTa.type == cgltf_attribute_type_tangent) l_oMt.m_vTangent = f_Vectors<glm::vec3>(l_oTa.data, 3);
                }
                l_oP.m_vTargets.push_back(std::move(l_oMt));
            }
            if (l_oP.m_vNormal.empty() && l_oP.m_eTopology == Topology::Triangles) f_FlatNormals(l_oP);
            l_oP.m_v3BoundsMin = l_oP.m_v3BoundsMax = l_oP.m_vPosition[0];
            for (const glm::vec3& l_v3V : l_oP.m_vPosition) {
                l_oP.m_v3BoundsMin = glm::min(l_oP.m_v3BoundsMin, l_v3V);
                l_oP.m_v3BoundsMax = glm::max(l_oP.m_v3BoundsMax, l_v3V);
            }
            l_oO.m_vPrimitives.push_back(std::move(l_oP));
        }
        size_t l_nTargets = 0;
        for (const Primitive& l_oP : l_oO.m_vPrimitives) l_nTargets = std::max(l_nTargets, l_oP.m_vTargets.size());
        l_oO.m_vWeights.resize(l_nTargets, 0.0f);
    }

    // Nodes.
    l_oM.m_vNodes.resize(l_pD->nodes_count);
    for (size_t l_nI = 0; l_nI < l_pD->nodes_count; l_nI++) {
        const cgltf_node& l_oS = l_pD->nodes[l_nI];
        Node& l_oO = l_oM.m_vNodes[l_nI];
        l_oO.m_strName = l_oS.name ? l_oS.name : "";
        l_oO.m_iParent = f_IndexOf(l_oS.parent, l_pD->nodes);
        for (size_t l_nC = 0; l_nC < l_oS.children_count; l_nC++) l_oO.m_vChildren.push_back(f_IndexOf(l_oS.children[l_nC], l_pD->nodes));
        if (l_oS.has_matrix) {   // decompose: animations and the editor work on T, R, S
            glm::mat4 l_mat4Mt = glm::make_mat4(l_oS.matrix);
            l_oO.m_v3Translation = glm::vec3(l_mat4Mt[3]);
            glm::vec3 l_v3Sc(glm::length(glm::vec3(l_mat4Mt[0])), glm::length(glm::vec3(l_mat4Mt[1])), glm::length(glm::vec3(l_mat4Mt[2])));
            if (glm::determinant(glm::mat3(l_mat4Mt)) < 0) l_v3Sc.x = -l_v3Sc.x;   // mirrored
            l_oO.m_v3Scale = l_v3Sc;
            glm::mat3 l_mat3Rm(glm::vec3(l_mat4Mt[0]) / (l_v3Sc.x != 0 ? l_v3Sc.x : 1.0f), glm::vec3(l_mat4Mt[1]) / (l_v3Sc.y != 0 ? l_v3Sc.y : 1.0f),
                         glm::vec3(l_mat4Mt[2]) / (l_v3Sc.z != 0 ? l_v3Sc.z : 1.0f));
            l_oO.m_qRotation = glm::normalize(glm::quat_cast(l_mat3Rm));
        } else {
            if (l_oS.has_translation) l_oO.m_v3Translation = glm::make_vec3(l_oS.translation);
            if (l_oS.has_rotation) l_oO.m_qRotation = glm::normalize(glm::quat(l_oS.rotation[3], l_oS.rotation[0], l_oS.rotation[1], l_oS.rotation[2]));
            if (l_oS.has_scale) l_oO.m_v3Scale = glm::make_vec3(l_oS.scale);
        }
        l_oO.m_iMesh = f_IndexOf(l_oS.mesh, l_pD->meshes);
        l_oO.m_iSkin = f_IndexOf(l_oS.skin, l_pD->skins);
        l_oO.m_vWeights.assign(l_oS.weights, l_oS.weights + l_oS.weights_count);
        if (l_oS.has_mesh_gpu_instancing) {
            std::vector<glm::vec3> l_vT, l_vSc;
            std::vector<glm::vec4> l_vR;
            size_t l_nCount = 0;
            for (size_t l_nA = 0; l_nA < l_oS.mesh_gpu_instancing.attributes_count; l_nA++) {
                const cgltf_attribute& l_oAt = l_oS.mesh_gpu_instancing.attributes[l_nA];
                const std::string l_strName = l_oAt.name ? l_oAt.name : "";
                if (l_strName == "TRANSLATION") l_vT = f_Vectors<glm::vec3>(l_oAt.data, 3);
                else if (l_strName == "ROTATION") l_vR = f_Vectors<glm::vec4>(l_oAt.data, 4);
                else if (l_strName == "SCALE") l_vSc = f_Vectors<glm::vec3>(l_oAt.data, 3, 1.0f);
                if (l_oAt.data) l_nCount = std::max(l_nCount, (size_t)l_oAt.data->count);
            }
            for (size_t l_nK = 0; l_nK < l_nCount; l_nK++) {
                const glm::vec3 l_v3Tt = l_nK < l_vT.size() ? l_vT[l_nK] : glm::vec3(0.0f);
                const glm::quat l_qRr = l_nK < l_vR.size() ? glm::normalize(glm::quat(l_vR[l_nK].w, l_vR[l_nK].x, l_vR[l_nK].y, l_vR[l_nK].z)) : glm::quat(1, 0, 0, 0);
                const glm::vec3 l_v3Ss = l_nK < l_vSc.size() ? l_vSc[l_nK] : glm::vec3(1.0f);
                l_oO.m_vInstances.push_back(f_ComposeTRS(l_v3Tt, l_qRr, l_v3Ss));
            }
        }
    }

    // Cameras. Unnamed ones take their node's name, else the nearest named parent's.
    l_oM.m_vCameras.resize(l_pD->cameras_count);
    for (size_t l_nI = 0; l_nI < l_pD->cameras_count; l_nI++) {
        const cgltf_camera& l_oS = l_pD->cameras[l_nI];
        Camera& l_oO = l_oM.m_vCameras[l_nI];
        l_oO.m_strName = l_oS.name ? l_oS.name : "";
        if (l_oS.type == cgltf_camera_type_orthographic) {
            l_oO.m_bPerspective = false;
            l_oO.m_fXmag = l_oS.data.orthographic.xmag;
            l_oO.m_fYmag = l_oS.data.orthographic.ymag;
            l_oO.m_fZnear = l_oS.data.orthographic.znear;
            l_oO.m_fZfar = l_oS.data.orthographic.zfar;
        } else {
            l_oO.m_fYfov = l_oS.data.perspective.yfov;
            l_oO.m_fAspectRatio = l_oS.data.perspective.has_aspect_ratio ? l_oS.data.perspective.aspect_ratio : 0.0f;
            l_oO.m_fZnear = l_oS.data.perspective.znear;
            l_oO.m_fZfar = l_oS.data.perspective.has_zfar ? l_oS.data.perspective.zfar : 0.0f;
        }
    }
    for (size_t l_nI = 0; l_nI < l_pD->nodes_count; l_nI++) {
        const int l_iCam = f_IndexOf(l_pD->nodes[l_nI].camera, l_pD->cameras);
        l_oM.m_vNodes[l_nI].m_iCamera = l_iCam;
        if (l_iCam < 0 || !l_oM.m_vCameras[(size_t)l_iCam].m_strName.empty()) continue;
        for (const cgltf_node* l_pN = &l_pD->nodes[l_nI]; l_pN; l_pN = l_pN->parent)
            if (l_pN->name && *l_pN->name) {
                l_oM.m_vCameras[(size_t)l_iCam].m_strName = l_pN->name;
                break;
            }
    }

    // Lights (KHR_lights_punctual).
    l_oM.m_vLights.resize(l_pD->lights_count);
    for (size_t l_nI = 0; l_nI < l_pD->lights_count; l_nI++) {
        const cgltf_light& l_oS = l_pD->lights[l_nI];
        Light& l_oO = l_oM.m_vLights[l_nI];
        l_oO.m_strName = l_oS.name ? l_oS.name : "";
        l_oO.m_eType = l_oS.type == cgltf_light_type_directional ? Light::Type::Directional
               : l_oS.type == cgltf_light_type_spot ? Light::Type::Spot : Light::Type::Point;
        l_oO.m_v3Color = glm::make_vec3(l_oS.color);
        l_oO.m_fIntensity = l_oS.intensity;
        l_oO.m_fRange = l_oS.range;
        l_oO.m_fInnerCone = l_oS.spot_inner_cone_angle;
        l_oO.m_fOuterCone = l_oS.spot_outer_cone_angle > 0 ? l_oS.spot_outer_cone_angle : l_oO.m_fOuterCone;
    }
    for (size_t l_nI = 0; l_nI < l_pD->nodes_count; l_nI++) l_oM.m_vNodes[l_nI].m_iLight = f_IndexOf(l_pD->nodes[l_nI].light, l_pD->lights);

    // Skins.
    l_oM.m_vSkins.resize(l_pD->skins_count);
    for (size_t l_nI = 0; l_nI < l_pD->skins_count; l_nI++) {
        const cgltf_skin& l_oS = l_pD->skins[l_nI];
        Skin& l_oO = l_oM.m_vSkins[l_nI];
        l_oO.m_strName = l_oS.name ? l_oS.name : "";
        for (size_t l_nJ = 0; l_nJ < l_oS.joints_count; l_nJ++) l_oO.m_vJoints.push_back(f_IndexOf(l_oS.joints[l_nJ], l_pD->nodes));
        l_oO.m_vInverseBind.assign(l_oO.m_vJoints.size(), glm::mat4(1.0f));
        if (l_oS.inverse_bind_matrices) {
            const std::vector<float> l_vF = f_Floats(l_oS.inverse_bind_matrices);
            for (size_t l_nJ = 0; l_nJ < l_oO.m_vJoints.size() && (l_nJ + 1) * 16 <= l_vF.size(); l_nJ++) l_oO.m_vInverseBind[l_nJ] = glm::make_mat4(&l_vF[l_nJ * 16]);
        }
        if ((int)l_oO.m_vJoints.size() > g_iMaxJoints)
            l_oM.m_vWarnings.push_back("skin '" + l_oO.m_strName + "' has " + std::to_string(l_oO.m_vJoints.size()) + " joints; only " +
                                 std::to_string(g_iMaxJoints) + " are animated");
    }

    // Animations.
    l_oM.m_vAnimations.resize(l_pD->animations_count);
    for (size_t l_nI = 0; l_nI < l_pD->animations_count; l_nI++) {
        const cgltf_animation& l_oS = l_pD->animations[l_nI];
        Animation& l_oO = l_oM.m_vAnimations[l_nI];
        l_oO.m_strName = l_oS.name ? l_oS.name : "animation " + std::to_string(l_nI);
        l_oO.m_vSamplers.resize(l_oS.samplers_count);
        for (size_t l_nK = 0; l_nK < l_oS.samplers_count; l_nK++) {
            const cgltf_animation_sampler& l_oSs = l_oS.samplers[l_nK];
            Sampler& l_oSo = l_oO.m_vSamplers[l_nK];
            l_oSo.m_vInput = f_Floats(l_oSs.input);
            l_oSo.m_vOutput = f_Floats(l_oSs.output);
            l_oSo.m_eInterpolation = l_oSs.interpolation == cgltf_interpolation_type_step ? Interpolation::Step
                             : l_oSs.interpolation == cgltf_interpolation_type_cubic_spline ? Interpolation::CubicSpline
                                                                                         : Interpolation::Linear;
            const size_t l_nKeys = l_oSo.m_vInput.size();
            const size_t l_nPer = l_oSo.m_eInterpolation == Interpolation::CubicSpline ? 3 : 1;
            l_oSo.m_iComponents = l_nKeys ? int(l_oSo.m_vOutput.size() / (l_nKeys * l_nPer)) : 0;
            if (!l_oSo.m_vInput.empty()) l_oO.m_fDuration = std::max(l_oO.m_fDuration, l_oSo.m_vInput.back());
        }
        for (size_t l_nK = 0; l_nK < l_oS.channels_count; l_nK++) {
            const cgltf_animation_channel& l_oC = l_oS.channels[l_nK];
            if (!l_oC.target_node || !l_oC.sampler) continue;
            Channel l_oCo;
            l_oCo.m_iSampler = f_IndexOf(l_oC.sampler, l_oS.samplers);
            l_oCo.m_iNode = f_IndexOf(l_oC.target_node, l_pD->nodes);
            switch (l_oC.target_path) {
            case cgltf_animation_path_type_translation: l_oCo.m_ePath = Path::Translation; break;
            case cgltf_animation_path_type_rotation: l_oCo.m_ePath = Path::Rotation; break;
            case cgltf_animation_path_type_scale: l_oCo.m_ePath = Path::Scale; break;
            case cgltf_animation_path_type_weights: l_oCo.m_ePath = Path::Weights; break;
            default: continue;
            }
            l_oO.m_vChannels.push_back(l_oCo);
        }
    }

    // Scenes (a file without one: every root node).
    for (size_t l_nI = 0; l_nI < l_pD->scenes_count; l_nI++) {
        Scene l_oSc;
        l_oSc.m_strName = l_pD->scenes[l_nI].name ? l_pD->scenes[l_nI].name : "";
        for (size_t l_nK = 0; l_nK < l_pD->scenes[l_nI].nodes_count; l_nK++) l_oSc.m_vRoots.push_back(f_IndexOf(l_pD->scenes[l_nI].nodes[l_nK], l_pD->nodes));
        l_oM.m_vScenes.push_back(l_oSc);
    }
    if (l_oM.m_vScenes.empty()) {
        Scene l_oSc;
        for (size_t l_nI = 0; l_nI < l_oM.m_vNodes.size(); l_nI++)
            if (l_oM.m_vNodes[l_nI].m_iParent < 0) l_oSc.m_vRoots.push_back((int)l_nI);
        l_oM.m_vScenes.push_back(l_oSc);
    }
    l_oM.m_iScene = l_pD->scene ? f_IndexOf(l_pD->scene, l_pD->scenes) : 0;
    f_ReadPhysics(l_pD, l_oM);   // KHR_physics_rigid_bodies + KHR_implicit_shapes (after the meshes and nodes)
    return true;
}

bool f_Finish(cgltf_result l_eR, cgltf_data* l_pD, const cgltf_options& l_oOpt, const std::string& l_strGltfPath, const std::string& l_strBaseDir,
            Model& l_oOut, std::string* l_pError) {
    if (l_eR != cgltf_result_success) {
        if (l_pError) *l_pError = "not a valid glTF file (cgltf error " + std::to_string((int)l_eR) + ")";
        return false;
    }
    l_eR = cgltf_load_buffers(&l_oOpt, l_pD, l_strGltfPath.c_str());
    if (l_eR != cgltf_result_success) {
        cgltf_free(l_pD);
        if (l_pError) *l_pError = "a buffer (.bin) could not be loaded (cgltf error " + std::to_string((int)l_eR) + ")";
        return false;
    }
    if (cgltf_validate(l_pD) != cgltf_result_success) {
        cgltf_free(l_pD);
        if (l_pError) *l_pError = "the file does not validate (an accessor or buffer is out of range)";
        return false;
    }
    l_oOut = Model();
    l_oOut.m_strPath = l_strGltfPath;
    const bool l_bOk = f_Convert(l_pD, l_strBaseDir, l_oOut, l_pError);
    cgltf_free(l_pD);
    return l_bOk;
}

// ---- animation sampling ---------------------------------------------------------------------------

// Key segment for t: i (and i + 1), with the 0..1 position s inside it.
void f_Locate(const std::vector<float>& l_vIn, float l_fT, size_t& l_nI, float& l_fS) {
    if (l_fT <= l_vIn.front()) {
        l_nI = 0;
        l_fS = 0;
        return;
    }
    if (l_fT >= l_vIn.back()) {
        l_nI = l_vIn.size() - 1;
        l_fS = 0;
        return;
    }
    l_nI = size_t(std::upper_bound(l_vIn.begin(), l_vIn.end(), l_fT) - l_vIn.begin()) - 1;
    const float l_fDt = l_vIn[l_nI + 1] - l_vIn[l_nI];
    l_fS = l_fDt > 0 ? (l_fT - l_vIn[l_nI]) / l_fDt : 0.0f;
}

void f_SampleValue(const Sampler& l_oSm, float l_fT, bool l_bIsRotation, std::vector<float>& l_vOut) {
    const size_t l_nN = (size_t)l_oSm.m_iComponents;
    l_vOut.assign(l_nN, 0.0f);
    if (l_oSm.m_vInput.empty() || l_nN == 0) return;
    size_t l_nI;
    float l_fS;
    f_Locate(l_oSm.m_vInput, l_fT, l_nI, l_fS);
    const bool l_bLast = l_nI + 1 >= l_oSm.m_vInput.size();
    if (l_oSm.m_eInterpolation == Interpolation::CubicSpline) {
        auto l_fnAt = [&](size_t l_nKey, int l_iPart) { return &l_oSm.m_vOutput[(l_nKey * 3 + (size_t)l_iPart) * l_nN]; };   // 0 in, 1 value, 2 out
        if (l_bLast || l_fS == 0) {
            for (size_t l_nC = 0; l_nC < l_nN; l_nC++) l_vOut[l_nC] = l_fnAt(l_nI, 1)[l_nC];
        } else {
            const float l_fDt = l_oSm.m_vInput[l_nI + 1] - l_oSm.m_vInput[l_nI];
            const float l_fS2 = l_fS * l_fS, l_fS3 = l_fS2 * l_fS;
            const float l_fH00 = 2 * l_fS3 - 3 * l_fS2 + 1, l_fH10 = l_fS3 - 2 * l_fS2 + l_fS, l_fH01 = -2 * l_fS3 + 3 * l_fS2, l_fH11 = l_fS3 - l_fS2;
            for (size_t l_nC = 0; l_nC < l_nN; l_nC++)
                l_vOut[l_nC] = l_fH00 * l_fnAt(l_nI, 1)[l_nC] + l_fH10 * l_fDt * l_fnAt(l_nI, 2)[l_nC] + l_fH01 * l_fnAt(l_nI + 1, 1)[l_nC] + l_fH11 * l_fDt * l_fnAt(l_nI + 1, 0)[l_nC];
        }
    } else if (l_oSm.m_eInterpolation == Interpolation::Step || l_bLast || l_fS == 0) {
        for (size_t l_nC = 0; l_nC < l_nN; l_nC++) l_vOut[l_nC] = l_oSm.m_vOutput[l_nI * l_nN + l_nC];
    } else if (l_bIsRotation) {
        const float* l_pA = &l_oSm.m_vOutput[l_nI * l_nN];
        const float* l_pB = &l_oSm.m_vOutput[(l_nI + 1) * l_nN];
        const glm::quat l_qQ = glm::slerp(glm::quat(l_pA[3], l_pA[0], l_pA[1], l_pA[2]), glm::quat(l_pB[3], l_pB[0], l_pB[1], l_pB[2]), l_fS);
        l_vOut = {l_qQ.x, l_qQ.y, l_qQ.z, l_qQ.w};
    } else {
        for (size_t l_nC = 0; l_nC < l_nN; l_nC++) l_vOut[l_nC] = l_oSm.m_vOutput[l_nI * l_nN + l_nC] * (1 - l_fS) + l_oSm.m_vOutput[(l_nI + 1) * l_nN + l_nC] * l_fS;
    }
}

}  // namespace

// ---- Model ------------------------------------------------------------------------------------------

size_t Model::f_VertexCount() const {
    size_t l_nN = 0;
    for (const Mesh& l_oM : m_vMeshes)
        for (const Primitive& l_oP : l_oM.m_vPrimitives) l_nN += l_oP.m_vPosition.size();
    return l_nN;
}

size_t Model::f_TriangleCount() const {
    size_t l_nN = 0;
    for (const Mesh& l_oM : m_vMeshes)
        for (const Primitive& l_oP : l_oM.m_vPrimitives)
            if (l_oP.m_eTopology == Topology::Triangles) l_nN += l_oP.m_vIndices.size() / 3;
    return l_nN;
}

size_t Model::f_MorphTargetCount() const {
    size_t l_nN = 0;
    for (const Mesh& l_oM : m_vMeshes) l_nN += l_oM.m_vWeights.size();
    return l_nN;
}

size_t Model::f_InstanceCount() const {
    size_t l_nN = 0;
    for (const Node& l_oNode : m_vNodes) l_nN += l_oNode.m_vInstances.size();
    return l_nN;
}

bool f_LoadModel(const std::string& l_strPath, Model& l_oOut, std::string* l_pError) {
    cgltf_options l_oOpt = f_CgltfOptions();
    cgltf_data* l_pD = nullptr;
    const cgltf_result l_eR = cgltf_parse_file(&l_oOpt, l_strPath.c_str(), &l_pD);
    if (l_eR == cgltf_result_file_not_found || l_eR == cgltf_result_io_error) {
        if (l_pError) *l_pError = "cannot open " + l_strPath;
        return false;
    }
    return f_Finish(l_eR, l_pD, l_oOpt, l_strPath, f_DirOf(l_strPath), l_oOut, l_pError);
}

bool f_LoadModelFromMemory(const void* l_pData, size_t l_nSize, const std::string& l_strBaseDir, Model& l_oOut, std::string* l_pError) {
    cgltf_options l_oOpt = f_CgltfOptions();
    cgltf_data* l_pD = nullptr;
    const cgltf_result l_eR = cgltf_parse(&l_oOpt, l_pData, l_nSize, &l_pD);
    return f_Finish(l_eR, l_pD, l_oOpt, l_strBaseDir + "/model.gltf", l_strBaseDir, l_oOut, l_pError);
}

glm::mat4 f_ComposeTRS(const glm::vec3& l_v3T, const glm::quat& l_qR, const glm::vec3& l_v3S) {
    glm::mat4 l_mat4M = glm::mat4_cast(l_qR);
    l_mat4M[0] *= l_v3S.x;
    l_mat4M[1] *= l_v3S.y;
    l_mat4M[2] *= l_v3S.z;
    l_mat4M[3] = glm::vec4(l_v3T, 1.0f);
    return l_mat4M;
}

void f_Morph(const Primitive& l_oP, const std::vector<float>& l_vWeights, std::vector<glm::vec3>& l_vPosition,
           std::vector<glm::vec3>& l_vNormal, std::vector<glm::vec4>& l_vTangent) {
    l_vPosition = l_oP.m_vPosition;
    l_vNormal = l_oP.m_vNormal;
    l_vTangent = l_oP.m_vTangent;
    for (size_t l_nT = 0; l_nT < l_oP.m_vTargets.size() && l_nT < l_vWeights.size(); l_nT++) {
        const float l_fW = l_vWeights[l_nT];
        if (l_fW == 0.0f) continue;
        const MorphTarget& l_oMt = l_oP.m_vTargets[l_nT];
        for (size_t l_nI = 0; l_nI < l_oMt.m_vPosition.size() && l_nI < l_vPosition.size(); l_nI++) l_vPosition[l_nI] += l_fW * l_oMt.m_vPosition[l_nI];
        for (size_t l_nI = 0; l_nI < l_oMt.m_vNormal.size() && l_nI < l_vNormal.size(); l_nI++) l_vNormal[l_nI] += l_fW * l_oMt.m_vNormal[l_nI];
        for (size_t l_nI = 0; l_nI < l_oMt.m_vTangent.size() && l_nI < l_vTangent.size(); l_nI++) l_vTangent[l_nI] += glm::vec4(l_fW * l_oMt.m_vTangent[l_nI], 0.0f);
    }
    for (glm::vec3& l_v3N : l_vNormal) {
        const float l_fL = glm::length(l_v3N);
        if (l_fL > 0) l_v3N /= l_fL;
    }
}

// ---- Pose -------------------------------------------------------------------------------------------

void Pose::f_Bind(const Model* l_pModel) {
    m_pModel = l_pModel;
    f_Reset();
}

void Pose::f_Reset() {
    if (!m_pModel) return;
    const size_t l_nN = m_pModel->m_vNodes.size();
    m_vT.resize(l_nN);
    m_vR.resize(l_nN);
    m_vS.resize(l_nN);
    m_vWeights.assign(l_nN, {});
    for (size_t l_nI = 0; l_nI < l_nN; l_nI++) {
        const Node& l_oNode = m_pModel->m_vNodes[l_nI];
        m_vT[l_nI] = l_oNode.m_v3Translation;
        m_vR[l_nI] = l_oNode.m_qRotation;
        m_vS[l_nI] = l_oNode.m_v3Scale;
        if (l_oNode.m_iMesh >= 0) {
            const Mesh& l_oMesh = m_pModel->m_vMeshes[(size_t)l_oNode.m_iMesh];
            m_vWeights[l_nI] = l_oNode.m_vWeights.empty() ? l_oMesh.m_vWeights : l_oNode.m_vWeights;
            m_vWeights[l_nI].resize(l_oMesh.m_vWeights.size(), 0.0f);
        }
    }
    f_UpdateWorld();
}

void Pose::f_Apply(int l_iIndex, float l_fT) { f_Sample(l_iIndex, l_fT, 1.0f); }

void Pose::f_Blend(int l_iIndex, float l_fT, float l_fW) { f_Sample(l_iIndex, l_fT, std::clamp(l_fW, 0.0f, 1.0f)); }

void Pose::f_Sample(int l_iIndex, float l_fT, float l_fW) {
    if (!m_pModel || l_iIndex < 0 || l_iIndex >= (int)m_pModel->m_vAnimations.size() || l_fW <= 0.0f) return;
    const Animation& l_oA = m_pModel->m_vAnimations[(size_t)l_iIndex];
    l_fT = std::clamp(l_fT, 0.0f, l_oA.m_fDuration);
    std::vector<float> l_vV;
    for (const Channel& l_oC : l_oA.m_vChannels) {
        if (l_oC.m_iNode < 0 || l_oC.m_iNode >= (int)m_vT.size() || l_oC.m_iSampler < 0 || l_oC.m_iSampler >= (int)l_oA.m_vSamplers.size()) continue;
        const size_t l_nN = (size_t)l_oC.m_iNode;
        f_SampleValue(l_oA.m_vSamplers[(size_t)l_oC.m_iSampler], l_fT, l_oC.m_ePath == Path::Rotation, l_vV);
        switch (l_oC.m_ePath) {
        case Path::Translation:
            if (l_vV.size() >= 3) m_vT[l_nN] = glm::mix(m_vT[l_nN], glm::vec3(l_vV[0], l_vV[1], l_vV[2]), l_fW);
            break;
        case Path::Scale:
            if (l_vV.size() >= 3) m_vS[l_nN] = glm::mix(m_vS[l_nN], glm::vec3(l_vV[0], l_vV[1], l_vV[2]), l_fW);
            break;
        case Path::Rotation:
            if (l_vV.size() >= 4) {
                const glm::quat l_qQ = glm::normalize(glm::quat(l_vV[3], l_vV[0], l_vV[1], l_vV[2]));
                m_vR[l_nN] = l_fW >= 1.0f ? l_qQ : glm::normalize(glm::slerp(m_vR[l_nN], l_qQ, l_fW));
            }
            break;
        case Path::Weights:
            for (size_t l_nK = 0; l_nK < l_vV.size() && l_nK < m_vWeights[l_nN].size(); l_nK++) m_vWeights[l_nN][l_nK] = m_vWeights[l_nN][l_nK] * (1 - l_fW) + l_vV[l_nK] * l_fW;
            break;
        }
    }
}

void Pose::f_UpdateWorld() {
    if (!m_pModel) return;
    const size_t l_nN = m_pModel->m_vNodes.size();
    m_vWorld.assign(l_nN, glm::mat4(1.0f));
    // Parents first: walk down from every root (files do not promise parents come first).
    std::vector<int> l_vStack;
    for (size_t l_nI = 0; l_nI < l_nN; l_nI++)
        if (m_pModel->m_vNodes[l_nI].m_iParent < 0) l_vStack.push_back((int)l_nI);
    while (!l_vStack.empty()) {
        const int l_iI = l_vStack.back();
        l_vStack.pop_back();
        const Node& l_oNode = m_pModel->m_vNodes[(size_t)l_iI];
        const glm::mat4 l_mat4Local = f_ComposeTRS(m_vT[(size_t)l_iI], m_vR[(size_t)l_iI], m_vS[(size_t)l_iI]);
        m_vWorld[(size_t)l_iI] = l_oNode.m_iParent >= 0 ? m_vWorld[(size_t)l_oNode.m_iParent] * l_mat4Local : l_mat4Local;
        for (int l_iC : l_oNode.m_vChildren) l_vStack.push_back(l_iC);
    }
}

void Pose::f_JointMatrices(int l_iSkin, std::vector<glm::mat4>& l_vOut) const {
    l_vOut.clear();
    if (!m_pModel || l_iSkin < 0 || l_iSkin >= (int)m_pModel->m_vSkins.size()) return;
    const Skin& l_oS = m_pModel->m_vSkins[(size_t)l_iSkin];
    l_vOut.resize(std::min(l_oS.m_vJoints.size(), (size_t)g_iMaxJoints));
    for (size_t l_nJ = 0; l_nJ < l_vOut.size(); l_nJ++) l_vOut[l_nJ] = m_vWorld[(size_t)l_oS.m_vJoints[l_nJ]] * l_oS.m_vInverseBind[l_nJ];
}

std::vector<PlacedCamera> Pose::f_Cameras() const {
    std::vector<PlacedCamera> l_vOut;
    if (!m_pModel) return l_vOut;
    for (size_t l_nI = 0; l_nI < m_pModel->m_vNodes.size() && l_nI < m_vWorld.size(); l_nI++) {
        const int l_iCam = m_pModel->m_vNodes[l_nI].m_iCamera;
        if (l_iCam < 0 || l_iCam >= (int)m_pModel->m_vCameras.size()) continue;
        PlacedCamera l_oP;
        l_oP.m_iCamera = l_iCam;
        l_oP.m_iNode = (int)l_nI;
        const glm::mat4& l_mat4W = m_vWorld[l_nI];
        // Rotation only: a scaled (or mirrored) node must not stretch the view.
        glm::vec3 l_v3Z = glm::vec3(l_mat4W[2]), l_v3Y = glm::vec3(l_mat4W[1]);
        l_v3Z = glm::length(l_v3Z) > 0 ? glm::normalize(l_v3Z) : glm::vec3(0, 0, 1);
        glm::vec3 l_v3X = glm::cross(l_v3Y, l_v3Z);
        l_v3X = glm::length(l_v3X) > 0 ? glm::normalize(l_v3X) : glm::vec3(1, 0, 0);
        l_v3Y = glm::cross(l_v3Z, l_v3X);
        l_oP.m_mat4World = glm::mat4(glm::vec4(l_v3X, 0), glm::vec4(l_v3Y, 0), glm::vec4(l_v3Z, 0), l_mat4W[3]);
        l_vOut.push_back(l_oP);
    }
    return l_vOut;
}

std::vector<PlacedLight> Pose::f_Lights() const {
    std::vector<PlacedLight> l_vOut;
    if (!m_pModel) return l_vOut;
    for (size_t l_nI = 0; l_nI < m_pModel->m_vNodes.size() && l_nI < m_vWorld.size(); l_nI++) {
        const int l_iL = m_pModel->m_vNodes[l_nI].m_iLight;
        if (l_iL < 0 || l_iL >= (int)m_pModel->m_vLights.size()) continue;
        PlacedLight l_oP;
        l_oP.m_iLight = l_iL;
        l_oP.m_v3Position = glm::vec3(m_vWorld[l_nI][3]);
        const glm::vec3 l_v3D = -glm::vec3(m_vWorld[l_nI][2]);
        l_oP.m_v3Direction = glm::length(l_v3D) > 0 ? glm::normalize(l_v3D) : glm::vec3(0, 0, -1);
        l_vOut.push_back(l_oP);
    }
    return l_vOut;
}

bool Pose::f_Bounds(glm::vec3& l_v3Mn, glm::vec3& l_v3Mx) const {
    if (!m_pModel || m_pModel->m_vScenes.empty()) return false;
    bool l_bAny = false;
    auto l_fnAdd = [&](const glm::vec3& l_v3P) {
        if (!l_bAny) l_v3Mn = l_v3Mx = l_v3P;
        l_v3Mn = glm::min(l_v3Mn, l_v3P);
        l_v3Mx = glm::max(l_v3Mx, l_v3P);
        l_bAny = true;
    };
    const Scene& l_oSc = m_pModel->m_vScenes[(size_t)std::clamp(m_pModel->m_iScene, 0, (int)m_pModel->m_vScenes.size() - 1)];
    std::vector<int> l_vStack(l_oSc.m_vRoots.begin(), l_oSc.m_vRoots.end());
    std::vector<glm::mat4> l_vJoints;
    while (!l_vStack.empty()) {
        const int l_iI = l_vStack.back();
        l_vStack.pop_back();
        const Node& l_oNode = m_pModel->m_vNodes[(size_t)l_iI];
        for (int l_iC : l_oNode.m_vChildren) l_vStack.push_back(l_iC);
        if (l_oNode.m_iMesh < 0) continue;
        const Mesh& l_oMesh = m_pModel->m_vMeshes[(size_t)l_oNode.m_iMesh];
        if (l_oNode.m_iSkin >= 0) {   // skinned on the CPU: the bind pose box can be far off
            f_JointMatrices(l_oNode.m_iSkin, l_vJoints);
            for (const Primitive& l_oP : l_oMesh.m_vPrimitives) {
                if (!l_oP.f_Skinned()) continue;
                for (size_t l_nV = 0; l_nV < l_oP.m_vPosition.size(); l_nV++) {
                    glm::mat4 l_mat4SkinM(0.0f);
                    for (int l_iK = 0; l_iK < 4; l_iK++) {
                        const int l_iJ = (int)l_oP.m_vJoints[l_nV][l_iK];
                        if (l_oP.m_vWeights[l_nV][l_iK] > 0 && l_iJ >= 0 && l_iJ < (int)l_vJoints.size()) l_mat4SkinM += l_oP.m_vWeights[l_nV][l_iK] * l_vJoints[(size_t)l_iJ];
                    }
                    l_fnAdd(glm::vec3(l_mat4SkinM * glm::vec4(l_oP.m_vPosition[l_nV], 1.0f)));
                }
            }
            continue;
        }
        const std::vector<glm::mat4> l_vOne{glm::mat4(1.0f)};
        const std::vector<glm::mat4>& l_vInst = l_oNode.m_vInstances.empty() ? l_vOne : l_oNode.m_vInstances;
        for (const Primitive& l_oP : l_oMesh.m_vPrimitives) {
            glm::vec3 l_v3Bmin = l_oP.m_v3BoundsMin, l_v3Bmax = l_oP.m_v3BoundsMax;
            if (!l_oP.m_vTargets.empty()) {   // morphed: the box of the morphed positions
                std::vector<glm::vec3> l_vMp, l_vMn2;
                std::vector<glm::vec4> l_vMt;
                f_Morph(l_oP, m_vWeights[(size_t)l_iI], l_vMp, l_vMn2, l_vMt);
                if (!l_vMp.empty()) l_v3Bmin = l_v3Bmax = l_vMp[0];
                for (const glm::vec3& l_v3V : l_vMp) {
                    l_v3Bmin = glm::min(l_v3Bmin, l_v3V);
                    l_v3Bmax = glm::max(l_v3Bmax, l_v3V);
                }
            }
            for (const glm::mat4& l_mat4Im : l_vInst)
                for (int l_iC = 0; l_iC < 8; l_iC++) {
                    const glm::vec3 l_v3Corner((l_iC & 1) ? l_v3Bmax.x : l_v3Bmin.x, (l_iC & 2) ? l_v3Bmax.y : l_v3Bmin.y, (l_iC & 4) ? l_v3Bmax.z : l_v3Bmin.z);
                    l_fnAdd(glm::vec3(m_vWorld[(size_t)l_iI] * l_mat4Im * glm::vec4(l_v3Corner, 1.0f)));
                }
        }
    }
    return l_bAny;
}

}  // namespace toms::gltf
