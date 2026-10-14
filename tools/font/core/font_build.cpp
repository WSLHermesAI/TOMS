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
const std::pair<const char*, uint32_t> kNames[] = {
    {"space", ' '},     {"dot", '.'},        {"period", '.'},     {"comma", ','},      {"colon", ':'},
    {"semicolon", ';'}, {"plus", '+'},       {"minus", '-'},      {"dash", '-'},       {"hyphen", '-'},
    {"percent", '%'},   {"slash", '/'},      {"backslash", '\\'}, {"star", '*'},       {"asterisk", '*'},
    {"dollar", '$'},    {"hash", '#'},       {"at", '@'},         {"question", '?'},   {"exclam", '!'},
    {"quote", '"'},     {"apostrophe", '\''}, {"lparen", '('},    {"rparen", ')'},     {"lbracket", '['},
    {"rbracket", ']'},  {"equal", '='},      {"equals", '='},     {"underscore", '_'}, {"less", '<'},
    {"greater", '>'},   {"pipe", '|'},       {"tilde", '~'},      {"amp", '&'},        {"caret", '^'},
    {"times", 0xD7},    {"yen", 0xA5},       {"euro", 0x20AC},    {"pound", 0xA3},
};

std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

// "x" -> 'x', "space" -> ' ', "U+00D7" -> 0xD7; also a bare " " (chars.txt `name= `).
uint32_t parseChar(const std::string& s) {
    if (s.empty()) return 0;
    size_t i = 0;
    const uint32_t cp = utf8Next(s, i);
    if (i == s.size() && cp != 0xFFFD) return cp;   // exactly one character
    return charFromName(s);
}

bool readText(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool transparent(const atlas::Image& img) { return atlas::opaqueBounds(img).empty(); }

}  // namespace

const char* pageLayoutName(PageLayout l) {
    switch (l) {
    case PageLayout::Tight: return "tight";
    case PageLayout::TightPow2: return "pot";
    default: return "row";
    }
}

bool parsePageLayout(const std::string& s, PageLayout& out) {
    const std::string l = lower(s);
    if (l == "row") out = PageLayout::Row;
    else if (l == "tight") out = PageLayout::Tight;
    else if (l == "pot" || l == "pow2" || l == "tight-pot") out = PageLayout::TightPow2;
    else return false;
    return true;
}

std::string utf8Encode(uint32_t cp) {
    std::string s;
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F));
    }
    return s;
}

uint32_t charFromName(const std::string& stem) {
    if (stem.empty()) return 0;
    size_t i = 0;
    const uint32_t cp = utf8Next(stem, i);
    if (i == stem.size() && cp != 0xFFFD) return cp;
    const std::string l = lower(stem);
    for (const auto& n : kNames)
        if (l == n.first) return n.second;
    if (l.size() > 2 && l[0] == 'u' && l[1] == '+') {
        char* end = nullptr;
        const unsigned long v = std::strtoul(l.c_str() + 2, &end, 16);
        if (*end == 0 && v > 0 && v <= 0x10FFFF) return (uint32_t)v;
    }
    return 0;
}

std::string glyphFileName(uint32_t cp) {
    for (const auto& n : kNames)
        if (n.second == cp) return n.first;
    if ((cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp >= 0xA0)
        return utf8Encode(cp);
    char buf[16];
    std::snprintf(buf, sizeof buf, "U+%04X", (unsigned)cp);
    return buf;
}

bool loadPicture(const std::string& path, GlyphPicture& out, std::string& err) {
    const fs::path p = atlas::u8path(path);
    out = GlyphPicture();
    out.file = atlas::u8str(p.filename());
    out.cp = charFromName(atlas::u8str(p.stem()));
    return atlas::loadImage(path, out.img, &err);
}

bool glyphsFromFont(const std::string& fntPath, std::vector<GlyphPicture>& out, std::string& err) {
    out.clear();
    BitmapFont font;
    atlas::Image page;
    if (!loadFont(fntPath, font, page, err)) return false;
    const std::string name = atlas::u8str(atlas::u8path(fntPath).filename());
    // a blank glyph (a space) becomes a transparent picture as tall as the tallest one, so it changes nothing
    // (lineHeight may be set larger than the pictures)
    int tallest = 1;
    for (const BitmapGlyph& g : font.glyphs) tallest = std::max(tallest, g.h);
    for (const BitmapGlyph& g : font.glyphs) {
        GlyphPicture p;
        p.cp = g.id;
        p.file = name + " '" + (g.id == ' ' ? std::string("space") : utf8Encode(g.id)) + "'";
        if (g.w > 0 && g.h > 0) p.img = atlas::crop(page, {g.x, g.y, g.w, g.h});
        else p.img = atlas::Image(std::max(1, g.xadvance), tallest);
        out.push_back(std::move(p));
    }
    return true;
}

int defaultSpaceAdvance(const std::vector<GlyphPicture>& glyphs) {
    for (const GlyphPicture& g : glyphs)
        if (g.cp == ' ' && g.img.valid()) return g.img.w;
    for (const GlyphPicture& g : glyphs)
        if (g.cp == '1' && g.img.valid()) return g.img.w;
    return 5;
}

int defaultLineHeight(const std::vector<GlyphPicture>& glyphs) {
    int h = 0;
    for (const GlyphPicture& g : glyphs)
        if (g.img.valid()) h = std::max(h, g.img.h);
    return h;
}

std::vector<std::string> checkGlyphs(const std::vector<GlyphPicture>& glyphs) {
    std::vector<std::string> problems;
    std::map<uint32_t, std::string> seen;
    for (const GlyphPicture& g : glyphs) {
        if (!g.cp) {
            problems.push_back(g.file + ": no character given");
            continue;
        }
        auto it = seen.find(g.cp);
        if (it != seen.end())
            problems.push_back(g.file + ": '" + utf8Encode(g.cp) + "' is already given to " + it->second);
        else
            seen[g.cp] = g.file;
    }
    return problems;
}

bool collectGlyphs(const std::string& folder, std::vector<GlyphPicture>& out, std::vector<std::string>& warnings,
                   std::string& err) {
    out.clear();
    const fs::path dir = atlas::u8path(folder);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        err = "not a folder: " + folder;
        return false;
    }
    // chars.txt: file stem -> character, in the order the glyphs go on the page
    std::map<std::string, uint32_t> named;   // lower-case stem -> cp
    std::map<std::string, int> order;        // lower-case stem -> position
    std::string text;
    if (readText(dir / "chars.txt", text)) {
        std::istringstream in(text);
        std::string line;
        int lineNo = 0;
        while (std::getline(in, line)) {
            lineNo++;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (trim(line).empty() || trim(line)[0] == '#') continue;
            const size_t eq = line.find('=');
            const std::string stem = eq == std::string::npos ? std::string() : lower(trim(line.substr(0, eq)));
            // the value is not trimmed when it is one space ("space= " would be odd, but allowed)
            std::string value = eq == std::string::npos ? std::string() : line.substr(eq + 1);
            if (value != " ") value = trim(value);
            const uint32_t cp = parseChar(value);
            if (stem.empty() || !cp) {
                warnings.push_back("chars.txt line " + std::to_string(lineNo) + ": expected file=char, got '" + line + "'");
                continue;
            }
            named[stem] = cp;
            order.emplace(stem, (int)order.size());
        }
    }

    struct Entry { GlyphPicture g; int order; };
    std::vector<Entry> entries;
    std::map<uint32_t, std::string> seen;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        if (!de.is_regular_file()) continue;
        const fs::path p = de.path();
        if (lower(atlas::u8str(p.extension())) != ".png") continue;
        const std::string stem = atlas::u8str(p.stem());
        const std::string file = atlas::u8str(p.filename());
        auto it = named.find(lower(stem));
        const uint32_t cp = it != named.end() ? it->second : charFromName(stem);
        if (!cp) {
            warnings.push_back(file + ": the name is not a character (rename it, or map it in chars.txt)");
            continue;
        }
        if (seen.count(cp)) {
            warnings.push_back(file + ": '" + utf8Encode(cp) + "' already comes from " + seen[cp] + "; skipped");
            continue;
        }
        Entry e;
        e.g.cp = cp;
        e.g.file = file;
        std::string loadErr;
        if (!atlas::loadImage(atlas::u8str(p), e.g.img, &loadErr)) {
            warnings.push_back(file + ": " + loadErr);
            continue;
        }
        auto o = order.find(lower(stem));
        e.order = o != order.end() ? o->second : 1 << 30;
        seen[cp] = file;
        entries.push_back(std::move(e));
    }
    // chars.txt order first, then by code point
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.order != b.order ? a.order < b.order : a.g.cp < b.g.cp;
    });
    for (auto& e : entries) out.push_back(std::move(e.g));
    if (out.empty()) {
        err = "no character pictures in " + folder;
        return false;
    }
    return true;
}

bool buildFont(const std::vector<GlyphPicture>& glyphs, const BuildOptions& opt, const std::string& pageFile,
               BuiltFont& out, std::string& err) {
    out = BuiltFont();
    if (glyphs.empty()) {
        err = "no glyphs";
        return false;
    }
    const std::vector<std::string> problems = checkGlyphs(glyphs);
    if (!problems.empty()) {
        err = problems[0] + (problems.size() > 1 ? " (+" + std::to_string(problems.size() - 1) + " more)" : "");
        return false;
    }
    struct Slot { uint32_t cp; int x, y, w, h, yoffset, xadvance; const atlas::Image* img; };
    std::vector<Slot> slots;
    bool haveSpace = false;
    for (const GlyphPicture& g : glyphs) {
        if (!g.img.valid()) {
            err = g.file + ": empty picture";
            return false;
        }
        haveSpace |= g.cp == ' ';
    }
    // glyphs sit on the bottom of the tallest picture; the next line starts lineHeight lower
    const int base = defaultLineHeight(glyphs);
    const int lineHeight = opt.lineHeight > 0 ? opt.lineHeight : base;
    const int spaceAdvance = opt.spaceAdvance >= 0 ? opt.spaceAdvance : defaultSpaceAdvance(glyphs);
    for (const GlyphPicture& g : glyphs) {
        Slot s{g.cp, 0, 0, 0, 0, 0, g.img.w + opt.advancePad, &g.img};
        if (g.cp == ' ') s.xadvance = spaceAdvance + opt.advancePad;
        if (!transparent(g.img)) {   // a blank picture (a space) only gives the advance
            s.w = g.img.w;
            s.h = g.img.h;
            s.yoffset = base - g.img.h;   // bottom aligned
        } else {
            s.img = nullptr;
        }
        slots.push_back(s);
    }
    if (!haveSpace) slots.push_back({' ', 0, 0, 0, 0, 0, spaceAdvance + opt.advancePad, nullptr});

    int pageW = 0, pageH = 0;
    if (opt.layout == PageLayout::Row) {
        // a row of pictures 1 px apart from (1, 1), wrapping at maxWidth
        int x = 1, y = 1, rowH = 0;
        for (Slot& s : slots) {
            if (!s.img) continue;
            if (x > 1 && x + s.w > opt.maxWidth) { x = 1; y += rowH + 1; rowH = 0; }
            s.x = x;
            s.y = y;
            x += s.w + 1;
            rowH = std::max(rowH, s.h);
            pageW = std::max(pageW, s.x + s.w);
            pageH = std::max(pageH, s.y + s.h);
        }
    } else {
        // the atlas tool's packer: the smallest page (power of two sides if asked), 1 px border and gaps
        std::vector<atlas::PackItem> items;
        std::vector<Slot*> packed;
        for (Slot& s : slots)
            if (s.img) {
                atlas::PackItem it;
                it.w = s.w;
                it.h = s.h;
                items.push_back(it);
                packed.push_back(&s);
            }
        atlas::PackSettings ps;
        ps.maxWidth = ps.maxHeight = opt.maxWidth;
        ps.powerOfTwo = opt.layout == PageLayout::TightPow2;
        ps.border = 1;
        ps.padding = 1;
        if (!items.empty()) {
            const atlas::PackResult pr = atlas::packPages(items, ps);
            if (!pr.errors.empty() || pr.pageSizes.size() != 1) {
                err = "the pictures do not fit on one " + std::to_string(opt.maxWidth) + "x" +
                      std::to_string(opt.maxWidth) + " page" + (pr.errors.empty() ? std::string() : ": " + pr.errors[0]);
                return false;
            }
            for (size_t i = 0; i < packed.size(); i++) {
                packed[i]->x = pr.placements[i].x;
                packed[i]->y = pr.placements[i].y;
            }
            pageW = pr.pageSizes[0].w;
            pageH = pr.pageSizes[0].h;
        }
    }
    if (pageW <= 0 || pageH <= 0) {
        err = "every picture is transparent";
        return false;
    }

    out.page = atlas::Image(pageW, pageH);
    for (const Slot& s : slots)
        if (s.img) atlas::blit(out.page, *s.img, {0, 0, s.w, s.h}, s.x, s.y);

    // The convert2fnt-web layout (tests/font/connect_score.fnt): LF, no final newline.
    std::ostringstream f;
    f << "info face=\"" << opt.face << "\" size=" << base
      << " bold=0 italic=0 charset=\"\" unicode=1 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=2,2 outline=0\n";
    f << "common lineHeight=" << lineHeight << " base=" << base << " scaleW=" << pageW << " scaleH=" << pageH
      << " pages=1 packed=0 alphaChnl=0 redChnl=0 greenChnl=0 blueChnl=0\n";
    f << "page id=0 file=\"" << pageFile << "\"\n";
    f << "chars count=" << slots.size() << "\n";
    for (const Slot& s : slots)
        f << "char id=" << s.cp << " x=" << s.x << " y=" << s.y << " width=" << s.w << " height=" << s.h
          << " xoffset=0 yoffset=" << s.yoffset << " xadvance=" << s.xadvance << " page=0 chnl=15 letter=\""
          << (s.cp == '"' ? std::string("quote") : utf8Encode(s.cp)) << "\"\n";
    f << "kernings count=0";
    out.fnt = f.str();
    return out.font.parse(out.fnt, &err);
}

bool writeFont(const BuiltFont& font, const std::string& fntPath, std::string& err) {
    const fs::path p = atlas::u8path(fntPath);
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    if (font.font.pages.empty()) {
        err = "the font has no page";
        return false;
    }
    const fs::path png = p.parent_path() / atlas::u8path(font.font.pages[0]);
    if (!atlas::savePng(atlas::u8str(png), font.page, &err)) return false;
    std::ofstream o(p, std::ios::binary);
    o << font.fnt;
    if (!o) {
        err = "cannot write " + fntPath;
        return false;
    }
    return true;
}

bool loadFont(const std::string& fntPath, BitmapFont& font, atlas::Image& page, std::string& err) {
    const fs::path p = atlas::u8path(fntPath);
    std::string text;
    if (!readText(p, text)) {
        err = "cannot read " + fntPath;
        return false;
    }
    if (!font.parse(text, &err)) {
        err = fntPath + ": " + err;
        return false;
    }
    if (font.pages.empty() || font.pages[0].empty()) {
        err = fntPath + ": no page file";
        return false;
    }
    if (font.pages.size() > 1) {
        err = fntPath + ": " + std::to_string(font.pages.size()) + " pages; only one page fonts are supported";
        return false;
    }
    const std::string png = atlas::u8str(p.parent_path() / atlas::u8path(font.pages[0]));
    if (!atlas::loadImage(png, page, &err)) return false;
    if (font.scaleW <= 0 || font.scaleH <= 0) { font.scaleW = page.w; font.scaleH = page.h; }
    return true;
}

bool splitFont(const std::string& fntPath, const std::string& outDir, int& written, std::string& err) {
    written = 0;
    std::vector<GlyphPicture> glyphs;
    if (!glyphsFromFont(fntPath, glyphs, err)) return false;
    const fs::path dir = atlas::u8path(outDir);
    std::error_code ec;
    fs::create_directories(dir, ec);
    // file names that differ only in case ("K" and "k") are one file on Windows: the second gets U+XXXX
    std::map<std::string, int> used;
    std::string chars = "# made by fontpack split from " + atlas::u8str(atlas::u8path(fntPath).filename()) +
                        ": file=character, in page order\n";
    for (const GlyphPicture& g : glyphs) {
        std::string stem = glyphFileName(g.cp);
        if (used.count(lower(stem))) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "U+%04X", (unsigned)g.cp);
            stem = buf;
        }
        used[lower(stem)] = 1;
        if (!atlas::savePng(atlas::u8str(dir / atlas::u8path(stem + ".png")), g.img, &err)) return false;
        chars += stem + "=" + (g.cp == ' ' ? std::string("space") : utf8Encode(g.cp)) + "\n";
        written++;
    }
    std::ofstream o(dir / "chars.txt", std::ios::binary);
    o << chars;
    return (bool)o;
}

atlas::Image renderText(const BitmapFont& font, const atlas::Image& page, const std::string& utf8, float scale,
                        int pad, const uint8_t background[4], int* missing) {
    float tw = 0, th = 0;
    font.measure(utf8, scale, tw, th);
    const std::vector<BitmapFont::Placed> placed = font.layout(utf8, (float)pad, (float)pad, scale,
                                                               BitmapFont::Align::Left, missing);
    // a glyph may reach past its advance (xoffset, a wide picture): grow to hold every picture
    float right = (float)pad + tw, bottom = (float)pad + th;
    for (const auto& p : placed) { right = std::max(right, p.x + p.w); bottom = std::max(bottom, p.y + p.h); }
    atlas::Image img(std::max(1, (int)std::ceil(right) + pad), std::max(1, (int)std::ceil(bottom) + pad));
    if (background)
        for (size_t i = 0; i < img.px.size(); i += 4)
            for (int c = 0; c < 4; c++) img.px[i + c] = background[c];
    // nearest sampling (the game point samples its sprite textures), "over" blending
    for (const auto& p : placed) {
        const BitmapGlyph& g = *p.glyph;
        const int x0 = (int)std::floor(p.x), y0 = (int)std::floor(p.y);
        const int x1 = (int)std::floor(p.x + p.w), y1 = (int)std::floor(p.y + p.h);
        for (int y = std::max(0, y0); y < std::min(img.h, y1); y++)
            for (int x = std::max(0, x0); x < std::min(img.w, x1); x++) {
                const int sx = g.x + std::min(g.w - 1, (int)((x - p.x + 0.5f) / scale));
                const int sy = g.y + std::min(g.h - 1, (int)((y - p.y + 0.5f) / scale));
                if (sx < 0 || sy < 0 || sx >= page.w || sy >= page.h) continue;
                const uint8_t* s = page.at(sx, sy);
                uint8_t* d = img.at(x, y);
                const int sa = s[3];
                if (sa == 0) continue;
                const int da = d[3];
                const int oa = sa + da * (255 - sa) / 255;
                for (int c = 0; c < 3; c++)
                    d[c] = (uint8_t)((s[c] * sa + d[c] * da * (255 - sa) / 255) / std::max(1, oa));
                d[3] = (uint8_t)oa;
            }
    }
    return img;
}

}  // namespace fonttool
