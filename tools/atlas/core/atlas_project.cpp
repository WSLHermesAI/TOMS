// atlas_project.cpp -- see atlas_project.h.
#include "atlas_project.h"

#include <json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace atlas {

// ---- paths -------------------------------------------------------------------------------------

fs::path u8path(const std::string& utf8) { return fs::u8path(utf8); }
std::string u8str(const fs::path& p) { return normalizePath(p.generic_u8string()); }

std::string normalizePath(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

static bool isAbsolute(const std::string& p) {
    return (!p.empty() && p[0] == '/') || (p.size() > 1 && p[1] == ':');
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty() || a == "." || isAbsolute(b)) return normalizePath(b);
    if (b.empty() || b == ".") return normalizePath(a);
    std::string r = normalizePath(a);
    if (r.back() != '/') r += '/';
    return r + normalizePath(b);
}

std::string parentDir(const std::string& p) {
    const std::string n = normalizePath(p);
    const size_t s = n.find_last_of('/');
    if (s == std::string::npos) return ".";
    if (s == 0) return "/";
    return n.substr(0, s);
}

std::string fileName(const std::string& p) {
    const std::string n = normalizePath(p);
    const size_t s = n.find_last_of('/');
    return s == std::string::npos ? n : n.substr(s + 1);
}

std::string fileStem(const std::string& p) {
    std::string f = fileName(p);
    const size_t d = f.find_last_of('.');
    return d == std::string::npos || d == 0 ? f : f.substr(0, d);
}

std::string Project::baseDir() const { return filePath.empty() ? "." : parentDir(filePath); }

std::string Project::resolve(const std::string& rel) const {
    if (rel.empty()) return rel;
    return u8str(u8path(joinPath(baseDir(), rel)).lexically_normal());
}

std::string Project::relativize(const std::string& path) const {
    std::error_code ec;
    const fs::path base = fs::absolute(u8path(baseDir()), ec).lexically_normal();
    const fs::path abs = fs::absolute(u8path(path), ec).lexically_normal();
    const fs::path rel = abs.lexically_relative(base);
    if (rel.empty()) return normalizePath(path);
    return u8str(rel);
}

SpriteDef* Project::findSprite(const std::string& n) {
    for (SpriteDef& s : sprites) if (s.name == n) return &s;
    return nullptr;
}
const SpriteDef* Project::findSprite(const std::string& n) const {
    for (const SpriteDef& s : sprites) if (s.name == n) return &s;
    return nullptr;
}
const Variant* Project::findVariant(const std::string& id) const {
    for (const Variant& v : variants) if (v.id == id) return &v;
    return nullptr;
}

// ---- JSON --------------------------------------------------------------------------------------

static void outputToJson(json& j, const Output& o) {
    j["dir"] = o.dir;
    if (!o.name.empty()) j["name"] = o.name;
    j["formats"] = o.formats;
}

static void outputFromJson(const json& j, Output& o) {
    if (!j.is_object()) return;
    o.dir = j.value("dir", o.dir);
    o.name = j.value("name", o.name);
    if (j.contains("formats") && j["formats"].is_array()) o.formats = j["formats"].get<std::vector<std::string>>();
}

std::string projectToJson(const Project& p) {
    const Settings& s = p.settings;
    json j;
    j["version"] = p.version;
    j["name"] = p.name;
    j["settings"] = {
        {"maxWidth", s.maxWidth}, {"maxHeight", s.maxHeight}, {"minWidth", s.minWidth}, {"minHeight", s.minHeight},
        {"powerOfTwo", s.powerOfTwo}, {"square", s.square}, {"fixedSize", s.fixedSize},
        {"padding", s.padding}, {"border", s.border}, {"extrude", s.extrude},
        {"trim", s.trim}, {"alphaThreshold", s.alphaThreshold}, {"dedupe", s.dedupe},
        {"premultiplyAlpha", s.premultiplyAlpha}, {"filter", s.filter},
        {"heuristic", heuristicName(s.heuristic)},
        {"defaultPivot", {s.defaultPivot[0], s.defaultPivot[1]}}};
    outputToJson(j["output"], p.output);
    auto folders = [](const std::vector<SourceFolder>& v) {
        json a = json::array();
        for (const SourceFolder& f : v) a.push_back({{"path", f.path}, {"recursive", f.recursive}, {"prefix", f.prefix}});
        return a;
    };
    if (p.embedded) {
        j["embedded"] = true;
        j["references"] = folders(p.references);
    }
    if (!p.embedded || !p.sources.empty()) j["sources"] = folders(p.sources);
    j["sprites"] = json::array();
    for (const SpriteDef& d : p.sprites) {
        json e;
        e["name"] = d.name;
        if (!d.file.empty()) e["file"] = d.file;
        if (d.isChild()) {
            e["parent"] = d.parent;
            e["rect"] = {d.rect.x, d.rect.y, d.rect.w, d.rect.h};
            if (d.bake) e["bake"] = true;
        }
        if (d.exclude) e["exclude"] = true;
        if (d.hasPivot) e["pivot"] = {d.pivot[0], d.pivot[1]};
        if (d.hasSplit) e["split"] = {d.split[0], d.split[1], d.split[2], d.split[3]};
        if (d.trim >= 0) e["trim"] = d.trim == 1;
        if (d.pinned) e["pin"] = {d.pinPage, d.pinX, d.pinY};
        if (!d.tags.empty()) e["tags"] = d.tags;
        j["sprites"].push_back(e);
    }
    j["animations"] = json::array();
    for (const Animation& a : p.animations) {
        json frames = json::array();
        for (const AnimFrame& f : a.frames) frames.push_back({{"sprite", f.sprite}, {"time", f.time}});
        j["animations"].push_back({{"name", a.name}, {"loop", a.loop}, {"frames", frames}});
    }
    j["variants"] = json::array();
    for (const Variant& v : p.variants) {
        json e{{"id", v.id}};
        if (!v.overrideDir.empty()) e["overrides"] = v.overrideDir;
        if (!v.reference.empty()) e["reference"] = v.reference;
        if (!v.images.empty()) {
            json names = json::array();
            for (const auto& kv : v.images) names.push_back(kv.first);
            e["images"] = names;
        }
        outputToJson(e["output"], v.output);
        j["variants"].push_back(e);
    }
    return j.dump(2) + "\n";
}

template <class T> static void readArr(const json& j, const char* key, T* out, size_t n) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != n) return;
    for (size_t i = 0; i < n; i++) out[i] = j[key][i].get<T>();
}

bool projectFromJson(const std::string& text, Project& p, std::string* err) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        if (err) *err = std::string("invalid JSON: ") + e.what();
        return false;
    }
    if (!j.is_object()) { if (err) *err = "project root must be an object"; return false; }
    try {
        p.version = j.value("version", 1);
        p.name = j.value("name", p.name);
        if (j.contains("settings")) {
            const json& sj = j["settings"];
            Settings& s = p.settings;
            s.maxWidth = sj.value("maxWidth", s.maxWidth);
            s.maxHeight = sj.value("maxHeight", s.maxHeight);
            s.minWidth = sj.value("minWidth", s.minWidth);
            s.minHeight = sj.value("minHeight", s.minHeight);
            s.powerOfTwo = sj.value("powerOfTwo", s.powerOfTwo);
            s.square = sj.value("square", s.square);
            s.fixedSize = sj.value("fixedSize", s.fixedSize);
            s.padding = sj.value("padding", s.padding);
            s.border = sj.value("border", s.border);
            s.extrude = sj.value("extrude", s.extrude);
            s.trim = sj.value("trim", s.trim);
            s.alphaThreshold = sj.value("alphaThreshold", s.alphaThreshold);
            s.dedupe = sj.value("dedupe", s.dedupe);
            s.premultiplyAlpha = sj.value("premultiplyAlpha", s.premultiplyAlpha);
            s.filter = sj.value("filter", s.filter);
            const std::string h = sj.value("heuristic", std::string("auto"));
            if (!parseHeuristic(h, s.heuristic)) { if (err) *err = "unknown heuristic '" + h + "'"; return false; }
            readArr(sj, "defaultPivot", s.defaultPivot, 2);
        }
        if (j.contains("output")) outputFromJson(j["output"], p.output);
        p.embedded = j.value("embedded", false);
        auto folders = [](const json& a) {
            std::vector<SourceFolder> v;
            for (const json& f : a) {
                SourceFolder sf;
                sf.path = normalizePath(f.value("path", std::string()));
                sf.recursive = f.value("recursive", true);
                sf.prefix = f.value("prefix", std::string());
                v.push_back(sf);
            }
            return v;
        };
        p.sources = folders(j.value("sources", json::array()));
        p.references = folders(j.value("references", json::array()));
        p.images.clear();
        p.sprites.clear();
        for (const json& e : j.value("sprites", json::array())) {
            SpriteDef d;
            d.name = e.value("name", std::string());
            d.file = normalizePath(e.value("file", std::string()));
            d.parent = e.value("parent", std::string());
            if (e.contains("rect")) {
                int r[4] = {0, 0, 0, 0};
                readArr(e, "rect", r, 4);
                d.rect = {r[0], r[1], r[2], r[3]};
            }
            d.bake = e.value("bake", false);
            d.exclude = e.value("exclude", false);
            if (e.contains("pivot")) { d.hasPivot = true; readArr(e, "pivot", d.pivot, 2); }
            if (e.contains("split")) { d.hasSplit = true; readArr(e, "split", d.split, 4); }
            if (e.contains("trim")) d.trim = e["trim"].get<bool>() ? 1 : 0;
            if (e.contains("pin")) {
                int pin[3] = {0, 0, 0};
                readArr(e, "pin", pin, 3);
                d.pinned = true; d.pinPage = pin[0]; d.pinX = pin[1]; d.pinY = pin[2];
            }
            if (e.contains("tags")) d.tags = e["tags"].get<std::vector<std::string>>();
            p.sprites.push_back(d);
        }
        p.animations.clear();
        for (const json& a : j.value("animations", json::array())) {
            Animation an;
            an.name = a.value("name", std::string());
            an.loop = a.value("loop", true);
            for (const json& f : a.value("frames", json::array()))
                an.frames.push_back({f.value("sprite", std::string()), f.value("time", 0.1f)});
            p.animations.push_back(an);
        }
        p.variants.clear();
        for (const json& v : j.value("variants", json::array())) {
            Variant vr;
            vr.id = v.value("id", std::string());
            vr.overrideDir = normalizePath(v.value("overrides", std::string()));
            vr.reference = normalizePath(v.value("reference", std::string()));
            for (const json& n : v.value("images", json::array())) vr.images[n.get<std::string>()] = nullptr;
            vr.output = p.output;
            if (v.contains("output")) outputFromJson(v["output"], vr.output);
            p.variants.push_back(vr);
        }
    } catch (const std::exception& e) {
        if (err) *err = std::string("bad project field: ") + e.what();
        return false;
    }
    return true;
}

bool loadProject(const std::string& path, Project& out, std::string* err) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    Project p;
    if (!projectFromJson(ss.str(), p, err)) { if (err) *err = path + ": " + *err; return false; }
    p.filePath = normalizePath(path);
    out = std::move(p);
    return true;
}

bool saveProject(const std::string& path, Project& p, std::string* err) {
    std::ofstream f(u8path(path), std::ios::binary);
    if (!f) { if (err) *err = "cannot write " + path; return false; }
    f << projectToJson(p);
    if (!f) { if (err) *err = "write failed: " + path; return false; }
    p.filePath = normalizePath(path);
    return true;
}

}  // namespace atlas
