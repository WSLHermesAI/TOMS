// atlas_project.cpp -- see atlas_project.h.
#include "atlas_project.h"

#include <json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace atlas {

// ---- paths -------------------------------------------------------------------------------------

fs::path f_U8path(const std::string& l_strUtf8) { return fs::u8path(l_strUtf8); }
std::string f_U8str(const fs::path& l_oP) { return f_NormalizePath(l_oP.generic_u8string()); }

std::string f_NormalizePath(std::string l_strP) {
    for (char& l_cC : l_strP) if (l_cC == '\\') l_cC = '/';
    return l_strP;
}

static bool f_IsAbsolute(const std::string& l_strP) {
    return (!l_strP.empty() && l_strP[0] == '/') || (l_strP.size() > 1 && l_strP[1] == ':');
}

std::string f_JoinPath(const std::string& l_strA, const std::string& l_strB) {
    if (l_strA.empty() || l_strA == "." || f_IsAbsolute(l_strB)) return f_NormalizePath(l_strB);
    if (l_strB.empty() || l_strB == ".") return f_NormalizePath(l_strA);
    std::string l_strR = f_NormalizePath(l_strA);
    if (l_strR.back() != '/') l_strR += '/';
    return l_strR + f_NormalizePath(l_strB);
}

std::string f_ParentDir(const std::string& l_strP) {
    const std::string l_strN = f_NormalizePath(l_strP);
    const size_t l_nS = l_strN.find_last_of('/');
    if (l_nS == std::string::npos) return ".";
    if (l_nS == 0) return "/";
    return l_strN.substr(0, l_nS);
}

std::string f_FileName(const std::string& l_strP) {
    const std::string l_strN = f_NormalizePath(l_strP);
    const size_t l_nS = l_strN.find_last_of('/');
    return l_nS == std::string::npos ? l_strN : l_strN.substr(l_nS + 1);
}

std::string f_FileStem(const std::string& l_strP) {
    std::string l_strF = f_FileName(l_strP);
    const size_t l_nD = l_strF.find_last_of('.');
    return l_nD == std::string::npos || l_nD == 0 ? l_strF : l_strF.substr(0, l_nD);
}

std::string Project::f_BaseDir() const { return m_strFilePath.empty() ? "." : f_ParentDir(m_strFilePath); }

std::string Project::f_Resolve(const std::string& l_strRel) const {
    if (l_strRel.empty()) return l_strRel;
    return f_U8str(f_U8path(f_JoinPath(f_BaseDir(), l_strRel)).lexically_normal());
}

std::string Project::f_Relativize(const std::string& l_strPath) const {
    std::error_code l_oEc;
    const fs::path l_oBase = fs::absolute(f_U8path(f_BaseDir()), l_oEc).lexically_normal();
    const fs::path l_oAbs = fs::absolute(f_U8path(l_strPath), l_oEc).lexically_normal();
    const fs::path l_oRel = l_oAbs.lexically_relative(l_oBase);
    if (l_oRel.empty()) return f_NormalizePath(l_strPath);
    return f_U8str(l_oRel);
}

SpriteDef* Project::f_FindSprite(const std::string& l_strN) {
    for (SpriteDef& l_oS : m_vSprites) if (l_oS.m_strName == l_strN) return &l_oS;
    return nullptr;
}
const SpriteDef* Project::f_FindSprite(const std::string& l_strN) const {
    for (const SpriteDef& l_oS : m_vSprites) if (l_oS.m_strName == l_strN) return &l_oS;
    return nullptr;
}
const Variant* Project::f_FindVariant(const std::string& l_strId) const {
    for (const Variant& l_oV : m_vVariants) if (l_oV.m_strId == l_strId) return &l_oV;
    return nullptr;
}

// ---- JSON --------------------------------------------------------------------------------------

static void f_OutputToJson(json& l_jJ, const Output& l_oO) {
    l_jJ["dir"] = l_oO.m_strDir;
    if (!l_oO.m_strName.empty()) l_jJ["name"] = l_oO.m_strName;
    l_jJ["formats"] = l_oO.m_vFormats;
}

static void f_OutputFromJson(const json& l_jJ, Output& l_oO) {
    if (!l_jJ.is_object()) return;
    l_oO.m_strDir = l_jJ.value("dir", l_oO.m_strDir);
    l_oO.m_strName = l_jJ.value("name", l_oO.m_strName);
    if (l_jJ.contains("formats") && l_jJ["formats"].is_array()) l_oO.m_vFormats = l_jJ["formats"].get<std::vector<std::string>>();
}

std::string f_ProjectToJson(const Project& l_oP) {
    const Settings& l_oS = l_oP.m_oSettings;
    json l_jJ;
    l_jJ["version"] = l_oP.m_iVersion;
    l_jJ["name"] = l_oP.m_strName;
    l_jJ["settings"] = {
        {"maxWidth", l_oS.m_iMaxWidth}, {"maxHeight", l_oS.m_iMaxHeight}, {"minWidth", l_oS.m_iMinWidth}, {"minHeight", l_oS.m_iMinHeight},
        {"powerOfTwo", l_oS.m_bPowerOfTwo}, {"square", l_oS.m_bSquare}, {"fixedSize", l_oS.m_bFixedSize},
        {"padding", l_oS.m_iPadding}, {"border", l_oS.m_iBorder}, {"extrude", l_oS.m_iExtrude},
        {"trim", l_oS.m_bTrim}, {"alphaThreshold", l_oS.m_iAlphaThreshold}, {"dedupe", l_oS.m_bDedupe},
        {"premultiplyAlpha", l_oS.m_bPremultiplyAlpha}, {"filter", l_oS.m_strFilter},
        {"heuristic", f_HeuristicName(l_oS.m_eHeuristic)},
        {"defaultPivot", {l_oS.m_arrDefaultPivot[0], l_oS.m_arrDefaultPivot[1]}}};
    f_OutputToJson(l_jJ["output"], l_oP.m_oOutput);
    auto l_fnFolders = [](const std::vector<SourceFolder>& l_vV) {
        json l_jA = json::array();
        for (const SourceFolder& l_oF : l_vV) l_jA.push_back({{"path", l_oF.m_strPath}, {"recursive", l_oF.m_bRecursive}, {"prefix", l_oF.m_strPrefix}});
        return l_jA;
    };
    if (l_oP.m_bEmbedded) {
        l_jJ["embedded"] = true;
        l_jJ["references"] = l_fnFolders(l_oP.m_vReferences);
    }
    if (!l_oP.m_bEmbedded || !l_oP.m_vSources.empty()) l_jJ["sources"] = l_fnFolders(l_oP.m_vSources);
    l_jJ["sprites"] = json::array();
    for (const SpriteDef& l_oD : l_oP.m_vSprites) {
        json l_jE;
        l_jE["name"] = l_oD.m_strName;
        if (!l_oD.m_strFile.empty()) l_jE["file"] = l_oD.m_strFile;
        if (l_oD.f_IsChild()) {
            l_jE["parent"] = l_oD.m_strParent;
            l_jE["rect"] = {l_oD.m_oRect.m_iX, l_oD.m_oRect.m_iY, l_oD.m_oRect.m_iW, l_oD.m_oRect.m_iH};
            if (l_oD.m_bBake) l_jE["bake"] = true;
        }
        if (l_oD.m_bExclude) l_jE["exclude"] = true;
        if (l_oD.m_bHasPivot) l_jE["pivot"] = {l_oD.m_arrPivot[0], l_oD.m_arrPivot[1]};
        if (l_oD.m_bHasSplit) l_jE["split"] = {l_oD.m_arrSplit[0], l_oD.m_arrSplit[1], l_oD.m_arrSplit[2], l_oD.m_arrSplit[3]};
        if (l_oD.m_iTrim >= 0) l_jE["trim"] = l_oD.m_iTrim == 1;
        if (l_oD.m_bPinned) l_jE["pin"] = {l_oD.m_iPinPage, l_oD.m_iPinX, l_oD.m_iPinY};
        if (!l_oD.m_vTags.empty()) l_jE["tags"] = l_oD.m_vTags;
        l_jJ["sprites"].push_back(l_jE);
    }
    l_jJ["animations"] = json::array();
    for (const Animation& l_oA : l_oP.m_vAnimations) {
        json l_jFrames = json::array();
        for (const AnimFrame& l_oF : l_oA.m_vFrames) l_jFrames.push_back({{"sprite", l_oF.m_strSprite}, {"time", l_oF.m_fTime}});
        l_jJ["animations"].push_back({{"name", l_oA.m_strName}, {"loop", l_oA.m_bLoop}, {"frames", l_jFrames}});
    }
    l_jJ["variants"] = json::array();
    for (const Variant& l_oV : l_oP.m_vVariants) {
        json l_jE{{"id", l_oV.m_strId}};
        if (!l_oV.m_strOverrideDir.empty()) l_jE["overrides"] = l_oV.m_strOverrideDir;
        if (!l_oV.m_strReference.empty()) l_jE["reference"] = l_oV.m_strReference;
        if (!l_oV.m_mapImages.empty()) {
            json l_jNames = json::array();
            for (const auto& l_pairKv : l_oV.m_mapImages) l_jNames.push_back(l_pairKv.first);
            l_jE["images"] = l_jNames;
        }
        f_OutputToJson(l_jE["output"], l_oV.m_oOutput);
        l_jJ["variants"].push_back(l_jE);
    }
    return l_jJ.dump(2) + "\n";
}

template <class T> static void f_ReadArr(const json& l_jJ, const char* l_pszKey, T* l_pOut, size_t l_nN) {
    if (!l_jJ.contains(l_pszKey) || !l_jJ[l_pszKey].is_array() || l_jJ[l_pszKey].size() != l_nN) return;
    for (size_t l_nI = 0; l_nI < l_nN; l_nI++) l_pOut[l_nI] = l_jJ[l_pszKey][l_nI].get<T>();
}

bool f_ProjectFromJson(const std::string& l_strText, Project& l_oP, std::string* l_pErr) {
    json l_jJ;
    try {
        l_jJ = json::parse(l_strText);
    } catch (const std::exception& l_oE) {
        if (l_pErr) *l_pErr = std::string("invalid JSON: ") + l_oE.what();
        return false;
    }
    if (!l_jJ.is_object()) { if (l_pErr) *l_pErr = "project root must be an object"; return false; }
    try {
        l_oP.m_iVersion = l_jJ.value("version", 1);
        l_oP.m_strName = l_jJ.value("name", l_oP.m_strName);
        if (l_jJ.contains("settings")) {
            const json& l_jSj = l_jJ["settings"];
            Settings& l_oS = l_oP.m_oSettings;
            l_oS.m_iMaxWidth = l_jSj.value("maxWidth", l_oS.m_iMaxWidth);
            l_oS.m_iMaxHeight = l_jSj.value("maxHeight", l_oS.m_iMaxHeight);
            l_oS.m_iMinWidth = l_jSj.value("minWidth", l_oS.m_iMinWidth);
            l_oS.m_iMinHeight = l_jSj.value("minHeight", l_oS.m_iMinHeight);
            l_oS.m_bPowerOfTwo = l_jSj.value("powerOfTwo", l_oS.m_bPowerOfTwo);
            l_oS.m_bSquare = l_jSj.value("square", l_oS.m_bSquare);
            l_oS.m_bFixedSize = l_jSj.value("fixedSize", l_oS.m_bFixedSize);
            l_oS.m_iPadding = l_jSj.value("padding", l_oS.m_iPadding);
            l_oS.m_iBorder = l_jSj.value("border", l_oS.m_iBorder);
            l_oS.m_iExtrude = l_jSj.value("extrude", l_oS.m_iExtrude);
            l_oS.m_bTrim = l_jSj.value("trim", l_oS.m_bTrim);
            l_oS.m_iAlphaThreshold = l_jSj.value("alphaThreshold", l_oS.m_iAlphaThreshold);
            l_oS.m_bDedupe = l_jSj.value("dedupe", l_oS.m_bDedupe);
            l_oS.m_bPremultiplyAlpha = l_jSj.value("premultiplyAlpha", l_oS.m_bPremultiplyAlpha);
            l_oS.m_strFilter = l_jSj.value("filter", l_oS.m_strFilter);
            const std::string l_strH = l_jSj.value("heuristic", std::string("auto"));
            if (!f_ParseHeuristic(l_strH, l_oS.m_eHeuristic)) { if (l_pErr) *l_pErr = "unknown heuristic '" + l_strH + "'"; return false; }
            f_ReadArr(l_jSj, "defaultPivot", l_oS.m_arrDefaultPivot, 2);
        }
        if (l_jJ.contains("output")) f_OutputFromJson(l_jJ["output"], l_oP.m_oOutput);
        l_oP.m_bEmbedded = l_jJ.value("embedded", false);
        auto l_fnFolders = [](const json& l_jA) {
            std::vector<SourceFolder> l_vV;
            for (const json& l_jF : l_jA) {
                SourceFolder l_oSf;
                l_oSf.m_strPath = f_NormalizePath(l_jF.value("path", std::string()));
                l_oSf.m_bRecursive = l_jF.value("recursive", true);
                l_oSf.m_strPrefix = l_jF.value("prefix", std::string());
                l_vV.push_back(l_oSf);
            }
            return l_vV;
        };
        l_oP.m_vSources = l_fnFolders(l_jJ.value("sources", json::array()));
        l_oP.m_vReferences = l_fnFolders(l_jJ.value("references", json::array()));
        l_oP.m_mapImages.clear();
        l_oP.m_vSprites.clear();
        for (const json& l_jE : l_jJ.value("sprites", json::array())) {
            SpriteDef l_oD;
            l_oD.m_strName = l_jE.value("name", std::string());
            l_oD.m_strFile = f_NormalizePath(l_jE.value("file", std::string()));
            l_oD.m_strParent = l_jE.value("parent", std::string());
            if (l_jE.contains("rect")) {
                int l_arrR[4] = {0, 0, 0, 0};
                f_ReadArr(l_jE, "rect", l_arrR, 4);
                l_oD.m_oRect = {l_arrR[0], l_arrR[1], l_arrR[2], l_arrR[3]};
            }
            l_oD.m_bBake = l_jE.value("bake", false);
            l_oD.m_bExclude = l_jE.value("exclude", false);
            if (l_jE.contains("pivot")) { l_oD.m_bHasPivot = true; f_ReadArr(l_jE, "pivot", l_oD.m_arrPivot, 2); }
            if (l_jE.contains("split")) { l_oD.m_bHasSplit = true; f_ReadArr(l_jE, "split", l_oD.m_arrSplit, 4); }
            if (l_jE.contains("trim")) l_oD.m_iTrim = l_jE["trim"].get<bool>() ? 1 : 0;
            if (l_jE.contains("pin")) {
                int l_arrPin[3] = {0, 0, 0};
                f_ReadArr(l_jE, "pin", l_arrPin, 3);
                l_oD.m_bPinned = true; l_oD.m_iPinPage = l_arrPin[0]; l_oD.m_iPinX = l_arrPin[1]; l_oD.m_iPinY = l_arrPin[2];
            }
            if (l_jE.contains("tags")) l_oD.m_vTags = l_jE["tags"].get<std::vector<std::string>>();
            l_oP.m_vSprites.push_back(l_oD);
        }
        l_oP.m_vAnimations.clear();
        for (const json& l_jA : l_jJ.value("animations", json::array())) {
            Animation l_oAn;
            l_oAn.m_strName = l_jA.value("name", std::string());
            l_oAn.m_bLoop = l_jA.value("loop", true);
            for (const json& l_jF : l_jA.value("frames", json::array()))
                l_oAn.m_vFrames.push_back({l_jF.value("sprite", std::string()), l_jF.value("time", 0.1f)});
            l_oP.m_vAnimations.push_back(l_oAn);
        }
        l_oP.m_vVariants.clear();
        for (const json& l_jV : l_jJ.value("variants", json::array())) {
            Variant l_oVr;
            l_oVr.m_strId = l_jV.value("id", std::string());
            l_oVr.m_strOverrideDir = f_NormalizePath(l_jV.value("overrides", std::string()));
            l_oVr.m_strReference = f_NormalizePath(l_jV.value("reference", std::string()));
            for (const json& l_jN : l_jV.value("images", json::array())) l_oVr.m_mapImages[l_jN.get<std::string>()] = nullptr;
            l_oVr.m_oOutput = l_oP.m_oOutput;
            if (l_jV.contains("output")) f_OutputFromJson(l_jV["output"], l_oVr.m_oOutput);
            l_oP.m_vVariants.push_back(l_oVr);
        }
    } catch (const std::exception& l_oE) {
        if (l_pErr) *l_pErr = std::string("bad project field: ") + l_oE.what();
        return false;
    }
    return true;
}

bool f_LoadProject(const std::string& l_strPath, Project& l_oOut, std::string* l_pErr) {
    std::ifstream l_oF(f_U8path(l_strPath), std::ios::binary);
    if (!l_oF) { if (l_pErr) *l_pErr = "cannot open " + l_strPath; return false; }
    std::stringstream l_oSs;
    l_oSs << l_oF.rdbuf();
    Project l_oP;
    if (!f_ProjectFromJson(l_oSs.str(), l_oP, l_pErr)) { if (l_pErr) *l_pErr = l_strPath + ": " + *l_pErr; return false; }
    l_oP.m_strFilePath = f_NormalizePath(l_strPath);
    l_oOut = std::move(l_oP);
    return true;
}

bool f_SaveProject(const std::string& l_strPath, Project& l_oP, std::string* l_pErr) {
    std::ofstream l_oF(f_U8path(l_strPath), std::ios::binary);
    if (!l_oF) { if (l_pErr) *l_pErr = "cannot write " + l_strPath; return false; }
    l_oF << f_ProjectToJson(l_oP);
    if (!l_oF) { if (l_pErr) *l_pErr = "write failed: " + l_strPath; return false; }
    l_oP.m_strFilePath = f_NormalizePath(l_strPath);
    return true;
}

}  // namespace atlas
