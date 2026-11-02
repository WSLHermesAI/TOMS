// atlas_export.cpp -- see atlas_export.h.
#include "atlas_export.h"
#include "atlas_xml.h"

#include <atlas_file.h>   // src/core/engine: the game's own .atlas reader
#include <json.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

namespace atlas {

// ---- helpers -----------------------------------------------------------------------------------

const std::vector<std::string>& f_ExportFormats() {
    static const std::vector<std::string> l_vF = {"atlas", "atlas-spine3", "plist", "tp-json", "pi", "rcss"};
    return l_vF;
}

bool f_IsExportFormat(const std::string& l_strF) {
    const auto& l_vAll = f_ExportFormats();
    return std::find(l_vAll.begin(), l_vAll.end(), l_strF) != l_vAll.end();
}

// Locale-independent, shortest round-trip float text ("0.5", "0.0703125").
static std::string f_Num(double l_dV) {
    if (l_dV == 0) return "0";
    char l_pszBuf[64];
    auto l_oR = std::to_chars(l_pszBuf, l_pszBuf + sizeof l_pszBuf, (float)l_dV);
    return std::string(l_pszBuf, l_oR.ptr);
}

// Fixed decimals, as the old PI editor wrote RelativePos ("18.00").
static std::string f_Fixed2(double l_dV) {
    char l_pszBuf[64];
    auto l_oR = std::to_chars(l_pszBuf, l_pszBuf + sizeof l_pszBuf, l_dV, std::chars_format::fixed, 2);
    return std::string(l_pszBuf, l_oR.ptr);
}

static std::string f_Ints(std::initializer_list<int> l_oV, const char* l_pszSep = ", ") {
    std::string l_strS;
    for (int l_iX : l_oV) { if (!l_strS.empty()) l_strS += l_pszSep; l_strS += std::to_string(l_iX); }
    return l_strS;
}

static std::vector<uint8_t> f_Bytes(const std::string& l_strS) { return std::vector<uint8_t>(l_strS.begin(), l_strS.end()); }

static bool f_DefaultPivot(const Region& l_oR) { return l_oR.m_arrPivot[0] == 0.5f && l_oR.m_arrPivot[1] == 0.5f; }

std::string f_PageBaseName(const BuildResult& l_oB, int l_iPage) {
    return l_oB.m_strName + (l_iPage == 0 ? std::string() : "_" + std::to_string(l_iPage));
}

static std::string f_PixelFormat(const BuildResult&) { return "RGBA8888"; }

// ---- libGDX / Spine 4 --------------------------------------------------------------------------

static void f_LibgdxPageHeader(std::ostringstream& l_oO, const BuildResult& l_oB, size_t l_nP, bool l_bSpine3) {
    const Page& l_oPg = l_oB.m_vPages[l_nP];
    const std::string l_strFilter = l_oB.m_oSettings.m_strFilter == "Linear" ? "Linear" : "Nearest";
    l_oO << l_oPg.m_strFile << "\n";
    l_oO << "size: " << l_oPg.m_oImage.m_iW << (l_bSpine3 ? "," : ", ") << l_oPg.m_oImage.m_iH << "\n";
    l_oO << "format: " << f_PixelFormat(l_oB) << "\n";
    l_oO << "filter: " << l_strFilter << (l_bSpine3 ? "," : ", ") << l_strFilter << "\n";
    l_oO << "repeat: none\n";
    if (!l_bSpine3 && l_oB.m_oSettings.m_bPremultiplyAlpha) l_oO << "pma: true\n";
}

std::string f_ExportLibgdx(const BuildResult& l_oB) {
    std::ostringstream l_oO;
    for (size_t l_nP = 0; l_nP < l_oB.m_vPages.size(); l_nP++) {
        if (l_nP) l_oO << "\n";
        f_LibgdxPageHeader(l_oO, l_oB, l_nP, false);
        for (const Region& l_oR : l_oB.m_vRegions) {
            if (l_oR.m_iPage != (int)l_nP) continue;
            l_oO << l_oR.m_strName << "\n";
            l_oO << "  bounds: " << f_Ints({l_oR.m_oFrame.m_iX, l_oR.m_oFrame.m_iY, l_oR.m_oFrame.m_iW, l_oR.m_oFrame.m_iH}) << "\n";
            if (l_oR.f_Trimmed())   // libGDX counts the offset's y from the bottom of the original image
                l_oO << "  offsets: " << f_Ints({l_oR.m_iOffsetX, l_oR.m_iOrigH - l_oR.m_iOffsetY - l_oR.m_oFrame.m_iH, l_oR.m_iOrigW, l_oR.m_iOrigH}) << "\n";
            if (l_oR.m_bHasSplit) l_oO << "  split: " << f_Ints({l_oR.m_arrSplit[0], l_oR.m_arrSplit[1], l_oR.m_arrSplit[2], l_oR.m_arrSplit[3]}) << "\n";
            if (l_oR.m_bChild) {
                l_oO << "  x_parent: " << l_oR.m_strParent << "\n";
                l_oO << "  x_local: " << f_Ints({l_oR.m_oLocal.m_iX, l_oR.m_oLocal.m_iY, l_oR.m_oLocal.m_iW, l_oR.m_oLocal.m_iH}) << "\n";
            }
            if (!f_DefaultPivot(l_oR)) l_oO << "  x_pivot: " << f_Num(l_oR.m_arrPivot[0]) << ", " << f_Num(l_oR.m_arrPivot[1]) << "\n";
            if (!l_oR.m_vTags.empty()) {
                l_oO << "  x_tags: ";
                for (size_t l_nI = 0; l_nI < l_oR.m_vTags.size(); l_nI++) l_oO << (l_nI ? ", " : "") << l_oR.m_vTags[l_nI];
                l_oO << "\n";
            }
        }
    }
    return l_oO.str();
}

std::string f_ExportSpine3(const BuildResult& l_oB) {
    std::ostringstream l_oO;
    for (size_t l_nP = 0; l_nP < l_oB.m_vPages.size(); l_nP++) {
        l_oO << "\n";   // the old writer starts every page with a blank line
        f_LibgdxPageHeader(l_oO, l_oB, l_nP, true);
        for (const Region& l_oR : l_oB.m_vRegions) {
            if (l_oR.m_iPage != (int)l_nP) continue;
            l_oO << l_oR.m_strName << "\n";
            l_oO << "  rotate: false\n";
            l_oO << "  xy: " << f_Ints({l_oR.m_oFrame.m_iX, l_oR.m_oFrame.m_iY}) << "\n";
            l_oO << "  size: " << f_Ints({l_oR.m_oFrame.m_iW, l_oR.m_oFrame.m_iH}) << "\n";
            if (l_oR.m_bHasSplit) l_oO << "  split: " << f_Ints({l_oR.m_arrSplit[0], l_oR.m_arrSplit[1], l_oR.m_arrSplit[2], l_oR.m_arrSplit[3]}) << "\n";
            l_oO << "  orig: " << f_Ints({l_oR.m_iOrigW, l_oR.m_iOrigH}) << "\n";
            l_oO << "  offset: " << f_Ints({l_oR.m_iOffsetX, l_oR.m_iOrigH - l_oR.m_iOffsetY - l_oR.m_oFrame.m_iH}) << "\n";
            l_oO << "  index: -1\n";
        }
    }
    return l_oO.str();
}

// ---- Cocos plist (TexturePacker format 3) ------------------------------------------------------

std::string f_ExportPlist(const BuildResult& l_oB, int l_iPage) {
    const Page& l_oPg = l_oB.m_vPages[l_iPage];
    std::ostringstream l_oO;
    auto l_fnStr = [&](const char* l_pszKey, const std::string& l_strV) {
        l_oO << "\t\t\t\t<key>" << l_pszKey << "</key>\n\t\t\t\t<string>" << f_XmlEscape(l_strV) << "</string>\n";
    };
    l_oO << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         "<plist version=\"1.0\">\n\t<dict>\n\t\t<key>frames</key>\n\t\t<dict>\n";
    for (const Region& l_oR : l_oB.m_vRegions) {
        if (l_oR.m_iPage != l_iPage) continue;
        l_oO << "\t\t\t<key>" << f_XmlEscape(l_oR.m_strName) << "</key>\n\t\t\t<dict>\n";
        l_oO << "\t\t\t\t<key>aliases</key>\n\t\t\t\t<array/>\n";
        // Offset of the trimmed centre from the original centre, y up (cocos convention).
        const double l_dOx = l_oR.m_iOffsetX + l_oR.m_oFrame.m_iW / 2.0 - l_oR.m_iOrigW / 2.0;
        const double l_dOy = -(l_oR.m_iOffsetY + l_oR.m_oFrame.m_iH / 2.0 - l_oR.m_iOrigH / 2.0);
        if (!f_DefaultPivot(l_oR)) l_fnStr("anchor", "{" + f_Num(l_oR.m_arrPivot[0]) + "," + f_Num(1.0 - l_oR.m_arrPivot[1]) + "}");
        l_fnStr("spriteOffset", "{" + f_Num(l_dOx) + "," + f_Num(l_dOy) + "}");
        l_fnStr("spriteSize", "{" + f_Ints({l_oR.m_oFrame.m_iW, l_oR.m_oFrame.m_iH}, ",") + "}");
        l_fnStr("spriteSourceSize", "{" + f_Ints({l_oR.m_iOrigW, l_oR.m_iOrigH}, ",") + "}");
        l_fnStr("textureRect", "{{" + f_Ints({l_oR.m_oFrame.m_iX, l_oR.m_oFrame.m_iY}, ",") + "},{" + f_Ints({l_oR.m_oFrame.m_iW, l_oR.m_oFrame.m_iH}, ",") + "}}");
        l_oO << "\t\t\t\t<key>textureRotated</key>\n\t\t\t\t<false/>\n";
        if (l_oR.m_bHasSplit) l_fnStr("x_split", "{" + f_Ints({l_oR.m_arrSplit[0], l_oR.m_arrSplit[1], l_oR.m_arrSplit[2], l_oR.m_arrSplit[3]}, ",") + "}");
        if (l_oR.m_bChild) {
            l_fnStr("x_parent", l_oR.m_strParent);
            l_fnStr("x_local", "{{" + f_Ints({l_oR.m_oLocal.m_iX, l_oR.m_oLocal.m_iY}, ",") + "},{" + f_Ints({l_oR.m_oLocal.m_iW, l_oR.m_oLocal.m_iH}, ",") + "}}");
        }
        if (!l_oR.m_vTags.empty()) {
            l_oO << "\t\t\t\t<key>x_tags</key>\n\t\t\t\t<array>\n";
            for (const std::string& l_strT : l_oR.m_vTags) l_oO << "\t\t\t\t\t<string>" << f_XmlEscape(l_strT) << "</string>\n";
            l_oO << "\t\t\t\t</array>\n";
        }
        l_oO << "\t\t\t</dict>\n";
    }
    l_oO << "\t\t</dict>\n\t\t<key>metadata</key>\n\t\t<dict>\n"
         "\t\t\t<key>format</key>\n\t\t\t<integer>3</integer>\n"
         "\t\t\t<key>pixelFormat</key>\n\t\t\t<string>" << f_PixelFormat(l_oB) << "</string>\n"
         "\t\t\t<key>premultiplyAlpha</key>\n\t\t\t<" << (l_oB.m_oSettings.m_bPremultiplyAlpha ? "true" : "false") << "/>\n"
         "\t\t\t<key>realTextureFileName</key>\n\t\t\t<string>" << f_XmlEscape(l_oPg.m_strFile) << "</string>\n"
         "\t\t\t<key>size</key>\n\t\t\t<string>{" << l_oPg.m_oImage.m_iW << "," << l_oPg.m_oImage.m_iH << "}</string>\n"
         "\t\t\t<key>textureFileName</key>\n\t\t\t<string>" << f_XmlEscape(l_oPg.m_strFile) << "</string>\n"
         "\t\t</dict>\n\t</dict>\n</plist>\n";
    return l_oO.str();
}

// ---- TexturePacker JSON hash -------------------------------------------------------------------

std::string f_ExportTpJson(const BuildResult& l_oB, int l_iPage) {
    const Page& l_oPg = l_oB.m_vPages[l_iPage];
    ordered_json l_jJ;
    ordered_json l_jFrames = ordered_json::object();
    for (const Region& l_oR : l_oB.m_vRegions) {
        if (l_oR.m_iPage != l_iPage) continue;
        ordered_json l_jF;
        l_jF["frame"] = {{"x", l_oR.m_oFrame.m_iX}, {"y", l_oR.m_oFrame.m_iY}, {"w", l_oR.m_oFrame.m_iW}, {"h", l_oR.m_oFrame.m_iH}};
        l_jF["rotated"] = false;
        l_jF["trimmed"] = l_oR.f_Trimmed();
        l_jF["spriteSourceSize"] = {{"x", l_oR.m_iOffsetX}, {"y", l_oR.m_iOffsetY}, {"w", l_oR.m_oFrame.m_iW}, {"h", l_oR.m_oFrame.m_iH}};
        l_jF["sourceSize"] = {{"w", l_oR.m_iOrigW}, {"h", l_oR.m_iOrigH}};
        l_jF["pivot"] = {{"x", l_oR.m_arrPivot[0]}, {"y", l_oR.m_arrPivot[1]}};
        ordered_json l_jX = ordered_json::object();
        if (l_oR.m_bHasSplit) l_jX["split"] = {l_oR.m_arrSplit[0], l_oR.m_arrSplit[1], l_oR.m_arrSplit[2], l_oR.m_arrSplit[3]};
        if (l_oR.m_bChild) { l_jX["parent"] = l_oR.m_strParent; l_jX["local"] = {l_oR.m_oLocal.m_iX, l_oR.m_oLocal.m_iY, l_oR.m_oLocal.m_iW, l_oR.m_oLocal.m_iH}; }
        if (!l_oR.m_vTags.empty()) l_jX["tags"] = l_oR.m_vTags;
        if (!l_jX.empty()) l_jF["x"] = l_jX;
        l_jFrames[l_oR.m_strName] = l_jF;
    }
    l_jJ["frames"] = l_jFrames;
    if (l_iPage == 0 && !l_oB.m_vAnimations.empty()) {   // Pixi reads "animations"; frame times go in meta.x
        ordered_json l_jAnims = ordered_json::object();
        for (const Animation& l_oA : l_oB.m_vAnimations) {
            ordered_json l_jList = ordered_json::array();
            for (const AnimFrame& l_oF : l_oA.m_vFrames) l_jList.push_back(l_oF.m_strSprite);
            l_jAnims[l_oA.m_strName] = l_jList;
        }
        l_jJ["animations"] = l_jAnims;
    }
    ordered_json l_jMeta;
    l_jMeta["app"] = "TOMS atlas tool";
    l_jMeta["version"] = "1";
    l_jMeta["image"] = l_oPg.m_strFile;
    l_jMeta["format"] = f_PixelFormat(l_oB);
    l_jMeta["size"] = {{"w", l_oPg.m_oImage.m_iW}, {"h", l_oPg.m_oImage.m_iH}};
    l_jMeta["scale"] = "1";
    if (l_iPage == 0 && !l_oB.m_vAnimations.empty()) {
        ordered_json l_jXa = ordered_json::object();
        for (const Animation& l_oA : l_oB.m_vAnimations) {
            ordered_json l_jTimes = ordered_json::array();
            for (const AnimFrame& l_oF : l_oA.m_vFrames) l_jTimes.push_back(l_oF.m_fTime);
            l_jXa[l_oA.m_strName] = {{"loop", l_oA.m_bLoop}, {"times", l_jTimes}};
        }
        l_jMeta["x"] = {{"animations", l_jXa}};
    }
    l_jJ["meta"] = l_jMeta;
    return l_jJ.dump(1, '\t') + "\n";
}

// ---- FM79979 PuzzleImage (.pi) -----------------------------------------------------------------

std::string f_ExportPi(const BuildResult& l_oB, int l_iPage) {
    const Page& l_oPg = l_oB.m_vPages[l_iPage];
    std::vector<const Region*> l_vUnits;
    for (const Region& l_oR : l_oB.m_vRegions)
        if (l_oR.m_iPage == l_iPage) l_vUnits.push_back(&l_oR);   // already sorted by name, as the old editor wrote them
    std::ostringstream l_oO;
    l_oO << "<!-- Author:TOMS atlas tool -->\n";
    std::string l_strOrder;
    for (const Region* l_pR : l_vUnits) l_strOrder += (l_strOrder.empty() ? "" : ",") + l_pR->m_strName;
    l_oO << "<PuzzleImage OriginalNameSort=\"" << f_XmlEscape(l_strOrder) << "\" ImageName=\"" << f_XmlEscape(l_oPg.m_strFile)
      << "\" Count=\"" << l_vUnits.size() << "\" GeneratePuzzleimageUnit=\"0\" ImageDistance=\""
      << l_oB.m_oSettings.m_iPadding << "," << l_oB.m_oSettings.m_iPadding << "\">\n";
    const double l_dW = l_oPg.m_oImage.m_iW, l_dH = l_oPg.m_oImage.m_iH;
    for (const Region* l_pR : l_vUnits) {
        l_oO << "    <PuzzleUnit Name=\"" << f_XmlEscape(l_pR->m_strName) << "\" UV=\"" << f_Num(l_pR->m_oFrame.m_iX / l_dW) << "," << f_Num(l_pR->m_oFrame.m_iY / l_dH)
          << "," << f_Num(l_pR->m_oFrame.f_Right() / l_dW) << "," << f_Num(l_pR->m_oFrame.f_Bottom() / l_dH) << "\" OffsetPos=\"" << l_pR->m_iOffsetX << ","
          << l_pR->m_iOffsetY << "\" Size=\"" << l_pR->m_oFrame.m_iW << "," << l_pR->m_oFrame.m_iH << "\" OriginalSize=\"" << l_pR->m_iOrigW << ","
          << l_pR->m_iOrigH << "\" ShowPosInPI=\"" << l_pR->m_oFrame.m_iX << "," << l_pR->m_oFrame.m_iY << "\" />\n";
    }
    for (const Region* l_pR : l_vUnits)
        if (l_pR->m_bChild && !l_pR->m_bBaked)   // a baked child has its own pixels: a plain unit for the old format
            l_oO << "    <PuzzleUnitChild Name=\"" << f_XmlEscape(l_pR->m_strName) << "\" AttachParent=\"" << f_XmlEscape(l_pR->m_strParent)
              << "\" RelativePos=\"" << f_Fixed2(l_pR->m_oLocal.m_iX) << "," << f_Fixed2(l_pR->m_oLocal.m_iY) << ",0.00\" />\n";
    // Sequence animations whose frames all live on this page.
    std::vector<const Animation*> l_vAnims;
    for (const Animation& l_oA : l_oB.m_vAnimations) {
        bool l_bHere = !l_oA.m_vFrames.empty();
        for (const AnimFrame& l_oF : l_oA.m_vFrames) {
            const Region* l_pR = l_oB.f_Find(l_oF.m_strSprite);
            if (!l_pR || l_pR->m_iPage != l_iPage) l_bHere = false;
        }
        if (l_bHere) l_vAnims.push_back(&l_oA);
    }
    if (!l_vAnims.empty()) {
        l_oO << "    <AnimationData Count=\"" << l_vAnims.size() << "\">\n";
        for (const Animation* l_pA : l_vAnims) {
            std::string l_strNames, l_strTimes;
            for (const AnimFrame& l_oF : l_pA->m_vFrames) { l_strNames += l_oF.m_strSprite + ","; l_strTimes += f_Num(l_oF.m_fTime) + ","; }
            l_oO << "        <AnimationDataUnit Name=\"" << f_XmlEscape(l_pA->m_strName) << "\" Count=\"" << l_pA->m_vFrames.size()
              << "\" ImageList=\"" << f_XmlEscape(l_strNames) << "\" TimeList=\"" << l_strTimes << "\" />\n";
        }
        l_oO << "    </AnimationData>\n";
    }
    l_oO << "</PuzzleImage>\n";
    return l_oO.str();
}

// ---- RmlUi @spritesheet ------------------------------------------------------------------------

static std::string f_RcssIdent(const std::string& l_strN) {
    std::string l_strS;
    for (char l_cC : l_strN) l_strS += (isalnum((unsigned char)l_cC) || l_cC == '-' || l_cC == '_') ? l_cC : '-';
    return l_strS;
}

std::string f_ExportRcss(const BuildResult& l_oB) {
    std::ostringstream l_oO;
    l_oO << "/* Generated by the TOMS atlas tool -- do not edit; change the .atlasproj and rebuild.\n"
         "   Sprite names: '/' and other characters RCSS does not allow become '-'. */\n";
    for (size_t l_nP = 0; l_nP < l_oB.m_vPages.size(); l_nP++) {
        l_oO << "\n@spritesheet " << f_RcssIdent(f_PageBaseName(l_oB, (int)l_nP)) << "\n{\n";
        l_oO << "    src: " << l_oB.m_vPages[l_nP].m_strFile << ";\n";
        l_oO << "    resolution: 1x;\n";
        for (const Region& l_oR : l_oB.m_vRegions)
            if (l_oR.m_iPage == (int)l_nP)
                l_oO << "    " << f_RcssIdent(l_oR.m_strName) << ": " << l_oR.m_oFrame.m_iX << "px " << l_oR.m_oFrame.m_iY << "px " << l_oR.m_oFrame.m_iW << "px "
                  << l_oR.m_oFrame.m_iH << "px;\n";
        l_oO << "}\n";
    }
    return l_oO.str();
}

// ---- all files ---------------------------------------------------------------------------------

bool f_ExportFiles(const BuildResult& l_oB, const std::vector<std::string>& l_vFormats, std::vector<ExportFile>& l_vOut, std::string* l_pErr) {
    l_vOut.clear();
    for (const std::string& l_strF : l_vFormats)
        if (!f_IsExportFormat(l_strF)) { if (l_pErr) *l_pErr = "unknown export format '" + l_strF + "'"; return false; }
    for (const Page& l_oP : l_oB.m_vPages) {
        if (!l_oP.m_oImage.f_Valid()) { if (l_pErr) *l_pErr = "page " + l_oP.m_strFile + " has no pixels (built without compositing)"; return false; }
        l_vOut.push_back({l_oP.m_strFile, f_EncodePng(l_oP.m_oImage)});
    }
    auto l_fnHas = [&](const char* l_pszF) { return std::find(l_vFormats.begin(), l_vFormats.end(), l_pszF) != l_vFormats.end(); };
    if (l_fnHas("atlas")) l_vOut.push_back({l_oB.m_strName + ".atlas", f_Bytes(f_ExportLibgdx(l_oB))});
    if (l_fnHas("atlas-spine3")) l_vOut.push_back({l_oB.m_strName + (l_fnHas("atlas") ? ".spine3.atlas" : ".atlas"), f_Bytes(f_ExportSpine3(l_oB))});
    for (int l_iP = 0; l_iP < (int)l_oB.m_vPages.size(); l_iP++) {
        if (l_fnHas("plist")) l_vOut.push_back({f_PageBaseName(l_oB, l_iP) + ".plist", f_Bytes(f_ExportPlist(l_oB, l_iP))});
        if (l_fnHas("tp-json")) l_vOut.push_back({f_PageBaseName(l_oB, l_iP) + ".json", f_Bytes(f_ExportTpJson(l_oB, l_iP))});
        if (l_fnHas("pi")) l_vOut.push_back({f_PageBaseName(l_oB, l_iP) + ".pi", f_Bytes(f_ExportPi(l_oB, l_iP))});
    }
    if (l_fnHas("rcss")) l_vOut.push_back({l_oB.m_strName + ".rcss", f_Bytes(f_ExportRcss(l_oB))});
    return true;
}

bool f_WriteFiles(const std::string& l_strDir, const std::vector<ExportFile>& l_vFiles, std::string* l_pErr,
                std::vector<std::string>* l_pChanged) {
    std::error_code l_oEc;
    fs::create_directories(f_U8path(l_strDir), l_oEc);
    for (const ExportFile& l_oF : l_vFiles) {
        const std::string l_strPath = f_JoinPath(l_strDir, l_oF.m_strName);
        // Leave an unchanged file alone, so its timestamp does not trigger rebuilds downstream.
        {
            std::ifstream l_oIn(f_U8path(l_strPath), std::ios::binary);
            if (l_oIn) {
                std::vector<uint8_t> l_vOld((std::istreambuf_iterator<char>(l_oIn)), std::istreambuf_iterator<char>());
                if (l_vOld == l_oF.m_vData) continue;
            }
        }
        std::ofstream l_oO(f_U8path(l_strPath), std::ios::binary);
        if (!l_oO || !l_oO.write((const char*)l_oF.m_vData.data(), (std::streamsize)l_oF.m_vData.size())) {
            if (l_pErr) *l_pErr = "cannot write " + l_strPath;
            return false;
        }
        if (l_pChanged) l_pChanged->push_back(l_oF.m_strName);
    }
    return true;
}

// ---- import ------------------------------------------------------------------------------------

static bool f_ReadText(const std::string& l_strPath, std::string& l_strOut, std::string* l_pErr) {
    std::ifstream l_oF(f_U8path(l_strPath), std::ios::binary);
    if (!l_oF) { if (l_pErr) *l_pErr = "cannot open " + l_strPath; return false; }
    std::stringstream l_oSs;
    l_oSs << l_oF.rdbuf();
    l_strOut = l_oSs.str();
    return true;
}

static std::vector<double> f_Numbers(const std::string& l_strS) {
    std::vector<double> l_vV;
    std::string l_strCur;
    for (char l_cC : l_strS + ",") {
        if (l_cC == ',' || l_cC == '{' || l_cC == '}' || l_cC == ' ') {
            if (!l_strCur.empty()) { l_vV.push_back(strtod(l_strCur.c_str(), nullptr)); l_strCur.clear(); }
        } else l_strCur += l_cC;
    }
    return l_vV;
}

static int f_Ri(double l_dV) { return (int)std::lround(l_dV); }

static bool f_LoadPage(const std::string& l_strDir, const std::string& l_strFile, Page& l_oPg, std::string* l_pErr) {
    l_oPg.m_strFile = l_strFile;
    return f_LoadImage(f_JoinPath(l_strDir, l_strFile), l_oPg.m_oImage, l_pErr);
}

bool f_ImportPi(const std::string& l_strPath, BuildResult& l_oOut, std::string* l_pErr) {
    l_oOut = BuildResult();
    std::string l_strText;
    if (!f_ReadText(l_strPath, l_strText, l_pErr)) return false;
    std::string l_strXerr;
    auto l_upRoot = f_ParseXml(l_strText, &l_strXerr);
    if (!l_upRoot) { if (l_pErr) *l_pErr = l_strPath + ": " + l_strXerr; return false; }
    if (l_upRoot->m_strName != "PuzzleImage") { if (l_pErr) *l_pErr = l_strPath + ": root is <" + l_upRoot->m_strName + ">, not <PuzzleImage>"; return false; }
    const std::string l_strImage = l_upRoot->f_AttrOr("ImageName");
    if (l_strImage.empty()) { if (l_pErr) *l_pErr = l_strPath + ": no ImageName"; return false; }
    std::string l_strImgFile = l_strImage;
    // Binary exports point at .pngb (Huffman); the plain .png is normally next to it.
    if (l_strImgFile.size() > 5 && l_strImgFile.substr(l_strImgFile.size() - 5) == ".pngb") l_strImgFile.resize(l_strImgFile.size() - 1);
    Page l_oPg;
    if (!f_LoadPage(f_ParentDir(l_strPath), l_strImgFile, l_oPg, l_pErr)) return false;
    l_oOut.m_strName = f_FileStem(l_strPath);
    const double l_dW = l_oPg.m_oImage.m_iW, l_dH = l_oPg.m_oImage.m_iH;
    l_oOut.m_vPages.push_back(std::move(l_oPg));
    for (const auto& l_upC : l_upRoot->m_vChildren) {
        if (l_upC->m_strName == "PuzzleUnit") {
            Region l_oR;
            l_oR.m_strName = l_upC->f_AttrOr("Name");
            const std::vector<double> l_vSize = f_Numbers(l_upC->f_AttrOr("Size"));
            const std::vector<double> l_vOff = f_Numbers(l_upC->f_AttrOr("OffsetPos", "0,0"));
            const std::vector<double> l_vShow = f_Numbers(l_upC->f_AttrOr("ShowPosInPI"));
            const std::vector<double> l_vUv = f_Numbers(l_upC->f_AttrOr("UV"));
            if (l_vSize.size() < 2) { l_oOut.m_vDiagnostics.push_back({Diagnostic::Warning, l_oR.m_strName, "PuzzleUnit has no Size; skipped"}); continue; }
            l_oR.m_oFrame.m_iW = f_Ri(l_vSize[0]); l_oR.m_oFrame.m_iH = f_Ri(l_vSize[1]);
            if (l_vShow.size() >= 2) { l_oR.m_oFrame.m_iX = f_Ri(l_vShow[0]); l_oR.m_oFrame.m_iY = f_Ri(l_vShow[1]); }
            else if (l_vUv.size() >= 2) { l_oR.m_oFrame.m_iX = f_Ri(l_vUv[0] * l_dW); l_oR.m_oFrame.m_iY = f_Ri(l_vUv[1] * l_dH); }
            if (l_vOff.size() >= 2) { l_oR.m_iOffsetX = f_Ri(l_vOff[0]); l_oR.m_iOffsetY = f_Ri(l_vOff[1]); }
            const std::vector<double> l_vOrig = f_Numbers(l_upC->f_AttrOr("OriginalSize"));
            if (l_vOrig.size() >= 2) { l_oR.m_iOrigW = f_Ri(l_vOrig[0]); l_oR.m_iOrigH = f_Ri(l_vOrig[1]); }
            else { l_oR.m_iOrigW = l_oR.m_iOffsetX + l_oR.m_oFrame.m_iW; l_oR.m_iOrigH = l_oR.m_iOffsetY + l_oR.m_oFrame.m_iH; }
            l_oOut.m_vRegions.push_back(l_oR);
        } else if (l_upC->m_strName == "AnimationData") {
            for (const auto& l_upA : l_upC->m_vChildren) {
                if (l_upA->m_strName != "AnimationDataUnit") continue;
                Animation l_oAn;
                l_oAn.m_strName = l_upA->f_AttrOr("Name");
                std::vector<std::string> l_vNames;
                std::string l_strCur;
                for (char l_cCh : l_upA->f_AttrOr("ImageList") + ",") {
                    if (l_cCh == ',') { if (!l_strCur.empty()) l_vNames.push_back(l_strCur); l_strCur.clear(); }
                    else l_strCur += l_cCh;
                }
                const std::vector<double> l_vTimes = f_Numbers(l_upA->f_AttrOr("TimeList"));
                for (size_t l_nI = 0; l_nI < l_vNames.size(); l_nI++)
                    l_oAn.m_vFrames.push_back({l_vNames[l_nI], l_nI < l_vTimes.size() ? (float)l_vTimes[l_nI] : 0.1f});
                l_oOut.m_vAnimations.push_back(l_oAn);
            }
        }
    }
    std::sort(l_oOut.m_vRegions.begin(), l_oOut.m_vRegions.end(), [](const Region& l_oA, const Region& l_oB) { return l_oA.m_strName < l_oB.m_strName; });
    // PuzzleUnitChild: the unit shares its parent's pixels. Its rect in the parent's original
    // pixels follows from where both originals sit in the atlas.
    for (const auto& l_upC : l_upRoot->m_vChildren) {
        if (l_upC->m_strName != "PuzzleUnitChild") continue;
        const std::string l_strName = l_upC->f_AttrOr("Name"), l_strParent = l_upC->f_AttrOr("AttachParent");
        Region* l_pR = nullptr;
        for (Region& l_oX : l_oOut.m_vRegions) if (l_oX.m_strName == l_strName) l_pR = &l_oX;
        const Region* l_pP = l_oOut.f_Find(l_strParent);
        if (!l_pR || !l_pP || l_pR == l_pP) { l_oOut.m_vDiagnostics.push_back({Diagnostic::Warning, l_strName, "PuzzleUnitChild refers to a missing unit"}); continue; }
        const int l_iLx = (l_pR->m_oFrame.m_iX - l_pR->m_iOffsetX) - (l_pP->m_oFrame.m_iX - l_pP->m_iOffsetX);
        const int l_iLy = (l_pR->m_oFrame.m_iY - l_pR->m_iOffsetY) - (l_pP->m_oFrame.m_iY - l_pP->m_iOffsetY);
        const IRect l_oLocal{l_iLx, l_iLy, l_pR->m_iOrigW, l_pR->m_iOrigH};
        if (l_iLx < 0 || l_iLy < 0 || l_oLocal.f_Right() > l_pP->m_iOrigW || l_oLocal.f_Bottom() > l_pP->m_iOrigH) {
            l_oOut.m_vDiagnostics.push_back({Diagnostic::Warning, l_strName, "child lies outside its parent '" + l_strParent + "'; imported as a normal image"});
            continue;
        }
        l_pR->m_bChild = true;
        l_pR->m_strParent = l_strParent;
        l_pR->m_oLocal = l_oLocal;
    }
    return true;
}

bool f_ImportLibgdx(const std::string& l_strPath, BuildResult& l_oOut, std::string* l_pErr) {
    l_oOut = BuildResult();
    std::string l_strText;
    if (!f_ReadText(l_strPath, l_strText, l_pErr)) return false;
    toms::AtlasFile l_oAf;
    std::string l_strPerr;
    if (!toms::f_ParseAtlas(l_strText, l_oAf, &l_strPerr)) { if (l_pErr) *l_pErr = l_strPath + ": " + l_strPerr; return false; }
    l_oOut.m_strName = f_FileStem(l_strPath);
    for (const toms::AtlasPage& l_oP : l_oAf.m_vPages) {
        Page l_oPg;
        if (!f_LoadPage(f_ParentDir(l_strPath), l_oP.m_strFile, l_oPg, l_pErr)) return false;
        l_oOut.m_vPages.push_back(std::move(l_oPg));
    }
    for (const toms::AtlasRegion& l_oA : l_oAf.m_vRegions) {
        Region l_oR;
        l_oR.m_strName = l_oA.m_strName;
        l_oR.m_iPage = l_oA.m_iPage;
        l_oR.m_oFrame = {l_oA.m_iX, l_oA.m_iY, l_oA.m_iW, l_oA.m_iH};
        l_oR.m_iOffsetX = l_oA.m_iOffX; l_oR.m_iOffsetY = l_oA.m_iOffY;
        l_oR.m_iOrigW = l_oA.m_iOrigW; l_oR.m_iOrigH = l_oA.m_iOrigH;
        l_oR.m_arrPivot[0] = l_oA.m_arrPivot[0]; l_oR.m_arrPivot[1] = l_oA.m_arrPivot[1];
        l_oR.m_bHasSplit = l_oA.m_bHasSplit;
        for (int l_iK = 0; l_iK < 4; l_iK++) l_oR.m_arrSplit[l_iK] = l_oA.m_arrSplit[l_iK];
        l_oR.m_vTags = l_oA.m_vTags;
        if (l_oA.m_bChild) { l_oR.m_bChild = true; l_oR.m_strParent = l_oA.m_strParent; l_oR.m_oLocal = {l_oA.m_arrLocal[0], l_oA.m_arrLocal[1], l_oA.m_arrLocal[2], l_oA.m_arrLocal[3]}; }
        if (l_oA.m_iIndex >= 0) l_oR.m_strName += "_" + std::to_string(l_oA.m_iIndex);
        if (l_oA.m_bRotated) {
            // Stored turned 90 degrees (libGDX: counter-clockwise). Un-rotate into a page of its own
            // so every later step sees an upright region.
            const Image& l_oSrc = l_oOut.m_vPages[l_oA.m_iPage].m_oImage;
            Image l_oUp(l_oA.m_iW, l_oA.m_iH);
            for (int l_iY = 0; l_iY < l_oA.m_iH; l_iY++)
                for (int l_iX = 0; l_iX < l_oA.m_iW; l_iX++) {
                    const int l_iSx = l_oA.m_iX + l_iY, l_iSy = l_oA.m_iY + (l_oA.m_iW - 1 - l_iX);
                    if (l_iSx >= 0 && l_iSy >= 0 && l_iSx < l_oSrc.m_iW && l_iSy < l_oSrc.m_iH) memcpy(l_oUp.f_At(l_iX, l_iY), l_oSrc.f_At(l_iSx, l_iSy), 4);
                }
            Page l_oExtra;
            l_oExtra.m_strFile = "(rotated " + l_oA.m_strName + ")";
            l_oExtra.m_oImage = std::move(l_oUp);
            l_oOut.m_vPages.push_back(std::move(l_oExtra));
            l_oR.m_iPage = (int)l_oOut.m_vPages.size() - 1;
            l_oR.m_oFrame = {0, 0, l_oA.m_iW, l_oA.m_iH};
            l_oOut.m_vDiagnostics.push_back({Diagnostic::Info, l_oR.m_strName, "rotated region was un-rotated (check its orientation)"});
        }
        l_oOut.m_vRegions.push_back(l_oR);
    }
    std::sort(l_oOut.m_vRegions.begin(), l_oOut.m_vRegions.end(), [](const Region& l_oA, const Region& l_oB) { return l_oA.m_strName < l_oB.m_strName; });
    return true;
}

bool f_ImportTpJson(const std::string& l_strPath, BuildResult& l_oOut, std::string* l_pErr) {
    l_oOut = BuildResult();
    std::string l_strText;
    if (!f_ReadText(l_strPath, l_strText, l_pErr)) return false;
    json l_jJ;
    try { l_jJ = json::parse(l_strText); } catch (const std::exception& l_oE) { if (l_pErr) *l_pErr = l_strPath + ": " + l_oE.what(); return false; }
    try {
        Page l_oPg;
        if (!f_LoadPage(f_ParentDir(l_strPath), l_jJ.at("meta").at("image").get<std::string>(), l_oPg, l_pErr)) return false;
        l_oOut.m_vPages.push_back(std::move(l_oPg));
        l_oOut.m_strName = f_FileStem(l_strPath);
        auto l_fnTake = [&](const std::string& l_strName, const json& l_jF) {
            Region l_oR;
            l_oR.m_strName = l_strName;
            const json& l_jFr = l_jF.at("frame");
            l_oR.m_oFrame = {l_jFr.at("x").get<int>(), l_jFr.at("y").get<int>(), l_jFr.at("w").get<int>(), l_jFr.at("h").get<int>()};
            if (l_jF.value("rotated", false)) l_oOut.m_vDiagnostics.push_back({Diagnostic::Warning, l_strName, "rotated frames are not supported; imported as stored"});
            if (l_jF.contains("spriteSourceSize")) { l_oR.m_iOffsetX = l_jF["spriteSourceSize"].value("x", 0); l_oR.m_iOffsetY = l_jF["spriteSourceSize"].value("y", 0); }
            l_oR.m_iOrigW = l_oR.m_oFrame.m_iW; l_oR.m_iOrigH = l_oR.m_oFrame.m_iH;
            if (l_jF.contains("sourceSize")) { l_oR.m_iOrigW = l_jF["sourceSize"].value("w", l_oR.m_iOrigW); l_oR.m_iOrigH = l_jF["sourceSize"].value("h", l_oR.m_iOrigH); }
            if (l_jF.contains("pivot")) { l_oR.m_arrPivot[0] = l_jF["pivot"].value("x", 0.5f); l_oR.m_arrPivot[1] = l_jF["pivot"].value("y", 0.5f); }
            if (l_jF.contains("x")) {
                const json& l_jX = l_jF["x"];
                if (l_jX.contains("split")) { l_oR.m_bHasSplit = true; for (int l_iK = 0; l_iK < 4; l_iK++) l_oR.m_arrSplit[l_iK] = l_jX["split"][l_iK].get<int>(); }
                if (l_jX.contains("parent")) {
                    l_oR.m_bChild = true;
                    l_oR.m_strParent = l_jX["parent"].get<std::string>();
                    const json& l_jL = l_jX.at("local");
                    l_oR.m_oLocal = {l_jL[0].get<int>(), l_jL[1].get<int>(), l_jL[2].get<int>(), l_jL[3].get<int>()};
                }
                if (l_jX.contains("tags")) l_oR.m_vTags = l_jX["tags"].get<std::vector<std::string>>();
            }
            l_oOut.m_vRegions.push_back(l_oR);
        };
        const json& l_jFrames = l_jJ.at("frames");
        if (l_jFrames.is_array()) { for (const json& l_jF : l_jFrames) l_fnTake(l_jF.at("filename").get<std::string>(), l_jF); }
        else for (auto l_oIt = l_jFrames.begin(); l_oIt != l_jFrames.end(); ++l_oIt) l_fnTake(l_oIt.key(), l_oIt.value());
        if (l_jJ.contains("animations")) {
            const json* l_pTimes = nullptr;
            if (l_jJ["meta"].contains("x") && l_jJ["meta"]["x"].contains("animations")) l_pTimes = &l_jJ["meta"]["x"]["animations"];
            for (auto l_oIt = l_jJ["animations"].begin(); l_oIt != l_jJ["animations"].end(); ++l_oIt) {
                Animation l_oA;
                l_oA.m_strName = l_oIt.key();
                const json* l_pT = l_pTimes && l_pTimes->contains(l_oA.m_strName) ? &(*l_pTimes)[l_oA.m_strName] : nullptr;
                if (l_pT) l_oA.m_bLoop = l_pT->value("loop", true);
                for (size_t l_nI = 0; l_nI < l_oIt.value().size(); l_nI++) {
                    float l_fTm = 0.1f;
                    if (l_pT && l_pT->contains("times") && l_nI < (*l_pT)["times"].size()) l_fTm = (*l_pT)["times"][l_nI].get<float>();
                    l_oA.m_vFrames.push_back({l_oIt.value()[l_nI].get<std::string>(), l_fTm});
                }
                l_oOut.m_vAnimations.push_back(l_oA);
            }
        }
    } catch (const std::exception& l_oE) {
        if (l_pErr) *l_pErr = l_strPath + ": " + l_oE.what();
        return false;
    }
    std::sort(l_oOut.m_vRegions.begin(), l_oOut.m_vRegions.end(), [](const Region& l_oA, const Region& l_oB) { return l_oA.m_strName < l_oB.m_strName; });
    return true;
}

// ---- Cocos / TexturePacker .plist (formats 0, 1, 2, 3) -------------------------------------------
// A plist dict is <key> followed by its value element. Frame names usually end in ".png" (Cocos looks sprites up
// by file name); that ending is dropped so a sprite is not called "hero.png" (and its image "hero.png.png").
// Rotated frames are stored turned 90 degrees clockwise: they are turned back onto an extra page (the project
// import cuts every sprite out of the pages, so it never sees the rotation).

namespace {

using DictItems = std::vector<std::pair<std::string, const XmlNode*>>;

DictItems f_DictItems(const XmlNode* l_pDict) {
    DictItems l_vOut;
    if (!l_pDict) return l_vOut;
    for (size_t l_nI = 0; l_nI + 1 < l_pDict->m_vChildren.size(); l_nI++)
        if (l_pDict->m_vChildren[l_nI]->m_strName == "key") {
            l_vOut.push_back({l_pDict->m_vChildren[l_nI]->m_strText, l_pDict->m_vChildren[l_nI + 1].get()});
            l_nI++;
        }
    return l_vOut;
}

const XmlNode* f_DictGet(const DictItems& l_vD, const std::string& l_strKey) {
    for (const auto& l_pairKv : l_vD) if (l_pairKv.first == l_strKey) return l_pairKv.second;
    return nullptr;
}

std::string f_PlistText(const XmlNode* l_pV) { return l_pV ? l_pV->m_strText : std::string(); }
bool f_PlistBool(const XmlNode* l_pV) { return l_pV && l_pV->m_strName == "true"; }
double f_PlistNumber(const XmlNode* l_pV, double l_dDef = 0.0) { return l_pV && !l_pV->m_strText.empty() ? strtod(l_pV->m_strText.c_str(), nullptr) : l_dDef; }

std::string f_DropImageExt(const std::string& l_strName) {
    std::string l_strLower = l_strName;
    for (char& l_cC : l_strLower) l_cC = (char)tolower((unsigned char)l_cC);
    for (const char* l_pszExt : {".png", ".jpg", ".jpeg", ".webp", ".tga", ".bmp"}) {
        const size_t l_nN = strlen(l_pszExt);
        if (l_strLower.size() > l_nN && l_strLower.compare(l_strLower.size() - l_nN, l_nN, l_pszExt) == 0) return l_strName.substr(0, l_strName.size() - l_nN);
    }
    return l_strName;
}

}  // namespace

bool f_ImportPlist(const std::string& l_strPath, BuildResult& l_oOut, std::string* l_pErr) {
    l_oOut = BuildResult();
    std::string l_strText;
    if (!f_ReadText(l_strPath, l_strText, l_pErr)) return false;
    std::string l_strPerr;
    std::unique_ptr<XmlNode> l_upRoot = f_ParseXml(l_strText, &l_strPerr);
    if (!l_upRoot || l_upRoot->m_strName != "plist" || l_upRoot->m_vChildren.empty() || l_upRoot->m_vChildren[0]->m_strName != "dict") {
        if (l_pErr) *l_pErr = l_strPath + ": not a plist" + (l_strPerr.empty() ? std::string() : " (" + l_strPerr + ")");
        return false;
    }
    const DictItems l_vTop = f_DictItems(l_upRoot->m_vChildren[0].get());
    const XmlNode* l_pFramesNode = f_DictGet(l_vTop, "frames");
    if (!l_pFramesNode || l_pFramesNode->m_strName != "dict") {
        if (l_pErr) *l_pErr = l_strPath + ": no \"frames\" dictionary (not a sprite sheet plist)";
        return false;
    }
    const DictItems l_vMeta = f_DictItems(f_DictGet(l_vTop, "metadata"));
    const int l_iFormat = (int)f_PlistNumber(f_DictGet(l_vMeta, "format"), 0);
    std::string l_strTexture = f_PlistText(f_DictGet(l_vMeta, "realTextureFileName"));
    if (l_strTexture.empty()) l_strTexture = f_PlistText(f_DictGet(l_vMeta, "textureFileName"));
    if (l_strTexture.empty()) l_strTexture = f_FileStem(l_strPath) + ".png";   // old files without metadata: the PNG next to it
    Page l_oPg;
    if (!f_LoadPage(f_ParentDir(l_strPath), l_strTexture, l_oPg, l_pErr)) return false;
    l_oOut.m_vPages.push_back(std::move(l_oPg));
    l_oOut.m_strName = f_FileStem(l_strPath);

    struct Rotated { size_t m_nRegion; IRect m_oStored; };
    std::vector<Rotated> l_vRotated;
    for (const auto& [l_strRawName, l_pValue] : f_DictItems(l_pFramesNode)) {
        if (!l_pValue || l_pValue->m_strName != "dict") continue;
        const DictItems l_vF = f_DictItems(l_pValue);
        Region l_oR;
        l_oR.m_strName = f_DropImageExt(l_strRawName);
        bool l_bRot = false;
        int l_iW = 0, l_iH = 0;
        double l_dCx = 0, l_dCy = 0;   // the trimmed centre's offset from the original centre, y up
        if (l_iFormat == 0) {
            l_oR.m_oFrame = {f_Ri(f_PlistNumber(f_DictGet(l_vF, "x"))), f_Ri(f_PlistNumber(f_DictGet(l_vF, "y"))), f_Ri(f_PlistNumber(f_DictGet(l_vF, "width"))),
                       f_Ri(f_PlistNumber(f_DictGet(l_vF, "height")))};
            l_iW = l_oR.m_oFrame.m_iW; l_iH = l_oR.m_oFrame.m_iH;
            l_dCx = f_PlistNumber(f_DictGet(l_vF, "offsetX"));
            l_dCy = f_PlistNumber(f_DictGet(l_vF, "offsetY"));
            l_oR.m_iOrigW = f_Ri(f_PlistNumber(f_DictGet(l_vF, "originalWidth"), l_iW));
            l_oR.m_iOrigH = f_Ri(f_PlistNumber(f_DictGet(l_vF, "originalHeight"), l_iH));
        } else {
            const bool l_bV3 = f_DictGet(l_vF, "textureRect") != nullptr;
            const std::vector<double> l_vRect = f_Numbers(f_PlistText(f_DictGet(l_vF, l_bV3 ? "textureRect" : "frame")));
            if (l_vRect.size() < 4) { l_oOut.m_vDiagnostics.push_back({Diagnostic::Warning, l_oR.m_strName, "no frame rectangle; skipped"}); continue; }
            l_bRot = f_PlistBool(f_DictGet(l_vF, l_bV3 ? "textureRotated" : "rotated"));
            l_iW = f_Ri(l_vRect[2]); l_iH = f_Ri(l_vRect[3]);   // the upright size; stored as h x w when rotated
            l_oR.m_oFrame = {f_Ri(l_vRect[0]), f_Ri(l_vRect[1]), l_iW, l_iH};
            const std::vector<double> l_vOff = f_Numbers(f_PlistText(f_DictGet(l_vF, l_bV3 ? "spriteOffset" : "offset")));
            if (l_vOff.size() >= 2) { l_dCx = l_vOff[0]; l_dCy = l_vOff[1]; }
            const std::vector<double> l_vSrc = f_Numbers(f_PlistText(f_DictGet(l_vF, l_bV3 ? "spriteSourceSize" : "sourceSize")));
            l_oR.m_iOrigW = l_vSrc.size() >= 2 ? f_Ri(l_vSrc[0]) : l_iW;
            l_oR.m_iOrigH = l_vSrc.size() >= 2 ? f_Ri(l_vSrc[1]) : l_iH;
        }
        // Offsets: Cocos stores the trimmed centre relative to the original centre with y up; format 2's
        // sourceColorRect has the top-left corner directly.
        l_oR.m_iOffsetX = f_Ri(l_dCx + l_oR.m_iOrigW / 2.0 - l_iW / 2.0);
        l_oR.m_iOffsetY = f_Ri(-l_dCy + l_oR.m_iOrigH / 2.0 - l_iH / 2.0);
        const std::vector<double> l_vColorRect = f_Numbers(f_PlistText(f_DictGet(l_vF, "sourceColorRect")));
        if (l_vColorRect.size() >= 2) { l_oR.m_iOffsetX = f_Ri(l_vColorRect[0]); l_oR.m_iOffsetY = f_Ri(l_vColorRect[1]); }
        if (const XmlNode* l_pA = f_DictGet(l_vF, "anchor")) {   // ours (and Cocos Creator's): x right, y up
            const std::vector<double> l_vP = f_Numbers(l_pA->m_strText);
            if (l_vP.size() >= 2) { l_oR.m_arrPivot[0] = (float)l_vP[0]; l_oR.m_arrPivot[1] = (float)(1.0 - l_vP[1]); }
        }
        if (const XmlNode* l_pS = f_DictGet(l_vF, "x_split")) {
            const std::vector<double> l_vV = f_Numbers(l_pS->m_strText);
            if (l_vV.size() >= 4) { l_oR.m_bHasSplit = true; for (int l_iK = 0; l_iK < 4; l_iK++) l_oR.m_arrSplit[l_iK] = f_Ri(l_vV[l_iK]); }
        }
        if (const XmlNode* l_pP = f_DictGet(l_vF, "x_parent")) {
            const std::vector<double> l_vL = f_Numbers(f_PlistText(f_DictGet(l_vF, "x_local")));
            if (l_vL.size() >= 4) {
                l_oR.m_bChild = true;
                l_oR.m_strParent = f_DropImageExt(l_pP->m_strText);
                l_oR.m_oLocal = {f_Ri(l_vL[0]), f_Ri(l_vL[1]), f_Ri(l_vL[2]), f_Ri(l_vL[3])};
            }
        }
        if (const XmlNode* l_pT = f_DictGet(l_vF, "x_tags"))
            for (const auto& l_upC : l_pT->m_vChildren) if (l_upC->m_strName == "string") l_oR.m_vTags.push_back(l_upC->m_strText);
        if (l_bRot) l_vRotated.push_back({l_oOut.m_vRegions.size(), IRect{l_oR.m_oFrame.m_iX, l_oR.m_oFrame.m_iY, l_iH, l_iW}});
        l_oOut.m_vRegions.push_back(l_oR);
        if (const XmlNode* l_pAliases = f_DictGet(l_vF, "aliases"))   // other names for the same pixels
            for (const auto& l_upC : l_pAliases->m_vChildren)
                if (l_upC->m_strName == "string" && !l_upC->m_strText.empty()) {
                    Region l_oA = l_oOut.m_vRegions.back();
                    l_oA.m_strName = f_DropImageExt(l_upC->m_strText);
                    l_oA.m_strAliasOf = l_oR.m_strName;
                    if (l_bRot) l_vRotated.push_back({l_oOut.m_vRegions.size(), IRect{l_oR.m_oFrame.m_iX, l_oR.m_oFrame.m_iY, l_iH, l_iW}});
                    l_oOut.m_vRegions.push_back(l_oA);
                }
    }
    // Rotated frames, turned back upright onto one extra page (a column of them).
    if (!l_vRotated.empty()) {
        int l_iPw = 0, l_iPh = 0;
        for (const Rotated& l_oRt : l_vRotated) { l_iPw = std::max(l_iPw, l_oRt.m_oStored.m_iH); l_iPh += l_oRt.m_oStored.m_iW + 1; }
        Page l_oUp;
        l_oUp.m_strFile = l_oOut.m_strName + "_unrotated.png";
        l_oUp.m_oImage = Image(l_iPw, l_iPh);
        const Image& l_oSrc = l_oOut.m_vPages[0].m_oImage;
        int l_iY = 0;
        for (const Rotated& l_oRt : l_vRotated) {
            Region& l_oR = l_oOut.m_vRegions[l_oRt.m_nRegion];
            const int l_iTexW = l_oRt.m_oStored.m_iH, l_iTexH = l_oRt.m_oStored.m_iW;   // upright size
            for (int l_iOy = 0; l_iOy < l_iTexH; l_iOy++)
                for (int l_iOx = 0; l_iOx < l_iTexW; l_iOx++) {
                    // stored clockwise: upright (ox, oy) is at stored (H - 1 - oy, ox)
                    const int l_iSx = l_oRt.m_oStored.m_iX + (l_iTexH - 1 - l_iOy), l_iSy = l_oRt.m_oStored.m_iY + l_iOx;
                    if (l_iSx < 0 || l_iSy < 0 || l_iSx >= l_oSrc.m_iW || l_iSy >= l_oSrc.m_iH) continue;
                    memcpy(l_oUp.m_oImage.f_At(l_iOx, l_iY + l_iOy), l_oSrc.f_At(l_iSx, l_iSy), 4);
                }
            l_oR.m_iPage = 1;
            l_oR.m_oFrame = {0, l_iY, l_iTexW, l_iTexH};
            l_iY += l_iTexH + 1;
        }
        l_oOut.m_vPages.push_back(std::move(l_oUp));
    }
    std::sort(l_oOut.m_vRegions.begin(), l_oOut.m_vRegions.end(), [](const Region& l_oA, const Region& l_oB) { return l_oA.m_strName < l_oB.m_strName; });
    return true;
}

bool f_ImportAtlasFile(const std::string& l_strPath, BuildResult& l_oOut, std::string* l_pErr) {
    std::string l_strExt = f_U8str(f_U8path(l_strPath).extension());
    for (char& l_cC : l_strExt) l_cC = (char)tolower((unsigned char)l_cC);
    if (l_strExt == ".pi") return f_ImportPi(l_strPath, l_oOut, l_pErr);
    if (l_strExt == ".atlas" || l_strExt == ".txt") return f_ImportLibgdx(l_strPath, l_oOut, l_pErr);
    if (l_strExt == ".json") return f_ImportTpJson(l_strPath, l_oOut, l_pErr);
    if (l_strExt == ".plist") return f_ImportPlist(l_strPath, l_oOut, l_pErr);
    if (l_pErr) *l_pErr = "unknown atlas type '" + l_strExt + "' (expected .pi, .atlas, .json or .plist)";
    return false;
}

Image f_RenderRegion(const BuildResult& l_oB, const Region& l_oR) {
    Image l_oOut(std::max(l_oR.m_iOrigW, 1), std::max(l_oR.m_iOrigH, 1));
    if (l_oR.m_iPage >= 0 && l_oR.m_iPage < (int)l_oB.m_vPages.size()) f_Blit(l_oOut, l_oB.m_vPages[l_oR.m_iPage].m_oImage, l_oR.m_oFrame, l_oR.m_iOffsetX, l_oR.m_iOffsetY);
    return l_oOut;
}

bool f_ImportToProject(const BuildResult& l_oIn, const std::string& l_strProjectPath, const std::string& l_strImageDir,
                     Project& l_oOut, std::string* l_pErr) {
    Project l_oP;
    l_oP.m_strFilePath = f_NormalizePath(l_strProjectPath);
    l_oP.m_strName = l_oIn.m_strName.empty() ? f_FileStem(l_strProjectPath) : l_oIn.m_strName;
    l_oP.m_oSettings.m_iPadding = 2;
    l_oP.m_bEmbedded = l_strImageDir.empty();
    std::error_code l_oEc;
    if (!l_oP.m_bEmbedded) fs::create_directories(f_U8path(l_strImageDir), l_oEc);
    for (const Region& l_oR : l_oIn.m_vRegions) {
        const Region* l_pParent = l_oR.m_bChild ? l_oIn.f_Find(l_oR.m_strParent) : nullptr;
        SpriteDef l_oD;
        l_oD.m_strName = l_oR.m_strName;
        if (l_oR.m_bChild && l_pParent) {
            l_oD.m_strParent = l_oR.m_strParent;
            l_oD.m_oRect = l_oR.m_oLocal;
        } else if (l_oP.m_bEmbedded) {
            l_oP.m_mapImages[l_oR.m_strName] = std::make_shared<const Image>(f_RenderRegion(l_oIn, l_oR));
        } else {
            const std::string l_strFile = f_JoinPath(l_strImageDir, l_oR.m_strName + ".png");
            fs::create_directories(f_U8path(l_strFile).parent_path(), l_oEc);
            if (!f_SavePng(l_strFile, f_RenderRegion(l_oIn, l_oR), l_pErr)) return false;
        }
        if (!(l_oR.m_arrPivot[0] == 0.5f && l_oR.m_arrPivot[1] == 0.5f)) { l_oD.m_bHasPivot = true; l_oD.m_arrPivot[0] = l_oR.m_arrPivot[0]; l_oD.m_arrPivot[1] = l_oR.m_arrPivot[1]; }
        if (l_oR.m_bHasSplit) { l_oD.m_bHasSplit = true; for (int l_iK = 0; l_iK < 4; l_iK++) l_oD.m_arrSplit[l_iK] = l_oR.m_arrSplit[l_iK]; }
        l_oD.m_vTags = l_oR.m_vTags;
        if (l_oD.f_IsChild() || l_oD.m_bHasPivot || l_oD.m_bHasSplit || !l_oD.m_vTags.empty()) l_oP.m_vSprites.push_back(l_oD);
    }
    if (!l_oP.m_bEmbedded) l_oP.m_vSources.push_back({l_oP.f_Relativize(l_strImageDir), true, ""});
    l_oP.m_vAnimations = l_oIn.m_vAnimations;
    l_oOut = std::move(l_oP);
    return true;
}

}  // namespace atlas
