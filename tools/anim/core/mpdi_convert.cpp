// mpdi_convert.cpp -- see mpdi_convert.h. The rules follow the old engine's reader and renderer:
// FM79979Engine/Core/GameplayUT/Render/MPDI/MPDIDataParse.cpp (ProcessMPDI, GetTexBehaviorDataWithImageIndexData),
// SubMPDI.cpp (UpdateData, GetRenderPuzzleDataAndMatrix), Render/Curve/Curve.cpp (DoLOD) and Glh.cpp
// (IndexToBlendingIndex).
#include "mpdi_convert.h"

#include "anim_clip.h"
#include "atlas_export.h"
#include "atlas_store.h"
#include "atlas_xml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using namespace toms::anim;

namespace animed {
namespace {

std::string f_U8(const fs::path& l_oP) { return l_oP.u8string(); }

bool f_ReadFile(const fs::path& l_oP, std::string& l_strOut) {
    std::ifstream l_oF(l_oP, std::ios::binary);
    if (!l_oF) return false;
    std::stringstream l_oSs;
    l_oSs << l_oF.rdbuf();
    l_strOut = l_oSs.str();
    return true;
}

std::string f_Lower(std::string l_strS) {
    for (char& l_cC : l_strS) l_cC = (char)std::tolower((unsigned char)l_cC);
    return l_strS;
}

std::string f_Trim(const std::string& l_strS) {
    size_t l_nA = 0, l_nB = l_strS.size();
    while (l_nA < l_nB && std::isspace((unsigned char)l_strS[l_nA])) l_nA++;
    while (l_nB > l_nA && std::isspace((unsigned char)l_strS[l_nB - 1])) l_nB--;
    return l_strS.substr(l_nA, l_nB - l_nA);
}

// "1.0,2.5,3," -> {1, 2.5, 3}
std::vector<float> f_Numbers(const std::string& l_strS) {
    std::vector<float> l_vOut;
    std::string l_strCur;
    for (char l_cC : l_strS + ",") {
        if (l_cC == ',' || l_cC == ':') {
            const std::string l_strT = f_Trim(l_strCur);
            if (!l_strT.empty()) l_vOut.push_back((float)std::atof(l_strT.c_str()));
            l_strCur.clear();
        } else {
            l_strCur += l_cC;
        }
    }
    return l_vOut;
}

std::vector<std::string> f_Fields(const std::string& l_strS) {   // the oldest format's "w,h,angle,r,g,b,a,image,mirror"
    std::vector<std::string> l_vOut;
    std::string l_strCur;
    for (char l_cC : l_strS) {
        if (l_cC == ',' || l_cC == ':') { l_vOut.push_back(f_Trim(l_strCur)); l_strCur.clear(); }
        else l_strCur += l_cC;
    }
    if (!f_Trim(l_strCur).empty()) l_vOut.push_back(f_Trim(l_strCur));
    return l_vOut;
}

bool f_Truthy(const std::string& l_strV) { return l_strV == "1" || f_Lower(l_strV) == "true"; }

// The engine's curve LOD: Chaikin corner cutting (Curve.cpp, TemplateDataIncreaseLod), on the points and the times.
template <class T> std::vector<T> f_Chaikin(const std::vector<T>& l_vIn) {
    if (l_vIn.size() < 2) return l_vIn;
    std::vector<T> l_vOut{l_vIn.front()};
    for (size_t l_nI = 0; l_nI + 1 < l_vIn.size(); l_nI++) {
        l_vOut.push_back(l_vIn[l_nI] * 0.75f + l_vIn[l_nI + 1] * 0.25f);
        l_vOut.push_back(l_vIn[l_nI] * 0.25f + l_vIn[l_nI + 1] * 0.75f);
    }
    l_vOut.push_back(l_vIn.back());
    return l_vOut;
}

// The P0, P1, ... attributes of an element, in index order.
std::vector<std::string> f_IndexedAttrs(const atlas::XmlNode& l_oN) {
    std::vector<std::pair<int, std::string>> l_vV;
    for (const auto& [l_strK, l_strVal] : l_oN.m_vAttrs)
        if (l_strK.size() > 1 && l_strK[0] == 'P' && std::isdigit((unsigned char)l_strK[1])) l_vV.push_back({std::atoi(l_strK.c_str() + 1), l_strVal});
    std::sort(l_vV.begin(), l_vV.end(), [](const auto& l_oA, const auto& l_oB) { return l_oA.first < l_oB.first; });
    std::vector<std::string> l_vOut;
    for (auto& l_pairP : l_vV) l_vOut.push_back(l_pairP.second);
    return l_vOut;
}

struct Point {
    glm::vec3 m_v3Pos{0.0f};
    float m_fT = 0.0f;
    glm::vec2 m_v2Size{0.0f};
    bool m_bHasScale = false;
    glm::vec2 m_v2Scale{1.0f};
    glm::vec3 m_v3Angle{0.0f};
    glm::vec4 m_v4Color{1.0f};
    std::string m_strPi, m_strImage;
    bool m_bMirror = false;
};

struct Path {
    std::string m_strName;
    float m_fStart = 0.0f;
    bool m_bBehavior = false, m_bLoop = false, m_bBlending = false;
    int m_iSrc = -1, m_iDst = -1;
    glm::vec3 m_v3RotOffset{0.0f};
    int m_iLod = 1;
    std::vector<Point> m_vPoints;
    std::vector<std::pair<int, std::string>> m_vHints;   // point index, event
};

struct PiEntry {
    std::string m_strId;          // the atlas id in the .anim (the .pi's name)
    std::string m_strAtlasPath;   // the converted .atlas, absolute
    atlas::BuildResult m_oSrc;  // the .pi as it was (sizes the MPDI's numbers are relative to)
    bool m_bOk = false;
};

struct Ctx {
    MpdiConvertReport& m_oReport;
    std::map<std::string, PiEntry> m_mapPis;   // key: the .pi's absolute path + the output folder
};

// A .pi named in the file: next to the .mpdi, with or without ".pi", any letter case.
fs::path f_FindPi(const fs::path& l_oDir, const std::string& l_strName) {
    std::string l_strN = f_Trim(l_strName);
    if (f_Lower(fs::u8path(l_strN).extension().u8string()) != ".pi") l_strN += ".pi";
    const fs::path l_oDirect = l_oDir / fs::u8path(l_strN);
    std::error_code l_oEc;
    if (fs::exists(l_oDirect, l_oEc)) return l_oDirect;
    const std::string l_strWant = f_Lower(fs::u8path(l_strN).filename().u8string());
    for (const auto& l_oE : fs::directory_iterator(l_oDir, l_oEc))
        if (l_oE.is_regular_file() && f_Lower(l_oE.path().filename().u8string()) == l_strWant) return l_oE.path();
    return {};
}

PiEntry* f_PiFor(Ctx& l_oCtx, const fs::path& l_oPiPath, const fs::path& l_oOutDir) {
    const std::string l_strKey = f_U8(fs::absolute(l_oPiPath)) + "|" + f_U8(fs::absolute(l_oOutDir));
    auto l_itIt = l_oCtx.m_mapPis.find(l_strKey);
    if (l_itIt != l_oCtx.m_mapPis.end()) return l_itIt->second.m_bOk ? &l_itIt->second : nullptr;
    PiEntry& l_oE = l_oCtx.m_mapPis[l_strKey];
    l_oE.m_strId = fs::u8path(l_oPiPath.filename().u8string()).stem().u8string();
    std::string l_strErr;
    if (!atlas::f_ImportPi(f_U8(l_oPiPath), l_oE.m_oSrc, &l_strErr)) {
        l_oCtx.m_oReport.m_vErrors.push_back(f_U8(l_oPiPath) + ": " + l_strErr);
        return nullptr;
    }
    l_oE.m_strAtlasPath = f_ConvertPi(f_U8(l_oPiPath), f_U8(l_oOutDir), l_oCtx.m_oReport);
    l_oE.m_bOk = !l_oE.m_strAtlasPath.empty();
    return l_oE.m_bOk ? &l_oE : nullptr;
}

}  // namespace

std::string f_ConvertPi(const std::string& l_strPiPath, const std::string& l_strOutDir, MpdiConvertReport& l_oReport) {
    atlas::BuildResult l_oIn;
    std::string l_strErr;
    if (!atlas::f_ImportPi(l_strPiPath, l_oIn, &l_strErr)) {
        l_oReport.m_vErrors.push_back(l_strPiPath + ": " + l_strErr);
        return {};
    }
    const fs::path l_oDir = l_strOutDir.empty() ? fs::u8path(l_strPiPath).parent_path() : fs::u8path(l_strOutDir);
    std::error_code l_oEc;
    fs::create_directories(l_oDir, l_oEc);
    const std::string l_strStem = fs::u8path(l_strPiPath).stem().u8string();
    const fs::path l_oProj = l_oDir / fs::u8path(l_strStem + ".atlasproj");
    atlas::Project l_oP;
    if (!atlas::f_ImportToProject(l_oIn, f_U8(l_oProj), "", l_oP, &l_strErr)) {
        l_oReport.m_vErrors.push_back(l_strPiPath + ": " + l_strErr);
        return {};
    }
    l_oP.m_oOutput.m_vFormats = {"atlas", "plist"};
    atlas::SaveResult l_oSr;
    if (!atlas::f_SaveProjectAll(f_U8(l_oProj), l_oP, {"plist"}, &l_oSr, &l_strErr)) {
        l_oReport.m_vErrors.push_back(l_strPiPath + ": " + l_strErr);
        return {};
    }
    l_oReport.m_iPiFiles++;
    l_oReport.m_vWritten.push_back(f_U8(l_oProj));
    for (const std::string& l_strW : l_oSr.m_vWritten) l_oReport.m_vWritten.push_back(l_strW);
    for (const atlas::Diagnostic& l_oD : l_oSr.m_vDiagnostics)
        if (l_oD.m_eLevel != atlas::Diagnostic::Info) l_oReport.m_vWarnings.push_back(l_strPiPath + ": " + l_oD.m_strSprite + ": " + l_oD.m_strMessage);
    const std::string l_strName = l_oP.m_oOutput.m_strName.empty() ? l_oP.m_strName : l_oP.m_oOutput.m_strName;
    return f_U8((l_oProj.parent_path() / fs::u8path(l_oP.m_oOutput.m_strDir) / fs::u8path(l_strName + ".atlas")).lexically_normal());
}

static bool f_ConvertMpdiWith(Ctx& l_oCtx, const std::string& l_strMpdiPath, const std::string& l_strOutDirIn);

bool f_ConvertMpdiFile(const std::string& l_strMpdiPath, const std::string& l_strOutDir, MpdiConvertReport& l_oReport) {
    Ctx l_oCtx{l_oReport, {}};   // one call, one cache of converted sheets (convertMpdiFolder shares one across files)
    return f_ConvertMpdiWith(l_oCtx, l_strMpdiPath, l_strOutDir);
}

static bool f_ConvertMpdiWith(Ctx& l_oCtx, const std::string& l_strMpdiPath, const std::string& l_strOutDirIn) {
    MpdiConvertReport& l_oReport = l_oCtx.m_oReport;
    const fs::path l_oSrc = fs::u8path(l_strMpdiPath);
    const fs::path l_oOutDir = l_strOutDirIn.empty() ? l_oSrc.parent_path() : fs::u8path(l_strOutDirIn);
    const std::string l_strWhere = f_U8(l_oSrc);
    auto l_fnWarn = [&](const std::string& l_strW) {
        const std::string l_strLine = l_strWhere + ": " + l_strW;
        if (std::find(l_oReport.m_vWarnings.begin(), l_oReport.m_vWarnings.end(), l_strLine) == l_oReport.m_vWarnings.end()) l_oReport.m_vWarnings.push_back(l_strLine);
    };
    std::string l_strText;
    if (!f_ReadFile(l_oSrc, l_strText)) { l_oReport.m_vErrors.push_back(l_strWhere + ": cannot read the file"); return false; }
    std::string l_strErr;
    std::unique_ptr<atlas::XmlNode> l_upDoc = atlas::f_ParseXml(l_strText, &l_strErr);
    const atlas::XmlNode* l_pRoot = l_upDoc.get();
    std::string l_strVersionText;
    if (!l_pRoot || l_pRoot->m_strName != "MPDIList") {
        // MPDI 1.0 files start with <Version MPDIVersion="1.0" /> and then <MPDIList>: two top-level elements, which
        // the old engine's reader took. Read them inside a made-up root.
        std::string l_strBody = l_strText;
        if (l_strBody.compare(0, 3, "\xEF\xBB\xBF") == 0) l_strBody.erase(0, 3);
        const size_t l_nDecl = l_strBody.find("?>");
        if (l_strBody.compare(0, 5, "<?xml") == 0 && l_nDecl != std::string::npos) l_strBody.erase(0, l_nDecl + 2);
        std::string l_strErr2;
        l_upDoc = atlas::f_ParseXml("<mpdi_file>" + l_strBody + "</mpdi_file>", &l_strErr2);
        l_pRoot = nullptr;
        if (l_upDoc)
            for (const auto& l_upC : l_upDoc->m_vChildren) {
                if (l_upC->m_strName == "MPDIList" && !l_pRoot) l_pRoot = l_upC.get();
                if (l_upC->m_strName == "Version") l_strVersionText = l_upC->f_AttrOr("MPDIVersion");
            }
        if (!l_pRoot) {
            l_oReport.m_vErrors.push_back(l_strWhere + ": not an MPDI file" + (l_strErr.empty() ? std::string() : " (" + l_strErr + ")"));
            return false;
        }
    }
    if (const std::string* l_pV = l_pRoot->f_Attr("MPDIVersion")) l_strVersionText = *l_pV;
    const float l_fVersion = (float)std::atof(l_strVersionText.empty() ? "0" : l_strVersionText.c_str());
    const bool l_bCentred = l_fVersion <= 1.0f + 1e-4f;          // 1.0 and older store the image centre
    const bool l_bElementPoints = l_fVersion >= 1.0f - 1e-4f;    // one PointDataList element per point

    // The sprite sheets.
    std::vector<std::string> l_vPiNames;
    for (const std::string& l_strN : f_Fields(l_pRoot->f_AttrOr("ImageName"))) if (!l_strN.empty()) l_vPiNames.push_back(l_strN);
    std::map<std::string, PiEntry*> l_mapPiById;   // lower-case id -> entry
    PiEntry* l_pFirstPi = nullptr;
    for (const std::string& l_strN : l_vPiNames) {
        const fs::path l_oPp = f_FindPi(l_oSrc.parent_path(), l_strN);
        if (l_oPp.empty()) { l_oReport.m_vErrors.push_back(l_strWhere + ": the sprite sheet " + l_strN + " is not next to it"); return false; }
        PiEntry* l_pE = f_PiFor(l_oCtx, l_oPp, l_oOutDir);
        if (!l_pE) { l_oReport.m_vErrors.push_back(l_strWhere + ": " + l_strN + " could not be converted"); return false; }
        l_mapPiById[f_Lower(l_pE->m_strId)] = l_pE;
        if (!l_pFirstPi) l_pFirstPi = l_pE;
    }
    if (!l_pFirstPi) { l_oReport.m_vErrors.push_back(l_strWhere + ": no sprite sheet (ImageName) named"); return false; }
    auto l_fnPick = [&](const std::string& l_strPiName) -> PiEntry* {
        if (l_strPiName.empty()) return l_pFirstPi;
        auto l_itIt = l_mapPiById.find(f_Lower(fs::u8path(f_Trim(l_strPiName)).stem().u8string()));
        if (l_itIt != l_mapPiById.end()) return l_itIt->second;
        // a sheet used by a point but not listed in ImageName: next to the .mpdi
        const fs::path l_oPp = f_FindPi(l_oSrc.parent_path(), l_strPiName);
        if (!l_oPp.empty())
            if (PiEntry* l_pE = f_PiFor(l_oCtx, l_oPp, l_oOutDir)) { l_mapPiById[f_Lower(l_pE->m_strId)] = l_pE; return l_pE; }
        l_fnWarn("the sprite sheet " + l_strPiName + " is missing; the first sheet is used");
        return l_pFirstPi;
    };

    AnimFile l_oFile;
    std::set<std::string> l_setAtlasIds;
    auto l_fnUseAtlas = [&](PiEntry* l_pE) {
        if (l_setAtlasIds.insert(l_pE->m_strId).second) {
            std::error_code l_oEc;
            fs::path l_oRel = fs::relative(fs::u8path(l_pE->m_strAtlasPath), l_oOutDir, l_oEc);
            l_oFile.m_vAtlases.push_back({l_pE->m_strId, (l_oEc ? fs::u8path(l_pE->m_strAtlasPath) : l_oRel).generic_u8string()});
        }
    };
    for (const auto& [l_strId, l_pE] : l_mapPiById) l_fnUseAtlas(l_pE);

    for (const auto& l_upClipNode : l_pRoot->m_vChildren) {
        if (l_upClipNode->m_strName != "MultiPathDynamicImage") continue;
        Clip l_oClip;
        l_oClip.m_strName = l_upClipNode->f_AttrOr("Name", "clip" + std::to_string(l_oFile.m_vClips.size()));
        l_oClip.m_fLength = (float)std::atof(l_upClipNode->f_AttrOr("TotalPlayTime", "0").c_str());
        const int l_iPc = std::atoi(l_upClipNode->f_AttrOr("PlayCount", "1").c_str());
        l_oClip.m_iPlayCount = l_iPc == 0 ? 1 : l_iPc;
        l_oClip.m_bStayAtLastFrame = l_upClipNode->f_Attr("StayAtLastFrame") || l_upClipNode->f_Attr("StopAtLastFrame");
        if (const std::string* l_pCv = l_upClipNode->f_Attr("CameraView")) {
            const std::vector<float> l_vM = f_Numbers(*l_pCv);
            bool l_bIdentity = l_vM.size() == 16;
            for (int l_iI = 0; l_bIdentity && l_iI < 16; l_iI++) l_bIdentity = std::fabs(l_vM[(size_t)l_iI] - ((l_iI % 5 == 0) ? 1.0f : 0.0f)) < 1e-4f;
            if (!l_bIdentity) l_fnWarn("clip " + l_oClip.m_strName + ": a CameraView transform is not converted");
        }
        if (l_upClipNode->f_Attr("ViewPort")) l_fnWarn("clip " + l_oClip.m_strName + ": its ViewPort clipping is not converted");
        l_oClip.m_oRoot.m_strName = l_oClip.m_strName;
        std::set<std::string> l_setNames;

        for (const auto& l_upPn : l_upClipNode->m_vChildren) {
            if (l_upPn->m_strName != "CueToStartCurvesWithTime" && l_upPn->m_strName != "CueToStart2DCurvesWithTime") continue;
            Path l_oPath;
            l_oPath.m_strName = l_upPn->f_AttrOr("Name", "path" + std::to_string(l_oClip.m_oRoot.m_vChildren.size()));
            l_oPath.m_fStart = (float)std::atof(l_upPn->f_AttrOr("StartTime", "0").c_str());
            l_oPath.m_bBehavior = l_upPn->f_Attr("BehaviorUpdate") != nullptr;
            l_oPath.m_bLoop = f_Truthy(l_upPn->f_AttrOr("Loop", "0"));
            l_oPath.m_bBlending = l_upPn->f_Attr("Blending") != nullptr;
            l_oPath.m_iSrc = std::atoi(l_upPn->f_AttrOr("SrcBlendingData", "-1").c_str());
            l_oPath.m_iDst = std::atoi(l_upPn->f_AttrOr("DestBlendingData", "-1").c_str());
            l_oPath.m_iLod = std::max(1, std::atoi(l_upPn->f_AttrOr("LOD", "1").c_str()));
            if (const std::string* l_pR = l_upPn->f_Attr("RotationPosOffset")) {
                const std::vector<float> l_vV = f_Numbers(*l_pR);
                if (l_vV.size() >= 2) l_oPath.m_v3RotOffset = {l_vV[0], l_vV[1], l_vV.size() > 2 ? l_vV[2] : 0.0f};
            }
            const std::string l_strAnchor = l_upPn->f_AttrOr("AnchorType", "CenterCenter");
            static const char* const l_arrAnchors[] = {"LeftUp", "CenterUp", "RightUp", "LeftCenter", "CenterCenter", "RightCenter",
                                                   "LeftDown", "CenterDown", "RightDown"};
            int l_iAnchorIndex = 4;
            for (int l_iK = 0; l_iK < 9; l_iK++) if (l_strAnchor == l_arrAnchors[l_iK]) l_iAnchorIndex = l_iK;
            if (l_iAnchorIndex == 4 && l_strAnchor != "CenterCenter") l_fnWarn("path " + l_oPath.m_strName + ": unknown anchor " + l_strAnchor + " is drawn centred");
            // Points.
            std::vector<glm::vec3> l_vOldPos;
            std::vector<float> l_vOldTime;
            for (const auto& l_upC : l_upPn->m_vChildren) {
                if (l_upC->m_strName == "CurvePoint") {
                    for (const std::string& l_strV : f_IndexedAttrs(*l_upC)) {
                        const std::vector<float> l_vF = f_Numbers(l_strV);
                        l_vOldPos.push_back({l_vF.size() > 0 ? l_vF[0] : 0.0f, l_vF.size() > 1 ? l_vF[1] : 0.0f, l_vF.size() > 2 ? l_vF[2] : 0.0f});
                    }
                } else if (l_upC->m_strName == "CurveTime") {
                    for (const std::string& l_strV : f_IndexedAttrs(*l_upC)) l_vOldTime.push_back((float)std::atof(l_strV.c_str()));
                } else if (l_upC->m_strName == "PointDataList") {
                    if (l_bElementPoints) {
                        Point l_oP;
                        const std::vector<float> l_vPos = f_Numbers(l_upC->f_AttrOr("Position", "0,0,0"));
                        l_oP.m_v3Pos = {l_vPos.size() > 0 ? l_vPos[0] : 0.0f, l_vPos.size() > 1 ? l_vPos[1] : 0.0f, l_vPos.size() > 2 ? l_vPos[2] : 0.0f};
                        l_oP.m_fT = (float)std::atof(l_upC->f_AttrOr("Time", "0").c_str());
                        const std::vector<float> l_vSize = f_Numbers(l_upC->f_AttrOr("Size", "0,0"));
                        if (l_vSize.size() >= 2) l_oP.m_v2Size = {l_vSize[0], l_vSize[1]};
                        if (const std::string* l_pS = l_upC->f_Attr("Scale")) {
                            const std::vector<float> l_vV = f_Numbers(*l_pS);
                            if (l_vV.size() >= 2) { l_oP.m_bHasScale = true; l_oP.m_v2Scale = {l_vV[0], l_vV[1]}; }
                        }
                        if (const std::string* l_pA = l_upC->f_Attr("Angle")) l_oP.m_v3Angle.z = (float)std::atof(l_pA->c_str());
                        if (const std::string* l_pA = l_upC->f_Attr("Angles")) {
                            const std::vector<float> l_vV = f_Numbers(*l_pA);
                            if (l_vV.size() >= 3) l_oP.m_v3Angle = {l_vV[0], l_vV[1], l_vV[2]};
                        }
                        if (const std::string* l_pCol = l_upC->f_Attr("Color")) {
                            const std::vector<float> l_vV = f_Numbers(*l_pCol);
                            if (l_vV.size() >= 4) l_oP.m_v4Color = {l_vV[0], l_vV[1], l_vV[2], l_vV[3]};
                        }
                        l_oP.m_strPi = l_upC->f_AttrOr("PIName");
                        l_oP.m_strImage = l_upC->f_AttrOr("ImageName");
                        l_oP.m_bMirror = f_Truthy(l_upC->f_AttrOr("Mirror", "0"));
                        l_oPath.m_vPoints.push_back(l_oP);
                    } else {   // the oldest format: "w,h,angle,r,g,b,a,image[,mirror]" per point, positions elsewhere
                        for (const std::string& l_strV : f_IndexedAttrs(*l_upC)) {
                            const std::vector<std::string> l_vF = f_Fields(l_strV);
                            Point l_oP;
                            if (l_vF.size() >= 8) {
                                l_oP.m_v2Size = {(float)std::atof(l_vF[0].c_str()), (float)std::atof(l_vF[1].c_str())};
                                l_oP.m_v3Angle.z = (float)std::atof(l_vF[2].c_str());
                                l_oP.m_v4Color = glm::vec4((float)std::atoi(l_vF[3].c_str()), (float)std::atoi(l_vF[4].c_str()),
                                                    (float)std::atoi(l_vF[5].c_str()), (float)std::atoi(l_vF[6].c_str())) / 255.0f;
                                l_oP.m_strImage = l_vF[7];
                                l_oP.m_bMirror = l_vF.size() > 8 && std::atoi(l_vF[8].c_str()) != 0;
                            }
                            l_oPath.m_vPoints.push_back(l_oP);
                        }
                    }
                } else if (l_upC->m_strName == "HintPoint") {
                    const int l_iIndex = std::atoi(l_upC->f_AttrOr("PointIndex", "-1").c_str());
                    const int l_iEvent = std::atoi(l_upC->f_AttrOr("Event", "0").c_str());
                    const std::string l_strName = l_upC->f_AttrOr("EventName");
                    if (l_iIndex >= 0) l_oPath.m_vHints.push_back({l_iIndex, (l_iEvent == 2 ? "sound:" : "") + l_strName});   // 2 = eAE_SOUND
                }
            }
            if (!l_bElementPoints)
                for (size_t l_nI = 0; l_nI < l_oPath.m_vPoints.size(); l_nI++) {
                    if (l_nI < l_vOldPos.size()) l_oPath.m_vPoints[l_nI].m_v3Pos = l_vOldPos[l_nI];
                    if (l_nI < l_vOldTime.size()) l_oPath.m_vPoints[l_nI].m_fT = l_vOldTime[l_nI];
                }
            if (l_oPath.m_vPoints.empty()) { l_fnWarn("path " + l_oPath.m_strName + ": no points (skipped)"); continue; }

            // Each point's sprite: its sheet's region gives the trimmed size (what Size scales) and the original size.
            struct Resolved { std::string m_strRef; glm::vec2 m_v2Frame{1.0f}, m_v2Orig{1.0f}, m_v2Trim{0.0f}; };
            std::vector<Resolved> l_vRes;
            for (const Point& l_oP : l_oPath.m_vPoints) {
                PiEntry* l_pE = l_fnPick(l_oP.m_strPi);
                l_fnUseAtlas(l_pE);
                Resolved l_oR;
                l_oR.m_strRef = f_SpriteRef(l_pE->m_strId, l_oP.m_strImage);
                if (const atlas::Region* l_pG = l_pE->m_oSrc.f_Find(l_oP.m_strImage)) {
                    l_oR.m_v2Frame = {std::max(1, l_pG->m_oFrame.m_iW), std::max(1, l_pG->m_oFrame.m_iH)};
                    l_oR.m_v2Orig = {std::max(1, l_pG->m_iOrigW), std::max(1, l_pG->m_iOrigH)};
                    l_oR.m_v2Trim = {l_pG->m_iOffsetX, l_pG->m_iOffsetY};
                } else {
                    l_fnWarn("path " + l_oPath.m_strName + ": the image " + l_oP.m_strImage + " is not in " + l_pE->m_strId);
                    l_oR.m_v2Frame = l_oR.m_v2Orig = glm::max(l_oP.m_v2Size, glm::vec2(1.0f));
                }
                l_vRes.push_back(l_oR);
            }

            // ---- the node(s) ----
            std::string l_strNodeName = l_oPath.m_strName;
            for (int l_iK = 2; !l_setNames.insert(l_strNodeName).second; l_iK++) l_strNodeName = l_oPath.m_strName + "_" + std::to_string(l_iK);
            Node l_oImage;
            l_oImage.m_strName = l_strNodeName;
            l_oImage.m_strSprite = l_vRes[0].m_strRef;
            l_oImage.m_bLoop = l_oPath.m_bLoop;
            l_oImage.m_bStayAtLastFrame = l_oClip.m_bStayAtLastFrame;
            if (l_oPath.m_bBlending) {
                // 1.3 and later store an index (Glh.cpp IndexToBlendingIndex), older files the GL enum itself.
                // Destination GL_ONE, or GL_DST_ALPHA (which is 1 on the opaque screen the games drew on): additive.
                const int l_iDst = l_fVersion >= 1.3f - 1e-4f ? (l_oPath.m_iDst == 1 ? 1 : l_oPath.m_iDst == 6 ? 771 : l_oPath.m_iDst == 7 ? 772 : l_oPath.m_iDst + 10000)
                                                        : l_oPath.m_iDst;
                const bool l_bAdd = l_iDst == 1 || l_iDst == 772;
                const bool l_bNormal = l_iDst == 771 || l_oPath.m_iDst < 0;
                if (l_bAdd) l_oImage.m_eBlend = Blend::Add;
                else if (!l_bNormal) l_fnWarn("path " + l_oPath.m_strName + ": blending " + std::to_string(l_oPath.m_iSrc) + "/" + std::to_string(l_oPath.m_iDst) +
                                       " is drawn as normal alpha blending");
            }
            Ease l_oLinear, l_oStepped;
            f_ParseEase("linear", l_oLinear);
            f_ParseEase("stepped", l_oStepped);
            const Ease& l_oDataEase = l_oPath.m_bBehavior ? l_oLinear : l_oStepped;
            const size_t l_nN = l_oPath.m_vPoints.size();
            std::vector<float> l_vTimes(l_nN);
            std::vector<glm::vec2> l_vCentres(l_nN);
            for (size_t l_nI = 0; l_nI < l_nN; l_nI++) {
                l_vTimes[l_nI] = l_oPath.m_fStart + l_oPath.m_vPoints[l_nI].m_fT;
                const glm::vec2 l_v2P(l_oPath.m_vPoints[l_nI].m_v3Pos);
                l_vCentres[l_nI] = l_bCentred ? l_v2P : l_v2P + l_vRes[l_nI].m_v2Orig * 0.5f;
            }
            // Position: the engine's LOD smoothing, then linear between the (smoothed) points.
            std::vector<glm::vec2> l_vLodPos = l_vCentres;
            std::vector<float> l_vLodTime = l_vTimes;
            for (int l_iK = 1; l_iK < l_oPath.m_iLod; l_iK++) {
                l_vLodPos = f_Chaikin(l_vLodPos);
                l_vLodTime = f_Chaikin(l_vLodTime);
            }
            std::vector<Key<glm::vec3>> l_vPosKeys;
            for (size_t l_nI = 0; l_nI < l_vLodPos.size(); l_nI++) l_vPosKeys.push_back({l_vLodTime[l_nI], glm::vec3(l_vLodPos[l_nI], 0.0f), l_oLinear});
            std::vector<Key<glm::vec3>> l_vRotKeys;
            std::vector<Key<glm::vec3>> l_vScaleKeys;
            std::vector<Key<glm::vec4>> l_vColorKeys;
            bool l_bAnyRot = false, l_bAnyScale = false, l_bAnyColor = false;
            for (size_t l_nI = 0; l_nI < l_nN; l_nI++) {
                const Point& l_oP = l_oPath.m_vPoints[l_nI];
                glm::vec2 l_v2S = l_oP.m_bHasScale ? l_oP.m_v2Scale : l_oP.m_v2Size / l_vRes[l_nI].m_v2Frame;
                if (l_oP.m_bMirror) l_v2S.x = -l_v2S.x;
                // x, y, z as they are: the runtime's rotation order is the engine's (Rx * Ry * Rz)
                l_vRotKeys.push_back({l_vTimes[l_nI], glm::vec3(l_oP.m_v3Angle), l_oDataEase});
                l_vScaleKeys.push_back({l_vTimes[l_nI], glm::vec3(l_v2S, 1.0f), l_oDataEase});
                l_vColorKeys.push_back({l_vTimes[l_nI], l_oP.m_v4Color, l_oDataEase});
                l_bAnyRot |= glm::vec3(l_oP.m_v3Angle) != glm::vec3(0.0f);
                l_bAnyScale |= std::fabs(l_v2S.x - 1.0f) > 1e-4f || std::fabs(l_v2S.y - 1.0f) > 1e-4f;
                l_bAnyColor |= l_oP.m_v4Color != glm::vec4(1.0f);
            }
            if (l_bAnyScale) l_oImage.m_vScaleKeys = l_vScaleKeys;
            if (l_bAnyColor) l_oImage.m_vColorKeys = l_vColorKeys;
            for (size_t l_nI = 0; l_nI < l_nN; l_nI++)
                if (l_nI == 0 ? l_vRes.size() > 1 && std::any_of(l_vRes.begin(), l_vRes.end(), [&](const Resolved& l_oR) { return l_oR.m_strRef != l_vRes[0].m_strRef; })
                           : l_vRes[l_nI].m_strRef != l_vRes[l_nI - 1].m_strRef)
                    l_oImage.m_vSpriteKeys.push_back({l_vTimes[l_nI], l_vRes[l_nI].m_strRef, l_oStepped});
            for (const auto& [l_iIndex, l_strEvent] : l_oPath.m_vHints)
                if (l_iIndex >= 0 && l_iIndex < (int)l_nN) l_oImage.m_vEventKeys.push_back({l_vTimes[(size_t)l_iIndex], l_strEvent, l_oStepped});

            // A non-centred anchor (SubMPDI.cpp GetRenderPuzzleDataAndMatrix): the drawn rectangle is the anchor's
            // rectangle of the drawn size, moved by the trim offset from the image centre (unscaled there), about the
            // point it turns around. Our sprite draws its whole original image centred and scaled, so the image sits
            // at anchorRect.xy + centreToTrim * (1 - scale) from that point.
            std::vector<Key<glm::vec3>> l_vImageOffsetKeys;
            if (l_iAnchorIndex != 4) {
                for (size_t l_nI = 0; l_nI < l_nN; l_nI++) {
                    const Point& l_oP = l_oPath.m_vPoints[l_nI];
                    const glm::vec2 l_v2Drawn = l_oP.m_bHasScale ? l_oP.m_v2Scale * l_vRes[l_nI].m_v2Frame : l_oP.m_v2Size;
                    const glm::vec2 l_v2Sc = l_v2Drawn / l_vRes[l_nI].m_v2Frame;
                    const int l_iAx = l_iAnchorIndex % 3, l_iAy = l_iAnchorIndex / 3;   // 0 left / up, 1 centre, 2 right / down
                    const glm::vec2 l_v2RectXY(l_iAx == 0 ? 0.0f : l_iAx == 1 ? -l_v2Drawn.x * 0.5f : -l_v2Drawn.x,
                                           l_iAy == 0 ? 0.0f : l_iAy == 1 ? -l_v2Drawn.y * 0.5f : -l_v2Drawn.y);
                    const glm::vec2 l_v2CentreToTrim = l_vRes[l_nI].m_v2Trim - l_vRes[l_nI].m_v2Orig * 0.5f;
                    // where our centred image's centre must be, from the turning point
                    const glm::vec2 l_v2Offset = l_v2RectXY + l_v2CentreToTrim * (glm::vec2(1.0f) - l_v2Sc);
                    l_vImageOffsetKeys.push_back({l_vTimes[l_nI], glm::vec3(l_v2Offset, 0.0f) + glm::vec3(l_oPath.m_v3RotOffset), l_oDataEase});
                }
            }
            Node l_oResult;
            if (l_oPath.m_v3RotOffset == glm::vec3(0.0f) && l_vImageOffsetKeys.empty()) {
                l_oImage.m_vPosKeys = l_vPosKeys;
                if (l_bAnyRot) l_oImage.m_vRotKeys = l_vRotKeys;
                l_oImage.m_v3Pos = l_vPosKeys.front().m_oV;
                l_oResult = l_oImage;
            } else {   // the image sits rotOffset from the point it turns about: a turning group with the image inside
                Node l_oGroup;
                l_oGroup.m_strName = l_strNodeName + "_pivot";
                l_oGroup.m_vPosKeys = l_vPosKeys;
                if (l_bAnyRot) l_oGroup.m_vRotKeys = l_vRotKeys;
                l_oGroup.m_v3Pos = l_vPosKeys.front().m_oV;
                l_oGroup.m_bLoop = l_oPath.m_bLoop;
                l_oGroup.m_bStayAtLastFrame = l_oClip.m_bStayAtLastFrame;
                l_oImage.m_bLoop = false;
                l_oImage.m_v3Pos = glm::vec3(l_oPath.m_v3RotOffset);
                if (!l_vImageOffsetKeys.empty()) {
                    l_oImage.m_vPosKeys = l_vImageOffsetKeys;
                    l_oImage.m_v3Pos = l_vImageOffsetKeys.front().m_oV;
                }
                l_oGroup.m_vChildren.push_back(l_oImage);
                l_oResult = l_oGroup;
            }
            if (l_oPath.m_fStart > 0.0f) {   // nothing shows before the path starts
                if (l_oResult.m_bLoop) {     // looping from its start, not from 0: a group holds the delay
                    Node l_oGate;
                    l_oGate.m_strName = l_strNodeName + "_start";
                    l_oGate.m_vVisibleKeys = {{0.0f, false, l_oStepped}, {l_oPath.m_fStart, true, l_oStepped}};
                    l_oGate.m_vChildren.push_back(l_oResult);
                    l_oResult = l_oGate;
                } else {
                    l_oResult.m_vVisibleKeys = {{0.0f, false, l_oStepped}, {l_oPath.m_fStart, true, l_oStepped}};
                }
            }
            l_oClip.m_oRoot.m_vChildren.push_back(l_oResult);
            l_oReport.m_iNodes++;
        }
        l_oFile.m_vClips.push_back(l_oClip);
        l_oReport.m_iClips++;
    }
    if (l_oFile.m_vClips.empty()) { l_oReport.m_vErrors.push_back(l_strWhere + ": no animations (MultiPathDynamicImage) in it"); return false; }

    std::error_code l_oEc;
    fs::create_directories(l_oOutDir, l_oEc);
    const fs::path l_oAnimPath = l_oOutDir / fs::u8path(l_oSrc.stem().u8string() + ".anim");
    std::ofstream l_oO(l_oAnimPath, std::ios::binary);
    if (!l_oO) { l_oReport.m_vErrors.push_back(l_strWhere + ": cannot write " + f_U8(l_oAnimPath)); return false; }
    l_oO << f_AnimToJson(l_oFile);
    l_oO.close();
    l_oReport.m_iAnimFiles++;
    l_oReport.m_vWritten.push_back(f_U8(l_oAnimPath));
    return true;
}

MpdiConvertReport f_ConvertMpdiFolder(const std::string& l_strFolder, const std::string& l_strOutDir) {
    MpdiConvertReport l_oReport;
    Ctx l_oCtx{l_oReport, {}};
    std::error_code l_oEc;
    std::vector<fs::path> l_vFiles;
    for (auto l_oIt = fs::recursive_directory_iterator(fs::u8path(l_strFolder), l_oEc); !l_oEc && l_oIt != fs::recursive_directory_iterator(); l_oIt.increment(l_oEc))
        if (l_oIt->is_regular_file(l_oEc) && f_Lower(l_oIt->path().extension().u8string()) == ".mpdi") l_vFiles.push_back(l_oIt->path());
    std::sort(l_vFiles.begin(), l_vFiles.end());
    l_oReport.m_iMpdiFiles = (int)l_vFiles.size();
    for (const fs::path& l_oF : l_vFiles) {
        std::string l_strOut;
        if (!l_strOutDir.empty()) {
            const fs::path l_oRel = fs::relative(l_oF.parent_path(), fs::u8path(l_strFolder), l_oEc);
            l_strOut = f_U8((fs::u8path(l_strOutDir) / l_oRel).lexically_normal());
        }
        f_ConvertMpdiWith(l_oCtx, f_U8(l_oF), l_strOut);
    }
    return l_oReport;
}

}  // namespace animed
