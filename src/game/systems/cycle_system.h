// cycle_system.h — M8: 輪迴 (rebirth/New Game+) config, per docs/story/STORY_BIBLE.md §8 and
// docs/story/STORY_DATA_SCHEMA.md §8's `cycles.json` schema.
//
// Unlike ending_system.h's resolver (which really does interpret arbitrary condition trees),
// there is no generic "carry/reset op" interpreter here: `STORY_DATA_SCHEMA.md`'s own schema
// shows a small, FIXED set of rules (halve-and-floor a few numbers, keep-or-clear everything
// else), so `Game::rebirth()` (game_story.cpp) just implements each rule directly in C++ against
// the real Player/RunStoryState/MetaSaveData/EquippedSet types -- the same "content is data, the
// transformation is explicit code" split this project already uses for skill/forge/hub/ending
// content. This file only holds the handful of actual NUMBERS a rewrite of `data/story/cycles.json`
// could tune (maxCycles, the two halving scales) without touching code.
#pragma once
#include <json.hpp>
#include <string>

namespace toms {

struct CyclesConfig {
    int maxCycles = 9;              // STORY_BIBLE.md §8: cycleMark caps at 9 (e_10's own condition)
    float skillEffectScale = 0.5f;  // "已學技能...數值效果 ×0.5" (tier<=0 roots are always exempt --
                                     // see applySkillEffects's own comment for why that lives there)
    float statBonusScale = 0.5f;    // "攻擊／防禦基礎加成 ×0.5" -- applied to the LEVEL-UP bonus over
                                     // the starting stat, not the starting stat itself
};

// Loads data/story/cycles.json. Missing/malformed file -> the documented defaults above (never
// throws), matching readJsonFileSafe's fail-soft convention used everywhere else -- a game with no
// cycles.json at all should still run rebirth with the bible's own baseline numbers, not refuse to.
CyclesConfig loadCyclesConfig(const std::string& path);

} // namespace toms
