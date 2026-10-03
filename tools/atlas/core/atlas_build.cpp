// atlas_build.cpp -- see atlas_build.h.
//
// Steps: resolve sprites (folders + explicit entries) -> load images (variant overrides) ->
// resolve child rects down to their root image -> trim (never cutting into a child) -> dedupe ->
// pack -> composite pages -> regions.
#include "atlas_build.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>

namespace fs = std::filesystem;

namespace atlas {

// ---- result helpers ----------------------------------------------------------------------------

int BuildResult::errorCount() const {
    int n = 0;
    for (const Diagnostic& d : diagnostics) n += d.level == Diagnostic::Error;
    return n;
}
int BuildResult::warningCount() const {
    int n = 0;
    for (const Diagnostic& d : diagnostics) n += d.level == Diagnostic::Warning;
    return n;
}
bool BuildResult::ok() const { return errorCount() == 0; }

const Region* BuildResult::find(const std::string& n) const {
    auto it = std::lower_bound(regions.begin(), regions.end(), n,
                               [](const Region& r, const std::string& k) { return r.name < k; });
    return it != regions.end() && it->name == n ? &*it : nullptr;
}

std::string formatDiagnostic(const Diagnostic& d) {
    const char* lv = d.level == Diagnostic::Error ? "error" : d.level == Diagnostic::Warning ? "warning" : "info";
    return std::string(lv) + ": " + (d.sprite.empty() ? "" : d.sprite + ": ") + d.message;
}

// ---- image sources -----------------------------------------------------------------------------

static std::vector<std::string> listPngsFs(const std::string& dir, bool recursive) {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path root = u8path(dir);
    if (!fs::is_directory(root, ec)) return out;
    auto take = [&](const fs::directory_entry& e) {
        if (!e.is_regular_file(ec)) return;
        std::string ext = u8str(e.path().extension());
        for (char& c : ext) c = (char)tolower((unsigned char)c);
        if (ext == ".png") out.push_back(normalizePath(dir) + "/" + u8str(e.path().lexically_relative(root)));
    };
    if (recursive) {
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) take(*it);
    } else {
        for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) take(*it);
    }
    std::sort(out.begin(), out.end());
    return out;
}

ImageSource fileImageSource() {
    ImageSource s;
    s.load = [](const std::string& p, Image& out, std::string* err) { return loadImage(p, out, err); };
    s.exists = [](const std::string& p) { std::error_code ec; return fs::is_regular_file(u8path(p), ec); };
    s.listPngs = listPngsFs;
    return s;
}

namespace {
struct CacheEntry { uintmax_t size = 0; fs::file_time_type time; Image img; };
struct ImageCache { std::unordered_map<std::string, CacheEntry> map; };
}  // namespace

ImageSource cachedImageSource(std::shared_ptr<void>& holder) {
    if (!holder) holder = std::make_shared<ImageCache>();
    auto cache = std::static_pointer_cast<ImageCache>(holder);
    ImageSource s = fileImageSource();
    s.load = [cache](const std::string& p, Image& out, std::string* err) {
        std::error_code ec;
        const uintmax_t size = fs::file_size(u8path(p), ec);
        const fs::file_time_type t = fs::last_write_time(u8path(p), ec);
        auto it = cache->map.find(p);
        if (!ec && it != cache->map.end() && it->second.size == size && it->second.time == t) {
            out = it->second.img;
            return true;
        }
        if (!loadImage(p, out, err)) return false;
        if (!ec) cache->map[p] = {size, t, out};
        return true;
    };
    return s;
}

// ---- resolve -----------------------------------------------------------------------------------

std::vector<ResolvedSprite> resolveSprites(const Project& p, const ImageSource& src, std::vector<Diagnostic>* diags) {
    auto diag = [&](Diagnostic::Level l, const std::string& s, const std::string& m) {
        if (diags) diags->push_back({l, s, m});
    };
    std::map<std::string, ResolvedSprite> byName;
    if (p.embedded) {
        for (const auto& kv : p.images) {
            ResolvedSprite r;
            r.def.name = kv.first;
            r.image = kv.second;
            r.scanned = true;
            byName[kv.first] = r;
        }
        if (!p.sources.empty())
            diag(Diagnostic::Warning, "", "an embedded project does not read source folders; import from them instead");
    }
    for (const SourceFolder& f : p.embedded ? std::vector<SourceFolder>() : p.sources) {
        const std::string dir = p.resolve(f.path);
        const std::vector<std::string> files = src.listPngs(dir, f.recursive);
        if (files.empty()) diag(Diagnostic::Warning, "", "source folder '" + f.path + "' has no PNG files");
        for (const std::string& file : files) {
            std::string rel = file.substr(std::min(file.size(), dir.size()));
            while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
            if (rel.size() > 4) rel.resize(rel.size() - 4);   // ".png"
            const std::string name = f.prefix + rel;
            if (byName.count(name)) { diag(Diagnostic::Warning, name, "found in two source folders; the first one is used"); continue; }
            ResolvedSprite r;
            r.def.name = name;
            r.file = file;
            r.scanned = true;
            byName[name] = r;
        }
    }
    for (const SpriteDef& d : p.sprites) {
        if (d.name.empty()) { diag(Diagnostic::Error, "", "a sprite entry has no name"); continue; }
        auto it = byName.find(d.name);
        if (p.embedded && !d.isChild() && !d.file.empty()) {
            diag(Diagnostic::Warning, d.name, "an embedded project does not read image files; import the image instead");
            continue;
        }
        if (p.embedded && d.exclude) diag(Diagnostic::Warning, d.name, "'exclude' does nothing in an embedded project; delete the sprite instead");
        if (d.isChild() || !d.file.empty()) {
            if (it != byName.end() && !it->second.scanned) { diag(Diagnostic::Error, d.name, "two sprite entries have this name"); continue; }
            if (it != byName.end() && d.isChild()) { diag(Diagnostic::Error, d.name, "a child sprite has the same name as an image"); continue; }
            ResolvedSprite r;
            r.def = d;
            r.file = d.isChild() ? std::string() : p.resolve(d.file);
            byName[d.name] = r;
        } else if (it != byName.end()) {
            ResolvedSprite& r = it->second;   // settings for a scanned sprite
            const std::string keepName = r.def.name;
            r.def = d;
            r.def.name = keepName;
        } else {
            diag(Diagnostic::Warning, d.name, "sprite entry has no file, no parent and matches no scanned image");
        }
    }
    std::vector<ResolvedSprite> out;
    for (auto& kv : byName)
        if (!kv.second.def.exclude || p.embedded) out.push_back(kv.second);
    return out;
}

// ---- build -------------------------------------------------------------------------------------

namespace {

struct Entry {                  // one sprite during the build
    ResolvedSprite rs;
    bool child = false;
    int root = -1;              // index of the root image entry (children)
    IRect abs;                  // children: rect in the root's original pixels (variant scaled)
    IRect local;                // children: rect relative to the direct parent (variant scaled)
    Image img;                  // packed entries: the original image (baked child: its crop)
    IRect trim;                 // packed entries: kept rect inside img
    bool packed = false;        // has its own place in the atlas
    int alias = -1;             // dedupe target entry
    int item = -1;              // pack item index
    int page = -1, fx = 0, fy = 0;   // frame position
};

int roundi(double v) { return (int)std::floor(v + 0.5); }

}  // namespace

BuildResult build(const Project& p, const ImageSource& src, const BuildOptions& opt) {
    BuildResult res;
    res.settings = p.settings;
    const Settings& S = p.settings;
    auto diag = [&](Diagnostic::Level l, const std::string& s, const std::string& m) { res.diagnostics.push_back({l, s, m}); };

    const Variant* variant = nullptr;
    if (!opt.variant.empty()) {
        variant = p.findVariant(opt.variant);
        if (!variant) { diag(Diagnostic::Error, "", "unknown variant '" + opt.variant + "'"); return res; }
    }
    res.name = p.outputName(variant ? variant->output : p.output);
    res.animations = p.animations;

    std::vector<ResolvedSprite> resolved = resolveSprites(p, src, &res.diagnostics);
    std::vector<Entry> E(resolved.size());
    std::unordered_map<std::string, int> index;
    for (size_t i = 0; i < resolved.size(); i++) {
        E[i].rs = resolved[i];
        E[i].child = resolved[i].def.isChild();
        index[resolved[i].def.name] = (int)i;
    }

    // Images (and the variant scale per root: variant size / base size).
    std::vector<double> sx(E.size(), 1.0), sy(E.size(), 1.0);
    for (size_t i = 0; i < E.size(); i++) {
        Entry& e = E[i];
        if (e.child) continue;
        std::string err;
        Image base;
        if (p.embedded) {
            if (!e.rs.image || !e.rs.image->valid()) { diag(Diagnostic::Error, e.rs.def.name, "has no pixels (the packed atlas did not load)"); continue; }
            base = *e.rs.image;
        } else if (!src.load(e.rs.file, base, &err)) {
            diag(Diagnostic::Error, e.rs.def.name, err);
            continue;
        }
        e.img = std::move(base);
        if (variant) {
            auto vi = variant->images.find(e.rs.def.name);
            const std::string ov = variant->overrideDir.empty() ? std::string()
                                 : joinPath(p.resolve(variant->overrideDir), e.rs.def.name + ".png");
            if (vi != variant->images.end()) {
                if (!vi->second || !vi->second->valid()) { diag(Diagnostic::Error, e.rs.def.name, "variant art has no pixels"); continue; }
                sx[i] = (double)vi->second->w / e.img.w;
                sy[i] = (double)vi->second->h / e.img.h;
                e.img = *vi->second;
                e.rs.file.clear();
            } else if (!ov.empty() && src.exists(ov)) {
                Image vimg;
                if (!src.load(ov, vimg, &err)) { diag(Diagnostic::Error, e.rs.def.name, err); continue; }
                sx[i] = (double)vimg.w / e.img.w;
                sy[i] = (double)vimg.h / e.img.h;
                e.img = std::move(vimg);
                e.rs.file = ov;
            }
        }
        e.packed = true;
    }

    // Children: walk up to the root image, adding up the rects.
    for (size_t i = 0; i < E.size(); i++) {
        if (!E[i].child) continue;
        const std::string& name = E[i].rs.def.name;
        IRect abs = E[i].rs.def.rect;
        std::set<int> seen{(int)i};
        int cur = (int)i, root = -1;
        bool bad = false;
        if (abs.w <= 0 || abs.h <= 0) { diag(Diagnostic::Error, name, "child rect must have a positive size"); continue; }
        while (true) {
            const std::string& par = E[cur].rs.def.parent;
            auto it = index.find(par);
            if (it == index.end()) { diag(Diagnostic::Error, name, "parent '" + par + "' does not exist"); bad = true; break; }
            const int pi = it->second;
            if (!seen.insert(pi).second) { diag(Diagnostic::Error, name, "parent chain loops back on itself"); bad = true; break; }
            // The rect of `cur` must lie inside its parent.
            const IRect& curRect = E[cur].rs.def.rect;
            int pw, ph;
            if (E[pi].child) { pw = E[pi].rs.def.rect.w; ph = E[pi].rs.def.rect.h; }
            else if (E[pi].img.valid()) { pw = (int)std::lround(E[pi].img.w / sx[pi]); ph = (int)std::lround(E[pi].img.h / sy[pi]); }
            else { bad = true; break; }   // root failed to load: already reported
            if (curRect.x < 0 || curRect.y < 0 || curRect.right() > pw || curRect.bottom() > ph) {
                diag(Diagnostic::Error, E[cur].rs.def.name, "rect " + std::to_string(curRect.x) + "," + std::to_string(curRect.y) + "," +
                     std::to_string(curRect.w) + "," + std::to_string(curRect.h) + " lies outside parent '" + par + "' (" +
                     std::to_string(pw) + "x" + std::to_string(ph) + ")");
                bad = true;
                break;
            }
            if (!E[pi].child) { root = pi; break; }
            abs.x += E[pi].rs.def.rect.x;
            abs.y += E[pi].rs.def.rect.y;
            cur = pi;
        }
        if (bad || root < 0) continue;
        Entry& e = E[i];
        e.root = root;
        // Variant art may be another size: scale the rect with the root image (edges rounded, so
        // neighbouring children still meet exactly).
        auto scaleRect = [&](const IRect& r) {
            const int x0 = roundi(r.x * sx[root]), y0 = roundi(r.y * sy[root]);
            return IRect{x0, y0, roundi(r.right() * sx[root]) - x0, roundi(r.bottom() * sy[root]) - y0};
        };
        e.abs = scaleRect(abs);
        const IRect parentAbs = scaleRect({abs.x - e.rs.def.rect.x, abs.y - e.rs.def.rect.y, 0, 0});
        e.local = {e.abs.x - parentAbs.x, e.abs.y - parentAbs.y, e.abs.w, e.abs.h};
        if (e.rs.def.bake) {
            e.img = crop(E[root].img, e.abs);
            e.trim = {0, 0, e.img.w, e.img.h};
            e.packed = true;
        }
    }
    // A child whose own parent failed is not resolved: report once so nothing goes missing silently.
    for (Entry& e : E)
        if (e.child && e.root < 0 && !e.rs.def.parent.empty()) {
            bool reported = false;
            for (const Diagnostic& d : res.diagnostics) if (d.sprite == e.rs.def.name) reported = true;
            if (!reported) diag(Diagnostic::Error, e.rs.def.name, "could not resolve its parent chain");
        }

    // Trim. A root never loses pixels that one of its (non-baked) children uses.
    std::vector<IRect> keep(E.size());
    for (size_t i = 0; i < E.size(); i++)
        if (E[i].child && E[i].root >= 0 && !E[i].rs.def.bake) keep[E[i].root] = unite(keep[E[i].root], E[i].abs);
    for (size_t i = 0; i < E.size(); i++) {
        Entry& e = E[i];
        if (!e.packed || e.child) continue;
        if (!e.img.valid()) { e.packed = false; continue; }
        const bool trimOn = e.rs.def.trim < 0 ? S.trim : e.rs.def.trim == 1;
        IRect r = trimOn ? opaqueBounds(e.img, S.alphaThreshold) : IRect{0, 0, e.img.w, e.img.h};
        r = unite(r, keep[i]);
        if (r.empty()) { r = {0, 0, 1, 1}; diag(Diagnostic::Info, e.rs.def.name, "image is fully transparent (kept as 1x1)"); }
        e.trim = r;
    }

    // Dedupe: identical kept pixels share one place. Pinned sprites always keep their own.
    if (S.dedupe) {
        std::unordered_map<uint64_t, std::vector<int>> byHash;
        for (size_t i = 0; i < E.size(); i++) {
            Entry& e = E[i];
            if (!e.packed || e.rs.def.pinned) continue;
            const uint64_t h = hashPixels(e.img, e.trim);
            int target = -1;
            for (int j : byHash[h])
                if (samePixels(E[j].img, E[j].trim, e.img, e.trim)) { target = j; break; }
            if (target >= 0) { e.alias = target; e.packed = false; }
            else byHash[h].push_back((int)i);
        }
    }

    // Pack.
    std::vector<PackItem> items;
    std::vector<int> itemEntry;
    const int ex = std::max(0, S.extrude);
    for (size_t i = 0; i < E.size(); i++) {
        Entry& e = E[i];
        if (!e.packed) continue;
        PackItem it;
        it.w = e.trim.w + 2 * ex;
        it.h = e.trim.h + 2 * ex;
        if (e.rs.def.pinned) {
            it.fixed = true;
            it.fixedPage = e.rs.def.pinPage;
            it.fixedX = e.rs.def.pinX - ex;
            it.fixedY = e.rs.def.pinY - ex;
        }
        e.item = (int)items.size();
        items.push_back(it);
        itemEntry.push_back((int)i);
    }
    PackSettings ps;
    ps.maxWidth = S.maxWidth; ps.maxHeight = S.maxHeight;
    ps.minWidth = std::min(S.minWidth, S.maxWidth); ps.minHeight = std::min(S.minHeight, S.maxHeight);
    ps.powerOfTwo = S.powerOfTwo; ps.square = S.square; ps.fixedSize = S.fixedSize;
    ps.border = S.border; ps.padding = S.padding; ps.heuristic = S.heuristic;
    PackResult pr = packPages(items, ps);
    for (const std::string& m : pr.errors) diag(Diagnostic::Error, "", m);
    for (size_t k = 0; k < items.size(); k++) {
        Entry& e = E[itemEntry[k]];
        const PackPlacement& pl = pr.placements[k];
        if (pl.page < 0) {
            diag(Diagnostic::Error, e.rs.def.name, "does not fit in a " + std::to_string(S.maxWidth) + "x" +
                 std::to_string(S.maxHeight) + " page (" + std::to_string(e.trim.w) + "x" + std::to_string(e.trim.h) + ")");
            continue;
        }
        e.page = pl.page;
        e.fx = pl.x + ex;
        e.fy = pl.y + ex;
    }

    // Pages.
    for (size_t pg = 0; pg < pr.pageSizes.size(); pg++) {
        Page page;
        page.file = res.name + (pg == 0 ? std::string() : "_" + std::to_string(pg)) + ".png";
        if (opt.composite) page.image = Image(pr.pageSizes[pg].w, pr.pageSizes[pg].h);
        else { page.image.w = pr.pageSizes[pg].w; page.image.h = pr.pageSizes[pg].h; }
        res.pages.push_back(std::move(page));
    }
    if (opt.composite) {
        for (Entry& e : E) {
            if (!e.packed || e.page < 0) continue;
            Image& dst = res.pages[e.page].image;
            blit(dst, e.img, e.trim, e.fx, e.fy);
            extrude(dst, {e.fx, e.fy, e.trim.w, e.trim.h}, ex);
        }
        if (S.premultiplyAlpha)
            for (Page& pg : res.pages) premultiply(pg.image);
    }

    // Regions.
    for (size_t i = 0; i < E.size(); i++) {
        const Entry& e = E[i];
        const SpriteDef& d = e.rs.def;
        Region r;
        r.name = d.name;
        r.tags = d.tags;
        r.pivot[0] = d.hasPivot ? d.pivot[0] : S.defaultPivot[0];
        r.pivot[1] = d.hasPivot ? d.pivot[1] : S.defaultPivot[1];
        r.hasSplit = d.hasSplit;
        for (int k = 0; k < 4; k++) r.split[k] = d.split[k];
        if (e.child) {
            if (e.root < 0) continue;
            r.child = true;
            r.parent = d.parent;
            r.local = e.local;
            r.origW = e.abs.w; r.origH = e.abs.h;
            if (d.bake) {
                if (e.page < 0) continue;
                r.baked = true;
                r.page = e.page;
                r.frame = {e.fx, e.fy, e.trim.w, e.trim.h};
            } else {
                const Entry& root = E[e.root];
                const Entry& placed = root.alias >= 0 ? E[root.alias] : root;
                if (placed.page < 0) continue;
                r.page = placed.page;
                r.frame = {placed.fx + e.abs.x - root.trim.x, placed.fy + e.abs.y - root.trim.y, e.abs.w, e.abs.h};
            }
        } else {
            if (!e.img.valid()) continue;
            const Entry& placed = e.alias >= 0 ? E[e.alias] : e;
            if (placed.page < 0) continue;
            r.page = placed.page;
            r.frame = {placed.fx, placed.fy, e.trim.w, e.trim.h};
            r.offsetX = e.trim.x; r.offsetY = e.trim.y;
            r.origW = e.img.w; r.origH = e.img.h;
            r.sourceFile = e.rs.file;
            if (e.alias >= 0) r.aliasOf = E[e.alias].rs.def.name;
        }
        if (r.hasSplit && (r.split[0] + r.split[1] > r.origW || r.split[2] + r.split[3] > r.origH || r.split[0] < 0 ||
                           r.split[1] < 0 || r.split[2] < 0 || r.split[3] < 0))
            diag(Diagnostic::Warning, r.name, "9-slice borders are larger than the sprite");
        if (r.name == res.name)
            diag(Diagnostic::Warning, r.name, "has the same name as the atlas (the FM79979 .pi loader renames it)");
        res.regions.push_back(std::move(r));
    }
    std::sort(res.regions.begin(), res.regions.end(), [](const Region& a, const Region& b) { return a.name < b.name; });

    for (const Animation& a : res.animations) {
        if (a.name.empty()) diag(Diagnostic::Error, "", "an animation has no name");
        for (const AnimFrame& f : a.frames)
            if (!res.find(f.sprite)) diag(Diagnostic::Error, a.name, "animation frame '" + f.sprite + "' is not a sprite");
    }
    if (res.regions.empty() && res.errorCount() == 0) diag(Diagnostic::Warning, "", "the atlas has no sprites");
    return res;
}

}  // namespace atlas
