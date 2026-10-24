// stage_checks.h -- the stage editor's checks, without Qt (docs/gameplay/02_STAGE_EDITOR.md, sections 2 and 6).
//
//   problems()     one floor: unknown chars, the player start, events placed / listed / defined, stairs and their
//                  targets, objects the player cannot reach, big monsters' room, object settings
//   storyCheck()   every floor in play order (F01, then nextFloor): the keys carried in, found, spent on the doors of the
//                  way up and carried out; events walled off or behind a door off the way up; event logic in order
//                  (an event needing another done, a variable reachable, a stair locked by an event)
//   safeCells(), autoPlace()   where an event can go without ever costing the key the way up needs
//
// Ported from tools/web_editors/stage_editor.html with the same orders and the same seeded random numbers, so a seed
// places events on the same cells in both editors. Messages are codes with arguments (the HTML editor's string keys);
// the window turns them into text in the user's language.
#pragma once

#include "stage_project.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace stage {

enum class Sev { Error, Warn, Info };

struct Issue {
    Sev sev = Sev::Warn;
    std::string code;                  // "pNoStart", "sNeedKeys", ...: the HTML editor's I18N keys
    std::vector<std::string> args;
    std::vector<Cell> cells;
    std::string fixList;               // pEvNotListed: the event id "Add to list" adds
    std::string brush;                 // pEvUnplaced: the brush a click picks
};

std::optional<Cell> startCell(const Project& p, const StageDoc& d);
// cells reachable from the start: walls block; doors, monsters and everything else can be passed in the end
// (empty when there is no start)
std::vector<char> reachableCells(const Project& p, const StageDoc& d);
std::vector<Issue> problems(const Project& p, const StageDoc& d);

// ---- story: keys and doors, floor by floor ----
enum KeyColor { Yellow, Blue, Red };
extern const char* const kKeyColors[3];
struct Keys {
    int n[3] = {0, 0, 0};
    int total() const { return n[0] + n[1] + n[2]; }
};
struct FloorFacts {
    int W = 0, H = 0;
    struct Door { int x, y, col; };
    std::vector<Door> doors;
    std::vector<int> keyAt;            // per cell: -1, or the key colour lying there
    std::vector<int> ups;              // cells of 'U'
    std::vector<int> doorIdx;          // per cell: -1, or the door's index
    std::optional<Cell> start;
};
struct FloodState {
    uint32_t mask = 0;                 // the doors opened
    std::vector<int> seenOrder;        // cells reached, in the order they were reached
    std::vector<char> seen;
    Keys found, opened, left;
    std::vector<int> frontier;         // doors next to the reached cells, still closed
    bool up = false;
    int n = 0;                         // doors opened
};
struct Explore {
    FloorFacts F;
    std::vector<FloodState> all;       // every reachable door set, in the order found
    int route = -1, carry = -1, widest = -1;   // fewest doors up; most keys left up; most cells
    std::vector<char> anySeen;
    bool hasUp() const;
};
Explore explore(const Project& p, const StageDoc& d, const Keys& keysIn);

struct StoryRow {
    std::string fid;
    int idx = 0;
    Keys keysIn;
    Explore ex;
    std::vector<Issue> issues;
    bool top = false;                  // no stairs up and no connect.up
};
std::vector<std::string> chainOrder(Project& p);
// Cached until Project::storyVer changes.
const std::vector<StoryRow>& storyCheck(Project& p);

std::vector<Cell> safeCells(Project& p, const std::string& fid);
bool isStepEvent(const Project& p, const std::string& id);

// Seeded random numbers as the HTML editor's (mulberry32 over an FNV-1a hash of the seed text).
std::function<double()> mulberry(uint32_t seed);
uint32_t hashStr(const std::string& s);
std::optional<Cell> pickCell(const Project& p, const StageDoc& d, const std::vector<Cell>& cands, const std::function<double()>& rng);

struct AutoPlaceResult {
    int placed = 0, floors = 0;
    std::vector<Issue> skipped;        // apSkipNoCell (id, floor), apSkipUndef (n)
};
// mode "missing": only listed events not placed yet; "all": every listed event moves to a new cell.
// Each floor that changes gets one undo step (label from labelFor(n)).
AutoPlaceResult autoPlace(Project& p, const std::vector<std::string>& fids, const std::string& mode, const std::string& seed,
                          const std::function<std::string(int)>& labelFor);

std::string numText(const Json& v);   // a number as the HTML editor prints it (3, 2.5)

}  // namespace stage
