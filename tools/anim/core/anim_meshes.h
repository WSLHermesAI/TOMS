// anim_meshes.h -- the sprite meshes clips bend (docs/34_MESH_ANIMATION.md), kept per atlas: `game.atlas` ->
// `game.mesh.json` (+ `game.mesh.bin`) beside it, keyed by sprite and grid (anim_player.h meshKey), so every clip using
// the atlas shares them. The anim tools add what their clips need -- every skinned or deformed node's sprite on that
// node's mesh grid, cut from the atlas page's alpha -- when a clip is saved in the editor or by
// `anim_editor --headless meshes`. The atlas tool never touches them. No Qt.
#pragma once
#include "anim_player.h"
#include "sprite_mesh.h"

#include <functional>
#include <string>
#include <vector>

namespace animed {

// The pixels of an atlas page: AtlasSet entry, page -> RGBA8 rows `stride` pixels apart (false: not loaded).
using PagePixels = std::function<bool(int entry, int page, const uint8_t*& rgba, int& w, int& h, int& stride)>;

struct MeshReport {
    int made = 0;                     // cut from alpha
    int kept = 0;                     // "hand" meshes, kept as they are
    std::vector<std::string> skipped; // "sprite: why" (a rotated or child region, no pixels, an empty sprite)
};

// Adds every mesh the file's clips need to its atlas's mesh file in `perEntry` (one per AtlasSet entry; resized to fit):
// cut from the sprite's alpha, or left as it is when the file has it marked "hand". The other meshes in the files
// (other clips') stay. `needed` (optional) gets "entry|key" of each.
void addAnimMeshes(const toms::anim::AnimFile& file, const toms::anim::AtlasSet& atlases, const PagePixels& pixels,
                   std::vector<toms::SpriteMeshFile>& perEntry, MeshReport* report = nullptr,
                   std::vector<std::pair<int, std::string>>* needed = nullptr);

// Reads a mesh file (and the .mesh.bin it names). No file: true, `out` empty, *binary false. `binary` (optional): the
// file keeps its numbers in a .mesh.bin.
bool readMeshFile(const std::string& meshPath, toms::SpriteMeshFile& out, bool* binary, std::string* err);
// Writes a mesh file: binary = JSON header + <name>.mesh.bin, else all in the JSON. An empty file removes the files
// instead. Unchanged files are left alone. `written` (optional) gets the files written or removed.
bool writeMeshFile(const std::string& meshPath, const toms::SpriteMeshFile& f, bool binary, std::string* err,
                   std::vector<std::string>* written = nullptr);
// "dir/game.mesh.json" -> "dir/game.mesh.bin".
std::string meshBinPath(const std::string& meshPath);

// `anim_editor --headless meshes <file.anim>... [--atlas [<id>=]<file.atlas>]... [--default-atlas [<id>=]<file.atlas>]
//  [--map <from.atlas>=<to.atlas>]... [--bin|--text] [--check] [--prune]`:
// adds the meshes the clip files need to their atlases' mesh files. --atlas replaces every file's atlas list;
// --default-atlas is the list of a file that names none (the game draws those with its sprite atlas); --map swaps one
// atlas for another wherever a file uses it (an art style's copy: its own pixels, its own mesh file). The mesh file
// keeps its form (a new one binary) unless --bin / --text. --prune: each mesh file gets exactly what the given clip
// files need (meshes no one of them uses go). --check writes nothing: exit 1 when
// a mesh file lacks a needed mesh, has a stale one, or (with --prune) has others. Exit 0 / 1 / 2 (a file could not be
// read) / 3 (usage).
int headlessMeshes(const std::vector<std::string>& args);

}  // namespace animed
