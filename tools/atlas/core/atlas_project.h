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
std::filesystem::path f_U8path(const std::string& l_strUtf8);
std::string f_U8str(const std::filesystem::path& l_oP);   // UTF-8 with '/' separators

// Images are shared and never changed in place, so copying a project (undo snapshots, background
// builds) copies pointers, not pixels.
using ImagePtr = std::shared_ptr<const Image>;

struct Settings {
    int m_iMaxWidth = 2048, m_iMaxHeight = 2048;
    int m_iMinWidth = 16, m_iMinHeight = 16;
    bool m_bPowerOfTwo = false;
    bool m_bSquare = false;
    bool m_bFixedSize = false;
    int m_iPadding = 2;            // pixels between sprites
    int m_iBorder = 0;             // pixels along the page edges
    int m_iExtrude = 1;            // edge pixels repeated around each sprite (stops filtering seams); 0..16
    bool m_bTrim = true;           // cut away transparent edges (the original size and offset are kept)
    int m_iAlphaThreshold = 0;     // alpha <= this counts as transparent when trimming
    bool m_bDedupe = true;         // identical images share one place in the atlas
    bool m_bPremultiplyAlpha = false;
    std::string m_strFilter = "Nearest";   // written to the atlas: Nearest or Linear
    Heuristic m_eHeuristic = Heuristic::Auto;
    float m_arrDefaultPivot[2] = {0.5f, 0.5f};
};

struct Output {
    std::string m_strDir = ".";              // relative to the project file
    std::string m_strName;                   // base file name; empty = project name
    // atlas (libGDX/Spine 4 + x_ extensions), atlas-spine3 (strict legacy), plist (Cocos),
    // tp-json (TexturePacker JSON hash), pi (FM79979 PuzzleImage), rcss (RmlUi @spritesheet)
    std::vector<std::string> m_vFormats = {"atlas"};
};

struct SourceFolder {
    std::string m_strPath;
    bool m_bRecursive = true;
    std::string m_strPrefix;                 // prepended to the names found here
};

struct SpriteDef {
    std::string m_strName;
    std::string m_strFile;                   // image sprite added by hand (relative path)
    std::string m_strParent;                 // child sprite: the parent's name
    IRect m_oRect;                         // child sprite: rect in the parent's original pixels
    bool m_bBake = false;                  // child sprite: copy its pixels and pack them separately
    bool m_bExclude = false;               // a scanned sprite left out of the atlas
    bool m_bHasPivot = false;
    float m_arrPivot[2] = {0.5f, 0.5f};      // 0..1 of the original size, from the top-left
    bool m_bHasSplit = false;
    int m_arrSplit[4] = {0, 0, 0, 0};        // 9-slice borders: left, right, top, bottom (pixels)
    int m_iTrim = -1;                      // -1 = project setting, 0 = off, 1 = on
    bool m_bPinned = false;                // fixed place in the atlas (top-left of the sprite)
    int m_iPinPage = 0, m_iPinX = 0, m_iPinY = 0;
    std::vector<std::string> m_vTags;

    bool f_IsChild() const { return !m_strParent.empty(); }
};

struct AnimFrame { std::string m_strSprite; float m_fTime = 0.1f; };
struct Animation {
    std::string m_strName;
    bool m_bLoop = true;
    std::vector<AnimFrame> m_vFrames;
};

struct Variant {
    std::string m_strId;
    std::string m_strOverrideDir;            // folder project: <overrideDir>/<sprite name>.png replaces that sprite
    std::string m_strReference;              // embedded project: a folder to import replacement art from
    Output m_oOutput;
    // Embedded project: replacement art by sprite name, stored in the variant's own packed atlas.
    // The JSON lists the names; openProject() fills in the pixels.
    std::map<std::string, ImagePtr> m_mapImages;
};

struct Project {
    int m_iVersion = 1;
    std::string m_strName = "atlas";
    Settings m_oSettings;
    Output m_oOutput;
    bool m_bEmbedded = false;                      // images live in the packed atlas (see the top)
    std::map<std::string, ImagePtr> m_mapImages;     // embedded images by sprite name (not in the JSON)
    std::vector<SourceFolder> m_vReferences;       // embedded: folders to import art from
    std::vector<SourceFolder> m_vSources;          // folder project: scanned at every build
    std::vector<SpriteDef> m_vSprites;
    std::vector<Animation> m_vAnimations;
    std::vector<Variant> m_vVariants;

    std::string m_strFilePath;               // where it was loaded from / saved to (not stored)

    std::string f_BaseDir() const;        // folder of filePath ("." when unsaved)
    std::string f_Resolve(const std::string& l_strRel) const;   // project-relative -> usable path
    std::string f_Relativize(const std::string& l_strPath) const;
    SpriteDef* f_FindSprite(const std::string& l_strName);
    const SpriteDef* f_FindSprite(const std::string& l_strName) const;
    const Variant* f_FindVariant(const std::string& l_strId) const;
    std::string f_OutputName(const Output& l_oO) const { return l_oO.m_strName.empty() ? m_strName : l_oO.m_strName; }
};

// The JSON only. An embedded project also needs its images: use openProject() and
// saveProjectAll() (atlas_store.h), which read and write the packed atlas too.
bool f_LoadProject(const std::string& l_strPath, Project& l_oOut, std::string* l_pErr = nullptr);
bool f_SaveProject(const std::string& l_strPath, Project& l_oP, std::string* l_pErr = nullptr);   // sets p.filePath
bool f_ProjectFromJson(const std::string& l_strText, Project& l_oOut, std::string* l_pErr = nullptr);
std::string f_ProjectToJson(const Project& l_oP);

// Path helpers ('/' separators everywhere).
std::string f_NormalizePath(std::string l_strP);
std::string f_JoinPath(const std::string& l_strA, const std::string& l_strB);
std::string f_ParentDir(const std::string& l_strP);
std::string f_FileStem(const std::string& l_strP);          // "a/b/c.png" -> "c"
std::string f_FileName(const std::string& l_strP);          // "a/b/c.png" -> "c.png"

}  // namespace atlas
