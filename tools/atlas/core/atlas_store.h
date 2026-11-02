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
std::string f_StorePath(const Project& l_oP, const Variant* l_pV = nullptr);

// loadProject() + the images of an embedded project (and of its variants) from the packed atlas.
bool f_OpenProject(const std::string& l_strPath, Project& l_oOut, std::string* l_pErr = nullptr);
bool f_LoadEmbeddedImages(Project& l_oP, std::string* l_pErr = nullptr);

struct SaveResult {
    std::vector<std::string> m_vWritten;       // files that changed on disk
    std::vector<Diagnostic> m_vDiagnostics;    // from the builds (child errors etc. do not stop a save)
};

// Saves the JSON to `path` (moving relative paths along when it is a new place, see rebaseProject)
// and, for an embedded project, builds the base art and every variant and writes their output
// formats plus `alsoFormats` -- always including "atlas", the store. Refuses (writing nothing)
// when an image would not be stored, e.g. it is larger than the maximum page.
bool f_SaveProjectAll(const std::string& l_strPath, Project& l_oP, const std::vector<std::string>& l_vAlsoFormats = {},
                    SaveResult* l_pResult = nullptr, std::string* l_pErr = nullptr);

// Gives the project a new file path, rewriting its relative paths (output folders, references,
// sources, files) so they still point at the same places -- except an output folder of ".", which
// stays next to the project (Save As takes the packed atlas along).
void f_RebaseProject(Project& l_oP, const std::string& l_strNewPath);

// Turns a folder project into an embedded one: every image found through sources/files goes into
// p.images, each variant's override art into variant.images, and the folders become references.
// Returns the number of images taken in.
int f_EmbedSources(Project& l_oP, const ImageSource& l_oSrc, std::vector<Diagnostic>* l_pDiags = nullptr);

// Art in the reference folders (project, or one variant's), compared with what the project holds.
struct ReferenceImage {
    enum Status { New, Changed, Same };
    std::string m_strName;   // sprite name it maps to (prefix + path below the folder, without .png)
    std::string m_strFile;   // the PNG
    Status m_eStatus = New;
};
std::vector<ReferenceImage> f_ScanReferences(const Project& l_oP, const ImageSource& l_oSrc, const std::string& l_strVariant = "");

// Adds or replaces one image (base art, or a variant's replacement art).
bool f_SetImage(Project& l_oP, const std::string& l_strName, const Image& l_oImg, const std::string& l_strVariant = "",
              std::string* l_pErr = nullptr);
// The name a PNG gets when added from `folder` (path below it without .png, '/' separators), or
// its file name without .png when `folder` is empty.
std::string f_ImageNameFor(const std::string& l_strFile, const std::string& l_strFolder = "", const std::string& l_strPrefix = "");

// Writes every image sprite (and with `children`, every child) at its original size as
// <dir>/<name>.png -- the way back to separate files.
bool f_ExtractImages(const Project& l_oP, const std::string& l_strDir, const std::string& l_strVariant, bool l_bChildren,
                   std::vector<std::string>* l_pWritten = nullptr, std::string* l_pErr = nullptr);

}  // namespace atlas
