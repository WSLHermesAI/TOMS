// game_story.cpp — narrative systems glue: dialogue nodes/choices/actions, missions,
// notifications and the game-state query. Split out of game.cpp 2026-09-13.
#include "game_internal.h"
#include "stair_links.h"   // toms::fillText
#include "story_actions.h"
#include "talk_system.h"

#include <functional>

using namespace toms::game_detail;

void Game::startDialogue(const std::string& npc) {
    dlgNpc = npc;
    dlgSpeaker_.clear();
    dlgTalkNpc_.clear();
    openDialogue(readDialogue(npc), DlgMode::Plain);
}

nlohmann::json Game::readDialogue(const std::string& id) const {
    if (id.empty()) return nlohmann::json();
    return readJsonFile(pathFor(toms::pattern::kDialogue, id));
}

bool Game::holds(const nlohmann::json& condition) const {
    return toms::evaluate(condition, GameConditionContext(pl, meta_, missionTrackers_, run_, equipped_, &curStage));
}

// The game's answers to talk_system's questions.
namespace {
class GameTalkQuery : public toms::talk::TalkQuery {
public:
    GameTalkQuery(std::function<bool(const nlohmann::json&)> holds, const toms::RunStoryState& run, std::mt19937& rng)
        : holds_(std::move(holds)), run_(run), rng_(rng) {}
    bool holds(const nlohmann::json& c) const override { return holds_(c); }
    bool eventStarted(const std::string& id) const override { return run_.eventStarted(id); }
    bool eventFinished(const std::string& id) const override { return run_.eventFinished(id); }
    int eventCount(const std::string& id) const override { return run_.eventCount(id); }
    int random(int n) override { return n <= 1 ? 0 : std::uniform_int_distribution<int>(0, n - 1)(rng_); }
private:
    std::function<bool(const nlohmann::json&)> holds_;
    const toms::RunStoryState& run_;
    std::mt19937& rng_;
};
}  // namespace

bool Game::openDialogue(const nlohmann::json& data, DlgMode mode) {
    dlgChoices.clear();
    dlgData = data;
    dlgMode_ = mode;
    if (mode != DlgMode::Topic && mode != DlgMode::After) dlgEvent_.clear();
    dlgFinishOnClose_ = mode == DlgMode::Topic && data.is_object() && data.value("finishOnClose", false);
    GameTalkQuery q([this](const nlohmann::json& c) { return holds(c); }, run_, talkRng_);
    const std::string start = toms::talk::startNode(data, q);
    if (start.empty()) { inDialogue = false; return false; }
    inDialogue = true;
    dlgNode = start;
    dlgSel = 0;
    enterNode(dlgNode);
    if (inDialogue) audio.play("dialogue_popup");
    return inDialogue;
}

void Game::enterNode(const std::string& node) {
    dlgChoices.clear();
    if (!dlgData.contains("nodes") || !dlgData["nodes"].contains(node)) {
        endDialogue(); return;   // missing node -> close safely (no nlohmann throw / abort)
    }
    const auto& n = dlgData["nodes"][node];
    if (!n.is_object() || !n.contains("choices")) return;
    for (auto& c : n["choices"]) {
        if (!c.is_object()) continue;
        // Milestone 3: a choice's optional `requires` gates whether it appears at all, evaluated
        // via the shared Condition Evaluator (condition.h).
        if (c.contains("requires") && !holds(c["requires"])) continue;
        DialogueChoice ch;
        ch.label = c.contains("label") ? locale_.field(c["label"]) : "";
        ch.next  = c.contains("next") && c["next"].is_string() ? c["next"].get<std::string>() : "";
        if (c.contains("action")) ch.action = c["action"];
        if (c.contains("actions")) ch.actions = c["actions"];
        ch.finish = c.value("finish", false);
        if (c.contains("fire") && c["fire"].is_array())
            for (const auto& f : c["fire"]) if (f.is_string()) ch.fire.push_back(f.get<std::string>());
        ch.id = c.value("id", std::string());
        dlgChoices.push_back(ch);
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
    if (dlgMode_ == DlgMode::Menu) {   // the topic menu: the action says which line (openTopicMenu)
        const std::string pick = ch.action.value("menu", std::string());
        const std::string npc = dlgTalkNpc_;
        if (pick == "topic") openTopic(ch.action.value("eventId", std::string()), ch.action.value("finished", false));
        else if (pick == "chat") {
            GameTalkQuery q([this](const nlohmann::json& c) { return holds(c); }, run_, talkRng_);
            const int line = toms::talk::nextChatLine(npcDef(npc), run_.npcTalk(npc), q);
            if (line >= 0) playChatLine(npc, line); else endDialogue();
        } else endDialogue();
        return;
    }
    const std::string eventId = dlgEvent_;
    const bool topic = dlgMode_ == DlgMode::Topic;
    // A topic's choices are remembered per event (choiceMade {eventId, id}), so later dialogues and events can ask
    // which way the player went.
    if (topic && !ch.id.empty()) run_.makeChoice(eventId, ch.id, true);
    if (!ch.action.is_null()) runStoryActions(ch.action);
    if (!ch.actions.is_null()) runStoryActions(ch.actions);
    if (topic) {   // an after-dialogue's `finish` / `fire` are ignored: the event is already finished
        for (const std::string& f : ch.fire) wakeEvent(f);
        if (ch.finish) finishTalkEvent(eventId);
    }
    if (!inDialogue) return;    // an action (e.g. enterBattle) may have already ended the dialogue
    if (ch.next.empty()) { endDialogue(); return; }
    dlgNode = ch.next; enterNode(dlgNode);
}

void Game::endDialogue() {
    if (inDialogue && dlgMode_ == DlgMode::Topic && dlgFinishOnClose_ && !dlgEvent_.empty()) finishTalkEvent(dlgEvent_);
    inDialogue = false;
    dlgChoices.clear();
    dlgMode_ = DlgMode::Plain;
    dlgFinishOnClose_ = false;
}

// ---------- NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md) ----------

void Game::loadNpcs() {
    npcDefs_.clear();
    const std::string dir = pathOf(toms::key::kFolderNpcs) + "/";
    for (const std::string& name : toms::vfsListDir(dir)) {
        if (name.size() < 5 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
        toms::talk::NpcDef d;
        std::string err;
        if (toms::talk::parseNpc(readJsonFile(dir + name), d, &err)) npcDefs_[d.id] = d;
        else fprintf(stderr, "[assets] npcs/%s: %s\n", name.c_str(), err.c_str());
    }
    // Event variables (story/vars.json, the event editor's Variables tab): defaults and ranges.
    std::map<std::string, toms::VarDecl> decls;
    nlohmann::json vj = readJsonFile(pathOf(toms::key::kDataStoryVars));
    if (vj.is_object())
        for (auto& [k, v] : vj.items()) {
            if (k.empty() || k[0] == '_' || !v.is_object()) continue;
            toms::VarDecl d;
            d.def = toms::varNumber(v.value("default", nlohmann::json(0)));
            if (v.value("type", std::string()) == "bool") { d.lo = 0; d.hi = 1; }
            if (v.contains("min") && v["min"].is_number_integer()) d.lo = v["min"].get<int>();
            if (v.contains("max") && v["max"].is_number_integer()) d.hi = v["max"].get<int>();
            decls[k] = d;
        }
    run_.setVarDecls(decls);
    fprintf(stderr, "[assets] npcs: %zu files, %zu with topics; %zu event variables\n", npcDefs_.size(), npcTopics_.size(), decls.size());
}

const toms::talk::NpcDef& Game::npcDef(const std::string& npcId) {
    auto it = npcDefs_.find(npcId);
    if (it == npcDefs_.end()) it = npcDefs_.emplace(npcId, toms::talk::defaultNpc(npcId, npcId)).first;   // no NPC file
    return it->second;
}

void Game::talkTo(const std::string& npcId) {
    if (npcId.empty() || modalActive()) return;
    const toms::talk::NpcDef& npc = npcDef(npcId);
    static const std::vector<toms::talk::Topic> kNone;
    auto t = npcTopics_.find(npcId);
    GameTalkQuery q([this](const nlohmann::json& c) { return holds(c); }, run_, talkRng_);
    const toms::talk::TalkPlan plan = toms::talk::planTalk(npc, t == npcTopics_.end() ? kNone : t->second, run_.npcTalk(npcId), q);
    markProgressDirty();   // the talk index and count are saved with the run
    dlgTalkNpc_ = npcId;
    switch (plan.kind) {
        case toms::talk::TalkKind::Chat:  playChatLine(npcId, plan.chatLine); break;
        case toms::talk::TalkKind::Topic: openTopic(plan.topic.eventId, plan.topic.finished); break;
        case toms::talk::TalkKind::Menu:  openTopicMenu(npcId, plan.menu); break;
        case toms::talk::TalkKind::Nothing: break;
    }
}

void Game::playChatLine(const std::string& npcId, int line) {
    const toms::talk::NpcDef& npc = npcDef(npcId);
    if (line < 0 || line >= (int)npc.lines.size()) { endDialogue(); return; }
    const toms::talk::ChatLine& l = npc.lines[line];
    dlgNpc = l.dialogue;
    dlgSpeaker_ = npc.nameKey.empty() ? std::string() : locale_.tr(npc.nameKey);
    if (!l.actions.is_null()) runStoryActions(l.actions);
    openDialogue(readDialogue(l.dialogue), DlgMode::Chat);
}

void Game::openTopic(const std::string& eventId, bool finished) {
    auto def = eventDefs_.find(eventId);
    if (eventId.empty() || def == eventDefs_.end()) { endDialogue(); return; }
    const toms::talk::NpcDef& npc = npcDef(dlgTalkNpc_);
    dlgSpeaker_ = npc.nameKey.empty() ? std::string() : locale_.tr(npc.nameKey);
    dlgEvent_ = eventId;
    std::string file;
    for (const auto& [n, list] : npcTopics_)
        for (const toms::talk::Topic& t : list)
            if (t.eventId == eventId) file = finished ? t.afterDialogue : t.dialogue;
    if (!finished) {
        run_.markEventStarted(eventId);   // the event log lists it as in progress until it finishes
        markProgressDirty();
    }
    nlohmann::json data = readDialogue(file);
    if (file.empty() || !data.is_object()) {
        // No dialogue file: the event's text as one page; OK finishes it (an after-view just closes).
        std::string text = locale_.tr(def->second.textKey);
        if (text == def->second.textKey) text = eventId;
        nlohmann::json ok = {{"label", locale_.tr("talk.ok")}, {"next", nullptr}};
        if (!finished) ok["finish"] = true;
        data = {{"start", "root"}, {"nodes", {{"root", {{"text", text}, {"choices", nlohmann::json::array({ok})}}}}}};
    }
    dlgNpc = file.empty() ? eventId : file;
    if (!openDialogue(data, finished ? DlgMode::After : DlgMode::Topic)) endDialogue();
}

void Game::openTopicMenu(const std::string& npcId, const std::vector<toms::talk::MenuEntry>& menu) {
    const toms::talk::NpcDef& npc = npcDef(npcId);
    dlgSpeaker_ = npc.nameKey.empty() ? std::string() : locale_.tr(npc.nameKey);
    nlohmann::json choices = nlohmann::json::array();
    for (const toms::talk::MenuEntry& m : menu) {
        auto d = eventDefs_.find(m.eventId);
        std::string title = d == eventDefs_.end() ? m.eventId : locale_.tr(d->second.titleKey);
        if (d != eventDefs_.end() && title == d->second.titleKey) title = m.eventId;
        const std::string mark = m.finished ? std::string("\xE2\x9C\x93 ") : std::string();   // a check mark: done
        choices.push_back({{"label", mark + title}, {"next", nullptr},
                           {"action", {{"menu", "topic"}, {"eventId", m.eventId}, {"finished", m.finished}}}});
    }
    choices.push_back({{"label", locale_.tr("talk.just_chat")}, {"next", nullptr}, {"action", {{"menu", "chat"}}}});
    choices.push_back({{"label", locale_.tr("talk.leave")}, {"next", nullptr}, {"action", {{"menu", "leave"}}}});
    const std::string greeting = npc.greetingKey.empty() ? std::string() : locale_.tr(npc.greetingKey);
    nlohmann::json data = {{"start", "menu"}, {"nodes", {{"menu", {{"text", greeting}, {"choices", choices}}}}}};
    dlgNpc = npcId;
    openDialogue(data, DlgMode::Menu);
}

void Game::finishTalkEvent(const std::string& eventId) {
    if (!eventId.empty()) completeEvent(eventId);
}

bool Game::canRepeat(const std::string& eventId) const {
    auto def = eventDefs_.find(eventId);
    if (def == eventDefs_.end() || def->second.once) return false;
    return def->second.maxTimes <= 0 || run_.eventCount(eventId) < def->second.maxTimes;
}

bool Game::completeEvent(const std::string& eventId, int depth) {
    if (eventId.empty()) return false;
    const bool again = run_.eventFinished(eventId);
    if (again && !canRepeat(eventId)) return false;
    if (auto def = eventDefs_.find(eventId); def != eventDefs_.end()) runStoryActions(def->second.actions);
    run_.countEvent(eventId);
    if (!again) finishEvent(eventId, depth);   // the first run: flag, log, `next`, stairs
    else markProgressDirty();                  // a repeat: its actions only
    return true;
}

void Game::noticeEventTitle(const std::string& eventId) {
    auto def = eventDefs_.find(eventId);
    std::string title = def == eventDefs_.end() ? eventId : locale_.tr(def->second.titleKey);
    if (def != eventDefs_.end() && title == def->second.titleKey) title = eventId;
    notifications_.push_back({title, 3600});
}

void Game::wakeEvent(const std::string& eventId, int depth) {
    if (eventId.empty() || (run_.eventFinished(eventId) && !canRepeat(eventId))) return;
    run_.markEventStarted(eventId);
    auto def = eventDefs_.find(eventId);
    if (def == eventDefs_.end() || def->second.trigger != "chain" || depth > 32) return;   // 32: a loop in the data
    if (!holds(def->second.requires)) return;
    noticeEventTitle(eventId);   // no tile, no NPC: the player sees its title (decision, 2026-10-16)
    completeEvent(eventId, depth + 1);
}

void Game::fireLinkedEvents(const Entity& e) {
    for (const std::string& id : e.links) {
        auto def = eventDefs_.find(id);
        if (run_.eventFinished(id) && !canRepeat(id)) continue;
        if (def != eventDefs_.end() && def->second.trigger == "talk") { run_.markEventStarted(id); continue; }   // a topic
        if (def != eventDefs_.end() && !holds(def->second.requires)) { run_.markEventStarted(id); continue; }
        noticeEventTitle(id);
        completeEvent(id);
    }
    if (!e.links.empty()) markProgressDirty();
}

void Game::varChanged(const std::string& var) {
    if (var.empty() || varChangeDepth_ >= 16) return;   // 16: events that keep changing each other's variables
    ++varChangeDepth_;
    std::vector<std::string> fire;
    for (const auto& [id, d] : eventDefs_)
        if (d.trigger == "varChanged" && d.triggerVar == var) fire.push_back(id);
    for (const std::string& id : fire) {
        if (run_.eventFinished(id) && !canRepeat(id)) continue;
        if (!holds(eventDefs_[id].requires)) continue;
        noticeEventTitle(id);
        completeEvent(id);
    }
    --varChangeDepth_;
}

std::string Game::npcSprite(const std::string& npcId) const {
    auto it = npcDefs_.find(npcId);
    if (it != npcDefs_.end() && !it->second.sprite.empty() && spriteAtlas_.find(it->second.sprite)) return it->second.sprite;
    if (spriteAtlas_.find("npc_" + npcId)) return "npc_" + npcId;
    return entSprite(npcId);
}

void Game::runStoryActions(const nlohmann::json& actions) {
    if (actions.is_null()) return;
    // A local class has the member function's access: Game's own state is changed directly.
    struct Effects : toms::story::StoryEffects {
        Game& g;
        explicit Effects(Game& game) : g(game) {}
        int* stat(const std::string& s) {
            Player& p = g.pl;
            if (s == "hp") return &p.hp;
            if (s == "maxhp") return &p.maxhp;
            if (s == "atk") return &p.atk;
            if (s == "def") return &p.def;
            if (s == "gold") return &p.gold;
            if (s == "lv") return &p.lv;
            for (const AttrDef& a : g.attrDefs_) if (a.id == s) return &p.attrs[a.id];   // str, dex, ... (stats.json)
            return nullptr;
        }
        void note(const std::string& s, int delta) {
            std::string name = s;
            for (const AttrDef& a : g.attrDefs_) if (a.id == s && !a.shortName.empty()) name = a.shortName;
            if (name == s) { std::string up; for (char c : s) up += (char)std::toupper((unsigned char)c); name = up; }
            g.notifications_.push_back({name + (delta >= 0 ? " +" : " ") + std::to_string(delta), 3200});
        }
        bool addStat(const std::string& s, int delta) override {
            if (s == "exp") { if (delta > 0) g.gainExp(delta); else g.pl.exp = std::max(0, g.pl.exp + delta); note(s, delta); return true; }
            int* v = stat(s);
            if (!v) return false;
            *v = std::max(0, *v + delta);
            if (s == "hp") *v = std::max(1, std::min(*v, g.effectiveMaxHp()));
            note(s, delta);
            return true;
        }
        bool setStat(const std::string& s, int value) override {
            if (s == "exp") { g.pl.exp = std::max(0, value); return true; }
            int* v = stat(s);
            if (!v) return false;
            *v = std::max(0, value);
            if (s == "hp") *v = std::max(1, std::min(*v, g.effectiveMaxHp()));
            return true;
        }
        bool give(const std::string& item, int count) override {
            if (item.empty()) return false;
            for (int i = 0; i < count; ++i) g.receiveItem(item);
            std::string name = g.itemName(item);
            if (count > 1) name += " x" + std::to_string(count);
            g.notifications_.push_back({toms::fillText(g.locale_.tr("event.gotItem"), {{"item", name}}), 3800});
            return true;
        }
        bool take(const std::string& item, int count) override {
            Player& p = g.pl;
            int* key = item == "key_yellow" ? &p.key_yellow : item == "key_blue" ? &p.key_blue : item == "key_red" ? &p.key_red : nullptr;
            if (key) { if (*key < count) return false; *key -= count; return true; }
            if ((int)std::count(p.inv.begin(), p.inv.end(), item) < count) return false;
            for (int i = 0; i < count; ++i) p.inv.erase(std::find(p.inv.begin(), p.inv.end(), item));
            g.notifications_.push_back({toms::fillText(g.locale_.tr("event.gaveItem"), {{"item", g.itemName(item)}}), 3200});
            return true;
        }
        void setVar(const std::string& v, int value) override {
            const int before = g.run_.var(v);
            g.run_.setVar(v, value);
            if (g.run_.var(v) != before) g.varChanged(v);
        }
        void addVar(const std::string& v, int delta) override {
            const int before = g.run_.var(v);
            g.run_.addVar(v, delta);
            if (g.run_.var(v) != before) g.varChanged(v);
        }
        void setFlag(const std::string& f) override { if (!f.empty()) g.run_.setFlag(f, true); }
        void setStoryFlag(const std::string& f) override { if (!f.empty()) toms::setStoryFlag(g.meta_, f); }
        void addCounter(const std::string& c, int delta) override { if (!c.empty() && delta != 0) g.run_.addCounter(c, delta); }
        void fireEvent(const std::string& id) override { g.wakeEvent(id); }
        void message(const std::string& key) override { g.notifications_.push_back({g.locale_.tr(key), 3800}); }
        bool other(const nlohmann::json& a) override { g.runDialogueAction(a); return true; }
    } fx(*this);
    const toms::story::RunReport rep = toms::story::runActions(actions, fx);
    for (const std::string& p : rep.problems) fprintf(stderr, "[story] action failed: %s\n", p.c_str());
    markProgressDirty();
}

// Milestone 4: executes a dialogue choice's optional `action` verb. Kept as a small, explicit
// enum-style switch rather than a scripting hook, matching this project's existing preference
// (see docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.4's `talent` enum / dialogue's own `action.give`
// precedent in the design docs).
// ---- reward events (docs/gameplay/07 phase 6) ----

void Game::openMiniGame(const std::string& id) {
    const toms::MiniGameDef* def = miniGames_.find(id);
    if (!def) { std::fprintf(stderr, "[minigame] no \"%s\" in data/minigames.json\n", id.c_str()); return; }
    if (def->limit > 0 && run_.counter("minigame." + id) >= def->limit) {   // played out for this run
        notifications_.push_back({locale_.tr("minigame.used_up"), 3000});
        return;
    }
    miniGame_ = MiniGameState{};
    miniGame_.open = true;
    miniGame_.id = id;
    audio.play("dialogue_popup");
}

void Game::miniGameTap() {
    if (!miniGame_.open) return;
    const toms::MiniGameDef* def = miniGames_.find(miniGame_.id);
    if (!def) { closeMiniGame(); return; }
    if (miniGame_.phase == 0) {   // start
        miniGame_.play.start(def->play, 0.0f);
        miniGame_.phase = 1;
        audio.play("confirm_click");
        return;
    }
    if (miniGame_.phase == 1) {
        const auto t = miniGame_.play.tap();
        audio.play(t == toms::ComboPlay::Tap::Hit ? "confirm_click" : "deny");
        if (miniGame_.play.done()) finishMiniGame();
        return;
    }
    closeMiniGame();   // the result: done
}

void Game::finishMiniGame() {
    const toms::MiniGameDef* def = miniGames_.find(miniGame_.id);
    miniGame_.phase = 2;
    if (!def) return;
    miniGame_.reward = toms::miniGameReward(*def, miniGame_.play.result());
    if (def->currency == "crystals") pl.crystals += miniGame_.reward;
    else pl.gold += miniGame_.reward;
    run_.addCounter("minigame." + miniGame_.id, 1);
    audio.play(miniGame_.reward > 0 ? "get_item" : "deny");
    markProgressDirty();
}

void Game::closeMiniGame() {
    if (miniGame_.phase == 1) return;   // playing: the play ends by itself
    miniGame_ = MiniGameState{};
}

// ---- the start story: the weapon trial (docs/gameplay/07 phase 3) ----

void Game::startWeaponTrial() { startDialogue("weapon_master"); }

void Game::tryWeapon(const std::string& weaponId) {
    if (!equipmentDefs_.count(weaponId)) return;
    equipped_.weaponId = weaponId;   // worn for the trial; owned only when chosen
    endDialogue();
    arena_ = Arena{};
    arena_.on = true;
    arena_.trial = true;
    arena_.enemies = {"slime"};      // a training slime: weak, slow to strike back
    arena_.overrides = {{"hp", 40}, {"atk", 2}, {"atk_interval_ms", 5000}};
    arenaNextFight();
    cs.log = locale_.tr("trial.fight_log");
}

void Game::endWeaponTrialFight() {
    arena_ = Arena{};
    cs = CombatState{};
    pl.hp = effectiveMaxHp();
    startDialogue("weapon_master");
    if (inDialogue) { dlgNode = "after"; enterNode(dlgNode); }
}

void Game::chooseWeapon() {
    const std::string chosen = equipped_.weaponId.empty() ? std::string(kStartingWeapon) : equipped_.weaponId;
    gearOwned_.clear();              // only the chosen weapon goes with the player
    ownGear(chosen, true);
    newsGear_ = false;
    run_.setFlag("start.weapon." + chosen);
    endDialogue();
    notifications_.push_back({trParam(locale_.tr("trial.chosen"), "name", itemName(chosen)), 3500});
    markProgressDirty();
}

void Game::runDialogueAction(const nlohmann::json& action) {
    if (!action.is_object()) return;
    std::string type = action.value("type", std::string());
    if (type == "tryWeapon") { tryWeapon(action.value("weapon", std::string())); return; }   // docs/gameplay/07 phase 3
    if (type == "chooseWeapon") { chooseWeapon(); return; }
    if (type == "miniGame") { endDialogue(); openMiniGame(action.value("id", std::string())); return; }   // docs/gameplay/07 phase 6
    if (type == "give") {
        std::string itemId = action.value("itemId", std::string());
        if (!itemId.empty()) receiveItem(itemId);
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
            // M9: a main-line choice that's also a side story's payoff (e.g. ss_04's return/study
            // split) can bundle the skill grant and the side-story completion right here, reusing
            // the two verbs below instead of inventing a second action per dialogue choice.
            if (action.contains("grantSkill") && action["grantSkill"].is_string())
                runDialogueAction({{"type", "unlockSkill"}, {"skillId", action["grantSkill"]}});
            if (action.contains("completeSideStory") && action["completeSideStory"].is_string())
                runDialogueAction({{"type", "setSideStoryState"},
                                    {"sideStoryId", action["completeSideStory"]}, {"state", "completed"}});
        }
    } else if (type == "addCounter") {
        // S3: the three accumulators (insight / resolve / humanity). Clamped by RunStoryState to the
        // range declared in data/story/counters.json.
        std::string name = action.value("counter", std::string());
        int delta = action.value("delta", 0);
        if (!name.empty() && delta != 0) run_.addCounter(name, delta);
    } else if (type == "unlockSkill") {
        // M9: a dialogue-granted skill (e.g. a side story's reward) -- same free-grant path as
        // Game::applyChapterGrants' own "skills" loop, just reachable from a choice instead of a
        // chapter's grants block.
        std::string skillId = action.value("skillId", std::string());
        if (!skillId.empty() && !run_.hasSkill(skillId)) {
            auto it = skillDefs_.find(skillId);
            int cost = (it != skillDefs_.end()) ? it->second.cost : 0;
            run_.unlockSkill(skillId, cost);
            std::string name = it != skillDefs_.end() ? locale_.field(it->second.name) : skillId;
            pushNotification(locale_.tr("skill.unlocked_prefix") + name);
        }
    } else if (type == "setSideStoryState") {
        // M9: side-story progress was tracked in RunStoryState (S3) but nothing ever wrote to it
        // until now -- a dialogue choice is the natural place a side story actually resolves.
        std::string sideStoryId = action.value("sideStoryId", std::string());
        std::string state = action.value("state", std::string());
        if (!sideStoryId.empty() && !state.empty()) run_.setSideStoryState(sideStoryId, state);
    }
}

// S4: applies a chapter's `grants` exactly once -- see the declaration in game.h for why this
// exists at all (nothing has ever read a chapter file at runtime before this).
void Game::applyChapterGrants(const std::string& chapterId) {
    if (chapterId.empty()) return;
    std::string grantedFlag = "_chapter_granted." + chapterId;
    if (run_.flag(grantedFlag)) return;   // idempotent: revisiting ch_01's floors must not re-grant
    nlohmann::json ch = readJsonFile(pathFor(toms::pattern::kChapter, chapterId));
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
    ownGear(def.resultEquipmentId, true);   // forged gear is owned and worn at once
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
    std::vector<MainMenuRow> rows = { MainMenuRow::Store, MainMenuRow::Skills };
    if (forgeMenuUnlocked()) rows.push_back(MainMenuRow::Forge);
    if (hubMenuUnlocked())   rows.push_back(MainMenuRow::Village);
    rows.push_back(MainMenuRow::Save);
    rows.push_back(MainMenuRow::Settings);
    if (fullscreenAvailable_) rows.push_back(MainMenuRow::Fullscreen);   // not on Android: always full screen
    rows.push_back(MainMenuRow::BackToTitle);
    return rows;
}

int Game::mainMenuRowIndex(MainMenuRow r) const {
    auto order = mainMenuOrder();
    auto it = std::find(order.begin(), order.end(), r);
    return it == order.end() ? 0 : (int)std::distance(order.begin(), it);
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
        pl.gold += def.rewardGold;
        // Mirrors movePlayer()'s item-pickup split: keys/coins apply immediately, everything
        // else (gems/potions/exp/scroll) goes into the inventory to be used later, not consumed
        // on the spot -- a reward potion should sit in the backpack like a picked-up one would.
        if (!def.rewardItemId.empty()) receiveItem(def.rewardItemId);
        gainExp(def.rewardExp);
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
    // listen(), not subscribe(): the global bus outlives this Game (the editor restarts the game for every scenario),
    // so the handlers must go with it -- busSubs_ removes them when the Game is destroyed.
    busSubs_.clear();   // loadAssets again on the same Game: never twice
    busSubs_.push_back(toms::globalEventBus().listen<toms::EnemyDefeated>([this](const toms::EnemyDefeated& e) {
        for (auto& [id, tracker] : missionTrackers_) {
            auto it = missionDefs_.find(id);
            if (it != missionDefs_.end()) toms::applyProgressEvent(tracker, it->second, "defeat", e.enemyId);
        }
    }));
    busSubs_.push_back(toms::globalEventBus().listen<toms::ItemCollected>([this](const toms::ItemCollected& e) {
        for (auto& [id, tracker] : missionTrackers_) {
            auto it = missionDefs_.find(id);
            if (it != missionDefs_.end()) toms::applyProgressEvent(tracker, it->second, "collect", e.itemId);
        }
    }));
}

// M7 (first slice): the one path that can end a run. Not re-validated against the resolver here
// (unlike tryUnlockSkill/tryCraft/activateHubLocation) -- the caller (finishCombatLose(), the only
// call site today) already re-checked checkNamedEndings() itself, and there is no player-facing
// "activate an ending" action to guard against a stale/duplicate click the way those three do.
void Game::triggerEnding(const std::string& endingId) {
    activeEndingId_ = endingId;
    if (std::find(meta_.endingsSeen.begin(), meta_.endingsSeen.end(), endingId) == meta_.endingsSeen.end())
        meta_.endingsSeen.push_back(endingId);
    saveCurrentRun();   // a run ending is significant enough to flush immediately, not just markProgressDirty()
}

bool Game::rebirthOffered() const {
    if (!endingActive()) return false;
    if (meta_.cycleIndex >= cyclesConfig_.maxCycles) return false;   // STORY_BIBLE.md §8: capped at 9
    for (auto& e : endingsTable_.endings)
        if (e.id == activeEndingId_) return e.allowsRebirth;
    return false;   // an unknown ending id (content bug) never offers a rebirth it can't carry out
}

// The "start over" exit -- always available, and the only one when rebirthOffered() is false.
void Game::dismissEndingScreen() {
    if (!endingActive()) return;
    activeEndingId_.clear();
    returnToTitle();
}

// M8 (first slice): docs/story/STORY_BIBLE.md §8.1's "入輪迴" -- carries skills (at half effect --
// see skillEffectScale()), memory shards (already reset(keepShards=true)'s job), skill points and
// the Super gauge cap (both halved-and-floored), and cycleIndex (+1) forward; resets everything
// else (equipment to the starting weapon, consumables/materials/gold/keys, main-line flags/choices/
// counters/side-stories, floor back to F01). Forge blueprints and known actives need no code here
// at all -- they already live on meta_, which this never touches except cycleIndex/endingsSeen.
bool Game::rebirth() {
    if (!rebirthOffered()) return false;

    // Capture what carries forward BEFORE the wipe below destroys it.
    std::vector<std::string> keptSkills = run_.skillsOwned();
    int halvedSkillPoints = run_.skillPoints() / 2;               // floor: non-negative int division
    int halvedSuperMax = std::max(1, run_.superMax() / 2);
    int oldAtkBonus = pl.atk - kStartingAtk, oldDefBonus = pl.def - kStartingDef;

    run_.reset(/*keepShards=*/true);   // the full wipe (see its own comment); shards survive
    for (auto& id : keptSkills) run_.unlockSkill(id, 0);   // restore ownership at zero cost
    run_.addSkillPoints(halvedSkillPoints);
    run_.setSuperMax(halvedSuperMax);

    meta_.cycleIndex += 1;

    pl.atk = kStartingAtk + oldAtkBonus / 2;
    pl.def = kStartingDef + oldDefBonus / 2;
    pl.maxhp = kStartingHp; pl.hp = kStartingHp;
    pl.gold = 0; pl.exp = 0; pl.lv = 1;
    pl.crystals = 0;
    pl.key_yellow = pl.key_blue = pl.key_red = 0;
    pl.inv.clear();
    resetAttrs();
    equipped_ = toms::EquippedSet{};
    gearOwned_.clear();
    weaponUses_.clear();
    ownGear(kStartingWeapon, true);

    activeEndingId_.clear();
    entityStatus_.clear();
    missionTrackers_.clear();
    loadStage("F01");
    saveCurrentRun();
    return true;
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
