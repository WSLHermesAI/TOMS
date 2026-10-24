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

int g_failed = 0;

void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? "ok" : "FAILED", what.c_str());
    if (!ok) g_failed++;
}

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// A font written by hand: three 10x20 glyphs and an 'A' with offsets, kerning A->V.
const char* kSmall =
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
const char* kArial =
    "info face=\"Arial\" size=110 bold=0 italic=0 charset=\"\" unicode=0 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=2,2\r\n"
    "common lineHeight=110 base=110 scaleW=1104 scaleH=110 pages=1 packed=0\r\n"
    "page id=0 file=\"total_score.png\"\r\n"
    "chars count=3\r\n"
    "char id=49 x=76 y=0 width=76 height=110 xoffset=0 yoffset=0 xadvance=76 page=0 chnl=0 page=0 letter=\"1\"\r\n"
    "char id=107 x=852 y=0 width=84 height=110 xoffset=0 yoffset=0 xadvance=84 page=0 chnl=0 page=0 letter=\"k\"\r\n"
    "char id=32 x=1020 y=0 width=84 height=110 xoffset=0 yoffset=0 xadvance=84 page=0 chnl=0 page=0 letter=\"space\"";

void testRuntime() {
    BitmapFont f;
    std::string err;
    check(f.parse(kSmall, &err) && f.glyphs.size() == 5 && f.lineHeight == 20 && f.scaleW == 64, "runtime: parse " + err);
    check(f.pages.size() == 1 && f.pages[0] == "t.png" && f.face == "t", "runtime: page and face");
    check(f.glyph(' ') && f.glyph(' ')->xadvance == 6, "runtime: letter=\" \" (a quoted space) parses");
    check(f.glyph('a') == f.glyph('A') && f.glyph('a'), "runtime: a lower-case letter falls back to upper case");

    int missing = -1;
    auto p = f.layout("12 1", 100, 50, 1.0f, BitmapFont::Align::Left, &missing);
    check(p.size() == 3 && missing == 0, "runtime: '12 1' places 3 pictures (the space has none)");
    check(p.size() == 3 && near(p[0].x, 100) && near(p[1].x, 110) && near(p[2].x, 128) && near(p[0].y, 50),
          "runtime: pen advances 10, 12, space 6");
    p = f.layout("AV", 0, 0, 2.0f);
    check(p.size() == 2 && near(p[0].x, 2) && near(p[0].y, 8) && near(p[0].w, 16) && near(p[1].x, 14),
          "runtime: scale 2 doubles offsets and sizes; kerning A->V -3");
    p = f.layout("1#2", 0, 0, 1.0f, BitmapFont::Align::Left, &missing);
    check(p.size() == 2 && missing == 1 && near(p[1].x, 10), "runtime: a missing character is skipped and counted");
    p = f.layout("1\n22", 0, 0, 1.0f, BitmapFont::Align::Right);
    check(p.size() == 3 && near(p[0].x, -10) && near(p[1].x, -24) && near(p[1].y, 20), "runtime: newline + right align");
    p = f.layout("12", 100, 0, 1.0f, BitmapFont::Align::Center);
    check(p.size() == 2 && near(p[0].x, 89), "runtime: center align");
    float w = 0, h = 0;
    f.measure("12\n1", 1.0f, w, h);
    check(near(w, 22) && near(h, 40), "runtime: measure two lines");

    std::vector<Quad> quads;
    const float tint[4] = {1, 0.5f, 1, 1};
    f.appendQuads("2", 0, 0, 1.0f, BitmapFont::Align::Left, tint, 7, quads);
    check(quads.size() == 1 && near(quads[0].uv[0], 10.f / 64) && near(quads[0].uv[3], 20.f / 32) &&
              quads[0].texture == 7 && near(quads[0].tint[1], 0.5f),
          "runtime: quads carry page UVs, texture and tint");

    // UTF-8: a CJK glyph
    BitmapFont cjk;
    check(cjk.parse("common lineHeight=10\nchar id=20013 x=0 y=0 width=10 height=10 xadvance=10\n") &&
              cjk.layout(u8"中中", 0, 0).size() == 2,
          "runtime: UTF-8 text finds U+4E2D");

    BitmapFont arial;
    check(arial.parse(kArial, &err) && arial.glyphs.size() == 3 && arial.lineHeight == 110, "runtime: old BMFont layout (CRLF) parses");
    check(arial.glyph('K') && arial.glyph('K')->id == 'k', "runtime: 'K' finds the old fonts' lower-case k");
    check(arial.glyph(' ') && arial.glyph(' ')->x == 1020, "runtime: letter=\"space\" is the space glyph");
    BitmapFont bad;
    check(!bad.parse("hello\n", &err) && !err.empty(), "runtime: not a .fnt -> an error");
}

void testNames() {
    const uint32_t chars[] = {'0', '9', 'K', 'x', '.', ',', ' ', '/', ':', '*', '?', '"', '<', '|', '%', 0xD7, 0x4E2D, 0x01};
    bool ok = true;
    for (uint32_t c : chars) {
        const std::string n = glyphFileName(c);
        ok &= charFromName(n) == c;
        ok &= n.find_first_of("\\/:*?\"<>|") == std::string::npos && n != "." && !n.empty();
    }
    check(ok, "names: every glyphFileName reads back and is a valid Windows file name");
    check(charFromName("DOT") == '.' && charFromName("u+004b") == 'K' && charFromName("hello") == 0,
          "names: aliases ignore case; U+XXXX; an unknown word is no character");
}

void testRoundTrip(const fs::path& data, const fs::path& scratch) {
    // single pictures -> the convert2fnt-web font, exactly
    std::vector<GlyphPicture> glyphs;
    std::vector<std::string> warnings;
    std::string err;
    const bool collected = collectGlyphs(atlas::u8str(data / "connect_score_pictures"), glyphs, warnings, err);
    check(collected && warnings.empty() && glyphs.size() == 15, "build: tests/font/connect_score_pictures has 15 pictures " + err);
    BuildOptions opt;
    opt.face = "convert2fnt-web";
    BuiltFont built;
    check(buildFont(glyphs, opt, "connect_score.png", built, err), "build: builds " + err);
    const std::string expected = readText(data / "connect_score.fnt");
    check(!expected.empty() && built.fnt == expected, "build: the .fnt equals connect_score.fnt byte for byte");
    if (built.fnt != expected) std::printf("--- built:\n%s\n--- expected:\n%s\n", built.fnt.c_str(), expected.c_str());
    atlas::Image ref;
    check(atlas::loadImage(atlas::u8str(data / "connect_score.png"), ref, &err) && ref.w == built.page.w &&
              ref.h == built.page.h && atlas::samePixels(ref, {0, 0, ref.w, ref.h}, built.page, {0, 0, ref.w, ref.h}),
          "build: the page equals connect_score.png pixel for pixel");

    // write -> split -> collect -> build: the same font again
    const fs::path out = scratch / "roundtrip" / "connect.fnt";
    check(writeFont(built, atlas::u8str(out), err) && fs::exists(scratch / "roundtrip" / "connect_score.png"),
          "write: .fnt + the page it names");
    int n = 0;
    check(splitFont(atlas::u8str(out), atlas::u8str(scratch / "split"), n, err) && n == 15, "split: 15 pictures " + err);
    std::vector<GlyphPicture> again;
    warnings.clear();
    BuiltFont rebuilt;
    check(collectGlyphs(atlas::u8str(scratch / "split"), again, warnings, err) &&
              buildFont(again, opt, "connect_score.png", rebuilt, err) && rebuilt.fnt == expected,
          "split + build: the same .fnt again");
}

// The space's width and the line height: auto values, and set by hand.
void testSpaceAndLines(const fs::path& data, const fs::path& scratch) {
    std::vector<GlyphPicture> all;
    std::vector<std::string> warnings;
    std::string err;
    collectGlyphs(atlas::u8str(data / "connect_score_pictures"), all, warnings, err);
    std::vector<GlyphPicture> noSpace, noOne;
    for (const GlyphPicture& g : all) {
        if (g.cp != ' ') noSpace.push_back(g);
        if (g.cp != ' ' && g.cp != '1') noOne.push_back(g);
    }
    BuiltFont b;
    check(defaultSpaceAdvance(all) == 86 && buildFont(all, {}, "s.png", b, err) && b.font.glyph(' ')->xadvance == 86,
          "space: a space picture gives its width (86)");
    check(defaultSpaceAdvance(noSpace) == 72 && buildFont(noSpace, {}, "s.png", b, err) && b.font.glyph(' ')->xadvance == 72,
          "space: no space picture -> as wide as the 1 picture (72)");
    check(defaultSpaceAdvance(noOne) == 5 && buildFont(noOne, {}, "s.png", b, err) && b.font.glyph(' ')->xadvance == 5,
          "space: no space and no 1 picture -> 5");
    BuildOptions opt;
    opt.spaceAdvance = 40;
    check(buildFont(all, opt, "s.png", b, err) && b.font.glyph(' ')->xadvance == 40,
          "space: a width set by hand wins over the space picture");

    check(defaultLineHeight(all) == 94 && buildFont(all, {}, "s.png", b, err) && b.font.lineHeight == 94,
          "line height: auto = the tallest picture (94)");
    opt = BuildOptions();
    opt.lineHeight = 130;
    check(buildFont(all, opt, "s.png", b, err) && b.font.lineHeight == 130 && b.font.base == 94 &&
              b.font.glyph('1')->yoffset == 0,
          "line height: set to 130, the glyphs stay on the tallest picture's baseline");
    const auto placed = b.font.layout("1\n1", 0, 0);
    check(placed.size() == 2 && near(placed[1].y, 130), "line height: the second line starts 130 lower");
    float w = 0, h = 0;
    b.font.measure("1\n1", 1.0f, w, h);
    check(near(h, 260), "line height: two lines measure 260");
    // save, open again as pictures: the blank space keeps its width and does not grow the tallest picture
    const fs::path fnt = scratch / "lines" / "lines.fnt";
    std::vector<GlyphPicture> back;
    check(writeFont(b, atlas::u8str(fnt), err) && glyphsFromFont(atlas::u8str(fnt), back, err) &&
              defaultLineHeight(back) == 94 && defaultSpaceAdvance(back) == 86,
          "line height: a saved font opens back with the same pictures (tallest 94, space 86)");
}

// The Tight layouts (the atlas tool's packer): every glyph keeps its pixels, none overlap, a power-of-two
// page when asked, and text draws exactly as with the one-row layout.
void testLayouts(const fs::path& data) {
    std::vector<GlyphPicture> pics;
    std::vector<std::string> warnings;
    std::string err;
    collectGlyphs(atlas::u8str(data / "connect_score_pictures"), pics, warnings, err);
    BitmapFont rowFont;
    atlas::Image rowPage;
    loadFont(atlas::u8str(data / "connect_score.fnt"), rowFont, rowPage, err);
    const std::string text = "12,345.6K\n9,999M 0";
    const atlas::Image rowText = renderText(rowFont, rowPage, text, 0.5f, 8);

    for (PageLayout layout : {PageLayout::Tight, PageLayout::TightPow2}) {
        const std::string name = std::string("layout ") + pageLayoutName(layout) + ": ";
        BuildOptions opt;
        opt.layout = layout;
        BuiltFont b;
        if (!buildFont(pics, opt, "t.png", b, err)) {
            check(false, name + "builds " + err);
            continue;
        }
        bool inside = true, apart = true, samePixels = true;
        const auto& gs = b.font.glyphs;
        for (size_t i = 0; i < gs.size(); i++) {
            if (gs[i].w == 0) continue;
            const atlas::IRect r{gs[i].x, gs[i].y, gs[i].w, gs[i].h};
            inside &= r.x >= 1 && r.y >= 1 && r.right() <= b.page.w && r.bottom() <= b.page.h;
            for (size_t j = i + 1; j < gs.size(); j++)
                if (gs[j].w) apart &= !r.intersects({gs[j].x - 1, gs[j].y - 1, gs[j].w + 2, gs[j].h + 2});
            samePixels &= atlas::samePixels(b.page, r, pics[i].img, {0, 0, pics[i].img.w, pics[i].img.h});
        }
        check(inside && apart && samePixels, name + "every picture on the page, 1 px apart, pixels kept");
        const bool pot = (b.page.w & (b.page.w - 1)) == 0 && (b.page.h & (b.page.h - 1)) == 0;
        if (layout == PageLayout::TightPow2)
            check(pot, name + "page " + std::to_string(b.page.w) + "x" + std::to_string(b.page.h) + " is a power of 2");
        else
            // the packer keeps 1 px clear on every side (the row only on the top and left); glyphs of one
            // height are already best in one row, so tight = the row + that border here
            check((long long)b.page.w * b.page.h <= (long long)(rowPage.w + 1) * (rowPage.h + 1),
                  name + "page " + std::to_string(b.page.w) + "x" + std::to_string(b.page.h) +
                      " no larger than the one row (" + std::to_string(rowPage.w) + "x" + std::to_string(rowPage.h) +
                      ") with its border");
        const atlas::Image t = renderText(b.font, b.page, text, 0.5f, 8);
        check(t.w == rowText.w && t.h == rowText.h && atlas::samePixels(t, {0, 0, t.w, t.h}, rowText, {0, 0, t.w, t.h}),
              name + "the test text draws exactly as with one row");
    }
    PageLayout l;
    check(parsePageLayout("pot", l) && l == PageLayout::TightPow2 && parsePageLayout("Tight", l) &&
              l == PageLayout::Tight && !parsePageLayout("grid", l),
          "layout: names parse");
    BuildOptions small;
    small.layout = PageLayout::TightPow2;
    small.maxWidth = 128;
    BuiltFont b;
    check(!buildFont(pics, small, "t.png", b, err) && err.find("do not fit") != std::string::npos,
          "layout: a page too small for the pictures is an error");
}

// Every font in tests/font (the old project's: BMFont "Arial" and convert2fnt-web) loads, draws its
// digits, and opens back into pictures that build again.
void testAllFonts(const fs::path& data) {
    int fonts = 0;
    for (const auto& de : fs::directory_iterator(data)) {
        if (de.path().extension() != ".fnt") continue;
        fonts++;
        const std::string name = atlas::u8str(de.path().filename());
        BitmapFont font;
        atlas::Image page;
        std::string err;
        int missing = -1;
        const bool loaded = loadFont(atlas::u8str(de.path()), font, page, err);
        const atlas::Image img = loaded ? renderText(font, page, "0123456789", 0.5f, 4, nullptr, &missing) : atlas::Image();
        check(loaded && missing == 0 && img.valid() && !atlas::opaqueBounds(img).empty(),
              "all fonts: " + name + " loads and draws 0-9 " + err);
        std::vector<GlyphPicture> pics;
        BuiltFont b;
        check(glyphsFromFont(atlas::u8str(de.path()), pics, err) && buildFont(pics, {}, "x.png", b, err) &&
                  b.font.glyphs.size() == font.glyphs.size(),
              "all fonts: " + name + " opens as pictures and builds again " + err);
    }
    check(fonts >= 1, "all fonts: tests/font has .fnt files");
}

// The editor's way: pictures added one by one, characters set by hand, a font opened back into pictures.
void testPictures(const fs::path& data, const fs::path& scratch) {
    std::vector<GlyphPicture> pics;
    std::string err;
    for (const char* name : {"0.png", "dot.png", "K.png"}) {
        GlyphPicture g;
        check(loadPicture(atlas::u8str(data / "connect_score_pictures" / name), g, err), std::string("pictures: load ") + name + " " + err);
        pics.push_back(std::move(g));
    }
    check(pics.size() == 3 && pics[0].cp == '0' && pics[1].cp == '.' && pics[2].cp == 'K',
          "pictures: the character is guessed from the file name");
    GlyphPicture unnamed;
    loadPicture(atlas::u8str(data / "connect_score_pictures" / "1.png"), unnamed, err);
    unnamed.cp = 0;
    unnamed.file = "my_glyph.png";
    pics.push_back(unnamed);
    std::vector<std::string> problems = checkGlyphs(pics);
    BuiltFont b;
    check(problems.size() == 1 && problems[0].find("my_glyph.png") != std::string::npos && !buildFont(pics, {}, "p.png", b, err),
          "pictures: one without a character is a problem, and the build refuses");
    pics.back().cp = '0';
    problems = checkGlyphs(pics);
    check(problems.size() == 1 && problems[0].find("already given") != std::string::npos,
          "pictures: a character given twice is a problem");
    pics.back().cp = '1';
    check(checkGlyphs(pics).empty() && buildFont(pics, {}, "p.png", b, err) && b.font.glyphs.size() == 5 &&
              b.font.glyphs[0].id == '0' && b.font.glyphs[3].id == '1',
          "pictures: set by hand, it builds in the order given (+ space)");

    std::vector<GlyphPicture> back;
    check(glyphsFromFont(atlas::u8str(data / "connect_score.fnt"), back, err) && back.size() == 15 && back[0].cp == '0' &&
              back[14].cp == ' ' && back[14].img.w == 86,
          "pictures: a font opens back into its 15 pictures (the space as a blank one as wide as its advance)");
    BuildOptions opt;
    opt.face = "convert2fnt-web";
    check(buildFont(back, opt, "connect_score.png", b, err) && b.fnt == readText(data / "connect_score.fnt"),
          "pictures: and builds the same font again");
    (void)scratch;
}

atlas::Image solid(int w, int h, uint8_t r) {
    atlas::Image img(w, h);
    for (size_t i = 0; i < img.px.size(); i += 4) { img.px[i] = r; img.px[i + 3] = 255; }
    return img;
}

void testOptions(const fs::path& scratch) {
    // K and k, '.', a CJK character and a different height; wrapping; spacing; chars.txt
    std::vector<GlyphPicture> g = {{'K', "K", solid(20, 30, 1)}, {'k', "k", solid(18, 30, 2)},
                                   {'.', "dot", solid(8, 10, 3)}, {0x4E2D, "zh", solid(30, 30, 4)}};
    BuildOptions opt;
    opt.maxWidth = 50;
    opt.advancePad = 2;
    opt.spaceAdvance = 12;
    BuiltFont b;
    std::string err;
    check(buildFont(g, opt, "w.png", b, err), "options: build " + err);
    const BitmapGlyph* dot = b.font.glyph('.');
    check(dot && dot->yoffset == 20 && dot->h == 10, "options: a shorter picture sits on the bottom of the line");
    check(b.font.glyph(0x4E2D) && b.font.glyph(0x4E2D)->y > 1 && b.page.w <= 50, "options: a row wraps at --max-width");
    check(b.font.glyph('K')->xadvance == 22 && b.font.glyph(' ')->xadvance == 14,
          "options: letter spacing pads every advance; --space sets the space");
    check(b.font.glyph('K') != b.font.glyph('k'), "options: K and k stay two glyphs");

    const fs::path fnt = scratch / "cases" / "cases.fnt";
    int n = 0;
    check(writeFont(b, atlas::u8str(fnt), err) && splitFont(atlas::u8str(fnt), atlas::u8str(scratch / "cases_split"), n, err),
          "options: write + split " + err);
    std::vector<GlyphPicture> back;
    std::vector<std::string> warnings;
    collectGlyphs(atlas::u8str(scratch / "cases_split"), back, warnings, err);
    bool both = false;
    for (const auto& a : back) both |= a.cp == 'k';
    check(back.size() == 5 && both && warnings.empty(), "options: K and k survive split on a case-blind file system");

    // a folder with a name that is no character, and chars.txt mapping one
    const fs::path dir = scratch / "named";
    fs::remove_all(dir);
    fs::create_directories(dir);
    atlas::savePng(atlas::u8str(dir / "seven.png"), solid(10, 10, 9));
    atlas::savePng(atlas::u8str(dir / "big K.png"), solid(10, 10, 9));
    atlas::savePng(atlas::u8str(dir / "1.png"), solid(10, 10, 9));
    { std::ofstream(dir / "chars.txt") << "# test\nseven=7\nbig K=K\n"; }
    std::vector<GlyphPicture> named;
    warnings.clear();
    check(collectGlyphs(atlas::u8str(dir), named, warnings, err) && named.size() == 3 && named[0].cp == '7' &&
              named[1].cp == 'K' && named[2].cp == '1',
          "folder: chars.txt maps names and sets the order (unlisted files after)");
    { std::ofstream(dir / "chars.txt") << "seven=7\n"; }
    warnings.clear();
    collectGlyphs(atlas::u8str(dir), named, warnings, err);
    check(named.size() == 2 && warnings.size() == 1 && warnings[0].find("big K.png") != std::string::npos,
          "folder: a file whose name is no character is skipped with a warning");
}

void testPreview(const fs::path& data, const fs::path& scratch) {
    BitmapFont font;
    atlas::Image page;
    std::string err;
    check(loadFont(atlas::u8str(data / "connect_score.fnt"), font, page, err), "preview: loads connect_score " + err);
    int missing = -1;
    const uint8_t bg[4] = {15, 15, 26, 255};   // kBackgroundClearColor
    const atlas::Image img = renderText(font, page, "12,345.6K\n9,999M 0", 0.5f, 8, bg, &missing);
    check(missing == 0 && img.valid(), "preview: the test text draws, every character in the font");
    const fs::path golden = data / "preview.png";
    const char* update = std::getenv("TOMS_UPDATE_GOLDEN");
    if (update && *update && std::string(update) != "0") {
        atlas::savePng(atlas::u8str(golden), img, &err);
        std::printf("[update] wrote %s\n", atlas::u8str(golden).c_str());
    }
    atlas::Image ref;
    const bool same = atlas::loadImage(atlas::u8str(golden), ref, &err) && ref.w == img.w && ref.h == img.h &&
                      atlas::samePixels(ref, {0, 0, ref.w, ref.h}, img, {0, 0, img.w, img.h});
    if (!same) {
        atlas::savePng(atlas::u8str(scratch / "preview.png"), img, &err);
        std::printf("  got %s (%dx%d), reference %dx%d\n", atlas::u8str(scratch / "preview.png").c_str(), img.w, img.h,
                    ref.w, ref.h);
    }
    check(same, "preview: equals tests/font/preview.png");
}

// Records what FontText asks of the renderer.
struct FakeRenderer : IRenderer {
    int textures = 0, released = 0;
    uint32_t texW = 0, texH = 0;
    std::vector<Quad> drawn;
    void init(uint32_t, uint32_t) override {}
    void loadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
    void loadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
    uint16_t loadTexture(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) override {
        texW = w; texH = h;
        return rgba.size() == (size_t)w * h * 4 ? (uint16_t)(++textures) : kSpriteAtlasTexture;
    }
    void releaseTexture(uint16_t) override { released++; }
    void begin() override {}
    void drawSprite(const Quad& q) override { drawn.push_back(q); }
    void end() override {}
    uint32_t width() const override { return 1280; }
    uint32_t height() const override { return 720; }
};

void testFontText(const fs::path& data) {
    FakeRenderer r;
    FontText score;
    std::string err;
    check(score.load(r, atlas::u8str(data / "connect_score.fnt"), &err) && r.textures == 1 && r.texW == 978 &&
              r.texH == 95, "FontText: load reads the .fnt and uploads its page " + err);
    score.render();
    check(r.drawn.empty(), "FontText: no text, nothing drawn");

    score.setText("1,234K");
    score.setPosition(900, 40);
    score.setAlign(BitmapFont::Align::Right);
    score.render();
    // 1 2 3 4 = 72 each, ',' 36, K 86: 410 wide, right edge at 900
    check(r.drawn.size() == 6 && near(r.drawn[0].rect[0], 490) && near(r.drawn[0].rect[1], 40) &&
              r.drawn[0].texture == 1 && near(score.width(), 410) && near(score.height(), 94),
          "FontText: setText + setPosition + right align -> 6 quads ending at x 900");
    r.drawn.clear();
    score.setScale(0.5f);
    score.setColor(1, 0, 0, 0.5f);
    score.render();
    check(r.drawn.size() == 6 && near(r.drawn[0].rect[0], 695) && near(r.drawn[0].rect[2], 36) &&
              near(r.drawn[0].tint[1], 0) && near(r.drawn[0].tint[3], 0.5f),
          "FontText: scale and colour apply on the next render");

    FontText copy = score;   // shares the page
    copy.setText("99");
    copy.setAlign(BitmapFont::Align::Left);
    copy.setPosition(0, 0);
    r.drawn.clear();
    copy.render();
    check(r.drawn.size() == 2 && r.textures == 1 && r.drawn[0].texture == 1 && near(r.drawn[0].rect[0], 0),
          "FontText: a copy draws its own text from the same texture");
    copy.setText("9#");
    check(copy.missingChars() == 1, "FontText: missingChars counts what the font lacks");
    r.drawn.clear();
    score.setVisible(false);
    score.render();
    check(r.drawn.empty(), "FontText: setVisible(false) hides it");
    score.release();
    check(r.released == 1 && !score.loaded(), "FontText: release frees the page");

    FontText bad;
    check(!bad.load(r, atlas::u8str(data / "nope.fnt"), &err) && !err.empty() && !bad.loaded(),
          "FontText: a missing file -> false and an error");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: font_tool_test <tests/font> <scratch dir>\n");
        return 3;
    }
    const fs::path data = atlas::u8path(argv[1]);
    const fs::path scratch = atlas::u8path(argv[2]);
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch, ec);

    testRuntime();
    testNames();
    testRoundTrip(data, scratch);
    testOptions(scratch);
    testPictures(data, scratch);
    testAllFonts(data);
    testLayouts(data);
    testSpaceAndLines(data, scratch);
    testPreview(data, scratch);
    testFontText(data);
    std::printf("font_tool_test: %d failed\n", g_failed);
    return g_failed ? 1 : 0;
}
