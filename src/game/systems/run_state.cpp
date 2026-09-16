// run_state.cpp — see run_state.h.
#include "run_state.h"

#include <algorithm>

namespace toms {

void RunStoryState::makeChoice(const std::string& choiceId, const std::string& optionId,
                               bool reversible) {
    if (choiceId.empty() || optionId.empty()) return;
    auto it = choices_.find(choiceId);
    if (it != choices_.end() && !reversible) return;   // first answer sticks (section 4.1)
    choices_[choiceId] = optionId;
}

bool RunStoryState::choiceMade(const std::string& choiceId, const std::string& optionId) const {
    auto it = choices_.find(choiceId);
    return it != choices_.end() && it->second == optionId;
}

std::string RunStoryState::choice(const std::string& choiceId) const {
    return lookup(choices_, choiceId);
}

int RunStoryState::counter(const std::string& name) const {
    auto it = counters_.find(name);
    return (it == counters_.end()) ? 0 : it->second;
}

void RunStoryState::setCounter(const std::string& name, int value) {
    if (name.empty()) return;
    int lo = -20, hi = 20;
    auto d = decls_.find(name);
    if (d != decls_.end()) { lo = d->second.lo; hi = d->second.hi; }
    counters_[name] = std::max(lo, std::min(hi, value));
}

void RunStoryState::addCounter(const std::string& name, int delta) {
    setCounter(name, counter(name) + delta);
}

bool RunStoryState::counterVisible(const std::string& name) const {
    auto d = decls_.find(name);
    const int threshold = (d == decls_.end()) ? 6 : d->second.displayAt;
    return counter(name) >= threshold;
}

void RunStoryState::setSideStoryState(const std::string& id, const std::string& state) {
    if (id.empty()) return;
    sideStories_[id] = state;
}

void RunStoryState::setFlag(const std::string& flag, bool on) {
    if (flag.empty()) return;
    flags_[flag] = on;
}

bool RunStoryState::flag(const std::string& flag) const {
    auto it = flags_.find(flag);
    return it != flags_.end() && it->second;
}

void RunStoryState::addShard(const std::string& shardId) {
    if (shardId.empty()) return;
    if (std::find(shards_.begin(), shards_.end(), shardId) == shards_.end())
        shards_.push_back(shardId);
}

bool RunStoryState::hasShard(const std::string& shardId) const {
    return std::find(shards_.begin(), shards_.end(), shardId) != shards_.end();
}

void RunStoryState::unlockSkill(const std::string& skillId, int cost) {
    if (skillId.empty() || hasSkill(skillId)) return;   // idempotent: already-owned is a no-op
    skillPoints_ = std::max(0, skillPoints_ - cost);
    skillsOwned_.push_back(skillId);
}

bool RunStoryState::hasSkill(const std::string& skillId) const {
    return std::find(skillsOwned_.begin(), skillsOwned_.end(), skillId) != skillsOwned_.end();
}

void RunStoryState::markFloorCleared(const std::string& floorId) {
    if (floorId.empty()) return;
    if (std::find(clearedFloors_.begin(), clearedFloors_.end(), floorId) == clearedFloors_.end())
        clearedFloors_.push_back(floorId);
    floor_ = floorId;
}

bool RunStoryState::floorCleared(const std::string& floorId) const {
    return std::find(clearedFloors_.begin(), clearedFloors_.end(), floorId) != clearedFloors_.end();
}

void RunStoryState::noteDeath(bool boss) {
    ++deathsTotal_;
    if (!boss) ++deathsNonBoss_;
}

void RunStoryState::reset(bool keepShards) {
    choices_.clear();
    counters_.clear();
    sideStories_.clear();
    flags_.clear();
    clearedFloors_.clear();
    deathsTotal_ = deathsNonBoss_ = 0;
    floor_ = "F01";
    skillPoints_ = 0;
    skillsOwned_.clear();
    superMax_ = kDefaultSuperMax;
    // Section 8: on its own, this wipes EVERYTHING run-scoped except the shards `keepShards`
    // preserves (and, on the meta save, not here: cycleIndex/endingsSeen/hintsUnlocked). A brand
    // New Game calls this with keepShards=false and stops here -- a genuinely fresh run.
    // Game::rebirth() (M8) is the OTHER caller, with keepShards=true: it calls this first for
    // exactly the same full wipe, then immediately restores skillsOwned_/skillPoints_/superMax_
    // itself at their HALVED values (docs/story/STORY_BIBLE.md §8's carry table) via the public
    // unlockSkill()/addSkillPoints()/setSuperMax() below -- this function only ever produces a
    // fully-wiped state; it is never the one deciding what a rebirth carries forward.
    if (!keepShards) shards_.clear();
}

void RunStoryState::writeInto(RunSaveData& r) const {
    r.choices = choices_;
    r.counters = counters_;
    r.sideStories = sideStories_;
    r.flags = flags_;
    r.floor = floor_;
    r.clearedFloors = clearedFloors_;
    r.shards = shards_;
    r.deathsTotal = deathsTotal_;
    r.deathsNonBoss = deathsNonBoss_;
    r.skillPoints = skillPoints_;
    r.skillsOwned = skillsOwned_;
    r.superMax = superMax_;
}

void RunStoryState::readFrom(const RunSaveData& r) {
    choices_ = r.choices;
    counters_ = r.counters;
    sideStories_ = r.sideStories;
    flags_ = r.flags;
    floor_ = r.floor.empty() ? "F01" : r.floor;
    clearedFloors_ = r.clearedFloors;
    shards_ = r.shards;
    deathsTotal_ = r.deathsTotal;
    deathsNonBoss_ = r.deathsNonBoss;
    skillPoints_ = r.skillPoints;
    skillsOwned_ = r.skillsOwned;
    superMax_ = r.superMax;
    // Re-apply the declared clamps: a hand-edited or older save must not smuggle an out-of-range
    // counter past the rules section 8's rebirth maths depends on.
    std::map<std::string, int> snapshot = counters_;
    counters_.clear();
    for (auto& [k, v] : snapshot) setCounter(k, v);
}

std::string RunStoryState::debugSummary() const {
    std::string out = "floor=" + floor_ + " shards=" + std::to_string(shards_.size())
                    + " deaths=" + std::to_string(deathsTotal_)
                    + " cleared=" + std::to_string(clearedFloors_.size()) + " choices=[";
    bool first = true;
    for (auto& [k, v] : choices_) {
        if (!first) out += ",";
        out += k + "=" + v;
        first = false;
    }
    out += "] counters=[";
    first = true;
    for (auto& [k, v] : counters_) {
        if (!first) out += ",";
        out += k + "=" + std::to_string(v);
        first = false;
    }
    return out + "]";
}

} // namespace toms
