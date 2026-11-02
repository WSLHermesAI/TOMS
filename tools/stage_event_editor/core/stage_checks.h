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
    Sev m_eSev = Sev::Warn;
    std::string m_strCode;                  // "pNoStart", "sNeedKeys", ...: the HTML editor's I18N keys
    std::vector<std::string> m_vArgs;
    std::vector<Cell> m_vCells;
    std::string m_strFixList;               // pEvNotListed: the event id "Add to list" adds
    std::string m_strBrush;                 // pEvUnplaced: the brush a click picks
};

std::optional<Cell> f_StartCell(const Project& l_oP, const StageDoc& l_oD);
// cells reachable from the start: walls block; doors, monsters and everything else can be passed in the end
// (empty when there is no start)
std::vector<char> f_ReachableCells(const Project& l_oP, const StageDoc& l_oD);
std::vector<Issue> f_Problems(const Project& l_oP, const StageDoc& l_oD);

// ---- story: keys and doors, floor by floor ----
enum KeyColor { Yellow, Blue, Red };
extern const char* const g_arrKeyColors[3];
struct Keys {
    int m_arrN[3] = {0, 0, 0};
    int f_Total() const { return m_arrN[0] + m_arrN[1] + m_arrN[2]; }
};
struct FloorFacts {
    int m_iW = 0, m_iH = 0;
    struct Door { int m_iX, m_iY, m_iCol; };
    std::vector<Door> m_vDoors;
    std::vector<int> m_vKeyAt;            // per cell: -1, or the key colour lying there
    std::vector<int> m_vUps;              // cells of 'U'
    std::vector<int> m_vDoorIdx;          // per cell: -1, or the door's index
    std::optional<Cell> m_optStart;
};
struct FloodState {
    uint32_t m_uMask = 0;                 // the doors opened
    std::vector<int> m_vSeenOrder;        // cells reached, in the order they were reached
    std::vector<char> m_vSeen;
    Keys m_oFound, m_oOpened, m_oLeft;
    std::vector<int> m_vFrontier;         // doors next to the reached cells, still closed
    bool m_bUp = false;
    int m_iN = 0;                         // doors opened
};
struct Explore {
    FloorFacts m_oF;
    std::vector<FloodState> m_vAll;       // every reachable door set, in the order found
    int m_iRoute = -1, m_iCarry = -1, m_iWidest = -1;   // fewest doors up; most keys left up; most cells
    std::vector<char> m_vAnySeen;
    bool f_HasUp() const;
};
Explore f_Explore(const Project& l_oP, const StageDoc& l_oD, const Keys& l_oKeysIn);

struct StoryRow {
    std::string m_strFid;
    int m_iIdx = 0;
    Keys m_oKeysIn;
    Explore m_oEx;
    std::vector<Issue> m_vIssues;
    bool m_bTop = false;                  // no stairs up and no connect.up
};
std::vector<std::string> f_ChainOrder(Project& l_oP);
// Cached until Project::storyVer changes.
const std::vector<StoryRow>& f_StoryCheck(Project& l_oP);

std::vector<Cell> f_SafeCells(Project& l_oP, const std::string& l_strFid);
bool f_IsStepEvent(const Project& l_oP, const std::string& l_strId);

// Seeded random numbers as the HTML editor's (mulberry32 over an FNV-1a hash of the seed text).
std::function<double()> f_Mulberry(uint32_t l_uSeed);
uint32_t f_HashStr(const std::string& l_strS);
std::optional<Cell> f_PickCell(const Project& l_oP, const StageDoc& l_oD, const std::vector<Cell>& l_vCands, const std::function<double()>& l_fnRng);

struct AutoPlaceResult {
    int m_iPlaced = 0, m_iFloors = 0;
    std::vector<Issue> m_vSkipped;        // apSkipNoCell (id, floor), apSkipUndef (n)
};
// mode "missing": only listed events not placed yet; "all": every listed event moves to a new cell.
// Each floor that changes gets one undo step (label from labelFor(n)).
AutoPlaceResult f_AutoPlace(Project& l_oP, const std::vector<std::string>& l_vFids, const std::string& l_strMode, const std::string& l_strSeed,
                          const std::function<std::string(int)>& l_fnLabelFor);

std::string f_NumText(const Json& l_jV);   // a number as the HTML editor prints it (3, 2.5)

}  // namespace stage
