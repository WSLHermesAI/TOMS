// anim_check.h -- problems in an .anim file: what the editor's Problems dock lists and
// `anim_editor --headless check` prints (no Qt).
#pragma once
#include "anim_clip.h"
#include "atlas_file.h"

#include <string>
#include <vector>

namespace animed {

struct Problem {
    enum Level { Error, Warning, Info };
    Level level = Warning;
    int clip = -1;                 // -1 = the file
    std::vector<int> path;         // node path in the clip (empty = root or n/a)
    std::string where;             // "clip/node/child"
    std::string message;
};

// One of the file's atlases, in lookup order (AnimFile::atlases, anim_player.h AtlasSet).
struct CheckAtlas {
    std::string id;                          // what "id:name" sprite references name it by
    std::string path;                        // as shown in messages (the file's relative path)
    const toms::AtlasFile* atlas = nullptr;  // nullptr: it could not be loaded
    std::string error;                       // why not (optional)
    bool absolute = false;                   // the file names it with an absolute path
};

// Problems of the file against its atlases: an error per atlas that could not be loaded; per sprite
// reference (anim_clip.h SpriteRef) -- "id:name": an error when no atlas has that id or that atlas
// lacks the name; a bare "name": an error when it is in no atlas (checked when at least one atlas
// loaded), a warning when it is in more than one (the first wins; storing the atlas fixes it); a
// warning per absolute atlas path.
std::vector<Problem> checkAnim(const toms::anim::AnimFile& f, const std::vector<CheckAtlas>& atlases);

const char* levelName(Problem::Level l);
int countLevel(const std::vector<Problem>& p, Problem::Level l);

// Reads a whole file (UTF-8 path); false when it cannot be opened.
bool readTextFile(const std::string& path, std::string& out);
// A usable atlas id: not empty; no ':' (it separates the id from the sprite name in a reference),
// '/', '\', '=' or white space.
bool isAtlasId(const std::string& id);
// True for a path that is not relative ("C:/x", "/x", "//server/x", "C:x").
bool isAbsoluteAtlasPath(const std::string& path);

// `anim_editor --headless check <file.anim> [--atlas [<id>=]<file.atlas>]...`: prints the problems,
// returns 0 (no errors), 2 (errors or the file did not parse) or 3 (usage). --atlas (repeatable)
// replaces the file's atlas list; its id is the file name without ".atlas" unless given.
int headlessMain(const std::vector<std::string>& args);

}  // namespace animed
