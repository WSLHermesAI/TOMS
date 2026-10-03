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
// Import: .pi (FM79979), .atlas (both dialects), TexturePacker .json (hash or array).
#pragma once
#include "atlas_build.h"

#include <string>
#include <vector>

namespace atlas {

struct ExportFile {
    std::string name;            // file name (no folder)
    std::vector<uint8_t> data;
};

const std::vector<std::string>& exportFormats();
bool isExportFormat(const std::string& f);

// All files for `formats`, page PNGs included.
bool exportFiles(const BuildResult& b, const std::vector<std::string>& formats, std::vector<ExportFile>& out,
                 std::string* err = nullptr);
// Files whose bytes did not change are left alone (their timestamps too); `changed` gets the names
// of the ones written.
bool writeFiles(const std::string& dir, const std::vector<ExportFile>& files, std::string* err = nullptr,
                std::vector<std::string>* changed = nullptr);

std::string exportLibgdx(const BuildResult& b);
std::string exportSpine3(const BuildResult& b);
std::string exportPlist(const BuildResult& b, int page);
std::string exportTpJson(const BuildResult& b, int page);
std::string exportPi(const BuildResult& b, int page);
std::string exportRcss(const BuildResult& b);
std::string pageBaseName(const BuildResult& b, int page);   // "game", "game_1", ...

// Readers. The result holds the pages (decoded images) and regions; settings are left default.
bool importAtlasFile(const std::string& path, BuildResult& out, std::string* err = nullptr);   // by extension
bool importPi(const std::string& path, BuildResult& out, std::string* err = nullptr);
bool importLibgdx(const std::string& path, BuildResult& out, std::string* err = nullptr);
bool importTpJson(const std::string& path, BuildResult& out, std::string* err = nullptr);

// Turns an imported atlas into a project: every image region is cut out at its original size
// (trimmed pixels put back at their offset); child regions become child sprites. With an empty
// imageDir the images stay in memory in an EMBEDDED project (output next to the project file; save
// it with saveProjectAll); otherwise they are written to imageDir as <name>.png for a FOLDER
// project. The project file is not written; its filePath is set to projectPath.
bool importToProject(const BuildResult& in, const std::string& projectPath, const std::string& imageDir,
                     Project& out, std::string* err = nullptr);

// Renders one region at its original size (transparent where trimmed) -- what a game would draw.
Image renderRegion(const BuildResult& b, const Region& r);

}  // namespace atlas
