// talk_system_test -- NPCs and their events (docs/event_editor/08_NPC_DIALOGUE.md): talk_system.h + story_actions.h.
//
//   * chat lines: one line, loop, stay, random (never twice in a row), a line joining when its condition comes true,
//     talkCountAtLeast seeing the talks before this one;
//   * topics: open / inactive / finished with and without an after-dialogue, woken by `next`, topicOrder, and when the
//     menu appears (decision 2: one open topic alone plays at once; next to a finished one, the menu);
//   * a dialogue's conditional start; the conditions varAtLeast / varEquals / talkCountAtLeast / onStage;
//   * story actions: every verb reaches the effects, failures are reported, checkActions finds bad data;
//   * the run save keeps event variables and talk state;
//   * the shipped data: every NPC file, talk event, dialogue and action is valid, and the village elder's five
//     events open and close in the order the story needs;
//   * the stage editor's per-object settings and the types' looks: on an object's cell, sprites in the atlas, clips that
//     exist, tints, linked events that exist.
// Run in assets/ (tests/CMakeLists.txt).
#include "condition.h"
#include "run_state.h"
#include "save_system.h"
#include "story_actions.h"
#include "talk_system.h"
#include "vfs.h"

#include <json.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int g_checks = 0, g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using nlohmann::json;
using namespace toms::talk;

// A run as the conditions see it: finished / started events, variables, counters, talk counts, the stage, choices.
struct FakeRun : toms::ConditionContext, TalkQuery {
    std::set<std::string> started, finished, cleared;
    std::map<std::string, int> vars, counters, talks, stats, counts;
    std::map<std::string, std::string> choices;
    std::string stage;
    std::vector<int> rolls;   // random(): these in turn, then 0
    // ConditionContext
    bool storyFlagSet(const std::string&) const override { return false; }
    int storyBeat() const override { return 0; }
    bool itemHeld(const std::string&, int) const override { return false; }
    int statValue(const std::string& s) const override { auto it = stats.find(s); return it == stats.end() ? 0 : it->second; }
    bool missionComplete(const std::string&) const override { return false; }
    bool missionActive(const std::string&) const override { return false; }
    bool stageCleared(const std::string& s) const override { return cleared.count(s) > 0; }
    bool runFlagSet(const std::string& f) const override { return f.rfind("event_", 0) == 0 && finished.count(f.substr(6)); }
    int counter(const std::string& c) const override { auto it = counters.find(c); return it == counters.end() ? 0 : it->second; }
    int varValue(const std::string& v) const override { auto it = vars.find(v); return it == vars.end() ? 0 : it->second; }
    int talkCount(const std::string& n) const override { auto it = talks.find(n); return it == talks.end() ? 0 : it->second; }
    std::string currentStage() const override { return stage; }
    bool choiceMade(const std::string& c, const std::string& o) const override { auto it = choices.find(c); return it != choices.end() && it->second == o; }
    // TalkQuery
    bool holds(const json& c) const override { return toms::evaluate(c, *this); }
    bool eventStarted(const std::string& id) const override { return started.count(id) || finished.count(id); }
    bool eventFinished(const std::string& id) const override { return finished.count(id) > 0; }
    int eventCount(const std::string& id) const override {
        auto it = counts.find(id);
        const int n = it == counts.end() ? 0 : it->second;
        return finished.count(id) ? std::max(n, 1) : n;
    }
    int random(int n) override {
        int r = 0;
        if (!rolls.empty()) { r = rolls.front(); rolls.erase(rolls.begin()); }
        return n <= 0 ? 0 : r % n;
    }
};

json readJson(const std::string& path) {
    std::string text;
    if (!toms::vfsReadAll(path, text)) return json();
    return json::parse(text, nullptr, false);
}

std::vector<std::string> jsonFiles(const std::string& dir) {
    std::vector<std::string> out;
    for (const std::string& n : toms::vfsListDir(dir))
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0) out.push_back(n);
    std::sort(out.begin(), out.end());
    return out;
}

NpcDef npcWith(std::vector<std::string> lines, ChatOrder order) {
    NpcDef n;
    n.id = "npc";
    for (auto& l : lines) n.lines.push_back({l, json(), json()});
    n.order = order;
    return n;
}

// Plays `talks` chat lines and returns their dialogue ids.
std::vector<std::string> chat(const NpcDef& npc, toms::NpcTalkState& st, FakeRun& run, int talks) {
    std::vector<std::string> out;
    for (int i = 0; i < talks; ++i) {
        const int l = nextChatLine(npc, st, run);
        out.push_back(l < 0 ? "-" : npc.lines[l].dialogue);
    }
    return out;
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (auto& x : v) s += (s.empty() ? "" : " ") + x;
    return s;
}

std::string menuIds(const std::vector<MenuEntry>& m) {
    std::string s;
    for (auto& e : m) s += (s.empty() ? "" : " ") + e.eventId + (e.finished ? "*" : "");
    return s;
}

void testChatLines() {
    FakeRun run;
    toms::NpcTalkState st;
    CHECK(joined(chat(npcWith({"A"}, ChatOrder::Loop), st, run, 4)) == "A A A A", "one line is always the first");
    st = {};
    CHECK(joined(chat(npcWith({"A", "B", "C"}, ChatOrder::Loop), st, run, 5)) == "A B C A B", "loop");
    st = {};
    CHECK(joined(chat(npcWith({"A", "B", "C"}, ChatOrder::Stay), st, run, 5)) == "A B C C C", "stay");
    st = {};
    run.rolls = {1, 1, 0, 2, 2};   // B, then B again would repeat -> moved on
    const std::vector<std::string> r = chat(npcWith({"A", "B", "C"}, ChatOrder::Random), st, run, 5);
    bool repeat = false;
    for (size_t i = 1; i < r.size(); ++i) repeat |= r[i] == r[i - 1];
    CHECK(!repeat, "random never plays a line twice in a row: %s", joined(r).c_str());

    // A line that needs 3 talks joins the rotation; conditions see the talks before this one.
    NpcDef n = npcWith({"A", "S"}, ChatOrder::Loop);
    n.lines[1].requires = {{"type", "talkCountAtLeast"}, {"npc", "npc"}, {"value", 3}};
    st = {};
    run = FakeRun{};
    std::vector<std::string> seen;
    for (int i = 0; i < 6; ++i) {
        run.talks["npc"] = st.count;
        const TalkPlan p = planTalk(n, {}, st, run);
        seen.push_back(p.kind == TalkKind::Chat ? n.lines[p.chatLine].dialogue : "?");
    }
    CHECK(joined(seen) == "A A A S A S", "a line needing 3 talks plays from the 4th talk: %s", joined(seen).c_str());
    CHECK(st.count == 6, "every talk is counted (%d)", st.count);
}

json event(const std::string& id, const std::string& npc, json extra = json::object()) {
    json e = {{"eventId", id}, {"trigger", {{"on", "talk"}, {"npc", npc}}}};
    for (auto& [k, v] : extra.items()) e[k] = v;
    return e;
}

void testTopics() {
    // The design's example (section 5): five events, three listed (two open, one finished), two not.
    const json pool = json::array({
        event("well", "v", {{"next", {"ledger"}}}),
        event("ledger", "v"),
        event("bell", "v", {{"requires", {{"type", "counterAtLeast"}, {"counter", "insight"}, {"min", 1}}}}),
        event("road", "v", {{"afterDialogue", "road_after"}}),
        event("wish", "v", {{"requires", {{"type", "stageCleared"}, {"stageId", "F10"}}}}),
        event("other", "w"),
        {{"eventId", "tile"}},   // not a talk event
    });
    const auto topics = collectTopics({pool});
    CHECK(topics.size() == 2 && topics.at("v").size() == 5 && topics.at("w").size() == 1, "topics grouped by NPC");
    CHECK(topics.at("v")[1].needsWake && !topics.at("v")[0].needsWake, "an event named in `next` waits to be woken");

    FakeRun run;
    run.counters["insight"] = 1;
    run.finished = {"road"};
    CHECK(menuIds(menuTopics(topics.at("v"), run)) == "well bell road*", "the example: %s", menuIds(menuTopics(topics.at("v"), run)).c_str());
    run.finished.insert("well");
    run.started.insert("ledger");
    CHECK(menuIds(menuTopics(topics.at("v"), run)) == "ledger bell road*",
          "finishing the well wakes the ledger; a finished event without an after-dialogue is hidden: %s",
          menuIds(menuTopics(topics.at("v"), run)).c_str());

    // topicOrder moves an event ahead of pool order.
    const auto ordered = collectTopics({json::array({event("a", "v"), event("b", "v", {{"topicOrder", 1}})})});
    CHECK(ordered.at("v")[0].eventId == "b", "topicOrder first");

    // When the menu appears (decision 2).
    NpcDef npc = npcWith({"chat"}, ChatOrder::Loop);
    npc.id = "v";
    toms::NpcTalkState st;
    FakeRun r2;
    const auto one = collectTopics({json::array({event("x", "v", {{"afterDialogue", "x_after"}}), event("y", "v")})});
    TalkPlan p = planTalk(npc, one.at("v"), st, r2);
    CHECK(p.kind == TalkKind::Menu && p.menu.size() == 2, "two open topics: the menu");
    r2.finished = {"y"};
    p = planTalk(npc, one.at("v"), st, r2);
    CHECK(p.kind == TalkKind::Topic && p.topic.eventId == "x", "one open topic, the other finished and hidden: plays at once");
    r2.finished = {"x"};
    p = planTalk(npc, one.at("v"), st, r2);
    CHECK(p.kind == TalkKind::Menu && menuIds(p.menu) == "y x*", "one open next to a finished one: the menu (%s)", menuIds(p.menu).c_str());
    // A repeatable topic ("once": false, maxTimes 2) is open again after finishing, until its runs are used up.
    const auto rep = collectTopics({json::array({event("r", "v", {{"once", false}, {"maxTimes", 2}, {"afterDialogue", "r_after"}}),
                                                 event("u", "v", {{"once", false}})})});
    FakeRun r3;
    r3.finished = {"r", "u"};
    r3.counts = {{"r", 1}, {"u", 40}};
    CHECK(menuIds(menuTopics(rep.at("v"), r3)) == "r u", "repeatable: open again after one run; no limit: always (%s)", menuIds(menuTopics(rep.at("v"), r3)).c_str());
    r3.counts["r"] = 2;
    CHECK(menuIds(menuTopics(rep.at("v"), r3)) == "u r*", "used up: listed as finished for its after-dialogue (%s)", menuIds(menuTopics(rep.at("v"), r3)).c_str());
    r2.finished = {"x", "y"};
    p = planTalk(npc, one.at("v"), st, r2);
    CHECK(p.kind == TalkKind::Menu && menuIds(p.menu) == "x*", "only a finished one with an after-dialogue: the menu");
    p = planTalk(npc, {}, st, r2);
    CHECK(p.kind == TalkKind::Chat, "no topics: a chat line");
    npc.menuAlways = true;
    r2.finished = {"y"};
    p = planTalk(npc, one.at("v"), st, r2);
    CHECK(p.kind == TalkKind::Menu, "menu \"always\"");
}

void testConditionsAndStart() {
    FakeRun run;
    run.vars["v"] = 1;
    run.talks["elder"] = 8;
    run.stage = "stage_11";
    CHECK(toms::evaluate({{"type", "varEquals"}, {"var", "v"}, {"value", true}}, run), "varEquals true = 1");
    CHECK(!toms::evaluate({{"type", "varEquals"}, {"var", "v"}, {"value", false}}, run), "varEquals false");
    CHECK(toms::evaluate({{"type", "varAtLeast"}, {"var", "v"}, {"value", 1}}, run), "varAtLeast");
    CHECK(toms::evaluate({{"type", "talkCountAtLeast"}, {"npc", "elder"}, {"value", 8}}, run), "talkCountAtLeast");
    CHECK(toms::evaluate({{"type", "onStage"}, {"stageId", "stage_11"}}, run), "onStage");
    const json d = {{"start", json::array({{{"node", "pay"}, {"requires", {{"type", "choiceMade"}, {"choiceId", "e"}, {"optionId", "pay"}}}},
                                           {{"node", "thanks"}}})}};
    CHECK(startNode(d, run) == "thanks", "conditional start: the fallback");
    run.choices["e"] = "pay";
    CHECK(startNode(d, run) == "pay", "conditional start: the choice made");
    CHECK(startNode({{"start", "root"}}, run) == "root", "plain start");
}

struct Recorder : toms::story::StoryEffects {
    std::vector<std::string> log;
    bool addStat(const std::string& s, int d) override { log.push_back("addStat " + s + " " + std::to_string(d)); return s != "nope"; }
    bool setStat(const std::string& s, int v) override { log.push_back("setStat " + s + " " + std::to_string(v)); return true; }
    bool give(const std::string& i, int c) override { log.push_back("give " + i + " " + std::to_string(c)); return true; }
    bool take(const std::string& i, int c) override { log.push_back("take " + i + " " + std::to_string(c)); return false; }
    void setVar(const std::string& v, int x) override { log.push_back("setVar " + v + " " + std::to_string(x)); }
    void addVar(const std::string& v, int d) override { log.push_back("addVar " + v + " " + std::to_string(d)); }
    void setFlag(const std::string& f) override { log.push_back("setFlag " + f); }
    void setStoryFlag(const std::string& f) override { log.push_back("setStoryFlag " + f); }
    void addCounter(const std::string& c, int d) override { log.push_back("addCounter " + c + " " + std::to_string(d)); }
    void fireEvent(const std::string& e) override { log.push_back("fireEvent " + e); }
    void message(const std::string& t) override { log.push_back("message " + t); }
};

void testActions() {
    Recorder r;
    const json acts = json::parse(R"([
        {"type": "addStat", "stat": "str", "delta": 2}, {"type": "setStat", "stat": "hp", "value": 50},
        {"type": "give", "itemId": "gem_atk"}, {"type": "take", "itemId": "potion_red", "count": 2},
        {"type": "setVar", "var": "seen", "value": true}, {"type": "addVar", "var": "n", "delta": 3},
        {"type": "setFlag", "flag": "f"}, {"type": "setStoryFlag", "flag": "s"},
        {"type": "addCounter", "counter": "insight", "delta": -1}, {"type": "fireEvent", "eventId": "ev_x"},
        {"type": "message", "text": "k"}, {"type": "addStat", "stat": "nope", "delta": 1}, {"type": "dance"}])");
    const toms::story::RunReport rep = toms::story::runActions(acts, r);
    CHECK(joined(r.log) == "addStat str 2 setStat hp 50 give gem_atk 1 take potion_red 2 setVar seen 1 addVar n 3 setFlag f "
                           "setStoryFlag s addCounter insight -1 fireEvent ev_x message k addStat nope 1",
          "every verb in order: %s", joined(r.log).c_str());
    CHECK(rep.ran == 10 && rep.problems.size() == 3, "ran %d, problems %zu (take, unknown stat, unknown verb)", rep.ran, rep.problems.size());
    Recorder one;
    toms::story::runActions({{"type", "give"}, {"itemId", "a"}}, one);
    CHECK(one.log.size() == 1, "a single action object runs too");

    toms::story::Catalog cat;
    cat.items = {"gem_atk"};
    cat.vars = {"seen"};
    const auto errs = toms::story::checkActions(json::parse(R"([
        {"type": "addStat", "stat": "luck", "delta": 1}, {"type": "addStat", "stat": "atk", "delta": "2"},
        {"type": "give", "itemId": "gem_x"}, {"type": "setVar", "var": "seen", "value": "yes"},
        {"type": "fly"}, {"type": "give", "itemId": "gem_atk", "count": 0}])"), cat, "t");
    CHECK(errs.size() == 6, "checkActions: %zu problems", errs.size());
    CHECK(toms::story::checkActions(json::parse(R"([{"type": "addStat", "stat": "atk", "delta": 2}, {"type": "startMission", "missionId": "m"}])"),
                                    cat, "t").empty(), "valid actions pass");
    CHECK(toms::story::grantsReward(json::parse(R"([{"type": "setVar", "var": "a", "value": 1}, {"type": "give", "itemId": "x"}])")), "a gift is a reward");
    CHECK(!toms::story::grantsReward(json::parse(R"([{"type": "setVar", "var": "a", "value": 1}])")), "a variable is not");
}

void testSave() {
    toms::RunStoryState run;
    run.setVarDecls({{"flag", {0, 0, 1}}, {"trust", {2, 0, 5}}});
    CHECK(run.var("trust") == 2 && run.var("unknown") == 0, "defaults");
    run.setVar("flag", 7);
    run.addVar("trust", 10);
    CHECK(run.var("flag") == 1 && run.var("trust") == 5, "clamped to the declared range");
    run.npcTalk("villager") = {3, 9, 1};
    toms::RunSaveData d;
    run.writeInto(d);
    const toms::RunSaveData back = toms::runFromJson(toms::toJson(d));
    toms::RunStoryState r2;
    r2.setVarDecls(run.varDecls());
    r2.readFrom(back);
    CHECK(r2.var("flag") == 1 && r2.var("trust") == 5, "variables survive a save");
    const toms::NpcTalkState t = r2.npcTalk("villager");
    CHECK(t.index == 3 && t.count == 9 && t.lastRandom == 1, "talk state survives a save");
    r2.reset();
    CHECK(r2.var("trust") == 2 && r2.npcTalk("villager").count == 0, "a new run starts over");
}

// ---------- the shipped data ----------

std::set<std::string> keysOf(const json& j) {
    std::set<std::string> s;
    if (j.is_object()) for (auto& [k, v] : j.items()) if (!k.empty() && k[0] != '_') s.insert(k);
    return s;
}

void testData() {
    // the catalogue every action is checked against
    toms::story::Catalog cat;
    cat.items = keysOf(readJson("data/items.json"));
    for (const char* k : {"key_yellow", "key_blue", "key_red"}) cat.items.insert(k);
    cat.vars = keysOf(readJson("data/story/vars.json"));
    cat.counters = keysOf(readJson("data/story/counters.json"));
    for (const auto& s : toms::story::knownStats()) cat.stats.insert(s);
    const json stats = readJson("data/stats.json");
    if (stats.contains("attributes")) for (const auto& a : stats["attributes"]) cat.stats.insert(a.value("id", std::string()));
    CHECK(cat.stats.count("str"), "STR is an attribute addStat can raise");

    std::vector<json> pools;
    std::map<std::string, json> events;
    for (const std::string& n : jsonFiles("data/events/"))
        if (n.rfind("pool_", 0) == 0) {
            const json p = readJson("data/events/" + n);
            pools.push_back(p.value("events", json::array()));
            for (const auto& e : pools.back()) { events[e.value("eventId", std::string())] = e; cat.events.insert(e.value("eventId", std::string())); }
        }
    for (const auto& [id, e] : events)
        for (const std::string& p : toms::story::checkActions(e.value("actions", json()), cat, id)) CHECK(false, "%s", p.c_str());

    // NPCs placed anywhere (tower floors and the old stages)
    std::set<std::string> placed;
    auto scan = [&](const std::string& path) {
        const json st = readJson(path);
        if (!st.contains("legend") || !st.contains("tiles")) return;
        std::string cells;
        for (const auto& row : st["tiles"]) if (row.is_string()) cells += row.get<std::string>();
        for (auto& [ch, sem] : st["legend"].items())
            if (sem.is_string() && sem.get<std::string>().rfind("npc:", 0) == 0 && cells.find(ch) != std::string::npos) placed.insert(sem.get<std::string>().substr(4));
    };
    for (const std::string& n : jsonFiles("data/story/floors/")) if (n.find(".stage.") != std::string::npos) scan("data/story/floors/" + n);
    for (const std::string& n : jsonFiles("data/stages/")) scan("data/stages/" + n);
    CHECK(placed.count("villager"), "the villager stands somewhere");

    // a dialogue file: it exists, every next / start node exists, every action is valid
    std::set<std::string> checked;
    auto checkDialogue = [&](const std::string& id, const char* role, bool repeatable) -> json {
        const json d = readJson("data/dialogue/" + id + ".json");
        CHECK(d.is_object() && d.contains("nodes"), "%s dialogue %s exists", role, id.c_str());
        if (!d.is_object() || !d.contains("nodes") || !checked.insert(id).second) return d;
        const json& nodes = d["nodes"];
        std::vector<std::string> starts;
        if (d["start"].is_string()) starts.push_back(d["start"]);
        else for (const auto& s : d["start"]) starts.push_back(s.value("node", std::string()));
        for (auto& s : starts) CHECK(nodes.contains(s), "%s: start node %s exists", id.c_str(), s.c_str());
        for (auto& [nid, node] : nodes.items())
            for (const auto& c : node.value("choices", json::array())) {
                // "next": "" closes the dialogue, like null
                if (c.contains("next") && c["next"].is_string() && !c["next"].get<std::string>().empty()) CHECK(nodes.contains(c["next"].get<std::string>()), "%s/%s: next %s exists", id.c_str(), nid.c_str(), c["next"].get<std::string>().c_str());
                for (const std::string& p : toms::story::checkActions(c.value("actions", json()), cat, id + "/" + nid)) CHECK(false, "%s", p.c_str());
                if (repeatable) CHECK(!toms::story::grantsReward(c.value("actions", json())) && !c.contains("finish"),
                                      "%s/%s: a repeatable dialogue gives no reward and finishes nothing", id.c_str(), nid.c_str());
                for (const auto& f : c.value("fire", json::array())) CHECK(events.count(f.get<std::string>()), "%s: fires a real event", id.c_str());
            }
        return d;
    };

    // NPC files
    std::map<std::string, NpcDef> npcs;
    for (const std::string& n : jsonFiles("data/npcs/")) {
        NpcDef d;
        std::string err;
        CHECK(parseNpc(readJson("data/npcs/" + n), d, &err), "npcs/%s: %s", n.c_str(), err.c_str());
        CHECK(d.id + ".json" == n, "npcs/%s: the file is named after its npcId", n.c_str());
        npcs[d.id] = d;
        for (const ChatLine& l : d.lines) {
            checkDialogue(l.dialogue, "chat", false);
            for (const std::string& p : toms::story::checkActions(l.actions, cat, n)) CHECK(false, "%s", p.c_str());
            CHECK(!toms::story::grantsReward(l.actions), "npcs/%s: a chat line (repeatable) gives no reward", n.c_str());
        }
    }
    for (const char* id : {"villager", "sorcerer", "king", "princess", "handmaiden", "skeleton_scholar"})
        CHECK(npcs.count(id), "every placed NPC has a file: %s", id);

    // repeatable and varChanged events: valid fields, a declared variable
    for (const auto& [id, e] : events) {
        if (e.contains("once")) CHECK(e["once"].is_boolean(), "%s: once is true / false", id.c_str());
        if (e.contains("maxTimes")) CHECK(e["maxTimes"].is_number_integer() && e["maxTimes"].get<int>() >= 0, "%s: maxTimes is 0 or more", id.c_str());
        if (e.contains("maxTimes")) CHECK(!e.value("once", true), "%s: maxTimes without \"once\": false does nothing", id.c_str());
        const json tr = e.value("trigger", json::object());
        if (tr.value("on", std::string()) == "varChanged")
            CHECK(cat.vars.count(tr.value("var", std::string())), "%s: fires when %s changes, which story/vars.json lacks", id.c_str(), tr.value("var", std::string()).c_str());
    }

    // talk events
    const auto topics = collectTopics(pools);
    for (const auto& [npc, list] : topics) {
        CHECK(placed.count(npc), "talk events' NPC %s is placed on a floor", npc.c_str());
        for (const Topic& t : list) {
            if (!t.dialogue.empty()) {
                const json d = checkDialogue(t.dialogue, "topic", false);
                bool finishes = d.is_object() && d.value("finishOnClose", false);
                const json nodes = d.is_object() ? d.value("nodes", json::object()) : json::object();
                for (auto& [nid, node] : nodes.items())
                    for (const auto& c : node.value("choices", json::array())) finishes |= c.value("finish", false);
                CHECK(finishes, "%s: its dialogue can finish it", t.eventId.c_str());
            }
            if (!t.afterDialogue.empty()) checkDialogue(t.afterDialogue, "after", true);
        }
    }

    // The village elder's story, played through the talk logic with a fake run.
    CHECK(topics.count("villager") && topics.at("villager").size() == 6, "the elder has six topics: his five worries and the potion trade");
    if (!topics.count("villager") || !npcs.count("villager")) return;
    const auto& elder = topics.at("villager");
    const NpcDef& villager = npcs.at("villager");
    FakeRun run;
    toms::NpcTalkState st;
    auto talk = [&]() { run.talks["villager"] = st.count; return planTalk(villager, elder, st, run); };
    TalkPlan p = talk();
    CHECK(p.kind == TalkKind::Menu && menuIds(p.menu) == "ev_elder_north_road ev_elder_well_night", "start: two topics (%s)", menuIds(p.menu).c_str());
    run.finished.insert("ev_elder_north_road");
    run.vars["elder_trust"] = 1;
    run.counters["insight"] = 1;   // the bell chain's reward
    p = talk();
    CHECK(menuIds(p.menu) == "ev_elder_well_night ev_elder_bell_rumour ev_elder_north_road*",
          "five events, three listed and two not (%s)", menuIds(p.menu).c_str());
    run.finished.insert("ev_elder_well_night");
    run.started.insert("ev_elder_ledger");   // woken by the well's `next`
    p = talk();
    CHECK(menuIds(p.menu) == "ev_elder_ledger ev_elder_bell_rumour ev_elder_north_road* ev_elder_well_night*",
          "the well wakes the ledger (%s)", menuIds(p.menu).c_str());
    run.finished.insert("ev_elder_ledger");
    run.finished.insert("ev_elder_bell_rumour");
    run.vars["elder_trust"] = 3;
    p = talk();
    CHECK(menuIds(p.menu).find("ev_elder_last_wish") == std::string::npos, "the last wish waits for F05");
    run.cleared.insert("F05");
    p = talk();
    CHECK(!p.menu.empty() && p.menu[0].eventId == "ev_elder_last_wish", "trust 3 and F05 cleared: the last wish (%s)", menuIds(p.menu).c_str());
    run.finished.insert("ev_elder_last_wish");
    p = talk();
    CHECK(menuIds(p.menu) == "ev_elder_potion ev_elder_north_road* ev_elder_well_night* ev_elder_bell_rumour*",
          "at the end: the potion trade and the three after-dialogues (%s)", menuIds(p.menu).c_str());
    run.counts["ev_elder_potion"] = 3;
    run.finished.insert("ev_elder_potion");
    p = talk();
    CHECK(menuIds(p.menu) == "ev_elder_north_road* ev_elder_well_night* ev_elder_bell_rumour*",
          "three potions bought: the trade is gone (%s)", menuIds(p.menu).c_str());

    // Chat lines: no topics -> the rotation; the secret joins after eight talks.
    FakeRun quiet;
    toms::NpcTalkState cs;
    std::vector<std::string> lines;
    for (int i = 0; i < 10; ++i) {
        quiet.talks["villager"] = cs.count;
        const TalkPlan c = planTalk(villager, {}, cs, quiet);
        lines.push_back(c.kind == TalkKind::Chat ? villager.lines[c.chatLine].dialogue : "?");
    }
    CHECK(lines[0] == "villager_elder" && lines[1] == "elder_chat_weather" && lines[2] == "villager_elder", "chat lines loop");
    CHECK(std::find(lines.begin(), lines.begin() + 8, "elder_secret_well") == lines.begin() + 8 &&
          std::find(lines.begin() + 8, lines.end(), "elder_secret_well") != lines.end(), "the well's secret only after eight talks: %s", joined(lines).c_str());
}

// ---------- the stage editor's per-object settings and the types' looks (docs/19_STAGE_EDITOR.md) ----------
void testObjects() {
    // the game atlas's sprite names (libGDX text: unindented lines without ':' after the header)
    std::set<std::string> sprites;
    {
        std::string text;
        if (toms::vfsReadAll("media/atlas/game.atlas", text)) {
            size_t a = 0; bool first = true;
            while (a < text.size()) {
                size_t b = text.find('\n', a); if (b == std::string::npos) b = text.size();
                std::string line = text.substr(a, b - a); if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!first && !line.empty() && line[0] != ' ' && line[0] != '\t' && line.find(':') == std::string::npos &&
                    line.find(".png") == std::string::npos) sprites.insert(line);
                first = false; a = b + 1;
            }
        }
    }
    CHECK(sprites.count("slime") && sprites.count("npc_king"), "the atlas's sprite names are read (%zu)", sprites.size());
    std::set<std::string> events;
    for (const std::string& n : jsonFiles("data/events/"))
        if (n.rfind("pool_", 0) == 0) for (const auto& e : readJson("data/events/" + n).value("events", json::array())) events.insert(e.value("eventId", std::string()));
    std::map<std::string, std::set<std::string>> clips;   // anim file -> its clip names
    auto clipOk = [&](const std::string& ref) {
        const size_t h = ref.find('#'); if (h == std::string::npos) return false;
        const std::string file = ref.substr(0, h);
        if (!clips.count(file)) { std::set<std::string>& s = clips[file];
            for (const auto& c : readJson("media/" + file).value("clips", json::array())) s.insert(c.value("name", std::string())); }
        return clips[file].count(ref.substr(h + 1)) > 0;
    };
    auto checkLook = [&](const json& look, const std::string& where) {
        if (!look.is_object()) { CHECK(look.is_null(), "%s: look is an object", where.c_str()); return; }
        if (look.contains("sprite")) { std::string s = look["sprite"].get<std::string>(); if (s.size() > 4 && s.compare(s.size() - 4, 4, ".png") == 0) s.resize(s.size() - 4);
            CHECK(sprites.count(s), "%s: sprite %s is in the game atlas", where.c_str(), s.c_str()); }
        if (look.contains("anim")) CHECK(look["anim"].is_string() && clipOk(look["anim"].get<std::string>()), "%s: clip %s exists", where.c_str(), look["anim"].dump().c_str());
        if (look.contains("tint")) { const json& t = look["tint"]; bool ok = t.is_array() && (t.size() == 3 || t.size() == 4);
            if (ok) for (const auto& v : t) ok = ok && v.is_number() && v.get<double>() >= 0 && v.get<double>() <= 4;
            CHECK(ok, "%s: tint is [r, g, b] or [r, g, b, a]", where.c_str()); }
    };
    // the types' looks
    const json enemiesJ = readJson("data/enemies.json"), itemsJ = readJson("data/items.json");   // named: .items() of a temporary dangles
    for (auto& [id, e] : enemiesJ.items()) if (e.is_object() && e.contains("look")) checkLook(e["look"], "enemies.json " + id);
    for (auto& [id, e] : itemsJ.items()) if (e.is_object() && e.contains("look")) checkLook(e["look"], "items.json " + id);
    for (const std::string& n : jsonFiles("data/npcs/")) { const json f = readJson("data/npcs/" + n); if (f.contains("look")) checkLook(f["look"], "npcs/" + n); }
    // every stage's per-object settings: on an object's cell, a valid look, real events
    std::vector<std::string> stages;
    for (const std::string& n : jsonFiles("data/story/floors/")) if (n.find(".stage.") != std::string::npos) stages.push_back("data/story/floors/" + n);
    for (const std::string& n : jsonFiles("data/stages/")) stages.push_back("data/stages/" + n);
    stages.push_back("../tests/stage/objects.stage.json");
    int objects = 0;
    for (const std::string& path : stages) {
        const json st = readJson(path);
        if (!st.is_object() || !st.contains("objects")) continue;
        const json& legend = st["legend"];
        for (auto& [key, o] : st["objects"].items()) {
            ++objects;
            const std::string where = path + " " + key;
            int x = -1, y = -1; std::sscanf(key.c_str(), "%d,%d", &x, &y);
            const bool in = y >= 0 && y < (int)st["tiles"].size() && x >= 0 && x < (int)st["tiles"][y].get<std::string>().size();
            CHECK(in, "%s: the cell is on the map", where.c_str());
            if (in) { const std::string c(1, st["tiles"][y].get<std::string>()[x]);
                CHECK(c != "." && c != "#" && legend.contains(c), "%s: an object stands on the cell ('%s')", where.c_str(), c.c_str()); }
            checkLook(o.value("look", json()), where);
            for (const auto& ev : o.value("events", json::array())) CHECK(events.count(ev.get<std::string>()), "%s: event %s exists", where.c_str(), ev.get<std::string>().c_str());
        }
    }
    CHECK(objects >= 4, "the test stage's objects are checked (%d)", objects);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        testChatLines();
        testTopics();
        testConditionsAndStart();
        testActions();
        testSave();
        testData();
        testObjects();
    } catch (const std::exception& e) {   // bad data must fail the test with a reason, not abort it
        std::printf("  FAIL: exception: %s\n", e.what());
        ++g_failed;
    }
    std::printf("talk_system_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
