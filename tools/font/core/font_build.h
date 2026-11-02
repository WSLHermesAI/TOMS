// font_build.h -- picture fonts from single pictures (docs/engine/09_FONT_TOOL.md).
//
// Input: pictures, one PNG per character, each given its character (code point). font_editor lets you
// add pictures and type each one's character; fontpack takes PNG files (or folders of them) and reads
// the character from the file name, or from --chars:
//   0.png  7.png  K.png  x.png          the character itself
//   dot.png  comma.png  space.png ...   a name for one a file name cannot hold (charFromName)
//   U+00D7.png                          any code point
//   chars.txt (in a folder, optional)   `file=char` lines; also the order of the glyphs on the page
// Output: <name>.fnt (BMFont text, the convert2fnt-web layout of tests/font/connect_score.fnt) + <name>.png:
// the pictures side by side, 1 px apart, each as it is (not trimmed), bottom aligned on one line.
// A space is always there: as wide as a space picture, else as the '1' picture, else 5 (or as asked). The
// line height (the step to the next line) is the tallest picture's height unless asked otherwise.
#pragma once
#include "atlas_image.h"
#include "bitmap_font.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fonttool {

struct GlyphPicture {
    uint32_t m_uCp = 0;
    std::string m_strFile;        // where it came from (messages)
    atlas::Image m_oImg;
};

// How the pictures are arranged on the page.
enum class PageLayout {
    Row,           // side by side from (1, 1), 1 px apart, wrapping at maxWidth: the convert2fnt-web layout
    Tight,         // the atlas tool's packer (atlas::packPages, MaxRects): the smallest page, 1 px apart
    TightPow2,     // the same, the page's width and height powers of two
};
const char* f_PageLayoutName(PageLayout l_eL);              // "row", "tight", "pot"
bool f_ParsePageLayout(const std::string& l_strS, PageLayout& l_eOut);

struct BuildOptions {
    std::string m_strFace = "fontpack";   // info face="...": the font's name, a label only (nothing reads it)
    PageLayout m_eLayout = PageLayout::Row;
    int m_iMaxWidth = 4096;             // Row: a row wraps before passing this; Tight: the largest page side
    int m_iSpaceAdvance = -1;           // the space's width: -1 = auto (defaultSpaceAdvance); >= 0 wins over a space picture
    int m_iLineHeight = -1;             // the step down to the next line ('\n'): -1 = the tallest picture
    int m_iAdvancePad = 0;              // added to every glyph's xadvance (letter spacing)
};

// The auto values of BuildOptions::spaceAdvance and lineHeight for these pictures: the space is as wide as
// a space picture if there is one, else as the '1' picture, else 5; the line is as tall as the tallest picture.
int f_DefaultSpaceAdvance(const std::vector<GlyphPicture>& l_vGlyphs);
int f_DefaultLineHeight(const std::vector<GlyphPicture>& l_vGlyphs);

struct BuiltFont {
    atlas::Image m_oPage;
    std::string m_strFnt;         // the .fnt text
    BitmapFont m_oFont;         // the same, parsed
};

// The character a file name (without .png) stands for; 0 when it names none.
uint32_t f_CharFromName(const std::string& l_strStem);
// A file name (without .png) for a character, that charFromName reads back and Windows accepts.
std::string f_GlyphFileName(uint32_t l_uCp);
std::string f_Utf8Encode(uint32_t l_uCp);

// Loads one picture; its character comes from the file name (0 when the name is no character).
bool f_LoadPicture(const std::string& l_strPath, GlyphPicture& l_oOut, std::string& l_strErr);
// Every glyph of a finished font as a picture (a space or other blank glyph: a transparent picture as
// wide as its advance), in page order: open a .fnt, change it, build it again.
bool f_GlyphsFromFont(const std::string& l_strFntPath, std::vector<GlyphPicture>& l_vOut, std::string& l_strErr);
// What stops these pictures from building: one line per picture without a character, or with a
// character an earlier picture already has. Empty = fine.
std::vector<std::string> f_CheckGlyphs(const std::vector<GlyphPicture>& l_vGlyphs);

// Reads the single pictures of a folder (and its chars.txt). warnings: files that were skipped.
bool f_CollectGlyphs(const std::string& l_strFolder, std::vector<GlyphPicture>& l_vOut, std::vector<std::string>& l_vWarnings,
                   std::string& l_strErr);
// Packs the pictures into one page; pageFile is the PNG name the .fnt refers to. Fails when
// checkGlyphs finds a problem.
bool f_BuildFont(const std::vector<GlyphPicture>& l_vGlyphs, const BuildOptions& l_oOpt, const std::string& l_strPageFile,
               BuiltFont& l_oOut, std::string& l_strErr);
// Writes out.fnt to fntPath and its page next to it.
bool f_WriteFont(const BuiltFont& l_oFont, const std::string& l_strFntPath, std::string& l_strErr);

// Reads a .fnt and its (first) page.
bool f_LoadFont(const std::string& l_strFntPath, BitmapFont& l_oFont, atlas::Image& l_oPage, std::string& l_strErr);
// The reverse of building: every glyph of a font as a single picture + chars.txt in outDir.
bool f_SplitFont(const std::string& l_strFntPath, const std::string& l_strOutDir, int& l_iWritten, std::string& l_strErr);

// Draws text with the font (BitmapFont::layout, as the game lays it out) onto a new image:
// `pad` pixels around it, `background` RGBA under it (alpha 0: transparent).
atlas::Image f_RenderText(const BitmapFont& l_oFont, const atlas::Image& l_oPage, const std::string& l_strUtf8, float l_fScale = 1.0f,
                        int l_iPad = 8, const uint8_t l_arrBackground[4] = nullptr, int* l_pMissing = nullptr);

}  // namespace fonttool
