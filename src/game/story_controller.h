// story_controller.h — main-story beat progression + story flags, operating directly on
// MetaSaveData (src/game/save_system.h) rather than a parallel struct. MetaSaveData is already
// the single source of truth for this data (see docs/IMPLEMENTATION_ROADMAP.md Milestone 3,
// which deliberately folds the "Story Controller" into it instead of duplicating state).
// See docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §7.
#pragma once
#include "save_system.h"

namespace toms {

inline void setStoryFlag(MetaSaveData& meta, const std::string& flag) { meta.storyFlags.insert(flag); }
inline bool hasStoryFlag(const MetaSaveData& meta, const std::string& flag) {
    return meta.storyFlags.count(flag) != 0;
}

// Monotonic: never regresses currentBeat, even if called with a smaller beat (e.g. replaying an
// earlier floor from a future Stage Select hub shouldn't roll story progress backwards).
inline void advanceStoryBeat(MetaSaveData& meta, int beat) {
    if (beat > meta.currentBeat) meta.currentBeat = beat;
}

} // namespace toms
