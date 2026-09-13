// game_story.cpp — narrative systems glue: dialogue nodes/choices/actions, missions,
// notifications and the game-state query. Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

void Game::startDialogue(const std::string& npc) {
    dlgData = readJsonFile(dataDir + "/../data/dialogue/" + npc + ".json");
    if (dlgData.is_null() || dlgData.empty()) { inDialogue=false; return; }
    if (!dlgData.contains("start") || !dlgData["start"].is_string()) { inDialogue = false; return; }
    inDialogue = true; dlgNode = dlgData["start"];
    dlgSel = 0;
    enterNode(dlgNode);
    audio.play("dialogue_popup");
}

void Game::enterNode(const std::string& node) {
    dlgChoices.clear();
    if (!dlgData.contains("nodes") || !dlgData["nodes"].contains(node)) {
        inDialogue = false; return;   // missing node -> close safely (no nlohmann throw / abort)
    }
    const auto& n = dlgData["nodes"][node];
    if (!n.is_object() || !n.contains("choices")) return;
    for (auto& c : n["choices"]) {
        if (!c.is_object()) continue;
        // Milestone 3: a choice's optional `requires` gates whether it appears at all, evaluated
        // via the shared Condition Evaluator (condition.h). No shipped dialogue file sets this
        // yet, so `contains("requires")` is false for all of them today -- purely additive.
        if (c.contains("requires") && !toms::evaluate(c["requires"], GameConditionContext(pl, meta_, missionTrackers_)))
            continue;
        std::string label = c.contains("label") ? locale_.field(c["label"]) : "";
        std::string next  = c.contains("next")  && !c["next"].is_null()  ? (std::string)c["next"]  : "";
        nlohmann::json action = c.contains("action") ? c["action"] : nlohmann::json();
        dlgChoices.push_back({label, next, action});
    }
    dlgSel = 0;
}

void Game::chooseDialogue(int idx) {
    if (!inDialogue) return;
    if (idx < 0 || idx >= (int)dlgChoices.size()) return;
    markProgressDirty();   // dialogue choices move story flags/beats, which the meta save holds
    audio.play("confirm_click");
    auto ch = dlgChoices[idx];   // copy: runDialogueAction (e.g. enterBattle) may resize dlgChoices
    toms::globalEventBus().publish(toms::ChoiceMade{dlgNpc, ch.label});
    if (!ch.action.is_null()) runDialogueAction(ch.action);
    if (!inDialogue) return;    // an action (e.g. enterBattle) may have already ended the dialogue
    if (ch.next.empty()) { inDialogue = false; return; }
    dlgNode = ch.next; enterNode(dlgNode);
}

// Milestone 4: executes a dialogue choice's optional `action` verb. Kept as a small, explicit
// enum-style switch rather than a scripting hook, matching this project's existing preference
// (see MAIN_BATTLE_SCENE_DESIGN.md §4.4's `talent` enum / dialogue's own `action.give`
// precedent in the design docs).
void Game::runDialogueAction(const nlohmann::json& action) {
    if (!action.is_object()) return;
    std::string type = action.value("type", std::string());
    if (type == "give") {
        std::string itemId = action.value("itemId", std::string());
        if (!itemId.empty()) applyItem(itemId);
    } else if (type == "setStoryFlag") {
        std::string flag = action.value("flag", std::string());
        if (!flag.empty()) toms::setStoryFlag(meta_, flag);
    } else if (type == "enterBattle") {
        // Only meaningful after a dialogue_gate encounter (movePlayer) stashed an enemy.
        if (hasPendingEncounter_) {
            inDialogue = false;
            hasPendingEncounter_ = false;
            startCombat(pendingEncounterEnemy_);
        }
    } else if (type == "startMission") {
        std::string missionId = action.value("missionId", std::string());
        if (!missionId.empty()) startMission(missionId);
    } else if (type == "claimMission") {
        std::string missionId = action.value("missionId", std::string());
        if (!missionId.empty()) claimMission(missionId);
    }
}

void Game::startMission(const std::string& id) {
    auto& t = missionTrackers_[id];   // creates a fresh (Locked, progress=0) tracker if absent
    if (t.missionId.empty()) t.missionId = id;
    t.state = toms::MissionState::Active;
}

// Milestone 8: grants the reward and flips Completed -> Claimed. Strictly gated on the tracker
// already being Completed, so a dialogue choice that stays visible after claiming (see the
// `requires: missionComplete` convention used in this session's authored content -- missionComplete()
// returns true for both Completed and Claimed) can never grant the reward twice.
void Game::claimMission(const std::string& id) {
    auto trackerIt = missionTrackers_.find(id);
    if (trackerIt == missionTrackers_.end() || trackerIt->second.state != toms::MissionState::Completed) return;
    auto defIt = missionDefs_.find(id);
    if (defIt != missionDefs_.end()) {
        const toms::MissionDefinition& def = defIt->second;
        pl.exp += def.rewardExp;
        pl.gold += def.rewardGold;
        // Mirrors movePlayer()'s item-pickup split: keys/coins apply immediately, everything
        // else (gems/potions/exp/scroll) goes into the inventory to be used later, not consumed
        // on the spot -- a reward potion should sit in the backpack like a picked-up one would.
        if (!def.rewardItemId.empty()) {
            bool immediate = (def.rewardItemId.rfind("key_",0)==0) || def.rewardItemId=="coin";
            if (immediate) applyItem(def.rewardItemId);
            else pl.inv.push_back(def.rewardItemId);
        }
        int need = pl.lv * 30;
        while (pl.exp >= need) {
            pl.exp -= need; pl.lv++; pl.atk += 2; pl.def += 1; pl.maxhp += 10; need = pl.lv*30;
            pushNotification(locale_.tr("battle.levelup") + std::to_string(pl.lv));
        }
    }
    trackerIt->second.state = toms::MissionState::Claimed;
    pushNotification(locale_.tr("mission.completed_prefix") + id);
}

// Subscribes mission-progress handlers to the global EventBus once (called from loadAssets).
// missionDefs_ is empty until Milestone 8 loads data/missions.json, so these handlers are inert
// today -- they become live the moment content defines a matching objective, with zero coupling
// back into Battle/Item code (which only ever calls publish(), unchanged since Milestone 1).
void Game::pushNotification(const std::string& msg) {
    notifications_.push_back({msg, 3000});   // 3 seconds on screen
}

void Game::rollDailyMissions() {
    std::string today = todayDateStringLocal();
    for (auto& [id, tracker] : missionTrackers_) {
        auto it = missionDefs_.find(id);
        if (it == missionDefs_.end() || it->second.kind != toms::MissionKind::Daily) continue;
        toms::MissionState before = tracker.state;
        toms::rollDailyReset(tracker, it->second, today);
        if (tracker.state == toms::MissionState::Available && before != toms::MissionState::Available)
            pushNotification(locale_.tr("mission.daily_reset_prefix") + id);
    }
}

void Game::wireMissionEvents() {
    toms::globalEventBus().subscribe<toms::EnemyDefeated>([this](const toms::EnemyDefeated& e) {
        for (auto& [id, tracker] : missionTrackers_) {
            auto it = missionDefs_.find(id);
            if (it != missionDefs_.end()) toms::applyProgressEvent(tracker, it->second, "defeat", e.enemyId);
        }
    });
    toms::globalEventBus().subscribe<toms::ItemCollected>([this](const toms::ItemCollected& e) {
        for (auto& [id, tracker] : missionTrackers_) {
            auto it = missionDefs_.find(id);
            if (it != missionDefs_.end()) toms::applyProgressEvent(tracker, it->second, "collect", e.itemId);
        }
    });
}

toms::GameState Game::currentState() const {
    using toms::GameState;
    // Title phase (Boot): reported as MainMenu, with its Settings page mapping to
    // GameState::Settings — game_state.h's table already allows MainMenu <-> Settings.
    if (title_.isOpen())
        return (title_.page() == toms::TitlePage::Settings) ? GameState::Settings : GameState::MainMenu;
    if (cs.active) return GameState::DirectBattle;
    if (inDialogue) return GameState::Dialogue;
    if (storeOpen || storeUnlockDlg) return GameState::Merchant;
    return GameState::Explore;   // includes inventory-open, an overlay atop Explore
}
