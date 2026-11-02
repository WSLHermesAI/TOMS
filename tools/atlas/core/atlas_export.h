// atlas_export.h -- writers for the built atlas, and readers to bring existing atlases in.
//
// Export formats (Output::formats):
//   atlas          libGDX / Spine 4 text atlas + x_ extension fields (TOMS, libGDX, Spine 4, Godot)
//   atlas-spine3   the strict old Spine 3 layout, no extensions (old runtimes read fixed lines)
//   plist          Cocos (TexturePacker plist format 3), one per page -- Cocos Creator SpriteAtlas
//   tp-json        TexturePacker JSON hash, one per page (Phaser, Pixi); extensions under "x"
//   pi             FM79979 PuzzleImage XML, one per page; children as PuzzleUnit + PuzzleUnitChild
//   rcss           RmlUi @spritesheet, one block per page
// Page images are always written as PNG.
//
// Import: .pi (FM79979), .atlas (both dialects), TexturePacker .json (hash or array), Cocos / TexturePacker
// .plist (formats 0-3: rotated frames are turned back upright, trims and aliases kept, our x_ fields read).
#pragma once
#include "atlas_build.h"

#include <string>
#include <vector>

namespace atlas {

struct ExportFile {
    std::string m_strName;            // file name (no folder)
    std::vector<uint8_t> m_vData;
};

const std::vector<std::string>& f_ExportFormats();
bool f_IsExportFormat(const std::string& l_strF);

// All files for `formats`, page PNGs included.
bool f_ExportFiles(const BuildResult& l_oB, const std::vector<std::string>& l_vFormats, std::vector<ExportFile>& l_vOut,
                 std::string* l_pErr = nullptr);
// Files whose bytes did not change are left alone (their timestamps too); `changed` gets the names
// of the ones written.
bool f_WriteFiles(const std::string& l_strDir, const std::vector<ExportFile>& l_vFiles, std::string* l_pErr = nullptr,
                std::vector<std::string>* l_pChanged = nullptr);

std::string f_ExportLibgdx(const BuildResult& l_oB);
std::string f_ExportSpine3(const BuildResult& l_oB);
std::string f_ExportPlist(const BuildResult& l_oB, int l_iPage);
std::string f_ExportTpJson(const BuildResult& l_oB, int l_iPage);
std::string f_ExportPi(const BuildResult& l_oB, int l_iPage);
std::string f_ExportRcss(const BuildResult& l_oB);
std::string f_PageBaseName(const BuildResult& l_oB, int l_iPage);   // "game", "game_1", ...

// Readers. The result holds the pages (decoded images) and regions; settings are left default.
bool f_ImportAtlasFile(const std::string& l_strPath, BuildResult& l_oOut, std::string* l_pErr = nullptr);   // by extension
bool f_ImportPi(const std::string& l_strPath, BuildResult& l_oOut, std::string* l_pErr = nullptr);
bool f_ImportLibgdx(const std::string& l_strPath, BuildResult& l_oOut, std::string* l_pErr = nullptr);
bool f_ImportTpJson(const std::string& l_strPath, BuildResult& l_oOut, std::string* l_pErr = nullptr);
bool f_ImportPlist(const std::string& l_strPath, BuildResult& l_oOut, std::string* l_pErr = nullptr);

// Turns an imported atlas into a project: every image region is cut out at its original size
// (trimmed pixels put back at their offset); child regions become child sprites. With an empty
// imageDir the images stay in memory in an EMBEDDED project (output next to the project file; save
// it with saveProjectAll); otherwise they are written to imageDir as <name>.png for a FOLDER
// project. The project file is not written; its filePath is set to projectPath.
bool f_ImportToProject(const BuildResult& l_oIn, const std::string& l_strProjectPath, const std::string& l_strImageDir,
                     Project& l_oOut, std::string* l_pErr = nullptr);

// Renders one region at its original size (transparent where trimmed) -- what a game would draw.
Image f_RenderRegion(const BuildResult& l_oB, const Region& l_oR);

}  // namespace atlas
