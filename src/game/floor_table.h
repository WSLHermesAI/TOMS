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
    std::string act;                 // "ch_01" (the raw chapter-file id)
    std::string actKey;              // "ch01" -- the act's i18n key stem (story.ch01.title)
    int actIndex = 1;                // 1..10, parsed from `act` (ch_07 -> 7)
    int indexInAct = 1;              // 1..7, the spec's own field
    int seq = 1;                     // 1..70: position in the tower, what the HUD counter shows
    std::string role;                // "normal" | "boss-stage" (STORY_DATA_SCHEMA section 4)
    std::string seal;                // 印記 this act is about (memory/qi/daoji/...)
    std::string nameKey;             // i18n key, e.g. "story.f07.name" (may be a literal string)
    std::string nextFloor;           // "" on the last floor: the tower ends there
    std::string handAuthoredStage;   // "stage01.json" on boss floors, "" otherwise
    int eventCount = 0;              // how many event slots the floor carries (used by the validator)
    // S3.5 (c): the floor's story keys, straight from the spec's `story` object, so the HUD can show
    // the floor's own line instead of the grid's placeholder note.
    std::string introKey;                        // e.g. "story.f07.intro"
    std::vector<std::string> ambientKeys;        // "story.f07.ambient.1|2" -- rotated as the player moves
    std::vector<std::string> eventIds;           // the floor's event slots, in spec order

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

// S3.5 (e): per-act palette. Every act renders with the same atlas, so without this the tower has no
// visual progression at all (owner: "all stages looks same"). Pure data -> RGB: the maze's wall/floor
// tiles are tinted by it, which is visible immediately and needs no new art (S8 owns real tilesets).
// Values are deliberately mid-bright so sprites, HUD text and the pad stay readable on top.
inline void floorThemeTint(int actIndex, float out[4]) {
    switch (actIndex) {
        case 1:  out[0]=1.00f; out[1]=0.94f; out[2]=0.82f; break;  // village  -- warm daylight
        case 2:  out[0]=0.88f; out[1]=1.00f; out[2]=0.86f; break;  // forest   -- green shade
        case 3:  out[0]=0.86f; out[1]=0.92f; out[2]=1.00f; break;  // gate     -- steel blue
        case 4:  out[0]=1.00f; out[1]=0.92f; out[2]=0.78f; break;  // market   -- amber lamp
        case 5:  out[0]=0.86f; out[1]=0.86f; out[2]=1.00f; break;  // barracks -- cold indigo
        case 6:  out[0]=0.78f; out[1]=0.95f; out[2]=0.98f; break;  // cavern   -- wet stone
        case 7:  out[0]=0.94f; out[1]=0.85f; out[2]=1.00f; break;  // library  -- violet dust
        case 8:  out[0]=0.88f; out[1]=0.98f; out[2]=0.94f; break;  // crypt    -- sickly green
        case 9:  out[0]=1.00f; out[1]=0.88f; out[2]=0.88f; break;  // sanctum  -- altar red
        default: out[0]=0.95f; out[1]=0.95f; out[2]=1.00f; break;  // peak     -- thin air, pale
    }
    out[3] = 1.0f;
}

// S3.5 (c): which of a floor's lines to show. The intro holds for a while, then the ambient lines
// rotate -- the same "nothing looks frozen" rule the title screen follows.
// turns < 0 -> 0 (the intro); otherwise 0 for the first kIntroTurns, then 1..n cycling.
inline int storyLineIndex(int turns, int ambientCount, int introTurns = 8) {
    if (turns < 0) turns = 0;
    if (ambientCount <= 0 || turns < introTurns) return 0;
    return 1 + ((turns - introTurns) / 8) % ambientCount;
}

} // namespace toms
