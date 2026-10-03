// atlas_core_test -- tests for tools/atlas/core (CTest: atlas.core).
//
//   packer      no overlaps, inside the page, padding kept, pinned rects exact, deterministic
//   build       trim, children (nested, trimmed parents), dedupe, bake, variants, errors
//   formats     every exporter read back (the .atlas through the game's own atlas_file.h) and the
//               pixels each sprite draws compared with its source image
//   golden      tests/data/*.pi (the old PI editor's output) -> project -> rebuilt .pi draws the
//               same pixels. ATLAS_PI_CORPUS=<folder> runs this over every .pi below that folder.
#include "atlas_build.h"
#include "atlas_export.h"
#include "atlas_store.h"
#include "atlas_xml.h"

#include <atlas_file.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>

using namespace atlas;
namespace fs = std::filesystem;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; std::fprintf(stderr, "  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_MSG(cond, msg) do { g_checks++; if (!(cond)) { g_failed++; std::fprintf(stderr, "  FAILED %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, std::string(msg).c_str()); } } while (0)

static void run(const char* name, const std::function<void()>& f) {
    const int before = g_failed;
    f();
    std::printf("%s %s\n", g_failed == before ? "[ ok ]" : "[FAIL]", name);
}

static std::string tempDir(const std::string& name) {
    const fs::path d = fs::temp_directory_path() / ("atlas_test_" + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return u8str(d);
}

// A w x h image: transparent margin `m`, a colour pattern derived from `seed` inside.
static Image makeImage(int w, int h, int m, int seed) {
    Image img(w, h);
    for (int y = m; y < h - m; y++)
        for (int x = m; x < w - m; x++) {
            uint8_t* p = img.at(x, y);
            p[0] = (uint8_t)(seed * 37 + x * 5);
            p[1] = (uint8_t)(seed * 91 + y * 7);
            p[2] = (uint8_t)(seed * 13 + x * y);
            p[3] = 255;
        }
    return img;
}

static bool sameImage(const Image& a, const Image& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }

// What a game draws: fully transparent pixels count as equal whatever their RGB (old atlases keep
// colour under alpha 0; trimming drops it).
static bool sameVisible(const Image& a, const Image& b) {
    if (a.w != b.w || a.h != b.h) return false;
    for (size_t i = 0; i < a.px.size(); i += 4) {
        if (a.px[i + 3] == 0 && b.px[i + 3] == 0) continue;
        if (memcmp(&a.px[i], &b.px[i], 4) != 0) return false;
    }
    return true;
}

static std::string readAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---- packer ------------------------------------------------------------------------------------

static void checkPacking(const std::vector<PackItem>& items, const PackSettings& s, const PackResult& r) {
    CHECK(r.placements.size() == items.size());
    for (size_t i = 0; i < items.size(); i++) {
        const PackPlacement& a = r.placements[i];
        CHECK_MSG(a.page >= 0, "item " + std::to_string(i) + " not placed");
        if (a.page < 0) continue;
        const IRect& pg = r.pageSizes[a.page];
        CHECK(a.x >= s.border && a.y >= s.border);
        CHECK(a.x + items[i].w <= pg.w - s.border && a.y + items[i].h <= pg.h - s.border);
        if (s.powerOfTwo) { CHECK((pg.w & (pg.w - 1)) == 0); CHECK((pg.h & (pg.h - 1)) == 0); }
        CHECK(pg.w <= s.maxWidth && pg.h <= s.maxHeight);
        for (size_t j = i + 1; j < items.size(); j++) {
            const PackPlacement& b = r.placements[j];
            if (b.page != a.page) continue;
            // Inflate both by padding on the right/bottom: they must still not overlap.
            const IRect ra{a.x, a.y, items[i].w + s.padding, items[i].h + s.padding};
            const IRect rb{b.x, b.y, items[j].w + s.padding, items[j].h + s.padding};
            CHECK_MSG(!ra.intersects(rb), "items " + std::to_string(i) + " and " + std::to_string(j) + " overlap");
        }
    }
}

static void testPacker() {
    std::mt19937 rng(1234);
    for (Heuristic h : {Heuristic::Auto, Heuristic::BestShortSideFit, Heuristic::BestAreaFit, Heuristic::BottomLeft,
                        Heuristic::ContactPoint, Heuristic::Shelf}) {
        std::vector<PackItem> items;
        for (int i = 0; i < 120; i++) items.push_back({(int)(rng() % 60) + 1, (int)(rng() % 60) + 1});
        PackSettings s;
        s.maxWidth = 256; s.maxHeight = 256; s.padding = 2; s.border = 1; s.heuristic = h; s.powerOfTwo = true;
        const PackResult r = packPages(items, s);
        CHECK(r.errors.empty());
        CHECK(r.pageSizes.size() >= 2);   // 120 rects of ~30x30 average do not fit in one 256 page
        checkPacking(items, s, r);
        const PackResult again = packPages(items, s);
        bool same = again.pageSizes.size() == r.pageSizes.size();
        for (size_t i = 0; same && i < items.size(); i++)
            same = again.placements[i].page == r.placements[i].page && again.placements[i].x == r.placements[i].x &&
                   again.placements[i].y == r.placements[i].y;
        CHECK_MSG(same, std::string("not deterministic: ") + heuristicName(h));
    }
    // Shrinks to a small page; pinned item lands exactly; too-big item is an error.
    std::vector<PackItem> items = {{10, 10}, {20, 5}, {5, 20}};
    PackItem pin{8, 8, true, 0, 40, 40};
    items.push_back(pin);
    PackSettings s;
    s.maxWidth = 512; s.maxHeight = 512; s.minWidth = 1; s.minHeight = 1; s.padding = 1;
    PackResult r = packPages(items, s);
    checkPacking(items, s, r);
    CHECK(r.pageSizes.size() == 1);
    CHECK(r.placements[3].x == 40 && r.placements[3].y == 40);
    // Shrunk to the used area: at least the pinned rect, at most the 64px bin it was packed in.
    CHECK(r.pageSizes[0].w >= 48 && r.pageSizes[0].w <= 64 && r.pageSizes[0].h >= 48 && r.pageSizes[0].h <= 64);
    items.push_back({600, 10});
    r = packPages(items, s);
    CHECK(!r.errors.empty());
    CHECK(r.placements[4].page == -1);
}

// ---- build -------------------------------------------------------------------------------------

struct Fixture {
    std::string dir;
    Project p;
};

// sprites/: floor 32x32 (opaque), coin 32x32 (4px margin), coin_copy (same pixels as coin),
//           frame 64x48 (opaque) with children; style/: floor at 64x64.
static Fixture makeFixture(const std::string& name) {
    Fixture f;
    f.dir = tempDir(name);
    fs::create_directories(f.dir + "/sprites/ui");
    fs::create_directories(f.dir + "/style/ui");
    savePng(f.dir + "/sprites/floor.png", makeImage(32, 32, 0, 1));
    savePng(f.dir + "/sprites/coin.png", makeImage(32, 32, 4, 2));
    savePng(f.dir + "/sprites/coin_copy.png", makeImage(32, 32, 4, 2));
    savePng(f.dir + "/sprites/ui/frame.png", makeImage(64, 48, 0, 3));
    savePng(f.dir + "/sprites/ui/panel.png", makeImage(40, 40, 10, 4));   // trimmed parent
    savePng(f.dir + "/style/floor.png", makeImage(64, 64, 0, 5));
    savePng(f.dir + "/style/ui/frame.png", makeImage(128, 96, 0, 6));

    Project& p = f.p;
    p.filePath = f.dir + "/game.atlasproj";
    p.name = "game";
    p.settings.padding = 2;
    p.settings.extrude = 1;
    p.settings.minWidth = p.settings.minHeight = 1;
    p.sources.push_back({"sprites", true, ""});
    p.output.dir = "out";
    p.output.formats = {"atlas", "atlas-spine3", "plist", "tp-json", "pi", "rcss"};

    SpriteDef floor; floor.name = "floor"; floor.tags = {"map"}; floor.hasPivot = true; floor.pivot[0] = 0.25f; floor.pivot[1] = 1.0f;
    p.sprites.push_back(floor);
    SpriteDef frame; frame.name = "ui/frame"; frame.hasSplit = true; frame.split[0] = 8; frame.split[1] = 8; frame.split[2] = 6; frame.split[3] = 6;
    p.sprites.push_back(frame);
    SpriteDef tl; tl.name = "ui/frame_tl"; tl.parent = "ui/frame"; tl.rect = {0, 0, 16, 12}; tl.tags = {"ui"};
    p.sprites.push_back(tl);
    SpriteDef mid; mid.name = "ui/frame_mid"; mid.parent = "ui/frame"; mid.rect = {16, 12, 32, 24};
    p.sprites.push_back(mid);
    SpriteDef dot; dot.name = "ui/frame_dot"; dot.parent = "ui/frame_mid"; dot.rect = {4, 4, 8, 8};   // nested child
    p.sprites.push_back(dot);
    SpriteDef corner; corner.name = "ui/panel_corner"; corner.parent = "ui/panel"; corner.rect = {0, 0, 6, 6};   // inside the transparent margin
    p.sprites.push_back(corner);
    SpriteDef baked; baked.name = "ui/frame_baked"; baked.parent = "ui/frame"; baked.rect = {48, 36, 16, 12}; baked.bake = true;
    p.sprites.push_back(baked);
    Animation a; a.name = "spin"; a.frames = {{"coin", 0.1f}, {"coin_copy", 0.2f}};
    p.animations.push_back(a);
    Variant v; v.id = "hd"; v.overrideDir = "style"; v.output = p.output; v.output.dir = "out_hd";
    p.variants.push_back(v);
    return f;
}

// What the sprite should look like: its source image, or for a child the rect of its root.
static Image expected(const Fixture& f, const std::string& name, const std::string& styleDir = "") {
    auto load = [&](const std::string& n) {
        Image img;
        if (!styleDir.empty() && loadImage(f.dir + "/" + styleDir + "/" + n + ".png", img)) return img;
        loadImage(f.dir + "/sprites/" + n + ".png", img);
        return img;
    };
    const SpriteDef* d = f.p.findSprite(name);
    if (!d || !d->isChild()) return load(name);
    IRect abs = d->rect;
    const SpriteDef* cur = d;
    while (true) {
        const SpriteDef* par = f.p.findSprite(cur->parent);
        if (par && par->isChild()) { abs.x += par->rect.x; abs.y += par->rect.y; cur = par; continue; }
        Image root = load(cur->parent);
        Image base;
        loadImage(f.dir + "/sprites/" + cur->parent + ".png", base);
        const double sx = (double)root.w / base.w, sy = (double)root.h / base.h;
        const int x0 = (int)std::floor(abs.x * sx + 0.5), y0 = (int)std::floor(abs.y * sy + 0.5);
        const IRect r{x0, y0, (int)std::floor(abs.right() * sx + 0.5) - x0, (int)std::floor(abs.bottom() * sy + 0.5) - y0};
        return crop(root, r);
    }
}

static void testBuild() {
    Fixture f = makeFixture("build");
    BuildResult b = build(f.p);
    for (const Diagnostic& d : b.diagnostics) std::printf("    %s\n", formatDiagnostic(d).c_str());
    CHECK(b.ok());
    CHECK(b.pages.size() == 1);
    // floor, coin, coin_copy, ui/frame, ui/panel + 5 children
    CHECK(b.regions.size() == 10);
    const Region* coin = b.find("coin");
    const Region* copy = b.find("coin_copy");
    CHECK(coin && copy);
    if (coin && copy) {
        CHECK(coin->frame == copy->frame);                 // dedupe: one place
        CHECK(copy->aliasOf == "coin");
        CHECK(coin->frame.w == 24 && coin->offsetX == 4);  // trimmed
        CHECK(coin->origW == 32);
    }
    const Region* frame = b.find("ui/frame");
    const Region* tl = b.find("ui/frame_tl");
    const Region* dot = b.find("ui/frame_dot");
    CHECK(frame && tl && dot);
    if (frame && tl && dot) {
        CHECK(tl->child && tl->parent == "ui/frame");
        CHECK(tl->frame.x == frame->frame.x && tl->frame.y == frame->frame.y && tl->frame.w == 16);
        CHECK(dot->frame.x == frame->frame.x + 20 && dot->frame.y == frame->frame.y + 16);
        CHECK(dot->local == (IRect{4, 4, 8, 8}));
        CHECK(frame->hasSplit && frame->split[2] == 6);
    }
    // The panel is trimmed, but never past its child in the transparent corner.
    const Region* panel = b.find("ui/panel");
    CHECK(panel && panel->offsetX == 0 && panel->offsetY == 0 && panel->frame.w == 30);
    const Region* baked = b.find("ui/frame_baked");
    CHECK(baked && baked->child && frame && !frame->frame.intersects(baked->frame));
    // Every sprite draws exactly its source pixels.
    for (const Region& r : b.regions)
        CHECK_MSG(sameImage(renderRegion(b, r), expected(f, r.name)), r.name);
    const Region* fl = b.find("floor");
    CHECK(fl && fl->pivot[0] == 0.25f && fl->tags.size() == 1);

    // Extrude: the pixel left of the floor's frame repeats its first column.
    if (fl) {
        const Image& pg = b.pages[0].image;
        CHECK(memcmp(pg.at(fl->frame.x - 1, fl->frame.y), pg.at(fl->frame.x, fl->frame.y), 4) == 0);
    }

    // Variant: other sizes, same names; children scale with their root.
    BuildResult hd = build(f.p, BuildOptions{"hd", true});
    CHECK(hd.ok());
    CHECK(hd.regions.size() == b.regions.size());
    const Region* hfl = hd.find("floor");
    CHECK(hfl && hfl->origW == 64);
    const Region* htl = hd.find("ui/frame_tl");
    CHECK(htl && htl->frame.w == 32 && htl->frame.h == 24 && htl->local == (IRect{0, 0, 32, 24}));
    for (const Region& r : hd.regions)
        CHECK_MSG(sameImage(renderRegion(hd, r), expected(f, r.name, "style")), "hd " + r.name);

    // Deterministic.
    BuildResult again = build(f.p);
    CHECK(again.pages.size() == b.pages.size() && again.pages[0].image.px == b.pages[0].image.px);

    // Errors.
    Project bad = f.p;
    SpriteDef out; out.name = "outside"; out.parent = "floor"; out.rect = {30, 30, 8, 8};
    SpriteDef orphan; orphan.name = "orphan"; orphan.parent = "nope"; orphan.rect = {0, 0, 1, 1};
    SpriteDef loopA; loopA.name = "loopA"; loopA.parent = "loopB"; loopA.rect = {0, 0, 1, 1};
    SpriteDef loopB; loopB.name = "loopB"; loopB.parent = "loopA"; loopB.rect = {0, 0, 1, 1};
    bad.sprites.insert(bad.sprites.end(), {out, orphan, loopA, loopB});
    BuildResult e = build(bad);
    CHECK(!e.ok());
    int found = 0;
    for (const Diagnostic& d : e.diagnostics)
        if (d.level == Diagnostic::Error && (d.sprite == "outside" || d.sprite == "orphan" || d.sprite == "loopA" || d.sprite == "loopB")) found++;
    CHECK(found >= 4);

    // Project JSON round trip.
    Project back;
    std::string err;
    CHECK(projectFromJson(projectToJson(f.p), back, &err));
    CHECK(projectToJson(back) == projectToJson(f.p));

    // Pinned sprite stays where it was put.
    Project pinned = f.p;
    SpriteDef* pf = pinned.findSprite("floor");
    pf->pinned = true; pf->pinX = 100; pf->pinY = 3;
    BuildResult pb = build(pinned);
    CHECK(pb.ok());
    const Region* pr = pb.find("floor");
    CHECK(pr && pr->frame.x == 100 && pr->frame.y == 3);
}

// ---- formats -----------------------------------------------------------------------------------

static void sameRegions(const BuildResult& a, const BuildResult& b, const std::string& what, bool children = true) {
    CHECK_MSG(a.regions.size() == b.regions.size(), what);
    for (const Region& r : a.regions) {
        const Region* o = b.find(r.name);
        CHECK_MSG(o, what + ": missing " + r.name);
        if (!o) continue;
        CHECK_MSG(o->frame == r.frame, what + ": frame " + r.name);
        CHECK_MSG(o->offsetX == r.offsetX && o->offsetY == r.offsetY, what + ": offset " + r.name);
        CHECK_MSG(o->origW == r.origW && o->origH == r.origH, what + ": orig " + r.name);
        if (children && !(r.baked && what == "pi")) {
            CHECK_MSG(o->child == r.child && o->parent == r.parent, what + ": parent " + r.name);
            if (r.child) CHECK_MSG(o->local.x == r.local.x && o->local.y == r.local.y, what + ": local " + r.name);
        }
        CHECK_MSG(sameImage(renderRegion(a, r), renderRegion(b, *o)), what + ": pixels " + r.name);
    }
}

static void testFormats() {
    Fixture f = makeFixture("formats");
    BuildResult b = build(f.p);
    CHECK(b.ok());
    std::vector<ExportFile> files;
    std::string err;
    CHECK(exportFiles(b, f.p.output.formats, files, &err));
    const std::string out = f.p.resolve(f.p.output.dir);
    CHECK(writeFiles(out, files, &err));

    // .atlas through the game's reader.
    toms::AtlasFile af;
    CHECK(toms::parseAtlas(readAll(out + "/game.atlas"), af, &err));
    CHECK(af.pages.size() == 1 && af.pages[0].w == b.pages[0].image.w);
    CHECK(af.regions.size() == b.regions.size());
    for (const Region& r : b.regions) {
        const toms::AtlasRegion* a = af.find(r.name);
        CHECK_MSG(a, r.name);
        if (!a) continue;
        CHECK_MSG(a->x == r.frame.x && a->y == r.frame.y && a->w == r.frame.w && a->h == r.frame.h, r.name);
        CHECK_MSG(a->offX == r.offsetX && a->offY == r.offsetY && a->origW == r.origW && a->origH == r.origH, r.name);
        CHECK_MSG(a->child == r.child && a->parent == r.parent, r.name);
        CHECK_MSG(a->pivot[0] == r.pivot[0] && a->pivot[1] == r.pivot[1], r.name);
        CHECK_MSG(a->tags == r.tags, r.name);
        CHECK_MSG(a->hasSplit == r.hasSplit, r.name);
        CHECK(std::abs(a->uv[0] - (float)r.frame.x / b.pages[0].image.w) < 1e-6f);
    }

    BuildResult in;
    CHECK(importLibgdx(out + "/game.atlas", in, &err));
    sameRegions(b, in, "atlas");
    CHECK(importLibgdx(out + "/game.spine3.atlas", in, &err));
    sameRegions(b, in, "spine3", false);
    CHECK(importTpJson(out + "/game.json", in, &err));
    sameRegions(b, in, "tp-json");
    CHECK(in.animations.size() == 1 && in.animations[0].frames.size() == 2 && in.animations[0].frames[1].time == 0.2f);
    CHECK(importPi(out + "/game.pi", in, &err));
    sameRegions(b, in, "pi");
    CHECK(in.animations.size() == 1 && in.animations[0].frames[1].sprite == "coin_copy");

    // plist: the frames dict has every sprite, textureRect matches.
    auto plist = parseXml(readAll(out + "/game.plist"), &err);
    CHECK(plist && plist->name == "plist");
    if (plist) {
        const XmlNode& dict = *plist->children[0];
        const XmlNode& frames = *dict.children[1];
        CHECK(frames.name == "dict" && frames.children.size() == b.regions.size() * 2);
        for (size_t i = 0; i + 1 < frames.children.size(); i += 2) {
            const Region* r = b.find(frames.children[i]->text);
            CHECK_MSG(r, frames.children[i]->text);
            if (!r) continue;
            const XmlNode& fd = *frames.children[i + 1];
            for (size_t k = 0; k + 1 < fd.children.size(); k += 2)
                if (fd.children[k]->text == "textureRect")
                    CHECK(fd.children[k + 1]->text == "{{" + std::to_string(r->frame.x) + "," + std::to_string(r->frame.y) + "},{" +
                                                         std::to_string(r->frame.w) + "," + std::to_string(r->frame.h) + "}}");
        }
    }
    // rcss: one line per sprite, '/' turned into '-'.
    const std::string rcss = readAll(out + "/game.rcss");
    CHECK(rcss.find("@spritesheet game") != std::string::npos);
    CHECK(rcss.find("ui-frame_tl: ") != std::string::npos);

    // Import into a project and rebuild: same pixels for every sprite.
    CHECK(importLibgdx(out + "/game.atlas", in, &err));
    Project p2;
    const std::string d2 = tempDir("formats_reimport");
    CHECK(importToProject(in, d2 + "/re.atlasproj", d2 + "/images", p2, &err));
    p2.settings.minWidth = p2.settings.minHeight = 1;
    BuildResult b2 = build(p2);
    CHECK(b2.ok());
    for (const Region& r : b.regions) {
        const Region* o = b2.find(r.name);
        CHECK_MSG(o && sameImage(renderRegion(b, r), renderRegion(b2, *o)), "reimport " + r.name);
        if (o) CHECK_MSG(o->child == r.child, "reimport child " + r.name);
    }

    // Writing unchanged files leaves them alone.
    const auto t0 = fs::last_write_time(out + "/game.atlas");
    CHECK(writeFiles(out, files, &err));
    CHECK(fs::last_write_time(out + "/game.atlas") == t0);
}

// ---- embedded projects (images inside the packed atlas) ----------------------------------------

static bool exists(const std::string& p) { std::error_code ec; return fs::is_regular_file(u8path(p), ec); }

static void testEmbedded() {
    Fixture f = makeFixture("embedded");
    const BuildResult ref = build(f.p);
    const BuildResult refHd = build(f.p, BuildOptions{"hd", true});
    CHECK(ref.ok() && refHd.ok());

    // Folder project -> embedded: the PNGs move inside, the folders become references.
    Project p = f.p;
    std::vector<Diagnostic> diags;
    CHECK(embedSources(p, fileImageSource(), &diags) == 5);
    CHECK(diags.empty());
    CHECK(p.embedded && p.sources.empty() && p.references.size() == 1);
    CHECK(p.variants[0].images.size() == 2 && p.variants[0].overrideDir.empty() && p.variants[0].reference == "style");

    // Saved somewhere else, with the packed atlas next to the project file.
    const std::string dir = tempDir("embedded_saved");
    p.output.dir = dir;
    p.variants[0].output.dir = dir + "/hd";
    SaveResult sr;
    std::string err;
    CHECK_MSG(saveProjectAll(dir + "/game.atlasproj", p, {"plist"}, &sr, &err), err);
    CHECK(p.output.dir == "." && p.variants[0].output.dir == "hd");
    CHECK(p.references[0].path != "sprites");   // still points at the fixture's folder
    for (const char* file : {"/game.atlasproj", "/game.atlas", "/game.png", "/game.plist", "/game.pi", "/hd/game.atlas", "/hd/game.png"})
        CHECK_MSG(exists(dir + file), file);

    // The original PNGs are not needed any more.
    fs::remove_all(u8path(f.dir + "/sprites"));
    fs::remove_all(u8path(f.dir + "/style"));
    Project q;
    CHECK_MSG(openProject(dir + "/game.atlasproj", q, &err), err);
    CHECK(q.embedded && q.images.size() == 5 && q.variants[0].images.size() == 2);
    const BuildResult b = build(q);
    const BuildResult bHd = build(q, BuildOptions{"hd", true});
    CHECK(b.ok() && bHd.ok());
    CHECK(b.regions.size() == ref.regions.size() && bHd.regions.size() == refHd.regions.size());
    for (const Region& r : ref.regions) {
        const Region* o = b.find(r.name);
        CHECK_MSG(o && sameImage(renderRegion(ref, r), renderRegion(b, *o)), "embedded " + r.name);
        if (o) CHECK_MSG(o->child == r.child && o->parent == r.parent && o->pivot[0] == r.pivot[0], "embedded meta " + r.name);
    }
    for (const Region& r : refHd.regions) {
        const Region* o = bHd.find(r.name);
        CHECK_MSG(o && sameImage(renderRegion(refHd, r), renderRegion(bHd, *o)), "embedded hd " + r.name);
    }

    // Saving again without changes rewrites nothing but the project file.
    SaveResult again;
    CHECK(saveProjectAll(q.filePath, q, {"plist"}, &again, &err));
    CHECK_MSG(again.written.size() == 1, std::to_string(again.written.size()) + " file(s) rewritten");

    // References: compared with what the project holds.
    fs::create_directories(u8path(f.dir + "/sprites"));
    savePng(f.dir + "/sprites/floor.png", makeImage(32, 32, 0, 1));   // same as inside
    savePng(f.dir + "/sprites/coin.png", makeImage(32, 32, 4, 9));    // changed
    savePng(f.dir + "/sprites/gem.png", makeImage(16, 16, 0, 7));     // new
    std::map<std::string, ReferenceImage::Status> st;
    for (const ReferenceImage& r : scanReferences(q, fileImageSource())) st[r.name] = r.status;
    CHECK(st.size() == 3);
    CHECK(st["floor"] == ReferenceImage::Same && st["coin"] == ReferenceImage::Changed && st["gem"] == ReferenceImage::New);
    Image gem;
    CHECK(loadImage(f.dir + "/sprites/gem.png", gem));
    CHECK(setImage(q, "gem", gem, "", &err));
    CHECK(!setImage(q, "ui/frame_tl", gem, "", &err));          // a child has no image
    CHECK(!setImage(q, "nothing", gem, "hd", &err));            // variant art needs a base sprite
    CHECK(saveProjectAll(q.filePath, q, {}, nullptr, &err));
    Project q2;
    CHECK(openProject(q.filePath, q2, &err) && q2.images.count("gem") && sameImage(*q2.images["gem"], gem));

    // An image that cannot be packed is never silently dropped: the save is refused.
    Project big = q2;
    CHECK(setImage(big, "huge", makeImage(5000, 4, 0, 3), "", &err));   // opaque: trimming cannot shrink it
    CHECK(!saveProjectAll(big.filePath, big, {}, nullptr, &err));
    CHECK(err.find("huge") != std::string::npos);
    Project q3;
    CHECK(openProject(q2.filePath, q3, &err) && !q3.images.count("huge"));   // nothing was written

    // Back to separate files.
    std::vector<std::string> written;
    const std::string xdir = tempDir("embedded_extract");
    CHECK(extractImages(q3, xdir, "", true, &written, &err));
    CHECK(written.size() == q3.images.size() + 5);   // images + children
    Image frame;
    CHECK(loadImage(xdir + "/ui/frame.png", frame) && frame.w == 64 && frame.h == 48);

    // A .pi imported as an embedded project, saved as .pi again, draws the same pixels.
    BuildResult old;
    CHECK(importPi(std::string(ATLAS_TEST_DATA) + "/9Slicing_TalkingDialob.pi", old, &err));
    const std::string pdir = tempDir("embedded_pi");
    Project pp;
    CHECK(importToProject(old, pdir + "/dialog.atlasproj", "", pp, &err));
    CHECK(pp.embedded && pp.images.size() == 1 && pp.sprites.size() == 3);   // bg_01 + 3 children
    pp.output.formats = {"pi"};
    CHECK_MSG(saveProjectAll(pp.filePath, pp, {}, nullptr, &err), err);
    BuildResult back;
    CHECK(importPi(pdir + "/9Slicing_TalkingDialob.pi", back, &err));
    for (const Region& r : old.regions) {
        const Region* o = back.find(r.name);
        CHECK_MSG(o && sameImage(renderRegion(old, r), renderRegion(back, *o)), "pi " + r.name);
    }
}

// ---- golden (.pi from the old editor) ----------------------------------------------------------

static void piRoundTrip(const std::string& path, int& count) {
    BuildResult old;
    std::string err;
    if (!importPi(path, old, &err)) { std::printf("    skip %s: %s\n", path.c_str(), err.c_str()); return; }
    count++;
    const bool trace = std::getenv("ATLAS_TRACE") != nullptr;
    auto step = [&](const char* what) { if (trace) { std::printf("      %s\n", what); std::fflush(stdout); } };
    step("tempdir");
    const std::string d = tempDir("golden_" + std::to_string(count));
    step("importToProject");
    Project p;
    CHECK_MSG(importToProject(old, d + "/p.atlasproj", d + "/img", p, &err), path + ": " + err);
    p.settings.maxWidth = p.settings.maxHeight = 8192;
    p.settings.minWidth = p.settings.minHeight = 1;
    step("build");
    BuildResult nb = build(p);
    CHECK_MSG(nb.ok(), path);
    step("export");
    std::vector<ExportFile> files;
    CHECK(exportFiles(nb, {"pi"}, files, &err));
    CHECK(writeFiles(d + "/out", files, &err));
    BuildResult back;
    step("reimport");
    CHECK_MSG(importPi(d + "/out/" + nb.name + ".pi", back, &err), path + ": " + err);
    step("compare");
    for (const Region& r : old.regions) {
        const Region* o = back.find(r.name);
        CHECK_MSG(o, path + ": missing " + r.name);
        if (!o) continue;
        CHECK_MSG(o->origW == r.origW && o->origH == r.origH, path + ": orig " + r.name);
        CHECK_MSG(o->child == r.child, path + ": child " + r.name);
        CHECK_MSG(sameVisible(renderRegion(old, r), renderRegion(back, *o)), path + ": pixels " + r.name);
    }
}

static void testGolden() {
    int n = 0;
    piRoundTrip(std::string(ATLAS_TEST_DATA) + "/9Slicing_TalkingDialob.pi", n);
    CHECK(n == 1);
    // The 9-slice sample: three PuzzleUnitChild units of bg_01.
    BuildResult old;
    std::string err;
    CHECK(importPi(std::string(ATLAS_TEST_DATA) + "/9Slicing_TalkingDialob.pi", old, &err));
    const Region* c = old.find("CentertUp");
    CHECK(c && c->child && c->parent == "bg_01" && c->local == (IRect{18, 0, 64, 18}));
    if (const char* corpus = std::getenv("ATLAS_PI_CORPUS")) {
        int m = 0;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(u8path(corpus), ec), end; !ec && it != end; it.increment(ec))
            if (it->path().extension() == ".pi") {
                const std::string file = u8str(it->path());
                if (std::getenv("ATLAS_TRACE")) { std::printf("    corpus file: %s\n", file.c_str()); std::fflush(stdout); }
                piRoundTrip(file, m);
            }
        std::printf("    corpus: %d .pi file(s)\n", m);
    }
}

int main() {
    run("packer", testPacker);
    run("build", testBuild);
    run("formats", testFormats);
    run("embedded", testEmbedded);
    run("golden .pi", testGolden);
    std::printf("%d check(s), %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
