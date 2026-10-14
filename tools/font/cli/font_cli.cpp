// font_cli.cpp -- the font tool's command line: fontpack, and `font_editor --headless` (docs/22_FONT_TOOL.md).
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

const char* kUsage =
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
    std::vector<std::string> pos;
    std::vector<std::pair<std::string, std::string>> opts;
    bool has(const std::string& k) const {
        for (const auto& o : opts) if (o.first == k) return true;
        return false;
    }
    std::string get(const std::string& k, const std::string& def = std::string()) const {
        for (const auto& o : opts) if (o.first == k) return o.second;
        return def;
    }
};

bool parseArgs(const std::vector<std::string>& argv, Args& a, std::string& err) {
    for (size_t i = 1; i < argv.size(); i++) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            std::string val;
            const size_t eq = s.find('=');
            if (eq != std::string::npos) { val = s.substr(eq + 1); s = s.substr(0, eq); }
            else if (s != "--help") {
                if (i + 1 >= argv.size()) { err = s + " needs a value"; return false; }
                val = argv[++i];
            }
            a.opts.emplace_back(s, val);
        } else {
            a.pos.push_back(s);
        }
    }
    return true;
}

bool toInt(const std::string& s, int& out) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end) return false;
    out = (int)v;
    return true;
}

int cmdBuild(const Args& a) {
    if (a.pos.empty() || !a.has("--out")) { std::fputs(kUsage, stderr); return 3; }
    BuildOptions opt;
    const std::string out = a.get("--out");
    opt.face = a.get("--face", atlas::u8str(atlas::u8path(out).stem()));
    if ((a.has("--max-width") && !toInt(a.get("--max-width"), opt.maxWidth)) ||
        (a.has("--space") && !toInt(a.get("--space"), opt.spaceAdvance)) ||
        (a.has("--line-height") && !toInt(a.get("--line-height"), opt.lineHeight)) ||
        (a.has("--advance-pad") && !toInt(a.get("--advance-pad"), opt.advancePad))) {
        std::fprintf(stderr, "fontpack: --max-width, --space, --line-height and --advance-pad take a whole number\n");
        return 3;
    }
    if (a.has("--layout") && !parsePageLayout(a.get("--layout"), opt.layout)) {
        std::fprintf(stderr, "fontpack: --layout is row, tight or pot\n");
        return 3;
    }
    // the pictures, in the order given: PNG files, or folders (all their PNGs, chars.txt order)
    std::vector<GlyphPicture> glyphs;
    std::vector<std::string> warnings;
    std::string err;
    for (const std::string& in : a.pos) {
        std::error_code ec;
        if (std::filesystem::is_directory(atlas::u8path(in), ec)) {
            std::vector<GlyphPicture> more;
            if (!collectGlyphs(in, more, warnings, err)) {
                std::fprintf(stderr, "error: %s\n", err.c_str());
                return 2;
            }
            for (auto& g : more) glyphs.push_back(std::move(g));
        } else {
            GlyphPicture g;
            if (!loadPicture(in, g, err)) {
                std::fprintf(stderr, "error: %s: %s\n", in.c_str(), err.c_str());
                return 2;
            }
            glyphs.push_back(std::move(g));
        }
    }
    // --chars "0123456789.,KM": the characters of the pictures, in order (instead of their file names)
    if (a.has("--chars")) {
        const std::string chars = a.get("--chars");
        std::vector<uint32_t> cps;
        for (size_t i = 0; i < chars.size();) cps.push_back(utf8Next(chars, i));
        if (cps.size() != glyphs.size()) {
            std::fprintf(stderr, "fontpack: --chars has %d characters for %d pictures\n", (int)cps.size(),
                         (int)glyphs.size());
            return 3;
        }
        for (size_t i = 0; i < cps.size(); i++) glyphs[i].cp = cps[i];
    }
    for (const auto& w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
    const std::vector<std::string> problems = checkGlyphs(glyphs);
    if (!problems.empty()) {
        for (const auto& p : problems) std::fprintf(stderr, "error: %s\n", p.c_str());
        std::fprintf(stderr, "fontpack: name each picture after its character, or give --chars\n");
        return 2;
    }
    BuiltFont font;
    const std::string png = atlas::u8str(atlas::u8path(out).stem()) + ".png";
    if (!buildFont(glyphs, opt, png, font, err) || !writeFont(font, out, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }
    std::string chars;
    for (const auto& g : font.font.glyphs) chars += utf8Encode(g.id);
    std::printf("%s: %d glyphs \"%s\", page %dx%d, line height %d\n", out.c_str(), (int)font.font.glyphs.size(),
                chars.c_str(), font.page.w, font.page.h, font.font.lineHeight);
    return warnings.empty() ? 0 : 1;
}

int cmdSplit(const Args& a) {
    if (a.pos.size() != 1 || !a.has("--out")) { std::fputs(kUsage, stderr); return 3; }
    int n = 0;
    std::string err;
    if (!splitFont(a.pos[0], a.get("--out"), n, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }
    std::printf("%s: %d pictures + chars.txt\n", a.get("--out").c_str(), n);
    return 0;
}

int cmdPreview(const Args& a) {
    if (a.pos.size() != 1 || !a.has("--out") || !a.has("--text")) { std::fputs(kUsage, stderr); return 3; }
    BitmapFont font;
    atlas::Image page;
    std::string err;
    if (!loadFont(a.pos[0], font, page, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }
    const float scale = a.has("--scale") ? (float)std::atof(a.get("--scale").c_str()) : 1.0f;
    int pad = 8;
    if ((a.has("--pad") && !toInt(a.get("--pad"), pad)) || scale <= 0) {
        std::fprintf(stderr, "fontpack: bad --scale or --pad\n");
        return 3;
    }
    uint8_t bg[4] = {0, 0, 0, 0};
    if (a.has("--bg")) {
        const std::string h = a.get("--bg");
        const unsigned long v = std::strtoul(h.c_str(), nullptr, 16);
        if (h.size() == 8) { bg[0] = (uint8_t)(v >> 24); bg[1] = (uint8_t)(v >> 16); bg[2] = (uint8_t)(v >> 8); bg[3] = (uint8_t)v; }
        else if (h.size() == 6) { bg[0] = (uint8_t)(v >> 16); bg[1] = (uint8_t)(v >> 8); bg[2] = (uint8_t)v; bg[3] = 255; }
        else { std::fprintf(stderr, "fontpack: --bg is RRGGBB or RRGGBBAA\n"); return 3; }
    }
    int missing = 0;
    const atlas::Image img = renderText(font, page, a.get("--text"), scale, pad, bg, &missing);
    if (!atlas::savePng(a.get("--out"), img, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }
    std::printf("%s: %dx%d\n", a.get("--out").c_str(), img.w, img.h);
    if (missing) std::fprintf(stderr, "warning: %d character(s) of the text are not in the font\n", missing);
    return missing ? 1 : 0;
}

int cmdInfo(const Args& a) {
    if (a.pos.size() != 1) { std::fputs(kUsage, stderr); return 3; }
    BitmapFont font;
    atlas::Image page;
    std::string err;
    if (!loadFont(a.pos[0], font, page, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }
    std::printf("face \"%s\", size %d, line height %d, base %d, page %s (%dx%d)\n", font.face.c_str(), font.size,
                font.lineHeight, font.base, font.pages[0].c_str(), page.w, page.h);
    for (const auto& g : font.glyphs)
        std::printf("  %-6s U+%04X  %4d,%-4d %3dx%-3d  offset %d,%d  advance %d\n",
                    g.id == ' ' ? "space" : utf8Encode(g.id).c_str(), (unsigned)g.id, g.x, g.y, g.w, g.h, g.xoffset,
                    g.yoffset, g.xadvance);
    return 0;
}

}  // namespace

int fontCliMain(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::fputs(kUsage, args.empty() ? stderr : stdout);
        return args.empty() ? 3 : 0;
    }
    Args a;
    std::string err;
    if (!parseArgs(args, a, err)) {
        std::fprintf(stderr, "fontpack: %s\n", err.c_str());
        return 3;
    }
    const std::string& cmd = args[0];
    if (cmd == "build") return cmdBuild(a);
    if (cmd == "split") return cmdSplit(a);
    if (cmd == "preview") return cmdPreview(a);
    if (cmd == "info") return cmdInfo(a);
    std::fprintf(stderr, "fontpack: unknown command '%s'\n\n%s", cmd.c_str(), kUsage);
    return 3;
}
