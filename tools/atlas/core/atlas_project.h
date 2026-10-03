// atlas_project.h -- the .atlasproj project file: what goes into an atlas and how it is built.
//
// Paths in the file are relative to the project file and use '/'.
//
// Two kinds of project:
//   - EMBEDDED (what the editors make, like the old PI editor): the images live in the packed
//     atlas the project writes next to itself (<output.dir>/<name>.atlas + its PNG pages). Opening
//     the project cuts every sprite back out of the packed texture into memory (atlas_store.h), so
//     the original PNGs are not needed. "references" are folders new or changed art can be
//     imported from; builds never read them.
//   - FOLDER (atlaspack pack, build scripts): "sources" are folders scanned for PNGs at every
//     build. A sprite's name is its path below the folder without ".png" ("ui/btn_ok").
//
// In both, "sprites" holds explicit entries: settings for an image by name (pivot, 9-slice, tags,
// pin...), or a CHILD sprite ("parent" + "rect"): no pixels of its own, a rectangle inside its
// parent (in the parent's original, untrimmed pixels). A folder project may also add single
// images with "file".
//
// Variants (art styles) replace images by name and write their own atlas with the same names.
#pragma once
#include "atlas_image.h"
#include "atlas_packer.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace atlas {

// Every string in the tool is UTF-8. These turn one into a path and back. On Windows a plain
// std::string path is read in the ANSI code page, and converting a path with characters that page
// cannot hold (e.g. a "教學" folder) throws -- never use .string()/.generic_string() on a path.
std::filesystem::path u8path(const std::string& utf8);
std::string u8str(const std::filesystem::path& p);   // UTF-8 with '/' separators

// Images are shared and never changed in place, so copying a project (undo snapshots, background
// builds) copies pointers, not pixels.
using ImagePtr = std::shared_ptr<const Image>;

struct Settings {
    int maxWidth = 2048, maxHeight = 2048;
    int minWidth = 16, minHeight = 16;
    bool powerOfTwo = false;
    bool square = false;
    bool fixedSize = false;
    int padding = 2;            // pixels between sprites
    int border = 0;             // pixels along the page edges
    int extrude = 0;            // edge pixels repeated around each sprite (stops filtering seams)
    bool trim = true;           // cut away transparent edges (the original size and offset are kept)
    int alphaThreshold = 0;     // alpha <= this counts as transparent when trimming
    bool dedupe = true;         // identical images share one place in the atlas
    bool premultiplyAlpha = false;
    std::string filter = "Nearest";   // written to the atlas: Nearest or Linear
    Heuristic heuristic = Heuristic::Auto;
    float defaultPivot[2] = {0.5f, 0.5f};
};

struct Output {
    std::string dir = ".";              // relative to the project file
    std::string name;                   // base file name; empty = project name
    // atlas (libGDX/Spine 4 + x_ extensions), atlas-spine3 (strict legacy), plist (Cocos),
    // tp-json (TexturePacker JSON hash), pi (FM79979 PuzzleImage), rcss (RmlUi @spritesheet)
    std::vector<std::string> formats = {"atlas"};
};

struct SourceFolder {
    std::string path;
    bool recursive = true;
    std::string prefix;                 // prepended to the names found here
};

struct SpriteDef {
    std::string name;
    std::string file;                   // image sprite added by hand (relative path)
    std::string parent;                 // child sprite: the parent's name
    IRect rect;                         // child sprite: rect in the parent's original pixels
    bool bake = false;                  // child sprite: copy its pixels and pack them separately
    bool exclude = false;               // a scanned sprite left out of the atlas
    bool hasPivot = false;
    float pivot[2] = {0.5f, 0.5f};      // 0..1 of the original size, from the top-left
    bool hasSplit = false;
    int split[4] = {0, 0, 0, 0};        // 9-slice borders: left, right, top, bottom (pixels)
    int trim = -1;                      // -1 = project setting, 0 = off, 1 = on
    bool pinned = false;                // fixed place in the atlas (top-left of the sprite)
    int pinPage = 0, pinX = 0, pinY = 0;
    std::vector<std::string> tags;

    bool isChild() const { return !parent.empty(); }
};

struct AnimFrame { std::string sprite; float time = 0.1f; };
struct Animation {
    std::string name;
    bool loop = true;
    std::vector<AnimFrame> frames;
};

struct Variant {
    std::string id;
    std::string overrideDir;            // folder project: <overrideDir>/<sprite name>.png replaces that sprite
    std::string reference;              // embedded project: a folder to import replacement art from
    Output output;
    // Embedded project: replacement art by sprite name, stored in the variant's own packed atlas.
    // The JSON lists the names; openProject() fills in the pixels.
    std::map<std::string, ImagePtr> images;
};

struct Project {
    int version = 1;
    std::string name = "atlas";
    Settings settings;
    Output output;
    bool embedded = false;                      // images live in the packed atlas (see the top)
    std::map<std::string, ImagePtr> images;     // embedded images by sprite name (not in the JSON)
    std::vector<SourceFolder> references;       // embedded: folders to import art from
    std::vector<SourceFolder> sources;          // folder project: scanned at every build
    std::vector<SpriteDef> sprites;
    std::vector<Animation> animations;
    std::vector<Variant> variants;

    std::string filePath;               // where it was loaded from / saved to (not stored)

    std::string baseDir() const;        // folder of filePath ("." when unsaved)
    std::string resolve(const std::string& rel) const;   // project-relative -> usable path
    std::string relativize(const std::string& path) const;
    SpriteDef* findSprite(const std::string& name);
    const SpriteDef* findSprite(const std::string& name) const;
    const Variant* findVariant(const std::string& id) const;
    std::string outputName(const Output& o) const { return o.name.empty() ? name : o.name; }
};

// The JSON only. An embedded project also needs its images: use openProject() and
// saveProjectAll() (atlas_store.h), which read and write the packed atlas too.
bool loadProject(const std::string& path, Project& out, std::string* err = nullptr);
bool saveProject(const std::string& path, Project& p, std::string* err = nullptr);   // sets p.filePath
bool projectFromJson(const std::string& text, Project& out, std::string* err = nullptr);
std::string projectToJson(const Project& p);

// Path helpers ('/' separators everywhere).
std::string normalizePath(std::string p);
std::string joinPath(const std::string& a, const std::string& b);
std::string parentDir(const std::string& p);
std::string fileStem(const std::string& p);          // "a/b/c.png" -> "c"
std::string fileName(const std::string& p);          // "a/b/c.png" -> "c.png"

}  // namespace atlas
