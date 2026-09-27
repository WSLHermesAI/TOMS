// hub_system.h — S6: the village hub's locations (data/hub.json), per STORY_BIBLE.md's ch_01
// "村莊樞紐" (unlocked F7, alongside 引氣) and docs/story/STORY_DATA_SCHEMA.md §1.2/§13's M5.
//
// Same shape as skill_system.h/forge_system.h on purpose: pure content + a pure predicate, no
// Game dependency. Unlike those two, a hub location has no cost and nothing to "own" -- it is
// simply reachable once its unlockFlag (a run flag the chapter-grant pipeline already sets, e.g.
// "hub.village") is set, so there is no MetaSaveData/RunStoryState field of its own to add.
#pragma once
#include <json.hpp>
#include <functional>
#include <map>
#include <string>

namespace toms {

struct HubLocationDefinition {
    std::string id;
    std::string unlockFlag;   // a run flag name, e.g. "hub.village" -- set by Game::applyChapterGrants
    // What activating this location does. Only "talk" is implemented so far (opens an NPC's
    // dialogue the same way bumping into them in the world would, via Game::startDialogue) --
    // the field exists as a string, not an enum, so a future kind (e.g. "forge", "shop") can be
    // added by content + a new case in the dispatcher, not a schema change.
    std::string actionKind;
    std::string actionNpc;    // valid when actionKind == "talk": an id into data/dialogue/<id>.json
    // Raw json (plain string or {code:text} multi-language map) -- resolved via Locale::field()
    // at display time, matching every other content file's name/desc convention.
    nlohmann::json name, desc;
};

nlohmann::json toJson(const HubLocationDefinition& d);
HubLocationDefinition hubLocationFromJson(const nlohmann::json& j);

// Loads data/hub.json's {id: HubLocationDefinition} map. Missing/malformed file -> empty map
// (never throws), matching readJsonFileSafe's fail-soft convention used everywhere else.
std::map<std::string, HubLocationDefinition> loadHubLocations(const std::string& path);

// True if `locationId` names a real hub location and `flags` has its unlockFlag set. Pure
// predicate -- the UI (dimming a not-yet-reached location) and the actual activation share this
// one rule instead of re-deriving it.
bool hubLocationUnlocked(const std::map<std::string, HubLocationDefinition>& defs,
                          const std::function<bool(const std::string&)>& hasFlag,
                          const std::string& locationId);

} // namespace toms
