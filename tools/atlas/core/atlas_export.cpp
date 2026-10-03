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

const std::vector<std::string>& exportFormats() {
    static const std::vector<std::string> f = {"atlas", "atlas-spine3", "plist", "tp-json", "pi", "rcss"};
    return f;
}

bool isExportFormat(const std::string& f) {
    const auto& all = exportFormats();
    return std::find(all.begin(), all.end(), f) != all.end();
}

// Locale-independent, shortest round-trip float text ("0.5", "0.0703125").
static std::string num(double v) {
    if (v == 0) return "0";
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, (float)v);
    return std::string(buf, r.ptr);
}

// Fixed decimals, as the old PI editor wrote RelativePos ("18.00").
static std::string fixed2(double v) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed, 2);
    return std::string(buf, r.ptr);
}

static std::string ints(std::initializer_list<int> v, const char* sep = ", ") {
    std::string s;
    for (int x : v) { if (!s.empty()) s += sep; s += std::to_string(x); }
    return s;
}

static std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

static bool defaultPivot(const Region& r) { return r.pivot[0] == 0.5f && r.pivot[1] == 0.5f; }

std::string pageBaseName(const BuildResult& b, int page) {
    return b.name + (page == 0 ? std::string() : "_" + std::to_string(page));
}

static std::string pixelFormat(const BuildResult&) { return "RGBA8888"; }

// ---- libGDX / Spine 4 --------------------------------------------------------------------------

static void libgdxPageHeader(std::ostringstream& o, const BuildResult& b, size_t p, bool spine3) {
    const Page& pg = b.pages[p];
    const std::string filter = b.settings.filter == "Linear" ? "Linear" : "Nearest";
    o << pg.file << "\n";
    o << "size: " << pg.image.w << (spine3 ? "," : ", ") << pg.image.h << "\n";
    o << "format: " << pixelFormat(b) << "\n";
    o << "filter: " << filter << (spine3 ? "," : ", ") << filter << "\n";
    o << "repeat: none\n";
    if (!spine3 && b.settings.premultiplyAlpha) o << "pma: true\n";
}

std::string exportLibgdx(const BuildResult& b) {
    std::ostringstream o;
    for (size_t p = 0; p < b.pages.size(); p++) {
        if (p) o << "\n";
        libgdxPageHeader(o, b, p, false);
        for (const Region& r : b.regions) {
            if (r.page != (int)p) continue;
            o << r.name << "\n";
            o << "  bounds: " << ints({r.frame.x, r.frame.y, r.frame.w, r.frame.h}) << "\n";
            if (r.trimmed())   // libGDX counts the offset's y from the bottom of the original image
                o << "  offsets: " << ints({r.offsetX, r.origH - r.offsetY - r.frame.h, r.origW, r.origH}) << "\n";
            if (r.hasSplit) o << "  split: " << ints({r.split[0], r.split[1], r.split[2], r.split[3]}) << "\n";
            if (r.child) {
                o << "  x_parent: " << r.parent << "\n";
                o << "  x_local: " << ints({r.local.x, r.local.y, r.local.w, r.local.h}) << "\n";
            }
            if (!defaultPivot(r)) o << "  x_pivot: " << num(r.pivot[0]) << ", " << num(r.pivot[1]) << "\n";
            if (!r.tags.empty()) {
                o << "  x_tags: ";
                for (size_t i = 0; i < r.tags.size(); i++) o << (i ? ", " : "") << r.tags[i];
                o << "\n";
            }
        }
    }
    return o.str();
}

std::string exportSpine3(const BuildResult& b) {
    std::ostringstream o;
    for (size_t p = 0; p < b.pages.size(); p++) {
        o << "\n";   // the old writer starts every page with a blank line
        libgdxPageHeader(o, b, p, true);
        for (const Region& r : b.regions) {
            if (r.page != (int)p) continue;
            o << r.name << "\n";
            o << "  rotate: false\n";
            o << "  xy: " << ints({r.frame.x, r.frame.y}) << "\n";
            o << "  size: " << ints({r.frame.w, r.frame.h}) << "\n";
            if (r.hasSplit) o << "  split: " << ints({r.split[0], r.split[1], r.split[2], r.split[3]}) << "\n";
            o << "  orig: " << ints({r.origW, r.origH}) << "\n";
            o << "  offset: " << ints({r.offsetX, r.origH - r.offsetY - r.frame.h}) << "\n";
            o << "  index: -1\n";
        }
    }
    return o.str();
}

// ---- Cocos plist (TexturePacker format 3) ------------------------------------------------------

std::string exportPlist(const BuildResult& b, int page) {
    const Page& pg = b.pages[page];
    std::ostringstream o;
    auto str = [&](const char* key, const std::string& v) {
        o << "\t\t\t\t<key>" << key << "</key>\n\t\t\t\t<string>" << xmlEscape(v) << "</string>\n";
    };
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         "<plist version=\"1.0\">\n\t<dict>\n\t\t<key>frames</key>\n\t\t<dict>\n";
    for (const Region& r : b.regions) {
        if (r.page != page) continue;
        o << "\t\t\t<key>" << xmlEscape(r.name) << "</key>\n\t\t\t<dict>\n";
        o << "\t\t\t\t<key>aliases</key>\n\t\t\t\t<array/>\n";
        // Offset of the trimmed centre from the original centre, y up (cocos convention).
        const double ox = r.offsetX + r.frame.w / 2.0 - r.origW / 2.0;
        const double oy = -(r.offsetY + r.frame.h / 2.0 - r.origH / 2.0);
        if (!defaultPivot(r)) str("anchor", "{" + num(r.pivot[0]) + "," + num(1.0 - r.pivot[1]) + "}");
        str("spriteOffset", "{" + num(ox) + "," + num(oy) + "}");
        str("spriteSize", "{" + ints({r.frame.w, r.frame.h}, ",") + "}");
        str("spriteSourceSize", "{" + ints({r.origW, r.origH}, ",") + "}");
        str("textureRect", "{{" + ints({r.frame.x, r.frame.y}, ",") + "},{" + ints({r.frame.w, r.frame.h}, ",") + "}}");
        o << "\t\t\t\t<key>textureRotated</key>\n\t\t\t\t<false/>\n";
        if (r.hasSplit) str("x_split", "{" + ints({r.split[0], r.split[1], r.split[2], r.split[3]}, ",") + "}");
        if (r.child) {
            str("x_parent", r.parent);
            str("x_local", "{{" + ints({r.local.x, r.local.y}, ",") + "},{" + ints({r.local.w, r.local.h}, ",") + "}}");
        }
        if (!r.tags.empty()) {
            o << "\t\t\t\t<key>x_tags</key>\n\t\t\t\t<array>\n";
            for (const std::string& t : r.tags) o << "\t\t\t\t\t<string>" << xmlEscape(t) << "</string>\n";
            o << "\t\t\t\t</array>\n";
        }
        o << "\t\t\t</dict>\n";
    }
    o << "\t\t</dict>\n\t\t<key>metadata</key>\n\t\t<dict>\n"
         "\t\t\t<key>format</key>\n\t\t\t<integer>3</integer>\n"
         "\t\t\t<key>pixelFormat</key>\n\t\t\t<string>" << pixelFormat(b) << "</string>\n"
         "\t\t\t<key>premultiplyAlpha</key>\n\t\t\t<" << (b.settings.premultiplyAlpha ? "true" : "false") << "/>\n"
         "\t\t\t<key>realTextureFileName</key>\n\t\t\t<string>" << xmlEscape(pg.file) << "</string>\n"
         "\t\t\t<key>size</key>\n\t\t\t<string>{" << pg.image.w << "," << pg.image.h << "}</string>\n"
         "\t\t\t<key>textureFileName</key>\n\t\t\t<string>" << xmlEscape(pg.file) << "</string>\n"
         "\t\t</dict>\n\t</dict>\n</plist>\n";
    return o.str();
}

// ---- TexturePacker JSON hash -------------------------------------------------------------------

std::string exportTpJson(const BuildResult& b, int page) {
    const Page& pg = b.pages[page];
    ordered_json j;
    ordered_json frames = ordered_json::object();
    for (const Region& r : b.regions) {
        if (r.page != page) continue;
        ordered_json f;
        f["frame"] = {{"x", r.frame.x}, {"y", r.frame.y}, {"w", r.frame.w}, {"h", r.frame.h}};
        f["rotated"] = false;
        f["trimmed"] = r.trimmed();
        f["spriteSourceSize"] = {{"x", r.offsetX}, {"y", r.offsetY}, {"w", r.frame.w}, {"h", r.frame.h}};
        f["sourceSize"] = {{"w", r.origW}, {"h", r.origH}};
        f["pivot"] = {{"x", r.pivot[0]}, {"y", r.pivot[1]}};
        ordered_json x = ordered_json::object();
        if (r.hasSplit) x["split"] = {r.split[0], r.split[1], r.split[2], r.split[3]};
        if (r.child) { x["parent"] = r.parent; x["local"] = {r.local.x, r.local.y, r.local.w, r.local.h}; }
        if (!r.tags.empty()) x["tags"] = r.tags;
        if (!x.empty()) f["x"] = x;
        frames[r.name] = f;
    }
    j["frames"] = frames;
    if (page == 0 && !b.animations.empty()) {   // Pixi reads "animations"; frame times go in meta.x
        ordered_json anims = ordered_json::object();
        for (const Animation& a : b.animations) {
            ordered_json list = ordered_json::array();
            for (const AnimFrame& f : a.frames) list.push_back(f.sprite);
            anims[a.name] = list;
        }
        j["animations"] = anims;
    }
    ordered_json meta;
    meta["app"] = "TOMS atlas tool";
    meta["version"] = "1";
    meta["image"] = pg.file;
    meta["format"] = pixelFormat(b);
    meta["size"] = {{"w", pg.image.w}, {"h", pg.image.h}};
    meta["scale"] = "1";
    if (page == 0 && !b.animations.empty()) {
        ordered_json xa = ordered_json::object();
        for (const Animation& a : b.animations) {
            ordered_json times = ordered_json::array();
            for (const AnimFrame& f : a.frames) times.push_back(f.time);
            xa[a.name] = {{"loop", a.loop}, {"times", times}};
        }
        meta["x"] = {{"animations", xa}};
    }
    j["meta"] = meta;
    return j.dump(1, '\t') + "\n";
}

// ---- FM79979 PuzzleImage (.pi) -----------------------------------------------------------------

std::string exportPi(const BuildResult& b, int page) {
    const Page& pg = b.pages[page];
    std::vector<const Region*> units;
    for (const Region& r : b.regions)
        if (r.page == page) units.push_back(&r);   // already sorted by name, as the old editor wrote them
    std::ostringstream o;
    o << "<!-- Author:TOMS atlas tool -->\n";
    std::string order;
    for (const Region* r : units) order += (order.empty() ? "" : ",") + r->name;
    o << "<PuzzleImage OriginalNameSort=\"" << xmlEscape(order) << "\" ImageName=\"" << xmlEscape(pg.file)
      << "\" Count=\"" << units.size() << "\" GeneratePuzzleimageUnit=\"0\" ImageDistance=\""
      << b.settings.padding << "," << b.settings.padding << "\">\n";
    const double W = pg.image.w, H = pg.image.h;
    for (const Region* r : units) {
        o << "    <PuzzleUnit Name=\"" << xmlEscape(r->name) << "\" UV=\"" << num(r->frame.x / W) << "," << num(r->frame.y / H)
          << "," << num(r->frame.right() / W) << "," << num(r->frame.bottom() / H) << "\" OffsetPos=\"" << r->offsetX << ","
          << r->offsetY << "\" Size=\"" << r->frame.w << "," << r->frame.h << "\" OriginalSize=\"" << r->origW << ","
          << r->origH << "\" ShowPosInPI=\"" << r->frame.x << "," << r->frame.y << "\" />\n";
    }
    for (const Region* r : units)
        if (r->child && !r->baked)   // a baked child has its own pixels: a plain unit for the old format
            o << "    <PuzzleUnitChild Name=\"" << xmlEscape(r->name) << "\" AttachParent=\"" << xmlEscape(r->parent)
              << "\" RelativePos=\"" << fixed2(r->local.x) << "," << fixed2(r->local.y) << ",0.00\" />\n";
    // Sequence animations whose frames all live on this page.
    std::vector<const Animation*> anims;
    for (const Animation& a : b.animations) {
        bool here = !a.frames.empty();
        for (const AnimFrame& f : a.frames) {
            const Region* r = b.find(f.sprite);
            if (!r || r->page != page) here = false;
        }
        if (here) anims.push_back(&a);
    }
    if (!anims.empty()) {
        o << "    <AnimationData Count=\"" << anims.size() << "\">\n";
        for (const Animation* a : anims) {
            std::string names, times;
            for (const AnimFrame& f : a->frames) { names += f.sprite + ","; times += num(f.time) + ","; }
            o << "        <AnimationDataUnit Name=\"" << xmlEscape(a->name) << "\" Count=\"" << a->frames.size()
              << "\" ImageList=\"" << xmlEscape(names) << "\" TimeList=\"" << times << "\" />\n";
        }
        o << "    </AnimationData>\n";
    }
    o << "</PuzzleImage>\n";
    return o.str();
}

// ---- RmlUi @spritesheet ------------------------------------------------------------------------

static std::string rcssIdent(const std::string& n) {
    std::string s;
    for (char c : n) s += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '-';
    return s;
}

std::string exportRcss(const BuildResult& b) {
    std::ostringstream o;
    o << "/* Generated by the TOMS atlas tool -- do not edit; change the .atlasproj and rebuild.\n"
         "   Sprite names: '/' and other characters RCSS does not allow become '-'. */\n";
    for (size_t p = 0; p < b.pages.size(); p++) {
        o << "\n@spritesheet " << rcssIdent(pageBaseName(b, (int)p)) << "\n{\n";
        o << "    src: " << b.pages[p].file << ";\n";
        o << "    resolution: 1x;\n";
        for (const Region& r : b.regions)
            if (r.page == (int)p)
                o << "    " << rcssIdent(r.name) << ": " << r.frame.x << "px " << r.frame.y << "px " << r.frame.w << "px "
                  << r.frame.h << "px;\n";
        o << "}\n";
    }
    return o.str();
}

// ---- all files ---------------------------------------------------------------------------------

bool exportFiles(const BuildResult& b, const std::vector<std::string>& formats, std::vector<ExportFile>& out, std::string* err) {
    out.clear();
    for (const std::string& f : formats)
        if (!isExportFormat(f)) { if (err) *err = "unknown export format '" + f + "'"; return false; }
    for (const Page& p : b.pages) {
        if (!p.image.valid()) { if (err) *err = "page " + p.file + " has no pixels (built without compositing)"; return false; }
        out.push_back({p.file, encodePng(p.image)});
    }
    auto has = [&](const char* f) { return std::find(formats.begin(), formats.end(), f) != formats.end(); };
    if (has("atlas")) out.push_back({b.name + ".atlas", bytes(exportLibgdx(b))});
    if (has("atlas-spine3")) out.push_back({b.name + (has("atlas") ? ".spine3.atlas" : ".atlas"), bytes(exportSpine3(b))});
    for (int p = 0; p < (int)b.pages.size(); p++) {
        if (has("plist")) out.push_back({pageBaseName(b, p) + ".plist", bytes(exportPlist(b, p))});
        if (has("tp-json")) out.push_back({pageBaseName(b, p) + ".json", bytes(exportTpJson(b, p))});
        if (has("pi")) out.push_back({pageBaseName(b, p) + ".pi", bytes(exportPi(b, p))});
    }
    if (has("rcss")) out.push_back({b.name + ".rcss", bytes(exportRcss(b))});
    return true;
}

bool writeFiles(const std::string& dir, const std::vector<ExportFile>& files, std::string* err,
                std::vector<std::string>* changed) {
    std::error_code ec;
    fs::create_directories(u8path(dir), ec);
    for (const ExportFile& f : files) {
        const std::string path = joinPath(dir, f.name);
        // Leave an unchanged file alone, so its timestamp does not trigger rebuilds downstream.
        {
            std::ifstream in(u8path(path), std::ios::binary);
            if (in) {
                std::vector<uint8_t> old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (old == f.data) continue;
            }
        }
        std::ofstream o(u8path(path), std::ios::binary);
        if (!o || !o.write((const char*)f.data.data(), (std::streamsize)f.data.size())) {
            if (err) *err = "cannot write " + path;
            return false;
        }
        if (changed) changed->push_back(f.name);
    }
    return true;
}

// ---- import ------------------------------------------------------------------------------------

static bool readText(const std::string& path, std::string& out, std::string* err) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static std::vector<double> numbers(const std::string& s) {
    std::vector<double> v;
    std::string cur;
    for (char c : s + ",") {
        if (c == ',' || c == '{' || c == '}' || c == ' ') {
            if (!cur.empty()) { v.push_back(strtod(cur.c_str(), nullptr)); cur.clear(); }
        } else cur += c;
    }
    return v;
}

static int ri(double v) { return (int)std::lround(v); }

static bool loadPage(const std::string& dir, const std::string& file, Page& pg, std::string* err) {
    pg.file = file;
    return loadImage(joinPath(dir, file), pg.image, err);
}

bool importPi(const std::string& path, BuildResult& out, std::string* err) {
    out = BuildResult();
    std::string text;
    if (!readText(path, text, err)) return false;
    std::string xerr;
    auto root = parseXml(text, &xerr);
    if (!root) { if (err) *err = path + ": " + xerr; return false; }
    if (root->name != "PuzzleImage") { if (err) *err = path + ": root is <" + root->name + ">, not <PuzzleImage>"; return false; }
    const std::string image = root->attrOr("ImageName");
    if (image.empty()) { if (err) *err = path + ": no ImageName"; return false; }
    std::string imgFile = image;
    // Binary exports point at .pngb (Huffman); the plain .png is normally next to it.
    if (imgFile.size() > 5 && imgFile.substr(imgFile.size() - 5) == ".pngb") imgFile.resize(imgFile.size() - 1);
    Page pg;
    if (!loadPage(parentDir(path), imgFile, pg, err)) return false;
    out.name = fileStem(path);
    const double W = pg.image.w, H = pg.image.h;
    out.pages.push_back(std::move(pg));
    for (const auto& c : root->children) {
        if (c->name == "PuzzleUnit") {
            Region r;
            r.name = c->attrOr("Name");
            const std::vector<double> size = numbers(c->attrOr("Size"));
            const std::vector<double> off = numbers(c->attrOr("OffsetPos", "0,0"));
            const std::vector<double> show = numbers(c->attrOr("ShowPosInPI"));
            const std::vector<double> uv = numbers(c->attrOr("UV"));
            if (size.size() < 2) { out.diagnostics.push_back({Diagnostic::Warning, r.name, "PuzzleUnit has no Size; skipped"}); continue; }
            r.frame.w = ri(size[0]); r.frame.h = ri(size[1]);
            if (show.size() >= 2) { r.frame.x = ri(show[0]); r.frame.y = ri(show[1]); }
            else if (uv.size() >= 2) { r.frame.x = ri(uv[0] * W); r.frame.y = ri(uv[1] * H); }
            if (off.size() >= 2) { r.offsetX = ri(off[0]); r.offsetY = ri(off[1]); }
            const std::vector<double> orig = numbers(c->attrOr("OriginalSize"));
            if (orig.size() >= 2) { r.origW = ri(orig[0]); r.origH = ri(orig[1]); }
            else { r.origW = r.offsetX + r.frame.w; r.origH = r.offsetY + r.frame.h; }
            out.regions.push_back(r);
        } else if (c->name == "AnimationData") {
            for (const auto& a : c->children) {
                if (a->name != "AnimationDataUnit") continue;
                Animation an;
                an.name = a->attrOr("Name");
                std::vector<std::string> names;
                std::string cur;
                for (char ch : a->attrOr("ImageList") + ",") {
                    if (ch == ',') { if (!cur.empty()) names.push_back(cur); cur.clear(); }
                    else cur += ch;
                }
                const std::vector<double> times = numbers(a->attrOr("TimeList"));
                for (size_t i = 0; i < names.size(); i++)
                    an.frames.push_back({names[i], i < times.size() ? (float)times[i] : 0.1f});
                out.animations.push_back(an);
            }
        }
    }
    std::sort(out.regions.begin(), out.regions.end(), [](const Region& a, const Region& b) { return a.name < b.name; });
    // PuzzleUnitChild: the unit shares its parent's pixels. Its rect in the parent's original
    // pixels follows from where both originals sit in the atlas.
    for (const auto& c : root->children) {
        if (c->name != "PuzzleUnitChild") continue;
        const std::string name = c->attrOr("Name"), parent = c->attrOr("AttachParent");
        Region* r = nullptr;
        for (Region& x : out.regions) if (x.name == name) r = &x;
        const Region* p = out.find(parent);
        if (!r || !p || r == p) { out.diagnostics.push_back({Diagnostic::Warning, name, "PuzzleUnitChild refers to a missing unit"}); continue; }
        const int lx = (r->frame.x - r->offsetX) - (p->frame.x - p->offsetX);
        const int ly = (r->frame.y - r->offsetY) - (p->frame.y - p->offsetY);
        const IRect local{lx, ly, r->origW, r->origH};
        if (lx < 0 || ly < 0 || local.right() > p->origW || local.bottom() > p->origH) {
            out.diagnostics.push_back({Diagnostic::Warning, name, "child lies outside its parent '" + parent + "'; imported as a normal image"});
            continue;
        }
        r->child = true;
        r->parent = parent;
        r->local = local;
    }
    return true;
}

bool importLibgdx(const std::string& path, BuildResult& out, std::string* err) {
    out = BuildResult();
    std::string text;
    if (!readText(path, text, err)) return false;
    toms::AtlasFile af;
    std::string perr;
    if (!toms::parseAtlas(text, af, &perr)) { if (err) *err = path + ": " + perr; return false; }
    out.name = fileStem(path);
    for (const toms::AtlasPage& p : af.pages) {
        Page pg;
        if (!loadPage(parentDir(path), p.file, pg, err)) return false;
        out.pages.push_back(std::move(pg));
    }
    for (const toms::AtlasRegion& a : af.regions) {
        Region r;
        r.name = a.name;
        r.page = a.page;
        r.frame = {a.x, a.y, a.w, a.h};
        r.offsetX = a.offX; r.offsetY = a.offY;
        r.origW = a.origW; r.origH = a.origH;
        r.pivot[0] = a.pivot[0]; r.pivot[1] = a.pivot[1];
        r.hasSplit = a.hasSplit;
        for (int k = 0; k < 4; k++) r.split[k] = a.split[k];
        r.tags = a.tags;
        if (a.child) { r.child = true; r.parent = a.parent; r.local = {a.local[0], a.local[1], a.local[2], a.local[3]}; }
        if (a.index >= 0) r.name += "_" + std::to_string(a.index);
        if (a.rotated) {
            // Stored turned 90 degrees (libGDX: counter-clockwise). Un-rotate into a page of its own
            // so every later step sees an upright region.
            const Image& src = out.pages[a.page].image;
            Image up(a.w, a.h);
            for (int y = 0; y < a.h; y++)
                for (int x = 0; x < a.w; x++) {
                    const int sx = a.x + y, sy = a.y + (a.w - 1 - x);
                    if (sx >= 0 && sy >= 0 && sx < src.w && sy < src.h) memcpy(up.at(x, y), src.at(sx, sy), 4);
                }
            Page extra;
            extra.file = "(rotated " + a.name + ")";
            extra.image = std::move(up);
            out.pages.push_back(std::move(extra));
            r.page = (int)out.pages.size() - 1;
            r.frame = {0, 0, a.w, a.h};
            out.diagnostics.push_back({Diagnostic::Info, r.name, "rotated region was un-rotated (check its orientation)"});
        }
        out.regions.push_back(r);
    }
    std::sort(out.regions.begin(), out.regions.end(), [](const Region& a, const Region& b) { return a.name < b.name; });
    return true;
}

bool importTpJson(const std::string& path, BuildResult& out, std::string* err) {
    out = BuildResult();
    std::string text;
    if (!readText(path, text, err)) return false;
    json j;
    try { j = json::parse(text); } catch (const std::exception& e) { if (err) *err = path + ": " + e.what(); return false; }
    try {
        Page pg;
        if (!loadPage(parentDir(path), j.at("meta").at("image").get<std::string>(), pg, err)) return false;
        out.pages.push_back(std::move(pg));
        out.name = fileStem(path);
        auto take = [&](const std::string& name, const json& f) {
            Region r;
            r.name = name;
            const json& fr = f.at("frame");
            r.frame = {fr.at("x").get<int>(), fr.at("y").get<int>(), fr.at("w").get<int>(), fr.at("h").get<int>()};
            if (f.value("rotated", false)) out.diagnostics.push_back({Diagnostic::Warning, name, "rotated frames are not supported; imported as stored"});
            if (f.contains("spriteSourceSize")) { r.offsetX = f["spriteSourceSize"].value("x", 0); r.offsetY = f["spriteSourceSize"].value("y", 0); }
            r.origW = r.frame.w; r.origH = r.frame.h;
            if (f.contains("sourceSize")) { r.origW = f["sourceSize"].value("w", r.origW); r.origH = f["sourceSize"].value("h", r.origH); }
            if (f.contains("pivot")) { r.pivot[0] = f["pivot"].value("x", 0.5f); r.pivot[1] = f["pivot"].value("y", 0.5f); }
            if (f.contains("x")) {
                const json& x = f["x"];
                if (x.contains("split")) { r.hasSplit = true; for (int k = 0; k < 4; k++) r.split[k] = x["split"][k].get<int>(); }
                if (x.contains("parent")) {
                    r.child = true;
                    r.parent = x["parent"].get<std::string>();
                    const json& l = x.at("local");
                    r.local = {l[0].get<int>(), l[1].get<int>(), l[2].get<int>(), l[3].get<int>()};
                }
                if (x.contains("tags")) r.tags = x["tags"].get<std::vector<std::string>>();
            }
            out.regions.push_back(r);
        };
        const json& frames = j.at("frames");
        if (frames.is_array()) { for (const json& f : frames) take(f.at("filename").get<std::string>(), f); }
        else for (auto it = frames.begin(); it != frames.end(); ++it) take(it.key(), it.value());
        if (j.contains("animations")) {
            const json* times = nullptr;
            if (j["meta"].contains("x") && j["meta"]["x"].contains("animations")) times = &j["meta"]["x"]["animations"];
            for (auto it = j["animations"].begin(); it != j["animations"].end(); ++it) {
                Animation a;
                a.name = it.key();
                const json* t = times && times->contains(a.name) ? &(*times)[a.name] : nullptr;
                if (t) a.loop = t->value("loop", true);
                for (size_t i = 0; i < it.value().size(); i++) {
                    float tm = 0.1f;
                    if (t && t->contains("times") && i < (*t)["times"].size()) tm = (*t)["times"][i].get<float>();
                    a.frames.push_back({it.value()[i].get<std::string>(), tm});
                }
                out.animations.push_back(a);
            }
        }
    } catch (const std::exception& e) {
        if (err) *err = path + ": " + e.what();
        return false;
    }
    std::sort(out.regions.begin(), out.regions.end(), [](const Region& a, const Region& b) { return a.name < b.name; });
    return true;
}

bool importAtlasFile(const std::string& path, BuildResult& out, std::string* err) {
    std::string ext = u8str(u8path(path).extension());
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    if (ext == ".pi") return importPi(path, out, err);
    if (ext == ".atlas" || ext == ".txt") return importLibgdx(path, out, err);
    if (ext == ".json") return importTpJson(path, out, err);
    if (err) *err = "unknown atlas type '" + ext + "' (expected .pi, .atlas or .json)";
    return false;
}

Image renderRegion(const BuildResult& b, const Region& r) {
    Image out(std::max(r.origW, 1), std::max(r.origH, 1));
    if (r.page >= 0 && r.page < (int)b.pages.size()) blit(out, b.pages[r.page].image, r.frame, r.offsetX, r.offsetY);
    return out;
}

bool importToProject(const BuildResult& in, const std::string& projectPath, const std::string& imageDir,
                     Project& out, std::string* err) {
    Project p;
    p.filePath = normalizePath(projectPath);
    p.name = in.name.empty() ? fileStem(projectPath) : in.name;
    p.settings.padding = 2;
    p.embedded = imageDir.empty();
    std::error_code ec;
    if (!p.embedded) fs::create_directories(u8path(imageDir), ec);
    for (const Region& r : in.regions) {
        const Region* parent = r.child ? in.find(r.parent) : nullptr;
        SpriteDef d;
        d.name = r.name;
        if (r.child && parent) {
            d.parent = r.parent;
            d.rect = r.local;
        } else if (p.embedded) {
            p.images[r.name] = std::make_shared<const Image>(renderRegion(in, r));
        } else {
            const std::string file = joinPath(imageDir, r.name + ".png");
            fs::create_directories(u8path(file).parent_path(), ec);
            if (!savePng(file, renderRegion(in, r), err)) return false;
        }
        if (!(r.pivot[0] == 0.5f && r.pivot[1] == 0.5f)) { d.hasPivot = true; d.pivot[0] = r.pivot[0]; d.pivot[1] = r.pivot[1]; }
        if (r.hasSplit) { d.hasSplit = true; for (int k = 0; k < 4; k++) d.split[k] = r.split[k]; }
        d.tags = r.tags;
        if (d.isChild() || d.hasPivot || d.hasSplit || !d.tags.empty()) p.sprites.push_back(d);
    }
    if (!p.embedded) p.sources.push_back({p.relativize(imageDir), true, ""});
    p.animations = in.animations;
    out = std::move(p);
    return true;
}

}  // namespace atlas
