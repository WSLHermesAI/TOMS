// talk_system.cpp -- see talk_system.h.
#include "talk_system.h"

#include <algorithm>
#include <set>

namespace toms::talk {

namespace {
std::string str(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
} // namespace

bool parseNpc(const nlohmann::json& j, NpcDef& out, std::string* err) {
    auto fail = [&](const char* why) { if (err) *err = why; return false; };
    if (!j.is_object()) return fail("not an object");
    out = NpcDef{};
    out.id = str(j, "npcId");
    if (out.id.empty()) return fail("no npcId");
    out.nameKey = str(j, "name");
    out.greetingKey = str(j, "greeting");
    out.sprite = str(j, "sprite");
    if (j.contains("look") && j["look"].is_object()) out.look = j["look"];
    const nlohmann::json chat = j.value("chat", nlohmann::json::object());
    if (chat.contains("lines") && chat["lines"].is_array())
        for (const auto& l : chat["lines"]) {
            ChatLine line;
            if (l.is_string()) line.dialogue = l.get<std::string>();
            else if (l.is_object()) {
                line.dialogue = str(l, "dialogue");
                if (l.contains("requires")) line.requires = l["requires"];
                if (l.contains("actions")) line.actions = l["actions"];
            }
            if (!line.dialogue.empty()) out.lines.push_back(line);
        }
    if (out.lines.empty()) return fail("no chat line");
    const std::string order = str(chat, "order");
    out.order = order == "stay" ? ChatOrder::Stay : order == "random" ? ChatOrder::Random : ChatOrder::Loop;
    if (j.contains("topics") && j["topics"].is_object()) out.menuAlways = str(j["topics"], "menu") == "always";
    return true;
}

NpcDef defaultNpc(const std::string& npcId, const std::string& dialogue) {
    NpcDef d;
    d.id = npcId;
    d.lines.push_back({dialogue, nlohmann::json(), nlohmann::json()});
    return d;
}

std::map<std::string, std::vector<Topic>> collectTopics(const std::vector<nlohmann::json>& pools) {
    std::map<std::string, std::vector<Topic>> out;
    std::set<std::string> woken;   // events some other event names in `next`
    for (const auto& pool : pools)
        if (pool.is_array())
            for (const auto& e : pool)
                if (e.is_object() && e.contains("next") && e["next"].is_array())
                    for (const auto& n : e["next"]) if (n.is_string()) woken.insert(n.get<std::string>());
    int seq = 0;
    for (const auto& pool : pools) {
        if (!pool.is_array()) continue;
        for (const auto& e : pool) {
            if (!e.is_object()) continue;
            ++seq;
            const nlohmann::json trig = e.value("trigger", nlohmann::json());
            if (!trig.is_object() || str(trig, "on") != "talk") continue;
            Topic t;
            t.eventId = str(e, "eventId");
            t.npc = str(trig, "npc");
            if (t.eventId.empty() || t.npc.empty()) continue;
            t.titleKey = e.value("title", t.eventId + ".title");
            t.dialogue = str(e, "dialogue");
            t.afterDialogue = str(e, "afterDialogue");
            if (e.contains("requires")) t.requires = e["requires"];
            if (e.contains("topicOrder") && e["topicOrder"].is_number_integer()) t.order = e["topicOrder"].get<int>();
            t.repeatable = !e.value("once", true);
            if (e.contains("maxTimes") && e["maxTimes"].is_number_integer()) t.maxTimes = std::max(0, e["maxTimes"].get<int>());
            t.seq = seq;
            t.needsWake = woken.count(t.eventId) > 0;
            out[t.npc].push_back(t);
        }
    }
    for (auto& [npc, list] : out)
        std::stable_sort(list.begin(), list.end(), [](const Topic& a, const Topic& b) {
            return a.order != b.order ? a.order < b.order : a.seq < b.seq;
        });
    return out;
}

std::vector<MenuEntry> menuTopics(const std::vector<Topic>& topics, const TalkQuery& q) {
    std::vector<MenuEntry> open, done;
    for (const Topic& t : topics) {
        const bool runsLeft = t.repeatable && (t.maxTimes == 0 || q.eventCount(t.eventId) < t.maxTimes);
        if (q.eventFinished(t.eventId) && !runsLeft) {
            if (!t.afterDialogue.empty()) done.push_back({t.eventId, true});
        } else if ((!t.needsWake || q.eventStarted(t.eventId)) && q.holds(t.requires)) {
            open.push_back({t.eventId, false});
        }
    }
    open.insert(open.end(), done.begin(), done.end());
    return open;
}

std::vector<int> availableLines(const NpcDef& npc, const TalkQuery& q) {
    std::vector<int> out;
    for (int i = 0; i < (int)npc.lines.size(); ++i)
        if (q.holds(npc.lines[i].requires)) out.push_back(i);
    return out;
}

int nextChatLine(const NpcDef& npc, NpcTalkState& state, TalkQuery& q) {
    const std::vector<int> avail = availableLines(npc, q);
    if (avail.empty()) return -1;
    const int n = (int)avail.size();
    if (n == 1) {   // one line: always it; the index still moves, so a line that joins later comes up in turn
        if (npc.order == ChatOrder::Loop) ++state.index;
        return avail[0];
    }
    if (npc.order == ChatOrder::Random) {
        int pick = q.random(n);
        if (avail[pick] == state.lastRandom) pick = (pick + 1 + q.random(n - 1)) % n;   // never twice in a row
        state.lastRandom = avail[pick];
        return avail[pick];
    }
    int at = state.index;
    if (npc.order == ChatOrder::Stay) at = std::min(at, n - 1);
    const int line = avail[at % n];
    if (npc.order == ChatOrder::Loop || state.index < n - 1) ++state.index;
    return line;
}

TalkPlan planTalk(const NpcDef& npc, const std::vector<Topic>& topics, NpcTalkState& state, TalkQuery& q) {
    TalkPlan plan;
    plan.menu = menuTopics(topics, q);
    const int open = (int)std::count_if(plan.menu.begin(), plan.menu.end(), [](const MenuEntry& m) { return !m.finished; });
    if (plan.menu.empty()) {
        plan.chatLine = nextChatLine(npc, state, q);
        plan.kind = plan.chatLine >= 0 ? TalkKind::Chat : TalkKind::Nothing;
    } else if (plan.menu.size() == 1 && open == 1 && !npc.menuAlways) {
        plan.kind = TalkKind::Topic;
        plan.topic = plan.menu[0];
        plan.menu.clear();
    } else {
        plan.kind = TalkKind::Menu;
    }
    ++state.count;
    return plan;
}

std::string startNode(const nlohmann::json& dialogue, const TalkQuery& q) {
    if (!dialogue.is_object() || !dialogue.contains("start")) return std::string();
    const nlohmann::json& s = dialogue["start"];
    if (s.is_string()) return s.get<std::string>();
    if (s.is_array())
        for (const auto& c : s)
            if (c.is_object() && q.holds(c.value("requires", nlohmann::json()))) return str(c, "node");
    return std::string();
}

} // namespace toms::talk
