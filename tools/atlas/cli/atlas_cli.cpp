// atlas_cli.cpp -- the command line of the atlas tool: atlaspack, and `atlas_editor --headless`.
//
// Commands: new, add, refs, extract, embed, build, import, pack, info -- see kUsage below.
// Exit codes: 0 ok, 1 warnings and --strict, 2 errors, 3 bad command line.
#include "atlas_cli.h"
#include "atlas_build.h"
#include "atlas_export.h"
#include "atlas_store.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using namespace atlas;
namespace fs = std::filesystem;

namespace {

const char* kUsage =
    "atlaspack -- packs sprites into texture atlases (TOMS atlas tool)\n"
    "\n"
    "Projects (.atlasproj) normally keep their images inside the packed atlas next to them\n"
    "(embedded, like the old PI editor); folders are only references to import art from.\n"
    "\n"
    "  atlaspack new     <OUT.atlasproj> [--source DIR]... [--format LIST] [options]\n"
    "                    a new embedded project; --source folders are imported and kept as references\n"
    "  atlaspack add     <project> <png|folder>... [--prefix P] [--variant ID] [--replace]\n"
    "  atlaspack refs    <project> [--variant ID] [--import new|changed|all]\n"
    "                    compare the reference folders with the project; import from them\n"
    "  atlaspack extract <project> --out DIR [--variant ID] [--children]   every sprite as a PNG again\n"
    "  atlaspack embed   <folder-project> [--save-as NEW.atlasproj] [--keep-output]\n"
    "                    turn a folder project into an embedded one (the folders become references)\n"
    "  atlaspack build   <project> [--variant ID | --all-variants] [--out DIR] [--format LIST] [--strict] [--check]\n"
    "                    --check: write nothing; exit 1 if the files on disk differ from a fresh build\n"
    "  atlaspack import  <file.pi|.atlas|.json> --project OUT.atlasproj [--images DIR] [--format LIST]\n"
    "                    an embedded project from an existing atlas (--images: a folder project instead)\n"
    "  atlaspack pack    <folder>... --out DIR [--name NAME] [--format LIST] [options] [--save-project FILE]\n"
    "                    pack folders straight to files (a folder project)\n"
    "  atlaspack info    <file.pi|.atlas|.json>\n"
    "\n"
    "formats (LIST is comma separated): atlas, atlas-spine3, plist, tp-json, pi, rcss\n"
    "options: --max WxH  --pot  --square  --padding N  --border N  --extrude N  --no-trim\n"
    "         --alpha-threshold N  --no-dedupe  --heuristic auto|bssf|blsf|baf|bl|cp|shelf  --filter Nearest|Linear\n"
    "exit codes: 0 ok, 1 warnings with --strict, 2 errors, 3 bad command line\n";

struct Args {
    std::vector<std::string> pos;
    std::vector<std::pair<std::string, std::string>> opts;   // --key value (value "" for flags)

    bool has(const std::string& k) const {
        for (const auto& o : opts) if (o.first == k) return true;
        return false;
    }
    std::string get(const std::string& k, const std::string& def = std::string()) const {
        for (const auto& o : opts) if (o.first == k) return o.second;
        return def;
    }
    std::vector<std::string> all(const std::string& k) const {
        std::vector<std::string> v;
        for (const auto& o : opts) if (o.first == k) v.push_back(o.second);
        return v;
    }
};

const std::vector<std::string> kFlags = {"--pot", "--square", "--no-trim", "--no-dedupe", "--strict", "--all-variants",
                                         "--quiet", "--help", "--check", "--replace", "--children", "--keep-output"};

bool parseArgs(const std::vector<std::string>& argv, size_t from, Args& a, std::string& err) {
    const size_t argc = argv.size();
    for (size_t i = from; i < argc; i++) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            std::string val;
            const size_t eq = s.find('=');
            if (eq != std::string::npos) { val = s.substr(eq + 1); s = s.substr(0, eq); }
            else if (std::find(kFlags.begin(), kFlags.end(), s) == kFlags.end()) {
                if (i + 1 >= argc) { err = s + " needs a value"; return false; }
                val = argv[++i];
            }
            a.opts.emplace_back(s, val);
        } else {
            a.pos.push_back(s);
        }
    }
    return true;
}

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : s + ",") {
        if (c == ',') { if (!cur.empty()) v.push_back(cur); cur.clear(); }
        else if (c != ' ') cur += c;
    }
    return v;
}

bool toInt(const std::string& s, int& out) {
    char* end = nullptr;
    const long v = strtol(s.c_str(), &end, 10);
    if (s.empty() || *end) return false;
    out = (int)v;
    return true;
}

// Applies packing options to settings; false on a bad value.
bool applyPackOptions(const Args& a, Settings& s, std::string& err) {
    if (a.has("--max")) {
        const std::string m = a.get("--max");
        const size_t x = m.find_first_of("xX");
        int w = 0, h = 0;
        if (x == std::string::npos ? !toInt(m, w) : (!toInt(m.substr(0, x), w) || !toInt(m.substr(x + 1), h))) { err = "bad --max " + m; return false; }
        s.maxWidth = w; s.maxHeight = h ? h : w;
    }
    if (a.has("--pot")) s.powerOfTwo = true;
    if (a.has("--square")) s.square = true;
    if (a.has("--no-trim")) s.trim = false;
    if (a.has("--no-dedupe")) s.dedupe = false;
    for (const char* k : {"--padding", "--border", "--extrude", "--alpha-threshold"}) {
        if (!a.has(k)) continue;
        int v;
        if (!toInt(a.get(k), v) || v < 0) { err = std::string("bad ") + k + " " + a.get(k); return false; }
        if (!strcmp(k, "--padding")) s.padding = v;
        else if (!strcmp(k, "--border")) s.border = v;
        else if (!strcmp(k, "--extrude")) s.extrude = v;
        else s.alphaThreshold = v;
    }
    if (a.has("--heuristic") && !parseHeuristic(a.get("--heuristic"), s.heuristic)) { err = "bad --heuristic " + a.get("--heuristic"); return false; }
    if (a.has("--filter")) s.filter = a.get("--filter");
    return true;
}

bool checkFormats(const std::vector<std::string>& f, std::string& err) {
    for (const std::string& x : f)
        if (!isExportFormat(x)) { err = "unknown format '" + x + "'"; return false; }
    return true;
}

// --check: are the files on disk what a build would write? Lists the ones that are not.
bool upToDate(const std::string& dir, const std::vector<ExportFile>& files, const std::string& label) {
    bool ok = true;
    for (const ExportFile& f : files) {
        std::ifstream in(u8path(joinPath(dir, f.name)), std::ios::binary);
        const std::vector<uint8_t> old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (in.is_open() && old == f.data) continue;
        std::fprintf(stderr, "%s: error: %s is %s -- run atlaspack build\n", label.c_str(), joinPath(dir, f.name).c_str(),
                     in.is_open() ? "out of date" : "missing");
        ok = false;
    }
    return ok;
}

// Builds one atlas and writes it (or with `check`, only compares). Returns the exit code.
int buildAndWrite(const Project& p, const std::string& variant, const std::string& outOverride,
                  const std::vector<std::string>& formatsOverride, bool strict, bool quiet, bool check = false) {
    const Variant* v = variant.empty() ? nullptr : p.findVariant(variant);
    const Output& o = v ? v->output : p.output;
    const std::string label = p.filePath.empty() ? p.name : p.filePath;
    BuildResult b = build(p, BuildOptions{variant, true});
    for (const Diagnostic& d : b.diagnostics)
        if (d.level != Diagnostic::Info || !quiet) std::fprintf(stderr, "%s: %s\n", label.c_str(), formatDiagnostic(d).c_str());
    if (!b.ok()) {
        std::fprintf(stderr, "%s: %d error(s); nothing written\n", label.c_str(), b.errorCount());
        return 2;
    }
    std::vector<std::string> formats = formatsOverride.empty() ? o.formats : formatsOverride;
    // An embedded project's images live in its .atlas: keep writing it to the project's own folder.
    if (p.embedded && outOverride.empty() && std::find(formats.begin(), formats.end(), "atlas") == formats.end())
        formats.push_back("atlas");
    std::vector<ExportFile> files;
    std::string err;
    if (!exportFiles(b, formats, files, &err)) { std::fprintf(stderr, "%s: error: %s\n", label.c_str(), err.c_str()); return 2; }
    const std::string dir = outOverride.empty() ? p.resolve(o.dir) : outOverride;
    if (check) {
        if (!upToDate(dir, files, label)) return 1;
        if (!quiet) std::printf("%s -> %s: up to date%s%s\n", label.c_str(), dir.c_str(), variant.empty() ? "" : ", variant ", variant.c_str());
        return strict && b.warningCount() > 0 ? 1 : 0;
    }
    if (!writeFiles(dir, files, &err)) { std::fprintf(stderr, "%s: error: %s\n", label.c_str(), err.c_str()); return 2; }
    if (!quiet) {
        std::printf("%s -> %s: %zu sprite(s), %zu page(s)", label.c_str(), dir.c_str(), b.regions.size(), b.pages.size());
        if (!variant.empty()) std::printf(" (variant %s)", variant.c_str());
        for (const Page& pg : b.pages) std::printf(" %s %dx%d", pg.file.c_str(), pg.image.w, pg.image.h);
        std::printf("\n");
    }
    return strict && b.warningCount() > 0 ? 1 : 0;
}

int cmdBuild(const Args& a) {
    if (a.pos.size() != 1) { std::fprintf(stderr, "build: give one project file\n"); return 3; }
    Project p;
    std::string err;
    if (!openProject(a.pos[0], p, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    const std::vector<std::string> formats = splitList(a.get("--format"));
    if (!checkFormats(formats, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 3; }
    const bool strict = a.has("--strict"), quiet = a.has("--quiet");
    const std::string out = a.get("--out");
    std::vector<std::string> variants;
    if (a.has("--variant")) variants.push_back(a.get("--variant"));
    else {
        variants.push_back("");
        if (a.has("--all-variants"))
            for (const Variant& v : p.variants) variants.push_back(v.id);
    }
    if (!out.empty() && variants.size() > 1) { std::fprintf(stderr, "build: --out cannot be used with --all-variants\n"); return 3; }
    int code = 0;
    for (const std::string& v : variants)
        code = std::max(code, buildAndWrite(p, v, out, formats, strict, quiet, a.has("--check")));
    return code;
}

int cmdPack(const Args& a) {
    if (a.pos.empty() || !a.has("--out")) { std::fprintf(stderr, "pack: give at least one folder and --out DIR\n"); return 3; }
    Project p;
    std::string err;
    const std::string outDir = normalizePath(a.get("--out"));
    p.name = a.get("--name", fileStem(u8str(u8path(a.pos[0]).lexically_normal())));
    if (p.name.empty() || p.name == ".") p.name = "atlas";
    if (!applyPackOptions(a, p.settings, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 3; }
    if (a.has("--format")) p.output.formats = splitList(a.get("--format"));
    if (!checkFormats(p.output.formats, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 3; }
    const std::string save = a.get("--save-project");
    p.filePath = save.empty() ? std::string() : normalizePath(save);
    for (const std::string& dir : a.pos) {
        const std::string abs = u8str(fs::absolute(u8path(dir)).lexically_normal());
        p.sources.push_back({p.filePath.empty() ? abs : p.relativize(abs), true, ""});
    }
    const std::string absOut = u8str(fs::absolute(u8path(outDir)).lexically_normal());
    p.output.dir = p.filePath.empty() ? absOut : p.relativize(absOut);
    if (!save.empty() && !saveProject(save, p, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    return buildAndWrite(p, "", "", {}, a.has("--strict"), a.has("--quiet"));
}

// Saves an embedded project with its packed atlas and reports it. Returns the exit code.
int saveAll(const std::string& path, Project& p, bool quiet) {
    SaveResult r;
    std::string err;
    const bool ok = saveProjectAll(path, p, {}, &r, &err);
    for (const Diagnostic& d : r.diagnostics)
        if (d.level != Diagnostic::Info || !quiet) std::fprintf(stderr, "%s: %s\n", path.c_str(), formatDiagnostic(d).c_str());
    if (!ok) { std::fprintf(stderr, "%s: error: %s; nothing written\n", path.c_str(), err.c_str()); return 2; }
    if (!quiet) for (const std::string& f : r.written) std::printf("  wrote %s\n", f.c_str());
    return 0;
}

int cmdImport(const Args& a) {
    if (a.pos.size() != 1 || !a.has("--project")) { std::fprintf(stderr, "import: give one atlas file and --project OUT.atlasproj\n"); return 3; }
    BuildResult in;
    std::string err;
    if (!importAtlasFile(a.pos[0], in, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    for (const Diagnostic& d : in.diagnostics) std::fprintf(stderr, "%s: %s\n", a.pos[0].c_str(), formatDiagnostic(d).c_str());
    const std::string projPath = a.get("--project");
    const std::string images = a.get("--images");   // empty: keep the images inside the project
    Project p;
    if (!importToProject(in, projPath, images, p, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    std::string ext = u8str(u8path(a.pos[0]).extension());
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    p.output.formats = a.has("--format") ? splitList(a.get("--format"))
                                         : std::vector<std::string>{ext == ".pi" ? "pi" : ext == ".json" ? "tp-json" : "atlas"};
    if (!checkFormats(p.output.formats, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 3; }
    p.output.dir = ".";
    int children = 0;
    for (const Region& r : in.regions) children += r.child;
    std::printf("%s -> %s: %zu sprite(s) (%d child)%s%s\n", a.pos[0].c_str(), projPath.c_str(), in.regions.size(), children,
                images.empty() ? "" : ", images in ", images.c_str());
    if (!p.embedded) {
        if (!saveProject(projPath, p, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
        return 0;
    }
    return saveAll(projPath, p, a.has("--quiet"));
}

int cmdInfo(const Args& a) {
    if (a.pos.size() != 1) { std::fprintf(stderr, "info: give one atlas file\n"); return 3; }
    BuildResult in;
    std::string err;
    if (!importAtlasFile(a.pos[0], in, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    for (size_t i = 0; i < in.pages.size(); i++)
        std::printf("page %zu: %s %dx%d\n", i, in.pages[i].file.c_str(), in.pages[i].image.w, in.pages[i].image.h);
    int bad = 0;
    for (const Region& r : in.regions) {
        std::printf("  %-32s page %d  frame %d,%d %dx%d  orig %dx%d  offset %d,%d", r.name.c_str(), r.page, r.frame.x,
                    r.frame.y, r.frame.w, r.frame.h, r.origW, r.origH, r.offsetX, r.offsetY);
        if (r.child) std::printf("  child of %s @ %d,%d", r.parent.c_str(), r.local.x, r.local.y);
        std::printf("\n");
        const Image& pg = in.pages[r.page].image;
        if (r.frame.x < 0 || r.frame.y < 0 || r.frame.right() > pg.w || r.frame.bottom() > pg.h) {
            std::fprintf(stderr, "%s: error: %s: frame lies outside its page\n", a.pos[0].c_str(), r.name.c_str());
            bad++;
        }
        if (r.offsetX + r.frame.w > r.origW || r.offsetY + r.frame.h > r.origH) {
            std::fprintf(stderr, "%s: warning: %s: trimmed rect is larger than the original size\n", a.pos[0].c_str(), r.name.c_str());
        }
    }
    for (const Animation& an : in.animations) std::printf("animation %s: %zu frame(s)\n", an.name.c_str(), an.frames.size());
    for (const Diagnostic& d : in.diagnostics) std::fprintf(stderr, "%s: %s\n", a.pos[0].c_str(), formatDiagnostic(d).c_str());
    return bad ? 2 : 0;
}

// Adds every PNG from `paths` (files, or folders searched below) to the project. Returns how many.
int addPngs(Project& p, const std::vector<std::string>& paths, const std::string& prefix, const std::string& variant,
            bool replace, bool quiet, int& errors) {
    const ImageSource src = fileImageSource();
    int added = 0;
    for (const std::string& arg : paths) {
        std::vector<std::pair<std::string, std::string>> files;   // file, name
        std::error_code ec;
        if (fs::is_directory(u8path(arg), ec)) {
            const std::string dir = u8str(fs::absolute(u8path(arg)).lexically_normal());
            for (const std::string& f : src.listPngs(dir, true)) files.push_back({f, imageNameFor(f, dir, prefix)});
        } else {
            files.push_back({arg, imageNameFor(arg, "", prefix)});
        }
        for (const auto& [file, name] : files) {
            const bool exists = variant.empty() ? p.images.count(name) > 0
                                                : (p.findVariant(variant) && p.findVariant(variant)->images.count(name) > 0);
            if (exists && !replace) {
                if (!quiet) std::printf("  skipped %s (already in the project; --replace to update)\n", name.c_str());
                continue;
            }
            Image img;
            std::string err;
            if (!loadImage(file, img, &err) || !setImage(p, name, img, variant, &err)) {
                std::fprintf(stderr, "error: %s\n", err.c_str());
                errors++;
                continue;
            }
            if (!quiet) std::printf("  %s %s\n", exists ? "replaced" : "added", name.c_str());
            added++;
        }
    }
    return added;
}

int cmdNew(const Args& a) {
    if (a.pos.size() != 1) { std::fprintf(stderr, "new: give the project file to create\n"); return 3; }
    const std::string path = a.pos[0];
    if (fs::exists(u8path(path))) { std::fprintf(stderr, "new: %s already exists\n", path.c_str()); return 2; }
    Project p;
    p.filePath = normalizePath(path);
    p.name = fileStem(path);
    p.embedded = true;
    std::string err;
    if (!applyPackOptions(a, p.settings, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 3; }
    if (a.has("--format")) p.output.formats = splitList(a.get("--format"));
    if (!checkFormats(p.output.formats, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 3; }
    // --source folders: their PNGs go in now, and the folders stay as references.
    int errors = 0;
    const std::vector<std::string> sources = a.all("--source");
    addPngs(p, sources, "", "", false, a.has("--quiet"), errors);
    for (const std::string& s : sources) p.references.push_back({p.relativize(u8str(fs::absolute(u8path(s)))), true, ""});
    if (errors) return 2;
    return saveAll(path, p, a.has("--quiet"));
}

int cmdEmbed(const Args& a) {
    if (a.pos.size() != 1) { std::fprintf(stderr, "embed: give one project file\n"); return 3; }
    Project p;
    std::string err;
    if (!loadProject(a.pos[0], p, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    if (p.embedded) { std::fprintf(stderr, "embed: %s is already an embedded project\n", a.pos[0].c_str()); return 2; }
    std::vector<Diagnostic> diags;
    const int n = embedSources(p, fileImageSource(), &diags);
    int errors = 0;
    for (const Diagnostic& d : diags) {
        std::fprintf(stderr, "%s: %s\n", a.pos[0].c_str(), formatDiagnostic(d).c_str());
        errors += d.level == Diagnostic::Error;
    }
    if (errors) { std::fprintf(stderr, "%s: %d error(s); nothing written\n", a.pos[0].c_str(), errors); return 2; }
    const std::string target = a.get("--save-as", a.pos[0]);
    // The packed atlas (the store) goes next to the project file, unless asked to keep the output folder.
    if (!a.has("--keep-output")) p.output.dir = p.relativize(u8str(fs::absolute(u8path(parentDir(target)))));
    std::printf("%s: %d image(s) taken in\n", a.pos[0].c_str(), n);
    return saveAll(target, p, a.has("--quiet"));
}

int cmdAdd(const Args& a) {
    if (a.pos.size() < 2) { std::fprintf(stderr, "add: give the project and at least one PNG or folder\n"); return 3; }
    Project p;
    std::string err;
    if (!openProject(a.pos[0], p, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    if (!p.embedded) { std::fprintf(stderr, "add: %s reads its images from folders; put the PNGs there (or run embed first)\n", a.pos[0].c_str()); return 2; }
    int errors = 0;
    const std::vector<std::string> paths(a.pos.begin() + 1, a.pos.end());
    const int n = addPngs(p, paths, a.get("--prefix"), a.get("--variant"), a.has("--replace"), a.has("--quiet"), errors);
    if (errors) return 2;
    if (n == 0) { std::printf("nothing to add\n"); return 0; }
    return saveAll(a.pos[0], p, a.has("--quiet"));
}

int cmdRefs(const Args& a) {
    if (a.pos.size() != 1) { std::fprintf(stderr, "refs: give one project file\n"); return 3; }
    Project p;
    std::string err;
    if (!openProject(a.pos[0], p, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    const std::string variant = a.get("--variant");
    if (!variant.empty() && !p.findVariant(variant)) { std::fprintf(stderr, "error: unknown variant '%s'\n", variant.c_str()); return 2; }
    const std::string mode = a.get("--import");   // new | changed | all
    if (!mode.empty() && mode != "new" && mode != "changed" && mode != "all") { std::fprintf(stderr, "refs: --import new|changed|all\n"); return 3; }
    const char* names[] = {"new", "changed", "same"};
    int taken = 0;
    for (const ReferenceImage& r : scanReferences(p, fileImageSource(), variant)) {
        std::printf("  %-8s %-32s %s\n", names[r.status], r.name.c_str(), r.file.c_str());
        const bool want = (r.status == ReferenceImage::New && (mode == "new" || mode == "all")) ||
                          (r.status == ReferenceImage::Changed && (mode == "changed" || mode == "all"));
        if (!want) continue;
        Image img;
        if (!loadImage(r.file, img, &err) || !setImage(p, r.name, img, variant, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
        taken++;
    }
    if (!taken) return 0;
    std::printf("imported %d image(s)\n", taken);
    return saveAll(a.pos[0], p, a.has("--quiet"));
}

int cmdExtract(const Args& a) {
    if (a.pos.size() != 1 || !a.has("--out")) { std::fprintf(stderr, "extract: give one project file and --out DIR\n"); return 3; }
    Project p;
    std::string err;
    if (!openProject(a.pos[0], p, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    std::vector<std::string> written;
    if (!extractImages(p, a.get("--out"), a.get("--variant"), a.has("--children"), &written, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }
    std::printf("%zu image(s) written to %s\n", written.size(), a.get("--out").c_str());
    return 0;
}

}  // namespace

std::vector<std::string> utf8CommandLine(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    // argv is in the ANSI code page; the wide command line keeps every character.
    int n = 0;
    if (LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n)) {
        for (int i = 1; i < n; i++) {
            const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(len > 0 ? (size_t)len - 1 : 0, '\0');
            if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], len, nullptr, nullptr);
            out.push_back(s);
        }
        LocalFree(w);
        SetConsoleOutputCP(CP_UTF8);
        return out;
    }
#endif
    for (int i = 1; i < argc; i++) out.push_back(argv[i]);
    return out;
}

int atlasCliMain(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        std::fputs(kUsage, args.empty() ? stderr : stdout);
        return args.empty() ? 3 : 0;
    }
    const std::string& cmd = args[0];
    Args a;
    std::string err;
    if (!parseArgs(args, 1, a, err)) { std::fprintf(stderr, "error: %s\n%s", err.c_str(), kUsage); return 3; }
    if (cmd == "build") return cmdBuild(a);
    if (cmd == "pack") return cmdPack(a);
    if (cmd == "import") return cmdImport(a);
    if (cmd == "info") return cmdInfo(a);
    if (cmd == "new") return cmdNew(a);
    if (cmd == "embed") return cmdEmbed(a);
    if (cmd == "add") return cmdAdd(a);
    if (cmd == "refs") return cmdRefs(a);
    if (cmd == "extract") return cmdExtract(a);
    if (cmd == "formats") { for (const std::string& f : exportFormats()) std::printf("%s\n", f.c_str()); return 0; }
    std::fprintf(stderr, "unknown command '%s'\n%s", cmd.c_str(), kUsage);
    return 3;
}
