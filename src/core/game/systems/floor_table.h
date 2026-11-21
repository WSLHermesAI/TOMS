// floor_table.h — the 70-floor tower as ordered data (S3.5 step a/b).
//
// Until now the run only knew the eleven hand-authored data/stages/*.json files: the Stage Select
// scanned that directory and stairs followed the `down` field among those eleven, so the seventy
// generated floors (data/story/floors/F01..F70, S2) were never named by any code path and never
// loaded in play.
//
// This unit turns the per-floor SPECS into the ordered floor list the rest of the game uses: seq
// 1..70, which act a floor belongs to, which floor comes next (the specs' `nextFloor` chain), and
// for the ten act-boss floors which hand-authored map stands in for the generated grid. It is pure
// data logic -- no Renderer, no Stage, no Game -- so floor_table_test.cpp can hold the shipped data
// to the contract headlessly. See docs/project/progress_report/PROGRESS_REPORT.md "S3.5" and docs/story/STORY_DATA_SCHEMA.md section 4.
#pragma once

#include <string>
#include <vector>

#include <json.hpp>   // vendored single header (external/json), the repo-wide include form
#include "stair_links.h"   // StairLink: a floor's stairs list (Q7)

namespace toms {

struct FloorInfo {
    std::string m_strId;                  // "F07"  -- also the stage id the run carries while here
    std::string m_strAct;                 // "ch_01" (the raw chapter-file id)
    std::string m_strActKey;              // "ch01" -- the act's i18n key stem (story.ch01.title)
    int m_iActIndex = 1;                // 1..10, parsed from `act` (ch_07 -> 7)
    int m_iIndexInAct = 1;              // 1..7, the spec's own field
    int m_iSeq = 1;                     // 1..70: position in the tower, what the HUD counter shows
    std::string m_strRole;                // "normal" | "boss-stage" (STORY_DATA_SCHEMA section 4)
    std::string m_strSeal;                // 印記 this act is about (memory/qi/daoji/...)
    std::string m_strNameKey;             // i18n key, e.g. "story.f07.name" (may be a literal string)
    std::string m_strNextFloor;           // "" on the last floor: the tower ends there
    std::string m_strHandAuthoredStage;   // "stage01.json" on boss floors, "" otherwise
    int m_iEventCount = 0;              // how many event slots the floor carries (used by the validator)
    // S3.5 (c): the floor's story keys, straight from the spec's `story` object, so the HUD can show
    // the floor's own line instead of the grid's placeholder note.
    std::string m_strIntroKey;                        // e.g. "story.f07.intro"
    std::vector<std::string> m_vAmbientKeys;        // "story.f07.ambient.1|2" -- rotated as the player moves
    std::vector<std::string> m_vEventIds;           // the floor's event slots, in spec order
    // The spec's `stairs` list (stair_links.h; docs/gameplay/event_editor/05_FLOOR_LINKS.md). hasStairs false: the floor
    // keeps the chain (up to nextFloor, down to the floor before it).
    std::vector<StairLink> m_vStairs;
    bool m_bHasStairs = false;

    bool f_IsBoss() const { return m_strRole == "boss-stage"; }
};

class FloorTable {
public:
    // Parse the specs (not their *.stage.json grids -- those are the maps, these are the tower).
    static FloorTable f_FromSpecs(const std::vector<nlohmann::json>& l_vSpecs);

    // Read every F*.json under `dir` (skipping F*.stage.json) and order them by the chain.
    static FloorTable f_LoadFromDirectory(const std::string& l_strDir);

    bool f_Empty() const { return m_vFloors.empty(); }
    int size() const { return (int)m_vFloors.size(); }

    // 1-based, like the counter: at(1) is the first floor. Out of range -> the last one / a throw-free
    // fallback, because every caller here is a draw or input path that must not abort mid-frame.
    const FloorInfo& f_At(int l_iSeq) const;
    const FloorInfo* f_Find(const std::string& l_strId) const;
    FloorInfo* f_Edit(const std::string& l_strId);        // test hooks (toms_game --stair-lock=...)

    // Progression. next() is the spec's own `nextFloor` ("" at the top); prev() is derived from the
    // chain, so a spec can never disagree with the floor the player actually came from.
    std::string f_Next(const std::string& l_strId) const;
    std::string f_Prev(const std::string& l_strId) const;
    int f_SeqOf(const std::string& l_strId) const;        // 0 when unknown

    // Where a floor's map lives, relative to the data root: boss floors use their hand-authored
    // map, the rest the generated grid. Empty when neither file is expected to exist.
    std::string f_MapRelPath(const std::string& l_strId) const;

    const std::vector<FloorInfo>& f_All() const { return m_vFloors; }

private:
    // Sorted by seq (the chain order), which is also F01..F70 today; the chain -- not the file
    // names -- decides, so a future act merge/reorder only touches the data.
    std::vector<FloorInfo> m_vFloors;
};

// S3.5 (c): which of a floor's lines to show. The intro holds for a while, then the ambient lines
// rotate -- the same "nothing looks frozen" rule the title screen follows.
// turns < 0 -> 0 (the intro); otherwise 0 for the first kIntroTurns, then 1..n cycling.
inline int f_StoryLineIndex(int l_iTurns, int l_iAmbientCount, int l_iIntroTurns = 8) {
    if (l_iTurns < 0) l_iTurns = 0;
    if (l_iAmbientCount <= 0 || l_iTurns < l_iIntroTurns) return 0;
    return 1 + ((l_iTurns - l_iIntroTurns) / 8) % l_iAmbientCount;
}

} // namespace toms
