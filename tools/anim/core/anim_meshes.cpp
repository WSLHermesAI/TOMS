// anim_meshes.cpp -- see anim_meshes.h.
#include "anim_meshes.h"
#include "anim_check.h"

#include "atlas_image.h"   // atlas_core: the page PNGs for the headless command

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>

namespace animed {

namespace fs = std::filesystem;

namespace {
std::string readAll(const fs::path& p, bool* ok = nullptr) {
    std::ifstream in(p, std::ios::binary);
    if (ok) *ok = (bool)in;
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}  // namespace

std::string meshBinPath(const std::string& meshPath) {
    const fs::path p = fs::u8path(meshPath);
    return (p.parent_path() / fs::u8path(p.stem().u8string() + ".bin")).u8string();   // game.mesh.json -> game.mesh.bin
}

void addAnimMeshes(const toms::anim::AnimFile& file, const toms::anim::AtlasSet& atlases, const PagePixels& pixels,
                   std::vector<toms::SpriteMeshFile>& perEntry, MeshReport* report,
                   std::vector<std::pair<int, std::string>>* needed) {
    if (perEntry.size() < atlases.entries.size()) perEntry.resize(atlases.entries.size());
    MeshReport rep;
    for (const toms::anim::MeshNeed& need : toms::anim::meshesNeeded(file, atlases)) {
        if (needed) needed->push_back({need.entry, need.key});
        toms::SpriteMeshFile& into = perEntry[(size_t)need.entry];
        if (const toms::SpriteMesh* was = into.find(need.key); was && was->source == "hand") {
            rep.kept++;
            continue;
        }
        const toms::AtlasRegion* r = need.region;
        if (r->rotated || r->child) {
            rep.skipped.push_back(need.key + ": " + (r->rotated ? "a rotated region" : "a child sprite (no pixels of its own)"));
            continue;
        }
        const uint8_t* rgba = nullptr;
        int w = 0, h = 0, stride = 0;
        if (!pixels || !pixels(need.entry, r->page, rgba, w, h, stride) || !rgba || r->x < 0 || r->y < 0 || r->x + r->w > w ||
            r->y + r->h > h) {
            rep.skipped.push_back(need.key + ": its atlas page is not loaded");
            continue;
        }
        toms::SpriteMesh m = toms::anim::cutMesh(need, rgba + ((size_t)r->y * stride + r->x) * 4, stride);
        if (m.triangles.empty()) {
            rep.skipped.push_back(need.key + ": no visible pixels");
            continue;
        }
        into.meshes[need.key] = std::move(m);
        rep.made++;
    }
    if (report) *report = std::move(rep);
}

bool readMeshFile(const std::string& meshPath, toms::SpriteMeshFile& out, bool* binary, std::string* err) {
    out = toms::SpriteMeshFile();
    if (binary) *binary = false;
    bool ok = false;
    const std::string text = readAll(fs::u8path(meshPath), &ok);
    if (!ok) return true;
    if (binary) *binary = text.find("\"buffers\"") != std::string::npos;
    const fs::path dir = fs::u8path(meshPath).parent_path();
    auto readBuffer = [&](const std::string& uri, std::string& bytes) {
        bool found = false;
        bytes = readAll(dir / fs::u8path(uri), &found);
        return found;
    };
    std::string e;
    if (toms::parseSpriteMeshes(text, readBuffer, out, &e)) return true;
    if (err) *err = meshPath + ": " + e;
    return false;
}

bool writeMeshFile(const std::string& meshPath, const toms::SpriteMeshFile& f, bool binary, std::string* err,
                   std::vector<std::string>* written) {
    const fs::path jsonPath = fs::u8path(meshPath), binPath = fs::u8path(meshBinPath(meshPath));
    std::error_code ec;
    if (f.meshes.empty()) {   // nothing to keep: no file
        for (const fs::path& p : {jsonPath, binPath})
            if (fs::exists(p, ec)) {
                fs::remove(p, ec);
                if (written) written->push_back(p.u8string());
            }
        return true;
    }
    std::string bin;
    const std::string json = toms::spriteMeshesToJson(f, binary ? binPath.filename().u8string() : std::string(), binary ? &bin : nullptr);
    auto put = [&](const fs::path& p, const std::string& bytes) {
        bool there = false;
        if (readAll(p, &there) == bytes && there) return true;   // unchanged: left alone (its timestamp too)
        std::ofstream o(p, std::ios::binary);
        o.write(bytes.data(), (std::streamsize)bytes.size());
        if (!o) {
            if (err) *err = "cannot write " + p.u8string();
            return false;
        }
        if (written) written->push_back(p.u8string());
        return true;
    };
    if (!put(jsonPath, json)) return false;
    if (binary) return put(binPath, bin);
    if (fs::exists(binPath, ec)) {   // now text: the old buffer goes
        fs::remove(binPath, ec);
        if (written) written->push_back(binPath.u8string());
    }
    return true;
}

namespace {
// One atlas the headless command works on (shared by every clip file that names it).
struct AtlasWork {
    std::string path;                  // as given / resolved
    toms::AtlasFile atlas;
    std::vector<atlas::Image> pages;
    toms::SpriteMeshFile disk, out;    // the mesh file as found; what it becomes
    bool binary = true, hadFile = false;
    std::set<std::string> needed;      // keys the clip files need
};
}  // namespace

int headlessMeshes(const std::vector<std::string>& args) {
    // args: "meshes" <file.anim>... [--atlas [<id>=]<path>]... [--bin|--text] [--check] [--prune]
    std::vector<std::string> files;
    std::vector<toms::anim::AtlasRef> overrides, defaults;
    std::map<std::string, std::string> mapped;   // absolute atlas path -> the one used instead
    auto absPath = [](const fs::path& p) { return fs::absolute(p).lexically_normal().u8string(); };
    auto atlasRef = [](const std::string& a) {
        toms::anim::AtlasRef r{toms::anim::defaultAtlasId(a), a};
        const size_t eq = a.find('=');
        if (eq != std::string::npos && eq > 0 && isAtlasId(a.substr(0, eq))) r = {a.substr(0, eq), a.substr(eq + 1)};
        return r;
    };
    bool wantBin = false, wantText = false, check = false, prune = false;
    for (size_t i = 1; i < args.size(); i++) {
        if (args[i] == "--bin") wantBin = true;
        else if (args[i] == "--text") wantText = true;
        else if (args[i] == "--check") check = true;
        else if (args[i] == "--prune") prune = true;
        else if (args[i] == "--atlas" && i + 1 < args.size()) overrides.push_back(atlasRef(args[++i]));
        else if (args[i] == "--default-atlas" && i + 1 < args.size()) defaults.push_back(atlasRef(args[++i]));
        else if (args[i] == "--map" && i + 1 < args.size()) {
            const std::string m = args[++i];
            const size_t eq = m.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= m.size()) {
                std::fprintf(stderr, "--map wants <from.atlas>=<to.atlas>: %s\n", m.c_str());
                return 3;
            }
            mapped[absPath(fs::u8path(m.substr(0, eq)))] = m.substr(eq + 1);
        } else if (args[i].rfind("--", 0) == 0) {
            std::fprintf(stderr, "unknown argument: %s\n", args[i].c_str());
            return 3;
        } else {
            files.push_back(args[i]);
        }
    }
    if (files.empty() || (wantBin && wantText)) {
        std::fputs("usage: anim_editor --headless meshes <file.anim>... [--atlas [<id>=]<file.atlas>]... [--default-atlas ...]\n"
                   "       [--map <from.atlas>=<to.atlas>]... [--bin|--text] [--check] [--prune]\n"
                   "  adds the meshes of the clips' skinned and deformed nodes (their sprites on their mesh grids) to the mesh\n"
                   "  file of each atlas they use, <atlas>.mesh.json (docs/engine/16_MESH_ANIMATION.md). --atlas: those atlases instead\n"
                   "  of each file's own; --default-atlas: for a file that names none; --map: an atlas swapped for another (an art\n"
                   "  style's copy). --bin / --text: the file's form (default: as it is; a new one\n"
                   "  binary). --prune: only what these clip files need stays. --check: write nothing, exit 1 when out of date\n",
                   stderr);
        return 3;
    }
    std::map<std::string, std::unique_ptr<AtlasWork>> works;   // by normalized path
    std::vector<std::string> order;
    std::string err;
    for (const std::string& file : files) {
        std::string text;
        toms::anim::AnimFile f;
        if (!readTextFile(file, text) || !toms::anim::parseAnim(text, f, &err)) {
            std::fprintf(stderr, "%s: error: %s\n", file.c_str(), err.empty() ? "cannot read the file" : err.c_str());
            return 2;
        }
        const bool fromArgs = !overrides.empty() || f.atlases.empty();   // as given on the command line (cwd-relative)
        const std::vector<toms::anim::AtlasRef>& refs = !overrides.empty() ? overrides : f.atlases.empty() ? defaults : f.atlases;
        toms::anim::AtlasSet set;
        std::vector<AtlasWork*> entryWork;
        for (const toms::anim::AtlasRef& ref : refs) {
            fs::path p = fromArgs || isAbsoluteAtlasPath(ref.path)
                             ? fs::u8path(ref.path)
                             : (fs::u8path(file).parent_path() / fs::u8path(ref.path)).lexically_normal();
            if (auto m = mapped.find(absPath(p)); m != mapped.end()) p = fs::u8path(m->second);
            const std::string key = absPath(p);
            auto it = works.find(key);
            if (it == works.end()) {
                auto w = std::make_unique<AtlasWork>();
                w->path = p.u8string();
                std::string atext;
                if (!readTextFile(w->path, atext) || !toms::parseAtlas(atext, w->atlas, &err)) {
                    std::fprintf(stderr, "%s: error: the atlas %s cannot be read%s%s\n", file.c_str(), w->path.c_str(),
                                 err.empty() ? "" : ": ", err.c_str());
                    return 2;
                }
                for (const toms::AtlasPage& pg : w->atlas.pages) {
                    atlas::Image img;
                    if (!atlas::loadImage((p.parent_path() / fs::u8path(pg.file)).u8string(), img, &err)) {
                        std::fprintf(stderr, "error: the page %s of %s: %s\n", pg.file.c_str(), w->path.c_str(), err.c_str());
                        return 2;
                    }
                    w->pages.push_back(std::move(img));
                }
                const std::string meshPath = toms::spriteMeshPath(w->path);
                w->hadFile = fs::exists(fs::u8path(meshPath));
                bool bin = false;
                if (!readMeshFile(meshPath, w->disk, &bin, &err)) std::fprintf(stderr, "warning: %s (made again)\n", err.c_str());
                w->binary = wantBin || (!wantText && (w->hadFile ? bin : true));
                w->out = prune ? toms::SpriteMeshFile() : w->disk;
                if (prune)   // "hand" meshes are kept if still needed: carried over for addAnimMeshes to find
                    for (const auto& [k, m] : w->disk.meshes)
                        if (m.source == "hand") w->out.meshes[k] = m;
                it = works.emplace(key, std::move(w)).first;
                order.push_back(key);
            }
            set.add(it->second->atlas, ref.id);
            entryWork.push_back(it->second.get());
        }
        std::vector<toms::SpriteMeshFile> perEntry;
        for (AtlasWork* w : entryWork) perEntry.push_back(w->out);
        const PagePixels pixels = [&](int entry, int page, const uint8_t*& rgba, int& w, int& h, int& stride) {
            if (entry < 0 || (size_t)entry >= entryWork.size()) return false;
            const auto& pp = entryWork[(size_t)entry]->pages;
            if (page < 0 || (size_t)page >= pp.size() || !pp[(size_t)page].valid()) return false;
            rgba = pp[(size_t)page].px.data();
            w = pp[(size_t)page].w;
            h = pp[(size_t)page].h;
            stride = w;
            return true;
        };
        MeshReport rep;
        std::vector<std::pair<int, std::string>> needed;
        addAnimMeshes(f, set, pixels, perEntry, &rep, &needed);
        for (const std::string& s : rep.skipped) std::fprintf(stderr, "%s: warning: no mesh for %s\n", file.c_str(), s.c_str());
        for (size_t e = 0; e < entryWork.size(); e++) entryWork[e]->out = std::move(perEntry[e]);
        for (const auto& [e, key] : needed) entryWork[(size_t)e]->needed.insert(key);
    }
    if (prune)   // carried-over "hand" meshes no clip needs go too
        for (const std::string& key : order) {
            AtlasWork& w = *works[key];
            for (auto it = w.out.meshes.begin(); it != w.out.meshes.end();)
                it = w.needed.count(it->first) ? std::next(it) : w.out.meshes.erase(it);
        }
    int outOfDate = 0;
    for (const std::string& key : order) {
        AtlasWork& w = *works[key];
        const std::string meshPath = toms::spriteMeshPath(w.path);
        if (check) {
            int stale = 0, extra = 0;
            for (const std::string& k : w.needed) {
                const toms::SpriteMesh* have = w.disk.find(k);
                const toms::SpriteMesh* want = w.out.find(k);
                if (want && (!have || !(*have == *want))) stale++;
            }
            if (prune)
                for (const auto& kv : w.disk.meshes) extra += !w.needed.count(kv.first);
            const bool ok = stale == 0 && extra == 0;
            outOfDate += !ok;
            if (ok) std::printf("%s: up to date (%zu mesh(es) these clips need)\n", meshPath.c_str(), w.needed.size());
            else
                std::printf("%s: out of date: %d mesh(es) missing or stale%s; run anim_editor --headless meshes, or save the clip in the anim editor\n",
                            meshPath.c_str(), stale, extra ? (", " + std::to_string(extra) + " no listed clip uses").c_str() : "");
            continue;
        }
        std::vector<std::string> written;
        if (!writeMeshFile(meshPath, w.out, w.binary, &err, &written)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
        std::printf("%s: %zu mesh(es), %zu needed by these clips, %s%s\n", meshPath.c_str(), w.out.meshes.size(), w.needed.size(),
                    w.binary ? "binary" : "text", written.empty() ? ", unchanged" : "");
    }
    return outOfDate ? 1 : 0;
}

}  // namespace animed
