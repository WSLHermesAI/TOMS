// font_tool_test -- the picture font runtime (src/core/engine/bitmap_font.h) and the font tool
// (tools/font/core/font_build.h) on tests/font (docs/engine/09_FONT_TOOL.md).
//
//   font_tool_test <tests/font> <scratch dir>
//
// tests/font/connect_score_pictures: the single pictures of connect_score (made with `fontpack split`) -- the test
// input. Building them must give tests/font/connect_score.fnt/.png (an old convert2fnt-web font) byte
// for byte / pixel for pixel. preview.png is the test text drawn with it; TOMS_UPDATE_GOLDEN=1 rewrites it.
#include "bitmap_font.h"
#include "font_build.h"
#include "atlas_project.h"   // u8path

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace fonttool;

namespace {

int g_iFailed = 0;

void f_Check(bool l_bOk, const std::string& l_strWhat) {
    std::printf("[%s] %s\n", l_bOk ? "ok" : "FAILED", l_strWhat.c_str());
    if (!l_bOk) g_iFailed++;
}

std::string f_ReadText(const fs::path& l_oP) {
    std::ifstream l_oF(l_oP, std::ios::binary);
    std::ostringstream l_oSs;
    l_oSs << l_oF.rdbuf();
    return l_oSs.str();
}

bool f_Near(float l_fA, float l_fB) { return std::fabs(l_fA - l_fB) < 1e-4f; }

// A font written by hand: three 10x20 glyphs and an 'A' with offsets, kerning A->V.
const char* g_pszSmall =
    "info face=\"t\" size=20\n"
    "common lineHeight=20 base=16 scaleW=64 scaleH=32 pages=1\n"
    "page id=0 file=\"t.png\"\n"
    "chars count=4\n"
    "char id=49 x=0 y=0 width=10 height=20 xoffset=0 yoffset=0 xadvance=10 page=0\n"
    "char id=50 x=10 y=0 width=10 height=20 xoffset=0 yoffset=0 xadvance=12 page=0\n"
    "char id=65 x=20 y=0 width=8 height=16 xoffset=1 yoffset=4 xadvance=10 page=0\n"
    "char id=86 x=30 y=0 width=8 height=16 xoffset=0 yoffset=4 xadvance=10 page=0\n"
    "char id=32 x=0 y=0 width=0 height=0 xoffset=0 yoffset=0 xadvance=6 page=0 letter=\" \"\n"
    "kernings count=1\n"
    "kerning first=65 second=86 amount=-3\n";

// The old BMFont layout ("Arial" header, e.g. total_score.fnt): CRLF here, a repeated page=,
// letter="space", lower-case k and m.
const char* g_pszArial =
    "info face=\"Arial\" size=110 bold=0 italic=0 charset=\"\" unicode=0 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=2,2\r\n"
    "common lineHeight=110 base=110 scaleW=1104 scaleH=110 pages=1 packed=0\r\n"
    "page id=0 file=\"total_score.png\"\r\n"
    "chars count=3\r\n"
    "char id=49 x=76 y=0 width=76 height=110 xoffset=0 yoffset=0 xadvance=76 page=0 chnl=0 page=0 letter=\"1\"\r\n"
    "char id=107 x=852 y=0 width=84 height=110 xoffset=0 yoffset=0 xadvance=84 page=0 chnl=0 page=0 letter=\"k\"\r\n"
    "char id=32 x=1020 y=0 width=84 height=110 xoffset=0 yoffset=0 xadvance=84 page=0 chnl=0 page=0 letter=\"space\"";

void f_TestRuntime() {
    BitmapFont l_oF;
    std::string l_strErr;
    f_Check(l_oF.f_Parse(g_pszSmall, &l_strErr) && l_oF.m_vGlyphs.size() == 5 && l_oF.m_iLineHeight == 20 && l_oF.m_iScaleW == 64, "runtime: parse " + l_strErr);
    f_Check(l_oF.m_vPages.size() == 1 && l_oF.m_vPages[0] == "t.png" && l_oF.m_strFace == "t", "runtime: page and face");
    f_Check(l_oF.f_Glyph(' ') && l_oF.f_Glyph(' ')->m_iXadvance == 6, "runtime: letter=\" \" (a quoted space) parses");
    f_Check(l_oF.f_Glyph('a') == l_oF.f_Glyph('A') && l_oF.f_Glyph('a'), "runtime: a lower-case letter falls back to upper case");

    int l_iMissing = -1;
    auto l_vP = l_oF.f_Layout("12 1", 100, 50, 1.0f, BitmapFont::Align::Left, &l_iMissing);
    f_Check(l_vP.size() == 3 && l_iMissing == 0, "runtime: '12 1' places 3 pictures (the space has none)");
    f_Check(l_vP.size() == 3 && f_Near(l_vP[0].m_fX, 100) && f_Near(l_vP[1].m_fX, 110) && f_Near(l_vP[2].m_fX, 128) && f_Near(l_vP[0].m_fY, 50),
          "runtime: pen advances 10, 12, space 6");
    l_vP = l_oF.f_Layout("AV", 0, 0, 2.0f);
    f_Check(l_vP.size() == 2 && f_Near(l_vP[0].m_fX, 2) && f_Near(l_vP[0].m_fY, 8) && f_Near(l_vP[0].m_fW, 16) && f_Near(l_vP[1].m_fX, 14),
          "runtime: scale 2 doubles offsets and sizes; kerning A->V -3");
    l_vP = l_oF.f_Layout("1#2", 0, 0, 1.0f, BitmapFont::Align::Left, &l_iMissing);
    f_Check(l_vP.size() == 2 && l_iMissing == 1 && f_Near(l_vP[1].m_fX, 10), "runtime: a missing character is skipped and counted");
    l_vP = l_oF.f_Layout("1\n22", 0, 0, 1.0f, BitmapFont::Align::Right);
    f_Check(l_vP.size() == 3 && f_Near(l_vP[0].m_fX, -10) && f_Near(l_vP[1].m_fX, -24) && f_Near(l_vP[1].m_fY, 20), "runtime: newline + right align");
    l_vP = l_oF.f_Layout("12", 100, 0, 1.0f, BitmapFont::Align::Center);
    f_Check(l_vP.size() == 2 && f_Near(l_vP[0].m_fX, 89), "runtime: center align");
    float l_fW = 0, l_fH = 0;
    l_oF.f_Measure("12\n1", 1.0f, l_fW, l_fH);
    f_Check(f_Near(l_fW, 22) && f_Near(l_fH, 40), "runtime: measure two lines");

    std::vector<Quad> l_vQuads;
    const float l_arrTint[4] = {1, 0.5f, 1, 1};
    l_oF.f_AppendQuads("2", 0, 0, 1.0f, BitmapFont::Align::Left, l_arrTint, 7, l_vQuads);
    f_Check(l_vQuads.size() == 1 && f_Near(l_vQuads[0].m_arrUv[0], 10.f / 64) && f_Near(l_vQuads[0].m_arrUv[3], 20.f / 32) &&
              l_vQuads[0].m_u16Texture == 7 && f_Near(l_vQuads[0].m_arrTint[1], 0.5f),
          "runtime: quads carry page UVs, texture and tint");

    // UTF-8: a CJK glyph
    BitmapFont l_oCjk;
    f_Check(l_oCjk.f_Parse("common lineHeight=10\nchar id=20013 x=0 y=0 width=10 height=10 xadvance=10\n") &&
              l_oCjk.f_Layout(u8"中中", 0, 0).size() == 2,
          "runtime: UTF-8 text finds U+4E2D");

    BitmapFont l_oArial;
    f_Check(l_oArial.f_Parse(g_pszArial, &l_strErr) && l_oArial.m_vGlyphs.size() == 3 && l_oArial.m_iLineHeight == 110, "runtime: old BMFont layout (CRLF) parses");
    f_Check(l_oArial.f_Glyph('K') && l_oArial.f_Glyph('K')->m_uId == 'k', "runtime: 'K' finds the old fonts' lower-case k");
    f_Check(l_oArial.f_Glyph(' ') && l_oArial.f_Glyph(' ')->m_iX == 1020, "runtime: letter=\"space\" is the space glyph");
    BitmapFont l_oBad;
    f_Check(!l_oBad.f_Parse("hello\n", &l_strErr) && !l_strErr.empty(), "runtime: not a .fnt -> an error");
}

void f_TestNames() {
    const uint32_t l_arrChars[] = {'0', '9', 'K', 'x', '.', ',', ' ', '/', ':', '*', '?', '"', '<', '|', '%', 0xD7, 0x4E2D, 0x01};
    bool l_bOk = true;
    for (uint32_t l_uC : l_arrChars) {
        const std::string l_strN = f_GlyphFileName(l_uC);
        l_bOk &= f_CharFromName(l_strN) == l_uC;
        l_bOk &= l_strN.find_first_of("\\/:*?\"<>|") == std::string::npos && l_strN != "." && !l_strN.empty();
    }
    f_Check(l_bOk, "names: every glyphFileName reads back and is a valid Windows file name");
    f_Check(f_CharFromName("DOT") == '.' && f_CharFromName("u+004b") == 'K' && f_CharFromName("hello") == 0,
          "names: aliases ignore case; U+XXXX; an unknown word is no character");
}

void f_TestRoundTrip(const fs::path& l_oData, const fs::path& l_oScratch) {
    // single pictures -> the convert2fnt-web font, exactly
    std::vector<GlyphPicture> l_vGlyphs;
    std::vector<std::string> l_vWarnings;
    std::string l_strErr;
    const bool l_bCollected = f_CollectGlyphs(atlas::f_U8str(l_oData / "connect_score_pictures"), l_vGlyphs, l_vWarnings, l_strErr);
    f_Check(l_bCollected && l_vWarnings.empty() && l_vGlyphs.size() == 15, "build: tests/font/connect_score_pictures has 15 pictures " + l_strErr);
    BuildOptions l_oOpt;
    l_oOpt.m_strFace = "convert2fnt-web";
    BuiltFont l_oBuilt;
    f_Check(f_BuildFont(l_vGlyphs, l_oOpt, "connect_score.png", l_oBuilt, l_strErr), "build: builds " + l_strErr);
    const std::string l_strExpected = f_ReadText(l_oData / "connect_score.fnt");
    f_Check(!l_strExpected.empty() && l_oBuilt.m_strFnt == l_strExpected, "build: the .fnt equals connect_score.fnt byte for byte");
    if (l_oBuilt.m_strFnt != l_strExpected) std::printf("--- built:\n%s\n--- expected:\n%s\n", l_oBuilt.m_strFnt.c_str(), l_strExpected.c_str());
    atlas::Image l_oRef;
    f_Check(atlas::f_LoadImage(atlas::f_U8str(l_oData / "connect_score.png"), l_oRef, &l_strErr) && l_oRef.m_iW == l_oBuilt.m_oPage.m_iW &&
              l_oRef.m_iH == l_oBuilt.m_oPage.m_iH && atlas::f_SamePixels(l_oRef, {0, 0, l_oRef.m_iW, l_oRef.m_iH}, l_oBuilt.m_oPage, {0, 0, l_oRef.m_iW, l_oRef.m_iH}),
          "build: the page equals connect_score.png pixel for pixel");

    // write -> split -> collect -> build: the same font again
    const fs::path l_oOut = l_oScratch / "roundtrip" / "connect.fnt";
    f_Check(f_WriteFont(l_oBuilt, atlas::f_U8str(l_oOut), l_strErr) && fs::exists(l_oScratch / "roundtrip" / "connect_score.png"),
          "write: .fnt + the page it names");
    int l_iN = 0;
    f_Check(f_SplitFont(atlas::f_U8str(l_oOut), atlas::f_U8str(l_oScratch / "split"), l_iN, l_strErr) && l_iN == 15, "split: 15 pictures " + l_strErr);
    std::vector<GlyphPicture> l_vAgain;
    l_vWarnings.clear();
    BuiltFont l_oRebuilt;
    f_Check(f_CollectGlyphs(atlas::f_U8str(l_oScratch / "split"), l_vAgain, l_vWarnings, l_strErr) &&
              f_BuildFont(l_vAgain, l_oOpt, "connect_score.png", l_oRebuilt, l_strErr) && l_oRebuilt.m_strFnt == l_strExpected,
          "split + build: the same .fnt again");
}

// The space's width and the line height: auto values, and set by hand.
void f_TestSpaceAndLines(const fs::path& l_oData, const fs::path& l_oScratch) {
    std::vector<GlyphPicture> l_vAll;
    std::vector<std::string> l_vWarnings;
    std::string l_strErr;
    f_CollectGlyphs(atlas::f_U8str(l_oData / "connect_score_pictures"), l_vAll, l_vWarnings, l_strErr);
    std::vector<GlyphPicture> l_vNoSpace, l_vNoOne;
    for (const GlyphPicture& l_oG : l_vAll) {
        if (l_oG.m_uCp != ' ') l_vNoSpace.push_back(l_oG);
        if (l_oG.m_uCp != ' ' && l_oG.m_uCp != '1') l_vNoOne.push_back(l_oG);
    }
    BuiltFont l_oB;
    f_Check(f_DefaultSpaceAdvance(l_vAll) == 86 && f_BuildFont(l_vAll, {}, "s.png", l_oB, l_strErr) && l_oB.m_oFont.f_Glyph(' ')->m_iXadvance == 86,
          "space: a space picture gives its width (86)");
    f_Check(f_DefaultSpaceAdvance(l_vNoSpace) == 72 && f_BuildFont(l_vNoSpace, {}, "s.png", l_oB, l_strErr) && l_oB.m_oFont.f_Glyph(' ')->m_iXadvance == 72,
          "space: no space picture -> as wide as the 1 picture (72)");
    f_Check(f_DefaultSpaceAdvance(l_vNoOne) == 5 && f_BuildFont(l_vNoOne, {}, "s.png", l_oB, l_strErr) && l_oB.m_oFont.f_Glyph(' ')->m_iXadvance == 5,
          "space: no space and no 1 picture -> 5");
    BuildOptions l_oOpt;
    l_oOpt.m_iSpaceAdvance = 40;
    f_Check(f_BuildFont(l_vAll, l_oOpt, "s.png", l_oB, l_strErr) && l_oB.m_oFont.f_Glyph(' ')->m_iXadvance == 40,
          "space: a width set by hand wins over the space picture");

    f_Check(f_DefaultLineHeight(l_vAll) == 94 && f_BuildFont(l_vAll, {}, "s.png", l_oB, l_strErr) && l_oB.m_oFont.m_iLineHeight == 94,
          "line height: auto = the tallest picture (94)");
    l_oOpt = BuildOptions();
    l_oOpt.m_iLineHeight = 130;
    f_Check(f_BuildFont(l_vAll, l_oOpt, "s.png", l_oB, l_strErr) && l_oB.m_oFont.m_iLineHeight == 130 && l_oB.m_oFont.m_iBase == 94 &&
              l_oB.m_oFont.f_Glyph('1')->m_iYoffset == 0,
          "line height: set to 130, the glyphs stay on the tallest picture's baseline");
    const auto l_vPlaced = l_oB.m_oFont.f_Layout("1\n1", 0, 0);
    f_Check(l_vPlaced.size() == 2 && f_Near(l_vPlaced[1].m_fY, 130), "line height: the second line starts 130 lower");
    float l_fW = 0, l_fH = 0;
    l_oB.m_oFont.f_Measure("1\n1", 1.0f, l_fW, l_fH);
    f_Check(f_Near(l_fH, 260), "line height: two lines measure 260");
    // save, open again as pictures: the blank space keeps its width and does not grow the tallest picture
    const fs::path l_oFnt = l_oScratch / "lines" / "lines.fnt";
    std::vector<GlyphPicture> l_vBack;
    f_Check(f_WriteFont(l_oB, atlas::f_U8str(l_oFnt), l_strErr) && f_GlyphsFromFont(atlas::f_U8str(l_oFnt), l_vBack, l_strErr) &&
              f_DefaultLineHeight(l_vBack) == 94 && f_DefaultSpaceAdvance(l_vBack) == 86,
          "line height: a saved font opens back with the same pictures (tallest 94, space 86)");
}

// The Tight layouts (the atlas tool's packer): every glyph keeps its pixels, none overlap, a power-of-two
// page when asked, and text draws exactly as with the one-row layout.
void f_TestLayouts(const fs::path& l_oData) {
    std::vector<GlyphPicture> l_vPics;
    std::vector<std::string> l_vWarnings;
    std::string l_strErr;
    f_CollectGlyphs(atlas::f_U8str(l_oData / "connect_score_pictures"), l_vPics, l_vWarnings, l_strErr);
    BitmapFont l_oRowFont;
    atlas::Image l_oRowPage;
    f_LoadFont(atlas::f_U8str(l_oData / "connect_score.fnt"), l_oRowFont, l_oRowPage, l_strErr);
    const std::string l_strText = "12,345.6K\n9,999M 0";
    const atlas::Image l_oRowText = f_RenderText(l_oRowFont, l_oRowPage, l_strText, 0.5f, 8);

    for (PageLayout l_eLayout : {PageLayout::Tight, PageLayout::TightPow2}) {
        const std::string l_strName = std::string("layout ") + f_PageLayoutName(l_eLayout) + ": ";
        BuildOptions l_oOpt;
        l_oOpt.m_eLayout = l_eLayout;
        BuiltFont l_oB;
        if (!f_BuildFont(l_vPics, l_oOpt, "t.png", l_oB, l_strErr)) {
            f_Check(false, l_strName + "builds " + l_strErr);
            continue;
        }
        bool l_bInside = true, l_bApart = true, l_bSamePixels = true;
        const auto& l_vGs = l_oB.m_oFont.m_vGlyphs;
        for (size_t l_nI = 0; l_nI < l_vGs.size(); l_nI++) {
            if (l_vGs[l_nI].m_iW == 0) continue;
            const atlas::IRect l_oR{l_vGs[l_nI].m_iX, l_vGs[l_nI].m_iY, l_vGs[l_nI].m_iW, l_vGs[l_nI].m_iH};
            l_bInside &= l_oR.m_iX >= 1 && l_oR.m_iY >= 1 && l_oR.f_Right() <= l_oB.m_oPage.m_iW && l_oR.f_Bottom() <= l_oB.m_oPage.m_iH;
            for (size_t l_nJ = l_nI + 1; l_nJ < l_vGs.size(); l_nJ++)
                if (l_vGs[l_nJ].m_iW) l_bApart &= !l_oR.f_Intersects({l_vGs[l_nJ].m_iX - 1, l_vGs[l_nJ].m_iY - 1, l_vGs[l_nJ].m_iW + 2, l_vGs[l_nJ].m_iH + 2});
            l_bSamePixels &= atlas::f_SamePixels(l_oB.m_oPage, l_oR, l_vPics[l_nI].m_oImg, {0, 0, l_vPics[l_nI].m_oImg.m_iW, l_vPics[l_nI].m_oImg.m_iH});
        }
        f_Check(l_bInside && l_bApart && l_bSamePixels, l_strName + "every picture on the page, 1 px apart, pixels kept");
        const bool l_bPot = (l_oB.m_oPage.m_iW & (l_oB.m_oPage.m_iW - 1)) == 0 && (l_oB.m_oPage.m_iH & (l_oB.m_oPage.m_iH - 1)) == 0;
        if (l_eLayout == PageLayout::TightPow2)
            f_Check(l_bPot, l_strName + "page " + std::to_string(l_oB.m_oPage.m_iW) + "x" + std::to_string(l_oB.m_oPage.m_iH) + " is a power of 2");
        else
            // the packer keeps 1 px clear on every side (the row only on the top and left); glyphs of one
            // height are already best in one row, so tight = the row + that border here
            f_Check((long long)l_oB.m_oPage.m_iW * l_oB.m_oPage.m_iH <= (long long)(l_oRowPage.m_iW + 1) * (l_oRowPage.m_iH + 1),
                  l_strName + "page " + std::to_string(l_oB.m_oPage.m_iW) + "x" + std::to_string(l_oB.m_oPage.m_iH) +
                      " no larger than the one row (" + std::to_string(l_oRowPage.m_iW) + "x" + std::to_string(l_oRowPage.m_iH) +
                      ") with its border");
        const atlas::Image l_oT = f_RenderText(l_oB.m_oFont, l_oB.m_oPage, l_strText, 0.5f, 8);
        f_Check(l_oT.m_iW == l_oRowText.m_iW && l_oT.m_iH == l_oRowText.m_iH && atlas::f_SamePixels(l_oT, {0, 0, l_oT.m_iW, l_oT.m_iH}, l_oRowText, {0, 0, l_oT.m_iW, l_oT.m_iH}),
              l_strName + "the test text draws exactly as with one row");
    }
    PageLayout l_eL;
    f_Check(f_ParsePageLayout("pot", l_eL) && l_eL == PageLayout::TightPow2 && f_ParsePageLayout("Tight", l_eL) &&
              l_eL == PageLayout::Tight && !f_ParsePageLayout("grid", l_eL),
          "layout: names parse");
    BuildOptions l_oSmall;
    l_oSmall.m_eLayout = PageLayout::TightPow2;
    l_oSmall.m_iMaxWidth = 128;
    BuiltFont l_oB;
    f_Check(!f_BuildFont(l_vPics, l_oSmall, "t.png", l_oB, l_strErr) && l_strErr.find("do not fit") != std::string::npos,
          "layout: a page too small for the pictures is an error");
}

// Every font in tests/font (the old project's: BMFont "Arial" and convert2fnt-web) loads, draws its
// digits, and opens back into pictures that build again.
void f_TestAllFonts(const fs::path& l_oData) {
    int l_iFonts = 0;
    for (const auto& l_oDe : fs::directory_iterator(l_oData)) {
        if (l_oDe.path().extension() != ".fnt") continue;
        l_iFonts++;
        const std::string l_strName = atlas::f_U8str(l_oDe.path().filename());
        BitmapFont l_oFont;
        atlas::Image l_oPage;
        std::string l_strErr;
        int l_iMissing = -1;
        const bool l_bLoaded = f_LoadFont(atlas::f_U8str(l_oDe.path()), l_oFont, l_oPage, l_strErr);
        const atlas::Image l_oImg = l_bLoaded ? f_RenderText(l_oFont, l_oPage, "0123456789", 0.5f, 4, nullptr, &l_iMissing) : atlas::Image();
        f_Check(l_bLoaded && l_iMissing == 0 && l_oImg.f_Valid() && !atlas::f_OpaqueBounds(l_oImg).f_Empty(),
              "all fonts: " + l_strName + " loads and draws 0-9 " + l_strErr);
        std::vector<GlyphPicture> l_vPics;
        BuiltFont l_oB;
        f_Check(f_GlyphsFromFont(atlas::f_U8str(l_oDe.path()), l_vPics, l_strErr) && f_BuildFont(l_vPics, {}, "x.png", l_oB, l_strErr) &&
                  l_oB.m_oFont.m_vGlyphs.size() == l_oFont.m_vGlyphs.size(),
              "all fonts: " + l_strName + " opens as pictures and builds again " + l_strErr);
    }
    f_Check(l_iFonts >= 1, "all fonts: tests/font has .fnt files");
}

// The editor's way: pictures added one by one, characters set by hand, a font opened back into pictures.
void f_TestPictures(const fs::path& l_oData, const fs::path& l_oScratch) {
    std::vector<GlyphPicture> l_vPics;
    std::string l_strErr;
    for (const char* l_pszName : {"0.png", "dot.png", "K.png"}) {
        GlyphPicture l_oG;
        f_Check(f_LoadPicture(atlas::f_U8str(l_oData / "connect_score_pictures" / l_pszName), l_oG, l_strErr), std::string("pictures: load ") + l_pszName + " " + l_strErr);
        l_vPics.push_back(std::move(l_oG));
    }
    f_Check(l_vPics.size() == 3 && l_vPics[0].m_uCp == '0' && l_vPics[1].m_uCp == '.' && l_vPics[2].m_uCp == 'K',
          "pictures: the character is guessed from the file name");
    GlyphPicture l_oUnnamed;
    f_LoadPicture(atlas::f_U8str(l_oData / "connect_score_pictures" / "1.png"), l_oUnnamed, l_strErr);
    l_oUnnamed.m_uCp = 0;
    l_oUnnamed.m_strFile = "my_glyph.png";
    l_vPics.push_back(l_oUnnamed);
    std::vector<std::string> l_vProblems = f_CheckGlyphs(l_vPics);
    BuiltFont l_oB;
    f_Check(l_vProblems.size() == 1 && l_vProblems[0].find("my_glyph.png") != std::string::npos && !f_BuildFont(l_vPics, {}, "p.png", l_oB, l_strErr),
          "pictures: one without a character is a problem, and the build refuses");
    l_vPics.back().m_uCp = '0';
    l_vProblems = f_CheckGlyphs(l_vPics);
    f_Check(l_vProblems.size() == 1 && l_vProblems[0].find("already given") != std::string::npos,
          "pictures: a character given twice is a problem");
    l_vPics.back().m_uCp = '1';
    f_Check(f_CheckGlyphs(l_vPics).empty() && f_BuildFont(l_vPics, {}, "p.png", l_oB, l_strErr) && l_oB.m_oFont.m_vGlyphs.size() == 5 &&
              l_oB.m_oFont.m_vGlyphs[0].m_uId == '0' && l_oB.m_oFont.m_vGlyphs[3].m_uId == '1',
          "pictures: set by hand, it builds in the order given (+ space)");

    std::vector<GlyphPicture> l_vBack;
    f_Check(f_GlyphsFromFont(atlas::f_U8str(l_oData / "connect_score.fnt"), l_vBack, l_strErr) && l_vBack.size() == 15 && l_vBack[0].m_uCp == '0' &&
              l_vBack[14].m_uCp == ' ' && l_vBack[14].m_oImg.m_iW == 86,
          "pictures: a font opens back into its 15 pictures (the space as a blank one as wide as its advance)");
    BuildOptions l_oOpt;
    l_oOpt.m_strFace = "convert2fnt-web";
    f_Check(f_BuildFont(l_vBack, l_oOpt, "connect_score.png", l_oB, l_strErr) && l_oB.m_strFnt == f_ReadText(l_oData / "connect_score.fnt"),
          "pictures: and builds the same font again");
    (void)l_oScratch;
}

atlas::Image f_Solid(int l_iW, int l_iH, uint8_t l_u8R) {
    atlas::Image l_oImg(l_iW, l_iH);
    for (size_t l_nI = 0; l_nI < l_oImg.m_vPx.size(); l_nI += 4) { l_oImg.m_vPx[l_nI] = l_u8R; l_oImg.m_vPx[l_nI + 3] = 255; }
    return l_oImg;
}

void f_TestOptions(const fs::path& l_oScratch) {
    // K and k, '.', a CJK character and a different height; wrapping; spacing; chars.txt
    std::vector<GlyphPicture> l_vG = {{'K', "K", f_Solid(20, 30, 1)}, {'k', "k", f_Solid(18, 30, 2)},
                                   {'.', "dot", f_Solid(8, 10, 3)}, {0x4E2D, "zh", f_Solid(30, 30, 4)}};
    BuildOptions l_oOpt;
    l_oOpt.m_iMaxWidth = 50;
    l_oOpt.m_iAdvancePad = 2;
    l_oOpt.m_iSpaceAdvance = 12;
    BuiltFont l_oB;
    std::string l_strErr;
    f_Check(f_BuildFont(l_vG, l_oOpt, "w.png", l_oB, l_strErr), "options: build " + l_strErr);
    const BitmapGlyph* l_pDot = l_oB.m_oFont.f_Glyph('.');
    f_Check(l_pDot && l_pDot->m_iYoffset == 20 && l_pDot->m_iH == 10, "options: a shorter picture sits on the bottom of the line");
    f_Check(l_oB.m_oFont.f_Glyph(0x4E2D) && l_oB.m_oFont.f_Glyph(0x4E2D)->m_iY > 1 && l_oB.m_oPage.m_iW <= 50, "options: a row wraps at --max-width");
    f_Check(l_oB.m_oFont.f_Glyph('K')->m_iXadvance == 22 && l_oB.m_oFont.f_Glyph(' ')->m_iXadvance == 14,
          "options: letter spacing pads every advance; --space sets the space");
    f_Check(l_oB.m_oFont.f_Glyph('K') != l_oB.m_oFont.f_Glyph('k'), "options: K and k stay two glyphs");

    const fs::path l_oFnt = l_oScratch / "cases" / "cases.fnt";
    int l_iN = 0;
    f_Check(f_WriteFont(l_oB, atlas::f_U8str(l_oFnt), l_strErr) && f_SplitFont(atlas::f_U8str(l_oFnt), atlas::f_U8str(l_oScratch / "cases_split"), l_iN, l_strErr),
          "options: write + split " + l_strErr);
    std::vector<GlyphPicture> l_vBack;
    std::vector<std::string> l_vWarnings;
    f_CollectGlyphs(atlas::f_U8str(l_oScratch / "cases_split"), l_vBack, l_vWarnings, l_strErr);
    bool l_bBoth = false;
    for (const auto& l_oA : l_vBack) l_bBoth |= l_oA.m_uCp == 'k';
    f_Check(l_vBack.size() == 5 && l_bBoth && l_vWarnings.empty(), "options: K and k survive split on a case-blind file system");

    // a folder with a name that is no character, and chars.txt mapping one
    const fs::path l_oDir = l_oScratch / "named";
    fs::remove_all(l_oDir);
    fs::create_directories(l_oDir);
    atlas::f_SavePng(atlas::f_U8str(l_oDir / "seven.png"), f_Solid(10, 10, 9));
    atlas::f_SavePng(atlas::f_U8str(l_oDir / "big K.png"), f_Solid(10, 10, 9));
    atlas::f_SavePng(atlas::f_U8str(l_oDir / "1.png"), f_Solid(10, 10, 9));
    { std::ofstream(l_oDir / "chars.txt") << "# test\nseven=7\nbig K=K\n"; }
    std::vector<GlyphPicture> l_vNamed;
    l_vWarnings.clear();
    f_Check(f_CollectGlyphs(atlas::f_U8str(l_oDir), l_vNamed, l_vWarnings, l_strErr) && l_vNamed.size() == 3 && l_vNamed[0].m_uCp == '7' &&
              l_vNamed[1].m_uCp == 'K' && l_vNamed[2].m_uCp == '1',
          "folder: chars.txt maps names and sets the order (unlisted files after)");
    { std::ofstream(l_oDir / "chars.txt") << "seven=7\n"; }
    l_vWarnings.clear();
    f_CollectGlyphs(atlas::f_U8str(l_oDir), l_vNamed, l_vWarnings, l_strErr);
    f_Check(l_vNamed.size() == 2 && l_vWarnings.size() == 1 && l_vWarnings[0].find("big K.png") != std::string::npos,
          "folder: a file whose name is no character is skipped with a warning");
}

void f_TestPreview(const fs::path& l_oData, const fs::path& l_oScratch) {
    BitmapFont l_oFont;
    atlas::Image l_oPage;
    std::string l_strErr;
    f_Check(f_LoadFont(atlas::f_U8str(l_oData / "connect_score.fnt"), l_oFont, l_oPage, l_strErr), "preview: loads connect_score " + l_strErr);
    int l_iMissing = -1;
    const uint8_t l_arrBg[4] = {15, 15, 26, 255};   // kBackgroundClearColor
    const atlas::Image l_oImg = f_RenderText(l_oFont, l_oPage, "12,345.6K\n9,999M 0", 0.5f, 8, l_arrBg, &l_iMissing);
    f_Check(l_iMissing == 0 && l_oImg.f_Valid(), "preview: the test text draws, every character in the font");
    const fs::path l_oGolden = l_oData / "preview.png";
    const char* l_pszUpdate = std::getenv("TOMS_UPDATE_GOLDEN");
    if (l_pszUpdate && *l_pszUpdate && std::string(l_pszUpdate) != "0") {
        atlas::f_SavePng(atlas::f_U8str(l_oGolden), l_oImg, &l_strErr);
        std::printf("[update] wrote %s\n", atlas::f_U8str(l_oGolden).c_str());
    }
    atlas::Image l_oRef;
    const bool l_bSame = atlas::f_LoadImage(atlas::f_U8str(l_oGolden), l_oRef, &l_strErr) && l_oRef.m_iW == l_oImg.m_iW && l_oRef.m_iH == l_oImg.m_iH &&
                      atlas::f_SamePixels(l_oRef, {0, 0, l_oRef.m_iW, l_oRef.m_iH}, l_oImg, {0, 0, l_oImg.m_iW, l_oImg.m_iH});
    if (!l_bSame) {
        atlas::f_SavePng(atlas::f_U8str(l_oScratch / "preview.png"), l_oImg, &l_strErr);
        std::printf("  got %s (%dx%d), reference %dx%d\n", atlas::f_U8str(l_oScratch / "preview.png").c_str(), l_oImg.m_iW, l_oImg.m_iH,
                    l_oRef.m_iW, l_oRef.m_iH);
    }
    f_Check(l_bSame, "preview: equals tests/font/preview.png");
}

// Records what FontText asks of the renderer.
struct FakeRenderer : IRenderer {
    int m_iTextures = 0, m_iReleased = 0;
    uint32_t m_uTexW = 0, m_uTexH = 0;
    std::vector<Quad> m_vDrawn;
    void f_Init(uint32_t, uint32_t) override {}
    void f_LoadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void f_LoadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    uint16_t f_LoadTexture(const std::vector<uint8_t>& l_vRgba, uint32_t l_uW, uint32_t l_uH) override {
        m_uTexW = l_uW; m_uTexH = l_uH;
        return l_vRgba.size() == (size_t)l_uW * l_uH * 4 ? (uint16_t)(++m_iTextures) : g_u16SpriteAtlasTexture;
    }
    void f_ReleaseTexture(uint16_t) override { m_iReleased++; }
    void begin() override {}
    void f_DrawSprite(const Quad& l_oQ) override { m_vDrawn.push_back(l_oQ); }
    void end() override {}
    uint32_t f_Width() const override { return 1280; }
    uint32_t f_Height() const override { return 720; }
};

void f_TestFontText(const fs::path& l_oData) {
    FakeRenderer l_oR;
    FontText l_oScore;
    std::string l_strErr;
    f_Check(l_oScore.f_Load(l_oR, atlas::f_U8str(l_oData / "connect_score.fnt"), &l_strErr) && l_oR.m_iTextures == 1 && l_oR.m_uTexW == 978 &&
              l_oR.m_uTexH == 95, "FontText: load reads the .fnt and uploads its page " + l_strErr);
    l_oScore.f_Render();
    f_Check(l_oR.m_vDrawn.empty(), "FontText: no text, nothing drawn");

    l_oScore.f_SetText("1,234K");
    l_oScore.f_SetPosition(900, 40);
    l_oScore.f_SetAlign(BitmapFont::Align::Right);
    l_oScore.f_Render();
    // 1 2 3 4 = 72 each, ',' 36, K 86: 410 wide, right edge at 900
    f_Check(l_oR.m_vDrawn.size() == 6 && f_Near(l_oR.m_vDrawn[0].m_arrRect[0], 490) && f_Near(l_oR.m_vDrawn[0].m_arrRect[1], 40) &&
              l_oR.m_vDrawn[0].m_u16Texture == 1 && f_Near(l_oScore.f_Width(), 410) && f_Near(l_oScore.f_Height(), 94),
          "FontText: setText + setPosition + right align -> 6 quads ending at x 900");
    l_oR.m_vDrawn.clear();
    l_oScore.f_SetScale(0.5f);
    l_oScore.f_SetColor(1, 0, 0, 0.5f);
    l_oScore.f_Render();
    f_Check(l_oR.m_vDrawn.size() == 6 && f_Near(l_oR.m_vDrawn[0].m_arrRect[0], 695) && f_Near(l_oR.m_vDrawn[0].m_arrRect[2], 36) &&
              f_Near(l_oR.m_vDrawn[0].m_arrTint[1], 0) && f_Near(l_oR.m_vDrawn[0].m_arrTint[3], 0.5f),
          "FontText: scale and colour apply on the next render");

    FontText l_oCopy = l_oScore;   // shares the page
    l_oCopy.f_SetText("99");
    l_oCopy.f_SetAlign(BitmapFont::Align::Left);
    l_oCopy.f_SetPosition(0, 0);
    l_oR.m_vDrawn.clear();
    l_oCopy.f_Render();
    f_Check(l_oR.m_vDrawn.size() == 2 && l_oR.m_iTextures == 1 && l_oR.m_vDrawn[0].m_u16Texture == 1 && f_Near(l_oR.m_vDrawn[0].m_arrRect[0], 0),
          "FontText: a copy draws its own text from the same texture");
    l_oCopy.f_SetText("9#");
    f_Check(l_oCopy.f_MissingChars() == 1, "FontText: missingChars counts what the font lacks");
    l_oR.m_vDrawn.clear();
    l_oScore.f_SetVisible(false);
    l_oScore.f_Render();
    f_Check(l_oR.m_vDrawn.empty(), "FontText: setVisible(false) hides it");
    l_oScore.f_Release();
    f_Check(l_oR.m_iReleased == 1 && !l_oScore.f_Loaded(), "FontText: release frees the page");

    FontText l_oBad;
    f_Check(!l_oBad.f_Load(l_oR, atlas::f_U8str(l_oData / "nope.fnt"), &l_strErr) && !l_strErr.empty() && !l_oBad.f_Loaded(),
          "FontText: a missing file -> false and an error");
}

}  // namespace

int main(int l_iArgc, char** l_pArgv) {
    if (l_iArgc < 3) {
        std::fprintf(stderr, "usage: font_tool_test <tests/font> <scratch dir>\n");
        return 3;
    }
    const fs::path l_oData = atlas::f_U8path(l_pArgv[1]);
    const fs::path l_oScratch = atlas::f_U8path(l_pArgv[2]);
    std::error_code l_oEc;
    fs::remove_all(l_oScratch, l_oEc);
    fs::create_directories(l_oScratch, l_oEc);

    f_TestRuntime();
    f_TestNames();
    f_TestRoundTrip(l_oData, l_oScratch);
    f_TestOptions(l_oScratch);
    f_TestPictures(l_oData, l_oScratch);
    f_TestAllFonts(l_oData);
    f_TestLayouts(l_oData);
    f_TestSpaceAndLines(l_oData, l_oScratch);
    f_TestPreview(l_oData, l_oScratch);
    f_TestFontText(l_oData);
    std::printf("font_tool_test: %d failed\n", g_iFailed);
    return g_iFailed ? 1 : 0;
}
