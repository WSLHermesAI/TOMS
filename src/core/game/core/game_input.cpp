// game_input.cpp — input: map clicks (click-to-move), held-direction
// movement, world interaction, and bump-to-fight. Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

void Game::f_HandleTouch(float l_fPx, float l_fPy, int l_iPhase) {
    // Only clicks that no UI element took arrive here (the RmlUi documents handle every button),
    // so the one thing left is the map: click-to-move, or interact on the player's own tile.
    if (!(l_fPx == l_fPx) || !(l_fPy == l_fPy)) return;             // NaN (zero-size canvas)
    if (l_fPx < 0 || l_fPy < 0 || (m_pRen && (l_fPx > (float)m_pRen->f_Width() || l_fPy > (float)m_pRen->f_Height()))) return;   // on the canvas
    if (l_iPhase != 0 || f_ModalActive()) return;
    int l_iTx, l_iTy;
    if (!f_ScreenToTile(l_fPx, l_fPy, l_iTx, l_iTy)) return;
    if (l_iTx == m_oPl.m_iX && l_iTy == m_oPl.m_iY) { f_CancelWalk(); f_Interact(); }
    else f_WalkTo(l_iTx, l_iTy);
}

// Milestone 9 polish: see setMoveHeldX/Y's declaration in game.h. A fresh press (or switching
// directions on the same axis) fires one immediate step, same feel as the old one-tap-one-tile
// behavior; the repeat while still held is ticked in update().
void Game::f_SetMoveHeldX(int l_iDir) {
    if (l_iDir != 0) f_CancelWalk();   // steering by hand takes over from click-to-move
    if (l_iDir == m_oMoveHoldX.m_iDir) return;
    m_oMoveHoldX.m_iDir = l_iDir; m_oMoveHoldX.m_iHoldMs = 0; m_oMoveHoldX.m_bRepeating = false;
    if (l_iDir != 0) f_MovePlayer(l_iDir, 0);
}

void Game::f_SetMoveHeldY(int l_iDir) {
    if (l_iDir != 0) f_CancelWalk();
    if (l_iDir == m_oMoveHoldY.m_iDir) return;
    m_oMoveHoldY.m_iDir = l_iDir; m_oMoveHoldY.m_iHoldMs = 0; m_oMoveHoldY.m_bRepeating = false;
    if (l_iDir != 0) f_MovePlayer(0, l_iDir);
}

// ---- Click-to-move (see walkTo()'s declaration in game.h) ----

bool Game::f_ScreenToTile(float l_fPx, float l_fPy, int& l_iTx, int& l_iTy) const {
    if (!m_pRen) return false;
    float l_fTs; int l_iCols, l_iRows; f_CameraViewportTiles(l_fTs, l_iCols, l_iRows);
    if (l_fTs <= 0.0f) return false;
    // draw(): tile (x,y) is at (-camX*ts + x*ts, 60 - camY*ts + y*ts).
    l_iTx = (int)std::floor(l_fPx / l_fTs + m_oCam.f_X());
    l_iTy = (int)std::floor((l_fPy - 60.0f) / l_fTs + m_oCam.f_Y());
    return l_iTx >= 0 && l_iTy >= 0 && l_iTx < (int)m_oSt.m_iWidth && l_iTy < (int)m_oSt.m_iHeight;
}

bool Game::f_WalkPassable(int l_iX, int l_iY) const {
    const char l_cC = m_oSt.f_At(l_iX, l_iY);
    if (l_cC == '#' || l_cC == 'y' || l_cC == 'b' || l_cC == 'r' || l_cC == 'U' || l_cC == 'D') return false;
    for (const auto& l_oE : m_oSt.m_vEntities)
        if (!l_oE.m_bConsumed && f_EntityCovers(l_oE, l_iX, l_iY)) return false;   // monsters, items, NPCs, events
    return true;
}

// Breadth-first search from the player to (tx,ty): shortest in steps, 4-way like the arrows.
bool Game::f_PlanWalk(int l_iTx, int l_iTy, std::vector<std::pair<int, int>>& l_vOut) const {
    l_vOut.clear();
    const int l_iGw = (int)m_oSt.m_iWidth, l_iGh = (int)m_oSt.m_iHeight;
    if (l_iTx < 0 || l_iTy < 0 || l_iTx >= l_iGw || l_iTy >= l_iGh || m_oSt.f_At(l_iTx, l_iTy) == '#') return false;
    if (l_iTx == m_oPl.m_iX && l_iTy == m_oPl.m_iY) return false;
    std::vector<int> l_vPrev((size_t)l_iGw * l_iGh, -1);
    std::vector<int> l_vQueue;
    const int l_iStart = m_oPl.m_iY * l_iGw + m_oPl.m_iX, l_iGoal = l_iTy * l_iGw + l_iTx;
    l_vPrev[l_iStart] = l_iStart;
    l_vQueue.push_back(l_iStart);
    static const int l_arrDx[4] = {0, 0, -1, 1}, l_arrDy[4] = {-1, 1, 0, 0};
    for (size_t l_nHead = 0; l_nHead < l_vQueue.size() && l_vPrev[l_iGoal] < 0; ++l_nHead) {
        const int l_iCx = l_vQueue[l_nHead] % l_iGw, l_iCy = l_vQueue[l_nHead] / l_iGw;
        for (int l_iD = 0; l_iD < 4; ++l_iD) {
            const int l_iNx = l_iCx + l_arrDx[l_iD], l_iNy = l_iCy + l_arrDy[l_iD];
            if (l_iNx < 0 || l_iNy < 0 || l_iNx >= l_iGw || l_iNy >= l_iGh) continue;
            const int l_iN = l_iNy * l_iGw + l_iNx;
            if (l_vPrev[l_iN] >= 0) continue;
            if (l_iN != l_iGoal && !f_WalkPassable(l_iNx, l_iNy)) continue;   // the target itself may be anything
            l_vPrev[l_iN] = l_vQueue[l_nHead];
            l_vQueue.push_back(l_iN);
        }
    }
    if (l_vPrev[l_iGoal] < 0) return false;
    for (int l_iN = l_iGoal; l_iN != l_iStart; l_iN = l_vPrev[l_iN]) l_vOut.emplace_back(l_iN % l_iGw, l_iN / l_iGw);
    std::reverse(l_vOut.begin(), l_vOut.end());
    return true;
}

bool Game::f_WalkTo(int l_iTx, int l_iTy) {
    f_CancelWalk();
    if (f_ModalActive()) return false;
    std::vector<std::pair<int, int>> l_vPath;
    if (!f_PlanWalk(l_iTx, l_iTy, l_vPath)) return false;
    m_vWalkPath = std::move(l_vPath);
    m_iWalkTargetX = l_iTx; m_iWalkTargetY = l_iTy;
    f_WalkStep();   // first step right away, like a key press; update() paces the rest
    return true;
}

void Game::f_WalkStep() {
    if (m_vWalkPath.empty()) return;
    // The world moves between steps (roamers): if the next tile is no longer free, re-plan.
    if (m_vWalkPath.size() > 1 && !f_WalkPassable(m_vWalkPath.front().first, m_vWalkPath.front().second)) {
        std::vector<std::pair<int, int>> l_vPath;
        if (!f_PlanWalk(m_iWalkTargetX, m_iWalkTargetY, l_vPath)) { f_CancelWalk(); return; }
        m_vWalkPath = std::move(l_vPath);
    }
    const auto [l_iNx, l_iNy] = m_vWalkPath.front();
    if (std::abs(l_iNx - m_oPl.m_iX) + std::abs(l_iNy - m_oPl.m_iY) != 1) { f_CancelWalk(); return; }
    m_vWalkPath.erase(m_vWalkPath.begin());
    f_MovePlayer(l_iNx - m_oPl.m_iX, l_iNy - m_oPl.m_iY);
    // Didn't get there (a locked door, a bumped boss) or arrived: stop. A battle, dialogue or the
    // stairs dialog it may have opened is caught by update()'s modal check.
    if (m_oPl.m_iX != l_iNx || m_oPl.m_iY != l_iNy || m_vWalkPath.empty()) f_CancelWalk();
}

void Game::f_OpenDoor(int l_iX, int l_iY, char l_cC) {
    if (l_cC == 'y') m_oPl.m_iKeyYellow--;
    if (l_cC == 'b') m_oPl.m_iKeyBlue--;
    if (l_cC == 'r') m_oPl.m_iKeyRed--;
    m_oSt.m_vTiles[l_iY][l_iX] = '.';
    for (auto& l_oE : m_oSt.m_vEntities)   // the door's own map entry ("door:yellow" in the legend) no longer blocks or draws
        if (!l_oE.m_bConsumed && l_oE.m_iX == l_iX && l_oE.m_iY == l_iY && l_oE.m_strKind.rfind("door:", 0) == 0) l_oE.m_bConsumed = true;
    toms::f_SetEntityStatus(m_mapEntityStatus, f_StatusKeyAt(l_iX, l_iY), toms::EntityStatus::Opened);
    for (const auto& l_oE : m_oSt.m_vEntities) if (l_oE.m_iX == l_iX && l_oE.m_iY == l_iY && l_oE.m_strKind.rfind("door:", 0) == 0) f_FireLinkedEvents(l_oE);
    m_oAudio.f_Play("confirm_click");
    (void)l_cC;
    if (MapObject* l_pDoor = m_oFloor.f_ObjectAt(l_iX, l_iY, "door:")) l_pDoor->f_Play("opening");   // its clip plays on the tile
}

void Game::f_MovePlayer(int l_iDx, int l_iDy) {
    if (f_ModalActive()) return;   // any modal overlay (combat/dialogue/inventory) blocks world input
    int l_iNx = m_oPl.m_iX + l_iDx, l_iNy = m_oPl.m_iY + l_iDy;
    char l_cC = m_oSt.f_At(l_iNx, l_iNy);
    if (l_cC == '#') return;
    // Doors need keys -- gate routed through the shared Condition Evaluator (condition.h) so
    // this uses the same "can the player do X" engine as dialogue `requires` gating, per
    // Milestone 3's retrofit. Same outcome as the three bespoke inline checks this replaces:
    // each door color needs >=1 of its matching key, expressed declaratively instead.
    static const std::map<char, std::string> l_mapDoorKeyItem = {
        {'y', "key_yellow"}, {'b', "key_blue"}, {'r', "key_red"}
    };
    if (auto l_itDoorIt = l_mapDoorKeyItem.find(l_cC); l_itDoorIt != l_mapDoorKeyItem.end()) {
        nlohmann::json l_jReq = { {"type", "itemHeld"}, {"itemId", l_itDoorIt->second}, {"count", 1} };
        if (!toms::f_Evaluate(l_jReq, GameConditionContext(m_oPl, m_oMeta, m_mapMissionTrackers, m_oRun, m_oEquipped))) return;
        f_OpenDoor(l_iNx, l_iNy, l_cC);   // uses the key; the door opens and vanishes; the player stays put this turn
        return;
    }
    // Something that is bumped instead of stepped onto (a gate: it opens for its key; the player stays put).
    for (const auto& l_spO : m_oFloor.f_Objects())
        if (l_spO->f_BumpBeforeStep(l_iNx, l_iNy)) return;
    // Q7: a locked stair is not stepped on: it says which event opens it.
    if (const toms::StairLink* l_pS = toms::f_StairAt(m_oSt.m_vStairs, l_iNx, l_iNy); l_pS && f_StairLocked(*l_pS)) {
        f_BumpLockedStairs(*l_pS);
        return;
    }
    // S1: a multi-grid monster is not walked into; it is fought from the adjacent tile (MonsterObject::bumpBig).
    for (const auto& l_spO : m_oFloor.f_Objects())
        if (l_spO->f_BumpBig(l_iNx, l_iNy)) return;
    m_oPl.m_iX = l_iNx; m_oPl.m_iY = l_iNy;
    m_oAudio.f_Play("walk");
    // The player acted -> the world takes its turn (roamers step once; S1).
    f_AdvanceRoamers();
    // What is on the new cell, in the stage file's order: each object does what its kind does (a monster's battle,
    // an item picked up, an event, an NPC's talk, a stair); Stop ends the step. A copy of the list, and a stop if the
    // floor changed under it (an event's action, a stair).
    const int l_iFloorGen = m_oFloor.f_Generation();
    const std::vector<std::shared_ptr<MapObject>> l_vHere = m_oFloor.f_Objects();
    for (const auto& l_spO : l_vHere) {
        const Entity* l_pE = l_spO->f_Entity();
        if (!l_pE || l_pE->m_bConsumed || !l_spO->f_Covers(l_iNx, l_iNy)) continue;
        if (l_spO->f_OnTouch(l_iNx, l_iNy) == MapObject::Touch::Stop || m_oFloor.f_Generation() != l_iFloorGen) return;
    }
    // stairs check (cell char)
    ++m_iStoryTurns;                 // S3.5 (c): the footer line rotates with movement, not with time
    f_StepOntoStairs(l_iNx, l_iNy);
}

// Q7 (stair_links.h): the stair under (x, y) leads where its link says; a locked one is never reached here
// (movePlayer stops at it).
bool Game::f_StepOntoStairs(int l_iX, int l_iY) {
    const toms::StairLink* l_pS = toms::f_StairAt(m_oSt.m_vStairs, l_iX, l_iY);
    if (!l_pS || l_pS->m_strTo.empty() || f_StairLocked(*l_pS)) return false;
    f_RequestStageTransition(l_pS->m_strTo, l_pS->m_bUp);
    m_strStairsConfirmGoEvent = toms::f_GoStairsEventId(m_oSt.m_strId, l_pS->m_strTo);
    return true;
}

void Game::f_BumpLockedStairs(const toms::StairLink& l_oS) {
    // W38: the events still to finish, each by name -- "???" for one not met yet (a spoiler; the event log hides it too)
    std::string l_strNames;
    int l_iLeft = 0;
    for (const std::string& l_strEv : l_oS.m_vUnlockEvents) {
        if (m_oRun.f_EventFinished(l_strEv)) continue;
        auto l_itD = m_mapEventDefs.find(l_strEv);
        std::string l_strName = l_itD == m_mapEventDefs.end() ? std::string() : m_oLocale.f_Tr(l_itD->second.m_strTitleKey);
        if (l_strName.empty() || (l_itD != m_mapEventDefs.end() && l_strName == l_itD->second.m_strTitleKey)) l_strName = l_strEv;
        if (!m_oRun.f_EventStarted(l_strEv)) l_strName = m_oLocale.f_Tr("events.unknown");
        l_strNames += (l_strNames.empty() ? "" : ", ") + ("\"" + l_strName + "\"");
        l_iLeft++;
    }
    const std::string l_strMsg = l_iLeft > 1
        ? toms::f_FillText(m_oLocale.f_Tr("stairs.locked.many"), {{"events", l_strNames}, {"n", std::to_string(l_iLeft)}})
        : toms::f_FillText(m_oLocale.f_Tr("stairs.locked"), {{"event", l_strNames.size() >= 2 ? l_strNames.substr(1, l_strNames.size() - 2) : l_strNames}});
    if (m_vNotifications.empty() || m_vNotifications.back().first != l_strMsg) m_vNotifications.push_back({l_strMsg, 3200});
    m_oAudio.f_Play("confirm_click");
}

const toms::StairLink* Game::f_FindStair(const std::string& l_strFloor, const std::string& l_strTo) const {
    if (m_oSt.m_strId == l_strFloor)
        for (const toms::StairLink& l_oS : m_oSt.m_vStairs) if (l_oS.m_strTo == l_strTo) return &l_oS;
    if (const toms::FloorInfo* l_pF = m_oFloors.f_Find(l_strFloor))
        for (const toms::StairLink& l_oS : l_pF->m_vStairs) if (l_oS.m_strTo == l_strTo) return &l_oS;
    return nullptr;
}

bool Game::f_StairJustOpened(const std::string& l_strFloor, const toms::StairLink& l_oS) const {
    if (l_oS.m_vUnlockEvents.empty() || f_StairLocked(l_oS)) return false;
    const std::string l_strGo = toms::f_GoStairsEventId(l_strFloor, l_oS.m_strTo);
    return m_oRun.f_EventStarted(l_strGo) && !m_oRun.f_EventFinished(l_strGo);
}

void Game::f_FinishEvent(const std::string& l_strEvId, int l_iDepth) {
    const bool l_bAlready = m_oRun.f_EventFinished(l_strEvId);
    m_oRun.f_SetFlag("event_" + l_strEvId, true);
    m_oRun.f_MarkEventFinished(l_strEvId);
    auto l_itDef = m_mapEventDefs.find(l_strEvId);
    if (l_itDef != m_mapEventDefs.end() && !l_bAlready)
        for (const std::string& l_strN : l_itDef->second.m_vNext) f_WakeEvent(l_strN, l_iDepth);   // started; a "chain" event runs now
    // Q7 / W38: the stairs this event helps open. When it was the last of a stair's events, a dialog names the stair
    // and where it is (an arrow marks it on the map), and its "go to the stairs" event starts. When others are still
    // to do, a short note says how many.
    if (auto l_itU = m_mapStairUnlocks.find(l_strEvId); l_itU != m_mapStairUnlocks.end() && !l_bAlready)
        for (const StairUnlock& l_oS : l_itU->second) {
            const toms::StairLink* l_pLink = f_FindStair(l_oS.m_strFloor, l_oS.m_strTo);
            int l_iLeft = 0;
            if (l_pLink) for (const std::string& l_strE : l_pLink->m_vUnlockEvents) if (!m_oRun.f_EventFinished(l_strE)) l_iLeft++;
            const std::map<std::string, std::string> l_mapVars = {{"from", toms::f_FloorLabel(l_oS.m_strFloor)}, {"to", toms::f_FloorLabel(l_oS.m_strTo)},
                                                             {"n", std::to_string(l_iLeft)}};
            if (l_iLeft > 0) {
                m_vNotifications.push_back({toms::f_FillText(m_oLocale.f_Tr("stairs.progress"), l_mapVars), 4000});
                continue;
            }
            m_oRun.f_MarkEventStarted(toms::f_GoStairsEventId(l_oS.m_strFloor, l_oS.m_strTo));
            std::string l_strBody = toms::f_FillText(m_oLocale.f_Tr("stairs.unlocked.body"), l_mapVars);
            l_strBody += " " + toms::f_FillText(m_oLocale.f_Tr(l_oS.m_strFloor == m_oSt.m_strId ? "stairs.unlocked.here" : "stairs.unlocked.there"), l_mapVars);
            m_vNotices.push_back({m_oLocale.f_Tr("stairs.unlocked.title"), l_strBody});
        }
    f_MarkProgressDirty();
}

void Game::f_BumpGate(Entity& l_oGate) {
    const std::string& l_strKeyId = l_oGate.m_strId;
    auto l_itIt = std::find(m_oPl.m_vInv.begin(), m_oPl.m_vInv.end(), l_strKeyId);
    const std::map<std::string, std::string> l_mapVars = {{"item", f_ItemName(l_strKeyId)}};
    if (l_itIt == m_oPl.m_vInv.end()) {
        const std::string l_strMsg = toms::f_FillText(m_oLocale.f_Tr("gate.locked"), l_mapVars);
        if (m_vNotifications.empty() || m_vNotifications.back().first != l_strMsg) m_vNotifications.push_back({l_strMsg, 3200});
        m_oAudio.f_Play("confirm_click");
        return;
    }
    m_oPl.m_vInv.erase(l_itIt);   // the key goes into the lock
    l_oGate.m_bConsumed = true;
    m_oSt.m_vTiles[l_oGate.m_iY][l_oGate.m_iX] = '.';
    toms::f_SetEntityStatus(m_mapEntityStatus, f_StatusKeyAt(l_oGate.m_iX, l_oGate.m_iY), toms::EntityStatus::Opened);
    f_FireLinkedEvents(l_oGate);
    m_vNotifications.push_back({toms::f_FillText(m_oLocale.f_Tr("gate.opened"), l_mapVars), 3200});
    m_oAudio.f_Play("confirm_click");
    f_MarkProgressDirty();   // (GateObject plays its opening clip)
}

void Game::f_DismissNotice() {
    if (m_vNotices.empty()) return;
    m_vNotices.erase(m_vNotices.begin());
    m_oAudio.f_Play("confirm_click");
}

void Game::f_LoadStairLocks() {
    m_mapStairUnlocks.clear();
    for (auto l_itIt = m_mapEventDefs.begin(); l_itIt != m_mapEventDefs.end();)
        l_itIt = l_itIt->second.m_strStairsFrom.empty() ? std::next(l_itIt) : m_mapEventDefs.erase(l_itIt);
    for (const toms::FloorInfo& l_oF : m_oFloors.f_All())
        for (const toms::StairLink& l_oS : l_oF.m_vStairs) {
            if (l_oS.m_vUnlockEvents.empty()) continue;
            for (const std::string& l_strEv : l_oS.m_vUnlockEvents) m_mapStairUnlocks[l_strEv].push_back({l_oF.m_strId, l_oS.m_strTo});
            EventDef l_oD;
            l_oD.m_strKind = "stairs";
            l_oD.m_strStairsFrom = l_oF.m_strId;
            l_oD.m_strStairsTo = l_oS.m_strTo;
            m_mapEventDefs[toms::f_GoStairsEventId(l_oF.m_strId, l_oS.m_strTo)] = l_oD;
        }
}

void Game::f_DebugLockStairs(const std::string& l_strFloor, const std::string& l_strEventId) {
    toms::FloorInfo* l_pF = m_oFloors.f_Edit(l_strFloor);
    if (!l_pF || l_strEventId.empty()) return;
    if (!l_pF->m_bHasStairs) {
        l_pF->m_vStairs = toms::f_DefaultStairLinks(l_pF->m_strNextFloor, m_oFloors.f_Prev(l_pF->m_strId));
        l_pF->m_bHasStairs = true;
    }
    for (toms::StairLink& l_oS : l_pF->m_vStairs)   // another call adds another event: the stair waits for all of them (W38)
        if (l_oS.m_bUp) { if (std::find(l_oS.m_vUnlockEvents.begin(), l_oS.m_vUnlockEvents.end(), l_strEventId) == l_oS.m_vUnlockEvents.end()) l_oS.m_vUnlockEvents.push_back(l_strEventId); break; }
    f_LoadStairLocks();
    if (m_oSt.m_strId == l_strFloor)   // the floor the player is on: its stairs take the lock now
        for (toms::StairLink& l_oS : m_oSt.m_vStairs)
            if (l_oS.m_bUp) { if (std::find(l_oS.m_vUnlockEvents.begin(), l_oS.m_vUnlockEvents.end(), l_strEventId) == l_oS.m_vUnlockEvents.end()) l_oS.m_vUnlockEvents.push_back(l_strEventId); break; }
}

void Game::f_DebugFinishEvent(const std::string& l_strEventId) {
    if (!l_strEventId.empty()) f_FinishEvent(l_strEventId);
}


void Game::f_Interact() {
    if (f_ModalActive()) return;   // any modal overlay blocks world interaction
    // what can be talked to on the player's tile (an NPC)
    for (const auto& l_spO : m_oFloor.f_Objects())
        if (l_spO->f_OnInteract()) return;
}

// S1: one turn for every room-wandering event monster. Walkability is answered here (the roamer
// class stays pure logic -- see roamer.h) and excludes tiles held by other live entities, so two
// roamers cannot stack and a roamer cannot walk into the player or an item.
void Game::f_AdvanceRoamers() {
    if (m_vRoamers.empty()) return;
    const int l_iGw = (int)m_oSt.m_iWidth, l_iGh = (int)m_oSt.m_iHeight;
    for (auto& [l_iEntIndex, l_oBrain] : m_vRoamers) {
        if (l_iEntIndex < 0 || l_iEntIndex >= (int)m_oSt.m_vEntities.size()) continue;
        const Entity& l_oSelf = m_oSt.m_vEntities[l_iEntIndex];
        if (l_oSelf.m_bConsumed) continue;   // fought and cleared: it stops roaming
        struct Query : toms::GridQuery {
            const Stage& m_oS; const Entity& m_oMe; int m_iMyIndex; int m_iPx, m_iPy;
            Query(const Stage& l_oS, const Entity& l_oMe, int l_iIdx, int l_iPx, int l_iPy)
                : m_oS(l_oS), m_oMe(l_oMe), m_iMyIndex(l_iIdx), m_iPx(l_iPx), m_iPy(l_iPy) {}
            bool f_Walkable(int l_iX, int l_iY) const override {
                if (m_oS.f_At(l_iX, l_iY) == '#') return false;
                if (l_iX == m_iPx && l_iY == m_iPy) return false;   // the player blocks (captured by value:
                                                        // the world doesn't move mid-turn)
                for (int l_iI = 0; l_iI < (int)m_oS.m_vEntities.size(); ++l_iI) {
                    const Entity& l_oO = m_oS.m_vEntities[l_iI];
                    if (l_iI == m_iMyIndex || l_oO.m_bConsumed) continue;
                    if (l_oO.m_strKind.rfind("monster:", 0) != 0) continue;   // items/NPCs are not obstacles
                    if (toms::f_FootprintCovers(l_oO.m_iX, l_oO.m_iY, l_oO.m_oFp, l_iX, l_iY)) return false;
                }
                (void)m_oMe;
                return true;
            }
        } l_oQuery(m_oSt, l_oSelf, l_iEntIndex, m_oPl.m_iX, m_oPl.m_iY);
        l_oBrain.f_Step(l_oQuery, m_oPl.m_iX, m_oPl.m_iY, l_iGw, l_iGh);
        // The brain owns the position; write it back so drawing, blocking and bump-to-fight all see
        // the roamer where it actually is.
        m_oSt.m_vEntities[l_iEntIndex].m_iX = l_oBrain.f_X();
        m_oSt.m_vEntities[l_iEntIndex].m_iY = l_oBrain.f_Y();
    }
}

// Shared by movePlayer() (bumping a monster tile) and debugStartNearestBattle() (verification
// harnesses): builds the EnemyInst from the entity and starts the fight -- or its dialogue gate.
void Game::f_EngageMonster(const Entity& l_oE) {
    EnemyInst l_oEn;
    if (!f_EnemyFromData(l_oE.m_strId, nlohmann::json::object(), l_oEn)) return;   // enemies.json, with its moves, look and events
    l_oEn.m_iX = l_oE.m_iX; l_oEn.m_iY = l_oE.m_iY;
    l_oEn.m_vStageParty = l_oE.m_vParty;
    // Milestone 4 (Encounter Resolution): resolveEncounterKind returns DirectBattle for every
    // monster tile in every shipped stage today (no stage sets encounter_overrides yet), so this is
    // byte-for-byte the same behavior as before unless/until a stage opts a tile into dialogue_gate.
    toms::EncounterKind l_eEk = toms::f_ResolveEncounterKind(l_oE.m_strKind, l_oE.m_strEncounterOverride);
    if (l_eEk == toms::EncounterKind::DialogueGate) {
        m_oPendingEncounterEnemy = l_oEn;
        m_bHasPendingEncounter = true;
        m_strDlgNpc = "enemy_" + l_oE.m_strId;   // so ChoiceMade{dlgNpc,...} carries the right id
        f_StartDialogue(m_strDlgNpc);
    } else {
        f_StartCombat(l_oEn, f_PartyOf(l_oEn));
    }
}

// The monsters fighting beside a map monster: the stage's for this one, else enemies.json "party" (up to two ids; docs/gameplay/13 §4.6, 19 §2.0).
std::vector<EnemyInst> Game::f_PartyOf(const EnemyInst& l_oLead) {
    std::vector<EnemyInst> l_vParty;
    if (!l_oLead.m_vStageParty.empty()) {
        for (const std::string& l_strId : l_oLead.m_vStageParty) {
            EnemyInst l_oM;
            if (f_EnemyFromData(l_strId, nlohmann::json::object(), l_oM)) l_vParty.push_back(l_oM);
            if ((int)l_vParty.size() + 1 >= toms::g_iMaxFoes) break;
        }
        return l_vParty;
    }
    auto l_itT = m_mapEnemyTpl.find(l_oLead.m_strId);
    if (l_itT == m_mapEnemyTpl.end() || !l_itT->second.contains("party") || !l_itT->second["party"].is_array()) return l_vParty;
    for (const auto& l_jId : l_itT->second["party"]) {
        EnemyInst l_oM;
        if (l_jId.is_string() && f_EnemyFromData(l_jId.get<std::string>(), nlohmann::json::object(), l_oM)) l_vParty.push_back(l_oM);
        if ((int)l_vParty.size() + 1 >= toms::g_iMaxFoes) break;
    }
    return l_vParty;
}

// Verification hook (see game.h; no host calls it at the moment): start a fight with the nearest un-consumed monster so the battle scene and
// its input can be driven without walking the maze first. Returns true when a fight is running.
bool Game::f_DebugStartNearestBattle() {
    if (m_oCs.m_bActive) return true;
    int l_iBestD = 1 << 30;
    const Entity* l_pBest = nullptr;
    for (const auto& l_oE : m_oSt.m_vEntities) {
        if (l_oE.m_bConsumed || l_oE.m_strKind.rfind("monster:", 0) != 0) continue;
        int l_iD = std::abs(l_oE.m_iX - m_oPl.m_iX) + std::abs(l_oE.m_iY - m_oPl.m_iY);
        if (l_iD < l_iBestD) { l_iBestD = l_iD; l_pBest = &l_oE; }
    }
    if (!l_pBest) return false;
    f_EngageMonster(*l_pBest);
    return m_oCs.m_bActive;
}

bool Game::f_DebugEquip(const std::string& l_strEquipmentId) {
    if (m_mapEquipmentDefs.find(l_strEquipmentId) == m_mapEquipmentDefs.end()) return false;
    f_OwnGear(l_strEquipmentId, true);
    return true;
}

void Game::f_DebugForceLose() {
    for (CombatState::Foe& l_oF : m_oCs.m_vFoes) l_oF.m_oEnemy.m_bBoss = false;   // a non-boss death is what deathsNonBoss (e_13) actually counts
    f_FinishCombatLose();
}
