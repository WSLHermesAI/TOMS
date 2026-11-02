// game_condition.h — GameConditionContext: adapts live Player + MetaSaveData state to the
// engine-level toms::ConditionContext interface (condition.h), so door/key gating and dialogue
// `requires` gating both go through the one shared evaluator instead of bespoke per-call-site
// checks (Milestone 3 — see docs/architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md section 9).
//
// Lived in game.cpp's anonymous namespace until the 2026-09-13 split, which moved the code using it
// (game_input.cpp, game_story.cpp) into their own translation units.
#pragma once

#include "game.h"
#include "condition.h"         // toms::ConditionContext (the interface implemented here)
#include "story_controller.h"  // toms::hasStoryFlag / storyBeat
#include "mission_system.h"    // toms::MissionTracker
#include "save_system.h"       // toms::MetaSaveData (story flags / cleared stages)
#include "run_state.h"         // toms::RunStoryState (S3: choices/counters/side stories/floors)
#include "equipment_system.h"  // toms::EquippedSet (M7: itemHeld() also checks equipped gear)
#include <map>
#include <string>

namespace toms {
namespace game_detail {

// GameConditionContext adapts live Player + MetaSaveData state to the engine-level
// toms::ConditionContext interface (condition.h), so door/key gating and dialogue `requires`
// gating both go through the one shared evaluator instead of bespoke checks (Milestone 3 —
// see docs/architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §9). Built from already-public Player
// fields + a MetaSaveData reference passed in by the Game methods that use it (movePlayer,
// enterNode), so it needs no friendship/new public API on Game.
class GameConditionContext : public toms::ConditionContext {
public:
    // `run` is REQUIRED, not defaulted: a default empty run state would silently answer "no choice
    // made yet" at any call site that forgot to pass it, which is exactly the kind of bug S3 exists
    // to prevent (the compiler now finds those sites instead). `equipped` is required for the same
    // reason, added for M7: several endings' `itemHeld` checks name equipment ids (e.g.
    // `qingxiao_blade`), and equipping something never adds it to `p.inv` (see buyStoreItem()/
    // Game::tryCraft() -- equipment is its own registry, not an inventory item), so `itemHeld`
    // must also recognize the currently equipped weapon/armor/talent or it could never be true for
    // those ids no matter what the player actually has equipped.
    GameConditionContext(const Player& l_oP, const toms::MetaSaveData& l_oMeta,
                          const std::map<std::string, toms::MissionTracker>& l_mapMissions,
                          const toms::RunStoryState& l_oRun, const toms::EquippedSet& l_oEquipped,
                          const std::string* l_pStage = nullptr)
        : m_oP(l_oP), m_oMeta(l_oMeta), m_mapMissions(l_mapMissions), m_oRun(l_oRun), m_oEq(l_oEquipped), m_pStage(l_pStage) {}
    bool f_StoryFlagSet(const std::string& l_strFlag) const override { return toms::f_HasStoryFlag(m_oMeta, l_strFlag); }
    bool f_RunFlagSet(const std::string& l_strFlag) const override { return m_oRun.f_Flag(l_strFlag); }
    int  f_StoryBeat() const override { return m_oMeta.m_iCurrentBeat; }
    bool f_ItemHeld(const std::string& l_strItemId, int l_iCount) const override {
        if (l_strItemId == "key_yellow") return m_oP.m_iKeyYellow >= l_iCount;
        if (l_strItemId == "key_blue")   return m_oP.m_iKeyBlue   >= l_iCount;
        if (l_strItemId == "key_red")    return m_oP.m_iKeyRed    >= l_iCount;
        // M7: an equipped weapon/armor/talent counts as "held" too (see the constructor's comment).
        if (l_strItemId == m_oEq.m_strWeaponId || l_strItemId == m_oEq.m_strArmorId || l_strItemId == m_oEq.m_strTalentId) return l_iCount <= 1;
        int l_iC = 0; for (auto& l_strS : m_oP.m_vInv) if (l_strS == l_strItemId) l_iC++;
        return l_iC >= l_iCount;
    }
    int f_StatValue(const std::string& l_strStat) const override {
        if (l_strStat == "atk")  return m_oP.m_iAtk;
        if (l_strStat == "def")  return m_oP.m_iDef;
        if (l_strStat == "hp")   return m_oP.m_iHp;
        if (l_strStat == "lv")   return m_oP.m_iLv;
        if (l_strStat == "gold") return m_oP.m_iGold;
        if (l_strStat == "exp")  return m_oP.m_iExp;
        if (l_strStat == "maxhp") return m_oP.m_iMaxhp;
        if (auto l_itA = m_oP.m_mapAttrs.find(l_strStat); l_itA != m_oP.m_mapAttrs.end()) return l_itA->second;   // str, dex, ... (data/stats.json)
        return 0;
    }
    // Milestone 4: real lookups against Game's live mission trackers (closes the stub these two
    // methods were in Milestone 3 — missionDefs_ may still be empty until Milestone 8 loads
    // content, but tracker *state* is real the moment a mission is started).
    bool f_MissionComplete(const std::string& l_strMissionId) const override {
        auto l_itIt = m_mapMissions.find(l_strMissionId);
        return l_itIt != m_mapMissions.end() &&
               (l_itIt->second.m_eState == toms::MissionState::Completed || l_itIt->second.m_eState == toms::MissionState::Claimed);
    }
    bool f_MissionActive(const std::string& l_strMissionId) const override {
        auto l_itIt = m_mapMissions.find(l_strMissionId);
        return l_itIt != m_mapMissions.end() && l_itIt->second.m_eState == toms::MissionState::Active;
    }
    // Milestone 5: a stage counts as "cleared" once the player has reached it at least once
    // (see Game::loadStage's meta_.unlockedStages tracking) -- the simplest sensible definition
    // for this linear-climb game; whether a *replayed* floor should repopulate enemies is a
    // separate, still-open design question (architecture-doc §5.2, tracked in the roadmap's
    // Milestone 7 balance checklist), not decided here.
    bool f_StageCleared(const std::string& l_strStageId) const override {
        // S3: a floor counts as cleared when the RUN says so (docs/story/STORY_DATA_SCHEMA.md section 9's
        // clearedFloors[]), falling back to the pre-S3 "reached at least once" rule for the eleven
        // hand-authored stages so existing content keeps behaving.
        if (m_oRun.f_FloorCleared(l_strStageId)) return true;
        return std::find(m_oMeta.m_vUnlockedStages.begin(), m_oMeta.m_vUnlockedStages.end(), l_strStageId) != m_oMeta.m_vUnlockedStages.end();
    }
    // ---- S3 leaves (docs/story/STORY_DATA_SCHEMA.md section 2.2) ----
    bool f_ChoiceMade(const std::string& l_strChoiceId, const std::string& l_strOptionId) const override {
        return m_oRun.f_ChoiceMade(l_strChoiceId, l_strOptionId);
    }
    std::string f_SideStoryState(const std::string& l_strSideStoryId) const override {
        return m_oRun.f_SideStoryState(l_strSideStoryId);
    }
    int f_Counter(const std::string& l_strName) const override { return m_oRun.f_Counter(l_strName); }
    int f_CycleIndex() const override { return m_oMeta.m_iCycleIndex; }
    // M7: e_07's condition ("記憶碎片不足").
    int f_MemoryShardCount() const override { return m_oRun.f_ShardCount(); }
    // M7: e_13's condition ("非首領戰死亡累積 ≥12").
    int f_DeathsNonBoss() const override { return m_oRun.f_DeathsNonBoss(); }
    int f_VarValue(const std::string& l_strVar) const override { return m_oRun.f_Var(l_strVar); }
    int f_TalkCount(const std::string& l_strNpc) const override { return m_oRun.f_NpcTalk(l_strNpc).m_iCount; }
    std::string f_CurrentStage() const override { return m_pStage ? *m_pStage : std::string(); }
private:
    const Player& m_oP;
    const toms::MetaSaveData& m_oMeta;
    const std::map<std::string, toms::MissionTracker>& m_mapMissions;
    const toms::RunStoryState& m_oRun;
    const toms::EquippedSet& m_oEq;
    const std::string* m_pStage;
};

} // namespace game_detail
} // namespace toms
