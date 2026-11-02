// atlas_file.h -- reads libGDX / Spine texture atlas files (.atlas), including the x_ extension
// fields written by the TOMS atlas tool (tools/atlas). Header only, no dependencies.
//
// Both atlas dialects are accepted:
//   new (libGDX 1.9.12+, Spine 4):  bounds: x, y, w, h     offsets: ox, oy, origW, origH
//   old (Spine 3, older libGDX):    xy / size / orig / offset / rotate / index
// libGDX offsets count y from the BOTTOM of the original image; AtlasRegion stores it from the top.
//
// Extension fields (other parsers keep or skip them; TOMS reads them):
//   x_parent: <name>        child sprite: no pixels of its own, a rect inside <name>
//   x_local: x, y, w, h     that rect, in the parent's original pixels
//   x_pivot: px, py        pivot, 0..1 of the original size from the top-left
//   x_tags: a, b            free tags (e.g. map, ui)
//
//   toms::AtlasFile a;
//   if (toms::parseAtlas(text, a, &err)) { const toms::AtlasRegion* r = a.find("floor"); r->uv ... }
#pragma once
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace toms {

struct AtlasPage {
    std::string m_strFile;
    int m_iW = 0, m_iH = 0;
    std::string m_strFormat, m_strFilter, m_strRepeat;
    bool m_bPma = false;
};

struct AtlasRegion {
    std::string m_strName;
    int m_iPage = 0;
    int m_iX = 0, m_iY = 0, m_iW = 0, m_iH = 0;     // pixels in the page; w/h unrotated (a rotated region covers h x w)
    int m_iOffX = 0, m_iOffY = 0;             // top-left of the packed pixels inside the original, y down
    int m_iOrigW = 0, m_iOrigH = 0;
    bool m_bRotated = false;
    int m_iIndex = -1;
    bool m_bHasSplit = false;
    int m_arrSplit[4] = {0, 0, 0, 0};        // left, right, top, bottom
    float m_arrPivot[2] = {0.5f, 0.5f};
    bool m_bChild = false;
    std::string m_strParent;
    int m_arrLocal[4] = {0, 0, 0, 0};
    std::vector<std::string> m_vTags;
    float m_arrUv[4] = {0, 0, 0, 0};         // u0, v0, u1, v1 (v down), set when the page size is known
};

struct AtlasFile {
    std::vector<AtlasPage> m_vPages;
    std::vector<AtlasRegion> m_vRegions;
    std::unordered_map<std::string, int> m_mapByName;

    const AtlasRegion* f_Find(const std::string& l_strName) const {
        auto l_itIt = m_mapByName.find(l_strName);
        return l_itIt == m_mapByName.end() ? nullptr : &m_vRegions[(size_t)l_itIt->second];
    }
    // (Re)computes every region's uv from the page sizes; call again if a page's size was only
    // known after its image was loaded.
    void f_ComputeUVs() {
        for (AtlasRegion& l_oR : m_vRegions) {
            if (l_oR.m_iPage < 0 || l_oR.m_iPage >= (int)m_vPages.size()) continue;
            const AtlasPage& l_oP = m_vPages[(size_t)l_oR.m_iPage];
            if (l_oP.m_iW <= 0 || l_oP.m_iH <= 0) continue;
            const int l_iPw = l_oR.m_bRotated ? l_oR.m_iH : l_oR.m_iW, l_iPh = l_oR.m_bRotated ? l_oR.m_iW : l_oR.m_iH;
            l_oR.m_arrUv[0] = (float)l_oR.m_iX / l_oP.m_iW;
            l_oR.m_arrUv[1] = (float)l_oR.m_iY / l_oP.m_iH;
            l_oR.m_arrUv[2] = (float)(l_oR.m_iX + l_iPw) / l_oP.m_iW;
            l_oR.m_arrUv[3] = (float)(l_oR.m_iY + l_iPh) / l_oP.m_iH;
        }
    }
};

namespace atlas_detail {

inline std::string f_Trim(const std::string& l_strS) {
    size_t l_nA = 0, l_nB = l_strS.size();
    while (l_nA < l_nB && (l_strS[l_nA] == ' ' || l_strS[l_nA] == '\t' || l_strS[l_nA] == '\r' || l_strS[l_nA] == '\n')) l_nA++;
    while (l_nB > l_nA && (l_strS[l_nB - 1] == ' ' || l_strS[l_nB - 1] == '\t' || l_strS[l_nB - 1] == '\r' || l_strS[l_nB - 1] == '\n')) l_nB--;
    return l_strS.substr(l_nA, l_nB - l_nA);
}

inline std::vector<std::string> f_SplitValues(const std::string& l_strS) {
    std::vector<std::string> l_vOut;
    size_t l_nStart = 0;
    while (l_nStart <= l_strS.size()) {
        const size_t l_nC = l_strS.find(',', l_nStart);
        const std::string l_strV = f_Trim(l_strS.substr(l_nStart, l_nC == std::string::npos ? std::string::npos : l_nC - l_nStart));
        if (!l_strV.empty() || l_nC != std::string::npos) l_vOut.push_back(l_strV);
        if (l_nC == std::string::npos) break;
        l_nStart = l_nC + 1;
    }
    return l_vOut;
}

inline int f_ToInt(const std::string& l_strS) { return (int)std::strtol(l_strS.c_str(), nullptr, 10); }
inline float f_ToFloat(const std::string& l_strS) { return std::strtof(l_strS.c_str(), nullptr); }

inline void f_Ints(const std::vector<std::string>& l_vV, int* l_pOut, size_t l_nN) {
    for (size_t l_nI = 0; l_nI < l_nN && l_nI < l_vV.size(); l_nI++) l_pOut[l_nI] = f_ToInt(l_vV[l_nI]);
}

}  // namespace atlas_detail

inline bool f_ParseAtlas(const std::string& l_strText, AtlasFile& l_oOut, std::string* l_pErr = nullptr) {
    using namespace atlas_detail;
    l_oOut = AtlasFile();
    bool l_bExpectPage = true;
    AtlasRegion* l_pCur = nullptr;
    // Raw offsets per region (old/new formats keep y from the bottom), resolved after parsing.
    struct Raw { bool m_bHasOrig = false, m_bHasOffset = false; int m_iOffX = 0, m_iOffYUp = 0; };
    std::vector<Raw> l_vRaw;
    size_t l_nPos = 0;
    int l_iLineNo = 0;
    while (l_nPos <= l_strText.size()) {
        size_t l_nNl = l_strText.find('\n', l_nPos);
        if (l_nNl == std::string::npos) l_nNl = l_strText.size();
        const std::string l_strLine = f_Trim(l_strText.substr(l_nPos, l_nNl - l_nPos));
        l_nPos = l_nNl + 1;
        l_iLineNo++;
        if (l_strLine.empty()) {
            l_bExpectPage = true;
            l_pCur = nullptr;
            if (l_nNl >= l_strText.size()) break;
            continue;
        }
        const size_t l_nColon = l_strLine.find(':');
        if (l_nColon == std::string::npos) {
            if (l_bExpectPage) {
                AtlasPage l_oP;
                l_oP.m_strFile = l_strLine;
                l_oOut.m_vPages.push_back(l_oP);
                l_bExpectPage = false;
                l_pCur = nullptr;
            } else {
                if (l_oOut.m_vPages.empty()) { if (l_pErr) *l_pErr = "line " + std::to_string(l_iLineNo) + ": region before any page"; return false; }
                AtlasRegion l_oR;
                l_oR.m_strName = l_strLine;
                l_oR.m_iPage = (int)l_oOut.m_vPages.size() - 1;
                l_oOut.m_vRegions.push_back(l_oR);
                l_vRaw.push_back(Raw());
                l_pCur = &l_oOut.m_vRegions.back();
            }
            continue;
        }
        if (l_oOut.m_vPages.empty()) { if (l_pErr) *l_pErr = "line " + std::to_string(l_iLineNo) + ": field before any page"; return false; }
        const std::string l_strKey = f_Trim(l_strLine.substr(0, l_nColon));
        const std::string l_strValue = f_Trim(l_strLine.substr(l_nColon + 1));
        const std::vector<std::string> l_vV = f_SplitValues(l_strValue);
        if (!l_pCur) {
            AtlasPage& l_oP = l_oOut.m_vPages.back();
            if (l_strKey == "size" && l_vV.size() >= 2) { l_oP.m_iW = f_ToInt(l_vV[0]); l_oP.m_iH = f_ToInt(l_vV[1]); }
            else if (l_strKey == "format") l_oP.m_strFormat = l_strValue;
            else if (l_strKey == "filter") l_oP.m_strFilter = l_strValue;
            else if (l_strKey == "repeat") l_oP.m_strRepeat = l_strValue;
            else if (l_strKey == "pma") l_oP.m_bPma = l_strValue == "true";
            continue;   // unknown page fields are ignored
        }
        Raw& l_oRw = l_vRaw.back();
        AtlasRegion& l_oR = *l_pCur;
        int l_arrT[4] = {0, 0, 0, 0};
        if (l_strKey == "bounds") { f_Ints(l_vV, l_arrT, 4); l_oR.m_iX = l_arrT[0]; l_oR.m_iY = l_arrT[1]; l_oR.m_iW = l_arrT[2]; l_oR.m_iH = l_arrT[3]; }
        else if (l_strKey == "xy") { f_Ints(l_vV, l_arrT, 2); l_oR.m_iX = l_arrT[0]; l_oR.m_iY = l_arrT[1]; }
        else if (l_strKey == "size") { f_Ints(l_vV, l_arrT, 2); l_oR.m_iW = l_arrT[0]; l_oR.m_iH = l_arrT[1]; }
        else if (l_strKey == "offsets") { f_Ints(l_vV, l_arrT, 4); l_oRw.m_bHasOffset = l_oRw.m_bHasOrig = true; l_oRw.m_iOffX = l_arrT[0]; l_oRw.m_iOffYUp = l_arrT[1]; l_oR.m_iOrigW = l_arrT[2]; l_oR.m_iOrigH = l_arrT[3]; }
        else if (l_strKey == "orig") { f_Ints(l_vV, l_arrT, 2); l_oRw.m_bHasOrig = true; l_oR.m_iOrigW = l_arrT[0]; l_oR.m_iOrigH = l_arrT[1]; }
        else if (l_strKey == "offset") { f_Ints(l_vV, l_arrT, 2); l_oRw.m_bHasOffset = true; l_oRw.m_iOffX = l_arrT[0]; l_oRw.m_iOffYUp = l_arrT[1]; }
        else if (l_strKey == "rotate") l_oR.m_bRotated = l_strValue == "true" || l_strValue == "90";
        else if (l_strKey == "index") l_oR.m_iIndex = f_ToInt(l_strValue);
        else if (l_strKey == "split") { f_Ints(l_vV, l_oR.m_arrSplit, 4); l_oR.m_bHasSplit = true; }
        else if (l_strKey == "x_pivot" && l_vV.size() >= 2) { l_oR.m_arrPivot[0] = f_ToFloat(l_vV[0]); l_oR.m_arrPivot[1] = f_ToFloat(l_vV[1]); }
        else if (l_strKey == "x_parent") { l_oR.m_strParent = l_strValue; l_oR.m_bChild = !l_strValue.empty(); }
        else if (l_strKey == "x_local") f_Ints(l_vV, l_oR.m_arrLocal, 4);
        else if (l_strKey == "x_tags") { for (const std::string& l_strS : l_vV) if (!l_strS.empty()) l_oR.m_vTags.push_back(l_strS); }
        // anything else (pad, custom fields of other tools) is ignored
    }
    for (size_t l_nI = 0; l_nI < l_oOut.m_vRegions.size(); l_nI++) {
        AtlasRegion& l_oR = l_oOut.m_vRegions[l_nI];
        if (!l_vRaw[l_nI].m_bHasOrig) { l_oR.m_iOrigW = l_oR.m_iW; l_oR.m_iOrigH = l_oR.m_iH; }
        l_oR.m_iOffX = l_vRaw[l_nI].m_iOffX;
        l_oR.m_iOffY = l_vRaw[l_nI].m_bHasOffset ? l_oR.m_iOrigH - l_vRaw[l_nI].m_iOffYUp - l_oR.m_iH : 0;
        if (l_oOut.m_mapByName.count(l_oR.m_strName) == 0) l_oOut.m_mapByName[l_oR.m_strName] = (int)l_nI;
    }
    l_oOut.f_ComputeUVs();
    return true;
}

// ---- RmlUi -------------------------------------------------------------------------------------
// RmlUi reads sprite sheets only from RCSS (@spritesheet). The game writes that text from the
// atlas it already loaded, at start-up, so the UI can never disagree with the atlas: .rml files
// name sprites (<img sprite="coin"/>), never positions.

// A sprite name as RCSS allows it: '/' and other characters become '-' ("ui/btn_ok" -> "ui-btn_ok").
inline std::string f_RmlSpriteName(const std::string& l_strName) {
    std::string l_strS;
    for (char l_cC : l_strName) {
        const bool l_bOk = (l_cC >= 'a' && l_cC <= 'z') || (l_cC >= 'A' && l_cC <= 'Z') || (l_cC >= '0' && l_cC <= '9') || l_cC == '-' || l_cC == '_';
        l_strS += l_bOk ? l_cC : '-';
    }
    return l_strS;
}

// One @spritesheet per page. pageDir is where the page images are, as seen from the RCSS
// document's folder (e.g. "../atlas/"). Image regions and child sprites alike are listed.
inline std::string f_RmlSpritesheets(const AtlasFile& l_oA, const std::string& l_strPageDir) {
    std::string l_strOut = "/* made by the game from its sprite atlas (tools/atlas) at start-up */\n";
    for (size_t l_nP = 0; l_nP < l_oA.m_vPages.size(); l_nP++) {
        l_strOut += "@spritesheet atlas-page" + std::to_string(l_nP) + "\n{\n    src: " + l_strPageDir + l_oA.m_vPages[l_nP].m_strFile + ";\n";
        for (const AtlasRegion& l_oR : l_oA.m_vRegions) {
            if (l_oR.m_iPage != (int)l_nP) continue;
            const int l_iPw = l_oR.m_bRotated ? l_oR.m_iH : l_oR.m_iW, l_iPh = l_oR.m_bRotated ? l_oR.m_iW : l_oR.m_iH;
            l_strOut += "    " + f_RmlSpriteName(l_oR.m_strName) + ": " + std::to_string(l_oR.m_iX) + "px " + std::to_string(l_oR.m_iY) + "px " +
                   std::to_string(l_iPw) + "px " + std::to_string(l_iPh) + "px;\n";
        }
        l_strOut += "}\n";
    }
    return l_strOut;
}

}  // namespace toms
