// ending_system.h — M7: the 15-ending resolver (data/story/endings.json), per
// docs/story/STORY_BIBLE.md §7 (the narrative source: 3 good, 11 bad/neutral, 1 hidden) and
// docs/story/STORY_DATA_SCHEMA.md §7 (the schema + the "first match wins, guaranteed fallback"
// judge algorithm).
//
// Deliberately built ON TOP OF the existing Condition Evaluator (src/engine/condition.h) rather
// than a second DSL: every ending's `requires` is the exact same condition tree shape dialogue
// `requires`/door-key gating already use, so this file contributes almost no new logic of its
// own -- just the schema (an ORDERED list + a fallback id) and the two ways the docs say it gets
// read (§7.2's full priority walk at F70; §7.3's much narrower per-trigger check elsewhere).
#pragma once
#include <json.hpp>
#include <string>
#include <vector>
#include "condition.h"   // toms::ConditionContext / toms::evaluate

namespace toms {

struct EndingDefinition {
    std::string id;
    std::string nameKey, textKey;   // data/text.json keys (narrative content, not inline i18n --
                                     // matches every other story.* key's convention, unlike the
                                     // system-content files' inline {code:text} objects)
    std::string tone;               // "good"/"bad"/"neutral"/"secret-good"/"secret-neutral" -- cosmetic only
    std::string unlocksHint;        // a meta hint id revealed once this ending is first seen (may be empty)
    bool allowsRebirth = false;     // whether M8's (not yet built) rebirth offer applies after this one
    nlohmann::json requires_;       // a condition.h tree, evaluated via toms::evaluate()
};

nlohmann::json toJson(const EndingDefinition& d);
EndingDefinition endingFromJson(const nlohmann::json& j);

struct EndingsTable {
    std::vector<EndingDefinition> endings;   // resolutionOrder == this array's own order
    std::string fallback;                    // MUST name an id present in `endings`
};

// Loads data/story/endings.json. Missing/malformed file -> an empty table (never throws),
// matching readJsonFileSafe's fail-soft convention used everywhere else.
EndingsTable loadEndings(const std::string& path);

// docs/story/STORY_DATA_SCHEMA.md §7.2: walks `table.endings` in order and returns the first whose
// `requires` is satisfied; if none match, returns the ending named by `table.fallback` (or nullptr
// if even that id doesn't exist in the table -- a content bug, not a runtime one, since V13-style
// validation is the tool's job, not this function's). This is the F70-completion judge -- nothing
// in the shipped 21 floors of content can reach F70 yet, so this path is authored and tested, not
// yet exercised by real play.
const EndingDefinition* judgeEnding(const EndingsTable& table, const ConditionContext& ctx);

// docs/story/STORY_DATA_SCHEMA.md §7.3's non-F70 trigger rows (a party wipe, a specific F69
// choice, ...): checks ONLY the ids named in `order`, in that exact sequence -- never the general
// resolutionOrder/fallback above. This matters: the fallback ending's own "any: ...or always"
// condition is unconditionally true, so running the FULL resolver on a plain wipe would wrongly
// end the run on literally every death. Returns nullptr if none of `order` match, meaning "not an
// ending -- whatever ordinary consequence the caller already had (e.g. respawn) still applies."
const EndingDefinition* checkNamedEndings(const EndingsTable& table, const std::vector<std::string>& order,
                                           const ConditionContext& ctx);

} // namespace toms
