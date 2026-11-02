// run_state.cpp — see run_state.h.
#include "run_state.h"

#include <algorithm>

namespace toms {

void RunStoryState::f_MakeChoice(const std::string& l_strChoiceId, const std::string& l_strOptionId,
                               bool l_bReversible) {
    if (l_strChoiceId.empty() || l_strOptionId.empty()) return;
    auto l_itIt = m_mapChoices.find(l_strChoiceId);
    if (l_itIt != m_mapChoices.end() && !l_bReversible) return;   // first answer sticks (section 4.1)
    m_mapChoices[l_strChoiceId] = l_strOptionId;
}

bool RunStoryState::f_ChoiceMade(const std::string& l_strChoiceId, const std::string& l_strOptionId) const {
    auto l_itIt = m_mapChoices.find(l_strChoiceId);
    return l_itIt != m_mapChoices.end() && l_itIt->second == l_strOptionId;
}

std::string RunStoryState::f_Choice(const std::string& l_strChoiceId) const {
    return f_Lookup(m_mapChoices, l_strChoiceId);
}

int RunStoryState::f_Counter(const std::string& l_strName) const {
    auto l_itIt = m_mapCounters.find(l_strName);
    return (l_itIt == m_mapCounters.end()) ? 0 : l_itIt->second;
}

void RunStoryState::f_SetCounter(const std::string& l_strName, int l_iValue) {
    if (l_strName.empty()) return;
    int l_iLo = -20, l_iHi = 20;
    auto l_itD = m_mapDecls.find(l_strName);
    if (l_itD != m_mapDecls.end()) { l_iLo = l_itD->second.m_iLo; l_iHi = l_itD->second.m_iHi; }
    m_mapCounters[l_strName] = std::max(l_iLo, std::min(l_iHi, l_iValue));
}

void RunStoryState::f_AddCounter(const std::string& l_strName, int l_iDelta) {
    f_SetCounter(l_strName, f_Counter(l_strName) + l_iDelta);
}

bool RunStoryState::f_CounterVisible(const std::string& l_strName) const {
    auto l_itD = m_mapDecls.find(l_strName);
    const int l_iThreshold = (l_itD == m_mapDecls.end()) ? 6 : l_itD->second.m_iDisplayAt;
    return f_Counter(l_strName) >= l_iThreshold;
}

void RunStoryState::f_SetSideStoryState(const std::string& l_strId, const std::string& l_strState) {
    if (l_strId.empty()) return;
    m_mapSideStories[l_strId] = l_strState;
}

void RunStoryState::f_SetFlag(const std::string& l_strFlag, bool l_bOn) {
    if (l_strFlag.empty()) return;
    m_mapFlags[l_strFlag] = l_bOn;
}

bool RunStoryState::f_Flag(const std::string& l_strFlag) const {
    auto l_itIt = m_mapFlags.find(l_strFlag);
    return l_itIt != m_mapFlags.end() && l_itIt->second;
}

void RunStoryState::f_AddShard(const std::string& l_strShardId) {
    if (l_strShardId.empty()) return;
    if (std::find(m_vShards.begin(), m_vShards.end(), l_strShardId) == m_vShards.end())
        m_vShards.push_back(l_strShardId);
}

bool RunStoryState::f_HasShard(const std::string& l_strShardId) const {
    return std::find(m_vShards.begin(), m_vShards.end(), l_strShardId) != m_vShards.end();
}

void RunStoryState::f_UnlockSkill(const std::string& l_strSkillId, int l_iCost) {
    if (l_strSkillId.empty() || f_HasSkill(l_strSkillId)) return;   // idempotent: already-owned is a no-op
    m_iSkillPoints = std::max(0, m_iSkillPoints - l_iCost);
    m_vSkillsOwned.push_back(l_strSkillId);
}

bool RunStoryState::f_HasSkill(const std::string& l_strSkillId) const {
    return std::find(m_vSkillsOwned.begin(), m_vSkillsOwned.end(), l_strSkillId) != m_vSkillsOwned.end();
}

void RunStoryState::f_MarkFloorCleared(const std::string& l_strFloorId) {
    if (l_strFloorId.empty()) return;
    if (std::find(m_vClearedFloors.begin(), m_vClearedFloors.end(), l_strFloorId) == m_vClearedFloors.end())
        m_vClearedFloors.push_back(l_strFloorId);
    m_strFloor = l_strFloorId;
}

bool RunStoryState::f_FloorCleared(const std::string& l_strFloorId) const {
    return std::find(m_vClearedFloors.begin(), m_vClearedFloors.end(), l_strFloorId) != m_vClearedFloors.end();
}

void RunStoryState::f_NoteDeath(bool l_bBoss) {
    ++m_iDeathsTotal;
    if (!l_bBoss) ++m_iDeathsNonBoss;
}

void RunStoryState::f_Reset(bool l_bKeepShards) {
    m_mapChoices.clear();
    m_mapCounters.clear();
    m_mapSideStories.clear();
    m_mapFlags.clear();
    m_vClearedFloors.clear();
    m_vEventsStarted.clear();
    m_vEventsFinished.clear();
    m_mapVars.clear();
    m_mapNpcTalk.clear();
    m_mapEventCounts.clear();
    m_iDeathsTotal = m_iDeathsNonBoss = 0;
    m_strFloor = "F01";
    m_iSkillPoints = 0;
    m_vSkillsOwned.clear();
    m_iSuperMax = m_iDefaultSuperMax;
    // Section 8: on its own, this wipes EVERYTHING run-scoped except the shards `keepShards`
    // preserves (and, on the meta save, not here: cycleIndex/endingsSeen/hintsUnlocked). A brand
    // New Game calls this with keepShards=false and stops here -- a genuinely fresh run.
    // Game::rebirth() (M8) is the OTHER caller, with keepShards=true: it calls this first for
    // exactly the same full wipe, then immediately restores skillsOwned_/skillPoints_/superMax_
    // itself at their HALVED values (docs/story/STORY_BIBLE.md §8's carry table) via the public
    // unlockSkill()/addSkillPoints()/setSuperMax() below -- this function only ever produces a
    // fully-wiped state; it is never the one deciding what a rebirth carries forward.
    if (!l_bKeepShards) m_vShards.clear();
}

void RunStoryState::f_WriteInto(RunSaveData& l_oR) const {
    l_oR.m_mapChoices = m_mapChoices;
    l_oR.m_mapCounters = m_mapCounters;
    l_oR.m_mapSideStories = m_mapSideStories;
    l_oR.m_mapFlags = m_mapFlags;
    l_oR.m_strFloor = m_strFloor;
    l_oR.m_vClearedFloors = m_vClearedFloors;
    l_oR.m_vEventsStarted = m_vEventsStarted;
    l_oR.m_vEventsFinished = m_vEventsFinished;
    l_oR.m_mapVars = m_mapVars;
    l_oR.m_mapEventCounts = m_mapEventCounts;
    l_oR.m_mapNpcTalk.clear();
    for (const auto& [l_strNpc, l_oT] : m_mapNpcTalk) l_oR.m_mapNpcTalk[l_strNpc] = {l_oT.m_iIndex, l_oT.m_iCount, l_oT.m_iLastRandom};
    l_oR.m_vShards = m_vShards;
    l_oR.m_iDeathsTotal = m_iDeathsTotal;
    l_oR.m_iDeathsNonBoss = m_iDeathsNonBoss;
    l_oR.m_iSkillPoints = m_iSkillPoints;
    l_oR.m_vSkillsOwned = m_vSkillsOwned;
    l_oR.m_iSuperMax = m_iSuperMax;
}

void RunStoryState::f_ReadFrom(const RunSaveData& l_oR) {
    m_mapChoices = l_oR.m_mapChoices;
    m_mapCounters = l_oR.m_mapCounters;
    m_mapSideStories = l_oR.m_mapSideStories;
    m_mapFlags = l_oR.m_mapFlags;
    m_strFloor = l_oR.m_strFloor.empty() ? "F01" : l_oR.m_strFloor;
    m_vClearedFloors = l_oR.m_vClearedFloors;
    m_vEventsStarted = l_oR.m_vEventsStarted;
    m_vEventsFinished = l_oR.m_vEventsFinished;
    for (const std::string& l_strId : m_vEventsFinished) f_MarkEventStarted(l_strId);   // an older or hand-edited save
    m_mapVars.clear();
    for (const auto& [l_strK, l_iV] : l_oR.m_mapVars) f_SetVar(l_strK, l_iV);                       // clamped like the counters below
    m_mapEventCounts = l_oR.m_mapEventCounts;
    m_mapNpcTalk.clear();
    for (const auto& [l_strNpc, l_vT] : l_oR.m_mapNpcTalk) {
        NpcTalkState& l_oS = m_mapNpcTalk[l_strNpc];
        if (l_vT.size() > 0) l_oS.m_iIndex = std::max(0, l_vT[0]);
        if (l_vT.size() > 1) l_oS.m_iCount = std::max(0, l_vT[1]);
        if (l_vT.size() > 2) l_oS.m_iLastRandom = l_vT[2];
    }
    m_vShards = l_oR.m_vShards;
    m_iDeathsTotal = l_oR.m_iDeathsTotal;
    m_iDeathsNonBoss = l_oR.m_iDeathsNonBoss;
    m_iSkillPoints = l_oR.m_iSkillPoints;
    m_vSkillsOwned = l_oR.m_vSkillsOwned;
    m_iSuperMax = l_oR.m_iSuperMax;
    // Re-apply the declared clamps: a hand-edited or older save must not smuggle an out-of-range
    // counter past the rules section 8's rebirth maths depends on.
    std::map<std::string, int> l_mapSnapshot = m_mapCounters;
    m_mapCounters.clear();
    for (auto& [l_strK, l_iV] : l_mapSnapshot) f_SetCounter(l_strK, l_iV);
}

void RunStoryState::f_MarkEventStarted(const std::string& l_strEventId) {
    if (!l_strEventId.empty() && !f_EventStarted(l_strEventId)) m_vEventsStarted.push_back(l_strEventId);
}

void RunStoryState::f_MarkEventFinished(const std::string& l_strEventId) {
    if (l_strEventId.empty()) return;
    f_MarkEventStarted(l_strEventId);
    if (!f_EventFinished(l_strEventId)) m_vEventsFinished.push_back(l_strEventId);
}

bool RunStoryState::f_EventStarted(const std::string& l_strEventId) const {
    return std::find(m_vEventsStarted.begin(), m_vEventsStarted.end(), l_strEventId) != m_vEventsStarted.end();
}

bool RunStoryState::f_EventFinished(const std::string& l_strEventId) const {
    return std::find(m_vEventsFinished.begin(), m_vEventsFinished.end(), l_strEventId) != m_vEventsFinished.end();
}

int RunStoryState::f_Var(const std::string& l_strName) const {
    if (auto l_itIt = m_mapVars.find(l_strName); l_itIt != m_mapVars.end()) return l_itIt->second;
    auto l_itD = m_mapVarDecls.find(l_strName);
    return l_itD == m_mapVarDecls.end() ? 0 : l_itD->second.m_iDef;
}

void RunStoryState::f_SetVar(const std::string& l_strName, int l_iValue) {
    if (l_strName.empty()) return;
    if (auto l_itD = m_mapVarDecls.find(l_strName); l_itD != m_mapVarDecls.end()) l_iValue = std::clamp(l_iValue, l_itD->second.m_iLo, l_itD->second.m_iHi);
    m_mapVars[l_strName] = l_iValue;
}

int RunStoryState::f_EventCount(const std::string& l_strEventId) const {
    auto l_itIt = m_mapEventCounts.find(l_strEventId);
    const int l_iN = l_itIt == m_mapEventCounts.end() ? 0 : l_itIt->second;
    return f_EventFinished(l_strEventId) ? std::max(l_iN, 1) : l_iN;
}

NpcTalkState RunStoryState::f_NpcTalk(const std::string& l_strNpc) const {
    auto l_itIt = m_mapNpcTalk.find(l_strNpc);
    return l_itIt == m_mapNpcTalk.end() ? NpcTalkState{} : l_itIt->second;
}

std::string RunStoryState::f_DebugSummary() const {
    std::string l_strOut = "floor=" + m_strFloor + " shards=" + std::to_string(m_vShards.size())
                    + " deaths=" + std::to_string(m_iDeathsTotal)
                    + " cleared=" + std::to_string(m_vClearedFloors.size()) + " choices=[";
    bool l_bFirst = true;
    for (auto& [l_strK, l_strV] : m_mapChoices) {
        if (!l_bFirst) l_strOut += ",";
        l_strOut += l_strK + "=" + l_strV;
        l_bFirst = false;
    }
    l_strOut += "] counters=[";
    l_bFirst = true;
    for (auto& [l_strK, l_iV] : m_mapCounters) {
        if (!l_bFirst) l_strOut += ",";
        l_strOut += l_strK + "=" + std::to_string(l_iV);
        l_bFirst = false;
    }
    return l_strOut + "]";
}

} // namespace toms
