// font_build.h -- picture fonts from single pictures (docs/22_FONT_TOOL.md).
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
    uint32_t cp = 0;
    std::string file;        // where it came from (messages)
    atlas::Image img;
};

// How the pictures are arranged on the page.
enum class PageLayout {
    Row,           // side by side from (1, 1), 1 px apart, wrapping at maxWidth: the convert2fnt-web layout
    Tight,         // the atlas tool's packer (atlas::packPages, MaxRects): the smallest page, 1 px apart
    TightPow2,     // the same, the page's width and height powers of two
};
const char* pageLayoutName(PageLayout l);              // "row", "tight", "pot"
bool parsePageLayout(const std::string& s, PageLayout& out);

struct BuildOptions {
    std::string face = "fontpack";   // info face="...": the font's name, a label only (nothing reads it)
    PageLayout layout = PageLayout::Row;
    int maxWidth = 4096;             // Row: a row wraps before passing this; Tight: the largest page side
    int spaceAdvance = -1;           // the space's width: -1 = auto (defaultSpaceAdvance); >= 0 wins over a space picture
    int lineHeight = -1;             // the step down to the next line ('\n'): -1 = the tallest picture
    int advancePad = 0;              // added to every glyph's xadvance (letter spacing)
};

// The auto values of BuildOptions::spaceAdvance and lineHeight for these pictures: the space is as wide as
// a space picture if there is one, else as the '1' picture, else 5; the line is as tall as the tallest picture.
int defaultSpaceAdvance(const std::vector<GlyphPicture>& glyphs);
int defaultLineHeight(const std::vector<GlyphPicture>& glyphs);

struct BuiltFont {
    atlas::Image page;
    std::string fnt;         // the .fnt text
    BitmapFont font;         // the same, parsed
};

// The character a file name (without .png) stands for; 0 when it names none.
uint32_t charFromName(const std::string& stem);
// A file name (without .png) for a character, that charFromName reads back and Windows accepts.
std::string glyphFileName(uint32_t cp);
std::string utf8Encode(uint32_t cp);

// Loads one picture; its character comes from the file name (0 when the name is no character).
bool loadPicture(const std::string& path, GlyphPicture& out, std::string& err);
// Every glyph of a finished font as a picture (a space or other blank glyph: a transparent picture as
// wide as its advance), in page order: open a .fnt, change it, build it again.
bool glyphsFromFont(const std::string& fntPath, std::vector<GlyphPicture>& out, std::string& err);
// What stops these pictures from building: one line per picture without a character, or with a
// character an earlier picture already has. Empty = fine.
std::vector<std::string> checkGlyphs(const std::vector<GlyphPicture>& glyphs);

// Reads the single pictures of a folder (and its chars.txt). warnings: files that were skipped.
bool collectGlyphs(const std::string& folder, std::vector<GlyphPicture>& out, std::vector<std::string>& warnings,
                   std::string& err);
// Packs the pictures into one page; pageFile is the PNG name the .fnt refers to. Fails when
// checkGlyphs finds a problem.
bool buildFont(const std::vector<GlyphPicture>& glyphs, const BuildOptions& opt, const std::string& pageFile,
               BuiltFont& out, std::string& err);
// Writes out.fnt to fntPath and its page next to it.
bool writeFont(const BuiltFont& font, const std::string& fntPath, std::string& err);

// Reads a .fnt and its (first) page.
bool loadFont(const std::string& fntPath, BitmapFont& font, atlas::Image& page, std::string& err);
// The reverse of building: every glyph of a font as a single picture + chars.txt in outDir.
bool splitFont(const std::string& fntPath, const std::string& outDir, int& written, std::string& err);

// Draws text with the font (BitmapFont::layout, as the game lays it out) onto a new image:
// `pad` pixels around it, `background` RGBA under it (alpha 0: transparent).
atlas::Image renderText(const BitmapFont& font, const atlas::Image& page, const std::string& utf8, float scale = 1.0f,
                        int pad = 8, const uint8_t background[4] = nullptr, int* missing = nullptr);

}  // namespace fonttool
