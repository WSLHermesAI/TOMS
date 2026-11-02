// state_machine_test.cpp — headless verification of the Game State Machine (Milestone 1).
// Exits 0 on success, 1 on any CHECK failure. Run: ./state_machine_test
#include "game_state.h"
#include <cstdio>
#include <queue>
#include <set>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

int main() {
    // 1. No dead ends: every real state has at least one outgoing transition.
    bool l_bNoDeadEnds = true;
    for (int l_iI = 0; l_iI < (int)GameState::Count; l_iI++) {
        GameState l_eS = (GameState)l_iI;
        bool l_bHasOut = false;
        for (auto& l_pairE : f_TransitionTable()) if (l_pairE.first == l_eS) { l_bHasOut = true; break; }
        if (!l_bHasOut) { printf("  (dead end: %s has no outgoing transition)\n", f_ToString(l_eS)); l_bNoDeadEnds = false; }
    }
    CHECK(l_bNoDeadEnds, "every state has >=1 outgoing transition (no dead ends)");

    // 2. No unreachable states: BFS from Boot must visit every state.
    std::set<GameState> l_setVisited;
    std::queue<GameState> l_oQ;
    l_oQ.push(GameState::Boot);
    l_setVisited.insert(GameState::Boot);
    while (!l_oQ.empty()) {
        GameState l_eCur = l_oQ.front(); l_oQ.pop();
        for (auto& l_pairE : f_TransitionTable())
            if (l_pairE.first == l_eCur && !l_setVisited.count(l_pairE.second)) { l_setVisited.insert(l_pairE.second); l_oQ.push(l_pairE.second); }
    }
    bool l_bAllReachable = true;
    for (int l_iI = 0; l_iI < (int)GameState::Count; l_iI++) {
        GameState l_eS = (GameState)l_iI;
        if (!l_setVisited.count(l_eS)) { printf("  (unreachable from Boot: %s)\n", f_ToString(l_eS)); l_bAllReachable = false; }
    }
    CHECK(l_bAllReachable, "every state is reachable from Boot");

    // 3. GameStateMachine follows the legal path and rejects illegal jumps.
    GameStateMachine l_oGsm;
    CHECK(l_oGsm.f_Current() == GameState::Boot, "initial state is Boot");
    CHECK(l_oGsm.f_TryTransition(GameState::MainMenu), "Boot -> MainMenu is legal");

    CHECK(!l_oGsm.f_TryTransition(GameState::DirectBattle), "MainMenu -> DirectBattle is illegal, rejected");
    CHECK(l_oGsm.f_Current() == GameState::MainMenu, "state unchanged after a rejected transition");

    CHECK(l_oGsm.f_TryTransition(GameState::StageSelect), "MainMenu -> StageSelect is legal");
    CHECK(l_oGsm.f_TryTransition(GameState::StageLoading), "StageSelect -> StageLoading is legal");
    CHECK(l_oGsm.f_TryTransition(GameState::Explore), "StageLoading -> Explore is legal");
    CHECK(l_oGsm.f_Current() == GameState::Explore, "current state is now Explore");

    // 4. History grows only on accepted transitions.
    size_t l_nHistBefore = l_oGsm.f_History().size();
    CHECK(!l_oGsm.f_TryTransition(GameState::Ending), "Explore -> Ending is illegal, rejected");
    CHECK(l_oGsm.f_History().size() == l_nHistBefore, "history unchanged after a rejected transition");
    CHECK(l_oGsm.f_TryTransition(GameState::EncounterResolve), "Explore -> EncounterResolve is legal");
    CHECK(l_oGsm.f_History().size() == l_nHistBefore + 1, "history grows by 1 after an accepted transition");

    // 5. A full lap: MainMenu -> ... -> StageComplete -> Ending -> Credits -> MainMenu.
    GameStateMachine l_oLap;
    bool l_bLapOk = l_oLap.f_TryTransition(GameState::MainMenu)
              && l_oLap.f_TryTransition(GameState::StageSelect)
              && l_oLap.f_TryTransition(GameState::StageLoading)
              && l_oLap.f_TryTransition(GameState::Explore)
              && l_oLap.f_TryTransition(GameState::StageComplete)
              && l_oLap.f_TryTransition(GameState::Ending)
              && l_oLap.f_TryTransition(GameState::Credits)
              && l_oLap.f_TryTransition(GameState::MainMenu);
    CHECK(l_bLapOk, "full Boot->...->Ending->Credits->MainMenu lap completes");
    CHECK(l_oLap.f_Current() == GameState::MainMenu, "lap ends back at MainMenu");

    if (g_iFail == 0) { printf("state_machine_test: ALL PASS (7 checks)\n"); return 0; }
    printf("state_machine_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}
