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
// to the contract headlessly. See docs/PROGRESS_REPORT.md "S3.5" and STORY_DATA_SCHEMA.md section 4.
#pragma once

#include <string>
#include <vector>

#include <json.hpp>   // vendored single header (external/json), the repo-wide include form

namespace toms {

struct FloorInfo {
    std::string id;                  // "F07"  -- also the stage id the run carries while here
    std::string act;                 // "ch_01"
    int actIndex = 1;                // 1..10, parsed from `act` (ch_07 -> 7)
    int indexInAct = 1;              // 1..7, the spec's own field
    int seq = 1;                     // 1..70: position in the tower, what the HUD counter shows
    std::string role;                // "normal" | "boss-stage" (STORY_DATA_SCHEMA section 4)
    std::string seal;                // 印記 this act is about (memory/qi/daoji/...)
    std::string nameKey;             // i18n key, e.g. "story.f07.name" (may be a literal string)
    std::string nextFloor;           // "" on the last floor: the tower ends there
    std::string handAuthoredStage;   // "stage01.json" on boss floors, "" otherwise
    int eventCount = 0;              // how many event slots the floor carries (used by the validator)

    bool isBoss() const { return role == "boss-stage"; }
};

class FloorTable {
public:
    // Parse the specs (not their *.stage.json grids -- those are the maps, these are the tower).
    static FloorTable fromSpecs(const std::vector<nlohmann::json>& specs);

    // Read every F*.json under `dir` (skipping F*.stage.json) and order them by the chain.
    static FloorTable loadFromDirectory(const std::string& dir);

    bool empty() const { return floors_.empty(); }
    int size() const { return (int)floors_.size(); }

    // 1-based, like the counter: at(1) is the first floor. Out of range -> the last one / a throw-free
    // fallback, because every caller here is a draw or input path that must not abort mid-frame.
    const FloorInfo& at(int seq) const;
    const FloorInfo* find(const std::string& id) const;

    // Progression. next() is the spec's own `nextFloor` ("" at the top); prev() is derived from the
    // chain, so a spec can never disagree with the floor the player actually came from.
    std::string next(const std::string& id) const;
    std::string prev(const std::string& id) const;
    int seqOf(const std::string& id) const;        // 0 when unknown

    // Where a floor's map lives, relative to the data root: boss floors use their hand-authored
    // map, the rest the generated grid. Empty when neither file is expected to exist.
    std::string mapRelPath(const std::string& id) const;

    const std::vector<FloorInfo>& all() const { return floors_; }

private:
    // Sorted by seq (the chain order), which is also F01..F70 today; the chain -- not the file
    // names -- decides, so a future act merge/reorder only touches the data.
    std::vector<FloorInfo> floors_;
};

} // namespace toms
