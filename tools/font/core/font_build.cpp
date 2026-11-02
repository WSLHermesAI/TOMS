// font_build.cpp -- see font_build.h.
#include "font_build.h"
#include "atlas_packer.h"    // packPages: the Tight layouts
#include "atlas_project.h"   // u8path, u8str

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace fonttool {

namespace {

// Names for characters a file name cannot hold (or that are easy to confuse in one).
const std::pair<const char*, uint32_t> g_arrNames[] = {
    {"space", ' '},     {"dot", '.'},        {"period", '.'},     {"comma", ','},      {"colon", ':'},
    {"semicolon", ';'}, {"plus", '+'},       {"minus", '-'},      {"dash", '-'},       {"hyphen", '-'},
    {"percent", '%'},   {"slash", '/'},      {"backslash", '\\'}, {"star", '*'},       {"asterisk", '*'},
    {"dollar", '$'},    {"hash", '#'},       {"at", '@'},         {"question", '?'},   {"exclam", '!'},
    {"quote", '"'},     {"apostrophe", '\''}, {"lparen", '('},    {"rparen", ')'},     {"lbracket", '['},
    {"rbracket", ']'},  {"equal", '='},      {"equals", '='},     {"underscore", '_'}, {"less", '<'},
    {"greater", '>'},   {"pipe", '|'},       {"tilde", '~'},      {"amp", '&'},        {"caret", '^'},
    {"times", 0xD7},    {"yen", 0xA5},       {"euro", 0x20AC},    {"pound", 0xA3},
};

std::string f_Lower(std::string l_strS) {
    for (char& l_cC : l_strS) if (l_cC >= 'A' && l_cC <= 'Z') l_cC = (char)(l_cC + 32);
    return l_strS;
}

std::string f_Trim(const std::string& l_strS) {
    size_t l_nA = 0, l_nB = l_strS.size();
    while (l_nA < l_nB && (l_strS[l_nA] == ' ' || l_strS[l_nA] == '\t' || l_strS[l_nA] == '\r')) l_nA++;
    while (l_nB > l_nA && (l_strS[l_nB - 1] == ' ' || l_strS[l_nB - 1] == '\t' || l_strS[l_nB - 1] == '\r')) l_nB--;
    return l_strS.substr(l_nA, l_nB - l_nA);
}

// "x" -> 'x', "space" -> ' ', "U+00D7" -> 0xD7; also a bare " " (chars.txt `name= `).
uint32_t f_ParseChar(const std::string& l_strS) {
    if (l_strS.empty()) return 0;
    size_t l_nI = 0;
    const uint32_t l_uCp = f_Utf8Next(l_strS, l_nI);
    if (l_nI == l_strS.size() && l_uCp != 0xFFFD) return l_uCp;   // exactly one character
    return f_CharFromName(l_strS);
}

bool f_ReadText(const fs::path& l_oP, std::string& l_strOut) {
    std::ifstream l_oF(l_oP, std::ios::binary);
    if (!l_oF) return false;
    std::ostringstream l_oSs;
    l_oSs << l_oF.rdbuf();
    l_strOut = l_oSs.str();
    return true;
}

bool f_Transparent(const atlas::Image& l_oImg) { return atlas::f_OpaqueBounds(l_oImg).f_Empty(); }

}  // namespace

const char* f_PageLayoutName(PageLayout l_eL) {
    switch (l_eL) {
    case PageLayout::Tight: return "tight";
    case PageLayout::TightPow2: return "pot";
    default: return "row";
    }
}

bool f_ParsePageLayout(const std::string& l_strS, PageLayout& l_eOut) {
    const std::string l_strL = f_Lower(l_strS);
    if (l_strL == "row") l_eOut = PageLayout::Row;
    else if (l_strL == "tight") l_eOut = PageLayout::Tight;
    else if (l_strL == "pot" || l_strL == "pow2" || l_strL == "tight-pot") l_eOut = PageLayout::TightPow2;
    else return false;
    return true;
}

std::string f_Utf8Encode(uint32_t l_uCp) {
    std::string l_strS;
    if (l_uCp < 0x80) l_strS += (char)l_uCp;
    else if (l_uCp < 0x800) { l_strS += (char)(0xC0 | (l_uCp >> 6)); l_strS += (char)(0x80 | (l_uCp & 0x3F)); }
    else if (l_uCp < 0x10000) {
        l_strS += (char)(0xE0 | (l_uCp >> 12)); l_strS += (char)(0x80 | ((l_uCp >> 6) & 0x3F)); l_strS += (char)(0x80 | (l_uCp & 0x3F));
    } else {
        l_strS += (char)(0xF0 | (l_uCp >> 18)); l_strS += (char)(0x80 | ((l_uCp >> 12) & 0x3F));
        l_strS += (char)(0x80 | ((l_uCp >> 6) & 0x3F)); l_strS += (char)(0x80 | (l_uCp & 0x3F));
    }
    return l_strS;
}

uint32_t f_CharFromName(const std::string& l_strStem) {
    if (l_strStem.empty()) return 0;
    size_t l_nI = 0;
    const uint32_t l_uCp = f_Utf8Next(l_strStem, l_nI);
    if (l_nI == l_strStem.size() && l_uCp != 0xFFFD) return l_uCp;
    const std::string l_strL = f_Lower(l_strStem);
    for (const auto& l_pairN : g_arrNames)
        if (l_strL == l_pairN.first) return l_pairN.second;
    if (l_strL.size() > 2 && l_strL[0] == 'u' && l_strL[1] == '+') {
        char* l_pszEnd = nullptr;
        const unsigned long l_uV = std::strtoul(l_strL.c_str() + 2, &l_pszEnd, 16);
        if (*l_pszEnd == 0 && l_uV > 0 && l_uV <= 0x10FFFF) return (uint32_t)l_uV;
    }
    return 0;
}

std::string f_GlyphFileName(uint32_t l_uCp) {
    for (const auto& l_pairN : g_arrNames)
        if (l_pairN.second == l_uCp) return l_pairN.first;
    if ((l_uCp >= '0' && l_uCp <= '9') || (l_uCp >= 'a' && l_uCp <= 'z') || (l_uCp >= 'A' && l_uCp <= 'Z') || l_uCp >= 0xA0)
        return f_Utf8Encode(l_uCp);
    char l_pszBuf[16];
    std::snprintf(l_pszBuf, sizeof l_pszBuf, "U+%04X", (unsigned)l_uCp);
    return l_pszBuf;
}

bool f_LoadPicture(const std::string& l_strPath, GlyphPicture& l_oOut, std::string& l_strErr) {
    const fs::path l_oP = atlas::f_U8path(l_strPath);
    l_oOut = GlyphPicture();
    l_oOut.m_strFile = atlas::f_U8str(l_oP.filename());
    l_oOut.m_uCp = f_CharFromName(atlas::f_U8str(l_oP.stem()));
    return atlas::f_LoadImage(l_strPath, l_oOut.m_oImg, &l_strErr);
}

bool f_GlyphsFromFont(const std::string& l_strFntPath, std::vector<GlyphPicture>& l_vOut, std::string& l_strErr) {
    l_vOut.clear();
    BitmapFont l_oFont;
    atlas::Image l_oPage;
    if (!f_LoadFont(l_strFntPath, l_oFont, l_oPage, l_strErr)) return false;
    const std::string l_strName = atlas::f_U8str(atlas::f_U8path(l_strFntPath).filename());
    // a blank glyph (a space) becomes a transparent picture as tall as the tallest one, so it changes nothing
    // (lineHeight may be set larger than the pictures)
    int l_iTallest = 1;
    for (const BitmapGlyph& l_oG : l_oFont.m_vGlyphs) l_iTallest = std::max(l_iTallest, l_oG.m_iH);
    for (const BitmapGlyph& l_oG : l_oFont.m_vGlyphs) {
        GlyphPicture l_oP;
        l_oP.m_uCp = l_oG.m_uId;
        l_oP.m_strFile = l_strName + " '" + (l_oG.m_uId == ' ' ? std::string("space") : f_Utf8Encode(l_oG.m_uId)) + "'";
        if (l_oG.m_iW > 0 && l_oG.m_iH > 0) l_oP.m_oImg = atlas::f_Crop(l_oPage, {l_oG.m_iX, l_oG.m_iY, l_oG.m_iW, l_oG.m_iH});
        else l_oP.m_oImg = atlas::Image(std::max(1, l_oG.m_iXadvance), l_iTallest);
        l_vOut.push_back(std::move(l_oP));
    }
    return true;
}

int f_DefaultSpaceAdvance(const std::vector<GlyphPicture>& l_vGlyphs) {
    for (const GlyphPicture& l_oG : l_vGlyphs)
        if (l_oG.m_uCp == ' ' && l_oG.m_oImg.f_Valid()) return l_oG.m_oImg.m_iW;
    for (const GlyphPicture& l_oG : l_vGlyphs)
        if (l_oG.m_uCp == '1' && l_oG.m_oImg.f_Valid()) return l_oG.m_oImg.m_iW;
    return 5;
}

int f_DefaultLineHeight(const std::vector<GlyphPicture>& l_vGlyphs) {
    int l_iH = 0;
    for (const GlyphPicture& l_oG : l_vGlyphs)
        if (l_oG.m_oImg.f_Valid()) l_iH = std::max(l_iH, l_oG.m_oImg.m_iH);
    return l_iH;
}

std::vector<std::string> f_CheckGlyphs(const std::vector<GlyphPicture>& l_vGlyphs) {
    std::vector<std::string> l_vProblems;
    std::map<uint32_t, std::string> l_mapSeen;
    for (const GlyphPicture& l_oG : l_vGlyphs) {
        if (!l_oG.m_uCp) {
            l_vProblems.push_back(l_oG.m_strFile + ": no character given");
            continue;
        }
        auto l_itIt = l_mapSeen.find(l_oG.m_uCp);
        if (l_itIt != l_mapSeen.end())
            l_vProblems.push_back(l_oG.m_strFile + ": '" + f_Utf8Encode(l_oG.m_uCp) + "' is already given to " + l_itIt->second);
        else
            l_mapSeen[l_oG.m_uCp] = l_oG.m_strFile;
    }
    return l_vProblems;
}

bool f_CollectGlyphs(const std::string& l_strFolder, std::vector<GlyphPicture>& l_vOut, std::vector<std::string>& l_vWarnings,
                   std::string& l_strErr) {
    l_vOut.clear();
    const fs::path l_oDir = atlas::f_U8path(l_strFolder);
    std::error_code l_oEc;
    if (!fs::is_directory(l_oDir, l_oEc)) {
        l_strErr = "not a folder: " + l_strFolder;
        return false;
    }
    // chars.txt: file stem -> character, in the order the glyphs go on the page
    std::map<std::string, uint32_t> l_mapNamed;   // lower-case stem -> cp
    std::map<std::string, int> l_mapOrder;        // lower-case stem -> position
    std::string l_strText;
    if (f_ReadText(l_oDir / "chars.txt", l_strText)) {
        std::istringstream l_oIn(l_strText);
        std::string l_strLine;
        int l_iLineNo = 0;
        while (std::getline(l_oIn, l_strLine)) {
            l_iLineNo++;
            if (!l_strLine.empty() && l_strLine.back() == '\r') l_strLine.pop_back();
            if (f_Trim(l_strLine).empty() || f_Trim(l_strLine)[0] == '#') continue;
            const size_t l_nEq = l_strLine.find('=');
            const std::string l_strStem = l_nEq == std::string::npos ? std::string() : f_Lower(f_Trim(l_strLine.substr(0, l_nEq)));
            // the value is not trimmed when it is one space ("space= " would be odd, but allowed)
            std::string l_strValue = l_nEq == std::string::npos ? std::string() : l_strLine.substr(l_nEq + 1);
            if (l_strValue != " ") l_strValue = f_Trim(l_strValue);
            const uint32_t l_uCp = f_ParseChar(l_strValue);
            if (l_strStem.empty() || !l_uCp) {
                l_vWarnings.push_back("chars.txt line " + std::to_string(l_iLineNo) + ": expected file=char, got '" + l_strLine + "'");
                continue;
            }
            l_mapNamed[l_strStem] = l_uCp;
            l_mapOrder.emplace(l_strStem, (int)l_mapOrder.size());
        }
    }

    struct Entry { GlyphPicture m_oG; int m_iOrder; };
    std::vector<Entry> l_vEntries;
    std::map<uint32_t, std::string> l_mapSeen;
    for (const auto& l_oDe : fs::directory_iterator(l_oDir, l_oEc)) {
        if (!l_oDe.is_regular_file()) continue;
        const fs::path l_oP = l_oDe.path();
        if (f_Lower(atlas::f_U8str(l_oP.extension())) != ".png") continue;
        const std::string l_strStem = atlas::f_U8str(l_oP.stem());
        const std::string l_strFile = atlas::f_U8str(l_oP.filename());
        auto l_itIt = l_mapNamed.find(f_Lower(l_strStem));
        const uint32_t l_uCp = l_itIt != l_mapNamed.end() ? l_itIt->second : f_CharFromName(l_strStem);
        if (!l_uCp) {
            l_vWarnings.push_back(l_strFile + ": the name is not a character (rename it, or map it in chars.txt)");
            continue;
        }
        if (l_mapSeen.count(l_uCp)) {
            l_vWarnings.push_back(l_strFile + ": '" + f_Utf8Encode(l_uCp) + "' already comes from " + l_mapSeen[l_uCp] + "; skipped");
            continue;
        }
        Entry l_oE;
        l_oE.m_oG.m_uCp = l_uCp;
        l_oE.m_oG.m_strFile = l_strFile;
        std::string l_strLoadErr;
        if (!atlas::f_LoadImage(atlas::f_U8str(l_oP), l_oE.m_oG.m_oImg, &l_strLoadErr)) {
            l_vWarnings.push_back(l_strFile + ": " + l_strLoadErr);
            continue;
        }
        auto l_itO = l_mapOrder.find(f_Lower(l_strStem));
        l_oE.m_iOrder = l_itO != l_mapOrder.end() ? l_itO->second : 1 << 30;
        l_mapSeen[l_uCp] = l_strFile;
        l_vEntries.push_back(std::move(l_oE));
    }
    // chars.txt order first, then by code point
    std::sort(l_vEntries.begin(), l_vEntries.end(), [](const Entry& l_oA, const Entry& l_oB) {
        return l_oA.m_iOrder != l_oB.m_iOrder ? l_oA.m_iOrder < l_oB.m_iOrder : l_oA.m_oG.m_uCp < l_oB.m_oG.m_uCp;
    });
    for (auto& l_oE : l_vEntries) l_vOut.push_back(std::move(l_oE.m_oG));
    if (l_vOut.empty()) {
        l_strErr = "no character pictures in " + l_strFolder;
        return false;
    }
    return true;
}

bool f_BuildFont(const std::vector<GlyphPicture>& l_vGlyphs, const BuildOptions& l_oOpt, const std::string& l_strPageFile,
               BuiltFont& l_oOut, std::string& l_strErr) {
    l_oOut = BuiltFont();
    if (l_vGlyphs.empty()) {
        l_strErr = "no glyphs";
        return false;
    }
    const std::vector<std::string> l_vProblems = f_CheckGlyphs(l_vGlyphs);
    if (!l_vProblems.empty()) {
        l_strErr = l_vProblems[0] + (l_vProblems.size() > 1 ? " (+" + std::to_string(l_vProblems.size() - 1) + " more)" : "");
        return false;
    }
    struct Slot { uint32_t m_uCp; int m_iX, m_iY, m_iW, m_iH, m_iYoffset, m_iXadvance; const atlas::Image* m_pImg; };
    std::vector<Slot> l_vSlots;
    bool l_bHaveSpace = false;
    for (const GlyphPicture& l_oG : l_vGlyphs) {
        if (!l_oG.m_oImg.f_Valid()) {
            l_strErr = l_oG.m_strFile + ": empty picture";
            return false;
        }
        l_bHaveSpace |= l_oG.m_uCp == ' ';
    }
    // glyphs sit on the bottom of the tallest picture; the next line starts lineHeight lower
    const int l_iBase = f_DefaultLineHeight(l_vGlyphs);
    const int l_iLineHeight = l_oOpt.m_iLineHeight > 0 ? l_oOpt.m_iLineHeight : l_iBase;
    const int l_iSpaceAdvance = l_oOpt.m_iSpaceAdvance >= 0 ? l_oOpt.m_iSpaceAdvance : f_DefaultSpaceAdvance(l_vGlyphs);
    for (const GlyphPicture& l_oG : l_vGlyphs) {
        Slot l_oS{l_oG.m_uCp, 0, 0, 0, 0, 0, l_oG.m_oImg.m_iW + l_oOpt.m_iAdvancePad, &l_oG.m_oImg};
        if (l_oG.m_uCp == ' ') l_oS.m_iXadvance = l_iSpaceAdvance + l_oOpt.m_iAdvancePad;
        if (!f_Transparent(l_oG.m_oImg)) {   // a blank picture (a space) only gives the advance
            l_oS.m_iW = l_oG.m_oImg.m_iW;
            l_oS.m_iH = l_oG.m_oImg.m_iH;
            l_oS.m_iYoffset = l_iBase - l_oG.m_oImg.m_iH;   // bottom aligned
        } else {
            l_oS.m_pImg = nullptr;
        }
        l_vSlots.push_back(l_oS);
    }
    if (!l_bHaveSpace) l_vSlots.push_back({' ', 0, 0, 0, 0, 0, l_iSpaceAdvance + l_oOpt.m_iAdvancePad, nullptr});

    int l_iPageW = 0, l_iPageH = 0;
    if (l_oOpt.m_eLayout == PageLayout::Row) {
        // a row of pictures 1 px apart from (1, 1), wrapping at maxWidth
        int l_iX = 1, l_iY = 1, l_iRowH = 0;
        for (Slot& l_oS : l_vSlots) {
            if (!l_oS.m_pImg) continue;
            if (l_iX > 1 && l_iX + l_oS.m_iW > l_oOpt.m_iMaxWidth) { l_iX = 1; l_iY += l_iRowH + 1; l_iRowH = 0; }
            l_oS.m_iX = l_iX;
            l_oS.m_iY = l_iY;
            l_iX += l_oS.m_iW + 1;
            l_iRowH = std::max(l_iRowH, l_oS.m_iH);
            l_iPageW = std::max(l_iPageW, l_oS.m_iX + l_oS.m_iW);
            l_iPageH = std::max(l_iPageH, l_oS.m_iY + l_oS.m_iH);
        }
    } else {
        // the atlas tool's packer: the smallest page (power of two sides if asked), 1 px border and gaps
        std::vector<atlas::PackItem> l_vItems;
        std::vector<Slot*> l_vPacked;
        for (Slot& l_oS : l_vSlots)
            if (l_oS.m_pImg) {
                atlas::PackItem l_oIt;
                l_oIt.m_iW = l_oS.m_iW;
                l_oIt.m_iH = l_oS.m_iH;
                l_vItems.push_back(l_oIt);
                l_vPacked.push_back(&l_oS);
            }
        atlas::PackSettings l_oPs;
        l_oPs.m_iMaxWidth = l_oPs.m_iMaxHeight = l_oOpt.m_iMaxWidth;
        l_oPs.m_bPowerOfTwo = l_oOpt.m_eLayout == PageLayout::TightPow2;
        l_oPs.m_iBorder = 1;
        l_oPs.m_iPadding = 1;
        if (!l_vItems.empty()) {
            const atlas::PackResult l_oPr = atlas::f_PackPages(l_vItems, l_oPs);
            if (!l_oPr.m_vErrors.empty() || l_oPr.m_vPageSizes.size() != 1) {
                l_strErr = "the pictures do not fit on one " + std::to_string(l_oOpt.m_iMaxWidth) + "x" +
                      std::to_string(l_oOpt.m_iMaxWidth) + " page" + (l_oPr.m_vErrors.empty() ? std::string() : ": " + l_oPr.m_vErrors[0]);
                return false;
            }
            for (size_t l_nI = 0; l_nI < l_vPacked.size(); l_nI++) {
                l_vPacked[l_nI]->m_iX = l_oPr.m_vPlacements[l_nI].m_iX;
                l_vPacked[l_nI]->m_iY = l_oPr.m_vPlacements[l_nI].m_iY;
            }
            l_iPageW = l_oPr.m_vPageSizes[0].m_iW;
            l_iPageH = l_oPr.m_vPageSizes[0].m_iH;
        }
    }
    if (l_iPageW <= 0 || l_iPageH <= 0) {
        l_strErr = "every picture is transparent";
        return false;
    }

    l_oOut.m_oPage = atlas::Image(l_iPageW, l_iPageH);
    for (const Slot& l_oS : l_vSlots)
        if (l_oS.m_pImg) atlas::f_Blit(l_oOut.m_oPage, *l_oS.m_pImg, {0, 0, l_oS.m_iW, l_oS.m_iH}, l_oS.m_iX, l_oS.m_iY);

    // The convert2fnt-web layout (tests/font/connect_score.fnt): LF, no final newline.
    std::ostringstream l_oF;
    l_oF << "info face=\"" << l_oOpt.m_strFace << "\" size=" << l_iBase
      << " bold=0 italic=0 charset=\"\" unicode=1 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=2,2 outline=0\n";
    l_oF << "common lineHeight=" << l_iLineHeight << " base=" << l_iBase << " scaleW=" << l_iPageW << " scaleH=" << l_iPageH
      << " pages=1 packed=0 alphaChnl=0 redChnl=0 greenChnl=0 blueChnl=0\n";
    l_oF << "page id=0 file=\"" << l_strPageFile << "\"\n";
    l_oF << "chars count=" << l_vSlots.size() << "\n";
    for (const Slot& l_oS : l_vSlots)
        l_oF << "char id=" << l_oS.m_uCp << " x=" << l_oS.m_iX << " y=" << l_oS.m_iY << " width=" << l_oS.m_iW << " height=" << l_oS.m_iH
          << " xoffset=0 yoffset=" << l_oS.m_iYoffset << " xadvance=" << l_oS.m_iXadvance << " page=0 chnl=15 letter=\""
          << (l_oS.m_uCp == '"' ? std::string("quote") : f_Utf8Encode(l_oS.m_uCp)) << "\"\n";
    l_oF << "kernings count=0";
    l_oOut.m_strFnt = l_oF.str();
    return l_oOut.m_oFont.f_Parse(l_oOut.m_strFnt, &l_strErr);
}

bool f_WriteFont(const BuiltFont& l_oFont, const std::string& l_strFntPath, std::string& l_strErr) {
    const fs::path l_oP = atlas::f_U8path(l_strFntPath);
    std::error_code l_oEc;
    if (l_oP.has_parent_path()) fs::create_directories(l_oP.parent_path(), l_oEc);
    if (l_oFont.m_oFont.m_vPages.empty()) {
        l_strErr = "the font has no page";
        return false;
    }
    const fs::path l_oPng = l_oP.parent_path() / atlas::f_U8path(l_oFont.m_oFont.m_vPages[0]);
    if (!atlas::f_SavePng(atlas::f_U8str(l_oPng), l_oFont.m_oPage, &l_strErr)) return false;
    std::ofstream l_oO(l_oP, std::ios::binary);
    l_oO << l_oFont.m_strFnt;
    if (!l_oO) {
        l_strErr = "cannot write " + l_strFntPath;
        return false;
    }
    return true;
}

bool f_LoadFont(const std::string& l_strFntPath, BitmapFont& l_oFont, atlas::Image& l_oPage, std::string& l_strErr) {
    const fs::path l_oP = atlas::f_U8path(l_strFntPath);
    std::string l_strText;
    if (!f_ReadText(l_oP, l_strText)) {
        l_strErr = "cannot read " + l_strFntPath;
        return false;
    }
    if (!l_oFont.f_Parse(l_strText, &l_strErr)) {
        l_strErr = l_strFntPath + ": " + l_strErr;
        return false;
    }
    if (l_oFont.m_vPages.empty() || l_oFont.m_vPages[0].empty()) {
        l_strErr = l_strFntPath + ": no page file";
        return false;
    }
    if (l_oFont.m_vPages.size() > 1) {
        l_strErr = l_strFntPath + ": " + std::to_string(l_oFont.m_vPages.size()) + " pages; only one page fonts are supported";
        return false;
    }
    const std::string l_strPng = atlas::f_U8str(l_oP.parent_path() / atlas::f_U8path(l_oFont.m_vPages[0]));
    if (!atlas::f_LoadImage(l_strPng, l_oPage, &l_strErr)) return false;
    if (l_oFont.m_iScaleW <= 0 || l_oFont.m_iScaleH <= 0) { l_oFont.m_iScaleW = l_oPage.m_iW; l_oFont.m_iScaleH = l_oPage.m_iH; }
    return true;
}

bool f_SplitFont(const std::string& l_strFntPath, const std::string& l_strOutDir, int& l_iWritten, std::string& l_strErr) {
    l_iWritten = 0;
    std::vector<GlyphPicture> l_vGlyphs;
    if (!f_GlyphsFromFont(l_strFntPath, l_vGlyphs, l_strErr)) return false;
    const fs::path l_oDir = atlas::f_U8path(l_strOutDir);
    std::error_code l_oEc;
    fs::create_directories(l_oDir, l_oEc);
    // file names that differ only in case ("K" and "k") are one file on Windows: the second gets U+XXXX
    std::map<std::string, int> l_mapUsed;
    std::string l_strChars = "# made by fontpack split from " + atlas::f_U8str(atlas::f_U8path(l_strFntPath).filename()) +
                        ": file=character, in page order\n";
    for (const GlyphPicture& l_oG : l_vGlyphs) {
        std::string l_strStem = f_GlyphFileName(l_oG.m_uCp);
        if (l_mapUsed.count(f_Lower(l_strStem))) {
            char l_pszBuf[16];
            std::snprintf(l_pszBuf, sizeof l_pszBuf, "U+%04X", (unsigned)l_oG.m_uCp);
            l_strStem = l_pszBuf;
        }
        l_mapUsed[f_Lower(l_strStem)] = 1;
        if (!atlas::f_SavePng(atlas::f_U8str(l_oDir / atlas::f_U8path(l_strStem + ".png")), l_oG.m_oImg, &l_strErr)) return false;
        l_strChars += l_strStem + "=" + (l_oG.m_uCp == ' ' ? std::string("space") : f_Utf8Encode(l_oG.m_uCp)) + "\n";
        l_iWritten++;
    }
    std::ofstream l_oO(l_oDir / "chars.txt", std::ios::binary);
    l_oO << l_strChars;
    return (bool)l_oO;
}

atlas::Image f_RenderText(const BitmapFont& l_oFont, const atlas::Image& l_oPage, const std::string& l_strUtf8, float l_fScale,
                        int l_iPad, const uint8_t l_arrBackground[4], int* l_pMissing) {
    float l_fTw = 0, l_fTh = 0;
    l_oFont.f_Measure(l_strUtf8, l_fScale, l_fTw, l_fTh);
    const std::vector<BitmapFont::Placed> l_vPlaced = l_oFont.f_Layout(l_strUtf8, (float)l_iPad, (float)l_iPad, l_fScale,
                                                               BitmapFont::Align::Left, l_pMissing);
    // a glyph may reach past its advance (xoffset, a wide picture): grow to hold every picture
    float l_fRight = (float)l_iPad + l_fTw, l_fBottom = (float)l_iPad + l_fTh;
    for (const auto& l_oP : l_vPlaced) { l_fRight = std::max(l_fRight, l_oP.m_fX + l_oP.m_fW); l_fBottom = std::max(l_fBottom, l_oP.m_fY + l_oP.m_fH); }
    atlas::Image l_oImg(std::max(1, (int)std::ceil(l_fRight) + l_iPad), std::max(1, (int)std::ceil(l_fBottom) + l_iPad));
    if (l_arrBackground)
        for (size_t l_nI = 0; l_nI < l_oImg.m_vPx.size(); l_nI += 4)
            for (int l_iC = 0; l_iC < 4; l_iC++) l_oImg.m_vPx[l_nI + l_iC] = l_arrBackground[l_iC];
    // nearest sampling (the game point samples its sprite textures), "over" blending
    for (const auto& l_oP : l_vPlaced) {
        const BitmapGlyph& l_oG = *l_oP.m_pGlyph;
        const int l_iX0 = (int)std::floor(l_oP.m_fX), l_iY0 = (int)std::floor(l_oP.m_fY);
        const int l_iX1 = (int)std::floor(l_oP.m_fX + l_oP.m_fW), l_iY1 = (int)std::floor(l_oP.m_fY + l_oP.m_fH);
        for (int l_iY = std::max(0, l_iY0); l_iY < std::min(l_oImg.m_iH, l_iY1); l_iY++)
            for (int l_iX = std::max(0, l_iX0); l_iX < std::min(l_oImg.m_iW, l_iX1); l_iX++) {
                const int l_iSx = l_oG.m_iX + std::min(l_oG.m_iW - 1, (int)((l_iX - l_oP.m_fX + 0.5f) / l_fScale));
                const int l_iSy = l_oG.m_iY + std::min(l_oG.m_iH - 1, (int)((l_iY - l_oP.m_fY + 0.5f) / l_fScale));
                if (l_iSx < 0 || l_iSy < 0 || l_iSx >= l_oPage.m_iW || l_iSy >= l_oPage.m_iH) continue;
                const uint8_t* l_pS = l_oPage.f_At(l_iSx, l_iSy);
                uint8_t* l_pD = l_oImg.f_At(l_iX, l_iY);
                const int l_iSa = l_pS[3];
                if (l_iSa == 0) continue;
                const int l_iDa = l_pD[3];
                const int l_iOa = l_iSa + l_iDa * (255 - l_iSa) / 255;
                for (int l_iC = 0; l_iC < 3; l_iC++)
                    l_pD[l_iC] = (uint8_t)((l_pS[l_iC] * l_iSa + l_pD[l_iC] * l_iDa * (255 - l_iSa) / 255) / std::max(1, l_iOa));
                l_pD[3] = (uint8_t)l_iOa;
            }
    }
    return l_oImg;
}

}  // namespace fonttool
