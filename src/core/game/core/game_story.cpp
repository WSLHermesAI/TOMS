// game_story.cpp — narrative systems glue: dialogue nodes/choices/actions, missions,
// notifications and the game-state query. Split out of game.cpp 2026-09-13.
#include "game_internal.h"
#include "stair_links.h"   // toms::fillText
#include "story_actions.h"
#include "talk_system.h"

#include <functional>

using namespace toms::game_detail;

void Game::f_StartDialogue(const std::string& l_strNpc) {
    m_strDlgNpc = l_strNpc;
    m_strDlgSpeaker.clear();
    m_strDlgTalkNpc.clear();
    f_OpenDialogue(f_ReadDialogue(l_strNpc), DlgMode::Plain);
}

nlohmann::json Game::f_ReadDialogue(const std::string& l_strId) const {
    if (l_strId.empty()) return nlohmann::json();
    return f_ReadJsonFile(f_PathFor(toms::pattern::g_pszDialogue, l_strId));
}

bool Game::f_Holds(const nlohmann::json& l_jCondition) const {
    return toms::f_Evaluate(l_jCondition, GameConditionContext(m_oPl, m_oMeta, m_mapMissionTrackers, m_oRun, m_oEquipped, &m_strCurStage));
}

// The game's answers to talk_system's questions.
namespace {
class GameTalkQuery : public toms::talk::TalkQuery {
public:
    GameTalkQuery(std::function<bool(const nlohmann::json&)> l_fnHolds, const toms::RunStoryState& l_oRun, std::mt19937& l_oRng)
        : m_fnHolds(std::move(l_fnHolds)), m_oRun(l_oRun), m_oRng(l_oRng) {}
    bool f_Holds(const nlohmann::json& l_jC) const override { return m_fnHolds(l_jC); }
    bool f_EventStarted(const std::string& l_strId) const override { return m_oRun.f_EventStarted(l_strId); }
    bool f_EventFinished(const std::string& l_strId) const override { return m_oRun.f_EventFinished(l_strId); }
    int f_EventCount(const std::string& l_strId) const override { return m_oRun.f_EventCount(l_strId); }
    int f_Random(int l_iN) override { return l_iN <= 1 ? 0 : std::uniform_int_distribution<int>(0, l_iN - 1)(m_oRng); }
private:
    std::function<bool(const nlohmann::json&)> m_fnHolds;
    const toms::RunStoryState& m_oRun;
    std::mt19937& m_oRng;
};
}  // namespace

bool Game::f_OpenDialogue(const nlohmann::json& l_jData, DlgMode l_eMode) {
    m_vDlgChoices.clear();
    m_jDlgData = l_jData;
    m_eDlgMode = l_eMode;
    if (l_eMode != DlgMode::Topic && l_eMode != DlgMode::After) m_strDlgEvent.clear();
    m_bDlgFinishOnClose = l_eMode == DlgMode::Topic && l_jData.is_object() && l_jData.value("finishOnClose", false);
    GameTalkQuery l_oQ([this](const nlohmann::json& l_jC) { return f_Holds(l_jC); }, m_oRun, m_oTalkRng);
    const std::string l_strStart = toms::talk::f_StartNode(l_jData, l_oQ);
    if (l_strStart.empty()) { m_bInDialogue = false; return false; }
    m_bInDialogue = true;
    m_strDlgNode = l_strStart;
    m_iDlgSel = 0;
    f_EnterNode(m_strDlgNode);
    if (m_bInDialogue) m_oAudio.f_Play("dialogue_popup");
    return m_bInDialogue;
}

void Game::f_EnterNode(const std::string& l_strNode) {
    m_vDlgChoices.clear();
    if (!m_jDlgData.contains("nodes") || !m_jDlgData["nodes"].contains(l_strNode)) {
        f_EndDialogue(); return;   // missing node -> close safely (no nlohmann throw / abort)
    }
    const auto& l_jN = m_jDlgData["nodes"][l_strNode];
    if (!l_jN.is_object() || !l_jN.contains("choices")) return;
    for (auto& l_jC : l_jN["choices"]) {
        if (!l_jC.is_object()) continue;
        // Milestone 3: a choice's optional `requires` gates whether it appears at all, evaluated
        // via the shared Condition Evaluator (condition.h).
        if (l_jC.contains("requires") && !f_Holds(l_jC["requires"])) continue;
        DialogueChoice l_oCh;
        l_oCh.m_strLabel = l_jC.contains("label") ? m_oLocale.f_Field(l_jC["label"]) : "";
        l_oCh.m_strNext  = l_jC.contains("next") && l_jC["next"].is_string() ? l_jC["next"].get<std::string>() : "";
        if (l_jC.contains("action")) l_oCh.m_jAction = l_jC["action"];
        if (l_jC.contains("actions")) l_oCh.m_jActions = l_jC["actions"];
        l_oCh.m_bFinish = l_jC.value("finish", false);
        if (l_jC.contains("fire") && l_jC["fire"].is_array())
            for (const auto& l_jF : l_jC["fire"]) if (l_jF.is_string()) l_oCh.m_vFire.push_back(l_jF.get<std::string>());
        l_oCh.m_strId = l_jC.value("id", std::string());
        m_vDlgChoices.push_back(l_oCh);
    }
    m_iDlgSel = 0;
}

void Game::f_ChooseDialogue(int l_iIdx) {
    if (!m_bInDialogue) return;
    if (l_iIdx < 0 || l_iIdx >= (int)m_vDlgChoices.size()) return;
    f_MarkProgressDirty();   // dialogue choices move story flags/beats, which the meta save holds
    m_oAudio.f_Play("confirm_click");
    auto l_oCh = m_vDlgChoices[l_iIdx];   // copy: runDialogueAction (e.g. enterBattle) may resize dlgChoices
    toms::f_GlobalEventBus().f_Publish(toms::ChoiceMade{m_strDlgNpc, l_oCh.m_strLabel});
    if (m_eDlgMode == DlgMode::Menu) {   // the topic menu: the action says which line (openTopicMenu)
        const std::string l_strPick = l_oCh.m_jAction.value("menu", std::string());
        const std::string l_strNpc = m_strDlgTalkNpc;
        if (l_strPick == "topic") f_OpenTopic(l_oCh.m_jAction.value("eventId", std::string()), l_oCh.m_jAction.value("finished", false));
        else if (l_strPick == "chat") {
            GameTalkQuery l_oQ([this](const nlohmann::json& l_jC) { return f_Holds(l_jC); }, m_oRun, m_oTalkRng);
            const int l_iLine = toms::talk::f_NextChatLine(f_NpcDef(l_strNpc), m_oRun.f_NpcTalk(l_strNpc), l_oQ);
            if (l_iLine >= 0) f_PlayChatLine(l_strNpc, l_iLine); else f_EndDialogue();
        } else f_EndDialogue();
        return;
    }
    const std::string l_strEventId = m_strDlgEvent;
    const bool l_bTopic = m_eDlgMode == DlgMode::Topic;
    // A topic's choices are remembered per event (choiceMade {eventId, id}), so later dialogues and events can ask
    // which way the player went.
    if (l_bTopic && !l_oCh.m_strId.empty()) m_oRun.f_MakeChoice(l_strEventId, l_oCh.m_strId, true);
    if (!l_oCh.m_jAction.is_null()) f_RunStoryActions(l_oCh.m_jAction);
    if (!l_oCh.m_jActions.is_null()) f_RunStoryActions(l_oCh.m_jActions);
    if (l_bTopic) {   // an after-dialogue's `finish` / `fire` are ignored: the event is already finished
        for (const std::string& l_strF : l_oCh.m_vFire) f_WakeEvent(l_strF);
        if (l_oCh.m_bFinish) f_FinishTalkEvent(l_strEventId);
    }
    if (!m_bInDialogue) return;    // an action (e.g. enterBattle) may have already ended the dialogue
    if (l_oCh.m_strNext.empty()) { f_EndDialogue(); return; }
    m_strDlgNode = l_oCh.m_strNext; f_EnterNode(m_strDlgNode);
}

void Game::f_EndDialogue() {
    if (m_bInDialogue && m_eDlgMode == DlgMode::Topic && m_bDlgFinishOnClose && !m_strDlgEvent.empty()) f_FinishTalkEvent(m_strDlgEvent);
    m_bInDialogue = false;
    m_vDlgChoices.clear();
    m_eDlgMode = DlgMode::Plain;
    m_bDlgFinishOnClose = false;
}

// ---------- NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md) ----------

void Game::f_LoadNpcs() {
    m_mapNpcDefs.clear();
    const std::string l_strDir = f_PathOf(toms::key::g_pszFolderNpcs) + "/";
    for (const std::string& l_strName : toms::f_VfsListDir(l_strDir)) {
        if (l_strName.size() < 5 || l_strName.compare(l_strName.size() - 5, 5, ".json") != 0) continue;
        toms::talk::NpcDef l_oD;
        std::string l_strErr;
        if (toms::talk::f_ParseNpc(f_ReadJsonFile(l_strDir + l_strName), l_oD, &l_strErr)) m_mapNpcDefs[l_oD.m_strId] = l_oD;
        else fprintf(stderr, "[assets] npcs/%s: %s\n", l_strName.c_str(), l_strErr.c_str());
    }
    // Event variables (story/vars.json, the event editor's Variables tab): defaults and ranges.
    std::map<std::string, toms::VarDecl> l_mapDecls;
    nlohmann::json l_jVj = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataStoryVars));
    if (l_jVj.is_object())
        for (auto& [l_strK, l_jV] : l_jVj.items()) {
            if (l_strK.empty() || l_strK[0] == '_' || !l_jV.is_object()) continue;
            toms::VarDecl l_oD;
            l_oD.m_iDef = toms::f_VarNumber(l_jV.value("default", nlohmann::json(0)));
            if (l_jV.value("type", std::string()) == "bool") { l_oD.m_iLo = 0; l_oD.m_iHi = 1; }
            if (l_jV.contains("min") && l_jV["min"].is_number_integer()) l_oD.m_iLo = l_jV["min"].get<int>();
            if (l_jV.contains("max") && l_jV["max"].is_number_integer()) l_oD.m_iHi = l_jV["max"].get<int>();
            l_mapDecls[l_strK] = l_oD;
        }
    m_oRun.f_SetVarDecls(l_mapDecls);
    fprintf(stderr, "[assets] npcs: %zu files, %zu with topics; %zu event variables\n", m_mapNpcDefs.size(), m_mapNpcTopics.size(), l_mapDecls.size());
}

const toms::talk::NpcDef& Game::f_NpcDef(const std::string& l_strNpcId) {
    auto l_itIt = m_mapNpcDefs.find(l_strNpcId);
    if (l_itIt == m_mapNpcDefs.end()) l_itIt = m_mapNpcDefs.emplace(l_strNpcId, toms::talk::f_DefaultNpc(l_strNpcId, l_strNpcId)).first;   // no NPC file
    return l_itIt->second;
}

void Game::f_TalkTo(const std::string& l_strNpcId) {
    if (l_strNpcId.empty() || f_ModalActive()) return;
    const toms::talk::NpcDef& l_oNpc = f_NpcDef(l_strNpcId);
    static const std::vector<toms::talk::Topic> l_vNone;
    auto l_itT = m_mapNpcTopics.find(l_strNpcId);
    GameTalkQuery l_oQ([this](const nlohmann::json& l_jC) { return f_Holds(l_jC); }, m_oRun, m_oTalkRng);
    const toms::talk::TalkPlan l_oPlan = toms::talk::f_PlanTalk(l_oNpc, l_itT == m_mapNpcTopics.end() ? l_vNone : l_itT->second, m_oRun.f_NpcTalk(l_strNpcId), l_oQ);
    f_MarkProgressDirty();   // the talk index and count are saved with the run
    m_strDlgTalkNpc = l_strNpcId;
    switch (l_oPlan.m_eKind) {
        case toms::talk::TalkKind::Chat:  f_PlayChatLine(l_strNpcId, l_oPlan.m_iChatLine); break;
        case toms::talk::TalkKind::Topic: f_OpenTopic(l_oPlan.m_oTopic.m_strEventId, l_oPlan.m_oTopic.m_bFinished); break;
        case toms::talk::TalkKind::Menu:  f_OpenTopicMenu(l_strNpcId, l_oPlan.m_vMenu); break;
        case toms::talk::TalkKind::Nothing: break;
    }
}

void Game::f_PlayChatLine(const std::string& l_strNpcId, int l_iLine) {
    const toms::talk::NpcDef& l_oNpc = f_NpcDef(l_strNpcId);
    if (l_iLine < 0 || l_iLine >= (int)l_oNpc.m_vLines.size()) { f_EndDialogue(); return; }
    const toms::talk::ChatLine& l_oL = l_oNpc.m_vLines[l_iLine];
    m_strDlgNpc = l_oL.m_strDialogue;
    m_strDlgSpeaker = l_oNpc.m_strNameKey.empty() ? std::string() : m_oLocale.f_Tr(l_oNpc.m_strNameKey);
    if (!l_oL.m_jActions.is_null()) f_RunStoryActions(l_oL.m_jActions);
    f_OpenDialogue(f_ReadDialogue(l_oL.m_strDialogue), DlgMode::Chat);
}

void Game::f_OpenTopic(const std::string& l_strEventId, bool l_bFinished) {
    auto l_itDef = m_mapEventDefs.find(l_strEventId);
    if (l_strEventId.empty() || l_itDef == m_mapEventDefs.end()) { f_EndDialogue(); return; }
    const toms::talk::NpcDef& l_oNpc = f_NpcDef(m_strDlgTalkNpc);
    m_strDlgSpeaker = l_oNpc.m_strNameKey.empty() ? std::string() : m_oLocale.f_Tr(l_oNpc.m_strNameKey);
    m_strDlgEvent = l_strEventId;
    std::string l_strFile;
    for (const auto& [l_strN, l_vList] : m_mapNpcTopics)
        for (const toms::talk::Topic& l_oT : l_vList)
            if (l_oT.m_strEventId == l_strEventId) l_strFile = l_bFinished ? l_oT.m_strAfterDialogue : l_oT.m_strDialogue;
    if (!l_bFinished) {
        m_oRun.f_MarkEventStarted(l_strEventId);   // the event log lists it as in progress until it finishes
        f_MarkProgressDirty();
    }
    nlohmann::json l_jData = f_ReadDialogue(l_strFile);
    if (l_strFile.empty() || !l_jData.is_object()) {
        // No dialogue file: the event's text as one page; OK finishes it (an after-view just closes).
        std::string l_strText = m_oLocale.f_Tr(l_itDef->second.m_strTextKey);
        if (l_strText == l_itDef->second.m_strTextKey) l_strText = l_strEventId;
        nlohmann::json l_jOk = {{"label", m_oLocale.f_Tr("talk.ok")}, {"next", nullptr}};
        if (!l_bFinished) l_jOk["finish"] = true;
        l_jData = {{"start", "root"}, {"nodes", {{"root", {{"text", l_strText}, {"choices", nlohmann::json::array({l_jOk})}}}}}};
    }
    m_strDlgNpc = l_strFile.empty() ? l_strEventId : l_strFile;
    if (!f_OpenDialogue(l_jData, l_bFinished ? DlgMode::After : DlgMode::Topic)) f_EndDialogue();
}

void Game::f_OpenTopicMenu(const std::string& l_strNpcId, const std::vector<toms::talk::MenuEntry>& l_vMenu) {
    const toms::talk::NpcDef& l_oNpc = f_NpcDef(l_strNpcId);
    m_strDlgSpeaker = l_oNpc.m_strNameKey.empty() ? std::string() : m_oLocale.f_Tr(l_oNpc.m_strNameKey);
    nlohmann::json l_jChoices = nlohmann::json::array();
    for (const toms::talk::MenuEntry& l_oM : l_vMenu) {
        auto l_itD = m_mapEventDefs.find(l_oM.m_strEventId);
        std::string l_strTitle = l_itD == m_mapEventDefs.end() ? l_oM.m_strEventId : m_oLocale.f_Tr(l_itD->second.m_strTitleKey);
        if (l_itD != m_mapEventDefs.end() && l_strTitle == l_itD->second.m_strTitleKey) l_strTitle = l_oM.m_strEventId;
        const std::string l_strMark = l_oM.m_bFinished ? std::string("\xE2\x9C\x93 ") : std::string();   // a check mark: done
        l_jChoices.push_back({{"label", l_strMark + l_strTitle}, {"next", nullptr},
                           {"action", {{"menu", "topic"}, {"eventId", l_oM.m_strEventId}, {"finished", l_oM.m_bFinished}}}});
    }
    l_jChoices.push_back({{"label", m_oLocale.f_Tr("talk.just_chat")}, {"next", nullptr}, {"action", {{"menu", "chat"}}}});
    l_jChoices.push_back({{"label", m_oLocale.f_Tr("talk.leave")}, {"next", nullptr}, {"action", {{"menu", "leave"}}}});
    const std::string l_strGreeting = l_oNpc.m_strGreetingKey.empty() ? std::string() : m_oLocale.f_Tr(l_oNpc.m_strGreetingKey);
    nlohmann::json l_jData = {{"start", "menu"}, {"nodes", {{"menu", {{"text", l_strGreeting}, {"choices", l_jChoices}}}}}};
    m_strDlgNpc = l_strNpcId;
    f_OpenDialogue(l_jData, DlgMode::Menu);
}

void Game::f_FinishTalkEvent(const std::string& l_strEventId) {
    if (!l_strEventId.empty()) f_CompleteEvent(l_strEventId);
}

bool Game::f_CanRepeat(const std::string& l_strEventId) const {
    auto l_itDef = m_mapEventDefs.find(l_strEventId);
    if (l_itDef == m_mapEventDefs.end() || l_itDef->second.m_bOnce) return false;
    return l_itDef->second.m_iMaxTimes <= 0 || m_oRun.f_EventCount(l_strEventId) < l_itDef->second.m_iMaxTimes;
}

bool Game::f_CompleteEvent(const std::string& l_strEventId, int l_iDepth) {
    if (l_strEventId.empty()) return false;
    const bool l_bAgain = m_oRun.f_EventFinished(l_strEventId);
    if (l_bAgain && !f_CanRepeat(l_strEventId)) return false;
    if (auto l_itDef = m_mapEventDefs.find(l_strEventId); l_itDef != m_mapEventDefs.end()) f_RunStoryActions(l_itDef->second.m_jActions);
    m_oRun.f_CountEvent(l_strEventId);
    if (!l_bAgain) f_FinishEvent(l_strEventId, l_iDepth);   // the first run: flag, log, `next`, stairs
    else f_MarkProgressDirty();                  // a repeat: its actions only
    return true;
}

void Game::f_NoticeEventTitle(const std::string& l_strEventId) {
    auto l_itDef = m_mapEventDefs.find(l_strEventId);
    std::string l_strTitle = l_itDef == m_mapEventDefs.end() ? l_strEventId : m_oLocale.f_Tr(l_itDef->second.m_strTitleKey);
    if (l_itDef != m_mapEventDefs.end() && l_strTitle == l_itDef->second.m_strTitleKey) l_strTitle = l_strEventId;
    m_vNotifications.push_back({l_strTitle, 3600});
}

void Game::f_WakeEvent(const std::string& l_strEventId, int l_iDepth) {
    if (l_strEventId.empty() || (m_oRun.f_EventFinished(l_strEventId) && !f_CanRepeat(l_strEventId))) return;
    m_oRun.f_MarkEventStarted(l_strEventId);
    auto l_itDef = m_mapEventDefs.find(l_strEventId);
    if (l_itDef == m_mapEventDefs.end() || l_itDef->second.m_strTrigger != "chain" || l_iDepth > 32) return;   // 32: a loop in the data
    if (!f_Holds(l_itDef->second.m_jRequires)) return;
    f_NoticeEventTitle(l_strEventId);   // no tile, no NPC: the player sees its title (decision, 2026-10-16)
    f_CompleteEvent(l_strEventId, l_iDepth + 1);
}

void Game::f_FireLinkedEvents(const Entity& l_oE) {
    for (const std::string& l_strId : l_oE.m_vLinks) {
        auto l_itDef = m_mapEventDefs.find(l_strId);
        if (m_oRun.f_EventFinished(l_strId) && !f_CanRepeat(l_strId)) continue;
        if (l_itDef != m_mapEventDefs.end() && l_itDef->second.m_strTrigger == "talk") { m_oRun.f_MarkEventStarted(l_strId); continue; }   // a topic
        if (l_itDef != m_mapEventDefs.end() && !f_Holds(l_itDef->second.m_jRequires)) { m_oRun.f_MarkEventStarted(l_strId); continue; }
        f_NoticeEventTitle(l_strId);
        f_CompleteEvent(l_strId);
    }
    if (!l_oE.m_vLinks.empty()) f_MarkProgressDirty();
}

void Game::f_VarChanged(const std::string& l_strVar) {
    if (l_strVar.empty() || m_iVarChangeDepth >= 16) return;   // 16: events that keep changing each other's variables
    ++m_iVarChangeDepth;
    std::vector<std::string> l_vFire;
    for (const auto& [l_strId, l_oD] : m_mapEventDefs)
        if (l_oD.m_strTrigger == "varChanged" && l_oD.m_strTriggerVar == l_strVar) l_vFire.push_back(l_strId);
    for (const std::string& l_strId : l_vFire) {
        if (m_oRun.f_EventFinished(l_strId) && !f_CanRepeat(l_strId)) continue;
        if (!f_Holds(m_mapEventDefs[l_strId].m_jRequires)) continue;
        f_NoticeEventTitle(l_strId);
        f_CompleteEvent(l_strId);
    }
    --m_iVarChangeDepth;
}

std::string Game::f_NpcSprite(const std::string& l_strNpcId) const {
    auto l_itIt = m_mapNpcDefs.find(l_strNpcId);
    if (l_itIt != m_mapNpcDefs.end() && !l_itIt->second.m_strSprite.empty() && m_oSpriteAtlas.f_Find(l_itIt->second.m_strSprite)) return l_itIt->second.m_strSprite;
    if (m_oSpriteAtlas.f_Find("npc_" + l_strNpcId)) return "npc_" + l_strNpcId;
    return f_EntSprite(l_strNpcId);
}

void Game::f_RunStoryActions(const nlohmann::json& l_jActions) {
    if (l_jActions.is_null()) return;
    // A local class has the member function's access: Game's own state is changed directly.
    struct Effects : toms::story::StoryEffects {
        Game& m_oG;
        explicit Effects(Game& l_oGame) : m_oG(l_oGame) {}
        int* f_Stat(const std::string& l_strS) {
            Player& l_oP = m_oG.m_oPl;
            if (l_strS == "hp") return &l_oP.m_iHp;
            if (l_strS == "maxhp") return &l_oP.m_iMaxhp;
            if (l_strS == "atk") return &l_oP.m_iAtk;
            if (l_strS == "def") return &l_oP.m_iDef;
            if (l_strS == "gold") return &l_oP.m_iGold;
            if (l_strS == "lv") return &l_oP.m_iLv;
            for (const AttrDef& l_oA : m_oG.m_vAttrDefs) if (l_oA.m_strId == l_strS) return &l_oP.m_mapAttrs[l_oA.m_strId];   // str, dex, ... (stats.json)
            return nullptr;
        }
        void f_Note(const std::string& l_strS, int l_iDelta) {
            std::string l_strName = l_strS;
            for (const AttrDef& l_oA : m_oG.m_vAttrDefs) if (l_oA.m_strId == l_strS && !l_oA.m_strShortName.empty()) l_strName = l_oA.m_strShortName;
            if (l_strName == l_strS) { std::string l_strUp; for (char l_cC : l_strS) l_strUp += (char)std::toupper((unsigned char)l_cC); l_strName = l_strUp; }
            m_oG.m_vNotifications.push_back({l_strName + (l_iDelta >= 0 ? " +" : " ") + std::to_string(l_iDelta), 3200});
        }
        bool f_AddStat(const std::string& l_strS, int l_iDelta) override {
            if (l_strS == "exp") { if (l_iDelta > 0) m_oG.f_GainExp(l_iDelta); else m_oG.m_oPl.m_iExp = std::max(0, m_oG.m_oPl.m_iExp + l_iDelta); f_Note(l_strS, l_iDelta); return true; }
            int* l_pV = f_Stat(l_strS);
            if (!l_pV) return false;
            *l_pV = std::max(0, *l_pV + l_iDelta);
            if (l_strS == "hp") *l_pV = std::max(1, std::min(*l_pV, m_oG.f_EffectiveMaxHp()));
            f_Note(l_strS, l_iDelta);
            return true;
        }
        bool f_SetStat(const std::string& l_strS, int l_iValue) override {
            if (l_strS == "exp") { m_oG.m_oPl.m_iExp = std::max(0, l_iValue); return true; }
            int* l_pV = f_Stat(l_strS);
            if (!l_pV) return false;
            *l_pV = std::max(0, l_iValue);
            if (l_strS == "hp") *l_pV = std::max(1, std::min(*l_pV, m_oG.f_EffectiveMaxHp()));
            return true;
        }
        bool f_Give(const std::string& l_strItem, int l_iCount) override {
            if (l_strItem.empty()) return false;
            for (int l_iI = 0; l_iI < l_iCount; ++l_iI) m_oG.f_ReceiveItem(l_strItem);
            std::string l_strName = m_oG.f_ItemName(l_strItem);
            if (l_iCount > 1) l_strName += " x" + std::to_string(l_iCount);
            m_oG.m_vNotifications.push_back({toms::f_FillText(m_oG.m_oLocale.f_Tr("event.gotItem"), {{"item", l_strName}}), 3800});
            return true;
        }
        bool f_Take(const std::string& l_strItem, int l_iCount) override {
            Player& l_oP = m_oG.m_oPl;
            int* l_pKey = l_strItem == "key_yellow" ? &l_oP.m_iKeyYellow : l_strItem == "key_blue" ? &l_oP.m_iKeyBlue : l_strItem == "key_red" ? &l_oP.m_iKeyRed : nullptr;
            if (l_pKey) { if (*l_pKey < l_iCount) return false; *l_pKey -= l_iCount; return true; }
            if ((int)std::count(l_oP.m_vInv.begin(), l_oP.m_vInv.end(), l_strItem) < l_iCount) return false;
            for (int l_iI = 0; l_iI < l_iCount; ++l_iI) l_oP.m_vInv.erase(std::find(l_oP.m_vInv.begin(), l_oP.m_vInv.end(), l_strItem));
            m_oG.m_vNotifications.push_back({toms::f_FillText(m_oG.m_oLocale.f_Tr("event.gaveItem"), {{"item", m_oG.f_ItemName(l_strItem)}}), 3200});
            return true;
        }
        void f_SetVar(const std::string& l_strV, int l_iValue) override {
            const int l_iBefore = m_oG.m_oRun.f_Var(l_strV);
            m_oG.m_oRun.f_SetVar(l_strV, l_iValue);
            if (m_oG.m_oRun.f_Var(l_strV) != l_iBefore) m_oG.f_VarChanged(l_strV);
        }
        void f_AddVar(const std::string& l_strV, int l_iDelta) override {
            const int l_iBefore = m_oG.m_oRun.f_Var(l_strV);
            m_oG.m_oRun.f_AddVar(l_strV, l_iDelta);
            if (m_oG.m_oRun.f_Var(l_strV) != l_iBefore) m_oG.f_VarChanged(l_strV);
        }
        void f_SetFlag(const std::string& l_strF) override { if (!l_strF.empty()) m_oG.m_oRun.f_SetFlag(l_strF, true); }
        void f_SetStoryFlag(const std::string& l_strF) override { if (!l_strF.empty()) toms::f_SetStoryFlag(m_oG.m_oMeta, l_strF); }
        void f_AddCounter(const std::string& l_strC, int l_iDelta) override { if (!l_strC.empty() && l_iDelta != 0) m_oG.m_oRun.f_AddCounter(l_strC, l_iDelta); }
        void f_FireEvent(const std::string& l_strId) override { m_oG.f_WakeEvent(l_strId); }
        void f_Message(const std::string& l_strKey) override { m_oG.m_vNotifications.push_back({m_oG.m_oLocale.f_Tr(l_strKey), 3800}); }
        bool f_Other(const nlohmann::json& l_jA) override { m_oG.f_RunDialogueAction(l_jA); return true; }
    } l_oFx(*this);
    const toms::story::RunReport l_oRep = toms::story::f_RunActions(l_jActions, l_oFx);
    for (const std::string& l_strP : l_oRep.m_vProblems) fprintf(stderr, "[story] action failed: %s\n", l_strP.c_str());
    f_MarkProgressDirty();
}

// Milestone 4: executes a dialogue choice's optional `action` verb. Kept as a small, explicit
// enum-style switch rather than a scripting hook, matching this project's existing preference
// (see docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.4's `talent` enum / dialogue's own `action.give`
// precedent in the design docs).
// ---- reward events (docs/gameplay/07 phase 6) ----

void Game::f_OpenMiniGame(const std::string& l_strId) {
    const toms::MiniGameDef* l_pDef = m_oMiniGames.f_Find(l_strId);
    if (!l_pDef) { std::fprintf(stderr, "[minigame] no \"%s\" in data/minigames.json\n", l_strId.c_str()); return; }
    if (l_pDef->m_iLimit > 0 && m_oRun.f_Counter("minigame." + l_strId) >= l_pDef->m_iLimit) {   // played out for this run
        m_vNotifications.push_back({m_oLocale.f_Tr("minigame.used_up"), 3000});
        return;
    }
    m_oMiniGame = MiniGameState{};
    m_oMiniGame.m_bOpen = true;
    m_oMiniGame.m_strId = l_strId;
    m_oAudio.f_Play("dialogue_popup");
}

void Game::f_MiniGameTap() {
    if (!m_oMiniGame.m_bOpen) return;
    const toms::MiniGameDef* l_pDef = m_oMiniGames.f_Find(m_oMiniGame.m_strId);
    if (!l_pDef) { f_CloseMiniGame(); return; }
    if (m_oMiniGame.m_iPhase == 0) {   // start
        m_oMiniGame.m_oPlay.f_Start(l_pDef->m_oPlay, 0.0f);
        m_oMiniGame.m_iPhase = 1;
        m_oAudio.f_Play("confirm_click");
        return;
    }
    if (m_oMiniGame.m_iPhase == 1) {
        const auto l_eT = m_oMiniGame.m_oPlay.f_Tap();
        m_oAudio.f_Play(l_eT == toms::ComboPlay::Tap::Hit ? "confirm_click" : "deny");
        if (m_oMiniGame.m_oPlay.f_Done()) f_FinishMiniGame();
        return;
    }
    f_CloseMiniGame();   // the result: done
}

void Game::f_FinishMiniGame() {
    const toms::MiniGameDef* l_pDef = m_oMiniGames.f_Find(m_oMiniGame.m_strId);
    m_oMiniGame.m_iPhase = 2;
    if (!l_pDef) return;
    m_oMiniGame.m_iReward = toms::f_MiniGameReward(*l_pDef, m_oMiniGame.m_oPlay.f_Result());
    if (l_pDef->m_strCurrency == "crystals") m_oPl.m_iCrystals += m_oMiniGame.m_iReward;
    else m_oPl.m_iGold += m_oMiniGame.m_iReward;
    m_oRun.f_AddCounter("minigame." + m_oMiniGame.m_strId, 1);
    m_oAudio.f_Play(m_oMiniGame.m_iReward > 0 ? "get_item" : "deny");
    f_MarkProgressDirty();
}

void Game::f_CloseMiniGame() {
    if (m_oMiniGame.m_iPhase == 1) return;   // playing: the play ends by itself
    m_oMiniGame = MiniGameState{};
}

// ---- the start story: the weapon trial (docs/gameplay/07 phase 3) ----

void Game::f_StartWeaponTrial() { f_StartDialogue("weapon_master"); }

void Game::f_TryWeapon(const std::string& l_strWeaponId) {
    if (!m_mapEquipmentDefs.count(l_strWeaponId)) return;
    m_oEquipped.m_strWeaponId = l_strWeaponId;   // worn for the trial; owned only when chosen
    f_EndDialogue();
    m_oArena = Arena{};
    m_oArena.m_bOn = true;
    m_oArena.m_bTrial = true;
    m_oArena.m_vEnemies = {"slime"};      // a training slime: weak, slow to strike back
    m_oArena.m_jOverrides = {{"hp", 40}, {"atk", 2}, {"atk_interval_ms", 5000}};
    f_ArenaNextFight();
    m_oCs.m_strLog = m_oLocale.f_Tr("trial.fight_log");
}

void Game::f_EndWeaponTrialFight() {
    m_oArena = Arena{};
    m_oCs = CombatState{};
    m_oPl.m_iHp = f_EffectiveMaxHp();
    f_StartDialogue("weapon_master");
    if (m_bInDialogue) { m_strDlgNode = "after"; f_EnterNode(m_strDlgNode); }
}

void Game::f_ChooseWeapon() {
    const std::string l_strChosen = m_oEquipped.m_strWeaponId.empty() ? std::string(m_pszStartingWeapon) : m_oEquipped.m_strWeaponId;
    m_vGearOwned.clear();              // only the chosen weapon goes with the player
    f_OwnGear(l_strChosen, true);
    m_bNewsGear = false;
    m_oRun.f_SetFlag("start.weapon." + l_strChosen);
    f_EndDialogue();
    m_vNotifications.push_back({f_TrParam(m_oLocale.f_Tr("trial.chosen"), "name", f_ItemName(l_strChosen)), 3500});
    f_MarkProgressDirty();
}

void Game::f_RunDialogueAction(const nlohmann::json& l_jAction) {
    if (!l_jAction.is_object()) return;
    std::string l_strType = l_jAction.value("type", std::string());
    if (l_strType == "tryWeapon") { f_TryWeapon(l_jAction.value("weapon", std::string())); return; }   // docs/gameplay/07 phase 3
    if (l_strType == "chooseWeapon") { f_ChooseWeapon(); return; }
    if (l_strType == "miniGame") { f_EndDialogue(); f_OpenMiniGame(l_jAction.value("id", std::string())); return; }   // docs/gameplay/07 phase 6
    if (l_strType == "give") {
        std::string l_strItemId = l_jAction.value("itemId", std::string());
        if (!l_strItemId.empty()) f_ReceiveItem(l_strItemId);
    } else if (l_strType == "setStoryFlag") {
        std::string l_strFlag = l_jAction.value("flag", std::string());
        if (!l_strFlag.empty()) toms::f_SetStoryFlag(m_oMeta, l_strFlag);
    } else if (l_strType == "enterBattle") {
        // Only meaningful after a dialogue_gate encounter (movePlayer) stashed an enemy.
        if (m_bHasPendingEncounter) {
            m_bInDialogue = false;
            m_bHasPendingEncounter = false;
            f_StartCombat(m_oPendingEncounterEnemy, f_PartyOf(m_oPendingEncounterEnemy));
        }
    } else if (l_strType == "startMission") {
        std::string l_strMissionId = l_jAction.value("missionId", std::string());
        if (!l_strMissionId.empty()) f_StartMission(l_strMissionId);
    } else if (l_strType == "claimMission") {
        std::string l_strMissionId = l_jAction.value("missionId", std::string());
        if (!l_strMissionId.empty()) f_ClaimMission(l_strMissionId);
    } else if (l_strType == "makeChoice") {
        // S3: record a main-line decision (docs/story/STORY_DATA_SCHEMA.md section 4.1). Main-line choices
        // are irreversible, so the first answer sticks even if the node is reached again.
        std::string l_strChoiceId = l_jAction.value("choiceId", std::string());
        std::string l_strOptionId = l_jAction.value("optionId", std::string());
        if (!l_strChoiceId.empty() && !l_strOptionId.empty()) {
            m_oRun.f_MakeChoice(l_strChoiceId, l_strOptionId, l_jAction.value("reversible", false));
            // The option's effects travel with the action: the chapter file is the authority
            // (section 4.1's option entry) and the dialogue node mirrors it, the same way every other
            // data-driven effect in this project is declared where it is used.
            if (l_jAction.contains("setFlags") && l_jAction["setFlags"].is_array())
                for (auto& l_jF : l_jAction["setFlags"])
                    if (l_jF.is_string()) m_oRun.f_SetFlag(l_jF.get<std::string>());
            if (l_jAction.contains("counters") && l_jAction["counters"].is_object())
                for (auto& [l_strName, l_jDelta] : l_jAction["counters"].items())
                    if (l_jDelta.is_number_integer()) m_oRun.f_AddCounter(l_strName, l_jDelta.get<int>());
            // M9: a main-line choice that's also a side story's payoff (e.g. ss_04's return/study
            // split) can bundle the skill grant and the side-story completion right here, reusing
            // the two verbs below instead of inventing a second action per dialogue choice.
            if (l_jAction.contains("grantSkill") && l_jAction["grantSkill"].is_string())
                f_RunDialogueAction({{"type", "unlockSkill"}, {"skillId", l_jAction["grantSkill"]}});
            if (l_jAction.contains("completeSideStory") && l_jAction["completeSideStory"].is_string())
                f_RunDialogueAction({{"type", "setSideStoryState"},
                                    {"sideStoryId", l_jAction["completeSideStory"]}, {"state", "completed"}});
        }
    } else if (l_strType == "addCounter") {
        // S3: the three accumulators (insight / resolve / humanity). Clamped by RunStoryState to the
        // range declared in data/story/counters.json.
        std::string l_strName = l_jAction.value("counter", std::string());
        int l_iDelta = l_jAction.value("delta", 0);
        if (!l_strName.empty() && l_iDelta != 0) m_oRun.f_AddCounter(l_strName, l_iDelta);
    } else if (l_strType == "unlockSkill") {
        // M9: a dialogue-granted skill (e.g. a side story's reward) -- same free-grant path as
        // Game::applyChapterGrants' own "skills" loop, just reachable from a choice instead of a
        // chapter's grants block.
        std::string l_strSkillId = l_jAction.value("skillId", std::string());
        if (!l_strSkillId.empty() && !m_oRun.f_HasSkill(l_strSkillId)) {
            auto l_itIt = m_mapSkillDefs.find(l_strSkillId);
            int l_iCost = (l_itIt != m_mapSkillDefs.end()) ? l_itIt->second.m_iCost : 0;
            m_oRun.f_UnlockSkill(l_strSkillId, l_iCost);
            std::string l_strName = l_itIt != m_mapSkillDefs.end() ? m_oLocale.f_Field(l_itIt->second.m_jName) : l_strSkillId;
            f_PushNotification(m_oLocale.f_Tr("skill.unlocked_prefix") + l_strName);
        }
    } else if (l_strType == "setSideStoryState") {
        // M9: side-story progress was tracked in RunStoryState (S3) but nothing ever wrote to it
        // until now -- a dialogue choice is the natural place a side story actually resolves.
        std::string l_strSideStoryId = l_jAction.value("sideStoryId", std::string());
        std::string l_strState = l_jAction.value("state", std::string());
        if (!l_strSideStoryId.empty() && !l_strState.empty()) m_oRun.f_SetSideStoryState(l_strSideStoryId, l_strState);
    }
}

// S4: applies a chapter's `grants` exactly once -- see the declaration in game.h for why this
// exists at all (nothing has ever read a chapter file at runtime before this).
void Game::f_ApplyChapterGrants(const std::string& l_strChapterId) {
    if (l_strChapterId.empty()) return;
    std::string l_strGrantedFlag = "_chapter_granted." + l_strChapterId;
    if (m_oRun.f_Flag(l_strGrantedFlag)) return;   // idempotent: revisiting ch_01's floors must not re-grant
    nlohmann::json l_jCh = f_ReadJsonFile(f_PathFor(toms::pattern::g_pszChapter, l_strChapterId));
    if (!l_jCh.is_object() || !l_jCh.contains("grants") || !l_jCh["grants"].is_object()) {
        // No chapter file yet (ch_04+, per S3's own scope) -- mark it granted anyway so the moment
        // that content DOES land, its grants apply from a fresh chapter entry, not retroactively
        // mid-chapter the session it happens to be authored.
        m_oRun.f_SetFlag(l_strGrantedFlag);
        return;
    }
    auto& l_jGrants = l_jCh["grants"];
    if (l_jGrants.contains("skillPoints") && l_jGrants["skillPoints"].is_number_integer())
        m_oRun.f_AddSkillPoints(l_jGrants["skillPoints"].get<int>());
    if (l_jGrants.contains("skills") && l_jGrants["skills"].is_array()) {
        for (auto& l_jS : l_jGrants["skills"]) {
            if (!l_jS.is_string()) continue;
            std::string l_strSkillId = l_jS.get<std::string>();
            auto l_itIt = m_mapSkillDefs.find(l_strSkillId);
            int l_iCost = (l_itIt != m_mapSkillDefs.end()) ? l_itIt->second.m_iCost : 0;   // chapter grants are free either way
            if (!m_oRun.f_HasSkill(l_strSkillId)) {
                m_oRun.f_UnlockSkill(l_strSkillId, l_iCost);
                std::string l_strName = l_itIt != m_mapSkillDefs.end() ? m_oLocale.f_Field(l_itIt->second.m_jName) : l_strSkillId;
                f_PushNotification(m_oLocale.f_Tr("skill.unlocked_prefix") + l_strName);
            }
        }
    }
    // S5: forge blueprints -- meta scope (survives rebirth, see MetaSaveData::forgeRecipesKnown),
    // unlike skills above which are run-scoped. Dedup by hand: this is a plain vector, not a set,
    // matching the shape section 8's rebirth table already implies for it (a small, append-mostly
    // list), and applyChapterGrants can't assume it will only ever run once per recipe across a
    // whole *cycle* the way run flags can.
    if (l_jGrants.contains("forgeRecipes") && l_jGrants["forgeRecipes"].is_array()) {
        for (auto& l_jR : l_jGrants["forgeRecipes"]) {
            if (!l_jR.is_string()) continue;
            std::string l_strRecipeId = l_jR.get<std::string>();
            if (std::find(m_oMeta.m_vForgeRecipesKnown.begin(), m_oMeta.m_vForgeRecipesKnown.end(), l_strRecipeId)
                == m_oMeta.m_vForgeRecipesKnown.end()) {
                m_oMeta.m_vForgeRecipesKnown.push_back(l_strRecipeId);
                auto l_itFit = m_mapForgeDefs.find(l_strRecipeId);
                std::string l_strName = l_itFit != m_mapForgeDefs.end() ? m_oLocale.f_Field(l_itFit->second.m_jName) : l_strRecipeId;
                f_PushNotification(m_oLocale.f_Tr("forge.recipe_learned_prefix") + l_strName);
            }
        }
    }
    // hub/unlocks: recorded as run flags so a later phase (the hub NPC, the forge, gated dialogue)
    // can read them the moment it exists, same "wire the data even if nothing consumes it yet"
    // pattern this project used for mission/equipment content before their own UIs landed.
    for (const char* l_pszKey : {"hub", "unlocks"}) {
        if (l_jGrants.contains(l_pszKey) && l_jGrants[l_pszKey].is_array())
            for (auto& l_jU : l_jGrants[l_pszKey]) if (l_jU.is_string()) m_oRun.f_SetFlag(l_jU.get<std::string>());
    }
    m_oRun.f_SetFlag(l_strGrantedFlag);
}

// S4: the one path that can spend a skill point. Re-checks canUnlockSkill itself (see the
// declaration in game.h) -- never trusts that a caller already validated it.
bool Game::f_TryUnlockSkill(const std::string& l_strSkillId) {
    if (!toms::f_CanUnlockSkill(m_mapSkillDefs, m_oRun.f_SkillsOwned(), m_oRun.f_SkillPoints(), l_strSkillId)) return false;
    m_oRun.f_UnlockSkill(l_strSkillId, m_mapSkillDefs.at(l_strSkillId).m_iCost);
    f_MarkProgressDirty();
    m_oAudio.f_Play("confirm_click");
    return true;
}

std::vector<std::string> Game::f_SkillMenuOrder() const {
    static const std::map<std::string, int> l_mapLineageOrder = { {"yinqi", 0}, {"yuqi", 1}, {"faqi", 2} };
    std::vector<std::string> l_vIds;
    l_vIds.reserve(m_mapSkillDefs.size());
    for (auto& [l_strId, l_oDef] : m_mapSkillDefs) l_vIds.push_back(l_strId);
    std::sort(l_vIds.begin(), l_vIds.end(), [&](const std::string& l_strA, const std::string& l_strB) {
        const auto& l_oDa = m_mapSkillDefs.at(l_strA); const auto& l_oDb = m_mapSkillDefs.at(l_strB);
        auto l_fnRank = [&](const std::string& l_strLineage) {
            auto l_itIt = l_mapLineageOrder.find(l_strLineage);
            return l_itIt == l_mapLineageOrder.end() ? (int)l_mapLineageOrder.size() : l_itIt->second;
        };
        int l_iRa = l_fnRank(l_oDa.m_strLineage), l_iRb = l_fnRank(l_oDb.m_strLineage);
        if (l_iRa != l_iRb) return l_iRa < l_iRb;
        if (l_oDa.m_iTier != l_oDb.m_iTier) return l_oDa.m_iTier < l_oDb.m_iTier;
        return l_strA < l_strB;   // stable tie-break for two nodes at the same lineage+tier
    });
    return l_vIds;
}

// S5: the forge sub-page's row order. There's no lineage grouping concept for recipes the way
// skills have, so plain id order (stable, deterministic, matches data/forge.json's own key order
// for the map's iteration) is all a 3-recipe list needs -- add a real sort key if that ever stops
// being enough.
std::vector<std::string> Game::f_ForgeMenuOrder() const {
    std::vector<std::string> l_vIds;
    l_vIds.reserve(m_mapForgeDefs.size());
    for (auto& [l_strId, l_oDef] : m_mapForgeDefs) l_vIds.push_back(l_strId);
    std::sort(l_vIds.begin(), l_vIds.end());
    return l_vIds;
}

// S5: the one path that can spend gold/materials on a craft. Re-checks canCraft itself (same
// "never trust the caller" rule as tryUnlockSkill) -- deducts cost and equips the result in one
// atomic step, so a UI bug can never spend materials without getting the item.
bool Game::f_TryCraft(const std::string& l_strRecipeId) {
    if (!toms::f_CanCraft(m_mapForgeDefs, m_oMeta.m_vForgeRecipesKnown, m_oPl.m_vInv, m_oPl.m_iGold, l_strRecipeId)) return false;
    const toms::ForgeRecipeDefinition& l_oDef = m_mapForgeDefs.at(l_strRecipeId);
    m_oPl.m_iGold -= l_oDef.m_iCostGold;
    for (auto& [l_strItemId, l_iNeed] : l_oDef.m_mapMaterials) {
        int l_iRemoved = 0;
        for (auto l_itIt = m_oPl.m_vInv.begin(); l_itIt != m_oPl.m_vInv.end() && l_iRemoved < l_iNeed; ) {
            if (*l_itIt == l_strItemId) { l_itIt = m_oPl.m_vInv.erase(l_itIt); ++l_iRemoved; }
            else ++l_itIt;
        }
    }
    f_OwnGear(l_oDef.m_strResultEquipmentId, true);   // forged gear is owned and worn at once
    f_MarkProgressDirty();
    m_oAudio.f_Play("get_item");
    return true;
}

// S6: the hub sub-page's row order. Same reasoning as forgeMenuOrder() -- no lineage-style
// grouping concept for a handful of named places, so plain id order is enough.
std::vector<std::string> Game::f_HubMenuOrder() const {
    std::vector<std::string> l_vIds;
    l_vIds.reserve(m_mapHubDefs.size());
    for (auto& [l_strId, l_oDef] : m_mapHubDefs) l_vIds.push_back(l_strId);
    std::sort(l_vIds.begin(), l_vIds.end());
    return l_vIds;
}

// S6: the one path that can activate a hub location. Re-checks hubLocationUnlocked itself (same
// "never trust the caller" rule as tryUnlockSkill/tryCraft) -- an already-locked row is a silent,
// safe no-op rather than something the UI needs to pre-check. Only "talk" is implemented today
// (see hub_system.h's own comment on actionKind); an unrecognized kind is also a safe no-op so a
// future content typo never crashes, just silently does nothing.
bool Game::f_ActivateHubLocation(const std::string& l_strLocationId) {
    auto l_fnHasFlag = [this](const std::string& l_strF) { return m_oRun.f_Flag(l_strF); };
    if (!toms::f_HubLocationUnlocked(m_mapHubDefs, l_fnHasFlag, l_strLocationId)) return false;
    const toms::HubLocationDefinition& l_oDef = m_mapHubDefs.at(l_strLocationId);
    if (l_oDef.m_strActionKind == "talk" && !l_oDef.m_strActionNpc.empty()) {
        f_CloseInGameMenu();       // the dialogue UI and the pause menu were never designed to overlap
        f_StartDialogue(l_oDef.m_strActionNpc);
        return true;
    }
    return false;
}

// S5/S6: the Main page's row order/count, re-derived fresh every call from the two conditional
// flags (see their declaration in game.h for why this replaced a hardcoded "forgeOn ? 5 : 4"
// the moment a second conditional row existed) -- the one function draw and every input handler
// below share, so they can never disagree about which row index opens which page.
std::vector<Game::MainMenuRow> Game::f_MainMenuOrder() const {
    std::vector<MainMenuRow> l_vRows = { MainMenuRow::Store, MainMenuRow::Skills };
    if (f_ForgeMenuUnlocked()) l_vRows.push_back(MainMenuRow::Forge);
    if (f_HubMenuUnlocked())   l_vRows.push_back(MainMenuRow::Village);
    l_vRows.push_back(MainMenuRow::Save);
    l_vRows.push_back(MainMenuRow::Settings);
    if (m_bFullscreenAvailable) l_vRows.push_back(MainMenuRow::Fullscreen);   // not on Android: always full screen
    l_vRows.push_back(MainMenuRow::BackToTitle);
    return l_vRows;
}

int Game::f_MainMenuRowIndex(MainMenuRow l_eR) const {
    auto l_vOrder = f_MainMenuOrder();
    auto l_itIt = std::find(l_vOrder.begin(), l_vOrder.end(), l_eR);
    return l_itIt == l_vOrder.end() ? 0 : (int)std::distance(l_vOrder.begin(), l_itIt);
}

void Game::f_StartMission(const std::string& l_strId) {
    auto& l_oT = m_mapMissionTrackers[l_strId];   // creates a fresh (Locked, progress=0) tracker if absent
    if (l_oT.m_strMissionId.empty()) l_oT.m_strMissionId = l_strId;
    l_oT.m_eState = toms::MissionState::Active;
}

// Milestone 8: grants the reward and flips Completed -> Claimed. Strictly gated on the tracker
// already being Completed, so a dialogue choice that stays visible after claiming (see the
// `requires: missionComplete` convention used in this session's authored content -- missionComplete()
// returns true for both Completed and Claimed) can never grant the reward twice.
void Game::f_ClaimMission(const std::string& l_strId) {
    auto l_itTrackerIt = m_mapMissionTrackers.find(l_strId);
    if (l_itTrackerIt == m_mapMissionTrackers.end() || l_itTrackerIt->second.m_eState != toms::MissionState::Completed) return;
    auto l_itDefIt = m_mapMissionDefs.find(l_strId);
    if (l_itDefIt != m_mapMissionDefs.end()) {
        const toms::MissionDefinition& l_oDef = l_itDefIt->second;
        m_oPl.m_iGold += l_oDef.m_iRewardGold;
        // Mirrors movePlayer()'s item-pickup split: keys/coins apply immediately, everything
        // else (gems/potions/exp/scroll) goes into the inventory to be used later, not consumed
        // on the spot -- a reward potion should sit in the backpack like a picked-up one would.
        if (!l_oDef.m_strRewardItemId.empty()) f_ReceiveItem(l_oDef.m_strRewardItemId);
        f_GainExp(l_oDef.m_iRewardExp);
    }
    l_itTrackerIt->second.m_eState = toms::MissionState::Claimed;
    f_PushNotification(m_oLocale.f_Tr("mission.completed_prefix") + l_strId);
}

// Subscribes mission-progress handlers to the global EventBus once (called from loadAssets).
// missionDefs_ is empty until Milestone 8 loads data/missions.json, so these handlers are inert
// today -- they become live the moment content defines a matching objective, with zero coupling
// back into Battle/Item code (which only ever calls publish(), unchanged since Milestone 1).
void Game::f_PushNotification(const std::string& l_strMsg) {
    m_vNotifications.push_back({l_strMsg, 3000});   // 3 seconds on screen
}

void Game::f_RollDailyMissions() {
    std::string l_strToday = f_TodayDateStringLocal();
    for (auto& [l_strId, l_oTracker] : m_mapMissionTrackers) {
        auto l_itIt = m_mapMissionDefs.find(l_strId);
        if (l_itIt == m_mapMissionDefs.end() || l_itIt->second.m_eKind != toms::MissionKind::Daily) continue;
        toms::MissionState l_eBefore = l_oTracker.m_eState;
        toms::f_RollDailyReset(l_oTracker, l_itIt->second, l_strToday);
        if (l_oTracker.m_eState == toms::MissionState::Available && l_eBefore != toms::MissionState::Available)
            f_PushNotification(m_oLocale.f_Tr("mission.daily_reset_prefix") + l_strId);
    }
}

void Game::f_WireMissionEvents() {
    // listen(), not subscribe(): the global bus outlives this Game (the editor restarts the game for every scenario),
    // so the handlers must go with it -- busSubs_ removes them when the Game is destroyed.
    m_vBusSubs.clear();   // loadAssets again on the same Game: never twice
    m_vBusSubs.push_back(toms::f_GlobalEventBus().f_Listen<toms::EnemyDefeated>([this](const toms::EnemyDefeated& l_oE) {
        for (auto& [l_strId, l_oTracker] : m_mapMissionTrackers) {
            auto l_itIt = m_mapMissionDefs.find(l_strId);
            if (l_itIt != m_mapMissionDefs.end()) toms::f_ApplyProgressEvent(l_oTracker, l_itIt->second, "defeat", l_oE.m_strEnemyId);
        }
    }));
    m_vBusSubs.push_back(toms::f_GlobalEventBus().f_Listen<toms::ItemCollected>([this](const toms::ItemCollected& l_oE) {
        for (auto& [l_strId, l_oTracker] : m_mapMissionTrackers) {
            auto l_itIt = m_mapMissionDefs.find(l_strId);
            if (l_itIt != m_mapMissionDefs.end()) toms::f_ApplyProgressEvent(l_oTracker, l_itIt->second, "collect", l_oE.m_strItemId);
        }
    }));
}

// M7 (first slice): the one path that can end a run. Not re-validated against the resolver here
// (unlike tryUnlockSkill/tryCraft/activateHubLocation) -- the caller (finishCombatLose(), the only
// call site today) already re-checked checkNamedEndings() itself, and there is no player-facing
// "activate an ending" action to guard against a stale/duplicate click the way those three do.
void Game::f_TriggerEnding(const std::string& l_strEndingId) {
    m_strActiveEndingId = l_strEndingId;
    if (std::find(m_oMeta.m_vEndingsSeen.begin(), m_oMeta.m_vEndingsSeen.end(), l_strEndingId) == m_oMeta.m_vEndingsSeen.end())
        m_oMeta.m_vEndingsSeen.push_back(l_strEndingId);
    f_SaveCurrentRun();   // a run ending is significant enough to flush immediately, not just markProgressDirty()
}

bool Game::f_RebirthOffered() const {
    if (!f_EndingActive()) return false;
    if (m_oMeta.m_iCycleIndex >= m_oCyclesConfig.m_iMaxCycles) return false;   // STORY_BIBLE.md §8: capped at 9
    for (auto& l_oE : m_oEndingsTable.m_vEndings)
        if (l_oE.m_strId == m_strActiveEndingId) return l_oE.m_bAllowsRebirth;
    return false;   // an unknown ending id (content bug) never offers a rebirth it can't carry out
}

// The "start over" exit -- always available, and the only one when rebirthOffered() is false.
void Game::f_DismissEndingScreen() {
    if (!f_EndingActive()) return;
    m_strActiveEndingId.clear();
    f_ReturnToTitle();
}

// M8 (first slice): docs/story/STORY_BIBLE.md §8.1's "入輪迴" -- carries skills (at half effect --
// see skillEffectScale()), memory shards (already reset(keepShards=true)'s job), skill points and
// the Super gauge cap (both halved-and-floored), and cycleIndex (+1) forward; resets everything
// else (equipment to the starting weapon, consumables/materials/gold/keys, main-line flags/choices/
// counters/side-stories, floor back to F01). Forge blueprints and known actives need no code here
// at all -- they already live on meta_, which this never touches except cycleIndex/endingsSeen.
bool Game::f_Rebirth() {
    if (!f_RebirthOffered()) return false;

    // Capture what carries forward BEFORE the wipe below destroys it.
    std::vector<std::string> l_vKeptSkills = m_oRun.f_SkillsOwned();
    int l_iHalvedSkillPoints = m_oRun.f_SkillPoints() / 2;               // floor: non-negative int division
    int l_iHalvedSuperMax = std::max(1, m_oRun.f_SuperMax() / 2);
    int l_iOldAtkBonus = m_oPl.m_iAtk - m_iStartingAtk, l_iOldDefBonus = m_oPl.m_iDef - m_iStartingDef;

    m_oRun.f_Reset(/*keepShards=*/true);   // the full wipe (see its own comment); shards survive
    for (auto& l_strId : l_vKeptSkills) m_oRun.f_UnlockSkill(l_strId, 0);   // restore ownership at zero cost
    m_oRun.f_AddSkillPoints(l_iHalvedSkillPoints);
    m_oRun.f_SetSuperMax(l_iHalvedSuperMax);

    m_oMeta.m_iCycleIndex += 1;

    m_oPl.m_iAtk = m_iStartingAtk + l_iOldAtkBonus / 2;
    m_oPl.m_iDef = m_iStartingDef + l_iOldDefBonus / 2;
    m_oPl.m_iMaxhp = m_iStartingHp; m_oPl.m_iHp = m_iStartingHp;
    m_oPl.m_iGold = 0; m_oPl.m_iExp = 0; m_oPl.m_iLv = 1;
    m_oPl.m_iCrystals = 0;
    m_oPl.m_iKeyYellow = m_oPl.m_iKeyBlue = m_oPl.m_iKeyRed = 0;
    m_oPl.m_vInv.clear();
    f_ResetAttrs();
    m_oEquipped = toms::EquippedSet{};
    m_vGearOwned.clear();
    m_mapWeaponUses.clear();
    f_OwnGear(m_pszStartingWeapon, true);

    m_strActiveEndingId.clear();
    m_mapEntityStatus.clear();
    m_mapMissionTrackers.clear();
    f_LoadStage("F01");
    f_SaveCurrentRun();
    return true;
}

toms::GameState Game::f_CurrentState() const {
    using toms::GameState;
    // Title phase (Boot): reported as MainMenu, with its Settings page mapping to
    // GameState::Settings — game_state.h's table already allows MainMenu <-> Settings.
    if (m_oTitle.f_IsOpen())
        return (m_oTitle.f_Page() == toms::TitlePage::Settings) ? GameState::Settings : GameState::MainMenu;
    if (m_oCs.m_bActive) return GameState::DirectBattle;
    if (m_bInDialogue) return GameState::Dialogue;
    if (m_bStoreOpen || m_bStoreUnlockDlg) return GameState::Merchant;
    return GameState::Explore;   // includes inventory-open, an overlay atop Explore
}
