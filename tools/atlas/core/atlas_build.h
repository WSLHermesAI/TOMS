// atlas_build.h -- turns a Project into atlas pages + regions (what every exporter writes).
#pragma once
#include "atlas_project.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace atlas {

struct Diagnostic {
    enum Level { Info, Warning, Error } level = Warning;
    std::string sprite;     // may be empty
    std::string message;
};

// One named rectangle in the atlas. Coordinates are pixels, origin top-left, y down.
struct Region {
    std::string name;
    int page = 0;
    IRect frame;             // where the pixels are in the page
    int offsetX = 0, offsetY = 0;   // where frame's top-left sits inside the original image
    int origW = 0, origH = 0;       // original (untrimmed) size
    float pivot[2] = {0.5f, 0.5f};
    bool hasSplit = false;
    int split[4] = {0, 0, 0, 0};    // left, right, top, bottom
    std::vector<std::string> tags;
    // Child sprite (no pixels of its own): parent + rect in the parent's original pixels.
    bool child = false;
    std::string parent;
    IRect local;
    bool baked = false;      // child with its own copy of the pixels (packed separately)
    std::string aliasOf;     // dedupe: same pixels as this region (frame is shared)
    std::string sourceFile;  // image sprites: the PNG it came from

    bool trimmed() const { return offsetX != 0 || offsetY != 0 || frame.w != origW || frame.h != origH; }
};

struct Page {
    std::string file;        // image file name, e.g. "game.png", "game_1.png"
    Image image;
};

struct BuildResult {
    std::string name;        // output base name
    Settings settings;
    std::vector<Page> pages;
    std::vector<Region> regions;          // sorted by name
    std::vector<Animation> animations;
    std::vector<Diagnostic> diagnostics;

    bool ok() const;
    const Region* find(const std::string& name) const;
    int errorCount() const;
    int warningCount() const;
};

// Where images come from. The default reads files; the editor caches decoded images, the web
// build reads from its in-memory file system.
struct ImageSource {
    std::function<bool(const std::string& path, Image& out, std::string* err)> load;
    std::function<bool(const std::string& path)> exists;
    // PNG paths below dir (absolute or project-resolved), '/' separators, sorted.
    std::function<std::vector<std::string>(const std::string& dir, bool recursive)> listPngs;
};
ImageSource fileImageSource();
// Same as fileImageSource() but keeps decoded images and reuses them while the file's size and
// modification time are unchanged (the editor rebuilds after every edit).
ImageSource cachedImageSource(std::shared_ptr<void>& cacheHolder);

// A sprite as the build sees it after merging scanned folders with the explicit entries.
struct ResolvedSprite {
    SpriteDef def;           // merged settings
    std::string file;        // resolved image path (folder projects; empty for a child)
    ImagePtr image;          // embedded projects: the pixels
    bool scanned = false;    // came from a source folder or the embedded images (named by them)
};
std::vector<ResolvedSprite> resolveSprites(const Project& p, const ImageSource& src,
                                           std::vector<Diagnostic>* diags = nullptr);

struct BuildOptions {
    std::string variant;     // empty = the base art
    bool composite = true;   // false: layout only (no page pixels), for fast previews
};

BuildResult build(const Project& p, const ImageSource& src, const BuildOptions& opt = {});
inline BuildResult build(const Project& p, const BuildOptions& opt = {}) { return build(p, fileImageSource(), opt); }

std::string formatDiagnostic(const Diagnostic& d);

}  // namespace atlas
