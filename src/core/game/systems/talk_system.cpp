// talk_system.cpp -- see talk_system.h.
#include "talk_system.h"

#include <algorithm>
#include <set>

namespace toms::talk {

namespace {
std::string f_Str(const nlohmann::json& l_jJ, const char* l_pszKey) {
    auto l_oIt = l_jJ.find(l_pszKey);
    return l_oIt != l_jJ.end() && l_oIt->is_string() ? l_oIt->get<std::string>() : std::string();
}
} // namespace

bool f_ParseNpc(const nlohmann::json& l_jJ, NpcDef& l_oOut, std::string* l_pErr) {
    auto l_fnFail = [&](const char* l_pszWhy) { if (l_pErr) *l_pErr = l_pszWhy; return false; };
    if (!l_jJ.is_object()) return l_fnFail("not an object");
    l_oOut = NpcDef{};
    l_oOut.m_strId = f_Str(l_jJ, "npcId");
    if (l_oOut.m_strId.empty()) return l_fnFail("no npcId");
    l_oOut.m_strNameKey = f_Str(l_jJ, "name");
    l_oOut.m_strGreetingKey = f_Str(l_jJ, "greeting");
    l_oOut.m_strSprite = f_Str(l_jJ, "sprite");
    if (l_jJ.contains("look") && l_jJ["look"].is_object()) l_oOut.m_jLook = l_jJ["look"];
    const nlohmann::json l_jChat = l_jJ.value("chat", nlohmann::json::object());
    if (l_jChat.contains("lines") && l_jChat["lines"].is_array())
        for (const auto& l_jL : l_jChat["lines"]) {
            ChatLine l_oLine;
            if (l_jL.is_string()) l_oLine.m_strDialogue = l_jL.get<std::string>();
            else if (l_jL.is_object()) {
                l_oLine.m_strDialogue = f_Str(l_jL, "dialogue");
                if (l_jL.contains("requires")) l_oLine.m_jRequires = l_jL["requires"];
                if (l_jL.contains("actions")) l_oLine.m_jActions = l_jL["actions"];
            }
            if (!l_oLine.m_strDialogue.empty()) l_oOut.m_vLines.push_back(l_oLine);
        }
    if (l_oOut.m_vLines.empty()) return l_fnFail("no chat line");
    const std::string l_strOrder = f_Str(l_jChat, "order");
    l_oOut.m_eOrder = l_strOrder == "stay" ? ChatOrder::Stay : l_strOrder == "random" ? ChatOrder::Random : ChatOrder::Loop;
    if (l_jJ.contains("topics") && l_jJ["topics"].is_object()) l_oOut.m_bMenuAlways = f_Str(l_jJ["topics"], "menu") == "always";
    return true;
}

NpcDef f_DefaultNpc(const std::string& l_strNpcId, const std::string& l_strDialogue) {
    NpcDef l_oD;
    l_oD.m_strId = l_strNpcId;
    l_oD.m_vLines.push_back({l_strDialogue, nlohmann::json(), nlohmann::json()});
    return l_oD;
}

std::map<std::string, std::vector<Topic>> f_CollectTopics(const std::vector<nlohmann::json>& l_vPools) {
    std::map<std::string, std::vector<Topic>> l_mapOut;
    std::set<std::string> l_setWoken;   // events some other event names in `next`
    for (const auto& l_jPool : l_vPools)
        if (l_jPool.is_array())
            for (const auto& l_jE : l_jPool)
                if (l_jE.is_object() && l_jE.contains("next") && l_jE["next"].is_array())
                    for (const auto& l_jN : l_jE["next"]) if (l_jN.is_string()) l_setWoken.insert(l_jN.get<std::string>());
    int l_iSeq = 0;
    for (const auto& l_jPool : l_vPools) {
        if (!l_jPool.is_array()) continue;
        for (const auto& l_jE : l_jPool) {
            if (!l_jE.is_object()) continue;
            ++l_iSeq;
            const nlohmann::json l_jTrig = l_jE.value("trigger", nlohmann::json());
            if (!l_jTrig.is_object() || f_Str(l_jTrig, "on") != "talk") continue;
            Topic l_oT;
            l_oT.m_strEventId = f_Str(l_jE, "eventId");
            l_oT.m_strNpc = f_Str(l_jTrig, "npc");
            if (l_oT.m_strEventId.empty() || l_oT.m_strNpc.empty()) continue;
            l_oT.m_strTitleKey = l_jE.value("title", l_oT.m_strEventId + ".title");
            l_oT.m_strDialogue = f_Str(l_jE, "dialogue");
            l_oT.m_strAfterDialogue = f_Str(l_jE, "afterDialogue");
            if (l_jE.contains("requires")) l_oT.m_jRequires = l_jE["requires"];
            if (l_jE.contains("topicOrder") && l_jE["topicOrder"].is_number_integer()) l_oT.m_iOrder = l_jE["topicOrder"].get<int>();
            l_oT.m_bRepeatable = !l_jE.value("once", true);
            if (l_jE.contains("maxTimes") && l_jE["maxTimes"].is_number_integer()) l_oT.m_iMaxTimes = std::max(0, l_jE["maxTimes"].get<int>());
            l_oT.m_iSeq = l_iSeq;
            l_oT.m_bNeedsWake = l_setWoken.count(l_oT.m_strEventId) > 0;
            l_mapOut[l_oT.m_strNpc].push_back(l_oT);
        }
    }
    for (auto& [l_strNpc, l_vList] : l_mapOut)
        std::stable_sort(l_vList.begin(), l_vList.end(), [](const Topic& l_oA, const Topic& l_oB) {
            return l_oA.m_iOrder != l_oB.m_iOrder ? l_oA.m_iOrder < l_oB.m_iOrder : l_oA.m_iSeq < l_oB.m_iSeq;
        });
    return l_mapOut;
}

std::vector<MenuEntry> f_MenuTopics(const std::vector<Topic>& l_vTopics, const TalkQuery& l_oQ) {
    std::vector<MenuEntry> l_vOpen, l_vDone;
    for (const Topic& l_oT : l_vTopics) {
        const bool l_bRunsLeft = l_oT.m_bRepeatable && (l_oT.m_iMaxTimes == 0 || l_oQ.f_EventCount(l_oT.m_strEventId) < l_oT.m_iMaxTimes);
        if (l_oQ.f_EventFinished(l_oT.m_strEventId) && !l_bRunsLeft) {
            if (!l_oT.m_strAfterDialogue.empty()) l_vDone.push_back({l_oT.m_strEventId, true});
        } else if ((!l_oT.m_bNeedsWake || l_oQ.f_EventStarted(l_oT.m_strEventId)) && l_oQ.f_Holds(l_oT.m_jRequires)) {
            l_vOpen.push_back({l_oT.m_strEventId, false});
        }
    }
    l_vOpen.insert(l_vOpen.end(), l_vDone.begin(), l_vDone.end());
    return l_vOpen;
}

std::vector<int> f_AvailableLines(const NpcDef& l_oNpc, const TalkQuery& l_oQ) {
    std::vector<int> l_vOut;
    for (int l_iI = 0; l_iI < (int)l_oNpc.m_vLines.size(); ++l_iI)
        if (l_oQ.f_Holds(l_oNpc.m_vLines[l_iI].m_jRequires)) l_vOut.push_back(l_iI);
    return l_vOut;
}

int f_NextChatLine(const NpcDef& l_oNpc, NpcTalkState& l_oState, TalkQuery& l_oQ) {
    const std::vector<int> l_vAvail = f_AvailableLines(l_oNpc, l_oQ);
    if (l_vAvail.empty()) return -1;
    const int l_iN = (int)l_vAvail.size();
    if (l_iN == 1) {   // one line: always it; the index still moves, so a line that joins later comes up in turn
        if (l_oNpc.m_eOrder == ChatOrder::Loop) ++l_oState.m_iIndex;
        return l_vAvail[0];
    }
    if (l_oNpc.m_eOrder == ChatOrder::Random) {
        int l_iPick = l_oQ.f_Random(l_iN);
        if (l_vAvail[l_iPick] == l_oState.m_iLastRandom) l_iPick = (l_iPick + 1 + l_oQ.f_Random(l_iN - 1)) % l_iN;   // never twice in a row
        l_oState.m_iLastRandom = l_vAvail[l_iPick];
        return l_vAvail[l_iPick];
    }
    int l_iAt = l_oState.m_iIndex;
    if (l_oNpc.m_eOrder == ChatOrder::Stay) l_iAt = std::min(l_iAt, l_iN - 1);
    const int l_iLine = l_vAvail[l_iAt % l_iN];
    if (l_oNpc.m_eOrder == ChatOrder::Loop || l_oState.m_iIndex < l_iN - 1) ++l_oState.m_iIndex;
    return l_iLine;
}

TalkPlan f_PlanTalk(const NpcDef& l_oNpc, const std::vector<Topic>& l_vTopics, NpcTalkState& l_oState, TalkQuery& l_oQ) {
    TalkPlan l_oPlan;
    l_oPlan.m_vMenu = f_MenuTopics(l_vTopics, l_oQ);
    const int l_iOpen = (int)std::count_if(l_oPlan.m_vMenu.begin(), l_oPlan.m_vMenu.end(), [](const MenuEntry& l_oM) { return !l_oM.m_bFinished; });
    if (l_oPlan.m_vMenu.empty()) {
        l_oPlan.m_iChatLine = f_NextChatLine(l_oNpc, l_oState, l_oQ);
        l_oPlan.m_eKind = l_oPlan.m_iChatLine >= 0 ? TalkKind::Chat : TalkKind::Nothing;
    } else if (l_oPlan.m_vMenu.size() == 1 && l_iOpen == 1 && !l_oNpc.m_bMenuAlways) {
        l_oPlan.m_eKind = TalkKind::Topic;
        l_oPlan.m_oTopic = l_oPlan.m_vMenu[0];
        l_oPlan.m_vMenu.clear();
    } else {
        l_oPlan.m_eKind = TalkKind::Menu;
    }
    ++l_oState.m_iCount;
    return l_oPlan;
}

std::string f_StartNode(const nlohmann::json& l_jDialogue, const TalkQuery& l_oQ) {
    if (!l_jDialogue.is_object() || !l_jDialogue.contains("start")) return std::string();
    const nlohmann::json& l_jS = l_jDialogue["start"];
    if (l_jS.is_string()) return l_jS.get<std::string>();
    if (l_jS.is_array())
        for (const auto& l_jC : l_jS)
            if (l_jC.is_object() && l_oQ.f_Holds(l_jC.value("requires", nlohmann::json()))) return f_Str(l_jC, "node");
    return std::string();
}

} // namespace toms::talk
