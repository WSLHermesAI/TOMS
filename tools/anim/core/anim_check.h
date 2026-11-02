// anim_check.h -- problems in an .anim file: what the editor's Problems dock lists and
// `anim_editor --headless check` prints (no Qt).
#pragma once
#include "anim_clip.h"
#include "atlas_file.h"
#include "sprite_mesh.h"

#include <string>
#include <vector>

namespace animed {

struct Problem {
    enum Level { Error, Warning, Info };
    Level m_eLevel = Warning;
    int m_iClip = -1;                 // -1 = the file
    std::vector<int> m_vPath;         // node path in the clip (empty = root or n/a)
    std::string m_strWhere;             // "clip/node/child"
    std::string m_strMessage;
};

// One of the file's atlases, in lookup order (AnimFile::atlases, anim_player.h AtlasSet).
struct CheckAtlas {
    std::string m_strId;                          // what "id:name" sprite references name it by
    std::string m_strPath;                        // as shown in messages (the file's relative path)
    const toms::AtlasFile* m_pAtlas = nullptr;  // nullptr: it could not be loaded
    std::string m_strError;                       // why not (optional)
    bool m_bAbsolute = false;                   // the file names it with an absolute path
    const toms::SpriteMeshFile* m_pMeshes = nullptr;   // <atlas>.mesh.json beside it; nullptr: it has none
};

// Problems of the file against its atlases: an error per atlas that could not be loaded; per sprite
// reference (anim_clip.h SpriteRef) -- "id:name": an error when no atlas has that id or that atlas
// lacks the name; a bare "name": an error when it is in no atlas (checked when at least one atlas
// loaded), a warning when it is in more than one (the first wins; storing the atlas fixes it); a
// warning per absolute atlas path; an error per skinned node's sprite that has no mesh in its atlas's mesh file (it
// would be drawn as a flat quad, unbent; docs/engine/16_MESH_ANIMATION.md).
std::vector<Problem> f_CheckAnim(const toms::anim::AnimFile& l_oF, const std::vector<CheckAtlas>& l_vAtlases);

const char* f_LevelName(Problem::Level l_eL);
int f_CountLevel(const std::vector<Problem>& l_vP, Problem::Level l_eL);

// Reads a whole file (UTF-8 path); false when it cannot be opened.
bool f_ReadTextFile(const std::string& l_strPath, std::string& l_strOut);
// A usable atlas id: not empty; no ':' (it separates the id from the sprite name in a reference),
// '/', '\', '=' or white space.
bool f_IsAtlasId(const std::string& l_strId);
// True for a path that is not relative ("C:/x", "/x", "//server/x", "C:x").
bool f_IsAbsoluteAtlasPath(const std::string& l_strPath);

// `anim_editor --headless meshes ...`: anim_meshes.h headlessMeshes.
// `anim_editor --headless check <file.anim> [--atlas [<id>=]<file.atlas>]...`: prints the problems,
// returns 0 (no errors), 2 (errors or the file did not parse) or 3 (usage). --atlas (repeatable)
// replaces the file's atlas list; its id is the file name without ".atlas" unless given.
int f_HeadlessMain(const std::vector<std::string>& l_vArgs);

}  // namespace animed
