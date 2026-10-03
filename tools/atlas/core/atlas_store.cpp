// atlas_store.cpp -- see atlas_store.h.
#include "atlas_store.h"
#include "atlas_export.h"

#include <algorithm>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace atlas {

std::string storePath(const Project& p, const Variant* v) {
    const Output& o = v ? v->output : p.output;
    return joinPath(p.resolve(o.dir), p.outputName(o) + ".atlas");
}

// ---- open --------------------------------------------------------------------------------------

namespace {

// Cuts the images out of one packed atlas. `want` = names to take (empty: every image region).
bool sliceStore(const Project& p, const std::string& atlasPath, const std::vector<std::string>& want,
                std::map<std::string, ImagePtr>& into, std::string* err) {
    BuildResult packed;
    std::string e;
    if (!importLibgdx(atlasPath, packed, &e)) {
        if (err) *err = "cannot read the packed atlas: " + e;
        return false;
    }
    auto take = [&](const Region& r) {
        Image img = renderRegion(packed, r);
        if (p.settings.premultiplyAlpha) unpremultiply(img);
        into[r.name] = std::make_shared<const Image>(std::move(img));
    };
    if (want.empty()) {
        for (const Region& r : packed.regions)
            if (!r.child) take(r);   // children (baked ones too) come from the project file
        return true;
    }
    for (const std::string& n : want) {
        const Region* r = packed.find(n);
        if (!r || r->child) {
            if (err) *err = atlasPath + " has no image '" + n + "'";
            return false;
        }
        take(*r);
    }
    return true;
}

bool sameVisible(const Image& a, const Image& b) {
    if (a.w != b.w || a.h != b.h) return false;
    for (size_t i = 0; i + 3 < a.px.size(); i += 4) {
        if (a.px[i + 3] == 0 && b.px[i + 3] == 0) continue;   // invisible: colour does not matter
        if (a.px[i] != b.px[i] || a.px[i + 1] != b.px[i + 1] || a.px[i + 2] != b.px[i + 2] || a.px[i + 3] != b.px[i + 3]) return false;
    }
    return true;
}

}  // namespace

bool loadEmbeddedImages(Project& p, std::string* err) {
    if (!p.embedded) return true;
    p.images.clear();
    const std::string base = storePath(p);
    std::error_code ec;
    if (!fs::is_regular_file(u8path(base), ec)) {
        if (err) *err = "the packed atlas " + base + " is missing (it holds this project's images)";
        return false;
    }
    if (!sliceStore(p, base, {}, p.images, err)) return false;
    for (Variant& v : p.variants) {
        if (v.images.empty()) continue;
        std::vector<std::string> names;
        for (const auto& kv : v.images) names.push_back(kv.first);
        if (!sliceStore(p, storePath(p, &v), names, v.images, err)) {
            if (err) *err = "variant " + v.id + ": " + *err;
            return false;
        }
    }
    return true;
}

bool openProject(const std::string& path, Project& out, std::string* err) {
    Project p;
    if (!loadProject(path, p, err)) return false;
    if (!loadEmbeddedImages(p, err)) {
        if (err) *err = path + ": " + *err;
        return false;
    }
    out = std::move(p);
    return true;
}

// ---- save --------------------------------------------------------------------------------------

void rebaseProject(Project& p, const std::string& newPath) {
    // An output of "." means "next to the project": the packed atlas moves along with it.
    std::vector<std::string*> paths;
    if (p.output.dir != "." && !p.output.dir.empty()) paths.push_back(&p.output.dir);
    for (SourceFolder& f : p.sources) paths.push_back(&f.path);
    for (SourceFolder& f : p.references) paths.push_back(&f.path);
    for (SpriteDef& d : p.sprites) paths.push_back(&d.file);
    for (Variant& v : p.variants) {
        paths.push_back(&v.output.dir);
        paths.push_back(&v.overrideDir);
        paths.push_back(&v.reference);
    }
    std::vector<std::string> abs;
    for (std::string* s : paths) abs.push_back(s->empty() ? std::string() : p.resolve(*s));
    p.filePath = normalizePath(newPath);
    for (size_t i = 0; i < paths.size(); i++)
        if (!abs[i].empty()) *paths[i] = p.relativize(abs[i]);
}

bool saveProjectAll(const std::string& path, Project& p, const std::vector<std::string>& alsoFormats,
                    SaveResult* result, std::string* err) {
    Project q = p;   // nothing changes in p unless everything worked
    if (normalizePath(path) != q.filePath) rebaseProject(q, path);
    SaveResult res;
    if (q.embedded) {
        struct Target { std::string dir; std::vector<ExportFile> files; };
        std::vector<Target> targets;
        std::set<std::string> stores;
        std::vector<const Variant*> all{nullptr};
        for (const Variant& v : q.variants) all.push_back(&v);
        const ImageSource src = fileImageSource();
        for (const Variant* v : all) {
            const std::string label = v ? "variant " + v->id + ": " : std::string();
            if (!stores.insert(storePath(q, v)).second) {
                if (err) *err = label + "writes to the same packed atlas as another variant (" + storePath(q, v) + ")";
                return false;
            }
            BuildResult b = build(q, src, BuildOptions{v ? v->id : std::string(), true});
            for (Diagnostic d : b.diagnostics) {
                d.message = label + d.message;
                res.diagnostics.push_back(d);
            }
            // Every image must have a place in the packed atlas, or it would be lost.
            std::vector<std::string> names;
            if (v) for (const auto& kv : v->images) names.push_back(kv.first);
            else for (const auto& kv : q.images) names.push_back(kv.first);
            for (const std::string& n : names) {
                const Region* r = b.find(n);
                if (!r || r->child) {
                    std::string why;
                    for (const Diagnostic& d : b.diagnostics) if (d.sprite == n) why = ": " + d.message;
                    if (err) *err = label + "image '" + n + "' could not be packed, so it would be lost" + why;
                    return false;
                }
            }
            const Output& o = v ? v->output : q.output;
            std::vector<std::string> formats = o.formats;
            for (const std::string& f : alsoFormats) formats.push_back(f);
            formats.push_back("atlas");   // the store
            std::sort(formats.begin(), formats.end());
            formats.erase(std::unique(formats.begin(), formats.end()), formats.end());
            Target t;
            t.dir = q.resolve(o.dir);
            if (!exportFiles(b, formats, t.files, err)) return false;
            targets.push_back(std::move(t));
        }
        for (const Target& t : targets) {
            std::vector<std::string> changed;
            if (!writeFiles(t.dir, t.files, err, &changed)) return false;
            for (const std::string& f : changed) res.written.push_back(joinPath(t.dir, f));
        }
    }
    if (!saveProject(path, q, err)) return false;
    res.written.push_back(q.filePath);
    p = std::move(q);
    if (result) *result = std::move(res);
    return true;
}

// ---- folders -> embedded -----------------------------------------------------------------------

int embedSources(Project& p, const ImageSource& src, std::vector<Diagnostic>* diags) {
    if (p.embedded) return 0;
    auto diag = [&](Diagnostic::Level l, const std::string& s, const std::string& m) {
        if (diags) diags->push_back({l, s, m});
    };
    std::map<std::string, ImagePtr> images;
    for (const ResolvedSprite& r : resolveSprites(p, src, diags)) {
        if (r.def.isChild()) continue;
        Image img;
        std::string err;
        if (!src.load(r.file, img, &err)) { diag(Diagnostic::Error, r.def.name, err); continue; }
        images[r.def.name] = std::make_shared<const Image>(std::move(img));
    }
    // Explicit entries keep their settings; files are now inside, excluded sprites are gone.
    std::vector<SpriteDef> defs;
    for (SpriteDef d : p.sprites) {
        if (d.exclude && !d.isChild()) continue;
        d.file.clear();
        d.exclude = false;
        defs.push_back(d);
    }
    p.sprites.swap(defs);
    for (Variant& v : p.variants) {
        if (v.overrideDir.empty()) continue;
        const std::string dir = p.resolve(v.overrideDir);
        for (const auto& kv : images) {
            const std::string f = joinPath(dir, kv.first + ".png");
            if (!src.exists(f)) continue;
            Image img;
            std::string err;
            if (!src.load(f, img, &err)) { diag(Diagnostic::Error, kv.first, "variant " + v.id + ": " + err); continue; }
            v.images[kv.first] = std::make_shared<const Image>(std::move(img));
        }
        v.reference = v.overrideDir;
        v.overrideDir.clear();
    }
    p.references.insert(p.references.end(), p.sources.begin(), p.sources.end());
    p.sources.clear();
    p.images = std::move(images);
    p.embedded = true;
    return (int)p.images.size();
}

// ---- references --------------------------------------------------------------------------------

std::string imageNameFor(const std::string& file, const std::string& folder, const std::string& prefix) {
    std::string rel = normalizePath(file);
    const std::string dir = normalizePath(folder);
    if (!dir.empty() && rel.compare(0, dir.size(), dir) == 0) {
        rel = rel.substr(dir.size());
        while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
    } else {
        rel = fileName(rel);
    }
    if (rel.size() > 4) {
        std::string ext = rel.substr(rel.size() - 4);
        for (char& c : ext) c = (char)tolower((unsigned char)c);
        if (ext == ".png") rel.resize(rel.size() - 4);
    }
    return prefix + rel;
}

std::vector<ReferenceImage> scanReferences(const Project& p, const ImageSource& src, const std::string& variant) {
    const Variant* v = variant.empty() ? nullptr : p.findVariant(variant);
    std::vector<SourceFolder> folders;
    if (v) { if (!v->reference.empty()) folders.push_back({v->reference, true, ""}); }
    else folders = p.references;
    std::map<std::string, ReferenceImage> found;
    for (const SourceFolder& f : folders) {
        const std::string dir = p.resolve(f.path);
        for (const std::string& file : src.listPngs(dir, f.recursive)) {
            ReferenceImage ri;
            ri.name = imageNameFor(file, dir, f.prefix);
            ri.file = file;
            if (found.count(ri.name)) continue;   // the first folder wins, as in builds
            if (v && !p.images.count(ri.name)) continue;   // a variant only replaces existing sprites
            const std::map<std::string, ImagePtr>& have = v ? v->images : p.images;
            auto it = have.find(ri.name);
            if (it == have.end() || !it->second) ri.status = ReferenceImage::New;
            else {
                Image img;
                ri.status = src.load(file, img, nullptr) && sameVisible(img, *it->second) ? ReferenceImage::Same : ReferenceImage::Changed;
            }
            found[ri.name] = ri;
        }
    }
    std::vector<ReferenceImage> out;
    for (auto& kv : found) out.push_back(kv.second);
    return out;
}

bool setImage(Project& p, const std::string& name, const Image& img, const std::string& variant, std::string* err) {
    if (name.empty()) { if (err) *err = "an image needs a name"; return false; }
    if (!img.valid()) { if (err) *err = name + ": the image is empty"; return false; }
    if (const SpriteDef* d = p.findSprite(name))
        if (d->isChild()) { if (err) *err = name + " is a child sprite; it has no image of its own"; return false; }
    if (!variant.empty()) {
        Variant* v = nullptr;
        for (Variant& x : p.variants) if (x.id == variant) v = &x;
        if (!v) { if (err) *err = "unknown variant '" + variant + "'"; return false; }
        if (!p.images.count(name)) { if (err) *err = "variant art for '" + name + "', which the base art does not have"; return false; }
        v->images[name] = std::make_shared<const Image>(img);
        return true;
    }
    p.images[name] = std::make_shared<const Image>(img);
    return true;
}

// ---- back to files -----------------------------------------------------------------------------

bool extractImages(const Project& p, const std::string& dir, const std::string& variant, bool children,
                   std::vector<std::string>* written, std::string* err) {
    if (!variant.empty() && !p.findVariant(variant)) { if (err) *err = "unknown variant '" + variant + "'"; return false; }
    BuildResult b = build(p, BuildOptions{variant, true});
    std::error_code ec;
    for (const Region& r : b.regions) {
        if (r.child && !children) continue;
        const std::string file = joinPath(dir, r.name + ".png");
        fs::create_directories(u8path(file).parent_path(), ec);
        if (!savePng(file, renderRegion(b, r), err)) return false;
        if (written) written->push_back(file);
    }
    return true;
}

}  // namespace atlas
