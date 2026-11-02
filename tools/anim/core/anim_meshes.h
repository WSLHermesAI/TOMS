// anim_meshes.h -- the sprite meshes clips bend (docs/engine/16_MESH_ANIMATION.md), kept per atlas: `game.atlas` ->
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
using PagePixels = std::function<bool(int l_iEntry, int l_iPage, const uint8_t*& l_pRgba, int& l_iW, int& l_iH, int& l_iStride)>;

struct MeshReport {
    int m_iMade = 0;                     // cut from alpha
    int m_iKept = 0;                     // "hand" meshes, kept as they are
    std::vector<std::string> m_vSkipped; // "sprite: why" (a rotated or child region, no pixels, an empty sprite)
};

// Adds every mesh the file's clips need to its atlas's mesh file in `perEntry` (one per AtlasSet entry; resized to fit):
// cut from the sprite's alpha, or left as it is when the file has it marked "hand". The other meshes in the files
// (other clips') stay. `needed` (optional) gets "entry|key" of each.
void f_AddAnimMeshes(const toms::anim::AnimFile& l_oFile, const toms::anim::AtlasSet& l_oAtlases, const PagePixels& l_fnPixels,
                   std::vector<toms::SpriteMeshFile>& l_vPerEntry, MeshReport* l_pReport = nullptr,
                   std::vector<std::pair<int, std::string>>* l_pNeeded = nullptr);

// Reads a mesh file (and the .mesh.bin it names). No file: true, `out` empty, *binary false. `binary` (optional): the
// file keeps its numbers in a .mesh.bin.
bool f_ReadMeshFile(const std::string& l_strMeshPath, toms::SpriteMeshFile& l_oOut, bool* l_pBinary, std::string* l_pErr);
// Writes a mesh file: binary = JSON header + <name>.mesh.bin, else all in the JSON. An empty file removes the files
// instead. Unchanged files are left alone. `written` (optional) gets the files written or removed.
bool f_WriteMeshFile(const std::string& l_strMeshPath, const toms::SpriteMeshFile& l_oF, bool l_bBinary, std::string* l_pErr,
                   std::vector<std::string>* l_pWritten = nullptr);
// "dir/game.mesh.json" -> "dir/game.mesh.bin".
std::string f_MeshBinPath(const std::string& l_strMeshPath);

// `anim_editor --headless meshes <file.anim>... [--atlas [<id>=]<file.atlas>]... [--default-atlas [<id>=]<file.atlas>]
//  [--map <from.atlas>=<to.atlas>]... [--bin|--text] [--check] [--prune]`:
// adds the meshes the clip files need to their atlases' mesh files. --atlas replaces every file's atlas list;
// --default-atlas is the list of a file that names none (the game draws those with its sprite atlas); --map swaps one
// atlas for another wherever a file uses it (an art style's copy: its own pixels, its own mesh file). The mesh file
// keeps its form (a new one binary) unless --bin / --text. --prune: each mesh file gets exactly what the given clip
// files need (meshes no one of them uses go). --check writes nothing: exit 1 when
// a mesh file lacks a needed mesh, has a stale one, or (with --prune) has others. Exit 0 / 1 / 2 (a file could not be
// read) / 3 (usage).
int f_HeadlessMeshes(const std::vector<std::string>& l_vArgs);

}  // namespace animed
