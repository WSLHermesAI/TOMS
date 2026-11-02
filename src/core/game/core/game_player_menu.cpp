// game_player_menu.cpp — the player menu (docs/gameplay/03_PLAYER_MENU.md): the walking scene's one button (≡,
// Esc) opens five tabs -- Status, Equipment, Items, Events, System. This file keeps its state, its
// keyboard and mouse handling, and builds its part of the UI (UiPlayer; the System tab's rows are
// UiMenu, built by buildMenuUi in game_ui.cpp). The screen itself is assets/media/ui/player.rml.
#include "game_internal.h"
#include "player_menu_rules.h"   // stacking, the grid cursor, dropping (unit-tested)

using namespace toms::game_detail;

namespace {

int f_ClampSel(int l_iSel, int l_iN) { return l_iN <= 0 ? 0 : std::max(0, std::min(l_iSel, l_iN - 1)); }

}  // namespace
using toms::f_GridMove;

// ---------- opening, closing, tabs ----------

void Game::f_OpenPlayerMenu(int l_iTab) {
    if (!m_bInGameMenuOpen) {
        m_bInGameMenuOpen = true;
        m_eInGameMenuPage = InGameMenuPage::Main;
        m_iInGameMenuSel = 0;
        m_bInGameLangConfirmOpen = false;
        m_bDropConfirmOpen = false;
        m_oAudio.f_Play("confirm_click");
    }
    if (l_iTab >= 0 && l_iTab < m_iMenuTabs) { m_eMenuTab = (MenuTab)l_iTab; m_bSaveLoadOpen = m_bSaveLoadConfirm = false; }
    f_PlayerMenuTab(0);   // marks the tab's news as seen
}

void Game::f_ClosePlayerMenu() {
    if (!m_bInGameMenuOpen) return;
    m_bInGameMenuOpen = false;
    m_bSaveLoadOpen = m_bSaveLoadConfirm = false;
    m_eInGameMenuPage = InGameMenuPage::Main;
    m_bInGameLangConfirmOpen = false;
    m_bDropConfirmOpen = false;
    m_mapAttrDraft.clear();   // points not confirmed are not kept
    m_oAudio.f_Play("close_ui");
}

void Game::f_ToggleInventory() {
    if (m_bInGameMenuOpen && m_eMenuTab == MenuTab::Items) { f_ClosePlayerMenu(); return; }
    if (m_bInGameMenuOpen) { m_eMenuTab = MenuTab::Items; m_bSaveLoadOpen = m_bSaveLoadConfirm = false; f_PlayerMenuTab(0); return; }
    if (f_ModalActive()) return;
    f_CancelWalk();
    f_OpenPlayerMenu((int)MenuTab::Items);
}

void Game::f_PlayerMenuTab(int l_iDelta) {
    if (!m_bInGameMenuOpen) return;
    if (m_bSaveLoadOpen) {   // Q / E / Tab: Save <-> Load
        if (l_iDelta != 0 && !m_bSaveLoadConfirm) { m_iSaveLoadMode = 1 - m_iSaveLoadMode; m_oAudio.f_Play("confirm_click"); }
        return;
    }
    if (l_iDelta != 0) {
        m_eMenuTab = (MenuTab)((((int)m_eMenuTab + l_iDelta) % m_iMenuTabs + m_iMenuTabs) % m_iMenuTabs);
        m_bDropConfirmOpen = false;
        m_bInGameLangConfirmOpen = false;
        m_oAudio.f_Play("confirm_click");
    }
    if (m_eMenuTab == MenuTab::Gear) m_bNewsGear = false;
    if (m_eMenuTab == MenuTab::Events) m_bNewsEvents = false;
    if (m_eMenuTab == MenuTab::Cards) m_bNewsCards = false;
}

// ---------- the Items and Equipment grids ----------

std::vector<Game::ItemCell> Game::f_ItemCells() const {
    std::vector<ItemCell> l_vOut;
    const bool l_bAll = m_iItemFilter == 0;
    if (l_bAll || m_iItemFilter == 1)                       // the backpack, copies stacked
        for (const toms::StackedItem& l_oS : toms::f_StackItems(m_oPl.m_vInv)) l_vOut.push_back({ItemCell::Inv, l_oS.m_strId, l_oS.m_iCount});
    if (l_bAll || m_iItemFilter == 3) {                     // keys are counters on the player, not backpack items
        const std::pair<const char*, int> l_arrKeys[] = {{"key_yellow", m_oPl.m_iKeyYellow}, {"key_blue", m_oPl.m_iKeyBlue}, {"key_red", m_oPl.m_iKeyRed}};
        for (const auto& l_pairK : l_arrKeys)
            if (l_pairK.second > 0) l_vOut.push_back({ItemCell::Key, l_pairK.first, l_pairK.second});
    }
    if (l_bAll || m_iItemFilter == 2)                       // owned weapons, armor, talents
        for (const std::string& l_strId : m_vGearOwned) l_vOut.push_back({ItemCell::Gear, l_strId, 1});
    return l_vOut;
}

std::vector<std::string> Game::f_GearCells() const {
    const toms::EquipmentSlot l_eWant = m_iGearSlot == 0 ? toms::EquipmentSlot::Weapon : toms::EquipmentSlot::Armor;
    std::vector<std::string> l_vOut;
    for (const std::string& l_strId : m_vGearOwned) {
        auto l_itIt = m_mapEquipmentDefs.find(l_strId);
        if (l_itIt != m_mapEquipmentDefs.end() && l_itIt->second.m_eSlot == l_eWant) l_vOut.push_back(l_strId);
    }
    return l_vOut;
}

static bool f_Worn(const toms::EquippedSet& l_oEq, const std::string& l_strId) {
    return !l_strId.empty() && (l_strId == l_oEq.m_strWeaponId || l_strId == l_oEq.m_strArmorId || l_strId == l_oEq.m_strTalentId);
}

std::vector<Game::ItemButton> Game::f_ItemButtons(const ItemCell& l_oC) const {
    std::vector<ItemButton> l_vOut;
    const bool l_bImportant = f_ItemImportant(l_oC.m_strId) || l_oC.m_eKind == ItemCell::Key;
    if (l_oC.m_eKind == ItemCell::Inv) {
        // a story item (a gate's key) has nothing to use: using it would only lose it
        l_vOut.push_back({ItemAction::Use, f_ItemType(l_oC.m_strId) != "story"});
        l_vOut.push_back({ItemAction::Drop, toms::f_CanDrop(l_bImportant, false)});
    } else if (l_oC.m_eKind == ItemCell::Key) {
        l_vOut.push_back({ItemAction::Drop, false});
    } else {
        const bool l_bIsWorn = f_Worn(m_oEquipped, l_oC.m_strId);
        auto l_itIt = m_mapEquipmentDefs.find(l_oC.m_strId);
        const bool l_bWeapon = l_itIt != m_mapEquipmentDefs.end() && l_itIt->second.m_eSlot == toms::EquipmentSlot::Weapon;
        if (!l_bIsWorn) l_vOut.push_back({ItemAction::Equip, true});
        else if (!l_bWeapon) l_vOut.push_back({ItemAction::Unequip, true});   // a weapon is swapped, never taken off
        l_vOut.push_back({ItemAction::Drop, toms::f_CanDrop(l_bImportant, l_bIsWorn)});
    }
    return l_vOut;
}

void Game::f_RunItemAction(const ItemCell& l_oC, ItemAction l_eA) {
    switch (l_eA) {
        case ItemAction::Use: {
            // The last copy goes, so the stack keeps its place in the grid (cells follow first pickups).
            auto l_itIt = std::find(m_oPl.m_vInv.rbegin(), m_oPl.m_vInv.rend(), l_oC.m_strId);
            if (l_itIt == m_oPl.m_vInv.rend()) return;
            m_oPl.m_vInv.erase(std::next(l_itIt).base());
            const std::string l_strStage = m_strCurStage;
            f_ApplyItem(l_oC.m_strId);
            f_MarkProgressDirty();
            if (m_strCurStage != l_strStage) f_ClosePlayerMenu();   // a scroll took the player to another floor
            break;
        }
        case ItemAction::Equip:
            f_EquipGear(l_oC.m_strId);
            m_oAudio.f_Play("confirm_click");
            break;
        case ItemAction::Unequip: {
            if (m_oEquipped.m_strArmorId == l_oC.m_strId) m_oEquipped.m_strArmorId.clear();
            if (m_oEquipped.m_strTalentId == l_oC.m_strId) m_oEquipped.m_strTalentId.clear();
            f_MarkProgressDirty();
            m_oAudio.f_Play("close_ui");
            break;
        }
        case ItemAction::Drop: {
            if (l_oC.m_eKind == ItemCell::Inv) {
                auto l_itIt = std::find(m_oPl.m_vInv.rbegin(), m_oPl.m_vInv.rend(), l_oC.m_strId);
                if (l_itIt != m_oPl.m_vInv.rend()) m_oPl.m_vInv.erase(std::next(l_itIt).base());
            } else if (l_oC.m_eKind == ItemCell::Gear && !f_Worn(m_oEquipped, l_oC.m_strId)) {
                m_vGearOwned.erase(std::remove(m_vGearOwned.begin(), m_vGearOwned.end(), l_oC.m_strId), m_vGearOwned.end());
            }
            f_MarkProgressDirty();
            m_oAudio.f_Play("close_ui");
            break;
        }
    }
}

// ---------- the Events tab ----------

std::vector<Game::LogEntry> Game::f_EventLog(bool l_bAll) const {
    std::vector<LogEntry> l_vOpen, l_vDone;
    auto l_fnText = [&](const std::string& l_strKey) {
        const std::string l_strT = m_oLocale.f_Tr(l_strKey);
        return l_strT == l_strKey ? std::string() : l_strT;
    };
    // Chapters: listed once the player has set foot in one of their floors; finished when their last
    // floor (the boss floor) is cleared -- the chapters' exitConditions, `stageCleared <boss floor>`.
    if (f_FloorMode()) {
        const std::string l_strHere = m_strCurFloorId.empty() ? m_oRun.f_Floor() : m_strCurFloorId;
        std::vector<std::string> l_vActs;
        for (const toms::FloorInfo& l_oF : m_oFloors.f_All())
            if (std::find(l_vActs.begin(), l_vActs.end(), l_oF.m_strAct) == l_vActs.end()) l_vActs.push_back(l_oF.m_strAct);
        for (const std::string& l_strAct : l_vActs) {
            bool l_bReached = false;
            std::string l_strLast, l_strKey;
            for (const toms::FloorInfo& l_oF : m_oFloors.f_All()) {
                if (l_oF.m_strAct != l_strAct) continue;
                l_strLast = l_oF.m_strId; l_strKey = l_oF.m_strActKey;
                if (l_oF.m_strId == l_strHere || m_oRun.f_FloorCleared(l_oF.m_strId)) l_bReached = true;
            }
            if (!l_bReached) continue;
            LogEntry l_oE;
            l_oE.m_iKind = 0;
            l_oE.m_strId = l_strAct;
            l_oE.m_strTitle = l_fnText("story." + l_strKey + ".title");
            if (l_oE.m_strTitle.empty()) l_oE.m_strTitle = l_strAct;
            l_oE.m_strDesc = l_fnText("story." + l_strKey + ".desc");
            l_oE.m_bDone = m_oRun.f_FloorCleared(l_strLast);
            (l_oE.m_bDone ? l_vDone : l_vOpen).push_back(l_oE);
        }
        std::reverse(l_vDone.begin(), l_vDone.end());   // the newest finished chapter first
    }
    // Story events: listed once started (met), finished once fired; only those whose kind (or own record)
    // is "inLog" and that have a pool record.
    auto l_fnEventName = [&](const std::string& l_strId) {
        auto l_itD = m_mapEventDefs.find(l_strId);
        if (l_itD != m_mapEventDefs.end() && !l_itD->second.m_strStairsFrom.empty())   // Q7: "Go to the stairs on floor 3"
            return toms::f_FillText(m_oLocale.f_Tr("stairs.go.title"), {{"from", toms::f_FloorLabel(l_itD->second.m_strStairsFrom)},
                                                                  {"to", toms::f_FloorLabel(l_itD->second.m_strStairsTo)}});
        std::string l_strT = l_itD == m_mapEventDefs.end() ? std::string() : l_fnText(l_itD->second.m_strTitleKey);
        return l_strT.empty() ? l_strId : l_strT;
    };
    auto l_fnConnectedNames = [&](const std::vector<std::string>& l_vIds) {
        std::vector<std::string> l_vOut;
        for (const std::string& l_strC : l_vIds) {
            if (!m_oRun.f_EventStarted(l_strC)) l_vOut.push_back(m_oLocale.f_Tr("events.unknown"));   // not met yet: no spoilers
            else l_vOut.push_back(l_fnEventName(l_strC) + (m_oRun.f_EventFinished(l_strC) ? "  ✓" : ""));
        }
        return l_vOut;
    };
    for (LogEntry& l_oE : l_vOpen) if (l_oE.m_iKind == 0) l_oE.m_vConnected = l_fnConnectedNames(m_mapChapterEvents.count(l_oE.m_strId) ? m_mapChapterEvents.at(l_oE.m_strId) : std::vector<std::string>{});
    for (LogEntry& l_oE : l_vDone) if (l_oE.m_iKind == 0) l_oE.m_vConnected = l_fnConnectedNames(m_mapChapterEvents.count(l_oE.m_strId) ? m_mapChapterEvents.at(l_oE.m_strId) : std::vector<std::string>{});
    std::vector<LogEntry> l_vEventsDone;
    auto l_fnEventEntry = [&](const std::string& l_strId, bool l_bIsDone) {
        LogEntry l_oE;
        l_oE.m_iKind = 2;
        l_oE.m_strId = l_strId;
        l_oE.m_strTitle = l_fnEventName(l_strId);
        const EventDef& l_oDef = m_mapEventDefs.at(l_strId);
        if (!l_oDef.m_strStairsFrom.empty()) {
            l_oE.m_strDesc = toms::f_FillText(m_oLocale.f_Tr("stairs.go.desc"), {{"from", toms::f_FloorLabel(l_oDef.m_strStairsFrom)},
                                                                   {"to", toms::f_FloorLabel(l_oDef.m_strStairsTo)}});
            // W38: where they are -- on this floor the arrow marks them; elsewhere, the floor and the spot
            if (!l_bIsDone) {
                const auto l_itSpot = m_mapStairSpots.find(l_oDef.m_strStairsFrom + ">" + l_oDef.m_strStairsTo);
                if (l_oDef.m_strStairsFrom == m_oSt.m_strId) l_oE.m_strDesc += " " + m_oLocale.f_Tr("stairs.go.here");
                else if (l_itSpot != m_mapStairSpots.end())
                    l_oE.m_strDesc += " " + toms::f_FillText(m_oLocale.f_Tr("stairs.go.where"), {{"from", toms::f_FloorLabel(l_oDef.m_strStairsFrom)},
                                                                                {"x", std::to_string(l_itSpot->second.first)},
                                                                                {"y", std::to_string(l_itSpot->second.second)}});
                else
                    l_oE.m_strDesc += " " + toms::f_FillText(m_oLocale.f_Tr("stairs.go.floor"), {{"from", toms::f_FloorLabel(l_oDef.m_strStairsFrom)}});
            }
        } else
            l_oE.m_strDesc = l_fnText(l_oDef.m_strDescKey);
        // Q7: an event that opens stairs says so in its description.
        if (auto l_itU = m_mapStairUnlocks.find(l_strId); l_itU != m_mapStairUnlocks.end())
            for (const StairUnlock& l_oS : l_itU->second)
            {
                l_oE.m_strDesc += (l_oE.m_strDesc.empty() ? "" : " ") +
                          toms::f_FillText(m_oLocale.f_Tr("stairs.unlocks"), {{"from", toms::f_FloorLabel(l_oS.m_strFloor)}, {"to", toms::f_FloorLabel(l_oS.m_strTo)}});
                // W38: a stair that waits for several events says how many are done
                if (const toms::StairLink* l_pLink = f_FindStair(l_oS.m_strFloor, l_oS.m_strTo); l_pLink && l_pLink->m_vUnlockEvents.size() > 1) {
                    int l_iDone = 0;
                    for (const std::string& l_strEv : l_pLink->m_vUnlockEvents) if (m_oRun.f_EventFinished(l_strEv)) l_iDone++;
                    l_oE.m_strDesc += " " + toms::f_FillText(m_oLocale.f_Tr("stairs.unlocks.count"), {{"done", std::to_string(l_iDone)},
                                                                                       {"all", std::to_string(l_pLink->m_vUnlockEvents.size())}});
                }
            }
        l_oE.m_bDone = l_bIsDone;
        l_oE.m_vConnected = l_fnConnectedNames(f_ConnectedEvents(l_strId));
        return l_oE;
    };
    for (const std::string& l_strId : m_oRun.f_EventsStarted()) {
        auto l_itD = m_mapEventDefs.find(l_strId);
        if (l_itD == m_mapEventDefs.end() || !l_itD->second.m_bInLog || m_oRun.f_EventFinished(l_strId)) continue;
        l_vOpen.push_back(l_fnEventEntry(l_strId, false));
    }
    const auto& l_vFin = m_oRun.f_EventsFinished();
    for (auto l_itIt = l_vFin.rbegin(); l_itIt != l_vFin.rend(); ++l_itIt) {   // the newest first
        auto l_itD = m_mapEventDefs.find(*l_itIt);
        if (l_itD != m_mapEventDefs.end() && l_itD->second.m_bInLog) l_vEventsDone.push_back(l_fnEventEntry(*l_itIt, true));
    }
    // Missions: listed once accepted; finished once the reward is claimed.
    for (const auto& [l_strId, l_oT] : m_mapMissionTrackers) {
        if (l_oT.m_eState != toms::MissionState::Active && l_oT.m_eState != toms::MissionState::Completed &&
            l_oT.m_eState != toms::MissionState::Claimed) continue;
        LogEntry l_oE;
        l_oE.m_iKind = 1;
        l_oE.m_strId = l_strId;
        l_oE.m_strTitle = l_fnText("mission." + l_strId + ".title");
        if (l_oE.m_strTitle.empty()) l_oE.m_strTitle = l_strId;
        l_oE.m_strDesc = l_fnText("mission." + l_strId + ".desc");
        l_oE.m_bDone = l_oT.m_eState == toms::MissionState::Claimed;
        (l_oE.m_bDone ? l_vDone : l_vOpen).push_back(l_oE);
    }
    l_vDone.insert(l_vDone.end(), l_vEventsDone.begin(), l_vEventsDone.end());
    if (l_bAll) l_vOpen.insert(l_vOpen.end(), l_vDone.begin(), l_vDone.end());
    return l_vOpen;
}

// The event ids a `requires` waits for: runFlagSet "event_<id>" (set when an event fires) or eventDone "<id>".
static void f_EventsWaitedFor(const nlohmann::json& l_jJ, std::vector<std::string>& l_vOut) {
    if (l_jJ.is_array()) { for (const auto& l_jX : l_jJ) f_EventsWaitedFor(l_jX, l_vOut); return; }
    if (!l_jJ.is_object()) return;
    for (const char* l_pszGroup : {"all", "any", "not"})
        if (l_jJ.contains(l_pszGroup)) f_EventsWaitedFor(l_jJ[l_pszGroup], l_vOut);
    const std::string l_strFlag = l_jJ.value("flag", std::string());
    if (l_jJ.value("type", std::string()) == "runFlagSet" && l_strFlag.rfind("event_", 0) == 0) l_vOut.push_back(l_strFlag.substr(6));
    if (l_jJ.contains("eventDone") && l_jJ["eventDone"].is_string()) l_vOut.push_back(l_jJ["eventDone"].get<std::string>());
}

std::vector<std::string> Game::f_ConnectedEvents(const std::string& l_strEventId) const {
    std::vector<std::string> l_vOut;
    auto l_fnAdd = [&](const std::string& l_strId) {
        if (!l_strId.empty() && l_strId != l_strEventId && m_mapEventDefs.count(l_strId) && std::find(l_vOut.begin(), l_vOut.end(), l_strId) == l_vOut.end()) l_vOut.push_back(l_strId);
    };
    auto l_itSelf = m_mapEventDefs.find(l_strEventId);
    if (l_itSelf != m_mapEventDefs.end()) {
        for (const std::string& l_strN : l_itSelf->second.m_vNext) l_fnAdd(l_strN);              // what it leads to
        std::vector<std::string> l_vWaits;
        f_EventsWaitedFor(l_itSelf->second.m_jRequires, l_vWaits);                      // what it waits for
        for (const std::string& l_strW : l_vWaits) l_fnAdd(l_strW);
    }
    for (const auto& [l_strEv, l_vList] : m_mapStairUnlocks)                            // Q7: the stairs an event opens
        for (const StairUnlock& l_oS : l_vList) {
            const std::string l_strGo = toms::f_GoStairsEventId(l_oS.m_strFloor, l_oS.m_strTo);
            if (l_strEv == l_strEventId) l_fnAdd(l_strGo);
            if (l_strGo == l_strEventId) l_fnAdd(l_strEv);
        }
    for (const auto& [l_strId, l_oD] : m_mapEventDefs) {                                // what leads here, or waits for it
        if (std::find(l_oD.m_vNext.begin(), l_oD.m_vNext.end(), l_strEventId) != l_oD.m_vNext.end()) l_fnAdd(l_strId);
        std::vector<std::string> l_vWaits;
        f_EventsWaitedFor(l_oD.m_jRequires, l_vWaits);
        if (std::find(l_vWaits.begin(), l_vWaits.end(), l_strEventId) != l_vWaits.end()) l_fnAdd(l_strId);
    }
    return l_vOut;
}

void Game::f_LoadEventDefs() {
    const std::string l_strDir = f_PathOf(toms::key::g_pszFolderEvents) + "/";
    // kinds.json (written by the event editor's Kinds tab): "inLog": false keeps a kind out of the log.
    std::map<std::string, bool> l_mapKindInLog;
    nlohmann::json l_jKinds = f_ReadJsonFile(f_PathOf(toms::key::g_pszDataEventKinds));
    if (l_jKinds.is_object())
        for (auto& [l_strK, l_jV] : l_jKinds.items())
            if (l_jV.is_object() && l_jV.contains("inLog")) l_mapKindInLog[l_strK] = l_jV.value("inLog", true);
    std::vector<std::string> l_vPoolNames = toms::f_VfsListDir(l_strDir);
    std::sort(l_vPoolNames.begin(), l_vPoolNames.end());   // pool order = file-name order (the NPC topic menu's order)
    std::vector<nlohmann::json> l_vPoolEvents;
    for (const std::string& l_strName : l_vPoolNames) {
        if (l_strName.rfind("pool_", 0) != 0 || l_strName.size() < 5 || l_strName.compare(l_strName.size() - 5, 5, ".json") != 0) continue;
        nlohmann::json l_jPool = f_ReadJsonFile(l_strDir + l_strName);
        if (!l_jPool.contains("events") || !l_jPool["events"].is_array()) continue;
        l_vPoolEvents.push_back(l_jPool["events"]);
        for (const auto& l_jE : l_jPool["events"]) {
            const std::string l_strId = l_jE.value("eventId", std::string());
            if (l_strId.empty()) continue;
            EventDef l_oD;
            l_oD.m_strKind = l_jE.value("kind", std::string());
            l_oD.m_strTitleKey = l_jE.value("title", l_strId + ".title");
            l_oD.m_strDescKey = l_jE.value("desc", l_strId + ".desc");
            if (l_jE.contains("next") && l_jE["next"].is_array())
                for (const auto& l_jN : l_jE["next"]) if (l_jN.is_string()) l_oD.m_vNext.push_back(l_jN.get<std::string>());
            if (l_jE.contains("requires")) l_oD.m_jRequires = l_jE["requires"];
            if (l_jE.contains("actions") && l_jE["actions"].is_array()) l_oD.m_jActions = l_jE["actions"];
            if (l_jE.contains("trigger") && l_jE["trigger"].is_object()) {
                l_oD.m_strTrigger = l_jE["trigger"].value("on", std::string("step"));
                l_oD.m_strTriggerVar = l_jE["trigger"].value("var", std::string());
            }
            l_oD.m_bOnce = l_jE.value("once", true);
            l_oD.m_iMaxTimes = std::max(0, l_jE.value("maxTimes", 0));
            l_oD.m_strTextKey = l_jE.value("text", l_strId + ".text");
            auto l_itK = l_mapKindInLog.find(l_oD.m_strKind);
            l_oD.m_bInLog = l_jE.value("inLog", l_itK == l_mapKindInLog.end() ? true : l_itK->second);
            m_mapEventDefs[l_strId] = l_oD;
        }
    }
    // The events bound to NPCs (trigger "talk"), in menu order (docs/gameplay/event_editor/08_NPC_DIALOGUE.md).
    m_mapNpcTopics = toms::talk::f_CollectTopics(l_vPoolEvents);
    // The chapters' story events (beats of kind "event"): a chapter's connected events in the log.
    const std::string l_strChDir = f_PathOf(toms::key::g_pszFolderChapters) + "/";
    for (const std::string& l_strName : toms::f_VfsListDir(l_strChDir)) {
        if (l_strName.size() < 5 || l_strName.compare(l_strName.size() - 5, 5, ".json") != 0) continue;
        nlohmann::json l_jCh = f_ReadJsonFile(l_strChDir + l_strName);
        const std::string l_strId = l_jCh.value("id", std::string());
        if (l_strId.empty() || !l_jCh.contains("beats") || !l_jCh["beats"].is_array()) continue;
        for (const auto& l_jB : l_jCh["beats"])
            if (l_jB.value("kind", std::string()) == "event" && l_jB.contains("eventId")) m_mapChapterEvents[l_strId].push_back(l_jB.value("eventId", std::string()));
    }
    fprintf(stderr, "[assets] events: %zu in the pools, %zu chapters with story events\n", m_mapEventDefs.size(), m_mapChapterEvents.size());
}

// ---------- keyboard ----------

void Game::f_PlayerMenuMove(int l_iDx, int l_iDy) {
    if (!m_bInGameMenuOpen) return;
    if (m_bSaveLoadOpen) {
        if (m_bSaveLoadConfirm) { if (l_iDx != 0 || l_iDy != 0) m_bSaveLoadConfirmYes = !m_bSaveLoadConfirmYes; return; }
        const int l_iN = (int)m_vSaveLoadSlots.size();
        if (l_iDy != 0 && l_iN > 0) m_iSaveLoadSel = ((m_iSaveLoadSel + l_iDy) % l_iN + l_iN) % l_iN;
        if (l_iDx != 0) f_PlayerMenuTab(l_iDx);
        return;
    }
    if (m_bDropConfirmOpen) { if (l_iDx != 0 || l_iDy != 0) m_bDropConfirmYes = !m_bDropConfirmYes; return; }
    switch (m_eMenuTab) {
        case MenuTab::Status: {
            // the attributes, then (with a draft) Confirm and Reset as two more stops, so a keyboard can reach them
            const int l_iN = (int)m_vAttrDefs.size() + (m_mapAttrDraft.empty() ? 0 : 2);
            if (l_iDy != 0 && l_iN > 0) m_iAttrSel = m_iAttrSel < 0 ? 0 : ((m_iAttrSel + l_iDy) % l_iN + l_iN) % l_iN;
            break;
        }
        case MenuTab::Gear: {
            const int l_iN = (int)f_GearCells().size();
            m_iGearSel = f_ClampSel(m_iGearSel, l_iN);
            if (!f_GridMove(m_iGearSel, l_iN, l_iDx, l_iDy)) { m_iGearSlot = 1 - m_iGearSlot; m_iGearSel = 0; }
            break;
        }
        case MenuTab::Items: {
            const int l_iN = (int)f_ItemCells().size();
            m_iItemSel = f_ClampSel(m_iItemSel, l_iN);
            if (!f_GridMove(m_iItemSel, l_iN, l_iDx, l_iDy)) { m_iItemFilter = ((m_iItemFilter + (l_iDx > 0 ? 1 : -1)) % 4 + 4) % 4; m_iItemSel = 0; }
            break;
        }
        case MenuTab::Cards: {
            const int l_iN = (int)f_CardCells().size();
            m_iCardSel = f_ClampSel(m_iCardSel, l_iN);
            f_GridMove(m_iCardSel, l_iN, l_iDx, l_iDy);
            break;
        }
        case MenuTab::Events: {
            if (l_iDx != 0) { m_iEventFilter = 1 - m_iEventFilter; m_iEventSel = 0; break; }
            const int l_iN = (int)f_EventLog(m_iEventFilter == 1).size();
            if (l_iN > 0) m_iEventSel = ((f_ClampSel(m_iEventSel, l_iN) + l_iDy) % l_iN + l_iN) % l_iN;
            break;
        }
        case MenuTab::System:
            if (l_iDy != 0) f_InGameMenuMove(l_iDy);
            else if (l_iDx != 0 && m_bInGameLangConfirmOpen) f_InGameMenuMove(l_iDx);
            break;
    }
}

void Game::f_PlayerMenuActivate() {
    if (!m_bInGameMenuOpen) return;
    if (m_bSaveLoadOpen) { if (m_bSaveLoadConfirm) f_SaveLoadAnswer(m_bSaveLoadConfirmYes); else f_SaveLoadAsk(m_iSaveLoadSel); return; }
    if (m_bDropConfirmOpen) { f_PlayerMenuEvent("pm_confirm", m_bDropConfirmYes ? 1 : 0); return; }
    switch (m_eMenuTab) {
        case MenuTab::Status:   // Enter places a point on the selected attribute (in the draft: Confirm keeps it)
            if (m_iAttrSel == (int)m_vAttrDefs.size() && !m_mapAttrDraft.empty()) f_ConfirmAttrPoints();
            else if (m_iAttrSel == (int)m_vAttrDefs.size() + 1 && !m_mapAttrDraft.empty()) f_ResetAttrDraft();
            else if (m_oPl.m_iAttrPoints - f_AttrDraftTotal() > 0 && m_iAttrSel >= 0) f_SpendAttrPoint(m_iAttrSel);
            if (m_iAttrSel >= (int)m_vAttrDefs.size() && m_mapAttrDraft.empty()) m_iAttrSel = (int)m_vAttrDefs.size() - 1;   // the stops went
            return;
        case MenuTab::Gear: {
            const auto l_vCells = f_GearCells();
            if (l_vCells.empty()) return;
            const ItemCell l_oC{ItemCell::Gear, l_vCells[f_ClampSel(m_iGearSel, (int)l_vCells.size())], 1};
            for (const ItemButton& l_oB : f_ItemButtons(l_oC))
                if (l_oB.m_bEnabled && l_oB.m_eAction != ItemAction::Drop) { f_RunItemAction(l_oC, l_oB.m_eAction); return; }
            return;
        }
        case MenuTab::Items: {
            const auto l_vCells = f_ItemCells();
            if (l_vCells.empty()) return;
            const ItemCell l_oC = l_vCells[f_ClampSel(m_iItemSel, (int)l_vCells.size())];
            const auto l_vButtons = f_ItemButtons(l_oC);
            for (size_t l_nI = 0; l_nI < l_vButtons.size(); l_nI++)
                if (l_vButtons[l_nI].m_bEnabled) { f_PlayerMenuEvent("pm_btn", (int)l_nI); return; }
            return;
        }
        case MenuTab::Cards:   // Enter: Train with the selected card (docs/gameplay/16 §5)
            f_PlayerMenuEvent("pm_btn", 0);
            return;
        case MenuTab::System:
            f_InGameMenuActivate();
            return;
        default:
            return;
    }
}

void Game::f_PlayerMenuBack() {
    if (!m_bInGameMenuOpen) return;
    if (m_bSaveLoadOpen) {
        if (m_bSaveLoadConfirm) m_bSaveLoadConfirm = false;
        else m_bSaveLoadOpen = false;
        m_oAudio.f_Play("close_ui");
        return;
    }
    if (m_bDropConfirmOpen) { m_bDropConfirmOpen = false; m_oAudio.f_Play("close_ui"); return; }
    if (m_eMenuTab == MenuTab::System) { f_InGameMenuBack(); return; }   // confirm -> page -> close
    f_ClosePlayerMenu();
}

// ---------- mouse / touch (the pm_* buttons of player.rml) ----------

void Game::f_PlayerMenuEvent(const std::string& l_strName, int l_iArg) {
    if (!m_bInGameMenuOpen) return;
    if (l_strName == "pm_close") { f_ClosePlayerMenu(); return; }
    // ---- Save / Load ----
    if (l_strName == "pm_sl_close") { if (m_bSaveLoadOpen) { m_bSaveLoadOpen = m_bSaveLoadConfirm = false; m_oAudio.f_Play("close_ui"); } return; }
    if (l_strName == "pm_sl_confirm") { f_SaveLoadAnswer(l_iArg != 0); return; }
    if (m_bSaveLoadOpen) {   // the screen is modal over the menu
        if (m_bSaveLoadConfirm) return;
        if (l_strName == "pm_sl_mode" && (l_iArg == 0 || l_iArg == 1) && l_iArg != m_iSaveLoadMode) { m_iSaveLoadMode = l_iArg; m_oAudio.f_Play("confirm_click"); }
        if (l_strName == "pm_sl_slot") f_SaveLoadAsk(l_iArg);
        if (l_strName == "pm_sl_hover" && l_iArg >= 0 && l_iArg < (int)m_vSaveLoadSlots.size()) m_iSaveLoadSel = l_iArg;
        return;
    }
    if (l_strName == "pm_confirm") {
        if (!m_bDropConfirmOpen) return;
        m_bDropConfirmOpen = false;
        if (l_iArg && m_eMenuTab == MenuTab::Items) {
            const auto l_vCells = f_ItemCells();
            if (!l_vCells.empty()) {
                const ItemCell l_oC = l_vCells[f_ClampSel(m_iItemSel, (int)l_vCells.size())];
                for (const ItemButton& l_oB : f_ItemButtons(l_oC))
                    if (l_oB.m_eAction == ItemAction::Drop && l_oB.m_bEnabled) { f_RunItemAction(l_oC, ItemAction::Drop); break; }
                m_iItemSel = f_ClampSel(m_iItemSel, (int)f_ItemCells().size());
            }
        } else {
            m_oAudio.f_Play("close_ui");
        }
        return;
    }
    if (m_bDropConfirmOpen) return;   // the dialog is modal
    if (l_strName == "pm_tab") {
        if (l_iArg >= 0 && l_iArg < m_iMenuTabs && l_iArg != (int)m_eMenuTab) { m_eMenuTab = (MenuTab)l_iArg; f_PlayerMenuTab(0); m_oAudio.f_Play("confirm_click"); }
        return;
    }
    if (l_strName == "pm_attr") { if (l_iArg >= 0 && l_iArg < (int)m_vAttrDefs.size()) m_iAttrSel = l_iArg; return; }
    if (l_strName == "pm_attr_add") { if (l_iArg >= 0 && l_iArg < (int)m_vAttrDefs.size()) { m_iAttrSel = l_iArg; f_SpendAttrPoint(l_iArg); } return; }
    if (l_strName == "pm_attr_sub") { if (l_iArg >= 0 && l_iArg < (int)m_vAttrDefs.size()) { m_iAttrSel = l_iArg; f_UnspendAttrPoint(l_iArg); } return; }
    if (l_strName == "pm_attr_confirm") { f_ConfirmAttrPoints(); return; }
    if (l_strName == "pm_attr_reset") { f_ResetAttrDraft(); return; }
    if (l_strName == "pm_slot") { if ((l_iArg == 0 || l_iArg == 1) && l_iArg != m_iGearSlot) { m_iGearSlot = l_iArg; m_iGearSel = 0; } return; }
    if (l_strName == "pm_filter") {
        if (m_eMenuTab == MenuTab::Items && l_iArg >= 0 && l_iArg < 4 && l_iArg != m_iItemFilter) { m_iItemFilter = l_iArg; m_iItemSel = 0; }
        if (m_eMenuTab == MenuTab::Gear && (l_iArg == 0 || l_iArg == 1) && l_iArg != m_iGearSlot) { m_iGearSlot = l_iArg; m_iGearSel = 0; }
        if (m_eMenuTab == MenuTab::Events && (l_iArg == 0 || l_iArg == 1) && l_iArg != m_iEventFilter) { m_iEventFilter = l_iArg; m_iEventSel = 0; }
        return;
    }
    if (l_strName == "pm_cell") {
        if (m_eMenuTab == MenuTab::Items) m_iItemSel = f_ClampSel(l_iArg, (int)f_ItemCells().size());
        if (m_eMenuTab == MenuTab::Gear) m_iGearSel = f_ClampSel(l_iArg, (int)f_GearCells().size());
        if (m_eMenuTab == MenuTab::Cards) m_iCardSel = f_ClampSel(l_iArg, (int)f_CardCells().size());
        return;
    }
    if (l_strName == "pm_btn") {
        ItemCell l_oC;
        if (m_eMenuTab == MenuTab::Items) {
            const auto l_vCells = f_ItemCells();
            if (l_vCells.empty()) return;
            l_oC = l_vCells[f_ClampSel(m_iItemSel, (int)l_vCells.size())];
        } else if (m_eMenuTab == MenuTab::Gear) {
            const auto l_vCells = f_GearCells();
            if (l_vCells.empty()) return;
            l_oC = {ItemCell::Gear, l_vCells[f_ClampSel(m_iGearSel, (int)l_vCells.size())], 1};
        } else if (m_eMenuTab == MenuTab::Cards) {   // Train: the training ground with the selected monster (16 §5)
            const auto l_vCells = f_CardCells();
            if (!l_vCells.empty() && l_iArg == 0) f_StartTraining(l_vCells[(size_t)f_ClampSel(m_iCardSel, (int)l_vCells.size())]);
            return;
        } else {
            return;
        }
        const auto l_vButtons = f_ItemButtons(l_oC);
        if (l_iArg < 0 || l_iArg >= (int)l_vButtons.size() || !l_vButtons[l_iArg].m_bEnabled) { m_oAudio.f_Play("deny"); return; }
        if (l_vButtons[l_iArg].m_eAction == ItemAction::Drop) { m_bDropConfirmOpen = true; m_bDropConfirmYes = false; return; }
        f_RunItemAction(l_oC, l_vButtons[l_iArg].m_eAction);
        if (m_eMenuTab == MenuTab::Items) m_iItemSel = f_ClampSel(m_iItemSel, (int)f_ItemCells().size());
        return;
    }
    if (l_strName == "pm_event") { m_iEventSel = f_ClampSel(l_iArg, (int)f_EventLog(m_iEventFilter == 1).size()); return; }
    if (l_strName == "pm_sys") {   // a row of the System tab's left list (a page may be open on the right)
        if (m_bInGameLangConfirmOpen) return;
        m_eInGameMenuPage = InGameMenuPage::Main;
        m_iInGameMenuSel = l_iArg;
        f_InGameMenuActivate();
        return;
    }
    if (l_strName == "pm_sys_hover") { if (m_eInGameMenuPage == InGameMenuPage::Main && !m_bInGameLangConfirmOpen) m_iInGameMenuSel = l_iArg; return; }
}

// ---------- the UI ----------

void Game::f_BuildPlayerUi(toms::UiState& l_oU) const {
    const toms::Locale& l_oL = m_oLocale;
    toms::UiPlayer& l_oP = l_oU.m_oPlayer;
    l_oP.m_bVisible = true;
    l_oP.m_iTab = (int)m_eMenuTab;
    l_oP.m_strHint = l_oL.f_Tr("player.hint");
    l_oP.m_strCloseLabel = l_oL.f_Tr("inventory.close");
    static const char* l_arrTabKeys[m_iMenuTabs] = {"player.tab.status", "player.tab.gear", "player.tab.items", "player.tab.events",
                                              "player.tab.cards", "player.tab.system"};
    const bool l_arrTabNews[m_iMenuTabs] = {false, m_bNewsGear, false, m_bNewsEvents, m_bNewsCards, m_bNewsStore || m_oRun.f_SkillPoints() > 0};
    for (int l_iI = 0; l_iI < m_iMenuTabs; l_iI++) l_oP.m_vTabs.push_back({l_oL.f_Tr(l_arrTabKeys[l_iI]), l_iI == (int)m_eMenuTab, l_arrTabNews[l_iI] && l_iI != (int)m_eMenuTab});
    const std::string l_strNone = l_oL.f_Tr("gear.none");
    auto l_fnSignedNum = [](int l_iV) { return (l_iV >= 0 ? "+" : "") + std::to_string(l_iV); };

    // The gear comparison: a piece against what its slot wears now.
    auto l_fnCompare = [&](const std::string& l_strId) {
        auto l_itIt = m_mapEquipmentDefs.find(l_strId);
        if (l_itIt == m_mapEquipmentDefs.end() || l_itIt->second.m_eSlot == toms::EquipmentSlot::Talent) return;
        const toms::EquipmentDefinition& l_oD = l_itIt->second;
        const bool l_bWeapon = l_oD.m_eSlot == toms::EquipmentSlot::Weapon;
        const std::string& l_strWornId = l_bWeapon ? m_oEquipped.m_strWeaponId : m_oEquipped.m_strArmorId;
        const toms::EquipmentDefinition* l_pCur = nullptr;
        if (auto l_itW = m_mapEquipmentDefs.find(l_strWornId); l_itW != m_mapEquipmentDefs.end()) l_pCur = &l_itW->second;
        l_oP.m_bDCompare = true;
        l_oP.m_strDNowLabel = l_oL.f_Tr("gear.now");
        l_oP.m_strDWithLabel = l_oL.f_Tr("gear.with");
        auto l_fnLine = [&](const std::string& l_strLabel, double l_dNow, double l_dWith, bool l_bHigherBetter, int l_iDecimals, const std::string& l_strUnit) {
            char l_pszA[32], l_pszB[32];
            std::snprintf(l_pszA, sizeof l_pszA, "%.*f%s", l_iDecimals, l_dNow, l_strUnit.c_str());
            std::snprintf(l_pszB, sizeof l_pszB, "%.*f%s", l_iDecimals, l_dWith, l_strUnit.c_str());
            int l_iTone = 0;
            if (l_dWith > l_dNow + 1e-6) l_iTone = l_bHigherBetter ? 1 : -1;
            if (l_dWith < l_dNow - 1e-6) l_iTone = l_bHigherBetter ? -1 : 1;
            l_oP.m_vDCompareLines.push_back({l_strLabel, l_pszA, l_pszB, l_iTone});
        };
        if (l_bWeapon) {
            const int l_iBase = f_EffectiveAtk() - (l_pCur ? l_pCur->m_iStatAtk : 0);
            l_fnLine("ATK", f_EffectiveAtk(), l_iBase + l_oD.m_iStatAtk, true, 0, "");
        } else {
            const int l_iBase = f_EffectiveDef() - (l_pCur ? l_pCur->m_iStatDef : 0);
            l_fnLine("DEF", f_EffectiveDef(), l_iBase + l_oD.m_iStatDef, true, 0, "");
        }
        const toms::PowerBarParams l_oNowBar = l_pCur ? l_pCur->m_oBar : toms::PowerBarParams{};
        l_fnLine(l_oL.f_Tr("gear.ramp"), l_oNowBar.m_fRampTime, l_oD.m_oBar.m_fRampTime, false, 1, "s");
        l_fnLine(l_oL.f_Tr("gear.zone"), l_oNowBar.m_fGreenHalf, l_oD.m_oBar.m_fGreenHalf, true, 0, "");
        if (l_bWeapon) l_fnLine(l_oL.f_Tr("gear.mult"), l_pCur ? l_pCur->m_fMaxMult : 2.0f, l_oD.m_fMaxMult, true, 1, "x");
        for (const AttrDef& l_oA : m_vAttrDefs) {
            const int l_iNowBonus = l_pCur && l_pCur->m_mapStats.count(l_oA.m_strId) ? l_pCur->m_mapStats.at(l_oA.m_strId) : 0;
            const int l_iNewBonus = l_oD.m_mapStats.count(l_oA.m_strId) ? l_oD.m_mapStats.at(l_oA.m_strId) : 0;
            if (l_iNowBonus == 0 && l_iNewBonus == 0) continue;
            const int l_iV = f_AttrValue(l_oA.m_strId);
            l_fnLine(l_oA.m_strShortName, l_iV, l_iV - l_iNowBonus + l_iNewBonus, true, 0, "");
        }
        auto l_fnActiveNames = [&](const toms::EquipmentDefinition* l_pE) {
            std::string l_strS;
            if (l_pE) for (const std::string& l_strA : l_pE->m_vActives) l_strS += (l_strS.empty() ? "" : ", ") + l_strA;
            return l_strS.empty() ? l_strNone : l_strS;
        };
        const std::string l_strNa = l_fnActiveNames(l_pCur), l_strNb = l_fnActiveNames(&l_oD);
        if (l_strNa != l_strNone || l_strNb != l_strNone) l_oP.m_vDCompareLines.push_back({l_oL.f_Tr("gear.skill"), l_strNa, l_strNb, 0});
    };
    auto l_fnButtonsUi = [&](const std::vector<ItemButton>& l_vBs) {
        for (const ItemButton& l_oB : l_vBs) {
            const char* l_pszKey = l_oB.m_eAction == ItemAction::Use ? "inventory.use" : l_oB.m_eAction == ItemAction::Equip ? "gear.equip"
                            : l_oB.m_eAction == ItemAction::Unequip ? "gear.unequip" : "inventory.drop";
            l_oP.m_vDButtons.push_back({l_oL.f_Tr(l_pszKey), l_oB.m_bEnabled});
        }
    };
    auto l_fnItemDetail = [&](const ItemCell& l_oC) {
        l_oP.m_bDetail = true;
        l_oP.m_strDIcon = f_ItemSprite(l_oC.m_strId);
        l_oP.m_strDName = f_ItemName(l_oC.m_strId);
        l_oP.m_strDDesc = f_ItemDesc(l_oC.m_strId);
        l_oP.m_vDLines.push_back({l_oL.f_Tr("store.effect_label"), f_ItemEffectSummary(l_oC.m_strId), "", 0});
        const int l_iPrice = f_ItemPrice(l_oC.m_strId);
        l_oP.m_vDLines.push_back({l_oL.f_Tr("store.price_label"),
                             l_iPrice >= 0 ? f_TrParam(l_oL.f_Tr("items.price_value"), "n", std::to_string(l_iPrice)) : l_oL.f_Tr("items.not_sold"), "", 0});
        l_oP.m_vDLines.push_back({l_oL.f_Tr("items.have"), std::to_string(l_oC.m_iCount), "", 0});
        if (l_oC.m_eKind == ItemCell::Gear && !f_Worn(m_oEquipped, l_oC.m_strId)) l_fnCompare(l_oC.m_strId);
        l_fnButtonsUi(f_ItemButtons(l_oC));
        if (l_oC.m_eKind == ItemCell::Key || f_ItemImportant(l_oC.m_strId)) l_oP.m_strDNote = l_oL.f_Tr("items.important");
        else if (l_oC.m_eKind == ItemCell::Gear && f_Worn(m_oEquipped, l_oC.m_strId)) l_oP.m_strDNote = l_oL.f_Tr("items.worn");
    };

    // Status, Events and System are filled every frame whichever tab shows: their lists sit in
    // documents that stay bound while hidden. Equipment and Items share one block, filled for the tab shown.
    {
        {
            const int l_iNeed = m_oPl.m_iLv * std::max(1, m_oLevelUp.m_iExpPerLevel);
            l_oP.m_strLevel = "Lv " + std::to_string(m_oPl.m_iLv);
            const int l_iMaxHp = f_EffectiveMaxHp();
            l_oP.m_strHpText = "HP " + std::to_string(m_oPl.m_iHp) + " / " + std::to_string(l_iMaxHp);
            l_oP.m_fHpPct = std::max(0.0f, std::min(100.0f, 100.0f * m_oPl.m_iHp / l_iMaxHp));
            l_oP.m_strExpText = "EXP " + std::to_string(m_oPl.m_iExp) + " / " + std::to_string(l_iNeed);
            l_oP.m_fExpPct = std::max(0.0f, std::min(100.0f, 100.0f * m_oPl.m_iExp / l_iNeed));
            l_oP.m_strCombatTitle = l_oL.f_Tr("status.combat");
            auto l_fnWithBonus = [&](int l_iEff, int l_iBase) { return std::to_string(l_iEff) + (l_iEff != l_iBase ? "  (" + l_fnSignedNum(l_iEff - l_iBase) + ")" : ""); };
            l_oP.m_vCombat.push_back({"ATK", l_fnWithBonus(f_EffectiveAtk(), m_oPl.m_iAtk), "", 0});
            l_oP.m_vCombat.push_back({"DEF", l_fnWithBonus(f_EffectiveDef(), m_oPl.m_iDef), "", 0});
            l_oP.m_vCombat.push_back({l_oL.f_Tr("status.gold"), std::to_string(m_oPl.m_iGold), "", 0});
            l_oP.m_vCombat.push_back({l_oL.f_Tr("hud.keys"), "Y " + std::to_string(m_oPl.m_iKeyYellow) + "   B " + std::to_string(m_oPl.m_iKeyBlue) +
                                "   R " + std::to_string(m_oPl.m_iKeyRed), "", 0});
            l_oP.m_vCombat.push_back({l_oL.f_Tr("status.floor"), m_oSt.m_strName + " (" + std::to_string(m_oSt.m_iIndex) + "/" + std::to_string(m_iTotalStages) + ")", "", 0});
            for (const LogEntry& l_oE : f_EventLog(false))
                if (l_oE.m_iKind == 0) { l_oP.m_vCombat.push_back({l_oL.f_Tr("status.chapter"), l_oE.m_strTitle, "", 0}); break; }
            auto l_fnGearName = [&](const std::string& l_strId) { return l_strId.empty() ? l_strNone : f_ItemName(l_strId); };
            l_oP.m_vMore.push_back({l_oL.f_Tr("status.weapon"), l_fnGearName(m_oEquipped.m_strWeaponId), "", 0});
            l_oP.m_vMore.push_back({l_oL.f_Tr("status.armor"), l_fnGearName(m_oEquipped.m_strArmorId), "", 0});
            std::string l_strSkills;
            for (const std::string& l_strId : m_oRun.f_SkillsOwned()) {
                auto l_itIt = m_mapSkillDefs.find(l_strId);
                l_strSkills += (l_strSkills.empty() ? "" : " · ") + (l_itIt == m_mapSkillDefs.end() ? l_strId : l_oL.f_Field(l_itIt->second.m_jName));
            }
            l_oP.m_vMore.push_back({l_oL.f_Tr("ingame_menu.skills"), l_strSkills.empty() ? l_strNone : l_strSkills, "", 0});
            l_oP.m_vMore.push_back({l_oL.f_Tr("status.shards"), std::to_string(m_oRun.f_ShardCount()), "", 0});
            std::string l_strMind;
            for (const auto& [l_strName, l_jLabel] : m_mapCounterLabels)
                if (m_oRun.f_CounterVisible(l_strName)) l_strMind += (l_strMind.empty() ? "" : " · ") + l_oL.f_Field(l_jLabel);
            if (!l_strMind.empty()) l_oP.m_vMore.push_back({l_oL.f_Tr("status.story"), l_strMind, "", 0});
            l_oP.m_strAttrTitle = l_oL.f_Tr("status.attributes");
            for (size_t l_nI = 0; l_nI < m_vAttrDefs.size(); l_nI++) {
                const AttrDef& l_oA = m_vAttrDefs[l_nI];
                const int l_iBase = m_oPl.m_mapAttrs.count(l_oA.m_strId) ? m_oPl.m_mapAttrs.at(l_oA.m_strId) : 0, l_iV = f_AttrValue(l_oA.m_strId);
                const int l_iDraft = m_mapAttrDraft.count(l_oA.m_strId) ? m_mapAttrDraft.at(l_oA.m_strId) : 0;
                toms::UiRow l_oR;
                l_oR.m_strLabel = l_oA.m_strShortName + "   " + l_oL.f_Tr(l_oA.m_strNameKey);
                l_oR.m_strSub = std::to_string(l_iV) + (l_iV != l_iBase ? "  (" + l_fnSignedNum(l_iV - l_iBase) + ")" : "") +
                        (l_iDraft > 0 ? "  → " + std::to_string(l_iV + l_iDraft) : "");   // what Confirm makes it
                l_oR.m_bExtra = l_iDraft > 0;   // its - button
                l_oR.m_bSelected = (int)l_nI == m_iAttrSel;
                l_oP.m_vAttrs.push_back(l_oR);
            }
            l_oP.m_strAttrDesc = (m_iAttrSel >= 0 && m_iAttrSel < (int)m_vAttrDefs.size()) ? l_oL.f_Tr(m_vAttrDefs[m_iAttrSel].m_strDescKey) : l_oL.f_Tr("status.attr_hint");
            if (m_iAttrSel >= 0 && m_iAttrSel < (int)m_vAttrDefs.size()) {   // what it does: per point, and in total now
                const AttrDef& l_oA = m_vAttrDefs[m_iAttrSel];
                auto l_fnFmt = [&](const std::string& l_strStat, float l_fV, bool l_bTotal) {
                    char l_pszB[32];
                    const bool l_bPct = l_strStat == "skillPower" || l_strStat == "goldGain";
                    if (l_bTotal && (l_strStat == "atk" || l_strStat == "def" || l_strStat == "maxhp")) l_fV = std::floor(l_fV);
                    if (std::fabs(l_fV - std::round(l_fV)) < 0.001f) std::snprintf(l_pszB, sizeof l_pszB, "%+d", (int)std::lround(l_fV));
                    else std::snprintf(l_pszB, sizeof l_pszB, "%+.1f", l_fV);
                    return std::string(l_pszB) + (l_bPct ? "% " : " ") + l_oL.f_Tr("stat.effect." + l_strStat);
                };
                std::string l_strPer, l_strNow;
                for (const auto& [l_strStat, l_fK] : l_oA.m_vEffects) {
                    if (l_fK == 0.0f) continue;
                    l_strPer += (l_strPer.empty() ? "" : " · ") + l_fnFmt(l_strStat, l_fK, false);
                    const int l_iDraft = m_mapAttrDraft.count(l_oA.m_strId) ? m_mapAttrDraft.at(l_oA.m_strId) : 0;   // as it will be after Confirm
                    l_strNow += (l_strNow.empty() ? "" : " · ") + l_fnFmt(l_strStat, (float)(f_AttrValue(l_oA.m_strId) + l_iDraft - l_oA.m_iBase) * l_fK, true);
                }
                l_oP.m_strAttrPer = l_strPer.empty() ? l_oL.f_Tr("status.attr_none") : f_TrParam(l_oL.f_Tr("status.attr_per"), "x", l_strPer);
                l_oP.m_strAttrNow = l_strPer.empty() ? "" : f_TrParam(l_oL.f_Tr("status.attr_now"), "x", l_strNow);
            }
            const int l_iLeft = m_oPl.m_iAttrPoints - f_AttrDraftTotal();
            if (m_oPl.m_iAttrPoints > 0) {
                l_oP.m_bAttrCanAdd = l_iLeft > 0;
                l_oP.m_strAttrPoints = f_TrParam(l_oL.f_Tr("status.points"), "n", std::to_string(l_iLeft));
                l_oP.m_strAttrPointsHint = l_oL.f_Tr(m_mapAttrDraft.empty() ? "status.points_hint" : "status.points_confirm_hint");
            }
            l_oP.m_bAttrDraft = !m_mapAttrDraft.empty();
            l_oP.m_bAttrConfirmSel = l_oP.m_bAttrDraft && m_iAttrSel == (int)m_vAttrDefs.size();
            l_oP.m_bAttrResetSel = l_oP.m_bAttrDraft && m_iAttrSel == (int)m_vAttrDefs.size() + 1;
            l_oP.m_strAttrConfirm = l_oL.f_Tr("status.confirm");
            l_oP.m_strAttrReset = l_oL.f_Tr("status.reset");
        }
        {
            auto l_fnKindKey = [](int l_iK) { return l_iK == 0 ? "events.kind.chapter" : l_iK == 1 ? "events.kind.mission" : "events.kind.event"; };
            const auto l_vUnfinished = f_EventLog(false), l_vAll = f_EventLog(true);
            l_oP.m_vEvFilters.push_back({l_oL.f_Tr("events.unfinished") + " (" + std::to_string(l_vUnfinished.size()) + ")", m_iEventFilter == 0, false});
            l_oP.m_vEvFilters.push_back({l_oL.f_Tr("events.all") + " (" + std::to_string(l_vAll.size()) + ")", m_iEventFilter == 1, false});
            const auto& l_vLog = m_iEventFilter == 1 ? l_vAll : l_vUnfinished;
            const int l_iSel = f_ClampSel(m_iEventSel, (int)l_vLog.size());
            for (size_t l_nI = 0; l_nI < l_vLog.size(); l_nI++) {
                const LogEntry& l_oE = l_vLog[l_nI];
                toms::UiRow l_oR;
                l_oR.m_strLabel = std::string(l_oE.m_bDone ? "✓ " : (l_oE.m_iKind == 0 ? "★ " : l_oE.m_iKind == 1 ? "◆ " : "● ")) + l_oE.m_strTitle;
                l_oR.m_strSub = l_oE.m_bDone ? l_oL.f_Tr("events.done") : l_oL.f_Tr(l_fnKindKey(l_oE.m_iKind));
                l_oR.m_bEnabled = !l_oE.m_bDone;
                l_oR.m_bSelected = (int)l_nI == l_iSel;
                l_oP.m_vEvRows.push_back(l_oR);
            }
            l_oP.m_strEvEmpty = l_oL.f_Tr(m_iEventFilter == 1 ? "events.empty_all" : "events.empty_unfinished");
            l_oP.m_strEvHint = l_oL.f_Tr("events.select");
            if (!l_vLog.empty()) {
                const LogEntry& l_oE = l_vLog[l_iSel];
                l_oP.m_bEvDetail = true;
                l_oP.m_strEvKind = l_oL.f_Tr(l_fnKindKey(l_oE.m_iKind)) + (l_oE.m_bDone ? "  ·  " + l_oL.f_Tr("events.done") : "");
                l_oP.m_strEvTitle = l_oE.m_strTitle;
                l_oP.m_strEvDesc = l_oE.m_strDesc;
                l_oP.m_strEvConnectedTitle = l_oL.f_Tr("events.connected");   // shown only when there are connected events
                l_oP.m_vEvConnected = l_oE.m_vConnected;
            }
        }
        f_BuildMenuUi(l_oU.m_oMenu);
        l_oP.m_strSysHint = l_oL.f_Tr("system.select");
        if (m_bSaveLoadOpen) f_BuildSaveLoadUi(l_oU.m_oSaveload);
    }
    switch (m_eMenuTab) {
        case MenuTab::Gear: {
            l_oP.m_vFilters.push_back({l_oL.f_Tr("status.weapon"), m_iGearSlot == 0, false});
            l_oP.m_vFilters.push_back({l_oL.f_Tr("status.armor"), m_iGearSlot == 1, false});
            l_oP.m_strWornTitle = l_oL.f_Tr("gear.worn");
            l_oP.m_vWorn.push_back({l_oL.f_Tr("status.weapon"), m_oEquipped.m_strWeaponId.empty() ? l_strNone : f_ItemName(m_oEquipped.m_strWeaponId), "", m_iGearSlot == 0 ? 1 : 0});
            l_oP.m_vWorn.push_back({l_oL.f_Tr("status.armor"), m_oEquipped.m_strArmorId.empty() ? l_strNone : f_ItemName(m_oEquipped.m_strArmorId), "", m_iGearSlot == 1 ? 1 : 0});
            const auto l_vCells = f_GearCells();
            const int l_iSel = f_ClampSel(m_iGearSel, (int)l_vCells.size());
            for (size_t l_nI = 0; l_nI < l_vCells.size(); l_nI++)
                l_oP.m_vCells.push_back({f_ItemSprite(l_vCells[l_nI]), f_ItemName(l_vCells[l_nI]), "", (int)l_nI == l_iSel, f_Worn(m_oEquipped, l_vCells[l_nI]), false});
            if (l_vCells.empty()) l_oP.m_strEmptyText = f_TrParam(l_oL.f_Tr("gear.empty"), "slot", l_oL.f_Tr(m_iGearSlot == 0 ? "status.weapon" : "status.armor"));
            l_oP.m_strSelectHint = l_oL.f_Tr("gear.select");
            if (!l_vCells.empty()) {
                const ItemCell l_oC{ItemCell::Gear, l_vCells[l_iSel], 1};
                l_oP.m_bDetail = true;
                l_oP.m_strDIcon = f_ItemSprite(l_oC.m_strId);
                l_oP.m_strDName = f_ItemName(l_oC.m_strId);
                l_oP.m_strDDesc = f_ItemDesc(l_oC.m_strId);
                if (!f_Worn(m_oEquipped, l_oC.m_strId)) l_fnCompare(l_oC.m_strId);
                else l_oP.m_strDNote = l_oL.f_Tr("gear.worn");
                std::vector<ItemButton> l_vBs;
                for (const ItemButton& l_oB : f_ItemButtons(l_oC)) if (l_oB.m_eAction != ItemAction::Drop) l_vBs.push_back(l_oB);
                l_fnButtonsUi(l_vBs);
            }
            break;
        }
        case MenuTab::Items: {
            static const char* l_arrFilters[4] = {"items.all", "items.use", "player.tab.gear", "hud.keys"};
            for (int l_iI = 0; l_iI < 4; l_iI++) l_oP.m_vFilters.push_back({l_oL.f_Tr(l_arrFilters[l_iI]), l_iI == m_iItemFilter, false});
            const auto l_vCells = f_ItemCells();
            const int l_iSel = f_ClampSel(m_iItemSel, (int)l_vCells.size());
            for (size_t l_nI = 0; l_nI < l_vCells.size(); l_nI++) {
                const ItemCell& l_oC = l_vCells[l_nI];
                l_oP.m_vCells.push_back({f_ItemSprite(l_oC.m_strId), f_ItemName(l_oC.m_strId), l_oC.m_iCount > 1 ? "x" + std::to_string(l_oC.m_iCount) : "", (int)l_nI == l_iSel,
                                   l_oC.m_eKind == ItemCell::Gear && f_Worn(m_oEquipped, l_oC.m_strId), l_oC.m_eKind == ItemCell::Key || f_ItemImportant(l_oC.m_strId)});
            }
            if (l_vCells.empty()) l_oP.m_strEmptyText = l_oL.f_Tr("inventory.empty_title");
            l_oP.m_strSelectHint = l_oL.f_Tr("items.select");
            if (!l_vCells.empty()) l_fnItemDetail(l_vCells[l_iSel]);
            if (m_bDropConfirmOpen && !l_vCells.empty()) {
                l_oP.m_bConfirmOpen = true;
                l_oP.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr("items.drop_confirm"), "name", f_ItemName(l_vCells[l_iSel].m_strId));
                l_oP.m_strYesLabel = l_oL.f_Tr("inventory.drop");
                l_oP.m_strNoLabel = l_oL.f_Tr("menu.back");
                l_oP.m_bConfirmYes = m_bDropConfirmYes;
            }
            break;
        }
        case MenuTab::Cards: {   // monster cards (docs/gameplay/07_GAMEPLAY_PLAN.md §3): a grid of the monsters met | the record
            const auto l_vCells = f_CardCells();
            const int l_iSel = f_ClampSel(m_iCardSel, (int)l_vCells.size());
            int l_iHeld = 0;
            for (const auto& [l_strId, l_oR] : m_oMeta.m_mapCards) if (l_oR.m_iCopies > 0 && m_mapEnemyTpl.count(l_strId)) l_iHeld++;
            l_oP.m_strWornTitle = f_TrParam(f_TrParam(l_oL.f_Tr("cards.count"), "n", std::to_string(l_iHeld)), "all", std::to_string(m_mapEnemyTpl.size()));
            for (size_t l_nI = 0; l_nI < l_vCells.size(); l_nI++) {
                const toms::CardRecord& l_oR = m_oMeta.m_mapCards.at(l_vCells[l_nI]);
                const auto& l_jT = m_mapEnemyTpl.at(l_vCells[l_nI]);
                l_oP.m_vCells.push_back({f_UiSprite(f_TypeSprite("monster", l_vCells[l_nI])), m_oLocale.f_Field(l_jT["name"]),
                                   l_oR.m_iCopies > 1 ? "x" + std::to_string(l_oR.m_iCopies) : "", (int)l_nI == l_iSel, false, l_oR.m_iCopies == 0});
            }
            if (l_vCells.empty()) l_oP.m_strEmptyText = l_oL.f_Tr("cards.empty");
            l_oP.m_strSelectHint = l_oL.f_Tr("cards.select");
            if (!l_vCells.empty()) {
                const std::string& l_strId = l_vCells[l_iSel];
                const toms::CardRecord& l_oR = m_oMeta.m_mapCards.at(l_strId);
                const auto& l_jT = m_mapEnemyTpl.at(l_strId);
                l_oP.m_bDetail = true;
                l_oP.m_strDIcon = f_UiSprite(f_TypeSprite("monster", l_strId));
                l_oP.m_strDName = m_oLocale.f_Field(l_jT["name"]);
                l_oP.m_strDDesc = l_oR.m_iCopies > 0 ? l_oL.f_Tr("cards.have") : l_oL.f_Tr("cards.not_yet");
                l_oP.m_vDLines.push_back({l_oL.f_Tr("cards.copies"), std::to_string(l_oR.m_iCopies), "", 0});
                l_oP.m_vDLines.push_back({l_oL.f_Tr("cards.defeated"), std::to_string(l_oR.m_iDefeated), "", 0});
                l_oP.m_vDLines.push_back({l_oL.f_Tr("cards.defeated_by"), std::to_string(l_oR.m_iDefeatedBy), "", 0});
                l_oP.m_vDLines.push_back({"HP", std::to_string(l_jT.value("hp", 0)), "", 0});
                l_oP.m_vDLines.push_back({"ATK", std::to_string(l_jT.value("atk", 0)), "", 0});
                l_oP.m_vDLines.push_back({"DEF", std::to_string(l_jT.value("def", 0)), "", 0});
                // the training ground (docs/gameplay/16 §5): spend its card for fights in a row, a few runs a day
                const int l_iLeft = f_TrainingRunsLeft();
                l_oP.m_vDButtons.push_back({f_TrParam(l_oL.f_Tr(l_iLeft > 0 ? "cards.train" : "cards.train_none"), "n", std::to_string(l_iLeft)),
                                       l_oR.m_iCopies > 0 && l_iLeft > 0});
            }
            break;
        }
        default:
            break;
    }
}

// The Cards tab's monsters: those with a record (met in a battle), the easiest first (HP, then attack, then id).
std::vector<std::string> Game::f_CardCells() const {
    std::vector<std::string> l_vOut;
    for (const auto& [l_strId, l_oR] : m_oMeta.m_mapCards) if (m_mapEnemyTpl.count(l_strId)) l_vOut.push_back(l_strId);
    auto l_fnKey = [&](const std::string& l_strId) {
        const auto& l_jT = m_mapEnemyTpl.at(l_strId);
        return std::make_tuple(l_jT.value("hp", 0), l_jT.value("atk", 0), l_strId);
    };
    std::sort(l_vOut.begin(), l_vOut.end(), [&](const std::string& l_strA, const std::string& l_strB) { return l_fnKey(l_strA) < l_fnKey(l_strB); });
    return l_vOut;
}

// ---------- Save / Load (saveload.rml) ----------
// Its own screen over the menu: a Save tab and a Load tab over the same slot cards. Choosing a slot only
// opens a confirmation; nothing is written or loaded until the player says yes. Saving into a slot makes
// it the run's slot (autosave writes there from then on), as loading one does.

void Game::f_OpenSaveLoad(int l_iMode) {
    m_bSaveLoadOpen = true;
    m_iSaveLoadMode = l_iMode == 1 ? 1 : 0;
    m_bSaveLoadConfirm = false;
    m_vSaveLoadSlots.clear();
    const std::string l_strDir = toms::f_DefaultSaveDir();
    for (int l_iI = 1; l_iI <= m_oTitle.f_SlotCount(); l_iI++) m_vSaveLoadSlots.push_back(toms::f_SummarizeSlot(l_strDir, l_iI));
    m_iSaveLoadSel = std::max(0, std::min(m_iActiveSlot - 1, (int)m_vSaveLoadSlots.size() - 1));
    m_oAudio.f_Play("confirm_click");
}

void Game::f_SaveLoadAsk(int l_iIndex) {
    if (l_iIndex < 0 || l_iIndex >= (int)m_vSaveLoadSlots.size()) return;
    m_iSaveLoadSel = l_iIndex;
    const bool l_bExists = m_vSaveLoadSlots[l_iIndex].m_bExists;
    if (m_iSaveLoadMode == 1 && !l_bExists) { m_oAudio.f_Play("deny"); return; }   // nothing to load
    if (m_iSaveLoadMode == 0 && m_vSaveLoadSlots[l_iIndex].m_bNewer) { m_oAudio.f_Play("deny"); return; }   // a newer game's save stays
    m_bSaveLoadConfirm = true;
    // The safe answer is preselected wherever something is lost: overwriting a save, leaving the run.
    m_bSaveLoadConfirmYes = m_iSaveLoadMode == 0 && !l_bExists;
    m_oAudio.f_Play("confirm_click");
}

void Game::f_SaveLoadAnswer(bool l_bYes) {
    if (!m_bSaveLoadConfirm) return;
    m_bSaveLoadConfirm = false;
    if (!l_bYes) { m_oAudio.f_Play("close_ui"); return; }
    const int l_iSlot = m_iSaveLoadSel + 1;
    if (m_iSaveLoadMode == 0) {
        m_iActiveSlot = l_iSlot;
        m_bSlotReadOnly = false;   // the Save tab never offers a newer game's slot (saveLoadAsk)
        f_SaveCurrentRun();
        m_vSaveLoadSlots[m_iSaveLoadSel] = toms::f_SummarizeSlot(toms::f_DefaultSaveDir(), l_iSlot);
        m_strToastMsg = f_TrParam(m_oLocale.f_Tr("saveload.saved"), "slot", std::to_string(l_iSlot));
        m_iToastTimer = 1600;
        m_oAudio.f_Play("confirm_click");
    } else if (!f_ContinueFromSlot(l_iSlot)) {   // loading closes the menu (applyLoadedRun)
        m_oAudio.f_Play("deny");
    }
}

void Game::f_BuildSaveLoadUi(toms::UiSaveLoad& l_oV) const {
    const toms::Locale& l_oL = m_oLocale;
    l_oV.m_bVisible = true;
    l_oV.m_strTitle = l_oL.f_Tr("saveload.title");
    l_oV.m_strHint = l_oL.f_Tr("saveload.hint");
    l_oV.m_strCloseLabel = l_oL.f_Tr("menu.back");
    l_oV.m_vModes.push_back({l_oL.f_Tr("saveload.save"), m_iSaveLoadMode == 0, false});
    l_oV.m_vModes.push_back({l_oL.f_Tr("saveload.load"), m_iSaveLoadMode == 1, false});
    for (size_t l_nI = 0; l_nI < m_vSaveLoadSlots.size(); l_nI++) {
        const toms::SlotSummary& l_oS = m_vSaveLoadSlots[l_nI];
        toms::UiSlot l_oC;
        l_oC.m_strTitle = l_oL.f_Tr("continue.slot") + " " + std::to_string(l_nI + 1);
        l_oC.m_bEmpty = !l_oS.m_bExists;
        if (l_oS.m_bExists) {
            const int l_iSec = std::max(0, l_oS.m_iPlayTimeSec);
            char l_pszT[32];
            std::snprintf(l_pszT, sizeof l_pszT, "%02d:%02d:%02d", l_iSec / 3600, (l_iSec / 60) % 60, l_iSec % 60);
            l_oC.m_strLine1 = (l_oS.m_strStageName.empty() ? l_oS.m_strStageId : l_oS.m_strStageName) + "   " + l_oL.f_Tr("hud.level") + " " + std::to_string(l_oS.m_iLv) +
                      "   HP " + std::to_string(l_oS.m_iHp) + "/" + std::to_string(l_oS.m_iMaxhp) + "   " + std::to_string(l_oS.m_iGold) + "G";
            l_oC.m_strLine2 = l_oL.f_Tr("continue.saved_at") + " " + l_oS.m_strSavedAt + "    " + l_oL.f_Tr("continue.play_time") + " " + l_pszT;
            if (l_oS.m_bFromBackup) l_oC.m_strLine2 += "    [" + l_oL.f_Tr("continue.from_backup") + "]";
            if (l_oS.m_bNewer) l_oC.m_strLine2 += "    [" + l_oL.f_Tr("continue.newer") + "]";
        } else if (l_oS.m_bDamaged) {
            l_oC.m_strLine1 = "[" + l_oL.f_Tr("continue.damaged") + "]";
        } else {
            l_oC.m_strLine1 = m_iSaveLoadMode == 1 ? l_oL.f_Tr("saveload.load_empty") : "[" + l_oL.f_Tr("continue.empty") + "]";
        }
        if ((int)l_nI + 1 == m_iActiveSlot) l_oC.m_strTag = l_oL.f_Tr("saveload.current");
        l_oC.m_bSelected = (int)l_nI == m_iSaveLoadSel;
        l_oC.m_bEnabled = m_iSaveLoadMode == 0 ? !l_oS.m_bNewer : l_oS.m_bExists;
        l_oV.m_vCards.push_back(l_oC);
    }
    if (m_bSaveLoadConfirm && m_iSaveLoadSel < (int)m_vSaveLoadSlots.size()) {
        const std::string l_strSlot = std::to_string(m_iSaveLoadSel + 1);
        const bool l_bExists = m_vSaveLoadSlots[m_iSaveLoadSel].m_bExists;
        l_oV.m_bConfirmOpen = true;
        if (m_iSaveLoadMode == 1) {
            l_oV.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr("saveload.load_confirm"), "slot", l_strSlot);
            l_oV.m_strConfirmBody = l_oL.f_Tr("saveload.load_body");
            l_oV.m_strYesLabel = l_oL.f_Tr("saveload.load");
        } else {
            l_oV.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr(l_bExists ? "saveload.overwrite_confirm" : "saveload.save_confirm"), "slot", l_strSlot);
            l_oV.m_strConfirmBody = l_bExists ? l_oL.f_Tr("saveload.overwrite_body") : "";
            l_oV.m_strYesLabel = l_oL.f_Tr("saveload.save");
        }
        l_oV.m_strNoLabel = l_oL.f_Tr("saveload.cancel");
        l_oV.m_bConfirmYes = m_bSaveLoadConfirmYes;
    }
}
