// talk_system.h -- what an NPC does when the player talks to it (docs/event_editor/08_NPC_DIALOGUE.md).
//
//   NPC file (data/npcs/<id>.json)   its name, greeting, chat lines (rotating with a talk index) and menu rule
//   topics                           events bound to the NPC ("trigger": {"on": "talk", "npc": id}), offered
//                                    while their `requires` holds and they are not finished; a finished one stays
//                                    listed (marked done) only when it has an `afterDialogue`
//   planTalk                         one talk: a chat line, one topic straight away, or the topic menu
//
// The same pieces connect any story: an event wakes the next one (`next`, a choice's `fire`), conditions read the
// run (finished events, variables, counters, talk counts, choices made), and every result is a story action
// (story_actions.h). Pure logic, no Game dependency: the game answers TalkQuery; talk_system_test.cpp checks it
// with a fake run and with the shipped data.
#pragma once

#include "run_state.h"   // toms::NpcTalkState

#include <json.hpp>

#include <climits>
#include <map>
#include <string>
#include <vector>

namespace toms::talk {

enum class ChatOrder { Loop, Stay, Random };

struct ChatLine {
    std::string dialogue;        // a dialogue file id (data/dialogue/<id>.json)
    nlohmann::json requires;     // null = always available
    nlohmann::json actions;      // null = none; run each time the line plays
};

struct NpcDef {
    std::string id, nameKey, greetingKey;
    std::string sprite;          // its map sprite (an atlas sprite name); empty: npc_<id>
    std::vector<ChatLine> lines;
    ChatOrder order = ChatOrder::Loop;
    bool menuAlways = false;     // topics.menu "always": the menu even for a single open topic
};

// Reads an NPC file. False (with a reason) when it has no id or no chat line.
bool parseNpc(const nlohmann::json& j, NpcDef& out, std::string* err = nullptr);
// The NPC used when there is no NPC file: one chat line, the dialogue file of the same id.
NpcDef defaultNpc(const std::string& npcId, const std::string& dialogue);

// An event bound to an NPC, as the menu needs it.
struct Topic {
    std::string eventId, npc, titleKey;
    std::string dialogue;        // empty: the event's text as one page
    std::string afterDialogue;   // empty: hidden once finished
    nlohmann::json requires;
    int order = INT_MAX;         // "topicOrder" (lower first)
    int seq = 0;                 // pool order: pools by file name, then the order inside the file
    bool needsWake = false;      // another event names it in `next`: it waits until woken (started)
    bool repeatable = false;     // "once": false -- open again after finishing, until maxTimes runs (0 = no limit)
    int maxTimes = 0;
};

// The talk-bound events of every pool record ({"eventId", "trigger", "dialogue", ...}), grouped by NPC and sorted
// in menu order. `pools` is the pool files' "events" arrays in file-name order.
std::map<std::string, std::vector<Topic>> collectTopics(const std::vector<nlohmann::json>& pools);

// What talk logic asks about the run.
class TalkQuery {
public:
    virtual ~TalkQuery() = default;
    virtual bool holds(const nlohmann::json& condition) const = 0;
    virtual bool eventStarted(const std::string& eventId) const = 0;
    virtual bool eventFinished(const std::string& eventId) const = 0;
    virtual int random(int n) = 0;   // 0..n-1
    virtual int eventCount(const std::string& /*eventId*/) const { return 0; }   // runs so far (repeatable events)
};

struct MenuEntry {
    std::string eventId;
    bool finished = false;       // listed for its after-dialogue
};

// The topics the menu lists now: open ones first, then finished ones with an after-dialogue, each in menu order.
// A repeatable topic is open again after finishing while it has runs left; used up, it is a finished one.
std::vector<MenuEntry> menuTopics(const std::vector<Topic>& topics, const TalkQuery& q);

// The chat lines available now (indexes into npc.lines, in order).
std::vector<int> availableLines(const NpcDef& npc, const TalkQuery& q);
// The chat line to play now, and the talk index moved on (section 4). -1 when no line is available.
int nextChatLine(const NpcDef& npc, NpcTalkState& state, TalkQuery& q);

enum class TalkKind { Nothing, Chat, Topic, Menu };
struct TalkPlan {
    TalkKind kind = TalkKind::Nothing;
    int chatLine = -1;               // Chat: the line (already moved past)
    MenuEntry topic;                 // Topic: the one to open
    std::vector<MenuEntry> menu;     // Menu: the topics to list
};
// One talk (section 5): counts it, then decides. Conditions see the count of the talks BEFORE this one, so a line
// that needs 8 talks plays from the 9th.
TalkPlan planTalk(const NpcDef& npc, const std::vector<Topic>& topics, NpcTalkState& state, TalkQuery& q);

// A dialogue file's first node: "start" is a node id, or a list of {"node", "requires"} (the first that holds; the
// last one should have no `requires`). Empty when none applies.
std::string startNode(const nlohmann::json& dialogue, const TalkQuery& q);

} // namespace toms::talk
