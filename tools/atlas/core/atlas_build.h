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
    enum Level { Info, Warning, Error } m_eLevel = Warning;
    std::string m_strSprite;     // may be empty
    std::string m_strMessage;
};

// One named rectangle in the atlas. Coordinates are pixels, origin top-left, y down.
struct Region {
    std::string m_strName;
    int m_iPage = 0;
    IRect m_oFrame;             // where the pixels are in the page
    int m_iOffsetX = 0, m_iOffsetY = 0;   // where frame's top-left sits inside the original image
    int m_iOrigW = 0, m_iOrigH = 0;       // original (untrimmed) size
    float m_arrPivot[2] = {0.5f, 0.5f};
    bool m_bHasSplit = false;
    int m_arrSplit[4] = {0, 0, 0, 0};    // left, right, top, bottom
    std::vector<std::string> m_vTags;
    // Child sprite (no pixels of its own): parent + rect in the parent's original pixels.
    bool m_bChild = false;
    std::string m_strParent;
    IRect m_oLocal;
    bool m_bBaked = false;      // child with its own copy of the pixels (packed separately)
    std::string m_strAliasOf;     // dedupe: same pixels as this region (frame is shared)
    std::string m_strSourceFile;  // image sprites: the PNG it came from

    bool f_Trimmed() const { return m_iOffsetX != 0 || m_iOffsetY != 0 || m_oFrame.m_iW != m_iOrigW || m_oFrame.m_iH != m_iOrigH; }
};

struct Page {
    std::string m_strFile;        // image file name, e.g. "game.png", "game_1.png"
    Image m_oImage;
};

struct BuildResult {
    std::string m_strName;        // output base name
    Settings m_oSettings;
    std::vector<Page> m_vPages;
    std::vector<Region> m_vRegions;          // sorted by name
    std::vector<Animation> m_vAnimations;
    std::vector<Diagnostic> m_vDiagnostics;

    bool f_Ok() const;
    const Region* f_Find(const std::string& l_strName) const;
    int f_ErrorCount() const;
    int f_WarningCount() const;
};

// Where images come from. The default reads files; the editor caches decoded images, the web
// build reads from its in-memory file system.
struct ImageSource {
    std::function<bool(const std::string& l_strPath, Image& l_oOut, std::string* l_pErr)> m_fnLoad;
    std::function<bool(const std::string& l_strPath)> m_fnExists;
    // PNG paths below dir (absolute or project-resolved), '/' separators, sorted.
    std::function<std::vector<std::string>(const std::string& l_strDir, bool l_bRecursive)> m_fnListPngs;
};
ImageSource f_FileImageSource();
// Same as fileImageSource() but keeps decoded images and reuses them while the file's size and
// modification time are unchanged (the editor rebuilds after every edit).
ImageSource f_CachedImageSource(std::shared_ptr<void>& l_spCacheHolder);

// A sprite as the build sees it after merging scanned folders with the explicit entries.
struct ResolvedSprite {
    SpriteDef m_oDef;           // merged settings
    std::string m_strFile;        // resolved image path (folder projects; empty for a child)
    ImagePtr m_spImage;          // embedded projects: the pixels
    bool m_bScanned = false;    // came from a source folder or the embedded images (named by them)
};
std::vector<ResolvedSprite> f_ResolveSprites(const Project& l_oP, const ImageSource& l_oSrc,
                                           std::vector<Diagnostic>* l_pDiags = nullptr);

struct BuildOptions {
    std::string m_strVariant;     // empty = the base art
    bool m_bComposite = true;   // false: layout only (no page pixels), for fast previews
};

BuildResult f_Build(const Project& l_oP, const ImageSource& l_oSrc, const BuildOptions& l_oOpt = {});
inline BuildResult f_Build(const Project& l_oP, const BuildOptions& l_oOpt = {}) { return f_Build(l_oP, f_FileImageSource(), l_oOpt); }

std::string f_FormatDiagnostic(const Diagnostic& l_oD);

}  // namespace atlas
