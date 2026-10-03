// atlas_store.h -- embedded projects: the packed atlas is where the images live.
//
// Like the old PI editor: saving writes the project and its packed atlas side by side
// (<output.dir>/<name>.atlas + PNG pages, plus the other output formats); opening cuts every sprite
// back out of the packed texture into memory at its original size. Each variant keeps its
// replacement art the same way in its own packed atlas. The original PNGs are not needed; folders
// can stay around as references to import new or changed art from.
#pragma once
#include "atlas_build.h"

#include <string>
#include <vector>

namespace atlas {

// The packed atlas that holds an embedded project's images (variant: its replacement art).
std::string storePath(const Project& p, const Variant* v = nullptr);

// loadProject() + the images of an embedded project (and of its variants) from the packed atlas.
bool openProject(const std::string& path, Project& out, std::string* err = nullptr);
bool loadEmbeddedImages(Project& p, std::string* err = nullptr);

struct SaveResult {
    std::vector<std::string> written;       // files that changed on disk
    std::vector<Diagnostic> diagnostics;    // from the builds (child errors etc. do not stop a save)
};

// Saves the JSON to `path` (moving relative paths along when it is a new place, see rebaseProject)
// and, for an embedded project, builds the base art and every variant and writes their output
// formats plus `alsoFormats` -- always including "atlas", the store. Refuses (writing nothing)
// when an image would not be stored, e.g. it is larger than the maximum page.
bool saveProjectAll(const std::string& path, Project& p, const std::vector<std::string>& alsoFormats = {},
                    SaveResult* result = nullptr, std::string* err = nullptr);

// Gives the project a new file path, rewriting its relative paths (output folders, references,
// sources, files) so they still point at the same places -- except an output folder of ".", which
// stays next to the project (Save As takes the packed atlas along).
void rebaseProject(Project& p, const std::string& newPath);

// Turns a folder project into an embedded one: every image found through sources/files goes into
// p.images, each variant's override art into variant.images, and the folders become references.
// Returns the number of images taken in.
int embedSources(Project& p, const ImageSource& src, std::vector<Diagnostic>* diags = nullptr);

// Art in the reference folders (project, or one variant's), compared with what the project holds.
struct ReferenceImage {
    enum Status { New, Changed, Same };
    std::string name;   // sprite name it maps to (prefix + path below the folder, without .png)
    std::string file;   // the PNG
    Status status = New;
};
std::vector<ReferenceImage> scanReferences(const Project& p, const ImageSource& src, const std::string& variant = "");

// Adds or replaces one image (base art, or a variant's replacement art).
bool setImage(Project& p, const std::string& name, const Image& img, const std::string& variant = "",
              std::string* err = nullptr);
// The name a PNG gets when added from `folder` (path below it without .png, '/' separators), or
// its file name without .png when `folder` is empty.
std::string imageNameFor(const std::string& file, const std::string& folder = "", const std::string& prefix = "");

// Writes every image sprite (and with `children`, every child) at its original size as
// <dir>/<name>.png -- the way back to separate files.
bool extractImages(const Project& p, const std::string& dir, const std::string& variant, bool children,
                   std::vector<std::string>* written = nullptr, std::string* err = nullptr);

}  // namespace atlas
