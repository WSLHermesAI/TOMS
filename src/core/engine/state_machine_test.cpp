// state_machine_test.cpp — headless verification of the Game State Machine (Milestone 1).
// Exits 0 on success, 1 on any CHECK failure. Run: ./state_machine_test
#include "game_state.h"
#include <cstdio>
#include <queue>
#include <set>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

int main() {
    // 1. No dead ends: every real state has at least one outgoing transition.
    bool noDeadEnds = true;
    for (int i = 0; i < (int)GameState::Count; i++) {
        GameState s = (GameState)i;
        bool hasOut = false;
        for (auto& e : transitionTable()) if (e.first == s) { hasOut = true; break; }
        if (!hasOut) { printf("  (dead end: %s has no outgoing transition)\n", toString(s)); noDeadEnds = false; }
    }
    CHECK(noDeadEnds, "every state has >=1 outgoing transition (no dead ends)");

    // 2. No unreachable states: BFS from Boot must visit every state.
    std::set<GameState> visited;
    std::queue<GameState> q;
    q.push(GameState::Boot);
    visited.insert(GameState::Boot);
    while (!q.empty()) {
        GameState cur = q.front(); q.pop();
        for (auto& e : transitionTable())
            if (e.first == cur && !visited.count(e.second)) { visited.insert(e.second); q.push(e.second); }
    }
    bool allReachable = true;
    for (int i = 0; i < (int)GameState::Count; i++) {
        GameState s = (GameState)i;
        if (!visited.count(s)) { printf("  (unreachable from Boot: %s)\n", toString(s)); allReachable = false; }
    }
    CHECK(allReachable, "every state is reachable from Boot");

    // 3. GameStateMachine follows the legal path and rejects illegal jumps.
    GameStateMachine gsm;
    CHECK(gsm.current() == GameState::Boot, "initial state is Boot");
    CHECK(gsm.tryTransition(GameState::MainMenu), "Boot -> MainMenu is legal");

    CHECK(!gsm.tryTransition(GameState::DirectBattle), "MainMenu -> DirectBattle is illegal, rejected");
    CHECK(gsm.current() == GameState::MainMenu, "state unchanged after a rejected transition");

    CHECK(gsm.tryTransition(GameState::StageSelect), "MainMenu -> StageSelect is legal");
    CHECK(gsm.tryTransition(GameState::StageLoading), "StageSelect -> StageLoading is legal");
    CHECK(gsm.tryTransition(GameState::Explore), "StageLoading -> Explore is legal");
    CHECK(gsm.current() == GameState::Explore, "current state is now Explore");

    // 4. History grows only on accepted transitions.
    size_t histBefore = gsm.history().size();
    CHECK(!gsm.tryTransition(GameState::Ending), "Explore -> Ending is illegal, rejected");
    CHECK(gsm.history().size() == histBefore, "history unchanged after a rejected transition");
    CHECK(gsm.tryTransition(GameState::EncounterResolve), "Explore -> EncounterResolve is legal");
    CHECK(gsm.history().size() == histBefore + 1, "history grows by 1 after an accepted transition");

    // 5. A full lap: MainMenu -> ... -> StageComplete -> Ending -> Credits -> MainMenu.
    GameStateMachine lap;
    bool lapOk = lap.tryTransition(GameState::MainMenu)
              && lap.tryTransition(GameState::StageSelect)
              && lap.tryTransition(GameState::StageLoading)
              && lap.tryTransition(GameState::Explore)
              && lap.tryTransition(GameState::StageComplete)
              && lap.tryTransition(GameState::Ending)
              && lap.tryTransition(GameState::Credits)
              && lap.tryTransition(GameState::MainMenu);
    CHECK(lapOk, "full Boot->...->Ending->Credits->MainMenu lap completes");
    CHECK(lap.current() == GameState::MainMenu, "lap ends back at MainMenu");

    if (g_fail == 0) { printf("state_machine_test: ALL PASS (7 checks)\n"); return 0; }
    printf("state_machine_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}
