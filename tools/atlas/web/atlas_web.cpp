// atlas_web.cpp -- the embind API of the web atlas tool (atlas_core compiled to WebAssembly).
//
// The editor works on EMBEDDED projects only (atlas_store.h): the Project held here owns its
// images in memory (Project::images, Variant::images). The browser writes raw file bytes (an opened
// project with its packed atlas, PNGs to add, reference folders) into Emscripten's in-memory file
// system (MEMFS) and calls these functions; images are decoded here (stb) exactly as the native
// tool does, so a web build gives the same atlas as atlaspack. Saving runs saveProjectAll() into a
// MEMFS folder that the page then zips. Undo/redo: snapshot() keeps a copy of the Project (cheap:
// images are shared pointers) under an id, restore(id) brings it back. Under Node the real disk is
// mounted with NODEFS and cliMain() runs the atlaspack command line (atlaspack.mjs).
//
// JS:  const M = await createAtlasModule();  const A = new M.AtlasWeb();
//      A.newProject('atlas', '/work/atlas.atlasproj');  A.setImageBytes('hero', pngBytes, '');
//      const r = JSON.parse(A.build(''));  const rgba = A.getPageRGBA(0);
#include "atlas_build.h"
#include "atlas_cli.h"
#include "atlas_export.h"
#include "atlas_store.h"

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <string>
#include <vector>

using namespace atlas;
using emscripten::val;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

val bytesToJs(const uint8_t* data, size_t size) {
    // A copy: the view into the wasm heap dies when memory grows.
    return val(emscripten::typed_memory_view(size, data)).call<val>("slice");
}

json rectJson(const IRect& r) { return json::array({r.x, r.y, r.w, r.h}); }

const char* levelName(Diagnostic::Level l) {
    return l == Diagnostic::Error ? "error" : l == Diagnostic::Warning ? "warning" : "info";
}

json diagJson(const std::vector<Diagnostic>& ds) {
    json a = json::array();
    for (const Diagnostic& d : ds) a.push_back({{"level", levelName(d.level)}, {"sprite", d.sprite}, {"message", d.message}});
    return a;
}

json regionJson(const Region& r) {
    json j{{"name", r.name}, {"page", r.page}, {"frame", rectJson(r.frame)},
           {"offsetX", r.offsetX}, {"offsetY", r.offsetY}, {"origW", r.origW}, {"origH", r.origH},
           {"pivot", {r.pivot[0], r.pivot[1]}}, {"hasSplit", r.hasSplit},
           {"split", {r.split[0], r.split[1], r.split[2], r.split[3]}}, {"tags", r.tags},
           {"child", r.child}, {"parent", r.parent}, {"local", rectJson(r.local)}, {"baked", r.baked},
           {"aliasOf", r.aliasOf}, {"sourceFile", r.sourceFile}, {"trimmed", r.trimmed()}};
    return j;
}

std::string errJson(const std::string& e) { return json{{"error", e}}.dump(); }

int countPngs(const ImageSource& src, const std::string& dir, bool recursive) {
    return (int)src.listPngs(dir, recursive).size();
}

class AtlasWeb {
public:
    AtlasWeb() {
        src_ = cachedImageSource(cache_);
        newProject("atlas", "/work/atlas.atlasproj");
    }

    // ---- files (MEMFS, or NODEFS under node) ----
    bool writeFile(const std::string& path, const val& bytes) {
        const std::vector<uint8_t> data = emscripten::convertJSArrayToNumberVector<uint8_t>(bytes);
        std::error_code ec;
        fs::create_directories(u8path(path).parent_path(), ec);
        std::ofstream f(u8path(path), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write((const char*)data.data(), (std::streamsize)data.size());
        invalidateImages();
        return (bool)f;
    }
    val readFile(const std::string& path) const {
        std::ifstream f(u8path(path), std::ios::binary);
        if (!f) return val::null();
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return bytesToJs(data.data(), data.size());
    }
    bool removeFile(const std::string& path) {
        std::error_code ec;
        const auto n = fs::remove_all(u8path(path), ec);
        invalidateImages();
        return !ec && n > 0;
    }
    bool exists(const std::string& path) const {
        std::error_code ec;
        return fs::exists(u8path(path), ec);
    }
    // Every regular file below dir (recursive), '/' paths, sorted. JSON array.
    std::string listFiles(const std::string& dir) const {
        std::vector<std::string> out;
        std::error_code ec;
        if (fs::is_directory(u8path(dir), ec))
            for (fs::recursive_directory_iterator it(u8path(dir), ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec)) out.push_back(u8str(it->path()));
        std::sort(out.begin(), out.end());
        return json(out).dump();
    }

    // ---- project ----
    // A new, empty embedded project (its packed atlas goes next to the project file).
    void newProject(const std::string& name, const std::string& path) {
        Project p;
        p.name = name.empty() ? "atlas" : name;
        p.embedded = true;
        p.output.dir = ".";
        p.filePath = normalizePath(path);
        project_ = std::move(p);
    }

    // The project's JSON as edited by the page. The images stay what they are here (the JSON does
    // not hold them): base images are kept, a variant keeps its art by id (or by position when the
    // id was just renamed). Returns "" or the error. path: where the project "lives".
    std::string setProject(const std::string& text, const std::string& path) {
        Project p;
        std::string err;
        if (!projectFromJson(text, p, &err)) return err.empty() ? "invalid project" : err;
        p.filePath = normalizePath(path);
        p.embedded = true;   // the web editor works on embedded projects only
        p.images = project_.images;
        for (size_t i = 0; i < p.variants.size(); i++) {
            Variant& v = p.variants[i];
            const Variant* old = project_.findVariant(v.id);
            if (!old && i < project_.variants.size() && !p.findVariant(project_.variants[i].id)) old = &project_.variants[i];
            v.images = old ? old->images : std::map<std::string, ImagePtr>();
        }
        project_ = std::move(p);
        return std::string();
    }
    std::string getProject() const { return projectToJson(project_); }
    std::string projectPath() const { return project_.filePath; }
    std::string resolvePath(const std::string& rel) const { return project_.resolve(rel); }
    std::string relativize(const std::string& path) const { return project_.relativize(path); }

    // What opening `path` needs, before opening it: JSON {error} or {embedded, name, store,
    // storeExists, variants:[{id, store, dir, needed, exists}], sources:[{path, dir, pngs}],
    // overrides:[{id, path, dir, pngs}]}.
    std::string inspect(const std::string& path) {
        Project p;
        std::string err;
        if (!loadProject(path, p, &err)) return errJson(err.empty() ? "cannot read " + path : err);
        std::error_code ec;
        json r{{"embedded", p.embedded}, {"name", p.name}};
        json vars = json::array(), sources = json::array(), overrides = json::array();
        if (p.embedded) {
            r["store"] = storePath(p);
            r["storeExists"] = fs::is_regular_file(u8path(storePath(p)), ec);
            for (const Variant& v : p.variants) {
                const std::string s = storePath(p, &v);
                vars.push_back({{"id", v.id}, {"store", s}, {"dir", parentDir(s)}, {"needed", !v.images.empty()},
                                {"exists", fs::is_regular_file(u8path(s), ec)}});
            }
        } else {
            for (const SourceFolder& f : p.sources)
                sources.push_back({{"path", f.path}, {"dir", p.resolve(f.path)}, {"pngs", countPngs(src_, p.resolve(f.path), f.recursive)}});
            for (const Variant& v : p.variants)
                if (!v.overrideDir.empty())
                    overrides.push_back({{"id", v.id}, {"path", v.overrideDir}, {"dir", p.resolve(v.overrideDir)},
                                         {"pngs", countPngs(src_, p.resolve(v.overrideDir), true)}});
        }
        r["variants"] = vars;
        r["sources"] = sources;
        r["overrides"] = overrides;
        return r.dump();
    }

    // Opens a project from MEMFS: an embedded one gets its images cut out of the packed atlas; a
    // folder project is converted (embedSources, its folders must be in MEMFS). dropMissingVariants:
    // a variant whose packed atlas is missing loses its art instead of failing the open.
    // JSON {error} or {project, converted, images, dropped:[ids], diagnostics}.
    std::string openProject(const std::string& path, bool dropMissingVariants) {
        Project p;
        std::string err;
        json dropped = json::array();
        std::vector<Diagnostic> diags;
        int converted = -1;
        try {
            if (!loadProject(path, p, &err)) return errJson(err.empty() ? "cannot read " + path : err);
            if (p.embedded) {
                std::error_code ec;
                if (dropMissingVariants)
                    for (Variant& v : p.variants)
                        if (!v.images.empty() && !fs::is_regular_file(u8path(storePath(p, &v)), ec)) {
                            v.images.clear();
                            dropped.push_back(v.id);
                        }
                if (!loadEmbeddedImages(p, &err)) return errJson(path + ": " + err);
            } else {
                converted = embedSources(p, src_, &diags);
            }
        } catch (const std::exception& e) {
            return errJson(e.what());
        }
        project_ = std::move(p);
        return json{{"project", projectToJson(project_)}, {"converted", converted}, {"images", (int)project_.images.size()},
                    {"dropped", dropped}, {"diagnostics", diagJson(diags)}}.dump();
    }

    // ---- images ----
    // Adds or replaces an image from PNG bytes (variant: that variant's replacement art). "" or the error.
    std::string setImageBytes(const std::string& name, const val& bytes, const std::string& variant) {
        const std::vector<uint8_t> data = emscripten::convertJSArrayToNumberVector<uint8_t>(bytes);
        Image img;
        std::string err;
        if (!loadImageFromMemory(data.data(), data.size(), img, &err)) return name + ": " + (err.empty() ? "not a PNG" : err);
        if (!setImage(project_, name, img, variant, &err)) return err.empty() ? "cannot set " + name : err;
        return std::string();
    }
    std::string setImageFile(const std::string& name, const std::string& path, const std::string& variant) {
        Image img;
        std::string err;
        if (!loadImage(path, img, &err)) return err.empty() ? "cannot read " + path : err;
        if (!setImage(project_, name, img, variant, &err)) return err.empty() ? "cannot set " + name : err;
        return std::string();
    }
    bool hasImage(const std::string& name, const std::string& variant) const {
        if (variant.empty()) return project_.images.count(name) > 0;
        const Variant* v = project_.findVariant(variant);
        return v && v->images.count(name) > 0;
    }
    std::string imageNames(const std::string& variant) const {
        std::vector<std::string> out;
        const Variant* v = variant.empty() ? nullptr : project_.findVariant(variant);
        for (const auto& kv : v ? v->images : project_.images) out.push_back(kv.first);
        return json(out).dump();
    }
    std::string imageNameFor(const std::string& file, const std::string& folder, const std::string& prefix) const {
        return atlas::imageNameFor(file, folder, prefix);
    }

    // Renames an image or child sprite everywhere: the images (base and every variant), its entry,
    // children's parents and animation frames. "" or the error.
    std::string renameSprite(const std::string& from, const std::string& to) {
        if (to.empty()) return "a sprite needs a name";
        if (to == from) return std::string();
        if (project_.images.count(to) || project_.findSprite(to)) return "'" + to + "' already exists";
        bool found = false;
        auto move = [&](std::map<std::string, ImagePtr>& m) {
            auto it = m.find(from);
            if (it == m.end()) return false;
            ImagePtr img = it->second;
            m.erase(it);
            m[to] = img;
            return true;
        };
        found |= move(project_.images);
        for (Variant& v : project_.variants) move(v.images);
        for (SpriteDef& d : project_.sprites) {
            if (d.name == from) { d.name = to; found = true; }
            if (d.parent == from) d.parent = to;
        }
        for (Animation& a : project_.animations)
            for (AnimFrame& f : a.frames) if (f.sprite == from) f.sprite = to;
        return found ? std::string() : "no sprite '" + from + "'";
    }

    // Deletes sprites (JSON array of names) with their children: images, variant art, entries,
    // animation frames. Returns how many sprites went.
    int deleteSprites(const std::string& namesJson) {
        std::set<std::string> victims;
        for (const json& n : json::parse(namesJson, nullptr, false)) if (n.is_string()) victims.insert(n.get<std::string>());
        for (bool more = true; more;) {   // children of children
            more = false;
            for (const SpriteDef& d : project_.sprites)
                if (d.isChild() && victims.count(d.parent) && victims.insert(d.name).second) more = true;
        }
        int n = 0;
        for (const std::string& s : victims) {
            bool any = project_.images.erase(s) > 0;
            for (Variant& v : project_.variants) v.images.erase(s);
            any |= project_.findSprite(s) != nullptr;
            n += any;
        }
        project_.sprites.erase(std::remove_if(project_.sprites.begin(), project_.sprites.end(),
                                              [&](const SpriteDef& d) { return victims.count(d.name) > 0; }),
                               project_.sprites.end());
        for (Animation& a : project_.animations)
            a.frames.erase(std::remove_if(a.frames.begin(), a.frames.end(), [&](const AnimFrame& f) { return victims.count(f.sprite) > 0; }),
                           a.frames.end());
        return n;
    }

    // Reference folders (project, or one variant's) compared with the images held: JSON
    // [{name, file, status: "new"|"changed"|"same"}].
    std::string scanReferences(const std::string& variant) const {
        json a = json::array();
        static const char* st[] = {"new", "changed", "same"};
        for (const ReferenceImage& r : atlas::scanReferences(project_, src_, variant))
            a.push_back({{"name", r.name}, {"file", r.file}, {"status", st[r.status]}});
        return a.dump();
    }

    // ---- undo snapshots ----
    int snapshot() {
        const int id = nextSnap_++;
        snaps_[id] = project_;
        return id;
    }
    bool restore(int id) {
        auto it = snaps_.find(id);
        if (it == snaps_.end()) return false;
        project_ = it->second;
        return true;
    }
    // Forgets every snapshot not in idsJson (the ids the page's undo/redo stacks still use).
    int keepSnapshots(const std::string& idsJson) {
        std::set<int> keep;
        for (const json& n : json::parse(idsJson, nullptr, false)) if (n.is_number_integer()) keep.insert(n.get<int>());
        for (auto it = snaps_.begin(); it != snaps_.end();) it = keep.count(it->first) ? std::next(it) : snaps_.erase(it);
        return (int)snaps_.size();
    }

    // ---- save / extract ----
    // The folders a save at `path` writes the packed atlases to (base first, then each variant), JSON.
    std::string outputDirs(const std::string& path) const {
        Project q = project_;
        q.filePath = normalizePath(path);
        json a = json::array();
        a.push_back(q.resolve(q.output.dir.empty() ? "." : q.output.dir));
        for (const Variant& v : q.variants) a.push_back(q.resolve(v.output.dir.empty() ? "." : v.output.dir));
        return a.dump();
    }
    // saveProjectAll() of a copy placed at `path` (relative paths are kept as they are, so the
    // saved project matches the folder layout around it). JSON {error} or {written, diagnostics}.
    std::string saveAll(const std::string& path, const std::string& alsoCsv) const {
        Project q = project_;
        q.filePath = normalizePath(path);
        SaveResult res;
        std::string err;
        try {
            if (!saveProjectAll(path, q, splitCsv(alsoCsv), &res, &err)) return errJson(err.empty() ? "save failed" : err);
        } catch (const std::exception& e) {
            return errJson(e.what());
        }
        return json{{"written", res.written}, {"diagnostics", diagJson(res.diagnostics)}}.dump();
    }
    std::string extract(const std::string& dir, const std::string& variant, bool children) const {
        std::vector<std::string> written;
        std::string err;
        try {
            if (!extractImages(project_, dir, variant, children, &written, &err)) return errJson(err.empty() ? "extract failed" : err);
        } catch (const std::exception& e) {
            return errJson(e.what());
        }
        return json{{"written", written}}.dump();
    }

    // The sprites as the build sees them, JSON.
    std::string resolve() const {
        std::vector<Diagnostic> diags;
        const std::vector<ResolvedSprite> rs = resolveSprites(project_, src_, &diags);
        json a = json::array();
        for (const ResolvedSprite& r : rs)
            a.push_back({{"name", r.def.name}, {"file", r.file}, {"scanned", r.scanned}, {"child", r.def.isChild()},
                         {"parent", r.def.parent}, {"exclude", r.def.exclude}, {"pinned", r.def.pinned}});
        return json{{"sprites", a}, {"diagnostics", diagJson(diags)}}.dump();
    }

    // ---- build ----
    std::string build(const std::string& variant) {
        BuildOptions o;
        o.variant = variant;
        o.composite = true;
        try {
            result_ = atlas::build(project_, src_, o);
        } catch (const std::exception& e) {
            result_ = BuildResult();
            result_.diagnostics.push_back({Diagnostic::Error, "", std::string("build failed: ") + e.what()});
        }
        return resultJson(result_);
    }
    int pageCount() const { return (int)result_.pages.size(); }
    val getPageRGBA(int i) const {
        if (i < 0 || i >= (int)result_.pages.size()) return val::null();
        const Image& img = result_.pages[i].image;
        return bytesToJs(img.px.data(), img.px.size());
    }
    val getPagePng(int i) const {
        if (i < 0 || i >= (int)result_.pages.size()) return val::null();
        const std::vector<uint8_t> png = encodePng(result_.pages[i].image);
        return bytesToJs(png.data(), png.size());
    }
    // One region at its original size, as a game draws it: {w, h, data(RGBA)} or null.
    val getRegionRGBA(const std::string& name) const {
        const Region* r = result_.find(name);
        if (!r) return val::null();
        const Image img = renderRegion(result_, *r);
        val o = val::object();
        o.set("w", img.w);
        o.set("h", img.h);
        o.set("data", bytesToJs(img.px.data(), img.px.size()));
        return o;
    }
    // The same as a PNG file (Save image as), or null.
    val getRegionPng(const std::string& name) const {
        const Region* r = result_.find(name);
        if (!r) return val::null();
        const std::vector<uint8_t> png = encodePng(renderRegion(result_, *r));
        return bytesToJs(png.data(), png.size());
    }

    // ---- export / import ----
    // formatsCsv: "atlas,tp-json"; empty = the project's (or variant's) formats.
    // Returns an array of {name, data:Uint8Array}; on error {error}.
    val exportFiles(const std::string& formatsCsv) const {
        std::vector<std::string> formats = splitCsv(formatsCsv);
        if (formats.empty()) formats = project_.output.formats;
        std::vector<ExportFile> files;
        std::string err;
        if (result_.pages.empty() && result_.regions.empty()) err = "nothing built";
        else if (!result_.ok()) err = std::to_string(result_.errorCount()) + " error(s): fix the problems first";
        else if (!atlas::exportFiles(result_, formats, files, &err) && err.empty()) err = "export failed";
        if (!err.empty()) {
            val o = val::object();
            o.set("error", err);
            return o;
        }
        val a = val::array();
        for (const ExportFile& f : files) {
            val o = val::object();
            o.set("name", f.name);
            o.set("data", bytesToJs(f.data.data(), f.data.size()));
            a.call<void>("push", o);
        }
        return a;
    }
    // Reads an atlas (.pi/.atlas/.json; its page PNGs next to it), cuts the sprites out into memory
    // and makes that the current (embedded) project at projectPath. JSON {error} or
    // {project, diagnostics, sprites, children}.
    std::string importAtlas(const std::string& path, const std::string& projectPath) {
        BuildResult in;
        std::string err;
        json r;
        try {
            if (!importAtlasFile(path, in, &err)) return errJson(err.empty() ? "cannot read " + path : err);
            Project p;
            if (!importToProject(in, projectPath, "", p, &err)) return errJson(err.empty() ? "import failed" : err);
            std::string ext = u8str(u8path(path).extension());
            for (char& c : ext) c = (char)tolower((unsigned char)c);
            p.output.formats = {ext == ".pi" ? "pi" : ext == ".json" ? "tp-json" : "atlas"};
            p.output.dir = ".";
            p.embedded = true;
            project_ = p;
            int children = 0;
            for (const Region& rg : in.regions) children += rg.child;
            r = {{"project", projectToJson(project_)}, {"diagnostics", diagJson(in.diagnostics)},
                 {"sprites", (int)in.regions.size()}, {"children", children}};
        } catch (const std::exception& e) {
            return errJson(e.what());
        }
        return r.dump();
    }

    std::string formats() const { return json(exportFormats()).dump(); }

private:
    void invalidateImages() {
        cache_.reset();
        src_ = cachedImageSource(cache_);
    }

    static std::vector<std::string> splitCsv(const std::string& s) {
        std::vector<std::string> v;
        std::string cur;
        for (char c : s + ",") {
            if (c == ',') { if (!cur.empty()) v.push_back(cur); cur.clear(); }
            else if (c != ' ') cur += c;
        }
        return v;
    }

    static std::string resultJson(const BuildResult& b) {
        json pages = json::array();
        long long pageArea = 0, used = 0;
        for (const Page& p : b.pages) {
            pages.push_back({{"file", p.file}, {"w", p.image.w}, {"h", p.image.h}});
            pageArea += (long long)p.image.w * p.image.h;
        }
        json regions = json::array();
        std::set<std::tuple<int, int, int, int, int>> placed;   // dedupe/children share pixels
        int images = 0, children = 0, aliases = 0;
        for (const Region& r : b.regions) {
            regions.push_back(regionJson(r));
            if (r.child && !r.baked) children++;
            else if (!r.aliasOf.empty()) aliases++;
            else images++;
            if ((!r.child || r.baked) && placed.insert({r.page, r.frame.x, r.frame.y, r.frame.w, r.frame.h}).second)
                used += (long long)r.frame.w * r.frame.h;
        }
        json stats{{"pages", (int)b.pages.size()}, {"regions", (int)b.regions.size()}, {"images", images},
                   {"children", children}, {"aliases", aliases}, {"errors", b.errorCount()},
                   {"warnings", b.warningCount()}, {"pageArea", pageArea}, {"usedArea", used},
                   {"occupancy", pageArea ? (double)used / (double)pageArea : 0.0}};
        json anims = json::array();
        for (const Animation& a : b.animations) {
            json fr = json::array();
            for (const AnimFrame& f : a.frames) fr.push_back({{"sprite", f.sprite}, {"time", f.time}});
            anims.push_back({{"name", a.name}, {"loop", a.loop}, {"frames", fr}});
        }
        return json{{"name", b.name}, {"ok", b.ok()}, {"pages", pages}, {"regions", regions},
                    {"diagnostics", diagJson(b.diagnostics)}, {"animations", anims}, {"stats", stats}}.dump();
    }

    Project project_;
    std::shared_ptr<void> cache_;
    ImageSource src_;
    BuildResult result_;
    std::map<int, Project> snaps_;
    int nextSnap_ = 1;
};

// atlaspack's command line. args: a JS array of strings. Returns the exit code.
int cliMain(const val& args) {
    std::vector<std::string> v;
    const int n = args["length"].as<int>();
    for (int i = 0; i < n; i++) v.push_back(args[i].as<std::string>());
    int code = 2;
    try {
        code = atlasCliMain(v);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
    }
    std::fflush(stdout);
    std::fflush(stderr);
    return code;
}

}  // namespace

EMSCRIPTEN_BINDINGS(atlas_web) {
    emscripten::class_<AtlasWeb>("AtlasWeb")
        .constructor<>()
        .function("writeFile", &AtlasWeb::writeFile)
        .function("readFile", &AtlasWeb::readFile)
        .function("removeFile", &AtlasWeb::removeFile)
        .function("exists", &AtlasWeb::exists)
        .function("listFiles", &AtlasWeb::listFiles)
        .function("newProject", &AtlasWeb::newProject)
        .function("setProject", &AtlasWeb::setProject)
        .function("getProject", &AtlasWeb::getProject)
        .function("projectPath", &AtlasWeb::projectPath)
        .function("resolvePath", &AtlasWeb::resolvePath)
        .function("relativize", &AtlasWeb::relativize)
        .function("inspect", &AtlasWeb::inspect)
        .function("openProject", &AtlasWeb::openProject)
        .function("setImageBytes", &AtlasWeb::setImageBytes)
        .function("setImageFile", &AtlasWeb::setImageFile)
        .function("hasImage", &AtlasWeb::hasImage)
        .function("imageNames", &AtlasWeb::imageNames)
        .function("imageNameFor", &AtlasWeb::imageNameFor)
        .function("renameSprite", &AtlasWeb::renameSprite)
        .function("deleteSprites", &AtlasWeb::deleteSprites)
        .function("scanReferences", &AtlasWeb::scanReferences)
        .function("snapshot", &AtlasWeb::snapshot)
        .function("restore", &AtlasWeb::restore)
        .function("keepSnapshots", &AtlasWeb::keepSnapshots)
        .function("outputDirs", &AtlasWeb::outputDirs)
        .function("saveAll", &AtlasWeb::saveAll)
        .function("extract", &AtlasWeb::extract)
        .function("resolve", &AtlasWeb::resolve)
        .function("build", &AtlasWeb::build)
        .function("pageCount", &AtlasWeb::pageCount)
        .function("getPageRGBA", &AtlasWeb::getPageRGBA)
        .function("getPagePng", &AtlasWeb::getPagePng)
        .function("getRegionRGBA", &AtlasWeb::getRegionRGBA)
        .function("getRegionPng", &AtlasWeb::getRegionPng)
        .function("exportFiles", &AtlasWeb::exportFiles)
        .function("importAtlas", &AtlasWeb::importAtlas)
        .function("formats", &AtlasWeb::formats);
    emscripten::function("cliMain", &cliMain);
}
