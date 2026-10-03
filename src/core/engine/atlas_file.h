// atlas_file.h -- reads libGDX / Spine texture atlas files (.atlas), including the x_ extension
// fields written by the TOMS atlas tool (tools/atlas). Header only, no dependencies.
//
// Both atlas dialects are accepted:
//   new (libGDX 1.9.12+, Spine 4):  bounds: x, y, w, h     offsets: ox, oy, origW, origH
//   old (Spine 3, older libGDX):    xy / size / orig / offset / rotate / index
// libGDX offsets count y from the BOTTOM of the original image; AtlasRegion stores it from the top.
//
// Extension fields (other parsers keep or skip them; TOMS reads them):
//   x_parent: <name>        child sprite: no pixels of its own, a rect inside <name>
//   x_local: x, y, w, h     that rect, in the parent's original pixels
//   x_pivot: px, py        pivot, 0..1 of the original size from the top-left
//   x_tags: a, b            free tags (e.g. map, ui)
//
//   toms::AtlasFile a;
//   if (toms::parseAtlas(text, a, &err)) { const toms::AtlasRegion* r = a.find("floor"); r->uv ... }
#pragma once
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace toms {

struct AtlasPage {
    std::string file;
    int w = 0, h = 0;
    std::string format, filter, repeat;
    bool pma = false;
};

struct AtlasRegion {
    std::string name;
    int page = 0;
    int x = 0, y = 0, w = 0, h = 0;     // pixels in the page; w/h unrotated (a rotated region covers h x w)
    int offX = 0, offY = 0;             // top-left of the packed pixels inside the original, y down
    int origW = 0, origH = 0;
    bool rotated = false;
    int index = -1;
    bool hasSplit = false;
    int split[4] = {0, 0, 0, 0};        // left, right, top, bottom
    float pivot[2] = {0.5f, 0.5f};
    bool child = false;
    std::string parent;
    int local[4] = {0, 0, 0, 0};
    std::vector<std::string> tags;
    float uv[4] = {0, 0, 0, 0};         // u0, v0, u1, v1 (v down), set when the page size is known
};

struct AtlasFile {
    std::vector<AtlasPage> pages;
    std::vector<AtlasRegion> regions;
    std::unordered_map<std::string, int> byName;

    const AtlasRegion* find(const std::string& name) const {
        auto it = byName.find(name);
        return it == byName.end() ? nullptr : &regions[(size_t)it->second];
    }
    // (Re)computes every region's uv from the page sizes; call again if a page's size was only
    // known after its image was loaded.
    void computeUVs() {
        for (AtlasRegion& r : regions) {
            if (r.page < 0 || r.page >= (int)pages.size()) continue;
            const AtlasPage& p = pages[(size_t)r.page];
            if (p.w <= 0 || p.h <= 0) continue;
            const int pw = r.rotated ? r.h : r.w, ph = r.rotated ? r.w : r.h;
            r.uv[0] = (float)r.x / p.w;
            r.uv[1] = (float)r.y / p.h;
            r.uv[2] = (float)(r.x + pw) / p.w;
            r.uv[3] = (float)(r.y + ph) / p.h;
        }
    }
};

namespace atlas_detail {

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

inline std::vector<std::string> splitValues(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t c = s.find(',', start);
        const std::string v = trim(s.substr(start, c == std::string::npos ? std::string::npos : c - start));
        if (!v.empty() || c != std::string::npos) out.push_back(v);
        if (c == std::string::npos) break;
        start = c + 1;
    }
    return out;
}

inline int toInt(const std::string& s) { return (int)std::strtol(s.c_str(), nullptr, 10); }
inline float toFloat(const std::string& s) { return std::strtof(s.c_str(), nullptr); }

inline void ints(const std::vector<std::string>& v, int* out, size_t n) {
    for (size_t i = 0; i < n && i < v.size(); i++) out[i] = toInt(v[i]);
}

}  // namespace atlas_detail

inline bool parseAtlas(const std::string& text, AtlasFile& out, std::string* err = nullptr) {
    using namespace atlas_detail;
    out = AtlasFile();
    bool expectPage = true;
    AtlasRegion* cur = nullptr;
    // Raw offsets per region (old/new formats keep y from the bottom), resolved after parsing.
    struct Raw { bool hasOrig = false, hasOffset = false; int offX = 0, offYUp = 0; };
    std::vector<Raw> raw;
    size_t pos = 0;
    int lineNo = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        lineNo++;
        if (line.empty()) {
            expectPage = true;
            cur = nullptr;
            if (nl >= text.size()) break;
            continue;
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            if (expectPage) {
                AtlasPage p;
                p.file = line;
                out.pages.push_back(p);
                expectPage = false;
                cur = nullptr;
            } else {
                if (out.pages.empty()) { if (err) *err = "line " + std::to_string(lineNo) + ": region before any page"; return false; }
                AtlasRegion r;
                r.name = line;
                r.page = (int)out.pages.size() - 1;
                out.regions.push_back(r);
                raw.push_back(Raw());
                cur = &out.regions.back();
            }
            continue;
        }
        if (out.pages.empty()) { if (err) *err = "line " + std::to_string(lineNo) + ": field before any page"; return false; }
        const std::string key = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));
        const std::vector<std::string> v = splitValues(value);
        if (!cur) {
            AtlasPage& p = out.pages.back();
            if (key == "size" && v.size() >= 2) { p.w = toInt(v[0]); p.h = toInt(v[1]); }
            else if (key == "format") p.format = value;
            else if (key == "filter") p.filter = value;
            else if (key == "repeat") p.repeat = value;
            else if (key == "pma") p.pma = value == "true";
            continue;   // unknown page fields are ignored
        }
        Raw& rw = raw.back();
        AtlasRegion& r = *cur;
        int t[4] = {0, 0, 0, 0};
        if (key == "bounds") { ints(v, t, 4); r.x = t[0]; r.y = t[1]; r.w = t[2]; r.h = t[3]; }
        else if (key == "xy") { ints(v, t, 2); r.x = t[0]; r.y = t[1]; }
        else if (key == "size") { ints(v, t, 2); r.w = t[0]; r.h = t[1]; }
        else if (key == "offsets") { ints(v, t, 4); rw.hasOffset = rw.hasOrig = true; rw.offX = t[0]; rw.offYUp = t[1]; r.origW = t[2]; r.origH = t[3]; }
        else if (key == "orig") { ints(v, t, 2); rw.hasOrig = true; r.origW = t[0]; r.origH = t[1]; }
        else if (key == "offset") { ints(v, t, 2); rw.hasOffset = true; rw.offX = t[0]; rw.offYUp = t[1]; }
        else if (key == "rotate") r.rotated = value == "true" || value == "90";
        else if (key == "index") r.index = toInt(value);
        else if (key == "split") { ints(v, r.split, 4); r.hasSplit = true; }
        else if (key == "x_pivot" && v.size() >= 2) { r.pivot[0] = toFloat(v[0]); r.pivot[1] = toFloat(v[1]); }
        else if (key == "x_parent") { r.parent = value; r.child = !value.empty(); }
        else if (key == "x_local") ints(v, r.local, 4);
        else if (key == "x_tags") { for (const std::string& s : v) if (!s.empty()) r.tags.push_back(s); }
        // anything else (pad, custom fields of other tools) is ignored
    }
    for (size_t i = 0; i < out.regions.size(); i++) {
        AtlasRegion& r = out.regions[i];
        if (!raw[i].hasOrig) { r.origW = r.w; r.origH = r.h; }
        r.offX = raw[i].offX;
        r.offY = raw[i].hasOffset ? r.origH - raw[i].offYUp - r.h : 0;
        if (out.byName.count(r.name) == 0) out.byName[r.name] = (int)i;
    }
    out.computeUVs();
    return true;
}

// ---- RmlUi -------------------------------------------------------------------------------------
// RmlUi reads sprite sheets only from RCSS (@spritesheet). The game writes that text from the
// atlas it already loaded, at start-up, so the UI can never disagree with the atlas: .rml files
// name sprites (<img sprite="coin"/>), never positions.

// A sprite name as RCSS allows it: '/' and other characters become '-' ("ui/btn_ok" -> "ui-btn_ok").
inline std::string rmlSpriteName(const std::string& name) {
    std::string s;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        s += ok ? c : '-';
    }
    return s;
}

// One @spritesheet per page. pageDir is where the page images are, as seen from the RCSS
// document's folder (e.g. "../atlas/"). Image regions and child sprites alike are listed.
inline std::string rmlSpritesheets(const AtlasFile& a, const std::string& pageDir) {
    std::string out = "/* made by the game from its sprite atlas (tools/atlas) at start-up */\n";
    for (size_t p = 0; p < a.pages.size(); p++) {
        out += "@spritesheet atlas-page" + std::to_string(p) + "\n{\n    src: " + pageDir + a.pages[p].file + ";\n";
        for (const AtlasRegion& r : a.regions) {
            if (r.page != (int)p) continue;
            const int pw = r.rotated ? r.h : r.w, ph = r.rotated ? r.w : r.h;
            out += "    " + rmlSpriteName(r.name) + ": " + std::to_string(r.x) + "px " + std::to_string(r.y) + "px " +
                   std::to_string(pw) + "px " + std::to_string(ph) + "px;\n";
        }
        out += "}\n";
    }
    return out;
}

}  // namespace toms
