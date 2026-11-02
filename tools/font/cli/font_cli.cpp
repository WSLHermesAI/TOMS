// font_cli.cpp -- the font tool's command line: fontpack, and `font_editor --headless` (docs/engine/09_FONT_TOOL.md).
//
//   fontpack build   <picture.png | folder>... --out NAME.fnt [--chars C] [--layout row|tight|pot] [--face F]
//                    [--max-width N] [--space N] [--line-height N] [--advance-pad N]
//   fontpack split   <font.fnt> --out <folder>          every glyph as a single picture (+ chars.txt)
//   fontpack preview <font.fnt> --text "1,234.5K" --out preview.png [--scale S] [--pad N] [--bg RRGGBBAA]
//   fontpack info    <font.fnt>
//
// Exit codes: 0 ok, 1 warnings (skipped files, characters the font lacks), 2 errors, 3 bad command line.
#include "font_cli.h"
#include "font_build.h"
#include "atlas_project.h"   // u8path, u8str

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace fonttool;

namespace {

const char* g_pszUsage =
    "fontpack -- picture fonts (BMFont .fnt + .png) from one picture per character (TOMS font tool)\n"
    "\n"
    "  fontpack build   <picture.png>... --out NAME.fnt [--chars C] [--layout L] [--face F] [--max-width N] [--space N]\n"
    "                   [--line-height N] [--advance-pad N]\n"
    "                   one picture per character, on the page in the order given; the character is the file\n"
    "                   name (0.png K.png dot.png comma.png U+00D7.png), or --chars gives them in order:\n"
    "                   fontpack build a.png b.png c.png --chars \"0.,\" --out score.fnt\n"
    "                   a folder stands for all its PNGs (and its chars.txt: file=char lines)\n"
    "                   --layout row (default: one row, as convert2fnt-web) | tight (the atlas packer: the\n"
    "                   smallest page) | pot (tight, the page's sides powers of two)\n"
    "                   --space: the space's width (default: a space picture's, else the 1 picture's, else 5)\n"
    "                   --line-height: the step to the next line (default: the tallest picture)\n"
    "  fontpack split   <font.fnt> --out <folder>     the reverse: every glyph as a single picture + chars.txt\n"
    "  fontpack preview <font.fnt> --text TEXT --out preview.png [--scale S] [--pad N] [--bg RRGGBBAA]\n"
    "  fontpack info    <font.fnt>\n"
    "\n"
    "exit codes: 0 ok, 1 warnings, 2 errors, 3 bad command line\n";

struct Args {
    std::vector<std::string> m_vPos;
    std::vector<std::pair<std::string, std::string>> m_vOpts;
    bool f_Has(const std::string& l_strK) const {
        for (const auto& l_pairO : m_vOpts) if (l_pairO.first == l_strK) return true;
        return false;
    }
    std::string f_Get(const std::string& l_strK, const std::string& l_strDef = std::string()) const {
        for (const auto& l_pairO : m_vOpts) if (l_pairO.first == l_strK) return l_pairO.second;
        return l_strDef;
    }
};

bool f_ParseArgs(const std::vector<std::string>& l_vArgv, Args& l_oA, std::string& l_strErr) {
    for (size_t l_nI = 1; l_nI < l_vArgv.size(); l_nI++) {
        std::string l_strS = l_vArgv[l_nI];
        if (l_strS.rfind("--", 0) == 0) {
            std::string l_strVal;
            const size_t l_nEq = l_strS.find('=');
            if (l_nEq != std::string::npos) { l_strVal = l_strS.substr(l_nEq + 1); l_strS = l_strS.substr(0, l_nEq); }
            else if (l_strS != "--help") {
                if (l_nI + 1 >= l_vArgv.size()) { l_strErr = l_strS + " needs a value"; return false; }
                l_strVal = l_vArgv[++l_nI];
            }
            l_oA.m_vOpts.emplace_back(l_strS, l_strVal);
        } else {
            l_oA.m_vPos.push_back(l_strS);
        }
    }
    return true;
}

bool f_ToInt(const std::string& l_strS, int& l_iOut) {
    char* l_pszEnd = nullptr;
    const long l_iV = std::strtol(l_strS.c_str(), &l_pszEnd, 10);
    if (l_strS.empty() || *l_pszEnd) return false;
    l_iOut = (int)l_iV;
    return true;
}

int f_CmdBuild(const Args& l_oA) {
    if (l_oA.m_vPos.empty() || !l_oA.f_Has("--out")) { std::fputs(g_pszUsage, stderr); return 3; }
    BuildOptions l_oOpt;
    const std::string l_strOut = l_oA.f_Get("--out");
    l_oOpt.m_strFace = l_oA.f_Get("--face", atlas::f_U8str(atlas::f_U8path(l_strOut).stem()));
    if ((l_oA.f_Has("--max-width") && !f_ToInt(l_oA.f_Get("--max-width"), l_oOpt.m_iMaxWidth)) ||
        (l_oA.f_Has("--space") && !f_ToInt(l_oA.f_Get("--space"), l_oOpt.m_iSpaceAdvance)) ||
        (l_oA.f_Has("--line-height") && !f_ToInt(l_oA.f_Get("--line-height"), l_oOpt.m_iLineHeight)) ||
        (l_oA.f_Has("--advance-pad") && !f_ToInt(l_oA.f_Get("--advance-pad"), l_oOpt.m_iAdvancePad))) {
        std::fprintf(stderr, "fontpack: --max-width, --space, --line-height and --advance-pad take a whole number\n");
        return 3;
    }
    if (l_oA.f_Has("--layout") && !f_ParsePageLayout(l_oA.f_Get("--layout"), l_oOpt.m_eLayout)) {
        std::fprintf(stderr, "fontpack: --layout is row, tight or pot\n");
        return 3;
    }
    // the pictures, in the order given: PNG files, or folders (all their PNGs, chars.txt order)
    std::vector<GlyphPicture> l_vGlyphs;
    std::vector<std::string> l_vWarnings;
    std::string l_strErr;
    for (const std::string& l_strIn : l_oA.m_vPos) {
        std::error_code l_oEc;
        if (std::filesystem::is_directory(atlas::f_U8path(l_strIn), l_oEc)) {
            std::vector<GlyphPicture> l_vMore;
            if (!f_CollectGlyphs(l_strIn, l_vMore, l_vWarnings, l_strErr)) {
                std::fprintf(stderr, "error: %s\n", l_strErr.c_str());
                return 2;
            }
            for (auto& l_oG : l_vMore) l_vGlyphs.push_back(std::move(l_oG));
        } else {
            GlyphPicture l_oG;
            if (!f_LoadPicture(l_strIn, l_oG, l_strErr)) {
                std::fprintf(stderr, "error: %s: %s\n", l_strIn.c_str(), l_strErr.c_str());
                return 2;
            }
            l_vGlyphs.push_back(std::move(l_oG));
        }
    }
    // --chars "0123456789.,KM": the characters of the pictures, in order (instead of their file names)
    if (l_oA.f_Has("--chars")) {
        const std::string l_strChars = l_oA.f_Get("--chars");
        std::vector<uint32_t> l_vCps;
        for (size_t l_nI = 0; l_nI < l_strChars.size();) l_vCps.push_back(f_Utf8Next(l_strChars, l_nI));
        if (l_vCps.size() != l_vGlyphs.size()) {
            std::fprintf(stderr, "fontpack: --chars has %d characters for %d pictures\n", (int)l_vCps.size(),
                         (int)l_vGlyphs.size());
            return 3;
        }
        for (size_t l_nI = 0; l_nI < l_vCps.size(); l_nI++) l_vGlyphs[l_nI].m_uCp = l_vCps[l_nI];
    }
    for (const auto& l_strW : l_vWarnings) std::fprintf(stderr, "warning: %s\n", l_strW.c_str());
    const std::vector<std::string> l_vProblems = f_CheckGlyphs(l_vGlyphs);
    if (!l_vProblems.empty()) {
        for (const auto& l_strP : l_vProblems) std::fprintf(stderr, "error: %s\n", l_strP.c_str());
        std::fprintf(stderr, "fontpack: name each picture after its character, or give --chars\n");
        return 2;
    }
    BuiltFont l_oFont;
    const std::string l_strPng = atlas::f_U8str(atlas::f_U8path(l_strOut).stem()) + ".png";
    if (!f_BuildFont(l_vGlyphs, l_oOpt, l_strPng, l_oFont, l_strErr) || !f_WriteFont(l_oFont, l_strOut, l_strErr)) {
        std::fprintf(stderr, "error: %s\n", l_strErr.c_str());
        return 2;
    }
    std::string l_strChars;
    for (const auto& l_oG : l_oFont.m_oFont.m_vGlyphs) l_strChars += f_Utf8Encode(l_oG.m_uId);
    std::printf("%s: %d glyphs \"%s\", page %dx%d, line height %d\n", l_strOut.c_str(), (int)l_oFont.m_oFont.m_vGlyphs.size(),
                l_strChars.c_str(), l_oFont.m_oPage.m_iW, l_oFont.m_oPage.m_iH, l_oFont.m_oFont.m_iLineHeight);
    return l_vWarnings.empty() ? 0 : 1;
}

int f_CmdSplit(const Args& l_oA) {
    if (l_oA.m_vPos.size() != 1 || !l_oA.f_Has("--out")) { std::fputs(g_pszUsage, stderr); return 3; }
    int l_iN = 0;
    std::string l_strErr;
    if (!f_SplitFont(l_oA.m_vPos[0], l_oA.f_Get("--out"), l_iN, l_strErr)) {
        std::fprintf(stderr, "error: %s\n", l_strErr.c_str());
        return 2;
    }
    std::printf("%s: %d pictures + chars.txt\n", l_oA.f_Get("--out").c_str(), l_iN);
    return 0;
}

int f_CmdPreview(const Args& l_oA) {
    if (l_oA.m_vPos.size() != 1 || !l_oA.f_Has("--out") || !l_oA.f_Has("--text")) { std::fputs(g_pszUsage, stderr); return 3; }
    BitmapFont l_oFont;
    atlas::Image l_oPage;
    std::string l_strErr;
    if (!f_LoadFont(l_oA.m_vPos[0], l_oFont, l_oPage, l_strErr)) {
        std::fprintf(stderr, "error: %s\n", l_strErr.c_str());
        return 2;
    }
    const float l_fScale = l_oA.f_Has("--scale") ? (float)std::atof(l_oA.f_Get("--scale").c_str()) : 1.0f;
    int l_iPad = 8;
    if ((l_oA.f_Has("--pad") && !f_ToInt(l_oA.f_Get("--pad"), l_iPad)) || l_fScale <= 0) {
        std::fprintf(stderr, "fontpack: bad --scale or --pad\n");
        return 3;
    }
    uint8_t l_arrBg[4] = {0, 0, 0, 0};
    if (l_oA.f_Has("--bg")) {
        const std::string l_strH = l_oA.f_Get("--bg");
        const unsigned long l_uV = std::strtoul(l_strH.c_str(), nullptr, 16);
        if (l_strH.size() == 8) { l_arrBg[0] = (uint8_t)(l_uV >> 24); l_arrBg[1] = (uint8_t)(l_uV >> 16); l_arrBg[2] = (uint8_t)(l_uV >> 8); l_arrBg[3] = (uint8_t)l_uV; }
        else if (l_strH.size() == 6) { l_arrBg[0] = (uint8_t)(l_uV >> 16); l_arrBg[1] = (uint8_t)(l_uV >> 8); l_arrBg[2] = (uint8_t)l_uV; l_arrBg[3] = 255; }
        else { std::fprintf(stderr, "fontpack: --bg is RRGGBB or RRGGBBAA\n"); return 3; }
    }
    int l_iMissing = 0;
    const atlas::Image l_oImg = f_RenderText(l_oFont, l_oPage, l_oA.f_Get("--text"), l_fScale, l_iPad, l_arrBg, &l_iMissing);
    if (!atlas::f_SavePng(l_oA.f_Get("--out"), l_oImg, &l_strErr)) {
        std::fprintf(stderr, "error: %s\n", l_strErr.c_str());
        return 2;
    }
    std::printf("%s: %dx%d\n", l_oA.f_Get("--out").c_str(), l_oImg.m_iW, l_oImg.m_iH);
    if (l_iMissing) std::fprintf(stderr, "warning: %d character(s) of the text are not in the font\n", l_iMissing);
    return l_iMissing ? 1 : 0;
}

int f_CmdInfo(const Args& l_oA) {
    if (l_oA.m_vPos.size() != 1) { std::fputs(g_pszUsage, stderr); return 3; }
    BitmapFont l_oFont;
    atlas::Image l_oPage;
    std::string l_strErr;
    if (!f_LoadFont(l_oA.m_vPos[0], l_oFont, l_oPage, l_strErr)) {
        std::fprintf(stderr, "error: %s\n", l_strErr.c_str());
        return 2;
    }
    std::printf("face \"%s\", size %d, line height %d, base %d, page %s (%dx%d)\n", l_oFont.m_strFace.c_str(), l_oFont.m_iSize,
                l_oFont.m_iLineHeight, l_oFont.m_iBase, l_oFont.m_vPages[0].c_str(), l_oPage.m_iW, l_oPage.m_iH);
    for (const auto& l_oG : l_oFont.m_vGlyphs)
        std::printf("  %-6s U+%04X  %4d,%-4d %3dx%-3d  offset %d,%d  advance %d\n",
                    l_oG.m_uId == ' ' ? "space" : f_Utf8Encode(l_oG.m_uId).c_str(), (unsigned)l_oG.m_uId, l_oG.m_iX, l_oG.m_iY, l_oG.m_iW, l_oG.m_iH, l_oG.m_iXoffset,
                    l_oG.m_iYoffset, l_oG.m_iXadvance);
    return 0;
}

}  // namespace

int f_FontCliMain(const std::vector<std::string>& l_vArgs) {
    if (l_vArgs.empty() || l_vArgs[0] == "--help" || l_vArgs[0] == "-h") {
        std::fputs(g_pszUsage, l_vArgs.empty() ? stderr : stdout);
        return l_vArgs.empty() ? 3 : 0;
    }
    Args l_oA;
    std::string l_strErr;
    if (!f_ParseArgs(l_vArgs, l_oA, l_strErr)) {
        std::fprintf(stderr, "fontpack: %s\n", l_strErr.c_str());
        return 3;
    }
    const std::string& l_strCmd = l_vArgs[0];
    if (l_strCmd == "build") return f_CmdBuild(l_oA);
    if (l_strCmd == "split") return f_CmdSplit(l_oA);
    if (l_strCmd == "preview") return f_CmdPreview(l_oA);
    if (l_strCmd == "info") return f_CmdInfo(l_oA);
    std::fprintf(stderr, "fontpack: unknown command '%s'\n\n%s", l_strCmd.c_str(), g_pszUsage);
    return 3;
}
