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
        if (c.contains("requires") && !toms::evaluate(c["requires"], GameConditionContext(pl, meta_, missionTrackers_, run_)))
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
// (see docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.4's `talent` enum / dialogue's own `action.give`
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
    } else if (type == "makeChoice") {
        // S3: record a main-line decision (docs/story/STORY_DATA_SCHEMA.md section 4.1). Main-line choices
        // are irreversible, so the first answer sticks even if the node is reached again.
        std::string choiceId = action.value("choiceId", std::string());
        std::string optionId = action.value("optionId", std::string());
        if (!choiceId.empty() && !optionId.empty()) {
            run_.makeChoice(choiceId, optionId, action.value("reversible", false));
            // The option's effects travel with the action: the chapter file is the authority
            // (section 4.1's option entry) and the dialogue node mirrors it, the same way every other
            // data-driven effect in this project is declared where it is used.
            if (action.contains("setFlags") && action["setFlags"].is_array())
                for (auto& f : action["setFlags"])
                    if (f.is_string()) run_.setFlag(f.get<std::string>());
            if (action.contains("counters") && action["counters"].is_object())
                for (auto& [name, delta] : action["counters"].items())
                    if (delta.is_number_integer()) run_.addCounter(name, delta.get<int>());
        }
    } else if (type == "addCounter") {
        // S3: the three accumulators (insight / resolve / humanity). Clamped by RunStoryState to the
        // range declared in data/story/counters.json.
        std::string name = action.value("counter", std::string());
        int delta = action.value("delta", 0);
        if (!name.empty() && delta != 0) run_.addCounter(name, delta);
    }
}

// S4: applies a chapter's `grants` exactly once -- see the declaration in game.h for why this
// exists at all (nothing has ever read a chapter file at runtime before this).
void Game::applyChapterGrants(const std::string& chapterId) {
    if (chapterId.empty()) return;
    std::string grantedFlag = "_chapter_granted." + chapterId;
    if (run_.flag(grantedFlag)) return;   // idempotent: revisiting ch_01's floors must not re-grant
    nlohmann::json ch = readJsonFile(dataDir + "/../data/story/chapters/" + chapterId + ".json");
    if (!ch.is_object() || !ch.contains("grants") || !ch["grants"].is_object()) {
        // No chapter file yet (ch_04+, per S3's own scope) -- mark it granted anyway so the moment
        // that content DOES land, its grants apply from a fresh chapter entry, not retroactively
        // mid-chapter the session it happens to be authored.
        run_.setFlag(grantedFlag);
        return;
    }
    auto& grants = ch["grants"];
    if (grants.contains("skillPoints") && grants["skillPoints"].is_number_integer())
        run_.addSkillPoints(grants["skillPoints"].get<int>());
    if (grants.contains("skills") && grants["skills"].is_array()) {
        for (auto& s : grants["skills"]) {
            if (!s.is_string()) continue;
            std::string skillId = s.get<std::string>();
            auto it = skillDefs_.find(skillId);
            int cost = (it != skillDefs_.end()) ? it->second.cost : 0;   // chapter grants are free either way
            if (!run_.hasSkill(skillId)) {
                run_.unlockSkill(skillId, cost);
                std::string name = it != skillDefs_.end() ? locale_.field(it->second.name) : skillId;
                pushNotification(locale_.tr("skill.unlocked_prefix") + name);
            }
        }
    }
    // S5: forge blueprints -- meta scope (survives rebirth, see MetaSaveData::forgeRecipesKnown),
    // unlike skills above which are run-scoped. Dedup by hand: this is a plain vector, not a set,
    // matching the shape section 8's rebirth table already implies for it (a small, append-mostly
    // list), and applyChapterGrants can't assume it will only ever run once per recipe across a
    // whole *cycle* the way run flags can.
    if (grants.contains("forgeRecipes") && grants["forgeRecipes"].is_array()) {
        for (auto& r : grants["forgeRecipes"]) {
            if (!r.is_string()) continue;
            std::string recipeId = r.get<std::string>();
            if (std::find(meta_.forgeRecipesKnown.begin(), meta_.forgeRecipesKnown.end(), recipeId)
                == meta_.forgeRecipesKnown.end()) {
                meta_.forgeRecipesKnown.push_back(recipeId);
                auto fit = forgeDefs_.find(recipeId);
                std::string name = fit != forgeDefs_.end() ? locale_.field(fit->second.name) : recipeId;
                pushNotification(locale_.tr("forge.recipe_learned_prefix") + name);
            }
        }
    }
    // hub/unlocks: recorded as run flags so a later phase (the hub NPC, the forge, gated dialogue)
    // can read them the moment it exists, same "wire the data even if nothing consumes it yet"
    // pattern this project used for mission/equipment content before their own UIs landed.
    for (const char* key : {"hub", "unlocks"}) {
        if (grants.contains(key) && grants[key].is_array())
            for (auto& u : grants[key]) if (u.is_string()) run_.setFlag(u.get<std::string>());
    }
    run_.setFlag(grantedFlag);
}

// S4: the one path that can spend a skill point. Re-checks canUnlockSkill itself (see the
// declaration in game.h) -- never trusts that a caller already validated it.
bool Game::tryUnlockSkill(const std::string& skillId) {
    if (!toms::canUnlockSkill(skillDefs_, run_.skillsOwned(), run_.skillPoints(), skillId)) return false;
    run_.unlockSkill(skillId, skillDefs_.at(skillId).cost);
    markProgressDirty();
    audio.play("confirm_click");
    return true;
}

std::vector<std::string> Game::skillMenuOrder() const {
    static const std::map<std::string, int> lineageOrder = { {"yinqi", 0}, {"yuqi", 1}, {"faqi", 2} };
    std::vector<std::string> ids;
    ids.reserve(skillDefs_.size());
    for (auto& [id, def] : skillDefs_) ids.push_back(id);
    std::sort(ids.begin(), ids.end(), [&](const std::string& a, const std::string& b) {
        const auto& da = skillDefs_.at(a); const auto& db = skillDefs_.at(b);
        auto rank = [&](const std::string& lineage) {
            auto it = lineageOrder.find(lineage);
            return it == lineageOrder.end() ? (int)lineageOrder.size() : it->second;
        };
        int ra = rank(da.lineage), rb = rank(db.lineage);
        if (ra != rb) return ra < rb;
        if (da.tier != db.tier) return da.tier < db.tier;
        return a < b;   // stable tie-break for two nodes at the same lineage+tier
    });
    return ids;
}

// S5: the forge sub-page's row order. There's no lineage grouping concept for recipes the way
// skills have, so plain id order (stable, deterministic, matches data/forge.json's own key order
// for the map's iteration) is all a 3-recipe list needs -- add a real sort key if that ever stops
// being enough.
std::vector<std::string> Game::forgeMenuOrder() const {
    std::vector<std::string> ids;
    ids.reserve(forgeDefs_.size());
    for (auto& [id, def] : forgeDefs_) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

// S5: the one path that can spend gold/materials on a craft. Re-checks canCraft itself (same
// "never trust the caller" rule as tryUnlockSkill) -- deducts cost and equips the result in one
// atomic step, so a UI bug can never spend materials without getting the item.
bool Game::tryCraft(const std::string& recipeId) {
    if (!toms::canCraft(forgeDefs_, meta_.forgeRecipesKnown, pl.inv, pl.gold, recipeId)) return false;
    const toms::ForgeRecipeDefinition& def = forgeDefs_.at(recipeId);
    pl.gold -= def.costGold;
    for (auto& [itemId, need] : def.materials) {
        int removed = 0;
        for (auto it = pl.inv.begin(); it != pl.inv.end() && removed < need; ) {
            if (*it == itemId) { it = pl.inv.erase(it); ++removed; }
            else ++it;
        }
    }
    auto eqIt = equipmentDefs_.find(def.resultEquipmentId);
    if (eqIt != equipmentDefs_.end()) {
        switch (eqIt->second.slot) {
            case toms::EquipmentSlot::Weapon: equipped_.weaponId = def.resultEquipmentId; break;
            case toms::EquipmentSlot::Armor:  equipped_.armorId  = def.resultEquipmentId; break;
            case toms::EquipmentSlot::Talent: equipped_.talentId = def.resultEquipmentId; break;
        }
    }
    markProgressDirty();
    audio.play("get_item");
    return true;
}

// S6: the hub sub-page's row order. Same reasoning as forgeMenuOrder() -- no lineage-style
// grouping concept for a handful of named places, so plain id order is enough.
std::vector<std::string> Game::hubMenuOrder() const {
    std::vector<std::string> ids;
    ids.reserve(hubDefs_.size());
    for (auto& [id, def] : hubDefs_) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

// S6: the one path that can activate a hub location. Re-checks hubLocationUnlocked itself (same
// "never trust the caller" rule as tryUnlockSkill/tryCraft) -- an already-locked row is a silent,
// safe no-op rather than something the UI needs to pre-check. Only "talk" is implemented today
// (see hub_system.h's own comment on actionKind); an unrecognized kind is also a safe no-op so a
// future content typo never crashes, just silently does nothing.
bool Game::activateHubLocation(const std::string& locationId) {
    auto hasFlag = [this](const std::string& f) { return run_.flag(f); };
    if (!toms::hubLocationUnlocked(hubDefs_, hasFlag, locationId)) return false;
    const toms::HubLocationDefinition& def = hubDefs_.at(locationId);
    if (def.actionKind == "talk" && !def.actionNpc.empty()) {
        closeInGameMenu();       // the dialogue UI and the pause menu were never designed to overlap
        startDialogue(def.actionNpc);
        return true;
    }
    return false;
}

// S5/S6: the Main page's row order/count, re-derived fresh every call from the two conditional
// flags (see their declaration in game.h for why this replaced a hardcoded "forgeOn ? 5 : 4"
// the moment a second conditional row existed) -- the one function draw and every input handler
// below share, so they can never disagree about which row index opens which page.
std::vector<Game::MainMenuRow> Game::mainMenuOrder() const {
    std::vector<MainMenuRow> rows = { MainMenuRow::Save, MainMenuRow::Settings, MainMenuRow::Skills };
    if (hubMenuUnlocked())   rows.push_back(MainMenuRow::Village);
    if (forgeMenuUnlocked()) rows.push_back(MainMenuRow::Forge);
    rows.push_back(MainMenuRow::BackToTitle);
    return rows;
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
