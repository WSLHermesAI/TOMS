// talk_system.h -- what an NPC does when the player talks to it (docs/gameplay/event_editor/08_NPC_DIALOGUE.md).
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
    std::string m_strDialogue;        // a dialogue file id (data/dialogue/<id>.json)
    nlohmann::json m_jRequires;     // null = always available
    nlohmann::json m_jActions;      // null = none; run each time the line plays
};

struct NpcDef {
    std::string m_strId, m_strNameKey, m_strGreetingKey;
    std::string m_strSprite;          // its map sprite (an atlas sprite name); empty: npc_<id>
    nlohmann::json m_jLook;         // {"sprite", "anim", "tint"} (the stage editor's palette detail); null = none
    std::vector<ChatLine> m_vLines;
    ChatOrder m_eOrder = ChatOrder::Loop;
    bool m_bMenuAlways = false;     // topics.menu "always": the menu even for a single open topic
};

// Reads an NPC file. False (with a reason) when it has no id or no chat line.
bool f_ParseNpc(const nlohmann::json& l_jJ, NpcDef& l_oOut, std::string* l_pErr = nullptr);
// The NPC used when there is no NPC file: one chat line, the dialogue file of the same id.
NpcDef f_DefaultNpc(const std::string& l_strNpcId, const std::string& l_strDialogue);

// An event bound to an NPC, as the menu needs it.
struct Topic {
    std::string m_strEventId, m_strNpc, m_strTitleKey;
    std::string m_strDialogue;        // empty: the event's text as one page
    std::string m_strAfterDialogue;   // empty: hidden once finished
    nlohmann::json m_jRequires;
    int m_iOrder = INT_MAX;         // "topicOrder" (lower first)
    int m_iSeq = 0;                 // pool order: pools by file name, then the order inside the file
    bool m_bNeedsWake = false;      // another event names it in `next`: it waits until woken (started)
    bool m_bRepeatable = false;     // "once": false -- open again after finishing, until maxTimes runs (0 = no limit)
    int m_iMaxTimes = 0;
};

// The talk-bound events of every pool record ({"eventId", "trigger", "dialogue", ...}), grouped by NPC and sorted
// in menu order. `pools` is the pool files' "events" arrays in file-name order.
std::map<std::string, std::vector<Topic>> f_CollectTopics(const std::vector<nlohmann::json>& l_vPools);

// What talk logic asks about the run.
class TalkQuery {
public:
    virtual ~TalkQuery() = default;
    virtual bool f_Holds(const nlohmann::json& l_jCondition) const = 0;
    virtual bool f_EventStarted(const std::string& l_strEventId) const = 0;
    virtual bool f_EventFinished(const std::string& l_strEventId) const = 0;
    virtual int f_Random(int l_iN) = 0;   // 0..n-1
    virtual int f_EventCount(const std::string& /*eventId*/) const { return 0; }   // runs so far (repeatable events)
};

struct MenuEntry {
    std::string m_strEventId;
    bool m_bFinished = false;       // listed for its after-dialogue
};

// The topics the menu lists now: open ones first, then finished ones with an after-dialogue, each in menu order.
// A repeatable topic is open again after finishing while it has runs left; used up, it is a finished one.
std::vector<MenuEntry> f_MenuTopics(const std::vector<Topic>& l_vTopics, const TalkQuery& l_oQ);

// The chat lines available now (indexes into npc.lines, in order).
std::vector<int> f_AvailableLines(const NpcDef& l_oNpc, const TalkQuery& l_oQ);
// The chat line to play now, and the talk index moved on (section 4). -1 when no line is available.
int f_NextChatLine(const NpcDef& l_oNpc, NpcTalkState& l_oState, TalkQuery& l_oQ);

enum class TalkKind { Nothing, Chat, Topic, Menu };
struct TalkPlan {
    TalkKind m_eKind = TalkKind::Nothing;
    int m_iChatLine = -1;               // Chat: the line (already moved past)
    MenuEntry m_oTopic;                 // Topic: the one to open
    std::vector<MenuEntry> m_vMenu;     // Menu: the topics to list
};
// One talk (section 5): counts it, then decides. Conditions see the count of the talks BEFORE this one, so a line
// that needs 8 talks plays from the 9th.
TalkPlan f_PlanTalk(const NpcDef& l_oNpc, const std::vector<Topic>& l_vTopics, NpcTalkState& l_oState, TalkQuery& l_oQ);

// A dialogue file's first node: "start" is a node id, or a list of {"node", "requires"} (the first that holds; the
// last one should have no `requires`). Empty when none applies.
std::string f_StartNode(const nlohmann::json& l_jDialogue, const TalkQuery& l_oQ);

} // namespace toms::talk
